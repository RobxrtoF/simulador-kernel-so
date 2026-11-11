/**
 * @file scheduling_policy.cpp
 * @brief Implementación de Round Robin, SRTF y de las operaciones de despacho/desalojo.
 */
#include "scheduling_policy.hpp"

#include <algorithm>

namespace sim {

void preemptRunning(SchedContext& ctx) {
    PCB* current = ctx.running();
    if (current == nullptr) return;
    current->state = ProcessState::Ready;
    current->quantumUsed = 0;
    if (current->executedTime() == 0) current->startTime = kUnset;  // nunca llegó a ejecutar
    ctx.ready.push_back(current->pid);
    ctx.runningPid = kNoPid;
}

void dispatch(SchedContext& ctx, int pid) {
    auto it = std::find(ctx.ready.begin(), ctx.ready.end(), pid);
    if (it != ctx.ready.end()) ctx.ready.erase(it);

    PCB* process = ctx.table.find(pid);
    if (process == nullptr) return;
    process->state = ProcessState::Running;
    process->quantumUsed = 0;
    if (!process->hasStarted()) process->startTime = ctx.now;  // primer uso de CPU -> respuesta
    ctx.runningPid = pid;
}

// ----------------------------------------------------------------------------
// Round Robin
// ----------------------------------------------------------------------------

RoundRobinPolicy::RoundRobinPolicy(int quantum) : quantum_(quantum > 0 ? quantum : kDefaultQuantum) {}

std::string RoundRobinPolicy::describe() const {
    return "RR (quantum = " + std::to_string(quantum_) + ")";
}

void RoundRobinPolicy::decide(SchedContext& ctx) {
    PCB* current = ctx.running();
    if (current != nullptr && current->quantumUsed >= quantum_) {
        if (!ctx.ready.empty()) {
            preemptRunning(ctx);        // agotó su quantum: al final de la cola de LISTOS
        } else {
            current->quantumUsed = 0;   // nadie espera: recibe una porción nueva
        }
    }
    if (ctx.runningPid == kNoPid && !ctx.ready.empty()) {
        dispatch(ctx, ctx.ready.front());  // FIFO: el primero de la cola
    }
}

// ----------------------------------------------------------------------------
// Shortest Remaining Time First (apropiativo)
// ----------------------------------------------------------------------------

SrtfPolicy::SrtfPolicy(int agingInterval) : agingInterval_(agingInterval > 0 ? agingInterval : kAgingOff) {}

std::string SrtfPolicy::describe() const {
    if (agingInterval_ == kAgingOff) return "SRTF (apropiativo)";
    const std::string every = agingInterval_ == 1 ? "cada tick" : "cada " + std::to_string(agingInterval_) + " ticks";
    return "SRTF (apropiativo, envejecimiento " + every + ")";
}

long SrtfPolicy::effectiveRemaining(const PCB& pcb) const {
    if (agingInterval_ == kAgingOff) return pcb.burstRemaining;
    return pcb.burstRemaining - pcb.waitingTime / agingInterval_;  // -1 por cada N ticks en LISTOS
}

bool SrtfPolicy::goesBefore(const PCB& a, const PCB& b) const {
    const long keyA = effectiveRemaining(a);
    const long keyB = effectiveRemaining(b);
    if (keyA != keyB) return keyA < keyB;
    if (a.priority != b.priority) return a.priority < b.priority;
    if (a.arrivalTime != b.arrivalTime) return a.arrivalTime < b.arrivalTime;
    return a.pid < b.pid;
}

void SrtfPolicy::decide(SchedContext& ctx) {
    if (ctx.ready.empty()) return;  // nadie compite por la CPU

    // Mejor candidato de la cola de LISTOS.
    const PCB* best = nullptr;
    for (int pid : ctx.ready) {
        const PCB* candidate = ctx.table.find(pid);
        if (candidate != nullptr && (best == nullptr || goesBefore(*candidate, *best))) best = candidate;
    }
    if (best == nullptr) return;

    const PCB* current = ctx.running();
    if (current != nullptr) {
        // Sin envejecimiento, la ráfaga efectiva es la restante: SRTF puro.
        const long bestKey = effectiveRemaining(*best);
        const long currentKey = effectiveRemaining(*current);
        const bool shorter = bestKey < currentKey;
        const bool tieButHigherPriority = bestKey == currentKey && best->priority < current->priority;
        if (!shorter && !tieButHigherPriority) return;  // el actual sigue ejecutando
        preemptRunning(ctx);                            // desalojo inmediato
    }
    dispatch(ctx, best->pid);
}

// ----------------------------------------------------------------------------
// Fábrica
// ----------------------------------------------------------------------------

std::unique_ptr<SchedulingPolicy> makePolicy(PolicyKind kind, int quantum, int agingInterval) {
    if (kind == PolicyKind::SRTF) return std::make_unique<SrtfPolicy>(agingInterval);
    return std::make_unique<RoundRobinPolicy>(quantum);
}

}  // namespace sim
