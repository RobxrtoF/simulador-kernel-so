/**
 * @file test_parser.cpp
 * @brief Pruebas del tokenizador y de la validación estricta de valores.
 */
#include <string>
#include <vector>

#include "parser.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {
bool isOk(const std::string& text, long expected) {
    long value = -12345;
    return parseInteger(text, value) == IntParse::Ok && value == expected;
}
bool isNotANumber(const std::string& text) {
    long value = 0;
    return parseInteger(text, value) == IntParse::NotANumber;
}
}  // namespace

TEST_CASE("Tokenizador: separa por espacios y tabuladores, ignorando repeticiones") {
    TokenizeResult r = tokenize("  create\tP1   5  2  ");
    CHECK(r.ok);
    CHECK_EQ(r.tokens.size(), static_cast<std::size_t>(4));
    if (r.tokens.size() == 4) {
        CHECK_EQ(r.tokens[0], "create");
        CHECK_EQ(r.tokens[1], "P1");
        CHECK_EQ(r.tokens[2], "5");
        CHECK_EQ(r.tokens[3], "2");
    }
}

TEST_CASE("Tokenizador: línea vacía o solo espacios no produce palabras") {
    CHECK(tokenize("").tokens.empty());
    CHECK(tokenize("   \t  ").tokens.empty());
    CHECK(tokenize("\r\n").tokens.empty());
}

TEST_CASE("Tokenizador: comillas dobles permiten espacios dentro de un argumento") {
    TokenizeResult r = tokenize("load \"mis trazas/carga 1.txt\"");
    CHECK(r.ok);
    CHECK_EQ(r.tokens.size(), static_cast<std::size_t>(2));
    if (r.tokens.size() == 2) CHECK_EQ(r.tokens[1], "mis trazas/carga 1.txt");
    TokenizeResult empty = tokenize("create \"\" 5");
    CHECK_EQ(empty.tokens.size(), static_cast<std::size_t>(3));
}

TEST_CASE("Tokenizador: comillas sin cerrar son un error") {
    TokenizeResult r = tokenize("load \"archivo.txt");
    CHECK(!r.ok);
    CHECK(!r.error.empty());
    CHECK(r.tokens.empty());
}

TEST_CASE("Enteros: acepta solo enteros en base 10 con signo opcional") {
    CHECK(isOk("5", 5));
    CHECK(isOk("007", 7));
    CHECK(isOk("+3", 3));
    CHECK(isOk("-4", -4));
    CHECK(isOk("0", 0));
    CHECK(isOk("2147483647", 2147483647L));
}

TEST_CASE("Enteros: rechaza texto, decimales, hexadecimales y sobrantes") {
    CHECK(isNotANumber(""));
    CHECK(isNotANumber("abc"));
    CHECK(isNotANumber("xyz"));
    CHECK(isNotANumber("2.5"));
    CHECK(isNotANumber("5abc"));
    CHECK(isNotANumber("0x10"));
    CHECK(isNotANumber(" 5"));
    CHECK(isNotANumber("-"));
    CHECK(isNotANumber("+"));
    CHECK(isNotANumber("1e3"));
}

TEST_CASE("Enteros: valores fuera del rango de int") {
    long value = 0;
    CHECK(parseInteger("2147483648", value) == IntParse::OutOfRange);
    CHECK(parseInteger("-99999999999", value) == IntParse::OutOfRange);
    CHECK(parseInteger("999999999999999999999999", value) == IntParse::OutOfRange);
}

TEST_CASE("trim: quita espacios, tabuladores y fin de línea de Windows") {
    CHECK_EQ(trim("  Job_A \t"), "Job_A");
    CHECK_EQ(trim("4\r"), "4");
    CHECK_EQ(trim("   "), "");
    CHECK_EQ(trim("x"), "x");
}

TEST_CASE("Nombres de proceso: reglas de validación") {
    std::string reason;
    CHECK(isValidProcessName("Proceso_A", reason));
    CHECK(isValidProcessName("Job-1.b", reason));
    CHECK(isValidProcessName("Señal", reason));
    CHECK(!isValidProcessName("", reason));
    CHECK(!isValidProcessName("con espacio", reason));
    CHECK_EQ(reason, "El nombre no puede contener espacios.");
    CHECK(!isValidProcessName(std::string(33, 'x'), reason));
    CHECK(isValidProcessName(std::string(32, 'x'), reason));
}

TEST_CASE("Ancho visible: cuenta caracteres UTF-8 y rellena correctamente") {
    CHECK_EQ(displayWidth("Ráfaga"), static_cast<std::size_t>(6));
    CHECK_EQ(padRight("ab", 4), "ab  ");
    CHECK_EQ(padRight("ñ", 3), "ñ  ");
    CHECK_EQ(padRight("largo", 2), "largo");
}
