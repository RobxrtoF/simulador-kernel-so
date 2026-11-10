/**
 * @file metrics.cpp
 * @brief Cálculo y formato de las métricas globales.
 */
#include "metrics.hpp"

#include <cstdio>
#include <sstream>

namespace sim {

SystemStats computeStats(const std::vector<PCB>& processes, long clock, long busyTicks) {
    SystemStats stats;
    stats.totalProcesses = processes.size();
    stats.clock = clock;
    stats.busyTicks = busyTicks;

    long sumWaiting = 0;
    long sumTurnaround = 0;
    long sumResponse = 0;
    for (const PCB& pcb : processes) {
        if (!pcb.isFinished()) continue;
        ++stats.finishedProcesses;
        sumWaiting += pcb.waitingTime;
        sumTurnaround += pcb.turnaroundTime();
        sumResponse += pcb.responseTime();
    }
    if (stats.finishedProcesses > 0) {
        const double n = static_cast<double>(stats.finishedProcesses);
        stats.avgWaiting = static_cast<double>(sumWaiting) / n;
        stats.avgTurnaround = static_cast<double>(sumTurnaround) / n;
        stats.avgResponse = static_cast<double>(sumResponse) / n;
    }
    return stats;
}

namespace {
/// Da formato a un promedio con dos decimales, o "N/A" si no hay procesos finalizados.
std::string formatAverage(double value, bool available) {
    if (!available) return "N/A (ningún proceso ha finalizado)";
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.2f ticks", value);
    return buffer;
}
}  // namespace

std::string renderStats(const SystemStats& stats) {
    const bool available = stats.finishedProcesses > 0;
    char usage[32];
    std::snprintf(usage, sizeof usage, "%.1f%%", stats.cpuUsage());

    std::ostringstream out;
    out << "ESTADÍSTICAS DEL SISTEMA:\n"
        << "- Procesos Finalizados        : " << stats.finishedProcesses << " / " << stats.totalProcesses << '\n'
        << "- Tiempo Promedio de Espera   : " << formatAverage(stats.avgWaiting, available) << '\n'
        << "- Tiempo Promedio de Retorno  : " << formatAverage(stats.avgTurnaround, available) << '\n'
        << "- Tiempo Promedio de Respuesta: " << formatAverage(stats.avgResponse, available) << '\n'
        << "- Uso de CPU                  : " << usage << '\n'
        << "- Tiempo Total Simulado       : " << stats.clock << " ticks\n";
    return out.str();
}

}  // namespace sim
