/**
 * @file report.cpp
 * @brief Implementación del formato de `ps` y `pcb`.
 */
#include "report.hpp"

#include <algorithm>
#include <sstream>

#include "parser.hpp"

namespace sim {

namespace {

constexpr std::size_t kPidWidth = 8;
constexpr std::size_t kMinNameWidth = 16;
constexpr std::size_t kStateWidth = 14;
constexpr std::size_t kBurstWidth = 18;

/// Tiempo de retorno para mostrar: solo existe cuando el proceso terminó.
std::string turnaroundText(const PCB& pcb) {
    switch (pcb.state) {
        case ProcessState::Terminated: return std::to_string(pcb.turnaroundTime()) + " ticks";
        case ProcessState::Running:    return "N/A (En ejecución)";
        case ProcessState::Ready:      return "N/A (En cola de listos)";
        case ProcessState::Blocked:    return "N/A (Bloqueado)";
        case ProcessState::New:        return "N/A (Aún no llega)";
    }
    return "N/A";
}

/// Tiempo de respuesta para mostrar: existe desde que el proceso usó la CPU por primera vez.
std::string responseText(const PCB& pcb) {
    if (!pcb.hasStarted()) return "N/A (Aún no ha usado la CPU)";
    return std::to_string(pcb.responseTime()) + " ticks";
}

}  // namespace

std::string renderProcessTable(const SystemSnapshot& snapshot) {
    std::ostringstream out;

    if (snapshot.processes.empty()) {
        out << "No hay procesos en el sistema. Use 'create' o 'load' para agregarlos.\n";
    } else {
        std::size_t nameWidth = kMinNameWidth;
        for (const PCB& pcb : snapshot.processes) nameWidth = std::max(nameWidth, displayWidth(pcb.name) + 2);

        const std::string header = padRight("PID", kPidWidth) + padRight("NOMBRE", nameWidth) +
                                   padRight("ESTADO", kStateWidth) + padRight("BURST RESTANTE", kBurstWidth) +
                                   "PRIORIDAD";
        out << header << '\n' << std::string(displayWidth(header), '-') << '\n';
        for (const PCB& pcb : snapshot.processes) {
            out << padRight(std::to_string(pcb.pid), kPidWidth) << padRight(pcb.name, nameWidth)
                << padRight(stateName(pcb.state), kStateWidth)
                << padRight(std::to_string(pcb.burstRemaining) + "s", kBurstWidth) << pcb.priority << '\n';
        }
    }

    // Contexto: reloj, política, motor y cola de LISTOS (en orden de atención).
    out << "\nReloj: t=" << snapshot.clock << " | Política: " << snapshot.policy
        << " | Motor: " << (snapshot.engineRunning ? "EN EJECUCIÓN" : "EN PAUSA") << " ("
        << snapshot.msPerTick << " ms/tick)\n";
    out << "Cola de LISTOS: ";
    if (snapshot.readyQueue.empty()) {
        out << "(vacía)";
    } else {
        bool first = true;
        for (int pid : snapshot.readyQueue) {
            const auto it = std::find_if(snapshot.processes.begin(), snapshot.processes.end(),
                                         [pid](const PCB& pcb) { return pcb.pid == pid; });
            out << (first ? "" : " -> ") << (it != snapshot.processes.end() ? it->name : std::to_string(pid));
            first = false;
        }
    }
    out << '\n';
    return out.str();
}

std::string renderPcb(const PCB& pcb) {
    const std::string rule(50, '=');
    std::ostringstream out;
    out << rule << '\n'
        << "        CONTROL BLOCK DE PROCESO (PCB) - PID: " << pcb.pid << '\n'
        << rule << '\n'
        << "Nombre del Proceso : " << pcb.name << '\n'
        << "Estado Actual      : " << stateName(pcb.state) << '\n'
        << "Prioridad          : " << pcb.priority << '\n'
        << "Tiempo de Llegada  : " << pcb.arrivalTime << " ticks\n"
        << "Ráfaga Inicial     : " << pcb.burstTotal << " ticks\n"
        << "Ráfaga Restante    : " << pcb.burstRemaining << " ticks\n"
        << "Tiempo de Espera   : " << pcb.waitingTime << " ticks\n"
        << "Tiempo de Retorno  : " << turnaroundText(pcb) << '\n'
        << "Tiempo de Respuesta: " << responseText(pcb) << '\n'
        << rule << '\n';
    return out.str();
}

}  // namespace sim
