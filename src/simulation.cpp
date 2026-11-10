/**
 * @file simulation.cpp
 * @brief Implementación del núcleo lógico (sin hilos) del simulador.
 */
#include "simulation.hpp"

#include <algorithm>

namespace sim {

Simulation::Simulation() : policy_(makePolicy(PolicyKind::RoundRobin, kDefaultQuantum)) {}

// ------------------------------------------------------------------- procesos

const PCB& Simulation::createProcess(const std::string& name, long burst, int priority) {
    PCB& pcb = table_.create(name, burst, priority, clock_);  // estado NUEVO
    admitArrivals();                                          // NUEVO -> LISTO
    return pcb;
}

const PCB& Simulation::addProcess(const std::string& name, long burst, int priority, long arrival) {
    return table_.create(name, burst, priority, arrival);  // queda NUEVO hasta su llegada
}

void Simulation::admitArrivals() {
    std::vector<PCB*> arriving;
    for (PCB* pcb : table_.all()) {  // all() viene ordenado por PID
        if (pcb->state == ProcessState::New && pcb->arrivalTime <= clock_) arriving.push_back(pcb);
    }
    // Orden de ingreso a la cola: por instante de llegada y, a igual llegada, por PID.
    std::stable_sort(arriving.begin(), arriving.end(),
                     [](const PCB* a, const PCB* b) { return a->arrivalTime < b->arrivalTime; });
    for (PCB* pcb : arriving) {
        pcb->state = ProcessState::Ready;
        ready_.push_back(pcb->pid);
    }
}

bool Simulation::blockProcess(int pid, std::string& error) {
    PCB* pcb = table_.find(pid);
    if (pcb == nullptr) {
        error = "No existe un proceso con PID " + std::to_string(pid) + ".";
        return false;
    }
    if (pcb->state == ProcessState::Running) {
        runningPid_ = kNoPid;  // libera la CPU; el despachador elige otro en el próximo tick
        pcb->quantumUsed = 0;
        if (pcb->executedTime() == 0) pcb->startTime = kUnset;
    } else if (pcb->state == ProcessState::Ready) {
        ready_.erase(std::remove(ready_.begin(), ready_.end(), pid), ready_.end());
    } else {
        error = "Solo se pueden bloquear procesos READY o RUNNING (PID " + std::to_string(pid) +
                " está " + stateName(pcb->state) + ").";
        return false;
    }
    pcb->state = ProcessState::Blocked;
    return true;
}

bool Simulation::unblockProcess(int pid, std::string& error) {
    PCB* pcb = table_.find(pid);
    if (pcb == nullptr) {
        error = "No existe un proceso con PID " + std::to_string(pid) + ".";
        return false;
    }
    if (pcb->state != ProcessState::Blocked) {
        error = "El proceso PID " + std::to_string(pid) + " no está bloqueado (está " +
                stateName(pcb->state) + ").";
        return false;
    }
    pcb->state = ProcessState::Ready;
    ready_.push_back(pid);
    return true;
}

// -------------------------------------------------------------- planificación

void Simulation::setPolicy(PolicyKind kind, int quantum) {
    policy_ = makePolicy(kind, quantum);
    if (PCB* running = table_.find(runningPid_)) running->quantumUsed = 0;
}

void Simulation::decide() {
    admitArrivals();
    SchedContext ctx{clock_, table_, ready_, runningPid_};
    policy_->decide(ctx);
}

// ---------------------------------------------------------- avance del tiempo

void Simulation::step() {
    // 1) Decisión para el instante actual (llegadas, quantum agotado, desalojo SRTF).
    decide();

    // 2) El proceso en ejecución consume una unidad de ráfaga.
    PCB* current = table_.find(runningPid_);
    if (current != nullptr) {
        --current->burstRemaining;
        ++current->quantumUsed;
        ++busyTicks_;
        gantt_.record(clock_, current->pid, current->name);
    } else {
        gantt_.record(clock_, kNoPid, "IDLE");  // CPU ociosa en este tick
    }

    // 3) Todos los procesos en la cola de LISTOS acumulan un tick de espera.
    for (int pid : ready_) {
        if (PCB* waiting = table_.find(pid)) ++waiting->waitingTime;
    }

    // 4) Avanza el reloj simulado.
    ++clock_;

    // 5) Si se agotó la ráfaga, el proceso FINALIZA en el nuevo instante.
    if (current != nullptr && current->burstRemaining == 0) {
        current->state = ProcessState::Terminated;
        current->finishTime = clock_;
        runningPid_ = kNoPid;
    }

    // 6) Decisión del nuevo instante, para que ps/pcb reflejen el estado vigente.
    if (hasUnfinishedWork()) decide();
}

// ------------------------------------------------------------------ consultas

SystemStats Simulation::stats() const {
    return computeStats(table_.snapshot(), clock_, busyTicks_);
}

std::size_t Simulation::releaseAll() {
    ready_.clear();
    runningPid_ = kNoPid;
    return table_.clear();
}

}  // namespace sim
