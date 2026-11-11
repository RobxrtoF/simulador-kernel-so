/**
 * @file parser.hpp
 * @brief Utilidades de parsing estricto usadas por la CLI y por la carga por lotes.
 *
 * Son funciones puras (no comparten estado), por lo que se pueden llamar desde
 * cualquier hilo sin sincronización.
 */
#ifndef SIM_PARSER_HPP
#define SIM_PARSER_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace sim {

/// Resultado de separar una línea de comando en palabras (tokens).
struct TokenizeResult {
    bool ok = true;                  ///< false si la línea tiene un error léxico.
    std::vector<std::string> tokens; ///< Palabras encontradas, en orden.
    std::string error;               ///< Descripción del error cuando ok == false.
};

/**
 * @brief Separa una línea en palabras usando espacios y tabuladores.
 *
 * Admite comillas dobles para argumentos que contienen espacios, por ejemplo
 * `load "mis trazas/carga.txt"`. Una comilla sin cerrar se reporta como error.
 */
TokenizeResult tokenize(const std::string& line);

/// Resultado de interpretar un número entero.
enum class IntParse {
    Ok,          ///< Conversión exitosa.
    NotANumber,  ///< El texto no es un entero en base 10 ("abc", "2.5", "7x", "").
    OutOfRange   ///< Es un entero pero no cabe en un int.
};

/**
 * @brief Interpreta `text` como entero en base 10 de forma estricta.
 *
 * Acepta un signo opcional seguido únicamente de dígitos. Rechaza espacios,
 * decimales, prefijos hexadecimales y cualquier carácter sobrante.
 * @param[out] value Valor convertido (solo se escribe si devuelve IntParse::Ok).
 */
IntParse parseInteger(const std::string& text, long& value);

/// Quita espacios, tabuladores, '\r' y '\n' al inicio y al final.
std::string trim(const std::string& text);

/**
 * @brief Limpia los caracteres «invisibles» que llegan al copiar y pegar.
 *
 * Al copiar comandos o trazas desde un PDF, Word o una página web se cuelan
 * caracteres Unicode que no se ven pero rompen la validación: el propio
 * enunciado trae un espacio de ancho cero (U+200B) al final de las líneas de
 * workload_test.txt. Esta función:
 *   - elimina los de ancho cero: U+200B, U+200C, U+200D, U+2060 y U+FEFF (BOM);
 *   - convierte en espacio normal los espacios Unicode: U+00A0 (espacio duro),
 *     U+2000 a U+200A, U+202F, U+205F y U+3000;
 *   - convierte las comillas tipográficas “ ” en comillas rectas (").
 * El resto del texto (letras con tilde, ñ, emojis) no se toca.
 */
std::string normalizeInput(const std::string& text);

/// Longitud máxima (en caracteres) del nombre de un proceso.
constexpr std::size_t kMaxNameLength = 32;

/**
 * @brief Valida el nombre de un proceso: 1..kMaxNameLength caracteres, sin
 *        espacios ni caracteres de control.
 * @param[out] reason Motivo del rechazo cuando devuelve false.
 */
bool isValidProcessName(const std::string& name, std::string& reason);

/// Ancho visible de un texto UTF-8 (cuenta caracteres, no bytes).
std::size_t displayWidth(const std::string& text);

/// Completa con espacios a la derecha hasta `width` caracteres visibles.
std::string padRight(const std::string& text, std::size_t width);

}  // namespace sim

#endif  // SIM_PARSER_HPP
