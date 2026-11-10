/**
 * @file test_round_robin.cpp
 * @brief Pruebas deterministas de la política Round Robin sobre Simulation.
 */
#include <deque>
#include <string>

#include "simulation.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {

/// Avanza la simulación hasta que todos los procesos terminen (con límite de seguridad).
void runToCompletion(Simulation& sim, int maxTicks = 1000) {
    while (sim.hasUnfinishedWork() && maxTicks-- > 0) sim.step();
}

std::string queueToString(const std::deque<int>& queue) {
    std::string text;
    for (int pid : queue) text += (text.empty() ? "" : ",") + std::to_string(pid);
    return text;
}

}  // namespace

TEST_CASE("RR q=2 (Escenario 1): alternancia de procesos cada 2 ticks") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.createProcess("P1", 5, 2);
    sim.createProcess("P2", 3, 1);
    sim.createProcess("P3", 2, 3);
    CHECK_EQ(queueToString(sim.readyQueue()), "1,2,3");

    sim.step();
    CHECK_EQ(sim.runningPid(), 1);   // t=1: P1 sigue (quantum 1/2)
    sim.step();
    CHECK_EQ(sim.runningPid(), 2);   // t=2: P1 agotó su quantum -> P2
    CHECK_EQ(queueToString(sim.readyQueue()), "3,1");
    sim.step();
    sim.step();
    CHECK_EQ(sim.runningPid(), 3);   // t=4: P3
    sim.step();
    sim.step();
    CHECK_EQ(sim.runningPid(), 1);   // t=6: P3 terminó -> P1
    CHECK(sim.table().find(3)->isFinished());
}

TEST_CASE("RR q=2 (Escenario 1): tiempos de fin, espera y respuesta") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.createProcess("P1", 5, 2);
    sim.createProcess("P2", 3, 1);
    sim.createProcess("P3", 2, 3);
    runToCompletion(sim);

    const PCB* p1 = sim.table().find(1);
    const PCB* p2 = sim.table().find(2);
    const PCB* p3 = sim.table().find(3);
    CHECK_EQ(sim.clock(), 10L);
    CHECK_EQ(p1->finishTime, 10L);
    CHECK_EQ(p2->finishTime, 9L);
    CHECK_EQ(p3->finishTime, 6L);
    CHECK_EQ(p1->waitingTime, 5L);
    CHECK_EQ(p2->waitingTime, 6L);
    CHECK_EQ(p3->waitingTime, 4L);
    CHECK_EQ(p1->responseTime(), 0L);
    CHECK_EQ(p2->responseTime(), 2L);
    CHECK_EQ(p3->responseTime(), 4L);
    // Coherencia: espera = retorno - ráfaga (no hubo bloqueos).
    CHECK_EQ(p1->waitingTime, p1->turnaroundTime() - p1->burstTotal);
    CHECK_EQ(p2->waitingTime, p2->turnaroundTime() - p2->burstTotal);
    CHECK_EQ(p3->waitingTime, p3->turnaroundTime() - p3->burstTotal);
}

TEST_CASE("RR: si nadie espera, el proceso sigue con una porción nueva") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.createProcess("Solo", 5, 3);
    for (int i = 0; i < 4; ++i) {
        sim.step();
        CHECK_EQ(sim.runningPid(), 1);
    }
    sim.step();
    CHECK(sim.table().find(1)->isFinished());
    CHECK_EQ(sim.table().find(1)->finishTime, 5L);
    CHECK_EQ(sim.table().find(1)->waitingTime, 0L);
}

TEST_CASE("RR: las llegadas de un instante entran a la cola antes que el proceso desalojado") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.addProcess("P1", 4, 3, 0);
    sim.addProcess("P2", 2, 3, 0);
    sim.addProcess("P3", 1, 3, 2);  // llega justo cuando P1 agota su quantum
    sim.admitArrivals();
    sim.step();
    sim.step();
    CHECK_EQ(sim.runningPid(), 2);
    CHECK_EQ(queueToString(sim.readyQueue()), "3,1");
}

TEST_CASE("RR: los procesos NUEVOS esperan su llegada y la CPU queda ociosa mientras tanto") {
    Simulation sim;
    sim.addProcess("Temprano", 2, 3, 0);
    sim.addProcess("Tarde", 1, 3, 5);
    sim.admitArrivals();
    CHECK_EQ(std::string(stateName(sim.table().find(2)->state)), "NEW");
    runToCompletion(sim);
    const PCB* late = sim.table().find(2);
    CHECK_EQ(sim.table().find(1)->finishTime, 2L);
    CHECK_EQ(late->startTime, 5L);
    CHECK_EQ(late->finishTime, 6L);
    CHECK_EQ(late->waitingTime, 0L);
    CHECK_EQ(late->responseTime(), 0L);
    CHECK(!sim.hasUnfinishedWork());
}

TEST_CASE("RR: create asigna la llegada en el instante actual del reloj") {
    Simulation sim;
    sim.createProcess("A", 3, 3);
    sim.step();
    sim.step();
    const PCB& b = sim.createProcess("B", 1, 3);
    CHECK_EQ(b.arrivalTime, 2L);
    CHECK_EQ(std::string(stateName(b.state)), "READY");
}

TEST_CASE("Simulation: releaseAll libera todos los PCB") {
    Simulation sim;
    sim.createProcess("A", 3, 3);
    sim.createProcess("B", 3, 3);
    CHECK_EQ(sim.releaseAll(), static_cast<std::size_t>(2));
    CHECK_EQ(sim.table().size(), static_cast<std::size_t>(0));
    CHECK(sim.readyQueue().empty());
    CHECK_EQ(sim.runningPid(), kNoPid);
}
