/**
 * @file report.hpp
 * @brief Formato de salida de los comandos `ps` y `pcb` (según el enunciado).
 *
 * Las funciones reciben COPIAS del estado (SystemSnapshot, PCB), así que se
 * ejecutan sin tener tomado el mutex del Kernel.
 */
#ifndef SIM_REPORT_HPP
#define SIM_REPORT_HPP

#include <string>

#include "kernel.hpp"
#include "pcb.hpp"

namespace sim {

/**
 * @brief Tabla del comando `ps`:
 * @code
 *   PID     NOMBRE          ESTADO        BURST RESTANTE    PRIORIDAD
 *   -----------------------------------------------------------------
 *   1       Proceso_A       RUNNING       3s                2
 * @endcode
 * seguida de una línea con el reloj, la política, el estado del motor y la cola de LISTOS.
 */
std::string renderProcessTable(const SystemSnapshot& snapshot);

/// Bloque del comando `pcb <PID>` con el detalle extendido del Process Control Block.
std::string renderPcb(const PCB& pcb);

}  // namespace sim

#endif  // SIM_REPORT_HPP
