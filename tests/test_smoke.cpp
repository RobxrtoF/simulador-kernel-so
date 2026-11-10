/**
 * @file test_smoke.cpp
 * @brief Prueba mínima para verificar que la infraestructura de pruebas compila y corre.
 *        Se reemplaza por pruebas reales a medida que se integran los módulos.
 */
#include <string>

#include "test_framework.hpp"

TEST_CASE("la infraestructura de pruebas está operativa") {
    CHECK(1 + 1 == 2);
    CHECK_EQ(std::string("kernel"), "kernel");
}
