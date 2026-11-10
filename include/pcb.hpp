/**
 * @file pcb.hpp
 * @brief Process Control Block (PCB) y estados del ciclo de vida de un proceso.
 *
 * El PCB concentra la información que el kernel simulado necesita para
 * administrar un proceso: identidad, prioridad, estado, tiempos del ciclo de
 * vida y contadores que usan los algoritmos de planificación.
 *
 * Todos los tiempos se expresan en *ticks* del reloj simulado (no en tiempo
 * real): un tick equivale a un ciclo de CPU.
 */
#ifndef SIM_PCB_HPP
#define SIM_PCB_HPP

#include <string>

namespace sim {

/**
 * @brief Estados del ciclo de vida de un proceso.
 *
 * | Enum       | Enunciado         | Etiqueta en consola |
 * |------------|-------------------|---------------------|
 * | New        | NUEVO             | NEW                 |
 * | Ready      | LISTO             | READY               |
 * | Running    | EJECUCIÓN         | RUNNING             |
 * | Blocked    | PAUSADO/BLOQUEADO | BLOCKED             |
 * | Terminated | FINALIZADO        | TERMINATED          |
 *
 * Transiciones válidas:
 *   NEW -> READY (llegada), READY -> RUNNING (despacho),
 *   RUNNING -> READY (desalojo), RUNNING -> TERMINATED (ráfaga agotada),
 *   READY/RUNNING -> BLOCKED (bloqueo), BLOCKED -> READY (desbloqueo).
 */
enum class ProcessState { New, Ready, Running, Blocked, Terminated };

/// Etiqueta del estado tal como la muestran los comandos `ps` y `pcb`.
const char* stateName(ProcessState state);

/// Marca de "instante que aún no ocurre" (primer uso de CPU o finalización).
constexpr long kUnset = -1;

/// PID reservado que significa "ningún proceso"; los PID válidos empiezan en 1.
constexpr int kNoPid = 0;

/// Rango de prioridades: 1 es la más alta y 5 la más baja.
constexpr int kMinPriority = 1;
constexpr int kMaxPriority = 5;
constexpr int kDefaultPriority = 3;

/**
 * @brief Process Control Block: toda la información de un proceso.
 */
struct PCB {
    int pid;               ///< Identificador único y autoincremental (desde 1).
    std::string name;      ///< Nombre dado por el usuario (sin espacios).
    int priority;          ///< 1 (más alta) .. 5 (más baja).
    ProcessState state;    ///< Estado actual del ciclo de vida.

    long arrivalTime;      ///< Tick en que el proceso llega al sistema.
    long burstTotal;       ///< Ráfaga inicial de CPU (ticks).
    long burstRemaining;   ///< Ráfaga que le falta por ejecutar (ticks).

    long startTime;        ///< Tick en que obtuvo la CPU por primera vez (kUnset si nunca).
    long finishTime;       ///< Tick en que terminó (kUnset si no ha terminado).
    long waitingTime;      ///< Ticks acumulados esperando en la cola de LISTOS.
    long quantumUsed;      ///< Ticks consumidos en la porción de CPU actual (Round Robin).

    /// Crea un PCB en estado NUEVO, con toda la ráfaga pendiente.
    PCB(int pid, std::string name, long burst, int priority, long arrival);

    /// Ticks de CPU que ya ejecutó.
    long executedTime() const { return burstTotal - burstRemaining; }
    /// true si ya obtuvo la CPU al menos una vez.
    bool hasStarted() const { return startTime != kUnset; }
    /// true si está en estado FINALIZADO.
    bool isFinished() const { return state == ProcessState::Terminated; }

    /// Tiempo de retorno: TAT = finalización - llegada. Solo válido si terminó.
    long turnaroundTime() const { return finishTime - arrivalTime; }
    /// Tiempo de respuesta: RT = primer uso de CPU - llegada. Solo válido si inició.
    long responseTime() const { return startTime - arrivalTime; }
};

}  // namespace sim

#endif  // SIM_PCB_HPP
