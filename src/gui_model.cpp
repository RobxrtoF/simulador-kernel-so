/**
 * @file gui_model.cpp
 * @brief Cálculos de la vista de la interfaz gráfica (sin dependencias gráficas).
 */
#include "gui_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sim {
namespace gui {

// ------------------------------------------------------------- velocidad

int speedFromSlider(double position) {
    const double t = std::clamp(position, 0.0, 1.0);
    const double ratio = static_cast<double>(kMaxSpeedMs) / kMinSpeedMs;
    const double ms = kMinSpeedMs * std::pow(ratio, t);
    // Paso del redondeo: fino en velocidades altas, más grueso en las lentas.
    const double step = ms < 200.0 ? 10.0 : (ms < 1000.0 ? 50.0 : 100.0);
    const int rounded = static_cast<int>(std::lround(ms / step) * step);
    return std::clamp(rounded, kMinSpeedMs, kMaxSpeedMs);
}

double sliderFromSpeed(int msPerTick) {
    const double ms = std::clamp(msPerTick, kMinSpeedMs, kMaxSpeedMs);
    const double ratio = static_cast<double>(kMaxSpeedMs) / kMinSpeedMs;
    return std::log(ms / kMinSpeedMs) / std::log(ratio);
}

// ------------------------------------------------------------------ Gantt

GanttViewport ganttViewport(double totalTicks, double widthPx, double panTicks) {
    GanttViewport view{};
    const double width = std::max(widthPx, 1.0);
    const double shown = std::max(totalTicks, kMinTicksShown);
    view.pixelsPerTick = std::clamp(width / shown, kMinPixelsPerTick, kMaxPixelsPerTick);
    view.visibleTicks = width / view.pixelsPerTick;
    view.maxPan = std::max(0.0, totalTicks - view.visibleTicks);
    const double pan = std::clamp(panTicks, 0.0, view.maxPan);
    view.firstTick = view.maxPan - pan;  // sin desplazamiento, la vista termina en el último tick
    return view;
}

long ganttTickStep(double pixelsPerTick, double minLabelSpacingPx) {
    if (pixelsPerTick <= 0.0) return 1;
    static const long kMultipliers[] = {1, 2, 5};
    for (long magnitude = 1; magnitude <= 1000000000L; magnitude *= 10) {
        for (long multiplier : kMultipliers) {
            const long step = multiplier * magnitude;
            if (static_cast<double>(step) * pixelsPerTick >= minLabelSpacingPx) return step;
        }
    }
    return 1000000000L;
}

std::vector<GanttBar> visibleBars(const std::vector<GanttSegment>& segments, double visualClock,
                                  double firstTick, double lastTick) {
    std::vector<GanttBar> bars;
    for (const GanttSegment& segment : segments) {
        const double start = static_cast<double>(segment.start);
        const double end = std::min(static_cast<double>(segment.end), visualClock);
        if (end <= start) continue;                      // todavía no se "revela"
        if (end < firstTick || start > lastTick) continue;  // fuera de la ventana visible
        bars.push_back(GanttBar{start, end, segment.pid, segment.isIdle() ? "IDLE" : segment.label,
                                segment.start, segment.end});
    }
    return bars;
}

// ------------------------------------------------------------ animaciones

double advanceVisualClock(double visual, long clock, bool engineRunning, int msPerTick, double dtSeconds) {
    const double target = static_cast<double>(clock);
    if (visual >= target) return target;                // nunca por delante del reloj real
    if (target - visual > 1.0) visual = target - 1.0;   // ni más de un tick por detrás
    // En marcha: un tick visual por cada tick real. En pausa: se pone al día rápido.
    const double ticksPerSecond = engineRunning ? 1000.0 / std::max(msPerTick, 1) : 12.0;
    return std::min(target, visual + ticksPerSecond * std::max(dtSeconds, 0.0));
}

double smoothTowards(double shown, double target, double dtSeconds, double speed) {
    const double factor = 1.0 - std::exp(-speed * std::max(dtSeconds, 0.0));
    const double next = shown + (target - shown) * factor;
    return std::fabs(target - next) < 1e-4 ? target : next;
}

// --------------------------------------------------------------- procesos

double completion(const PCB& pcb) {
    if (pcb.burstTotal <= 0) return 0.0;
    return std::clamp(static_cast<double>(pcb.executedTime()) / static_cast<double>(pcb.burstTotal), 0.0, 1.0);
}

StateCounts countStates(const std::vector<PCB>& processes) {
    StateCounts counts;
    for (const PCB& pcb : processes) {
        switch (pcb.state) {
            case ProcessState::New:        ++counts.created; break;
            case ProcessState::Ready:      ++counts.ready; break;
            case ProcessState::Running:    ++counts.running; break;
            case ProcessState::Blocked:    ++counts.blocked; break;
            case ProcessState::Terminated: ++counts.terminated; break;
        }
    }
    return counts;
}

std::size_t paletteIndex(int pid, std::size_t paletteSize) {
    if (paletteSize == 0) return 0;
    const int index = pid > 0 ? pid - 1 : 0;  // el PID 1 recibe el primer color
    return static_cast<std::size_t>(index) % paletteSize;
}

// ------------------------------------------------------------------ texto

std::string formatAverage(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.2f", value);
    return buffer;
}

std::string ellipsize(const std::string& text, double maxWidth,
                      const std::function<double(const std::string&)>& measure) {
    if (measure(text) <= maxWidth) return text;
    static const std::string kEllipsis = "\xE2\x80\xA6";  // "…" en UTF-8
    std::string cut = text;
    while (!cut.empty()) {
        // Quita un carácter completo: retrocede sobre los bytes de continuación de UTF-8.
        std::size_t last = cut.size() - 1;
        while (last > 0 && (static_cast<unsigned char>(cut[last]) & 0xC0) == 0x80) --last;
        cut.erase(last);
        if (measure(cut + kEllipsis) <= maxWidth) return cut + kEllipsis;
    }
    return measure(kEllipsis) <= maxWidth ? kEllipsis : std::string();
}

}  // namespace gui
}  // namespace sim
