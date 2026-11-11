/**
 * @file test_gui.cpp
 * @brief Pruebas de la interfaz gráfica que no necesitan abrir una ventana.
 *
 * - Cálculos de la vista (gui_model): escala de velocidad, ventana visible y
 *   eje del Gantt, animaciones y recorte de texto.
 * - Coordinación entre hilos: la CLI pide la ventana (requestGui) y el hilo
 *   principal despierta (waitForGuiRequest); `exit` también lo despierta.
 * - Comando `gui` con y sin la interfaz compilada.
 */
#include <pthread.h>

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "gui.hpp"
#include "gui_model.hpp"
#include "kernel.hpp"
#include "simulation.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {

bool near(double actual, double expected, double tolerance = 1e-9) { return std::fabs(actual - expected) < tolerance; }

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

/// Mide una cadena UTF-8 como 10 px por carácter (los bytes de continuación no cuentan).
double tenPixelsPerCharacter(const std::string& text) {
    double width = 0.0;
    for (unsigned char c : text) {
        if ((c & 0xC0) != 0x80) width += 10.0;
    }
    return width;
}

/// Hilo que hace el papel del hilo principal: espera la orden de abrir la ventana.
struct MainThreadStandIn {
    Kernel* kernel;
    bool mustOpenWindow;
};

void* waitForGui(void* arg) {
    auto* standIn = static_cast<MainThreadStandIn*>(arg);
    standIn->mustOpenWindow = standIn->kernel->waitForGuiRequest();
    return nullptr;
}

}  // namespace

// ------------------------------------------------------------------ gui_model

TEST_CASE("GUI: el control de velocidad cubre 50-5000 ms en escala logarítmica") {
    CHECK_EQ(gui::speedFromSlider(0.0), 50);
    CHECK_EQ(gui::speedFromSlider(1.0), 5000);
    CHECK_EQ(gui::speedFromSlider(0.5), 500);   // punto medio geométrico de 50 y 5000
    CHECK_EQ(gui::speedFromSlider(-3.0), 50);   // fuera de rango: se acota
    CHECK_EQ(gui::speedFromSlider(7.0), 5000);
    for (int ms : {50, 100, 250, 500, 1000, 2000, 5000}) {
        CHECK_EQ(gui::speedFromSlider(gui::sliderFromSpeed(ms)), ms);  // ida y vuelta
    }
    CHECK(gui::sliderFromSpeed(100) < gui::sliderFromSpeed(1000));
    CHECK(near(gui::sliderFromSpeed(50), 0.0) && near(gui::sliderFromSpeed(5000), 1.0));
}

TEST_CASE("GUI: mientras el Gantt cabe se muestra completo; después sigue al final") {
    // 7 ticks en 720 px: se muestran al menos 12 ticks, todo desde t=0.
    gui::GanttViewport fits = gui::ganttViewport(7.0, 720.0, 0.0);
    CHECK(near(fits.pixelsPerTick, 60.0));
    CHECK(near(fits.firstTick, 0.0));
    CHECK(near(fits.maxPan, 0.0));

    // 100 ticks no caben ni con la escala mínima (18 px): se ven los últimos 40.
    gui::GanttViewport follows = gui::ganttViewport(100.0, 720.0, 0.0);
    CHECK(near(follows.pixelsPerTick, gui::kMinPixelsPerTick));
    CHECK(near(follows.visibleTicks, 40.0));
    CHECK(near(follows.firstTick, 60.0));
    CHECK(near(follows.maxPan, 60.0));

    CHECK(near(gui::ganttViewport(100.0, 720.0, 10.0).firstTick, 50.0));   // 10 ticks hacia atrás
    CHECK(near(gui::ganttViewport(100.0, 720.0, 999.0).firstTick, 0.0));   // no pasa del inicio
    CHECK(near(gui::ganttViewport(100.0, 720.0, -5.0).firstTick, 60.0));   // ni del final
}

TEST_CASE("GUI: las marcas del eje del Gantt van cada 1, 2, 5, 10... ticks sin encimarse") {
    CHECK_EQ(gui::ganttTickStep(72.0, 44.0), 1L);
    CHECK_EQ(gui::ganttTickStep(30.0, 44.0), 2L);
    CHECK_EQ(gui::ganttTickStep(18.0, 44.0), 5L);
    CHECK_EQ(gui::ganttTickStep(5.0, 44.0), 10L);
    CHECK_EQ(gui::ganttTickStep(0.5, 44.0), 100L);
    CHECK_EQ(gui::ganttTickStep(0.0, 44.0), 1L);
}

TEST_CASE("GUI: los tramos del Gantt se recortan al reloj visual y a la ventana visible") {
    const std::vector<GanttSegment> segments = {
        {0, 2, 1, "P1"}, {2, 4, 2, "P2"}, {4, 5, kNoPid, "IDLE"}, {5, 8, 1, "P1"}};

    const std::vector<gui::GanttBar> revealing = gui::visibleBars(segments, 2.5, 0.0, 12.0);
    CHECK_EQ(revealing.size(), static_cast<std::size_t>(2));  // P2 está a medio revelar
    if (revealing.size() == 2) {
        CHECK(near(revealing[1].end, 2.5));
        CHECK_EQ(revealing[1].fullEnd, 4L);  // el tramo real termina en 4
    }

    const std::vector<gui::GanttBar> scrolled = gui::visibleBars(segments, 8.0, 4.5, 12.0);
    CHECK_EQ(scrolled.size(), static_cast<std::size_t>(2));   // P1 (0-2) y P2 (2-4) quedan a la izquierda
    if (scrolled.size() == 2) {
        CHECK(scrolled[0].isIdle());
        CHECK_EQ(scrolled[0].label, "IDLE");
        CHECK_EQ(scrolled[1].pid, 1);
    }
}

TEST_CASE("GUI: el reloj visual recorre cada tick sin adelantarse al reloj real") {
    // En marcha a 1000 ms/tick: un cuarto de segundo avanza un cuarto de tick.
    CHECK(near(gui::advanceVisualClock(3.0, 4, true, 1000, 0.25), 3.25));
    CHECK(near(gui::advanceVisualClock(3.9, 4, true, 1000, 0.5), 4.0));   // no pasa del reloj real
    CHECK(near(gui::advanceVisualClock(1.0, 9, true, 1000, 0.1), 8.1));   // ni se atrasa más de un tick
    CHECK(near(gui::advanceVisualClock(5.0, 2, true, 1000, 0.1), 2.0));   // si el reloj retrocede, se iguala
    // En pausa se pone al día más rápido que el tick configurado.
    CHECK(gui::advanceVisualClock(3.0, 4, false, 5000, 0.05) > gui::advanceVisualClock(3.0, 4, true, 5000, 0.05));
}

TEST_CASE("GUI: el suavizado se acerca al objetivo y termina exactamente en él") {
    double shown = 0.0;
    double previous = shown;
    for (int frame = 0; frame < 200; ++frame) {
        shown = gui::smoothTowards(shown, 1.0, 1.0 / 60.0);
        CHECK(shown >= previous && shown <= 1.0);
        previous = shown;
    }
    CHECK(near(shown, 1.0));
    CHECK(near(gui::smoothTowards(0.4, 0.4, 0.016), 0.4));
}

TEST_CASE("GUI: progreso, conteo por estado y color estable de los procesos") {
    PCB half(1, "A", 4, 3, 0);
    half.burstRemaining = 2;
    CHECK(near(gui::completion(half), 0.5));

    std::vector<PCB> processes = {PCB(1, "A", 4, 3, 0), PCB(2, "B", 2, 3, 0), PCB(3, "C", 1, 3, 5)};
    processes[0].state = ProcessState::Running;
    processes[1].state = ProcessState::Terminated;
    const gui::StateCounts counts = gui::countStates(processes);
    CHECK_EQ(counts.running, static_cast<std::size_t>(1));
    CHECK_EQ(counts.terminated, static_cast<std::size_t>(1));
    CHECK_EQ(counts.created, static_cast<std::size_t>(1));
    CHECK_EQ(counts.total(), static_cast<std::size_t>(3));

    CHECK_EQ(gui::paletteIndex(1, 10), static_cast<std::size_t>(0));
    CHECK_EQ(gui::paletteIndex(11, 10), static_cast<std::size_t>(0));  // el color se repite cada 10 PID
    CHECK_EQ(gui::paletteIndex(4, 10), static_cast<std::size_t>(3));
}

TEST_CASE("GUI: el texto largo se recorta con puntos suspensivos sin romper UTF-8") {
    const std::string ellipsis = "\xE2\x80\xA6";  // "…"
    CHECK_EQ(gui::ellipsize("Editor", 100.0, tenPixelsPerCharacter), "Editor");
    CHECK_EQ(gui::ellipsize("Compilador", 60.0, tenPixelsPerCharacter), "Compi" + ellipsis);
    CHECK_EQ(gui::ellipsize("Ñandú_áéí", 50.0, tenPixelsPerCharacter), "Ñand" + ellipsis);
    CHECK_EQ(gui::ellipsize("Largo", 5.0, tenPixelsPerCharacter), "");
    CHECK_EQ(gui::formatAverage(2.5), "2.50");
}

// ------------------------------------------------------------- hilos y Kernel

TEST_CASE("GUI: la CLI pide la ventana y el hilo principal despierta para abrirla") {
    Kernel kernel;
    MainThreadStandIn standIn{&kernel, false};
    pthread_t thread;
    pthread_create(&thread, nullptr, waitForGui, &standIn);
    CHECK(kernel.requestGui());        // comando `gui`
    pthread_join(thread, nullptr);
    CHECK(standIn.mustOpenWindow);
    CHECK(!kernel.requestGui());       // con la ventana abierta no se pide otra
    kernel.notifyGuiClosed();
    CHECK(kernel.requestGui());        // al cerrarla se puede volver a abrir
}

TEST_CASE("GUI: exit despierta al hilo principal aunque nadie haya pedido la ventana") {
    Kernel kernel;
    MainThreadStandIn standIn{&kernel, true};
    pthread_t thread;
    pthread_create(&thread, nullptr, waitForGui, &standIn);
    CHECK(!kernel.shutdownRequested());
    kernel.requestShutdown();
    pthread_join(thread, nullptr);
    CHECK(!standIn.mustOpenWindow);
    CHECK(kernel.shutdownRequested());
    CHECK(!kernel.requestGui());       // durante el cierre ya no se abre la ventana
}

TEST_CASE("GUI: la instantánea conserva el quantum de Round Robin al pasar por SRTF") {
    Kernel kernel;
    CHECK_EQ(kernel.snapshot().quantum, kDefaultQuantum);
    kernel.setPolicy(PolicyKind::RoundRobin, 4);
    CHECK_EQ(kernel.snapshot().quantum, 4);
    kernel.setPolicy(PolicyKind::SRTF, kDefaultQuantum);
    CHECK_EQ(kernel.snapshot().quantum, 4);  // la ventana lo sigue mostrando
    kernel.setPolicy(PolicyKind::RoundRobin, kernel.snapshot().quantum);
    CHECK_EQ(kernel.snapshot().policy, "RR (quantum = 4)");
}

TEST_CASE("CLI gui: pide la ventana o explica cómo compilar la interfaz") {
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);
    cli.execute("gui");
    if (guiAvailable()) {
        CHECK(contains(out.str(), "[OK]: Abriendo la interfaz gráfica."));
        out.str("");
        cli.execute("gui");
        CHECK_EQ(out.str(), "[INFO]: La interfaz gráfica ya está abierta.\n");
    } else {
        CHECK(contains(out.str(), "[INFO]: Este ejecutable se compiló sin la interfaz gráfica (bonus)."));
        CHECK(contains(out.str(), "make gui"));
        CHECK(kernel.requestGui());  // la CLI no llegó a pedir la ventana
    }
    out.str("");
    cli.execute("gui ahora");
    CHECK(contains(out.str(), "[ERROR DE SINTAXIS]: El comando 'gui' no recibe parámetros."));
}
