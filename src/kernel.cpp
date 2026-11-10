/**
 * @file kernel.cpp
 * @brief Implementación del estado compartido y de su sincronización.
 *
 * Cada método público abre una sección crítica con MutexGuard
 * (pthread_mutex_lock al entrar, pthread_mutex_unlock al salir).
 */
#include "kernel.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "sync.hpp"

namespace sim {

Kernel::Kernel() {
    int rc = pthread_mutex_init(&mutex_, nullptr);
    if (rc != 0) throw std::runtime_error(std::string("pthread_mutex_init: ") + std::strerror(rc));
    rc = pthread_cond_init(&wakeEngine_, nullptr);
    if (rc != 0) {
        pthread_mutex_destroy(&mutex_);
        throw std::runtime_error(std::string("pthread_cond_init: ") + std::strerror(rc));
    }
}

Kernel::~Kernel() {
    pthread_cond_destroy(&wakeEngine_);
    pthread_mutex_destroy(&mutex_);
}

// ============================ API del Hilo CLI ============================

PCB Kernel::createProcess(const std::string& name, long burst, int priority) {
    MutexGuard lock(mutex_);
    const PCB created = sim_.createProcess(name, burst, priority);
    pthread_cond_signal(&wakeEngine_);  // si el motor esperaba trabajo, ya lo hay
    return created;
}

std::vector<PCB> Kernel::loadProcesses(const std::vector<ProcessSpec>& specs, long& baseTick) {
    MutexGuard lock(mutex_);
    baseTick = sim_.clock();
    std::vector<int> pids;
    pids.reserve(specs.size());
    for (const ProcessSpec& spec : specs) {
        pids.push_back(sim_.addProcess(spec.name, spec.burst, spec.priority, baseTick + spec.arrival).pid);
    }
    sim_.admitArrivals();  // los que llegan "ahora" pasan a LISTO de inmediato

    std::vector<PCB> created;
    created.reserve(pids.size());
    for (int pid : pids) created.push_back(*sim_.table().find(pid));
    pthread_cond_signal(&wakeEngine_);
    return created;
}

void Kernel::setPolicy(PolicyKind kind, int quantum) {
    MutexGuard lock(mutex_);
    sim_.setPolicy(kind, quantum);
}

void Kernel::setSpeed(int msPerTick) {
    MutexGuard lock(mutex_);
    msPerTick_ = std::max(1, msPerTick);
}

bool Kernel::start() {
    MutexGuard lock(mutex_);
    if (running_) return false;
    running_ = true;
    pthread_cond_signal(&wakeEngine_);  // el motor puede estar dormido en pthread_cond_wait
    return true;
}

bool Kernel::pause() {
    MutexGuard lock(mutex_);
    if (!running_) return false;
    running_ = false;  // el motor lo verá antes de su próximo tick
    return true;
}

void Kernel::requestShutdown() {
    MutexGuard lock(mutex_);
    shutdown_ = true;
    running_ = false;
    pthread_cond_broadcast(&wakeEngine_);
}

bool Kernel::findProcess(int pid, PCB& out) const {
    MutexGuard lock(mutex_);
    const PCB* pcb = sim_.table().find(pid);
    if (pcb == nullptr) return false;
    out = *pcb;
    return true;
}

SystemSnapshot Kernel::snapshot() const {
    MutexGuard lock(mutex_);
    SystemSnapshot snap;
    snap.clock = sim_.clock();
    snap.engineRunning = running_;
    snap.msPerTick = msPerTick_;
    snap.policy = sim_.policy().describe();
    snap.runningPid = sim_.runningPid();
    snap.cpuIdleWithPendingWork = sim_.cpuIdleWithPendingWork();
    snap.processes = sim_.table().snapshot();
    snap.readyQueue.assign(sim_.readyQueue().begin(), sim_.readyQueue().end());
    snap.gantt = sim_.gantt().segments();
    snap.stats = sim_.stats();
    return snap;
}

// ============================ API del Hilo Motor ============================

bool Kernel::runNextTick() {
    MutexGuard lock(mutex_);
    // Mientras esté en pausa o sin trabajo, el motor duerme (pthread_cond_wait libera
    // el mutex mientras espera y lo vuelve a tomar al despertar).
    while (!shutdown_ && (!running_ || !sim_.hasUnfinishedWork())) {
        pthread_cond_wait(&wakeEngine_, &mutex_);
    }
    if (shutdown_) return false;
    sim_.step();  // un tick completo de CPU, de forma atómica respecto a la CLI
    return true;
}

void Kernel::sleepOneTick() {
    long elapsedMs = 0;
    for (;;) {
        long targetMs;
        {
            MutexGuard lock(mutex_);
            if (shutdown_ || !running_) return;  // pausa o apagado: no seguir esperando
            targetMs = msPerTick_;               // set_speed se aplica de inmediato
        }
        if (elapsedMs >= targetMs) return;
        const long sliceMs = std::min<long>(kSleepSliceMs, targetMs - elapsedMs);
        usleep(static_cast<useconds_t>(sliceMs * 1000));  // retardo real SIN el mutex
        elapsedMs += sliceMs;
    }
}

// ================================== Cierre ==================================

std::size_t Kernel::releaseAll() {
    MutexGuard lock(mutex_);
    return sim_.releaseAll();
}

}  // namespace sim
