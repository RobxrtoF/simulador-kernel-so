/**
 * @file gantt.cpp
 * @brief Implementación del diagrama de Gantt.
 */
#include "gantt.hpp"

#include <sstream>

namespace sim {

void GanttChart::record(long tick, int pid, const std::string& label) {
    if (!segments_.empty()) {
        GanttSegment& last = segments_.back();
        if (last.pid == pid && last.end == tick) {  // mismo proceso, tick contiguo: se extiende
            last.end = tick + 1;
            return;
        }
    }
    segments_.push_back(GanttSegment{tick, tick + 1, pid, label});
}

std::string renderGantt(const std::vector<GanttSegment>& segments, long clock,
                        bool cpuIdleWithPendingWork) {
    std::ostringstream out;
    out << "DIAGRAMA DE GANTT (0 - " << clock << " ticks):\n";

    bool first = true;
    for (const GanttSegment& segment : segments) {
        if (!first) out << " -> ";
        first = false;
        out << '[' << segment.start << '-' << segment.end << "s: "
            << (segment.isIdle() ? "IDLE" : segment.label) << ']';
    }

    // CPU libre "ahora" con trabajo pendiente: se indica con [IDLE] (sin duración aún).
    const bool lastIsIdle = !segments.empty() && segments.back().isIdle();
    if (cpuIdleWithPendingWork && !lastIsIdle) {
        if (!first) out << " -> ";
        out << "[IDLE]";
        first = false;
    }

    if (first) out << "(sin ejecución registrada)";
    out << '\n';
    return out.str();
}

}  // namespace sim
