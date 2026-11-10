/**
 * @file pcb.cpp
 * @brief Implementación del Process Control Block.
 */
#include "pcb.hpp"

#include <utility>

namespace sim {

const char* stateName(ProcessState state) {
    switch (state) {
        case ProcessState::New:        return "NEW";
        case ProcessState::Ready:      return "READY";
        case ProcessState::Running:    return "RUNNING";
        case ProcessState::Blocked:    return "BLOCKED";
        case ProcessState::Terminated: return "TERMINATED";
    }
    return "UNKNOWN";
}

PCB::PCB(int newPid, std::string newName, long burst, int newPriority, long arrival)
    : pid(newPid),
      name(std::move(newName)),
      priority(newPriority),
      state(ProcessState::New),
      arrivalTime(arrival),
      burstTotal(burst),
      burstRemaining(burst),
      startTime(kUnset),
      finishTime(kUnset),
      waitingTime(0),
      quantumUsed(0) {}

}  // namespace sim
