/**
 * @file process_table.hpp
 * @brief Tabla de procesos: dueña de todos los PCB del sistema.
 */
#ifndef SIM_PROCESS_TABLE_HPP
#define SIM_PROCESS_TABLE_HPP

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "pcb.hpp"

namespace sim {

/**
 * @brief Almacena los PCB indexados por PID y asigna PID autoincrementales.
 *
 * Cada PCB vive en memoria dinámica (std::unique_ptr) y se libera con clear()
 * o al destruir la tabla. Las direcciones de los PCB son estables mientras
 * existan, por lo que los punteros devueltos por find() siguen siendo válidos
 * aunque se creen procesos nuevos.
 *
 * La tabla NO es thread-safe por sí sola: el Kernel la protege con su mutex.
 */
class ProcessTable {
public:
    /// Crea un PCB en estado NUEVO con el siguiente PID disponible.
    PCB& create(const std::string& name, long burst, int priority, long arrival);

    /// Busca un proceso por PID; devuelve nullptr si no existe.
    PCB* find(int pid);
    const PCB* find(int pid) const;

    /// Punteros a todos los PCB, ordenados por PID.
    std::vector<PCB*> all();
    std::vector<const PCB*> all() const;

    /// Copias de todos los PCB, ordenadas por PID (para consultas desde otro hilo).
    std::vector<PCB> snapshot() const;

    std::size_t size() const { return pcbs_.size(); }
    /// Cantidad de procesos en estado FINALIZADO.
    std::size_t countFinished() const;
    /// true si existe al menos un proceso que todavía no termina.
    bool hasUnfinished() const;

    /// Libera la memoria de todos los PCB y devuelve cuántos se liberaron.
    std::size_t clear();

private:
    std::map<int, std::unique_ptr<PCB>> pcbs_;  ///< PID -> PCB (ordenado por PID).
    int nextPid_ = 1;                           ///< Próximo PID a asignar.
};

}  // namespace sim

#endif  // SIM_PROCESS_TABLE_HPP
