# =============================================================================
#  Simulador de Kernel y Gestor de Procesos en Consola
#  (Shell & Scheduling Engine) - Proyecto Sistemas Operativos, Fase I (UCAB)
#
#  Compila con g++ (C++17) y soporte de hilos POSIX (-pthread).
#
#  Objetivos:
#    make          Compila el ejecutable ./simulador
#    make test     Compila y ejecuta las pruebas unitarias
#    make e2e      Ejecuta los escenarios de prueba del enunciado (CLI real)
#    make tsan     Repite las pruebas con ThreadSanitizer (detecta data races)
#    make clean    Elimina los artefactos de compilación
# =============================================================================

CXX      ?= g++
OPT      ?= -O2 -g
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Wshadow $(OPT) -pthread
LDFLAGS  := -pthread

BUILD    := build
BIN      := simulador

SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:src/%.cpp=$(BUILD)/%.o)
LIB_OBJ  := $(filter-out $(BUILD)/main.o,$(OBJ))
TEST_SRC := $(wildcard tests/*.cpp)
TEST_OBJ := $(TEST_SRC:tests/%.cpp=$(BUILD)/tests/%.o)
TEST_BIN := $(BUILD)/run_tests

# Compilación aparte con ThreadSanitizer (no mezcla objetos con la normal).
TSAN_VARS := BUILD=$(BUILD)-tsan BIN=$(BUILD)-tsan/simulador \
             OPT="-O1 -g -fsanitize=thread" LDFLAGS="-pthread -fsanitize=thread"

.PHONY: all test e2e tsan clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/tests/%.o: tests/%.cpp | $(BUILD)/tests
	$(CXX) $(CPPFLAGS) -Itests $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST_BIN): $(LIB_OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	./$(TEST_BIN)

e2e: $(BIN)
	./tests/e2e/run_all.sh ./$(BIN)

tsan:
	$(MAKE) $(TSAN_VARS) all test
	TSAN_OPTIONS="halt_on_error=1" ./tests/e2e/run_all.sh $(BUILD)-tsan/simulador

$(BUILD) $(BUILD)/tests:
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(BUILD)-tsan $(BIN)

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)
