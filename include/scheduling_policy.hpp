/**
 * @file scheduling_policy.hpp
 * @brief Políticas de planificación de CPU (patrón Strategy).
 *
 * Políticas implementadas: Round Robin y Shortest Remaining Time First (SRTF).
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
enum class PolicyKind { RoundRobin, SRTF };

/// Quantum por defecto de Round Robin (ticks).
constexpr int kDefaultQuantum = 2;

/// Envejecimiento de SRTF desactivado (SRTF puro). Es el valor por defecto.
constexpr int kAgingOff = 0;

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

/**
 * @brief Shortest Remaining Time First (apropiativo).
 *
 * Siempre ejecuta el proceso con MENOR ráfaga restante. Cuando llega (o se
 * desbloquea) un proceso con ráfaga restante estrictamente menor que la del
 * proceso en ejecución, este es desalojado de inmediato.
 *
 * Desempates (criterio "SRTF / Prioridades apropiativo"):
 *   1) menor ráfaga restante, 2) mayor prioridad (número menor),
 *   3) llegada más temprana, 4) PID menor.
 * Solo se desaloja al proceso en ejecución si el candidato tiene menos ráfaga
 * restante, o igual ráfaga y mejor prioridad (evita cambios de contexto inútiles).
 *
 * Inanición y envejecimiento (opcional, desactivado por defecto):
 * SRTF es un caso particular de la planificación por prioridades en el que la
 * "prioridad" es la ráfaga restante (Silberschatz, cap. 5.3.3). Por eso un
 * proceso largo puede quedar postergado indefinidamente si siguen llegando
 * procesos cortos (inanición). Con envejecimiento cada N ticks, la política
 * compara la RÁFAGA EFECTIVA:
 *
 *     ráfaga efectiva = ráfaga restante - (ticks esperando en LISTOS / N)
 *
 * (división entera). Es la técnica del libro, "aumentar gradualmente la
 * prioridad de los procesos que estén esperando en el sistema durante mucho
 * tiempo" (p. 146), aplicada a la clave de SRTF: cada N ticks de espera el
 * proceso "parece" un tick más corto. Como la
 * espera acumulada no se pierde al obtener la CPU, el proceso envejecido no es
 * desalojado en cadena por los cortos que siguen llegando.
 * La ráfaga restante real, el Gantt y las métricas no se alteran: solo cambia
 * el orden de selección. Con N = kAgingOff la política es SRTF puro.
 */
class SrtfPolicy : public SchedulingPolicy {
public:
    /// @param agingInterval Ticks de espera por cada tick que se descuenta (kAgingOff = SRTF puro).
    explicit SrtfPolicy(int agingInterval = kAgingOff);
    PolicyKind kind() const override { return PolicyKind::SRTF; }
    std::string describe() const override;
    void decide(SchedContext& ctx) override;

    /// Intervalo de envejecimiento en ticks (kAgingOff si está desactivado).
    int agingInterval() const { return agingInterval_; }
    /// Ráfaga con la que compite el proceso: la restante, menos el bono de envejecimiento.
    long effectiveRemaining(const PCB& pcb) const;
    /// true si `a` debe ejecutarse antes que `b` según SRTF (ráfaga efectiva) y sus desempates.
    bool goesBefore(const PCB& a, const PCB& b) const;

private:
    int agingInterval_;
};

/// Crea la política indicada (el quantum solo aplica a RR y el envejecimiento solo a SRTF).
std::unique_ptr<SchedulingPolicy> makePolicy(PolicyKind kind, int quantum = kDefaultQuantum,
                                             int agingInterval = kAgingOff);

}  // namespace sim

#endif  // SIM_SCHEDULING_POLICY_HPP
