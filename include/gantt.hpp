/**
 * @file gantt.hpp
 * @brief Registro del historial de ejecución y su representación como diagrama de Gantt.
 */
#ifndef SIM_GANTT_HPP
#define SIM_GANTT_HPP

#include <string>
#include <vector>

#include "pcb.hpp"

namespace sim {

/// Tramo continuo de tiempo [start, end) en que la CPU ejecutó el mismo proceso (o estuvo ociosa).
struct GanttSegment {
    long start;          ///< Primer tick del tramo.
    long end;            ///< Tick siguiente al último del tramo (exclusivo).
    int pid;             ///< Proceso ejecutado; kNoPid si la CPU estuvo ociosa.
    std::string label;   ///< Nombre del proceso o "IDLE".

    bool isIdle() const { return pid == kNoPid; }
};

/**
 * @brief Historial de ejecución tick a tick, compactado en tramos.
 *
 * Ticks consecutivos del mismo proceso se agrupan en un solo tramo, por lo que
 * el diagrama muestra cada cambio de contexto (o periodo ocioso) una sola vez.
 */
class GanttChart {
public:
    /// Registra que durante el tick [tick, tick+1) se ejecutó `pid` (0 = CPU ociosa).
    void record(long tick, int pid, const std::string& label);
    const std::vector<GanttSegment>& segments() const { return segments_; }
    void clear() { segments_.clear(); }

private:
    std::vector<GanttSegment> segments_;
};

/**
 * @brief Representación en texto con el formato del enunciado:
 * @code
 *   DIAGRAMA DE GANTT (0 - 8 ticks):
 *   [0-2s: P1] -> [2-4s: P2] -> [4-6s: P3] -> [6-8s: P1] -> [IDLE]
 * @endcode
 * Los periodos ociosos ya transcurridos se muestran como `[a-bs: IDLE]`.
 * @param clock Instante actual del reloj simulado.
 * @param cpuIdleWithPendingWork Si es true (CPU libre en este instante pero quedan
 *        procesos sin terminar) se agrega un `[IDLE]` final.
 */
std::string renderGantt(const std::vector<GanttSegment>& segments, long clock,
                        bool cpuIdleWithPendingWork);

}  // namespace sim

#endif  // SIM_GANTT_HPP
