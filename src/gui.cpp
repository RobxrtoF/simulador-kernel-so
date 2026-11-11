/**
 * @file gui.cpp
 * @brief Interfaz gráfica en tiempo real (bonus) dibujada con raylib.
 *
 * Organización del archivo:
 *   1. Tema (colores) y utilidades de dibujo.
 *   2. Fuentes: Inter (assets/fonts), cargada al tamaño exacto de pantalla.
 *   3. GuiApp: un método por panel (cabecera, controles, indicadores, tabla
 *      de procesos, CPU, colas y diagrama de Gantt).
 *   4. runGui(): crea la ventana y ejecuta el bucle de dibujo.
 *
 * La ventana nunca toca el estado del simulador: en cada cuadro pide una copia
 * con Kernel::snapshot() y dibuja esa copia. Los botones llaman a los mismos
 * métodos del Kernel que usa la CLI, así que todo pasa por el mutex.
 * Los cálculos que no son dibujo están en gui_model.cpp (con pruebas).
 */
#include "gui.hpp"

#ifdef SIM_WITH_GUI

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gui_model.hpp"
#include "kernel.hpp"

#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#else
#include <sys/stat.h>

#include <cstring>
#endif

namespace sim {

namespace {

// ============================================================================
// 0. macOS: la ventana oculta no debe frenar al simulador
// ============================================================================

#if defined(__APPLE__)
/**
 * @brief Declara ante macOS que el proceso hace trabajo en tiempo real.
 *
 * macOS trata como inactiva a una aplicación gráfica cuya ventana queda
 * tapada o minimizada (por ejemplo, detrás de la terminal donde se escriben
 * los comandos): deja de entregarle el refresco de pantalla, de modo que
 * EndDrawing() se bloquea varios segundos, y puede aplazar sus temporizadores
 * (App Nap). Medido con la ventana minimizada: 3 cuadros en 16 s, frente a
 * 900 con esta llamada. Sin ella, `exit` tardaría segundos en cerrar la
 * ventana y el reloj del Hilo Motor podría atrasarse.
 *
 * La actividad se declara con NSProcessInfo a través de la API en C del
 * runtime de Objective-C (el proyecto sigue siendo solo C++) y dura hasta que
 * termina el proceso.
 */
void disableAppNap() {
    static bool done = false;
    if (done) return;
    done = true;

    const Class processInfoClass = objc_getClass("NSProcessInfo");
    const Class stringClass = objc_getClass("NSString");
    if (processInfoClass == nullptr || stringClass == nullptr) return;

    using ClassMessage = id (*)(Class, SEL);
    using StringMessage = id (*)(Class, SEL, const char*);
    using ActivityMessage = id (*)(id, SEL, unsigned long long, id);
    using ObjectMessage = id (*)(id, SEL);

    const id processInfo =
        reinterpret_cast<ClassMessage>(objc_msgSend)(processInfoClass, sel_registerName("processInfo"));
    const id reason = reinterpret_cast<StringMessage>(objc_msgSend)(
        stringClass, sel_registerName("stringWithUTF8String:"), "Simulacion de planificacion en tiempo real");
    if (processInfo == nullptr || reason == nullptr) return;

    // NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
    const unsigned long long options = 0x00EFFFFFULL | 0xFF00000000ULL;
    const id activity = reinterpret_cast<ActivityMessage>(objc_msgSend)(
        processInfo, sel_registerName("beginActivityWithOptions:reason:"), options, reason);
    if (activity != nullptr) reinterpret_cast<ObjectMessage>(objc_msgSend)(activity, sel_registerName("retain"));
}
#else
void disableAppNap() {}
#endif

// ============================================================================
// 0b. Linux: comprobar que hay servidor gráfico antes de crear la ventana
// ============================================================================

#if defined(__APPLE__)
bool displayAvailable() { return true; }
#else
/**
 * @brief true si hay un servidor gráfico al que conectarse.
 *
 * raylib 5.x no comprueba si pudo crear la ventana: sin servidor gráfico
 * (sesión por SSH, WSL sin WSLg, un servidor de CI) sigue adelante, llama a
 * OpenGL sin contexto y el proceso termina con una violación de segmento. La
 * shell no debe caerse por pedir la ventana, así que se comprueba antes:
 *   - Wayland: la variable WAYLAND_DISPLAY.
 *   - X11 local (DISPLAY=":N"): que exista el socket /tmp/.X11-unix/XN.
 *   - X11 remoto (DISPLAY="host:N", por ejemplo con ssh -X): se intenta.
 * Si la comprobación se equivoca en un entorno poco común, SIM_GUI_FORCE=1 la omite.
 */
bool displayAvailable() {
    if (std::getenv("SIM_GUI_FORCE") != nullptr) return true;
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    if (wayland != nullptr && *wayland != '\0') return true;
    const char* display = std::getenv("DISPLAY");
    if (display == nullptr || *display == '\0') return false;
    if (display[0] != ':') return true;
    const std::string number(display + 1, std::strcspn(display + 1, "."));
    struct stat info {};
    return stat(("/tmp/.X11-unix/X" + number).c_str(), &info) == 0;
}
#endif

// ============================================================================
// 1. Tema y utilidades de dibujo
// ============================================================================

constexpr Color kBg{9, 13, 24, 255};          // fondo de la ventana
constexpr Color kSurface{16, 23, 40, 255};    // paneles
constexpr Color kSurfaceHi{24, 33, 56, 255};  // filas resaltadas, fichas
constexpr Color kBorder{34, 45, 72, 255};
constexpr Color kTrack{30, 40, 66, 255};      // fondo de barras de progreso
constexpr Color kText{230, 236, 246, 255};
constexpr Color kMuted{144, 158, 184, 255};
constexpr Color kFaint{98, 112, 142, 255};
constexpr Color kInk{8, 12, 22, 255};         // texto sobre colores claros
constexpr Color kAccent{88, 144, 255, 255};
constexpr Color kGreen{52, 211, 153, 255};
constexpr Color kAmber{251, 191, 36, 255};
constexpr Color kRed{248, 113, 113, 255};
constexpr Color kSlate{124, 138, 166, 255};
constexpr Color kIdle{27, 36, 58, 255};       // tramos ociosos del Gantt
constexpr Color kHollow{48, 62, 98, 255};     // bloques vacíos (quantum sin usar)

/// Un color por proceso (se repite cada 10 PID). Tonos claros: llevan texto oscuro encima.
constexpr Color kProcessPalette[] = {
    {96, 165, 250, 255},  {52, 211, 153, 255}, {251, 191, 36, 255},  {251, 113, 133, 255},
    {167, 139, 250, 255}, {34, 211, 238, 255}, {251, 146, 60, 255},  {163, 230, 53, 255},
    {244, 114, 182, 255}, {45, 212, 191, 255},
};
constexpr std::size_t kPaletteSize = sizeof(kProcessPalette) / sizeof(kProcessPalette[0]);

Color processColor(int pid) { return kProcessPalette[gui::paletteIndex(pid, kPaletteSize)]; }

Color withAlpha(Color color, float alpha) {
    color.a = static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    return color;
}

Color stateColor(ProcessState state) {
    switch (state) {
        case ProcessState::New:        return kAccent;
        case ProcessState::Ready:      return kAmber;
        case ProcessState::Running:    return kGreen;
        case ProcessState::Blocked:    return kRed;
        case ProcessState::Terminated: return kSlate;
    }
    return kSlate;
}

/// Rectángulo con esquinas redondeadas de `radius` pixeles.
void roundRect(Rectangle rect, float radius, Color color) {
    if (rect.width <= 0.0f || rect.height <= 0.0f) return;
    const float shortSide = std::min(rect.width, rect.height);
    const float roundness = std::min(1.0f, 2.0f * radius / shortSide);
    DrawRectangleRounded(rect, roundness, 12, color);
}

/// Panel: superficie con borde de 1 px.
void panel(Rectangle rect) {
    roundRect(rect, 14.0f, kBorder);
    roundRect({rect.x + 1.0f, rect.y + 1.0f, rect.width - 2.0f, rect.height - 2.0f}, 13.0f, kSurface);
}

bool contains(Rectangle rect, Vector2 point) { return CheckCollisionPointRec(point, rect); }

/// Rectángulo reducido `margin` pixeles por cada lado.
Rectangle inset(Rectangle rect, float margin) {
    return {rect.x + margin, rect.y + margin, rect.width - 2.0f * margin, rect.height - 2.0f * margin};
}

/// Parte común de dos rectángulos (vacía si no se tocan).
Rectangle intersect(Rectangle a, Rectangle b) {
    const float left = std::max(a.x, b.x);
    const float top = std::max(a.y, b.y);
    const float right = std::min(a.x + a.width, b.x + b.width);
    const float bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
}

/// Limita el dibujo a `rect` hasta EndScissorMode().
void clip(Rectangle rect) {
    BeginScissorMode(static_cast<int>(rect.x), static_cast<int>(rect.y), static_cast<int>(rect.width),
                     static_cast<int>(rect.height));
}

// ============================================================================
// 2. Fuentes
// ============================================================================

enum class Weight { Regular = 0, SemiBold = 1 };

/**
 * @brief Fuentes de la interfaz, cargadas bajo demanda para cada tamaño.
 *
 * Cada tamaño se genera a la resolución real de la pantalla (tamaño x escala
 * HiDPI), de modo que el texto se ve nítido también en pantallas Retina. En
 * pantallas de densidad normal se genera al doble y se reduce al dibujar:
 * raylib guarda el avance de cada letra en pixeles enteros, y con letras de
 * 12 px ese redondeo deja el texto apretado; al doble de resolución el error
 * se reduce a la mitad. Si no se encuentran los archivos de Inter se usa la
 * fuente integrada de raylib.
 */
class Fonts {
public:
    Fonts() {
        for (int c = 32; c <= 126; ++c) codepoints_.push_back(c);    // ASCII
        for (int c = 160; c <= 255; ++c) codepoints_.push_back(c);   // Latin-1: á é í ó ú ñ ¿ ¡ ·
        // Guiones, puntos suspensivos, flechas y signo menos: – — … ← → −
        for (int c : {0x2013, 0x2014, 0x2026, 0x2190, 0x2192, 0x2212}) codepoints_.push_back(c);

        const std::string appDir = GetApplicationDirectory();
        for (const std::string& dir : {std::string("assets/fonts/"), appDir + "assets/fonts/",
                                       appDir + "../assets/fonts/"}) {
            if (FileExists((dir + "Inter-Regular.ttf").c_str()) && FileExists((dir + "Inter-SemiBold.ttf").c_str())) {
                paths_[0] = dir + "Inter-Regular.ttf";
                paths_[1] = dir + "Inter-SemiBold.ttf";
                break;
            }
        }
    }
    ~Fonts() { unloadAll(); }
    Fonts(const Fonts&) = delete;
    Fonts& operator=(const Fonts&) = delete;

    /// Cambia la escala HiDPI: las fuentes se vuelven a generar a la nueva resolución.
    void setScale(float scale) {
        if (std::fabs(scale - scale_) < 0.01f) return;
        unloadAll();
        scale_ = scale;
    }

    struct Entry {
        Font font;
        bool owned;  ///< false si es la fuente integrada de raylib (no se libera).
    };

    const Entry& get(Weight weight, int size) {
        const float oversample = scale_ < 1.5f ? 2.0f : 1.0f;  // pantallas 1x: ver el comentario de la clase
        const int pixels = std::max(8, static_cast<int>(std::lround(static_cast<float>(size) * scale_ * oversample)));
        const std::pair<int, int> key{static_cast<int>(weight), pixels};
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;

        Entry entry{GetFontDefault(), false};
        const std::string& path = paths_[static_cast<int>(weight)];
        if (!path.empty()) {
            const Font loaded = LoadFontEx(path.c_str(), pixels, codepoints_.data(),
                                           static_cast<int>(codepoints_.size()));
            if (loaded.texture.id != 0 && loaded.glyphCount > 0) {
                SetTextureFilter(loaded.texture, TEXTURE_FILTER_BILINEAR);
                entry = Entry{loaded, true};
            }
        }
        return cache_.emplace(key, entry).first->second;
    }

private:
    void unloadAll() {
        for (auto& item : cache_) {
            if (item.second.owned) UnloadFont(item.second.font);
        }
        cache_.clear();
    }

    std::map<std::pair<int, int>, Entry> cache_;
    std::string paths_[2];
    std::vector<int> codepoints_;
    float scale_ = 1.0f;
};

// ============================================================================
// 3. Aplicación
// ============================================================================

class GuiApp {
public:
    explicit GuiApp(Kernel& kernel) : kernel_(kernel) {
        if (const char* path = std::getenv("SIM_GUI_SCREENSHOT")) screenshotPath_ = path;
        if (const char* delay = std::getenv("SIM_GUI_SCREENSHOT_DELAY")) screenshotDelay_ = std::atof(delay);
        if (const char* close = std::getenv("SIM_GUI_AUTOCLOSE")) autoClose_ = std::string(close) == "1";
        snap_ = kernel_.snapshot();
        visualClock_ = static_cast<double>(snap_.clock);  // al abrir no se anima el historial previo
    }

    /// Un cuadro completo: lee el estado, atiende la entrada y dibuja.
    void frame() {
        fonts_.setScale(GetWindowScaleDPI().x);
        dt_ = GetFrameTime();
        mouse_ = GetMousePosition();
        pressed_ = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        down_ = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
        wheel_ = GetMouseWheelMove();
        wantHand_ = false;
        tooltip_.clear();

        snap_ = kernel_.snapshot();  // copia consistente, tomada bajo el mutex del Kernel
        visualClock_ = gui::advanceVisualClock(visualClock_, snap_.clock, snap_.engineRunning, snap_.msPerTick, dt_);
        for (const PCB& pcb : snap_.processes) {
            auto it = shownCompletion_.find(pcb.pid);
            const double target = gui::completion(pcb);
            if (it == shownCompletion_.end()) shownCompletion_[pcb.pid] = target;
            else it->second = gui::smoothTowards(it->second, target, dt_);
        }
        shownCpuUsage_ = gui::smoothTowards(shownCpuUsage_, snap_.stats.cpuUsage(), dt_);
        handleKeyboard();

        BeginDrawing();
        ClearBackground(kBg);
        layoutAndDraw();
        drawTooltip();
        SetMouseCursor(wantHand_ ? MOUSE_CURSOR_POINTING_HAND : MOUSE_CURSOR_DEFAULT);
        captureScreenshotIfRequested();
        EndDrawing();
    }

    bool wantsClose() const { return closeRequested_; }

private:
    // ------------------------------------------------------------------ texto

    float textWidth(const std::string& value, int size, Weight weight = Weight::Regular) {
        const Fonts::Entry& entry = fonts_.get(weight, size);
        const float spacing = entry.owned ? 0.0f : static_cast<float>(size) / 10.0f;
        return MeasureTextEx(entry.font, value.c_str(), static_cast<float>(size), spacing).x;
    }

    void text(const std::string& value, float x, float y, int size, Color color, Weight weight = Weight::Regular) {
        const Fonts::Entry& entry = fonts_.get(weight, size);
        const float spacing = entry.owned ? 0.0f : static_cast<float>(size) / 10.0f;
        DrawTextEx(entry.font, value.c_str(), {std::round(x), std::round(y)}, static_cast<float>(size), spacing, color);
    }

    void textRight(const std::string& value, float right, float y, int size, Color color,
                   Weight weight = Weight::Regular) {
        text(value, right - textWidth(value, size, weight), y, size, color, weight);
    }

    void textCentered(const std::string& value, float centerX, float y, int size, Color color,
                      Weight weight = Weight::Regular) {
        text(value, centerX - textWidth(value, size, weight) / 2.0f, y, size, color, weight);
    }

    /// Texto recortado con "…" para que quepa en `maxWidth`.
    std::string fit(const std::string& value, float maxWidth, int size, Weight weight = Weight::Regular) {
        return gui::ellipsize(value, maxWidth,
                              [&](const std::string& s) { return static_cast<double>(textWidth(s, size, weight)); });
    }

    // ---------------------------------------------------------------- controles

    bool hover(Rectangle rect) {
        const bool inside = contains(rect, mouse_);
        if (inside) wantHand_ = true;
        return inside;
    }

    /// Píldora informativa alineada a la derecha. Devuelve el borde izquierdo.
    float pillRight(float right, float centerY, const std::string& label, Color color, bool dot, bool pulse) {
        const int size = 13;
        const float width = textWidth(label, size, Weight::SemiBold) + (dot ? 40.0f : 26.0f);
        const Rectangle rect{right - width, centerY - 14.0f, width, 28.0f};
        roundRect(rect, 14.0f, withAlpha(color, 0.32f));
        roundRect(inset(rect, 1.0f), 13.0f, kBg);
        roundRect(inset(rect, 1.0f), 13.0f, withAlpha(color, 0.12f));
        float textX = rect.x + 13.0f;
        if (dot) {
            const Vector2 center{rect.x + 16.0f, centerY};
            if (pulse) {
                const float phase = static_cast<float>(std::fmod(GetTime(), 1.4) / 1.4);
                DrawCircleV(center, 4.0f + 6.0f * phase, withAlpha(color, 0.35f * (1.0f - phase)));
            }
            DrawCircleV(center, 4.0f, color);
            textX = rect.x + 27.0f;
        }
        text(label, textX, centerY - 8.0f, size, color, Weight::SemiBold);
        return rect.x;
    }

    /// Botón principal (iniciar / pausar) con icono dibujado.
    bool primaryButton(Rectangle rect, const std::string& label, Color color, bool pauseIcon) {
        const bool over = hover(rect);
        roundRect(rect, 10.0f, withAlpha(color, over ? 1.0f : 0.9f));
        const float labelWidth = textWidth(label, 14, Weight::SemiBold);
        const float startX = rect.x + (rect.width - (labelWidth + 22.0f)) / 2.0f;
        const float centerY = rect.y + rect.height / 2.0f;
        if (pauseIcon) {
            DrawRectangleRec({startX, centerY - 6.0f, 4.0f, 12.0f}, kInk);
            DrawRectangleRec({startX + 7.0f, centerY - 6.0f, 4.0f, 12.0f}, kInk);
        } else {
            DrawTriangle({startX, centerY - 7.0f}, {startX, centerY + 7.0f}, {startX + 11.0f, centerY}, kInk);
        }
        text(label, startX + 22.0f, centerY - 8.5f, 14, kInk, Weight::SemiBold);
        return over && pressed_;
    }

    /// Botón secundario pequeño.
    bool ghostButton(Rectangle rect, const std::string& label) {
        const bool over = hover(rect);
        roundRect(rect, 8.0f, kBorder);
        roundRect(inset(rect, 1.0f), 7.0f,
                  over ? kSurfaceHi : kSurface);
        textCentered(label, rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f - 7.5f, 12, kText,
                     Weight::SemiBold);
        return over && pressed_;
    }

    /// Selector de dos opciones. Devuelve el índice pulsado o -1.
    int segmented(Rectangle rect, const char* first, const char* second, int active) {
        roundRect(rect, 9.0f, kTrack);
        int clicked = -1;
        const float half = rect.width / 2.0f;
        const char* labels[2] = {first, second};
        for (int i = 0; i < 2; ++i) {
            const Rectangle cell{rect.x + half * static_cast<float>(i) + 3.0f, rect.y + 3.0f, half - 6.0f,
                                 rect.height - 6.0f};
            const bool over = hover(cell);
            if (i == active) roundRect(cell, 7.0f, kAccent);
            else if (over) roundRect(cell, 7.0f, kSurfaceHi);
            textCentered(labels[i], cell.x + cell.width / 2.0f, cell.y + cell.height / 2.0f - 8.0f, 13,
                         i == active ? kInk : kMuted, Weight::SemiBold);
            if (over && pressed_ && i != active) clicked = i;
        }
        return clicked;
    }

    /// Contador con botones − y +. Devuelve -1, 0 o +1.
    int stepper(Rectangle rect, const std::string& value, bool enabled) {
        roundRect(rect, 9.0f, kTrack);
        int delta = 0;
        const float side = rect.height;
        const Rectangle minus{rect.x, rect.y, side, side};
        const Rectangle plus{rect.x + rect.width - side, rect.y, side, side};
        const Color sign = enabled ? kText : kFaint;
        for (int i = 0; i < 2; ++i) {
            const Rectangle cell = i == 0 ? minus : plus;
            const bool over = enabled && hover(cell);
            if (over) roundRect(inset(cell, 3.0f), 7.0f, kSurfaceHi);
            const float cx = cell.x + cell.width / 2.0f;
            const float cy = cell.y + cell.height / 2.0f;
            DrawRectangleRec({cx - 5.0f, cy - 1.0f, 10.0f, 2.0f}, sign);
            if (i == 1) DrawRectangleRec({cx - 1.0f, cy - 5.0f, 2.0f, 10.0f}, sign);
            if (over && pressed_) delta = i == 0 ? -1 : 1;
        }
        textCentered(value, rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f - 8.5f, 14,
                     enabled ? kText : kFaint, Weight::SemiBold);
        return delta;
    }

    /// Barra de progreso horizontal.
    void progressBar(Rectangle rect, double fraction, Color color) {
        roundRect(rect, rect.height / 2.0f, kTrack);
        const float width = rect.width * static_cast<float>(std::clamp(fraction, 0.0, 1.0));
        if (width < 1.0f) return;
        roundRect({rect.x, rect.y, std::max(width, rect.height), rect.height}, rect.height / 2.0f, color);
    }

    // ------------------------------------------------------------------ teclado

    void handleKeyboard() {
        if (IsKeyPressed(KEY_SPACE)) toggleEngine();
        if (IsKeyPressed(KEY_R)) kernel_.setPolicy(PolicyKind::RoundRobin, snap_.quantum);
        if (IsKeyPressed(KEY_S)) kernel_.setPolicy(PolicyKind::SRTF, snap_.quantum);
        if (IsKeyPressed(KEY_LEFT)) ganttPan_ += 2.0;
        if (IsKeyPressed(KEY_RIGHT)) ganttPan_ = std::max(0.0, ganttPan_ - 2.0);
        if (IsKeyPressed(KEY_END)) ganttPan_ = 0.0;
    }

    void toggleEngine() {
        if (snap_.engineRunning) kernel_.pause();
        else kernel_.start();
    }

    const PCB* findProcess(int pid) const {
        for (const PCB& pcb : snap_.processes) {
            if (pcb.pid == pid) return &pcb;
        }
        return nullptr;
    }

    bool hasPendingWork() const {
        return std::any_of(snap_.processes.begin(), snap_.processes.end(),
                           [](const PCB& pcb) { return !pcb.isFinished(); });
    }

    // ------------------------------------------------------------------- diseño

    void layoutAndDraw() {
        const float width = static_cast<float>(GetScreenWidth());
        const float height = static_cast<float>(GetScreenHeight());
        const float pad = 20.0f;
        const float gap = 12.0f;
        const float inner = width - 2.0f * pad;

        drawHeader(width);
        float y = 64.0f;
        drawToolbar({pad, y, inner, 56.0f});
        y += 56.0f + gap;
        drawIndicators({pad, y, inner, 88.0f}, gap);
        y += 88.0f + gap;

        const float footerHeight = 30.0f;
        const float ganttHeight = 196.0f;
        const float mainHeight = std::max(220.0f, height - y - gap - ganttHeight - footerHeight);
        const float leftWidth = std::floor((inner - gap) * 0.64f);
        const float rightX = pad + leftWidth + gap;
        const float rightWidth = inner - gap - leftWidth;
        const float cpuHeight = 168.0f;
        drawProcessTable({pad, y, leftWidth, mainHeight});
        drawCpu({rightX, y, rightWidth, cpuHeight});
        drawQueues({rightX, y + cpuHeight + gap, rightWidth, mainHeight - cpuHeight - gap});
        y += mainHeight + gap;
        drawGantt({pad, y, inner, ganttHeight});
        drawFooter(pad, y + ganttHeight + 9.0f, inner);
    }

    // ------------------------------------------------------------------ cabecera

    void drawHeader(float width) {
        const Rectangle logo{20.0f, 12.0f, 36.0f, 36.0f};
        roundRect(logo, 10.0f, kAccent);  // tres barras: un Gantt en miniatura
        roundRect({logo.x + 8.0f, logo.y + 9.0f, 20.0f, 4.0f}, 2.0f, WHITE);
        roundRect({logo.x + 8.0f, logo.y + 16.0f, 12.0f, 4.0f}, 2.0f, withAlpha(WHITE, 0.85f));
        roundRect({logo.x + 15.0f, logo.y + 23.0f, 13.0f, 4.0f}, 2.0f, withAlpha(WHITE, 0.7f));

        text("Simulador de Kernel", 68.0f, 10.0f, 19, kText, Weight::SemiBold);
        text("Shell & Scheduling Engine  ·  Sistemas Operativos, Fase I", 68.0f, 35.0f, 13, kMuted);

        float right = width - 20.0f;
        const float centerY = 30.0f;
        if (snap_.engineRunning && hasPendingWork()) {
            right = pillRight(right, centerY, "Motor en ejecución", kGreen, true, true);
        } else if (snap_.engineRunning) {
            right = pillRight(right, centerY, "Motor esperando procesos", kAccent, true, true);
        } else {
            right = pillRight(right, centerY, "Motor en pausa", kAmber, true, false);
        }
        right = pillRight(right - 8.0f, centerY, "t = " + std::to_string(snap_.clock), kText, false, false);
        const float available = right - 8.0f - 480.0f;  // no encimar el título
        if (available > 80.0f) {
            pillRight(right - 8.0f, centerY, fit(snap_.policy, available - 30.0f, 13, Weight::SemiBold), kMuted, false,
                      false);
        }
        DrawRectangleRec({0.0f, 59.0f, width, 1.0f}, withAlpha(kBorder, 0.7f));
    }

    // ------------------------------------------------------------- barra de controles

    void drawToolbar(Rectangle rect) {
        panel(rect);
        const float centerY = rect.y + rect.height / 2.0f;
        float x = rect.x + 10.0f;

        const bool resume = snap_.clock > 0;
        const Rectangle engineButton{x, centerY - 18.0f, 136.0f, 36.0f};
        const bool clicked =
            snap_.engineRunning ? primaryButton(engineButton, "Pausar", kAmber, true)
                                : primaryButton(engineButton, resume ? "Reanudar" : "Iniciar", kGreen, false);
        if (clicked) toggleEngine();
        x += engineButton.width + 16.0f;
        DrawRectangleRec({x, rect.y + 14.0f, 1.0f, rect.height - 28.0f}, kBorder);
        x += 16.0f;

        const bool roundRobin = snap_.policyKind == PolicyKind::RoundRobin;
        auto label = [&](const std::string& caption) {
            text(caption, x, centerY - 8.0f, 13, kMuted);
            x += textWidth(caption, 13) + 10.0f;
        };

        label("Política");
        const int chosen = segmented({x, centerY - 17.0f, 128.0f, 34.0f}, "RR", "SRTF", roundRobin ? 0 : 1);
        if (chosen == 0) kernel_.setPolicy(PolicyKind::RoundRobin, snap_.quantum);
        if (chosen == 1) kernel_.setPolicy(PolicyKind::SRTF, snap_.quantum);
        x += 128.0f + 22.0f;

        label("Quantum");
        const int quantumDelta =
            stepper({x, centerY - 17.0f, 104.0f, 34.0f}, std::to_string(snap_.quantum), roundRobin);
        if (quantumDelta != 0) {
            kernel_.setPolicy(PolicyKind::RoundRobin, std::clamp(snap_.quantum + quantumDelta, 1, 99));
        }
        x += 104.0f + 22.0f;

        label("Envejecimiento");
        const std::string agingText = snap_.agingInterval == kAgingOff ? "No" : std::to_string(snap_.agingInterval);
        const int agingDelta = stepper({x, centerY - 17.0f, 104.0f, 34.0f}, agingText, !roundRobin);
        if (agingDelta != 0) kernel_.setAging(std::clamp(snap_.agingInterval + agingDelta, 0, 99));
        x += 104.0f + 22.0f;

        label("Velocidad");
        const float valueWidth = 98.0f;
        const float sliderRight = rect.x + rect.width - 16.0f - valueWidth;
        drawSpeedSlider({x + 4.0f, centerY - 12.0f, std::max(90.0f, sliderRight - x - 8.0f), 24.0f});
        textRight(std::to_string(snap_.msPerTick) + " ms/tick", rect.x + rect.width - 16.0f, centerY - 8.0f, 13, kText,
                  Weight::SemiBold);
    }

    /// Deslizador de velocidad: a la derecha, más rápido (menos milisegundos por tick).
    void drawSpeedSlider(Rectangle rect) {
        const float centerY = rect.y + rect.height / 2.0f;
        const Rectangle grab{rect.x - 10.0f, rect.y - 4.0f, rect.width + 20.0f, rect.height + 8.0f};
        const bool over = hover(grab);
        if (over && pressed_) draggingSpeed_ = true;
        if (!down_) draggingSpeed_ = false;

        double position = 1.0 - gui::sliderFromSpeed(snap_.msPerTick);
        if (draggingSpeed_) {
            position = std::clamp(static_cast<double>((mouse_.x - rect.x) / rect.width), 0.0, 1.0);
            const int ms = gui::speedFromSlider(1.0 - position);
            if (ms != snap_.msPerTick) kernel_.setSpeed(ms);
            wantHand_ = true;
        }
        const float knobX = rect.x + rect.width * static_cast<float>(position);
        roundRect({rect.x, centerY - 3.0f, rect.width, 6.0f}, 3.0f, kTrack);
        roundRect({rect.x, centerY - 3.0f, std::max(6.0f, knobX - rect.x), 6.0f}, 3.0f, kAccent);
        DrawCircleV({knobX, centerY}, (over || draggingSpeed_) ? 10.0f : 9.0f, kAccent);
        DrawCircleV({knobX, centerY}, 5.0f, WHITE);
    }

    // --------------------------------------------------------------- indicadores

    void indicator(Rectangle rect, const std::string& caption, const std::string& value, const std::string& unit,
                   const std::string& note) {
        panel(rect);
        text(fit(caption, rect.width - 32.0f, 12), rect.x + 16.0f, rect.y + 12.0f, 12, kMuted);
        text(value, rect.x + 16.0f, rect.y + 30.0f, 26, kText, Weight::SemiBold);
        if (!unit.empty()) {
            text(unit, rect.x + 16.0f + textWidth(value, 26, Weight::SemiBold) + 7.0f, rect.y + 42.0f, 13, kMuted);
        }
        if (!note.empty()) text(fit(note, rect.width - 32.0f, 12), rect.x + 16.0f, rect.y + 64.0f, 12, kFaint);
    }

    void drawIndicators(Rectangle rect, float gap) {
        const int count = 6;
        const float cardWidth = (rect.width - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        auto card = [&](int index) {
            return Rectangle{rect.x + static_cast<float>(index) * (cardWidth + gap), rect.y, cardWidth, rect.height};
        };
        const SystemStats& stats = snap_.stats;
        const bool finished = stats.finishedProcesses > 0;

        indicator(card(0), "Reloj del sistema", std::to_string(snap_.clock), "ticks",
                  std::to_string(snap_.msPerTick) + " ms por tick");

        // Procesos: finalizados / total y distribución por estado.
        const Rectangle processCard = card(1);
        indicator(processCard, "Procesos finalizados", std::to_string(stats.finishedProcesses),
                  "de " + std::to_string(stats.totalProcesses), "");
        const gui::StateCounts counts = gui::countStates(snap_.processes);
        const Rectangle bar{processCard.x + 16.0f, processCard.y + 68.0f, processCard.width - 32.0f, 6.0f};
        roundRect(bar, 3.0f, kTrack);
        if (counts.total() > 0) {
            const std::pair<std::size_t, Color> parts[] = {{counts.terminated, kSlate}, {counts.running, kGreen},
                                                           {counts.ready, kAmber},      {counts.blocked, kRed},
                                                           {counts.created, kAccent}};
            clip(bar);
            float partX = bar.x;
            for (const auto& part : parts) {
                const float partWidth = bar.width * static_cast<float>(part.first) / static_cast<float>(counts.total());
                if (partWidth > 0.0f) DrawRectangleRec({partX, bar.y, partWidth - 1.0f, bar.height}, part.second);
                partX += partWidth;
            }
            EndScissorMode();
        }

        const std::string none = "—";
        const std::string pending = "aún sin procesos finalizados";
        const std::string over = "sobre " + std::to_string(stats.finishedProcesses) + " finalizado" +
                                 (stats.finishedProcesses == 1 ? "" : "s");
        indicator(card(2), "Espera promedio (WT)", finished ? gui::formatAverage(stats.avgWaiting) : none,
                  finished ? "ticks" : "", finished ? over : pending);
        indicator(card(3), "Retorno promedio (TAT)", finished ? gui::formatAverage(stats.avgTurnaround) : none,
                  finished ? "ticks" : "", finished ? over : pending);
        indicator(card(4), "Respuesta promedio (RT)", finished ? gui::formatAverage(stats.avgResponse) : none,
                  finished ? "ticks" : "", finished ? over : pending);

        const Rectangle cpuCard = card(5);
        char usage[16];
        std::snprintf(usage, sizeof usage, "%.1f", stats.cpuUsage());
        indicator(cpuCard, "Uso de CPU", usage, "%", "");
        progressBar({cpuCard.x + 16.0f, cpuCard.y + 68.0f, cpuCard.width - 32.0f, 6.0f}, shownCpuUsage_ / 100.0,
                    kAccent);
    }

    // ---------------------------------------------------------- tabla de procesos

    void badge(float x, float y, const std::string& label) {
        const float width = textWidth(label, 12, Weight::SemiBold) + 16.0f;
        roundRect({x, y, width, 20.0f}, 10.0f, kTrack);
        text(label, x + 8.0f, y + 3.0f, 12, kMuted, Weight::SemiBold);
    }

    void drawProcessTable(Rectangle rect) {
        panel(rect);
        text("Procesos", rect.x + 16.0f, rect.y + 14.0f, 15, kText, Weight::SemiBold);
        badge(rect.x + 16.0f + textWidth("Procesos", 15, Weight::SemiBold) + 10.0f, rect.y + 13.0f,
              std::to_string(snap_.processes.size()));

        // Columnas: anchos fijos salvo el nombre, que usa el espacio restante.
        const float left = rect.x + 16.0f;
        const float inner = rect.width - 32.0f;
        const float pidWidth = 44.0f, stateWidth = 116.0f;
        const float progressWidth = std::clamp(inner * 0.22f, 140.0f, 210.0f);
        // Las cuatro columnas numéricas se estrechan en ventanas pequeñas para dejar sitio al nombre.
        const float numberWidth =
            std::clamp((inner - pidWidth - stateWidth - progressWidth - 110.0f) / 4.0f, 54.0f, 72.0f);
        const float nameWidth = std::max(90.0f, inner - pidWidth - stateWidth - progressWidth - 4.0f * numberWidth);
        const float xPid = left;
        const float xName = xPid + pidWidth;
        const float xState = xName + nameWidth;
        const float xProgress = xState + stateWidth;
        const float xNumbers = xProgress + progressWidth;

        const float headerY = rect.y + 46.0f;
        const Color head = kFaint;
        text("PID", xPid, headerY, 11, head, Weight::SemiBold);
        text("PROCESO", xName, headerY, 11, head, Weight::SemiBold);
        text("ESTADO", xState, headerY, 11, head, Weight::SemiBold);
        text("PROGRESO", xProgress, headerY, 11, head, Weight::SemiBold);
        const char* numberTitles[4] = {"RESTANTE", "PRIORIDAD", "ESPERA", "LLEGADA"};
        for (int i = 0; i < 4; ++i) {
            textCentered(numberTitles[i], xNumbers + numberWidth * (static_cast<float>(i) + 0.5f), headerY, 11, head,
                         Weight::SemiBold);
        }
        DrawRectangleRec({left, rect.y + 66.0f, inner, 1.0f}, kBorder);

        const Rectangle view{rect.x + 6.0f, rect.y + 68.0f, rect.width - 12.0f, rect.height - 68.0f - 10.0f};
        if (snap_.processes.empty()) {
            const float centerX = rect.x + rect.width / 2.0f;
            const float centerY = view.y + view.height / 2.0f;
            textCentered("Todavía no hay procesos", centerX, centerY - 20.0f, 15, kMuted, Weight::SemiBold);
            textCentered("Créelos en la consola con  create <nombre> <burst> [prioridad]  o  load <archivo>", centerX,
                         centerY + 4.0f, 13, kFaint);
            return;
        }

        const float rowHeight = 34.0f;
        const float contentHeight = rowHeight * static_cast<float>(snap_.processes.size());
        const float maxScroll = std::max(0.0f, contentHeight - view.height);
        if (contains(view, mouse_) && wheel_ != 0.0f) tableScroll_ -= wheel_ * rowHeight * 1.5f;
        tableScroll_ = std::clamp(tableScroll_, 0.0f, maxScroll);

        clip(view);
        for (std::size_t i = 0; i < snap_.processes.size(); ++i) {
            const PCB& pcb = snap_.processes[i];
            const float rowY = view.y + rowHeight * static_cast<float>(i) - tableScroll_;
            if (rowY + rowHeight < view.y || rowY > view.y + view.height) continue;
            const Rectangle row{view.x + 2.0f, rowY + 2.0f, view.width - 4.0f, rowHeight - 4.0f};
            const float centerY = rowY + rowHeight / 2.0f;
            const bool running = pcb.state == ProcessState::Running;
            const bool finished = pcb.isFinished();
            const bool rowHover = contains(row, mouse_) && contains(view, mouse_);

            if (running) {
                roundRect(row, 8.0f, withAlpha(kGreen, 0.10f));
                roundRect({row.x, row.y + 5.0f, 3.0f, row.height - 10.0f}, 1.5f, kGreen);
            } else if (rowHover) {
                roundRect(row, 8.0f, withAlpha(kSurfaceHi, 0.8f));
            }

            const Color strong = finished ? kMuted : kText;
            const Color color = processColor(pcb.pid);
            text(std::to_string(pcb.pid), xPid, centerY - 8.0f, 13, kMuted);
            DrawCircleV({xName + 5.0f, centerY}, 4.5f, finished ? withAlpha(color, 0.55f) : color);
            text(fit(pcb.name, nameWidth - 26.0f, 14, running ? Weight::SemiBold : Weight::Regular), xName + 17.0f,
                 centerY - 8.5f, 14, strong, running ? Weight::SemiBold : Weight::Regular);

            const Color stateTint = stateColor(pcb.state);
            const std::string stateLabel = stateName(pcb.state);
            const float pillWidth = textWidth(stateLabel, 11, Weight::SemiBold) + 20.0f;
            roundRect({xState, centerY - 10.0f, pillWidth, 20.0f}, 10.0f, withAlpha(stateTint, 0.16f));
            text(stateLabel, xState + 10.0f, centerY - 6.5f, 11, stateTint, Weight::SemiBold);

            const double shown = shownCompletion_.count(pcb.pid) ? shownCompletion_[pcb.pid] : gui::completion(pcb);
            progressBar({xProgress, centerY - 4.0f, progressWidth - 62.0f, 8.0f}, shown,
                        finished ? withAlpha(color, 0.55f) : color);
            textRight(std::to_string(pcb.executedTime()) + "/" + std::to_string(pcb.burstTotal),
                      xProgress + progressWidth - 16.0f, centerY - 7.5f, 12, kMuted);

            const std::string numbers[4] = {std::to_string(pcb.burstRemaining), std::to_string(pcb.priority),
                                            std::to_string(pcb.waitingTime), "t=" + std::to_string(pcb.arrivalTime)};
            for (int n = 0; n < 4; ++n) {
                textCentered(numbers[n], xNumbers + numberWidth * (static_cast<float>(n) + 0.5f), centerY - 8.0f, 13,
                             n == 0 ? strong : kMuted, n == 0 ? Weight::SemiBold : Weight::Regular);
            }
        }
        EndScissorMode();

        if (maxScroll > 0.0f) {  // barra de desplazamiento
            const float trackHeight = view.height - 8.0f;
            const float thumbHeight = std::max(28.0f, trackHeight * view.height / contentHeight);
            const float thumbY = view.y + 4.0f + (trackHeight - thumbHeight) * (tableScroll_ / maxScroll);
            roundRect({rect.x + rect.width - 8.0f, thumbY, 4.0f, thumbHeight}, 2.0f, kBorder);
        }
    }

    // ------------------------------------------------------------------------ CPU

    void drawCpu(Rectangle rect) {
        panel(rect);
        text("CPU", rect.x + 16.0f, rect.y + 14.0f, 15, kText, Weight::SemiBold);
        const PCB* running = findProcess(snap_.runningPid);
        if (running != nullptr && !snap_.engineRunning) {
            textRight("en pausa", rect.x + rect.width - 16.0f, rect.y + 16.0f, 12, kAmber, Weight::SemiBold);
        }

        const Vector2 center{rect.x + 62.0f, rect.y + 104.0f};
        const float outer = 40.0f, innerRadius = 31.0f;
        DrawRing(center, innerRadius, outer, 0.0f, 360.0f, 72, kTrack);
        const float textX = rect.x + 122.0f;
        const float textWidthMax = rect.width - 122.0f - 16.0f;

        if (running == nullptr) {
            textCentered("—", center.x, center.y - 11.0f, 20, kFaint, Weight::SemiBold);
            text("CPU ociosa", textX, rect.y + 76.0f, 19, kMuted, Weight::SemiBold);
            std::string reason = "No hay procesos en el sistema";
            if (!snap_.processes.empty()) {
                const gui::StateCounts counts = gui::countStates(snap_.processes);
                if (counts.terminated == counts.total()) reason = "Todos los procesos finalizaron";
                else if (counts.created > 0 && counts.ready == 0) reason = "Esperando la llegada de procesos";
                else if (counts.blocked > 0 && counts.ready == 0) reason = "Los procesos pendientes están bloqueados";
                else reason = "Hay procesos listos: pulse Iniciar";
            }
            text(fit(reason, textWidthMax, 13), textX, rect.y + 104.0f, 13, kFaint);
            return;
        }

        const Color color = processColor(running->pid);
        const double shown = shownCompletion_.count(running->pid) ? shownCompletion_[running->pid]
                                                                  : gui::completion(*running);
        if (shown > 0.001) {
            DrawRing(center, innerRadius, outer, -90.0f, -90.0f + 360.0f * static_cast<float>(shown), 72, color);
        }
        textCentered(std::to_string(static_cast<int>(std::lround(shown * 100.0))) + "%", center.x, center.y - 9.0f, 16,
                     kText, Weight::SemiBold);

        text(fit(running->name, textWidthMax, 20, Weight::SemiBold), textX, rect.y + 54.0f, 20, kText,
             Weight::SemiBold);
        text("PID " + std::to_string(running->pid) + "   ·   Prioridad " + std::to_string(running->priority), textX,
             rect.y + 82.0f, 13, kMuted);
        text("Restante: " + std::to_string(running->burstRemaining) + " de " + std::to_string(running->burstTotal) +
                 " ticks",
             textX, rect.y + 103.0f, 13, kText);

        const float detailY = rect.y + 128.0f;
        if (snap_.policyKind == PolicyKind::RoundRobin) {
            // Porción de quantum consumida: un bloque por tick (hasta 12 bloques).
            text("Quantum", textX, detailY, 12, kFaint);
            const float blocksX = textX + textWidth("Quantum", 12) + 10.0f;
            if (snap_.quantum <= 12) {
                for (int i = 0; i < snap_.quantum; ++i) {
                    const bool used = i < running->quantumUsed;
                    roundRect({blocksX + static_cast<float>(i) * 13.0f, detailY + 2.0f, 10.0f, 10.0f}, 3.0f,
                              used ? color : kHollow);
                }
            } else {
                text(std::to_string(running->quantumUsed) + " / " + std::to_string(snap_.quantum), blocksX, detailY, 12,
                     kMuted, Weight::SemiBold);
            }
        } else if (snap_.agingInterval != kAgingOff) {
            const long effective = running->burstRemaining - running->waitingTime / snap_.agingInterval;
            text("Ráfaga efectiva: " + std::to_string(effective) + "  (envejecimiento)", textX, detailY, 12, kFaint);
        } else {
            text("Menor ráfaga restante (SRTF)", textX, detailY, 12, kFaint);
        }
    }

    // --------------------------------------------------------------------- colas

    struct Chip {
        int pid;
        std::string label;   ///< Nombre del proceso.
        std::string detail;  ///< Dato secundario (ráfaga restante, llegada...).
    };

    /// Dibuja fichas en varias líneas dentro de `area`. Devuelve la coordenada Y siguiente.
    float flowChips(Rectangle area, float y, const std::vector<Chip>& chips, Color tint, bool arrows) {
        const float chipHeight = 26.0f;
        float x = area.x;
        for (std::size_t i = 0; i < chips.size(); ++i) {
            const Chip& chip = chips[i];
            const std::string name = fit(chip.label, 120.0f, 13, Weight::SemiBold);
            const float width = 24.0f + textWidth(name, 13, Weight::SemiBold) +
                                (chip.detail.empty() ? 10.0f : textWidth(chip.detail, 12) + 18.0f);
            const float arrowWidth = arrows && i > 0 ? 16.0f : 0.0f;
            if (x + arrowWidth + width > area.x + area.width && x > area.x) {
                x = area.x;
                y += chipHeight + 8.0f;
            }
            if (y + chipHeight > area.y + area.height) {  // no cabe: se indica cuántas faltan
                const std::string more = "+" + std::to_string(chips.size() - i) + " más";
                const float moreWidth = textWidth(more, 12, Weight::SemiBold) + 18.0f;
                const Rectangle tag{area.x + area.width - moreWidth, area.y + area.height - 22.0f, moreWidth, 22.0f};
                roundRect({tag.x - 6.0f, tag.y - 2.0f, tag.width + 6.0f, tag.height + 4.0f}, 8.0f, kSurface);
                roundRect(tag, 11.0f, kTrack);
                text(more, tag.x + 9.0f, tag.y + 4.0f, 12, kMuted, Weight::SemiBold);
                return area.y + area.height;
            }
            if (arrowWidth > 0.0f && x > area.x) {
                const float cy = y + chipHeight / 2.0f;
                DrawLineEx({x + 4.0f, cy - 4.0f}, {x + 9.0f, cy}, 1.6f, kFaint);
                DrawLineEx({x + 9.0f, cy}, {x + 4.0f, cy + 4.0f}, 1.6f, kFaint);
                x += arrowWidth;
            }
            const Rectangle rect{x, y, width, chipHeight};
            roundRect(rect, 13.0f, withAlpha(tint, 0.30f));
            roundRect(inset(rect, 1.0f), 12.0f, kSurfaceHi);
            DrawCircleV({rect.x + 13.0f, rect.y + chipHeight / 2.0f}, 4.0f, processColor(chip.pid));
            text(name, rect.x + 22.0f, rect.y + 5.5f, 13, kText, Weight::SemiBold);
            if (!chip.detail.empty()) {
                text(chip.detail, rect.x + 22.0f + textWidth(name, 13, Weight::SemiBold) + 8.0f, rect.y + 6.5f, 12,
                     kMuted);
            }
            x += width + 6.0f;
        }
        return y + chipHeight;
    }

    void drawQueues(Rectangle rect) {
        panel(rect);
        text("Cola de LISTOS", rect.x + 16.0f, rect.y + 14.0f, 15, kText, Weight::SemiBold);
        badge(rect.x + 16.0f + textWidth("Cola de LISTOS", 15, Weight::SemiBold) + 10.0f, rect.y + 13.0f,
              std::to_string(snap_.readyQueue.size()));
        textRight(snap_.policyKind == PolicyKind::RoundRobin ? "orden de atención (FIFO)" : "orden de ingreso",
                  rect.x + rect.width - 16.0f, rect.y + 16.0f, 12, kFaint);

        const Rectangle area{rect.x + 16.0f, rect.y + 46.0f, rect.width - 32.0f, rect.height - 46.0f - 12.0f};
        clip({rect.x + 2.0f, rect.y + 40.0f, rect.width - 4.0f, rect.height - 44.0f});
        float y = area.y;

        std::vector<Chip> ready;
        for (int pid : snap_.readyQueue) {
            if (const PCB* pcb = findProcess(pid)) {
                const std::string unit = pcb->burstRemaining == 1 ? " tick" : " ticks";
                ready.push_back({pid, pcb->name, std::to_string(pcb->burstRemaining) + unit});
            }
        }
        if (ready.empty()) {
            text("(vacía)", area.x, y + 4.0f, 13, kFaint);
            y += 26.0f;
        } else {
            y = flowChips(area, y, ready, kAmber, true);
        }

        // Secciones secundarias: procesos que aún no llegan (NEW) y bloqueados.
        // Si hay de los dos tipos se reparten en dos columnas para que quepan.
        std::vector<Chip> arriving, blocked;
        for (const PCB& pcb : snap_.processes) {
            if (pcb.state == ProcessState::New) {
                arriving.push_back({pcb.pid, pcb.name, "llega en t=" + std::to_string(pcb.arrivalTime)});
            }
            if (pcb.state == ProcessState::Blocked) blocked.push_back({pcb.pid, pcb.name, ""});
        }
        struct Section {
            const char* title;
            const std::vector<Chip>* chips;
            Color tint;
        };
        std::vector<Section> sections;
        if (!arriving.empty()) sections.push_back({"POR LLEGAR", &arriving, kAccent});
        if (!blocked.empty()) sections.push_back({"BLOQUEADOS", &blocked, kRed});
        y += 14.0f;
        if (!sections.empty() && y + 45.0f <= area.y + area.height) {
            const float columnGap = 16.0f;
            const float columnWidth = (area.width - columnGap * static_cast<float>(sections.size() - 1)) /
                                      static_cast<float>(sections.size());
            for (std::size_t i = 0; i < sections.size(); ++i) {
                const float columnX = area.x + static_cast<float>(i) * (columnWidth + columnGap);
                const std::string title =
                    std::string(sections[i].title) + "  " + std::to_string(sections[i].chips->size());
                text(title, columnX, y, 11, kFaint, Weight::SemiBold);
                const Rectangle column{columnX, y + 19.0f, columnWidth, area.y + area.height - (y + 19.0f)};
                flowChips(column, column.y, *sections[i].chips, sections[i].tint, false);
            }
        }
        EndScissorMode();
    }

    // --------------------------------------------------------------------- Gantt

    void drawGantt(Rectangle rect) {
        panel(rect);
        const std::string title = "Diagrama de Gantt";
        text(title, rect.x + 16.0f, rect.y + 14.0f, 15, kText, Weight::SemiBold);
        const float titleEnd = rect.x + 16.0f + textWidth(title, 15, Weight::SemiBold);
        const std::string range = "0 – " + std::to_string(snap_.clock) + " ticks";
        text(range, titleEnd + 12.0f, rect.y + 16.0f, 13, kMuted);
        float headerLeft = titleEnd + 12.0f + textWidth(range, 13) + 24.0f;

        const Rectangle lane{rect.x + 16.0f, rect.y + 52.0f, rect.width - 32.0f, 72.0f};
        gui::GanttViewport view = gui::ganttViewport(visualClock_, lane.width, ganttPan_);
        ganttPan_ = std::clamp(ganttPan_, 0.0, view.maxPan);

        // Desplazamiento: rueda, arrastre o teclas. Sin desplazamiento, la vista sigue en vivo.
        const bool overPanel = contains(rect, mouse_);
        if (overPanel && wheel_ != 0.0f) {
            ganttPan_ = std::clamp(ganttPan_ + static_cast<double>(wheel_) * 3.0, 0.0, view.maxPan);
        }
        if (contains(lane, mouse_) && pressed_ && view.maxPan > 0.0) {
            draggingGantt_ = true;
            dragStartX_ = mouse_.x;
            dragStartPan_ = ganttPan_;
        }
        if (!down_) draggingGantt_ = false;
        if (draggingGantt_) {
            const double draggedTicks = static_cast<double>(mouse_.x - dragStartX_) / view.pixelsPerTick;
            ganttPan_ = std::clamp(dragStartPan_ + draggedTicks, 0.0, view.maxPan);
        }
        view = gui::ganttViewport(visualClock_, lane.width, ganttPan_);

        // Derecha de la cabecera: estado "en vivo" o botón para volver al final.
        float headerRight = rect.x + rect.width - 16.0f;
        if (ganttPan_ > 0.01) {
            const Rectangle button{headerRight - 112.0f, rect.y + 11.0f, 112.0f, 26.0f};
            if (ghostButton(button, "Seguir en vivo")) ganttPan_ = 0.0;
            headerRight = button.x - 14.0f;
        } else if (snap_.engineRunning && hasPendingWork()) {
            const std::string live = "EN VIVO";
            const float liveWidth = textWidth(live, 11, Weight::SemiBold);
            text(live, headerRight - liveWidth, rect.y + 18.0f, 11, kGreen, Weight::SemiBold);
            DrawCircleV({headerRight - liveWidth - 9.0f, rect.y + 24.0f}, 3.5f, kGreen);
            headerRight -= liveWidth + 30.0f;
        }
        drawLegend(headerLeft, headerRight, rect.y + 17.0f);

        roundRect(lane, 10.0f, withAlpha(kTrack, 0.55f));
        const auto toX = [&](double tick) {
            return lane.x + static_cast<float>((tick - view.firstTick) * view.pixelsPerTick);
        };

        if (snap_.gantt.empty()) {
            textCentered("Aún no hay ejecución registrada. Pulse Iniciar o escriba  start  en la consola.",
                         lane.x + lane.width / 2.0f, lane.y + lane.height / 2.0f - 8.0f, 13, kFaint);
        }

        // El recorte deja 8 px por encima y por debajo del carril para el cursor.
        const Rectangle laneClip{lane.x, lane.y - 8.0f, lane.width, lane.height + 16.0f};
        clip(laneClip);
        const std::vector<gui::GanttBar> bars =
            gui::visibleBars(snap_.gantt, visualClock_, view.firstTick, view.firstTick + view.visibleTicks);
        for (const gui::GanttBar& bar : bars) {
            const float x0 = toX(bar.start);
            const float x1 = toX(bar.end);
            const Rectangle body{x0 + 1.0f, lane.y + 6.0f, x1 - x0 - 2.0f, lane.height - 12.0f};
            if (body.width < 1.0f) continue;
            const Color color = bar.isIdle() ? kIdle : processColor(bar.pid);
            roundRect(body, 7.0f, color);
            if (bar.isIdle()) {  // tramo ocioso: rayado diagonal
                clip(intersect(body, laneClip));
                for (float hx = body.x - body.height; hx < body.x + body.width; hx += 9.0f) {
                    DrawLineEx({hx, body.y + body.height}, {hx + body.height, body.y}, 1.0f, withAlpha(kFaint, 0.35f));
                }
                EndScissorMode();
                clip(laneClip);
            }
            const std::string label = fit(bar.label, body.width - 12.0f, 13, Weight::SemiBold);
            if (!label.empty() && body.width >= 26.0f) {
                const std::string span = std::to_string(bar.fullStart) + " – " + std::to_string(bar.fullEnd);
                const bool twoLines = body.width >= textWidth(span, 11) + 14.0f && body.width >= 52.0f;
                const float middle = body.y + body.height / 2.0f;
                const Color ink = bar.isIdle() ? kFaint : kInk;
                textCentered(label, body.x + body.width / 2.0f, middle - (twoLines ? 15.0f : 8.0f), 13, ink,
                             Weight::SemiBold);
                if (twoLines) {
                    textCentered(span, body.x + body.width / 2.0f, middle + 3.0f, 11,
                                 withAlpha(ink, bar.isIdle() ? 0.9f : 0.68f));
                }
            }
            if (contains(body, mouse_) && contains(lane, mouse_) && !draggingGantt_) {
                const long ticks = bar.fullEnd - bar.fullStart;
                tooltip_ = {bar.isIdle() ? "CPU ociosa" : bar.label + "  ·  PID " + std::to_string(bar.pid),
                            "t = " + std::to_string(bar.fullStart) + " → " + std::to_string(bar.fullEnd) + "   (" +
                                std::to_string(ticks) + (ticks == 1 ? " tick)" : " ticks)")};
            }
        }
        // Cursor del instante actual.
        if (visualClock_ > 0.0) {
            const float cursorX = toX(visualClock_);
            DrawRectangleRec({cursorX - 1.0f, lane.y - 4.0f, 2.0f, lane.height + 8.0f}, kText);
            DrawTriangle({cursorX - 5.0f, lane.y - 8.0f}, {cursorX, lane.y - 1.0f}, {cursorX + 5.0f, lane.y - 8.0f},
                         kText);
        }
        EndScissorMode();

        // Eje de tiempo: marcas cada 1, 2, 5, 10... ticks según la escala.
        const float axisY = lane.y + lane.height + 9.0f;
        const long step = gui::ganttTickStep(view.pixelsPerTick, 44.0);
        const long firstMark = static_cast<long>(std::ceil(view.firstTick / static_cast<double>(step))) * step;
        const double lastVisible = view.firstTick + view.visibleTicks + 1e-6;
        for (long tick = firstMark; static_cast<double>(tick) <= lastVisible; tick += step) {
            const float x = toX(static_cast<double>(tick));
            if (x < lane.x - 0.5f || x > lane.x + lane.width + 0.5f) continue;
            DrawRectangleRec({x - 0.5f, axisY, 1.0f, 5.0f}, kFaint);
            const std::string number = std::to_string(tick);
            const float half = textWidth(number, 12) / 2.0f;
            const float labelX = std::clamp(x - half, lane.x, lane.x + lane.width - 2.0f * half);
            text(number, labelX, axisY + 9.0f, 12, kMuted);
        }
        textRight("ticks", lane.x + lane.width, axisY + 27.0f, 11, kFaint);
        if (view.maxPan > 0.0) {
            text("Rueda o arrastre para ver el historial", lane.x, axisY + 27.0f, 11, kFaint);
        }
    }

    /// Leyenda de colores (los procesos que quepan entre `left` y `right`).
    void drawLegend(float left, float right, float y) {
        float x = right;
        std::size_t drawn = 0;
        std::vector<std::pair<float, const PCB*>> items;  // se calculan de derecha a izquierda
        for (auto it = snap_.processes.rbegin(); it != snap_.processes.rend(); ++it) {
            const std::string name = fit(it->name, 110.0f, 12);
            const float width = textWidth(name, 12) + 18.0f + 14.0f;
            if (x - width < left) break;
            x -= width;
            items.emplace_back(x, &*it);
            ++drawn;
        }
        for (const auto& item : items) {
            DrawCircleV({item.first + 5.0f, y + 7.0f}, 4.0f, processColor(item.second->pid));
            text(fit(item.second->name, 110.0f, 12), item.first + 14.0f, y, 12, kMuted);
        }
        if (drawn < snap_.processes.size() && drawn > 0) {
            textRight("+" + std::to_string(snap_.processes.size() - drawn), x - 4.0f, y, 12, kFaint);
        }
    }

    // ----------------------------------------------------------------------- pie

    void drawFooter(float x, float y, float width) {
        text("Espacio: iniciar / pausar     R / S: política RR / SRTF     ← →: desplazar el Gantt", x + 4.0f, y, 12,
             kFaint);
        textRight("La consola sigue activa: ventana y shell controlan el mismo Kernel", x + width - 4.0f, y, 12,
                  kFaint);
    }

    void drawTooltip() {
        if (tooltip_.empty()) return;
        const float width =
            std::max(textWidth(tooltip_.title, 13, Weight::SemiBold), textWidth(tooltip_.detail, 12)) + 24.0f;
        const float height = 50.0f;
        float x = mouse_.x + 14.0f;
        float y = mouse_.y - height - 10.0f;
        x = std::min(x, static_cast<float>(GetScreenWidth()) - width - 8.0f);
        if (y < 8.0f) y = mouse_.y + 18.0f;
        roundRect({x, y, width, height}, 9.0f, kBorder);
        roundRect({x + 1.0f, y + 1.0f, width - 2.0f, height - 2.0f}, 8.0f, Color{6, 9, 18, 250});
        text(tooltip_.title, x + 12.0f, y + 9.0f, 13, kText, Weight::SemiBold);
        text(tooltip_.detail, x + 12.0f, y + 28.0f, 12, kMuted);
    }

    // ------------------------------------------------------------------ captura

    /**
     * Si SIM_GUI_SCREENSHOT=<archivo.png> está definida, guarda una captura a
     * los SIM_GUI_SCREENSHOT_DELAY segundos (2 por defecto). Con
     * SIM_GUI_AUTOCLOSE=1 la ventana se cierra después. Se usa para la
     * documentación y para la prueba automática de la CI.
     */
    void captureScreenshotIfRequested() {
        if (screenshotPath_.empty() || screenshotDone_ || GetTime() < screenshotDelay_) return;
        rlDrawRenderBatchActive();  // vuelca lo dibujado antes de leer la pantalla
        const Image image = LoadImageFromScreen();
        ExportImage(image, screenshotPath_.c_str());
        UnloadImage(image);
        screenshotDone_ = true;
        if (autoClose_) closeRequested_ = true;
    }

    struct Tooltip {
        std::string title;
        std::string detail;
        bool empty() const { return title.empty(); }
        void clear() {
            title.clear();
            detail.clear();
        }
    };

    Kernel& kernel_;
    Fonts fonts_;
    SystemSnapshot snap_;

    // Entrada del cuadro actual.
    Vector2 mouse_{0.0f, 0.0f};
    bool pressed_ = false;
    bool down_ = false;
    float wheel_ = 0.0f;
    float dt_ = 0.0f;
    bool wantHand_ = false;
    Tooltip tooltip_;

    // Estado de la vista (no del simulador).
    double visualClock_ = 0.0;                           ///< Reloj animado del Gantt.
    double ganttPan_ = 0.0;                              ///< Ticks hacia atrás desde el final.
    bool draggingGantt_ = false;
    float dragStartX_ = 0.0f;
    double dragStartPan_ = 0.0;
    bool draggingSpeed_ = false;
    float tableScroll_ = 0.0f;
    std::unordered_map<int, double> shownCompletion_;    ///< Progreso animado por PID.
    double shownCpuUsage_ = 0.0;

    std::string screenshotPath_;
    double screenshotDelay_ = 2.0;
    bool screenshotDone_ = false;
    bool autoClose_ = false;
    bool closeRequested_ = false;
};

}  // namespace

// ============================================================================
// 4. Ventana y bucle de dibujo
// ============================================================================

bool guiAvailable() { return true; }

void runGui(Kernel& kernel) {
    const auto reportFailure = [] {
        std::cout << "\n[ERROR]: No se pudo abrir la ventana gráfica (¿hay un entorno gráfico disponible?). "
                     "La consola sigue funcionando.\n"
                  << std::flush;
    };
    if (!displayAvailable()) {   // Linux sin servidor gráfico: raylib 5.x se caería
        reportFailure();
        return;
    }
    disableAppNap();             // macOS: la ventana oculta no frena el bucle ni el reloj
    SetTraceLogLevel(LOG_NONE);  // raylib no escribe en la consola: es de la shell
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI | FLAG_VSYNC_HINT);
    InitWindow(1320, 860, "Simulador de Kernel — Shell & Scheduling Engine");
    if (!IsWindowReady()) {
        reportFailure();
        return;
    }
    SetWindowMinSize(1120, 740);
    SetExitKey(KEY_NULL);  // Esc no cierra la ventana por accidente
    SetTargetFPS(60);

    {
        GuiApp app(kernel);  // las fuentes se liberan antes de cerrar la ventana
        while (!WindowShouldClose() && !kernel.shutdownRequested() && !app.wantsClose()) {
            app.frame();
        }
    }
    CloseWindow();
}

}  // namespace sim

#else  // ------------------------------------------------ compilación sin interfaz gráfica

namespace sim {

bool guiAvailable() { return false; }

void runGui(Kernel&) {}

}  // namespace sim

#endif  // SIM_WITH_GUI
