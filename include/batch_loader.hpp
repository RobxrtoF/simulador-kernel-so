/**
 * @file batch_loader.hpp
 * @brief Parser de archivos de traza (workload) para la carga por lotes (`load`).
 *
 * Formato: una línea por proceso, campos separados por coma.
 * @code
 *   # NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME
 *   Job_A, 4, 2, 0
 *   Job_B, 2, 1, 1
 * @endcode
 * - Se ignoran las líneas vacías, las que empiezan con '#' y una cabecera sin '#'.
 * - Se toleran espacios alrededor de los campos, fines de línea de Windows (\r\n)
 *   y la marca BOM de UTF-8 (archivos creados con el Bloc de notas).
 * - El archivo completo se valida ANTES de crear procesos: si hay un solo error
 *   no se carga nada y se informan todos los errores con su número de línea.
 */
#ifndef SIM_BATCH_LOADER_HPP
#define SIM_BATCH_LOADER_HPP

#include <iosfwd>
#include <string>
#include <vector>

namespace sim {

/// Un proceso descrito en el archivo de traza (ya validado).
struct ProcessSpec {
    std::string name;  ///< NOMBRE
    long burst;        ///< BURST_TIME (> 0)
    int priority;      ///< PRIORIDAD (1..5)
    long arrival;      ///< ARRIVAL_TIME (>= 0), relativo al instante de la carga
    int line;          ///< Línea del archivo donde aparece
};

/// Resultado de leer y validar un archivo de traza.
struct LoadResult {
    bool fileOpened = false;            ///< false si el archivo no existe o no se pudo abrir.
    std::vector<ProcessSpec> processes; ///< Procesos válidos, en el orden del archivo.
    std::vector<std::string> errors;    ///< Errores encontrados (con número de línea).

    /// true si el archivo se leyó completo, sin errores y con al menos un proceso.
    bool ok() const { return fileOpened && errors.empty(); }
};

/// Verifica que `path` exista y sea un archivo regular, lo lee y valida su formato.
LoadResult parseWorkloadFile(const std::string& path);

/// Valida el contenido de una traza ya abierta (útil para pruebas).
LoadResult parseWorkload(std::istream& in);

}  // namespace sim

#endif  // SIM_BATCH_LOADER_HPP
