/**
 * @file test_srtf.cpp
 * @brief Pruebas deterministas de SRTF apropiativo y del cambio de política en caliente.
 */
#include <string>

#include "simulation.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {
void runToCompletion(Simulation& sim, int maxTicks = 1000) {
    while (sim.hasUnfinishedWork() && maxTicks-- > 0) sim.step();
}

/// Carga la traza del Escenario 2 del enunciado.
void loadScenario2(Simulation& sim) {
    sim.addProcess("Job_A", 4, 2, 0);
    sim.addProcess("Job_B", 2, 1, 1);
    sim.addProcess("Job_C", 1, 3, 3);
    sim.admitArrivals();
}
}  // namespace

TEST_CASE("SRTF (Escenario 2): desalojo inmediato en t=1 cuando llega Job_B") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    loadScenario2(sim);
    sim.step();  // t=0 ejecuta Job_A; en t=1 llega Job_B (2 < 3 restantes de Job_A)
    CHECK_EQ(sim.clock(), 1L);
    CHECK_EQ(sim.runningPid(), 2);
    const PCB* jobA = sim.table().find(1);
    CHECK_EQ(std::string(stateName(jobA->state)), "READY");
    CHECK_EQ(jobA->burstRemaining, 3L);
}

TEST_CASE("SRTF (Escenario 2): tiempos de fin, espera y respuesta") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    loadScenario2(sim);
    runToCompletion(sim);
    const PCB* a = sim.table().find(1);
    const PCB* b = sim.table().find(2);
    const PCB* c = sim.table().find(3);
    CHECK_EQ(sim.clock(), 7L);
    CHECK_EQ(a->finishTime, 7L);
    CHECK_EQ(b->finishTime, 3L);
    CHECK_EQ(c->finishTime, 4L);
    CHECK_EQ(a->waitingTime, 3L);
    CHECK_EQ(b->waitingTime, 0L);
    CHECK_EQ(c->waitingTime, 0L);
    CHECK_EQ(a->responseTime(), 0L);
    CHECK_EQ(b->responseTime(), 0L);
    CHECK_EQ(c->responseTime(), 0L);
}

TEST_CASE("SRTF: a igual ráfaga restante gana la mayor prioridad (número menor)") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.createProcess("Baja", 3, 4);
    sim.createProcess("Alta", 3, 1);
    sim.step();
    CHECK_EQ(sim.table().find(2)->startTime, 0L);
    CHECK(!sim.table().find(1)->hasStarted());
}

TEST_CASE("SRTF: no desaloja si el recién llegado no es estrictamente mejor") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.createProcess("X", 3, 1);
    sim.step();                       // X: quedan 2
    sim.createProcess("Y", 2, 1);     // misma ráfaga restante y misma prioridad
    sim.step();
    CHECK_EQ(sim.table().find(1)->burstRemaining, 1L);
    CHECK_EQ(sim.table().find(2)->burstRemaining, 2L);
}

TEST_CASE("Cambio en caliente RR -> SRTF a mitad de la ejecución") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.createProcess("P1", 5, 2);
    sim.createProcess("P2", 3, 1);
    sim.createProcess("P3", 2, 3);
    sim.step();
    sim.step();
    sim.step();                       // t=3: P2 ejecutando (le quedan 2)
    sim.setPolicy(PolicyKind::SRTF);
    CHECK_EQ(sim.policy().describe(), "SRTF (apropiativo)");
    runToCompletion(sim);
    CHECK_EQ(sim.table().find(2)->finishTime, 5L);
    CHECK_EQ(sim.table().find(3)->finishTime, 7L);
    CHECK_EQ(sim.table().find(1)->finishTime, 10L);
}

TEST_CASE("Cambio de política: descripción y quantum de RR") {
    Simulation sim;
    CHECK_EQ(sim.policy().describe(), "RR (quantum = 2)");
    sim.setPolicy(PolicyKind::RoundRobin, 4);
    CHECK_EQ(sim.policy().describe(), "RR (quantum = 4)");
    CHECK(sim.policy().kind() == PolicyKind::RoundRobin);
    sim.setPolicy(PolicyKind::SRTF);
    CHECK(sim.policy().kind() == PolicyKind::SRTF);
}

TEST_CASE("SRTF: un proceso que llega con menos ráfaga desaloja al que ya ejecutaba") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.createProcess("Largo", 5, 3);
    sim.step();                        // t=0: Largo ejecuta (le quedan 4)
    sim.createProcess("Medio", 3, 3);  // llega en t=1 con 3 < 4
    sim.step();
    CHECK_EQ(sim.runningPid(), 2);
    CHECK_EQ(sim.table().find(1)->startTime, 0L);  // Largo conserva su primer uso de CPU
    CHECK_EQ(sim.table().find(2)->startTime, 1L);
}

TEST_CASE("SRTF: un proceso desalojado antes de ejecutar su primer tick no registra respuesta") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.createProcess("A", 1, 3);
    sim.createProcess("B", 5, 3);
    sim.step();                        // A termina en t=1 y B queda despachado para t=1
    CHECK_EQ(sim.runningPid(), 2);
    CHECK_EQ(sim.table().find(2)->startTime, 1L);
    sim.createProcess("C", 2, 3);      // llega en t=1, antes de que B ejecute
    sim.step();                        // C (2) desaloja a B (5) sin que B haya ejecutado
    CHECK(!sim.table().find(2)->hasStarted());
    CHECK_EQ(std::string(stateName(sim.table().find(2)->state)), "READY");
    runToCompletion(sim);
    const PCB* b = sim.table().find(2);
    CHECK_EQ(b->startTime, 3L);
    CHECK_EQ(b->finishTime, 8L);
    CHECK_EQ(b->responseTime(), 3L);
    CHECK_EQ(b->waitingTime, 3L);
}
