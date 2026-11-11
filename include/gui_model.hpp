/**
 * @file gui_model.hpp
 * @brief Lógica de la interfaz gráfica que no depende de la librería gráfica.
 *
 * Aquí están los cálculos de la vista: qué parte del Gantt se ve, cada cuántos
 * ticks va una marca del eje, la escala del control de velocidad y las
 * animaciones. Están separados del dibujo (gui.cpp) para probarlos con
 * `make test` sin abrir una ventana ni enlazar raylib.
 */
#ifndef SIM_GUI_MODEL_HPP
#define SIM_GUI_MODEL_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "gantt.hpp"
#include "pcb.hpp"

namespace sim {
namespace gui {

// ------------------------------------------------------------- velocidad
/// Límites del control de velocidad: los mismos que valida `set_speed`.
constexpr int kMinSpeedMs = 50;
constexpr int kMaxSpeedMs = 5000;

/**
 * @brief Convierte la posición del deslizador (0..1) en milisegundos por tick.
 *
 * La escala es logarítmica (50 ms ... 5000 ms), porque el rango abarca dos
 * órdenes de magnitud, y el resultado se redondea a valores cómodos.
 */
int speedFromSlider(double position);
/// Inversa de speedFromSlider: posición (0..1) que corresponde a `msPerTick`.
double sliderFromSpeed(int msPerTick);

// ------------------------------------------------------------------ Gantt
/// Escala horizontal del Gantt (pixeles por tick) y ticks mínimos que se muestran.
constexpr double kMinPixelsPerTick = 18.0;
constexpr double kMaxPixelsPerTick = 72.0;
constexpr double kMinTicksShown = 12.0;

/// Parte del diagrama de Gantt visible en pantalla.
struct GanttViewport {
    double pixelsPerTick;  ///< Escala horizontal.
    double firstTick;      ///< Tick (fraccionario) del borde izquierdo.
    double visibleTicks;   ///< Ticks que caben en el ancho disponible.
    double maxPan;         ///< Máximo desplazamiento hacia atrás (0 si todo cabe).
};

/**
 * @brief Calcula la ventana visible del Gantt.
 *
 * Mientras el historial quepa, se muestra completo desde t=0 y la escala se
 * ajusta al ancho. Cuando ya no cabe, la escala queda en el mínimo legible y
 * la vista sigue al final del historial; `panTicks` la desplaza hacia atrás.
 *
 * @param totalTicks Ticks transcurridos (reloj visual).
 * @param widthPx    Ancho disponible en pixeles.
 * @param panTicks   Desplazamiento hacia atrás desde el final (0 = seguir el final).
 */
GanttViewport ganttViewport(double totalTicks, double widthPx, double panTicks);

/// Separación entre marcas del eje: el menor valor 1, 2, 5, 10, 20, 50... cuyas etiquetas no se enciman.
long ganttTickStep(double pixelsPerTick, double minLabelSpacingPx);

/// Tramo del Gantt recortado a lo que debe dibujarse.
struct GanttBar {
    double start;       ///< Inicio (ticks).
    double end;         ///< Fin (ticks), recortado al reloj visual.
    int pid;            ///< Proceso (kNoPid si la CPU estuvo ociosa).
    std::string label;  ///< Nombre del proceso o "IDLE".
    long fullStart;     ///< Inicio real del tramo completo.
    long fullEnd;       ///< Fin real del tramo completo.

    bool isIdle() const { return pid == kNoPid; }
};

/// Tramos que caen dentro de [firstTick, lastTick], recortados al reloj visual.
std::vector<GanttBar> visibleBars(const std::vector<GanttSegment>& segments, double visualClock,
                                  double firstTick, double lastTick);

// ------------------------------------------------------------ animaciones
/**
 * @brief Avanza el reloj visual hacia el reloj real de la simulación.
 *
 * El motor ejecuta el tick completo y después duerme `msPerTick`; el reloj
 * visual recorre ese tick durante la espera, de modo que el Gantt crece de
 * forma continua. Nunca se adelanta al reloj real ni se atrasa más de un tick.
 */
double advanceVisualClock(double visual, long clock, bool engineRunning, int msPerTick, double dtSeconds);

/// Acerca `shown` a `target` con suavizado exponencial (independiente de los FPS).
double smoothTowards(double shown, double target, double dtSeconds, double speed = 14.0);

// --------------------------------------------------------------- procesos
/// Fracción de la ráfaga ya ejecutada (0..1).
double completion(const PCB& pcb);

/// Cantidad de procesos en cada estado.
struct StateCounts {
    std::size_t created = 0;     ///< NEW
    std::size_t ready = 0;       ///< READY
    std::size_t running = 0;     ///< RUNNING
    std::size_t blocked = 0;     ///< BLOCKED
    std::size_t terminated = 0;  ///< TERMINATED

    std::size_t total() const { return created + ready + running + blocked + terminated; }
};
StateCounts countStates(const std::vector<PCB>& processes);

/// Color estable de un proceso: índice dentro de una paleta de `paletteSize` colores.
std::size_t paletteIndex(int pid, std::size_t paletteSize);

// ------------------------------------------------------------------ texto
/// Número con dos decimales ("2.50"), como en el comando `stats`.
std::string formatAverage(double value);

/**
 * @brief Recorta `text` y le agrega "…" hasta que quepa en `maxWidth`.
 * @param measure Función que devuelve el ancho en pixeles de una cadena UTF-8.
 */
std::string ellipsize(const std::string& text, double maxWidth,
                      const std::function<double(const std::string&)>& measure);

}  // namespace gui
}  // namespace sim

#endif  // SIM_GUI_MODEL_HPP
