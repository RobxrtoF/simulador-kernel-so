/**
 * @file test_srtf.cpp
 * @brief Pruebas deterministas de SRTF apropiativo y del cambio de política en caliente.
 */
#include <cmath>
#include <string>

#include "batch_loader.hpp"
#include "gantt.hpp"
#include "simulation.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {
void runToCompletion(Simulation& sim, int maxTicks = 1000) {
    while (sim.hasUnfinishedWork() && maxTicks-- > 0) sim.step();
}

/// Segunda línea de `gantt` (la secuencia de tramos), sin la cabecera.
std::string ganttLine(const Simulation& sim) {
    const std::string text = renderGantt(sim.gantt().segments(), sim.clock(), false);
    const std::size_t start = text.find('\n') + 1;
    return text.substr(start, text.size() - start - 1);
}

bool near(double actual, double expected) { return std::fabs(actual - expected) < 1e-9; }

/// Carga en la simulación una traza de workloads/ (las pruebas corren desde la raíz).
bool loadWorkload(Simulation& sim, const std::string& path) {
    const LoadResult trace = parseWorkloadFile(path);
    if (!trace.ok()) return false;
    for (const ProcessSpec& spec : trace.processes) sim.addProcess(spec.name, spec.burst, spec.priority, spec.arrival);
    sim.admitArrivals();
    return true;
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

// ----------------------------------------------------------------------------
// Ejemplos de la bibliografía del curso: el simulador reproduce sus Gantt.
// ----------------------------------------------------------------------------

TEST_CASE("SRTF reproduce el ejemplo de Silberschatz (7.ª ed., sec. 5.3.2, p. 145)") {
    // P1..P4 llegan en 0, 1, 2, 3 con ráfagas 8, 4, 9, 5. Espera media: 26 / 4 = 6,5.
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.addProcess("P1", 8, 3, 0);
    sim.addProcess("P2", 4, 3, 1);
    sim.addProcess("P3", 9, 3, 2);
    sim.addProcess("P4", 5, 3, 3);
    sim.admitArrivals();
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim), "[0-1s: P1] -> [1-5s: P2] -> [5-10s: P4] -> [10-17s: P1] -> [17-26s: P3]");
    CHECK_EQ(sim.table().find(1)->waitingTime, 9L);   // 10 - 1
    CHECK_EQ(sim.table().find(2)->waitingTime, 0L);   // 1 - 1
    CHECK_EQ(sim.table().find(3)->waitingTime, 15L);  // 17 - 2
    CHECK_EQ(sim.table().find(4)->waitingTime, 2L);   // 5 - 3
    CHECK(near(sim.stats().avgWaiting, 6.5));
}

TEST_CASE("SRTF reproduce la figura 8.5 de Stallings (2.ª ed., «Menor tiempo restante», p. 354)") {
    // Procesos 1..5: llegadas 0, 2, 4, 6, 8 y servicios 3, 6, 4, 5, 2.
    // En t=10 empatan 2 y 4 (5 restantes): gana el que llegó antes.
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.addProcess("P1", 3, 3, 0);
    sim.addProcess("P2", 6, 3, 2);
    sim.addProcess("P3", 4, 3, 4);
    sim.addProcess("P4", 5, 3, 6);
    sim.addProcess("P5", 2, 3, 8);
    sim.admitArrivals();
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim), "[0-3s: P1] -> [3-4s: P2] -> [4-8s: P3] -> [8-10s: P5] -> [10-15s: P2] -> [15-20s: P4]");
    CHECK(near(sim.stats().avgTurnaround, 36.0 / 5.0));  // retornos 3, 13, 4, 14, 2
}

// ----------------------------------------------------------------------------
// Inanición: workloads/inanicion_srtf.txt (cálculo a mano en el README).
// ----------------------------------------------------------------------------

TEST_CASE("Inanición en SRTF: Largo espera mientras sigan llegando procesos cortos") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    CHECK(loadWorkload(sim, "workloads/inanicion_srtf.txt"));
    CHECK_EQ(sim.table().size(), static_cast<std::size_t>(9));

    // En cada llegada, el corto (2) le gana a Largo (6): Largo no ejecuta hasta t=16.
    for (int t = 0; t < 15; ++t) sim.step();
    const PCB* largo = sim.table().find(1);
    CHECK(!largo->hasStarted());     // t=15: Corto_8 todavía ejecuta
    sim.step();
    CHECK_EQ(sim.runningPid(), 1);   // t=16: ya no quedan cortos
    CHECK_EQ(largo->startTime, 16L);
    CHECK_EQ(largo->waitingTime, 16L);

    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim),
             "[0-2s: Corto_1] -> [2-4s: Corto_2] -> [4-6s: Corto_3] -> [6-8s: Corto_4] -> "
             "[8-10s: Corto_5] -> [10-12s: Corto_6] -> [12-14s: Corto_7] -> [14-16s: Corto_8] -> "
             "[16-22s: Largo]");
    CHECK_EQ(largo->responseTime(), 16L);
    CHECK_EQ(largo->turnaroundTime(), 22L);
    for (int pid = 2; pid <= 9; ++pid) CHECK_EQ(sim.table().find(pid)->waitingTime, 0L);

    const SystemStats stats = sim.stats();
    CHECK(near(stats.avgWaiting, 16.0 / 9.0));    // 1,78: solo espera Largo
    CHECK(near(stats.avgTurnaround, 38.0 / 9.0)); // (22 + 8 x 2) / 9 = 4,22
    CHECK_EQ(stats.busyTicks, 22L);
}
