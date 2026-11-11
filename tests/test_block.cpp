/**
 * @file test_block.cpp
 * @brief Pruebas del estado PAUSADO/BLOQUEADO (comandos block / unblock).
 */
#include <sstream>
#include <string>

#include "cli.hpp"
#include "gantt.hpp"
#include "kernel.hpp"
#include "simulation.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {
void runToCompletion(Simulation& sim, int maxTicks = 1000) {
    while (sim.hasUnfinishedWork() && maxTicks-- > 0) sim.step();
}

std::string ganttLine(const Simulation& sim) {
    const std::string text = renderGantt(sim.gantt().segments(), sim.clock(), sim.cpuIdleWithPendingWork());
    const std::size_t start = text.find('\n') + 1;
    return text.substr(start, text.size() - start - 1);
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
}  // namespace

TEST_CASE("Bloqueo: un proceso RUNNING bloqueado libera la CPU y vuelve al desbloquearse") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.createProcess("P1", 4, 3);
    sim.createProcess("P2", 2, 3);
    sim.step();  // t=0: P1
    std::string error;
    CHECK(sim.blockProcess(1, error));
    CHECK_EQ(std::string(stateName(sim.table().find(1)->state)), "BLOCKED");
    CHECK_EQ(sim.runningPid(), kNoPid);
    sim.step();  // t=1: P2 toma la CPU
    CHECK_EQ(sim.runningPid(), 2);
    CHECK(sim.unblockProcess(1, error));
    CHECK_EQ(std::string(stateName(sim.table().find(1)->state)), "READY");
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim), "[0-1s: P1] -> [1-3s: P2] -> [3-6s: P1]");
    const PCB* p1 = sim.table().find(1);
    CHECK_EQ(p1->finishTime, 6L);
    CHECK_EQ(p1->waitingTime, 1L);  // el tick bloqueado NO cuenta como espera en LISTOS
    CHECK_EQ(p1->turnaroundTime(), 6L);
}

TEST_CASE("Bloqueo: con todos los procesos bloqueados la CPU queda ociosa") {
    Simulation sim;
    sim.createProcess("Unico", 2, 3);
    std::string error;
    CHECK(sim.blockProcess(1, error));
    CHECK(sim.hasUnfinishedWork());
    CHECK(sim.cpuIdleWithPendingWork());
    sim.step();
    sim.step();
    CHECK_EQ(ganttLine(sim), "[0-2s: IDLE]");
    CHECK(sim.unblockProcess(1, error));
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim), "[0-2s: IDLE] -> [2-4s: Unico]");
    CHECK_EQ(sim.stats().cpuUsage(), 50.0);
}

TEST_CASE("Bloqueo: transiciones inválidas se rechazan con un motivo") {
    Simulation sim;
    sim.addProcess("Futuro", 3, 3, 10);
    sim.createProcess("Corto", 1, 3);
    std::string error;
    CHECK(!sim.blockProcess(99, error));
    CHECK_EQ(error, "No existe un proceso con PID 99.");
    CHECK(!sim.blockProcess(1, error));  // NEW: todavía no llega
    CHECK(contains(error, "Solo se pueden bloquear procesos READY o RUNNING (PID 1 está NEW)"));
    CHECK(!sim.unblockProcess(2, error));
    CHECK(contains(error, "no está bloqueado"));
    sim.step();  // Corto termina
    CHECK(!sim.blockProcess(2, error));
    CHECK(contains(error, "TERMINATED"));
}

TEST_CASE("CLI: comandos block y unblock") {
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);
    cli.execute("create P1 3 2");
    out.str("");
    cli.execute("block 1");
    CHECK(contains(out.str(), "[OK]: Proceso PID 1 bloqueado (BLOCKED)."));
    out.str("");
    cli.execute("block 1");
    CHECK(contains(out.str(), "[ERROR DE PARAMETRO]: Solo se pueden bloquear procesos READY o RUNNING"));
    out.str("");
    cli.execute("unblock abc");
    CHECK_EQ(out.str(), "[ERROR DE TIPO]: El PID debe ser un número entero positivo.\n");
    out.str("");
    cli.execute("unblock 1");
    CHECK(contains(out.str(), "[OK]: Proceso PID 1 desbloqueado"));
    out.str("");
    cli.execute("ps");
    CHECK(contains(out.str(), "1      P1             READY"));
}
