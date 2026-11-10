/**
 * @file test_kernel.cpp
 * @brief Pruebas de concurrencia: Kernel compartido + Hilo Motor real (pthread).
 *
 * Se ejecutan también bajo ThreadSanitizer (`make tsan`) para detectar data races.
 */
#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <functional>
#include <string>

#include "engine.hpp"
#include "gantt.hpp"
#include "kernel.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {

/// Lanza el Hilo Motor al construirse y lo detiene (exit + pthread_join) al destruirse.
class EngineThread {
public:
    explicit EngineThread(Kernel& kernel) : kernel_(kernel) {
        pthread_create(&thread_, nullptr, engineThreadMain, &kernel_);
    }
    ~EngineThread() { stop(); }
    void stop() {
        if (joined_) return;
        kernel_.requestShutdown();
        pthread_join(thread_, nullptr);
        joined_ = true;
    }

private:
    Kernel& kernel_;
    pthread_t thread_{};
    bool joined_ = false;
};

/// Espera (consultando el Kernel) a que se cumpla una condición o venza el plazo.
bool waitFor(const Kernel& kernel, const std::function<bool(const SystemSnapshot&)>& condition,
             int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition(kernel.snapshot())) return true;
        usleep(1000);
    }
    return false;
}

bool allFinished(const SystemSnapshot& snap) {
    if (snap.processes.empty()) return false;
    for (const PCB& pcb : snap.processes)
        if (!pcb.isFinished()) return false;
    return true;
}

}  // namespace

TEST_CASE("Kernel + Hilo Motor: el Escenario 1 (RR q=2) termina con el Gantt esperado") {
    Kernel kernel;
    kernel.setSpeed(1);
    kernel.setPolicy(PolicyKind::RoundRobin, 2);
    kernel.createProcess("P1", 5, 2);
    kernel.createProcess("P2", 3, 1);
    kernel.createProcess("P3", 2, 3);
    EngineThread engine(kernel);
    CHECK(kernel.start());
    CHECK(waitFor(kernel, allFinished));
    const SystemSnapshot snap = kernel.snapshot();
    CHECK_EQ(snap.clock, 10L);
    CHECK_EQ(renderGantt(snap.gantt, snap.clock, snap.cpuIdleWithPendingWork),
             "DIAGRAMA DE GANTT (0 - 10 ticks):\n"
             "[0-2s: P1] -> [2-4s: P2] -> [4-6s: P3] -> [6-8s: P1] -> [8-9s: P2] -> [9-10s: P1]\n");
}

TEST_CASE("Kernel: start/pause controlan el reloj y no se puede iniciar dos veces") {
    Kernel kernel;
    kernel.setSpeed(5);
    kernel.createProcess("Largo", 100000, 3);
    EngineThread engine(kernel);
    CHECK(kernel.start());
    CHECK(!kernel.start());
    CHECK(waitFor(kernel, [](const SystemSnapshot& s) { return s.clock >= 3; }));
    CHECK(kernel.pause());
    CHECK(!kernel.pause());
    const long pausedAt = kernel.snapshot().clock;
    usleep(60 * 1000);
    CHECK_EQ(kernel.snapshot().clock, pausedAt);  // en pausa el reloj no avanza
    CHECK(kernel.start());
    CHECK(waitFor(kernel, [pausedAt](const SystemSnapshot& s) { return s.clock > pausedAt; }));
}

TEST_CASE("Kernel: sin procesos el motor duerme y despierta al crear uno") {
    Kernel kernel;
    kernel.setSpeed(1);
    EngineThread engine(kernel);
    CHECK(kernel.start());
    usleep(30 * 1000);
    CHECK_EQ(kernel.snapshot().clock, 0L);  // no hay trabajo: el reloj no avanza
    kernel.createProcess("Tarde", 3, 3);
    CHECK(waitFor(kernel, allFinished));
    CHECK_EQ(kernel.snapshot().clock, 3L);
}

TEST_CASE("Kernel: exit detiene al motor aunque el tick dure 5000 ms") {
    Kernel kernel;
    kernel.setSpeed(5000);
    kernel.createProcess("P", 10, 3);
    EngineThread engine(kernel);
    CHECK(kernel.start());
    CHECK(waitFor(kernel, [](const SystemSnapshot& s) { return s.clock >= 1; }));
    const auto begin = std::chrono::steady_clock::now();
    engine.stop();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
    CHECK(elapsed.count() < 500);
}

TEST_CASE("Kernel: consultas y creaciones concurrentes mientras el motor ejecuta") {
    Kernel kernel;
    kernel.setSpeed(1);
    kernel.setPolicy(PolicyKind::SRTF, 2);
    for (int i = 0; i < 5; ++i) kernel.createProcess("Base" + std::to_string(i), 20, 3);
    EngineThread engine(kernel);
    CHECK(kernel.start());
    long lastClock = 0;
    bool monotonic = true;
    for (int i = 0; i < 300; ++i) {
        const SystemSnapshot snap = kernel.snapshot();
        if (snap.clock < lastClock) monotonic = false;
        lastClock = snap.clock;
        PCB pcb(0, "", 0, 3, 0);
        kernel.findProcess(1 + (i % 5), pcb);
        if (i % 30 == 0) kernel.createProcess("Extra" + std::to_string(i), 2, 1 + (i % 5));
        if (i % 50 == 0) kernel.setPolicy(i % 100 == 0 ? PolicyKind::RoundRobin : PolicyKind::SRTF, 3);
    }
    CHECK(monotonic);
    CHECK(waitFor(kernel, allFinished, 10000));
    const SystemSnapshot snap = kernel.snapshot();
    CHECK_EQ(snap.stats.finishedProcesses, snap.processes.size());
    CHECK_EQ(snap.stats.busyTicks, 5L * 20 + 10L * 2);  // toda la ráfaga se ejecutó una sola vez
}

TEST_CASE("Kernel: releaseAll libera los PCB al cerrar") {
    Kernel kernel;
    kernel.createProcess("A", 1, 3);
    kernel.createProcess("B", 1, 3);
    CHECK_EQ(kernel.releaseAll(), static_cast<std::size_t>(2));
    CHECK(kernel.snapshot().processes.empty());
}
