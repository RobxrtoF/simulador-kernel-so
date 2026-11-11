/**
 * @file kernel.hpp
 * @brief Estado compartido entre el Hilo CLI y el Hilo Motor, protegido con pthread_mutex_t.
 *
 * El Kernel envuelve a Simulation (procesos, cola de LISTOS, reloj, Gantt) y a
 * las banderas de control del motor (en ejecución, apagado, velocidad).
 * Todo acceso a ese estado pasa por métodos del Kernel que toman `mutex_` con
 * pthread_mutex_lock / pthread_mutex_unlock (mediante MutexGuard), así que
 * ningún hilo lee o modifica los procesos sin exclusión mutua.
 *
 * Además usa una variable de condición (pthread_cond_t) para que el motor
 * duerma sin consumir CPU mientras está en pausa o no hay procesos pendientes.
 *
 * Reglas de diseño:
 *  - El retardo real de cada tick (usleep) se hace SIN el mutex tomado, para
 *    que la CLI responda de inmediato aunque el reloj sea lento.
 *  - Las consultas devuelven COPIAS (SystemSnapshot, PCB) para formatear la
 *    salida sin retener el mutex.
 */
#ifndef SIM_KERNEL_HPP
#define SIM_KERNEL_HPP

#include <pthread.h>

#include <cstddef>
#include <string>
#include <vector>

#include "batch_loader.hpp"
#include "gantt.hpp"
#include "metrics.hpp"
#include "pcb.hpp"
#include "scheduling_policy.hpp"
#include "simulation.hpp"

namespace sim {

/// Copia consistente del estado del sistema, tomada en una sola sección crítica.
struct SystemSnapshot {
    long clock = 0;                       ///< Instante actual (ticks).
    bool engineRunning = false;           ///< true si el reloj de CPU está corriendo.
    int msPerTick = 0;                    ///< Duración real de cada tick.
    std::string policy;                   ///< Descripción de la política activa.
    PolicyKind policyKind = PolicyKind::RoundRobin;  ///< Algoritmo activo.
    int agingInterval = kAgingOff;        ///< Envejecimiento de SRTF configurado (ticks).
    int runningPid = kNoPid;              ///< Proceso en EJECUCIÓN.
    bool cpuIdleWithPendingWork = false;  ///< CPU libre con procesos sin terminar.
    std::vector<PCB> processes;           ///< Todos los PCB, ordenados por PID.
    std::vector<int> readyQueue;          ///< Cola de LISTOS (PIDs, en orden).
    std::vector<GanttSegment> gantt;      ///< Historial de ejecución.
    SystemStats stats;                    ///< Métricas globales.
};

class Kernel {
public:
    static constexpr int kDefaultMsPerTick = 1000;  ///< Velocidad por defecto (1 s por tick).

    Kernel();
    ~Kernel();
    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    // ===================== API usada por el Hilo CLI =====================

    /// Crea un proceso que llega en el instante actual (NUEVO -> LISTO). Devuelve una copia del PCB.
    PCB createProcess(const std::string& name, long burst, int priority);
    /**
     * @brief Inserta en bloque los procesos de un archivo de traza.
     *
     * Las llegadas son relativas al instante de la carga: un proceso con
     * ARRIVAL_TIME = a llega en baseTick + a. Devuelve copias de los PCB creados.
     */
    std::vector<PCB> loadProcesses(const std::vector<ProcessSpec>& specs, long& baseTick);
    /// Pasa un proceso LISTO o en EJECUCIÓN a BLOQUEADO.
    bool blockProcess(int pid, std::string& error);
    /// Devuelve un proceso BLOQUEADO a la cola de LISTOS.
    bool unblockProcess(int pid, std::string& error);
    /// Cambia la política de planificación en caliente.
    void setPolicy(PolicyKind kind, int quantum);
    /// Activa (ticks > 0) o desactiva (kAgingOff) el envejecimiento de SRTF, en caliente.
    void setAging(int ticks);
    /// Cambia la duración real de cada tick (la CLI valida el rango 50-5000 ms).
    void setSpeed(int msPerTick);
    /// Inicia o reanuda el reloj de CPU. false si ya estaba en ejecución.
    bool start();
    /// Pausa el reloj de CPU. false si ya estaba en pausa.
    bool pause();
    /// Pide al motor que termine (lo despierta si está esperando).
    void requestShutdown();
    /// Copia del PCB de `pid`. false si no existe.
    bool findProcess(int pid, PCB& out) const;
    /// Copia consistente de todo el estado (para ps, gantt, stats).
    SystemSnapshot snapshot() const;

    // ==================== API usada por el Hilo Motor ====================

    /**
     * @brief Espera hasta que haya que ejecutar un tick y lo ejecuta.
     *
     * Bloquea al hilo en pthread_cond_wait mientras el reloj esté en pausa o no
     * haya procesos pendientes. Devuelve false cuando se pidió el apagado.
     */
    bool runNextTick();

    /**
     * @brief Retardo real de un tick (usleep por tramos cortos, sin el mutex).
     *
     * Termina antes si se pausa o se apaga el sistema, y aplica de inmediato
     * un cambio de velocidad hecho con set_speed. La duración se mide con un
     * reloj monótono, así que el tick dura lo configurado aunque usleep se
     * alargue.
     */
    void sleepOneTick();

    // ================= Cierre (tras pthread_join de ambos hilos) =================

    /// Libera la memoria dinámica de todos los PCB. Devuelve cuántos se liberaron.
    std::size_t releaseAll();

private:
    static constexpr int kSleepSliceMs = 10;  ///< Tramo máximo de cada usleep.

    mutable pthread_mutex_t mutex_;  ///< Protege TODO el estado que sigue.
    pthread_cond_t wakeEngine_;      ///< Despierta al motor (start, create, load, exit...).
    Simulation sim_;                 ///< Procesos, colas, reloj y Gantt.
    bool running_ = false;           ///< Reloj de CPU en marcha (start/pause).
    bool shutdown_ = false;          ///< Se pidió terminar la simulación (exit).
    int msPerTick_ = kDefaultMsPerTick;
};

}  // namespace sim

#endif  // SIM_KERNEL_HPP
