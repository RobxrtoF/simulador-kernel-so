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
#    make gui      Compila ./simulador con la interfaz gráfica (bonus; requiere raylib)
#    make gui-check  Compila con la interfaz y prueba la ventana (abre una ventana real)
#    make clean    Elimina los artefactos de compilación
# =============================================================================

CXX      ?= g++
OPT      ?= -O2 -g
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Wshadow $(OPT) -pthread
LDFLAGS  := -pthread

BUILD    := build
BIN      := simulador

# -----------------------------------------------------------------------------
# Interfaz gráfica opcional (bonus). `make gui` equivale a `make GUI=1` con un
# directorio de objetos propio. Define SIM_WITH_GUI y enlaza raylib, que se
# localiza con pkg-config. Sin GUI=1 el proyecto no depende de raylib.
#   macOS:  brew install raylib pkgconf
#   Linux:  ver el README (o usar CMake: cmake -DSIM_GUI=ON descarga raylib)
# -----------------------------------------------------------------------------
GUI      ?= 0
VARIANT  := base
LDLIBS   :=
ifeq ($(GUI),1)
  ifneq ($(shell pkg-config --exists raylib 2>/dev/null && echo si),si)
    $(error No se encontró raylib con pkg-config. macOS: 'brew install raylib pkgconf'. Linux: vea el README o use 'cmake -S . -B build-gui -DSIM_GUI=ON')
  endif
  VARIANT  := gui
  # -isystem: las advertencias de la cabecera de raylib no son del proyecto.
  CPPFLAGS += -DSIM_WITH_GUI $(patsubst -I%,-isystem %,$(shell pkg-config --cflags raylib))
  LDLIBS   += $(shell pkg-config --libs raylib)
  ifeq ($(shell uname -s),Darwin)
    LDLIBS += -lobjc   # runtime de Objective-C: la ventana desactiva App Nap (ver src/gui.cpp)
  endif
endif

SRC      := $(wildcard src/*.cpp)
OBJ      := $(SRC:src/%.cpp=$(BUILD)/%.o)
LIB_OBJ  := $(filter-out $(BUILD)/main.o,$(OBJ))
TEST_SRC := $(wildcard tests/*.cpp)
TEST_OBJ := $(TEST_SRC:tests/%.cpp=$(BUILD)/tests/%.o)
TEST_BIN := $(BUILD)/run_tests

# Compilación aparte con ThreadSanitizer (no mezcla objetos con la normal).
TSAN_VARS := BUILD=$(BUILD)-tsan BIN=$(BUILD)-tsan/simulador \
             OPT="-O1 -g -fsanitize=thread" LDFLAGS="-pthread -fsanitize=thread"

# Marca de la variante con la que se enlazó el ejecutable: al pasar de `make`
# a `make gui` (o al revés) cambia su contenido y el ejecutable se vuelve a enlazar.
STAMP := $(dir $(BIN)).$(notdir $(BIN)).variante

.PHONY: all test e2e tsan gui gui-check clean FORCE

all: $(BIN)

$(BIN): $(OBJ) $(STAMP)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $@ $(LDFLAGS) $(LDLIBS)

$(STAMP): FORCE | $(BUILD)
	@echo '$(VARIANT) $(BUILD)' | cmp -s - $@ || echo '$(VARIANT) $(BUILD)' > $@

FORCE:

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/tests/%.o: tests/%.cpp | $(BUILD)/tests
	$(CXX) $(CPPFLAGS) -Itests $(CXXFLAGS) -MMD -MP -c $< -o $@

$(TEST_BIN): $(LIB_OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

test: $(TEST_BIN)
	./$(TEST_BIN)

e2e: $(BIN)
	./tests/e2e/run_all.sh ./$(BIN)

tsan:
	$(MAKE) $(TSAN_VARS) all test
	TSAN_OPTIONS="halt_on_error=1" ./tests/e2e/run_all.sh $(BUILD)-tsan/simulador

gui:
	$(MAKE) GUI=1 BUILD=$(BUILD)-gui all

gui-check: gui
	./tests/gui/prueba_ventana.sh ./$(BIN)

$(BUILD) $(BUILD)/tests:
	mkdir -p $@

clean:
	rm -rf $(BUILD) $(BUILD)-tsan $(BUILD)-gui $(BIN) $(STAMP)

-include $(OBJ:.o=.d) $(TEST_OBJ:.o=.d)
