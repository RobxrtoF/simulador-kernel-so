/**
 * @file test_envejecimiento.cpp
 * @brief Pruebas del envejecimiento opcional de SRTF (solución a la inanición).
 *
 * Con envejecimiento cada N ticks, SRTF compara la ráfaga efectiva
 * (ráfaga restante - espera / N). Desactivado por defecto: el Escenario 2 del
 * enunciado debe dar exactamente el mismo Gantt.
 */
#include <cmath>
#include <sstream>
#include <string>

#include "batch_loader.hpp"
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
    const std::string text = renderGantt(sim.gantt().segments(), sim.clock(), false);
    const std::size_t start = text.find('\n') + 1;
    return text.substr(start, text.size() - start - 1);
}

bool near(double actual, double expected) { return std::fabs(actual - expected) < 1e-9; }

bool loadWorkload(Simulation& sim, const std::string& path) {
    const LoadResult trace = parseWorkloadFile(path);
    if (!trace.ok()) return false;
    for (const ProcessSpec& spec : trace.processes) sim.addProcess(spec.name, spec.burst, spec.priority, spec.arrival);
    sim.admitArrivals();
    return true;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

/// Ejecuta una línea en la shell y devuelve exactamente lo que imprimió.
std::string runCommand(Cli& cli, std::ostringstream& out, const std::string& line) {
    out.str("");
    out.clear();
    cli.execute(line);
    return out.str();
}

}  // namespace

TEST_CASE("Envejecimiento: desactivado por defecto, SRTF conserva su descripción") {
    Simulation sim;
    CHECK_EQ(sim.agingInterval(), kAgingOff);
    sim.setPolicy(PolicyKind::SRTF);
    CHECK_EQ(sim.policy().describe(), "SRTF (apropiativo)");
}

TEST_CASE("Envejecimiento: la ráfaga efectiva baja 1 tick por cada N ticks de espera") {
    SrtfPolicy aging(3);
    PCB pcb(1, "P", 6, 3, 0);
    CHECK_EQ(aging.effectiveRemaining(pcb), 6L);
    pcb.waitingTime = 2;
    CHECK_EQ(aging.effectiveRemaining(pcb), 6L);  // aún no completa 3 ticks
    pcb.waitingTime = 3;
    CHECK_EQ(aging.effectiveRemaining(pcb), 5L);
    pcb.waitingTime = 9;
    CHECK_EQ(aging.effectiveRemaining(pcb), 3L);
    CHECK_EQ(SrtfPolicy().effectiveRemaining(pcb), 6L);  // sin envejecimiento: la restante
}

TEST_CASE("Envejecimiento: se conserva al cambiar de política y se describe en SRTF") {
    Simulation sim;
    sim.setAging(3);                                   // con RR activo: queda guardado
    CHECK_EQ(sim.policy().describe(), "RR (quantum = 2)");
    sim.setPolicy(PolicyKind::SRTF);
    CHECK_EQ(sim.policy().describe(), "SRTF (apropiativo, envejecimiento cada 3 ticks)");
    sim.setAging(1);                                   // en caliente, con SRTF activo
    CHECK_EQ(sim.policy().describe(), "SRTF (apropiativo, envejecimiento cada tick)");
    sim.setAging(0);
    CHECK_EQ(sim.policy().describe(), "SRTF (apropiativo)");
}

TEST_CASE("Envejecimiento cada 2 ticks: Largo obtiene la CPU en t=8 en la traza de inanición") {
    Simulation sim;
    sim.setAging(2);
    sim.setPolicy(PolicyKind::SRTF);
    CHECK(loadWorkload(sim, "workloads/inanicion_srtf.txt"));

    // En t=8 Largo lleva 8 ticks en LISTOS: 6 - 8/2 = 2, empata con Corto_5 (2)
    // y gana por llegada (t=0 < t=8).
    for (int t = 0; t < 8; ++t) sim.step();
    CHECK_EQ(sim.runningPid(), 1);
    CHECK_EQ(sim.table().find(1)->waitingTime, 8L);

    runToCompletion(sim);
    // Largo ejecuta 6 ticks seguidos: su espera acumulada no se pierde, así que
    // los cortos que siguen llegando no lo desalojan.
    CHECK_EQ(ganttLine(sim),
             "[0-2s: Corto_1] -> [2-4s: Corto_2] -> [4-6s: Corto_3] -> [6-8s: Corto_4] -> "
             "[8-14s: Largo] -> [14-16s: Corto_5] -> [16-18s: Corto_6] -> [18-20s: Corto_7] -> "
             "[20-22s: Corto_8]");
    const PCB* largo = sim.table().find(1);
    CHECK_EQ(largo->responseTime(), 8L);
    CHECK_EQ(largo->turnaroundTime(), 14L);
    for (int pid = 6; pid <= 9; ++pid) CHECK_EQ(sim.table().find(pid)->waitingTime, 6L);  // Corto_5..8

    // Precio de la equidad: la espera máxima baja de 16 a 8 ticks, pero la
    // espera media sube (SRTF puro es óptimo en espera media).
    const SystemStats stats = sim.stats();
    CHECK(near(stats.avgWaiting, 32.0 / 9.0));     // 3,56 (SRTF puro: 1,78)
    CHECK(near(stats.avgTurnaround, 54.0 / 9.0));  // 6,00 (SRTF puro: 4,22)
    CHECK_EQ(stats.busyTicks, 22L);
}

namespace {
std::string scenario2Gantt(int agingInterval) {
    Simulation sim;
    sim.setAging(agingInterval);
    sim.setPolicy(PolicyKind::SRTF);
    sim.addProcess("Job_A", 4, 2, 0);
    sim.addProcess("Job_B", 2, 1, 1);
    sim.addProcess("Job_C", 1, 3, 3);
    sim.admitArrivals();
    runToCompletion(sim);
    return ganttLine(sim);
}
}  // namespace

TEST_CASE("Envejecimiento: por qué va desactivado por defecto (Escenario 2)") {
    const std::string expected = "[0-1s: Job_A] -> [1-3s: Job_B] -> [3-4s: Job_C] -> [4-7s: Job_A]";
    CHECK_EQ(scenario2Gantt(kAgingOff), expected);  // el Gantt exacto que exige el enunciado
    CHECK_EQ(scenario2Gantt(2), expected);          // un envejecimiento moderado no lo cambia
    CHECK_EQ(scenario2Gantt(5), expected);
    // Cada 1 tick ya es otra política: en t=3 Job_A (3 - 2 = 1) empata con Job_C (1)
    // y gana por prioridad (2 < 3). Por eso SRTF puro es el valor por defecto.
    CHECK_EQ(scenario2Gantt(1), "[0-1s: Job_A] -> [1-3s: Job_B] -> [3-6s: Job_A] -> [6-7s: Job_C]");
}

TEST_CASE("CLI set_aging: validación estricta y mensajes") {
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);

    CHECK_EQ(runCommand(cli, out, "set_aging 2"),
             "[OK]: Envejecimiento activado: por cada 2 ticks de espera, la ráfaga efectiva del proceso baja 1 tick."
             " Se aplica con SRTF (política actual: RR (quantum = 2)).\n");
    CHECK_EQ(runCommand(cli, out, "set_sched SRTF"),
             "[OK]: Política de planificación: SRTF (apropiativo, envejecimiento cada 2 ticks).\n");
    CHECK_EQ(runCommand(cli, out, "set_aging 1"),
             "[OK]: Envejecimiento activado: por cada 1 tick de espera, la ráfaga efectiva del proceso baja 1 tick."
             " Política: SRTF (apropiativo, envejecimiento cada tick).\n");
    CHECK_EQ(runCommand(cli, out, "set_aging 0"),
             "[OK]: Envejecimiento desactivado (SRTF puro). Política: SRTF (apropiativo).\n");
    CHECK_EQ(kernel.snapshot().agingInterval, kAgingOff);

    CHECK_EQ(runCommand(cli, out, "set_aging -1"),
             "[ERROR DE PARAMETRO]: El intervalo de envejecimiento debe ser un entero mayor o igual que 0 "
             "(0 = desactivado).\n");
    CHECK_EQ(runCommand(cli, out, "set_aging dos"),
             "[ERROR DE TIPO]: El intervalo de envejecimiento debe ser un número entero de ticks.\n");
    CHECK(contains(runCommand(cli, out, "set_aging 99999999999999999999"), "está fuera de rango."));
    CHECK(contains(runCommand(cli, out, "set_aging"), "[ERROR DE SINTAXIS]: Parámetros insuficientes."));
    CHECK(contains(runCommand(cli, out, "set_aging 2 3"), "Uso correcto: set_aging <ticks>"));
    CHECK(contains(runCommand(cli, out, "help"), "set_aging <ticks>"));
}
