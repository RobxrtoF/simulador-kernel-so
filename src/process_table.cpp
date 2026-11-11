/**
 * @file process_table.cpp
 * @brief Implementación de la tabla de procesos.
 */
#include "process_table.hpp"

#include <utility>

namespace sim {

PCB& ProcessTable::create(const std::string& name, long burst, int priority, long arrival) {
    const int pid = nextPid_++;
    auto pcb = std::make_unique<PCB>(pid, name, burst, priority, arrival);
    PCB& ref = *pcb;
    pcbs_.emplace(pid, std::move(pcb));
    return ref;
}

PCB* ProcessTable::find(int pid) {
    auto it = pcbs_.find(pid);
    return it == pcbs_.end() ? nullptr : it->second.get();
}

const PCB* ProcessTable::find(int pid) const {
    auto it = pcbs_.find(pid);
    return it == pcbs_.end() ? nullptr : it->second.get();
}

std::vector<PCB*> ProcessTable::all() {
    std::vector<PCB*> result;
    result.reserve(pcbs_.size());
    for (auto& entry : pcbs_) result.push_back(entry.second.get());
    return result;
}

std::vector<const PCB*> ProcessTable::all() const {
    std::vector<const PCB*> result;
    result.reserve(pcbs_.size());
    for (const auto& entry : pcbs_) result.push_back(entry.second.get());
    return result;
}

std::vector<PCB> ProcessTable::snapshot() const {
    std::vector<PCB> result;
    result.reserve(pcbs_.size());
    for (const auto& entry : pcbs_) result.push_back(*entry.second);
    return result;
}

std::size_t ProcessTable::countFinished() const {
    std::size_t finished = 0;
    for (const auto& entry : pcbs_)
        if (entry.second->isFinished()) ++finished;
    return finished;
}

bool ProcessTable::hasUnfinished() const {
    for (const auto& entry : pcbs_)
        if (!entry.second->isFinished()) return true;
    return false;
}

std::size_t ProcessTable::clear() {
    const std::size_t released = pcbs_.size();
    pcbs_.clear();  // cada unique_ptr libera (delete) su PCB
    return released;
}

}  // namespace sim
