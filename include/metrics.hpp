/**
 * @file metrics.hpp
 * @brief Métricas de rendimiento del planificador.
 *
 * Definiciones (por proceso finalizado, en ticks):
 *   - Retorno     TAT = finalización - llegada
 *   - Espera      WT  = ticks en la cola de LISTOS (= TAT - ráfaga si nunca se bloqueó)
 *   - Respuesta   RT  = primer uso de CPU - llegada
 * Globales:
 *   - Promedios de WT, TAT y RT sobre los procesos finalizados.
 *   - Uso de CPU = ticks con CPU ocupada / ticks simulados * 100.
 */
#ifndef SIM_METRICS_HPP
#define SIM_METRICS_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "pcb.hpp"

namespace sim {

/// Métricas globales del sistema en un instante dado.
struct SystemStats {
    std::size_t totalProcesses = 0;     ///< Procesos registrados.
    std::size_t finishedProcesses = 0;  ///< Procesos en estado FINALIZADO.
    double avgWaiting = 0.0;            ///< Promedio de WT de los finalizados.
    double avgTurnaround = 0.0;         ///< Promedio de TAT de los finalizados.
    double avgResponse = 0.0;           ///< Promedio de RT de los finalizados.
    long clock = 0;                     ///< Ticks simulados hasta ahora.
    long busyTicks = 0;                 ///< Ticks con la CPU ocupada.

    /// Porcentaje de uso de CPU (0 si todavía no ha transcurrido ningún tick).
    double cpuUsage() const { return clock > 0 ? 100.0 * static_cast<double>(busyTicks) / clock : 0.0; }
};

/// Calcula las métricas a partir de los PCB, el reloj y los ticks ocupados.
SystemStats computeStats(const std::vector<PCB>& processes, long clock, long busyTicks);

/**
 * @brief Representación en texto con el formato del enunciado:
 * @code
 *   ESTADÍSTICAS DEL SISTEMA:
 *   - Procesos Finalizados        : 2 / 4
 *   - Tiempo Promedio de Espera   : 2.50 ticks
 *   ...
 * @endcode
 */
std::string renderStats(const SystemStats& stats);

}  // namespace sim

#endif  // SIM_METRICS_HPP
