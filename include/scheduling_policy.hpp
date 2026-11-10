/**
 * @file scheduling_policy.hpp
 * @brief Políticas de planificación de CPU (patrón Strategy).
 *
 * Política implementada: Round Robin.
 * Cada política solo decide QUÉ proceso ocupa la CPU en un instante dado; el
 * avance del reloj y la contabilidad de tiempos los hace Simulation::step().
 */
#ifndef SIM_SCHEDULING_POLICY_HPP
#define SIM_SCHEDULING_POLICY_HPP

#include <deque>
#include <memory>
#include <string>

#include "pcb.hpp"
#include "process_table.hpp"

namespace sim {

/// Algoritmos de planificación disponibles.
enum class PolicyKind { RoundRobin };

/// Quantum por defecto de Round Robin (ticks).
constexpr int kDefaultQuantum = 2;

/**
 * @brief Vista del estado que una política necesita para decidir.
 *
 * La política nunca toma el mutex: quien la invoca (Simulation, que a su vez
 * es usada por el Kernel con el mutex tomado) garantiza acceso exclusivo.
 */
struct SchedContext {
    long now;                 ///< Instante actual (tick).
    ProcessTable& table;      ///< Todos los procesos del sistema.
    std::deque<int>& ready;   ///< Cola de LISTOS: PIDs en orden de ingreso a la cola.
    int& runningPid;          ///< Proceso en EJECUCIÓN (kNoPid si la CPU está libre).

    /// PCB del proceso en ejecución, o nullptr si la CPU está libre.
    PCB* running() const { return table.find(runningPid); }
};

/**
 * @brief Desaloja al proceso en ejecución: pasa a LISTO y va al final de la cola.
 *
 * Si fue despachado pero no alcanzó a ejecutar ningún tick, su primer uso de
 * CPU todavía no ocurrió, así que se anula su instante de inicio.
 */
void preemptRunning(SchedContext& ctx);

/// Despacha `pid` (que está en la cola de LISTOS) a la CPU: pasa a EJECUCIÓN.
void dispatch(SchedContext& ctx, int pid);

/**
 * @brief Interfaz común de las políticas de planificación.
 *
 * decide() se invoca al inicio de cada tick y justo después de él (para que
 * las consultas muestren el estado vigente). Debe ser idempotente: si nada
 * cambió, repetirla en el mismo instante no altera la decisión.
 */
class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;
    /// Algoritmo que implementa.
    virtual PolicyKind kind() const = 0;
    /// Descripción para el usuario, p. ej. "RR (quantum = 2)".
    virtual std::string describe() const = 0;
    /// Toma la decisión de planificación en el instante ctx.now.
    virtual void decide(SchedContext& ctx) = 0;
};

/**
 * @brief Round Robin: cola FIFO con porciones de `quantum` ticks.
 *
 * - Un proceso que agota su quantum vuelve al final de la cola de LISTOS
 *   (después de los procesos que llegaron en ese mismo instante).
 * - Si agota su quantum y nadie espera, sigue ejecutando con una porción nueva.
 * - Los procesos nuevos no desalojan al que está ejecutando.
 */
class RoundRobinPolicy : public SchedulingPolicy {
public:
    explicit RoundRobinPolicy(int quantum = kDefaultQuantum);
    PolicyKind kind() const override { return PolicyKind::RoundRobin; }
    std::string describe() const override;
    void decide(SchedContext& ctx) override;
    int quantum() const { return quantum_; }

private:
    int quantum_;
};

/// Crea la política indicada (el quantum solo aplica a Round Robin).
std::unique_ptr<SchedulingPolicy> makePolicy(PolicyKind kind, int quantum = kDefaultQuantum);

}  // namespace sim

#endif  // SIM_SCHEDULING_POLICY_HPP
