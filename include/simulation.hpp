/**
 * @file simulation.hpp
 * @brief Núcleo lógico del simulador: procesos, cola de LISTOS, reloj y planificación.
 *
 * Esta clase NO usa hilos ni es thread-safe: el Kernel la envuelve y protege
 * cada acceso con un pthread_mutex_t. Mantenerla separada permite probar los
 * algoritmos de forma determinista, avanzando tick a tick, sin depender del
 * tiempo real.
 *
 * Modelo de tiempo: el reloj es un entero (clock) que avanza +1 por cada tick.
 * El tick t representa el intervalo [t, t+1). En cada step():
 *   1. Se admiten las llegadas del instante t y la política decide quién usa la CPU.
 *   2. El proceso en ejecución consume 1 unidad de ráfaga; los LISTOS acumulan espera.
 *   3. El reloj pasa a t+1; si la ráfaga llegó a 0 el proceso FINALIZA en t+1.
 *   4. Se vuelve a decidir para t+1, de modo que las consultas muestren quién
 *      tiene la CPU en el instante actual.
 */
#ifndef SIM_SIMULATION_HPP
#define SIM_SIMULATION_HPP

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "gantt.hpp"
#include "metrics.hpp"
#include "pcb.hpp"
#include "process_table.hpp"
#include "scheduling_policy.hpp"

namespace sim {

class Simulation {
public:
    /// Arranca con el reloj en 0 y la política Round Robin (quantum 2).
    Simulation();

    // ----------------------------------------------------------------- procesos
    /**
     * @brief Crea un proceso que llega en el instante actual.
     *
     * Se construye su PCB en estado NUEVO y se admite de inmediato a la cola
     * de LISTOS (comando `create`).
     */
    const PCB& createProcess(const std::string& name, long burst, int priority);

    /**
     * @brief Registra un proceso que llegará en el instante `arrival`.
     *
     * Queda en estado NUEVO hasta que el reloj alcance su llegada (carga por lotes).
     */
    const PCB& addProcess(const std::string& name, long burst, int priority, long arrival);

    /// Admite a LISTOS (en orden de llegada y PID) los procesos NUEVOS cuya llegada ya ocurrió.
    void admitArrivals();

    /// Bloquea un proceso LISTO o en EJECUCIÓN. Devuelve false (y el motivo) si no es posible.
    bool blockProcess(int pid, std::string& error);
    /// Devuelve un proceso BLOQUEADO al final de la cola de LISTOS.
    bool unblockProcess(int pid, std::string& error);

    // ------------------------------------------------------------ planificación
    /// Cambia la política activa (en caliente). El proceso en ejecución inicia una porción nueva.
    void setPolicy(PolicyKind kind, int quantum = kDefaultQuantum);
    const SchedulingPolicy& policy() const { return *policy_; }

    /**
     * @brief Activa (ticks > 0) o desactiva (kAgingOff) el envejecimiento de SRTF.
     *
     * El valor se conserva aunque se cambie de política: si la política activa
     * es SRTF se aplica de inmediato; si es Round Robin, al volver a SRTF.
     */
    void setAging(int ticks);
    /// Intervalo de envejecimiento configurado (kAgingOff = SRTF puro).
    int agingInterval() const { return agingInterval_; }

    // -------------------------------------------------------- avance del tiempo
    /// true si existe algún proceso sin terminar (hay trabajo pendiente).
    bool hasUnfinishedWork() const { return table_.hasUnfinished(); }
    /// Ejecuta exactamente un tick de CPU.
    void step();

    // ---------------------------------------------------------------- consultas
    long clock() const { return clock_; }
    int runningPid() const { return runningPid_; }
    const std::deque<int>& readyQueue() const { return ready_; }
    const ProcessTable& table() const { return table_; }
    /// Ticks en los que la CPU estuvo ocupada.
    long busyTicks() const { return busyTicks_; }
    /// Historial de ejecución (diagrama de Gantt).
    const GanttChart& gantt() const { return gantt_; }
    /// Métricas globales calculadas a partir de los PCB.
    SystemStats stats() const;
    /// true si la CPU está libre pero quedan procesos sin terminar.
    bool cpuIdleWithPendingWork() const { return runningPid_ == kNoPid && hasUnfinishedWork(); }

    /// Libera la memoria dinámica de todos los PCB. Devuelve cuántos se liberaron.
    std::size_t releaseAll();

private:
    /// Admite llegadas y deja que la política decida para el instante actual.
    void decide();

    ProcessTable table_;                        ///< Dueña de los PCB.
    std::deque<int> ready_;                     ///< Cola de LISTOS (PIDs).
    std::unique_ptr<SchedulingPolicy> policy_;  ///< Política activa (Strategy).
    GanttChart gantt_;                          ///< Historial de ejecución.
    long busyTicks_ = 0;                        ///< Ticks con CPU ocupada.
    long clock_ = 0;                            ///< Reloj simulado (ticks).
    int runningPid_ = kNoPid;                   ///< Proceso en EJECUCIÓN.
    int agingInterval_ = kAgingOff;             ///< Envejecimiento de SRTF (desactivado por defecto).
};

}  // namespace sim

#endif  // SIM_SIMULATION_HPP
