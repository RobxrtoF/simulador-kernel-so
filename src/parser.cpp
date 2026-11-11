/**
 * @file parser.cpp
 * @brief Implementación del parsing estricto de comandos y valores.
 */
#include "parser.hpp"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>

namespace sim {

TokenizeResult tokenize(const std::string& line) {
    TokenizeResult result;
    std::string current;
    bool inToken = false;   // hay una palabra en construcción (aunque sea "" entre comillas)
    bool inQuotes = false;

    for (char c : line) {
        if (inQuotes) {
            if (c == '"') {
                inQuotes = false;
            } else {
                current += c;
            }
        } else if (c == '"') {
            inQuotes = true;
            inToken = true;
        } else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (inToken) {
                result.tokens.push_back(current);
                current.clear();
                inToken = false;
            }
        } else {
            current += c;
            inToken = true;
        }
    }

    if (inQuotes) {
        result.ok = false;
        result.error = "Comillas sin cerrar.";
        result.tokens.clear();
        return result;
    }
    if (inToken) result.tokens.push_back(current);
    return result;
}

IntParse parseInteger(const std::string& text, long& value) {
    if (text.empty()) return IntParse::NotANumber;

    std::size_t firstDigit = 0;
    if (text[0] == '+' || text[0] == '-') firstDigit = 1;
    if (firstDigit == text.size()) return IntParse::NotANumber;  // solo el signo

    for (std::size_t i = firstDigit; i < text.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[i]))) return IntParse::NotANumber;
    }

    errno = 0;
    const long long parsed = std::strtoll(text.c_str(), nullptr, 10);
    if (errno == ERANGE || parsed > INT_MAX || parsed < INT_MIN) return IntParse::OutOfRange;

    value = static_cast<long>(parsed);
    return IntParse::Ok;
}

std::string normalizeInput(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = byte(i);
        if (c == 0xC2 && i + 1 < text.size() && byte(i + 1) == 0xA0) {  // U+00A0 espacio duro
            out += ' ';
            i += 2;
            continue;
        }
        if (c == 0xE2 && i + 2 < text.size()) {
            const unsigned char b1 = byte(i + 1), b2 = byte(i + 2);
            if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) { out += ' '; i += 3; continue; }  // U+2000..U+200A
            if (b1 == 0x80 && b2 >= 0x8B && b2 <= 0x8D) { i += 3; continue; }              // U+200B..U+200D
            if (b1 == 0x80 && b2 == 0xAF) { out += ' '; i += 3; continue; }                // U+202F
            if (b1 == 0x80 && (b2 == 0x9C || b2 == 0x9D)) { out += '"'; i += 3; continue; } // “ ”
            if (b1 == 0x81 && b2 == 0x9F) { out += ' '; i += 3; continue; }                // U+205F
            if (b1 == 0x81 && b2 == 0xA0) { i += 3; continue; }                            // U+2060
        }
        if (c == 0xE3 && i + 2 < text.size() && byte(i + 1) == 0x80 && byte(i + 2) == 0x80) {  // U+3000
            out += ' ';
            i += 3;
            continue;
        }
        if (c == 0xEF && i + 2 < text.size() && byte(i + 1) == 0xBB && byte(i + 2) == 0xBF) {  // U+FEFF
            i += 3;
            continue;
        }
        out += text[i];
        ++i;
    }
    return out;
}

std::string trim(const std::string& text) {
    const char* whitespace = " \t\r\n";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string::npos) return "";
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

bool isValidProcessName(const std::string& name, std::string& reason) {
    if (name.empty()) {
        reason = "El nombre no puede estar vacío.";
        return false;
    }
    for (char ch : name) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isspace(c)) {
            reason = "El nombre no puede contener espacios.";
            return false;
        }
        if (std::iscntrl(c)) {
            reason = "El nombre contiene caracteres de control no válidos.";
            return false;
        }
    }
    if (displayWidth(name) > kMaxNameLength) {
        reason = "El nombre no puede superar " + std::to_string(kMaxNameLength) + " caracteres.";
        return false;
    }
    return true;
}

std::size_t displayWidth(const std::string& text) {
    std::size_t width = 0;
    for (char ch : text) {
        // En UTF-8 los bytes de continuación tienen la forma 10xxxxxx.
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++width;
    }
    return width;
}

std::string padRight(const std::string& text, std::size_t width) {
    const std::size_t current = displayWidth(text);
    if (current >= width) return text;
    return text + std::string(width - current, ' ');
}

}  // namespace sim
