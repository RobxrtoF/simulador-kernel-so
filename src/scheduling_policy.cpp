/**
 * @file scheduling_policy.cpp
 * @brief Implementación de Round Robin y de las operaciones de despacho/desalojo.
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
// Fábrica
// ----------------------------------------------------------------------------

std::unique_ptr<SchedulingPolicy> makePolicy(PolicyKind kind, int quantum) {
    (void)kind;  // por ahora solo existe Round Robin
    return std::make_unique<RoundRobinPolicy>(quantum);
}

}  // namespace sim
