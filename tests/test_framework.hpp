/**
 * @file test_framework.hpp
 * @brief Mini framework de pruebas unitarias, sin dependencias externas.
 *
 * Uso:
 * @code
 *   TEST_CASE("descripción de la prueba") {
 *       CHECK(condicion);
 *       CHECK_EQ(valor_obtenido, valor_esperado);
 *   }
 * @endcode
 * Cada TEST_CASE se registra solo; tests/test_main.cpp los ejecuta todos y
 * devuelve código de salida 1 si alguno falla (útil para CI y `make test`).
 */
#ifndef SIM_TEST_FRAMEWORK_HPP
#define SIM_TEST_FRAMEWORK_HPP

#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace testfw {

/// Una prueba registrada: nombre legible y función a ejecutar.
struct TestCase {
    std::string name;
    std::function<void()> body;
};

/// Lista global de pruebas (se llena antes de main mediante Registrar).
std::vector<TestCase>& registry();

/// Registra un fallo de la prueba en curso.
void reportFailure(const char* file, int line, const std::string& message);

/// Objeto auxiliar cuyo constructor registra una prueba.
struct Registrar {
    Registrar(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

/// Compara dos valores y, si difieren, informa ambos.
template <class A, class B>
void checkEqual(const A& actual, const B& expected, const char* exprA, const char* exprB,
                const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream os;
        os << "CHECK_EQ(" << exprA << ", " << exprB << ")\n"
           << "        obtenido: " << actual << "\n"
           << "        esperado: " << expected;
        reportFailure(file, line, os.str());
    }
}

}  // namespace testfw

#define TF_CONCAT_INNER(a, b) a##b
#define TF_CONCAT(a, b) TF_CONCAT_INNER(a, b)

/// Declara y registra una prueba.
#define TEST_CASE(name)                                                                 \
    static void TF_CONCAT(tf_test_, __LINE__)();                                        \
    static const testfw::Registrar TF_CONCAT(tf_registrar_, __LINE__)(                  \
        name, TF_CONCAT(tf_test_, __LINE__));                                           \
    static void TF_CONCAT(tf_test_, __LINE__)()

/// Falla la prueba si la expresión es falsa (la prueba continúa).
#define CHECK(expr)                                                                     \
    do {                                                                                \
        if (!(expr)) testfw::reportFailure(__FILE__, __LINE__, "CHECK(" #expr ")");      \
    } while (0)

/// Falla la prueba si los valores difieren, mostrando ambos.
#define CHECK_EQ(actual, expected) \
    testfw::checkEqual((actual), (expected), #actual, #expected, __FILE__, __LINE__)

#endif  // SIM_TEST_FRAMEWORK_HPP
