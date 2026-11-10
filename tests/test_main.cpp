/**
 * @file test_main.cpp
 * @brief Ejecutor de las pruebas unitarias registradas con TEST_CASE.
 */
#include <exception>
#include <iostream>

#include "test_framework.hpp"

namespace testfw {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

namespace {
int g_failuresInCurrentTest = 0;
}

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_failuresInCurrentTest;
    std::cout << "    FALLO en " << file << ":" << line << "\n        " << message << "\n";
}

}  // namespace testfw

int main() {
    std::size_t failed = 0;
    for (const auto& test : testfw::registry()) {
        testfw::g_failuresInCurrentTest = 0;
        try {
            test.body();
        } catch (const std::exception& e) {
            testfw::reportFailure("(excepción)", 0, e.what());
        }
        if (testfw::g_failuresInCurrentTest == 0) {
            std::cout << "[  OK  ] " << test.name << "\n";
        } else {
            std::cout << "[ FALLA] " << test.name << "\n";
            ++failed;
        }
    }
    const std::size_t total = testfw::registry().size();
    std::cout << "\n" << (total - failed) << "/" << total << " pruebas superadas\n";
    return failed == 0 ? 0 : 1;
}
