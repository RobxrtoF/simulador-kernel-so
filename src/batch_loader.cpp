/**
 * @file batch_loader.cpp
 * @brief Lectura y validación de archivos de traza.
 */
#include "batch_loader.hpp"

#include <sys/stat.h>

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fstream>
#include <istream>

#include "parser.hpp"
#include "pcb.hpp"

namespace sim {

namespace {

constexpr std::size_t kFieldCount = 4;
const char* const kHeader[kFieldCount] = {"NOMBRE", "BURST_TIME", "PRIORIDAD", "ARRIVAL_TIME"};

/// Separa una línea por comas y recorta los espacios de cada campo.
std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (char c : line) {
        if (c == ',') {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(trim(current));
    return fields;
}

/// true si la línea es la cabecera de columnas escrita sin '#'.
bool isHeader(const std::vector<std::string>& fields) {
    if (fields.size() != kFieldCount) return false;
    for (std::size_t i = 0; i < kFieldCount; ++i) {
        std::string upper = fields[i];
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper != kHeader[i]) return false;
    }
    return true;
}

/// Valida un campo numérico; si falla agrega el error y devuelve false.
bool readField(const std::string& text, const std::string& field, long min, long max,
               const std::string& rangeMessage, const std::string& where,
               std::vector<std::string>& errors, long& out) {
    long value = 0;
    switch (parseInteger(text, value)) {
        case IntParse::NotANumber:
            errors.push_back(where + field + " '" + text + "' no es un número entero.");
            return false;
        case IntParse::OutOfRange:
            errors.push_back(where + field + " '" + text + "' está fuera de rango.");
            return false;
        case IntParse::Ok:
            break;
    }
    if (value < min || value > max) {
        errors.push_back(where + field + " " + rangeMessage + " (se recibió " + text + ").");
        return false;
    }
    out = value;
    return true;
}

/// true si `path` existe y es un archivo regular.
bool isRegularFile(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

}  // namespace

std::string resolveWorkloadPath(const std::string& path, const std::string& searchDir) {
    struct stat info {};
    if (stat(path.c_str(), &info) == 0) return path;               // existe: se usa tal cual
    if (path.empty() || path.find('/') != std::string::npos) return path;  // ruta explícita
    const std::string candidate = searchDir + "/" + path;
    return isRegularFile(candidate) ? candidate : path;
}

LoadResult parseWorkloadFile(const std::string& path) {
    LoadResult result;
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        result.errors.push_back("No se encontró el archivo '" + path + "' (" + std::strerror(errno) + ").");
        return result;
    }
    if (!S_ISREG(info.st_mode)) {
        result.errors.push_back("'" + path + "' no es un archivo regular.");
        return result;
    }
    std::ifstream file(path);
    if (!file) {
        result.errors.push_back("No se pudo abrir el archivo '" + path + "' (" + std::strerror(errno) + ").");
        return result;
    }
    return parseWorkload(file);
}

LoadResult parseWorkload(std::istream& in) {
    LoadResult result;
    result.fileOpened = true;

    std::string raw;
    int lineNumber = 0;
    while (std::getline(in, raw)) {
        ++lineNumber;
        if (lineNumber == 1 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);  // BOM UTF-8

        // Trazas copiadas desde el PDF del enunciado traen U+200B al final de cada línea.
        const std::string line = trim(normalizeInput(raw));
        if (line.empty() || line[0] == '#') continue;

        const std::vector<std::string> fields = splitFields(line);
        if (isHeader(fields)) continue;

        const std::string where = "línea " + std::to_string(lineNumber) + ": ";
        if (fields.size() != kFieldCount) {
            result.errors.push_back(where + "se esperaban 4 campos (NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME) y hay " +
                                    std::to_string(fields.size()) + ".");
            continue;
        }

        bool valid = true;
        std::string reason;
        if (!isValidProcessName(fields[0], reason)) {
            result.errors.push_back(where + "NOMBRE '" + fields[0] + "' no es válido: " + reason);
            valid = false;
        }
        long burst = 0;
        long priority = 0;
        long arrival = 0;
        valid &= readField(fields[1], "BURST_TIME", 1, INT_MAX, "debe ser mayor que 0", where, result.errors, burst);
        valid &= readField(fields[2], "PRIORIDAD", kMinPriority, kMaxPriority, "debe estar entre 1 y 5", where,
                           result.errors, priority);
        valid &= readField(fields[3], "ARRIVAL_TIME", 0, INT_MAX, "debe ser mayor o igual que 0", where,
                           result.errors, arrival);
        if (valid) {
            result.processes.push_back(ProcessSpec{fields[0], burst, static_cast<int>(priority), arrival, lineNumber});
        }
    }

    if (result.errors.empty() && result.processes.empty()) {
        result.errors.push_back("El archivo no contiene procesos (solo comentarios o líneas vacías).");
    }
    return result;
}

}  // namespace sim
