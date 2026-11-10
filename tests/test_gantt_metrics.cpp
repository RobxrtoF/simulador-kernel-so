/**
 * @file test_gantt_metrics.cpp
 * @brief Pruebas del diagrama de Gantt y de las métricas, incluidos los escenarios del enunciado.
 */
#include <string>

#include "gantt.hpp"
#include "metrics.hpp"
#include "simulation.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {
void runToCompletion(Simulation& sim, int maxTicks = 1000) {
    while (sim.hasUnfinishedWork() && maxTicks-- > 0) sim.step();
}

/// Solo la línea de tramos del diagrama (sin el encabezado).
std::string ganttLine(const Simulation& sim) {
    const std::string text = renderGantt(sim.gantt().segments(), sim.clock(), sim.cpuIdleWithPendingWork());
    const std::size_t start = text.find('\n') + 1;
    return text.substr(start, text.size() - start - 1);
}
}  // namespace

TEST_CASE("Gantt: ticks consecutivos del mismo proceso se agrupan en un tramo") {
    GanttChart chart;
    chart.record(0, 1, "P1");
    chart.record(1, 1, "P1");
    chart.record(2, 2, "P2");
    chart.record(3, 0, "IDLE");
    chart.record(4, 0, "IDLE");
    chart.record(5, 1, "P1");
    CHECK_EQ(chart.segments().size(), static_cast<std::size_t>(4));
    CHECK_EQ(renderGantt(chart.segments(), 6, false),
             "DIAGRAMA DE GANTT (0 - 6 ticks):\n"
             "[0-2s: P1] -> [2-3s: P2] -> [3-5s: IDLE] -> [5-6s: P1]\n");
}

TEST_CASE("Gantt: formato del ejemplo del enunciado con [IDLE] final") {
    GanttChart chart;
    for (long t = 0; t < 8; ++t) {
        const int pid = (t < 2 || t >= 6) ? 1 : (t < 4 ? 2 : 3);
        chart.record(t, pid, "P" + std::to_string(pid));
    }
    CHECK_EQ(renderGantt(chart.segments(), 8, true),
             "DIAGRAMA DE GANTT (0 - 8 ticks):\n"
             "[0-2s: P1] -> [2-4s: P2] -> [4-6s: P3] -> [6-8s: P1] -> [IDLE]\n");
}

TEST_CASE("Gantt: sin ejecución registrada") {
    CHECK_EQ(renderGantt({}, 0, false), "DIAGRAMA DE GANTT (0 - 0 ticks):\n(sin ejecución registrada)\n");
    CHECK_EQ(renderGantt({}, 0, true), "DIAGRAMA DE GANTT (0 - 0 ticks):\n[IDLE]\n");
}

TEST_CASE("Escenario 2 (SRTF): la secuencia del Gantt coincide exactamente con el enunciado") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.addProcess("Job_A", 4, 2, 0);
    sim.addProcess("Job_B", 2, 1, 1);
    sim.addProcess("Job_C", 1, 3, 3);
    sim.admitArrivals();
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim), "[0-1s: Job_A] -> [1-3s: Job_B] -> [3-4s: Job_C] -> [4-7s: Job_A]");
}

TEST_CASE("Escenario 1 (RR q=2): Gantt completo") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.createProcess("P1", 5, 2);
    sim.createProcess("P2", 3, 1);
    sim.createProcess("P3", 2, 3);
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim),
             "[0-2s: P1] -> [2-4s: P2] -> [4-6s: P3] -> [6-8s: P1] -> [8-9s: P2] -> [9-10s: P1]");
}

TEST_CASE("Escenario 1 con cambio en caliente a SRTF en t=3") {
    Simulation sim;
    sim.setPolicy(PolicyKind::RoundRobin, 2);
    sim.createProcess("P1", 5, 2);
    sim.createProcess("P2", 3, 1);
    sim.createProcess("P3", 2, 3);
    for (int i = 0; i < 3; ++i) sim.step();
    sim.setPolicy(PolicyKind::SRTF);
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim), "[0-2s: P1] -> [2-5s: P2] -> [5-7s: P3] -> [7-10s: P1]");
}

TEST_CASE("Métricas del Escenario 2: promedios y uso de CPU") {
    Simulation sim;
    sim.setPolicy(PolicyKind::SRTF);
    sim.addProcess("Job_A", 4, 2, 0);
    sim.addProcess("Job_B", 2, 1, 1);
    sim.addProcess("Job_C", 1, 3, 3);
    sim.admitArrivals();
    runToCompletion(sim);
    const SystemStats stats = sim.stats();
    CHECK_EQ(stats.finishedProcesses, static_cast<std::size_t>(3));
    CHECK_EQ(stats.avgWaiting, 1.0);             // (3 + 0 + 0) / 3
    CHECK_EQ(stats.avgTurnaround, 10.0 / 3.0);   // (7 + 2 + 1) / 3
    CHECK_EQ(stats.avgResponse, 0.0);
    CHECK_EQ(stats.cpuUsage(), 100.0);
    CHECK_EQ(renderStats(stats),
             "ESTADÍSTICAS DEL SISTEMA:\n"
             "- Procesos Finalizados        : 3 / 3\n"
             "- Tiempo Promedio de Espera   : 1.00 ticks\n"
             "- Tiempo Promedio de Retorno  : 3.33 ticks\n"
             "- Tiempo Promedio de Respuesta: 0.00 ticks\n"
             "- Uso de CPU                  : 100.0%\n"
             "- Tiempo Total Simulado       : 7 ticks\n");
}

TEST_CASE("Métricas: la CPU ociosa reduce el porcentaje de uso") {
    Simulation sim;
    sim.addProcess("A", 2, 3, 0);
    sim.addProcess("B", 1, 3, 5);
    sim.admitArrivals();
    runToCompletion(sim);
    CHECK_EQ(ganttLine(sim), "[0-2s: A] -> [2-5s: IDLE] -> [5-6s: B]");
    CHECK_EQ(sim.busyTicks(), 3L);
    CHECK_EQ(sim.stats().cpuUsage(), 50.0);
}

TEST_CASE("Métricas: sin procesos finalizados los promedios no aplican") {
    Simulation sim;
    sim.createProcess("A", 3, 3);
    sim.step();
    const std::string text = renderStats(sim.stats());
    CHECK(text.find("- Procesos Finalizados        : 0 / 1") != std::string::npos);
    CHECK(text.find("N/A") != std::string::npos);
    CHECK(text.find("- Uso de CPU                  : 100.0%") != std::string::npos);
}
