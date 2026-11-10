/**
 * @file test_cli.cpp
 * @brief Pruebas de la shell: validación estricta, mensajes de error y formato de salida.
 */
#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <sstream>
#include <string>

#include "cli.hpp"
#include "engine.hpp"
#include "kernel.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {

/// Shell de prueba: ejecuta una línea y devuelve exactamente lo que imprimió.
struct TestShell {
    Kernel kernel;
    std::ostringstream out;
    Cli cli{kernel, out};

    std::string run(const std::string& line) {
        out.str("");
        out.clear();
        cli.execute(line);
        return out.str();
    }
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

/// Ejecuta el motor en un hilo real hasta que todos los procesos terminen.
void runEngineToCompletion(Kernel& kernel) {
    kernel.setSpeed(1);
    pthread_t engine;
    pthread_create(&engine, nullptr, engineThreadMain, &kernel);
    kernel.start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const SystemSnapshot snap = kernel.snapshot();
        bool done = !snap.processes.empty();
        for (const PCB& pcb : snap.processes) done = done && pcb.isFinished();
        if (done) break;
        usleep(1000);
    }
    kernel.requestShutdown();
    pthread_join(engine, nullptr);
}

}  // namespace

TEST_CASE("CLI: mensajes de error exactos del enunciado") {
    TestShell shell;
    CHECK_EQ(shell.run("create Proceso_A"),
             "[ERROR DE SINTAXIS]: Parámetros insuficientes.\n"
             "Uso correcto: create <nombre> <burst_time> [prioridad]\n");
    CHECK_EQ(shell.run("pcb abc"), "[ERROR DE TIPO]: El PID debe ser un número entero positivo.\n");
    CHECK_EQ(shell.run("set_sched FIFO"),
             "[ERROR DE PARAMETRO]: Algoritmo no reconocido. Algoritmos válidos: RR, SRTF.\n");
}

TEST_CASE("CLI: validación de create (tipos, rangos y cantidad de parámetros)") {
    TestShell shell;
    CHECK(contains(shell.run("create BadProcess"), "[ERROR DE SINTAXIS]: Parámetros insuficientes."));
    CHECK(contains(shell.run("create P1 abc"), "[ERROR DE TIPO]: El burst_time debe ser un número entero."));
    CHECK(contains(shell.run("create P1 2.5"), "[ERROR DE TIPO]"));
    CHECK(contains(shell.run("create P1 0"), "[ERROR DE PARAMETRO]: El burst_time debe ser un entero mayor que 0."));
    CHECK(contains(shell.run("create P1 -3"), "[ERROR DE PARAMETRO]"));
    CHECK(contains(shell.run("create P1 5 x"), "[ERROR DE TIPO]: La prioridad debe ser un número entero."));
    CHECK(contains(shell.run("create P1 5 0"), "[ERROR DE PARAMETRO]: La prioridad debe estar entre 1 y 5."));
    CHECK(contains(shell.run("create P1 5 6"), "[ERROR DE PARAMETRO]: La prioridad debe estar entre 1 y 5."));
    CHECK(contains(shell.run("create P1 5 2 9"), "[ERROR DE SINTAXIS]: Demasiados parámetros."));
    CHECK(contains(shell.run("create \"con espacio\" 5"), "El nombre no puede contener espacios."));
    CHECK(contains(shell.run("create P1 99999999999"), "fuera de rango"));
    CHECK(shell.kernel.snapshot().processes.empty());  // ningún intento inválido creó procesos
}

TEST_CASE("CLI: create válido asigna PID autoincremental y prioridad por defecto 3") {
    TestShell shell;
    CHECK_EQ(shell.run("create P1 5 2"),
             "[OK]: Proceso 'P1' creado con PID 1 (ráfaga: 5, prioridad: 2, llegada: t=0) -> READY.\n");
    CHECK(contains(shell.run("create P2 3"), "PID 2 (ráfaga: 3, prioridad: 3"));
}

TEST_CASE("CLI: comandos sin parámetros rechazan argumentos adicionales") {
    TestShell shell;
    CHECK_EQ(shell.run("ps extra"),
             "[ERROR DE SINTAXIS]: El comando 'ps' no recibe parámetros.\nUso correcto: ps\n");
    CHECK(contains(shell.run("start ya"), "El comando 'start' no recibe parámetros."));
    CHECK(contains(shell.run("gantt 1"), "[ERROR DE SINTAXIS]"));
    CHECK(contains(shell.run("stats x"), "[ERROR DE SINTAXIS]"));
    CHECK(contains(shell.run("exit now"), "[ERROR DE SINTAXIS]"));
}

TEST_CASE("CLI: pcb valida el PID y su existencia") {
    TestShell shell;
    CHECK_EQ(shell.run("pcb 0"), "[ERROR DE PARAMETRO]: El PID debe ser un número entero positivo.\n");
    CHECK_EQ(shell.run("pcb -2"), "[ERROR DE PARAMETRO]: El PID debe ser un número entero positivo.\n");
    CHECK_EQ(shell.run("pcb 99"), "[ERROR DE PARAMETRO]: No existe un proceso con PID 99.\n");
    CHECK(contains(shell.run("pcb"), "[ERROR DE SINTAXIS]: Parámetros insuficientes."));
    CHECK(contains(shell.run("pcb 1 2"), "[ERROR DE SINTAXIS]: Demasiados parámetros."));
}

TEST_CASE("CLI: set_sched y set_speed validan algoritmo, quantum y rango de velocidad") {
    TestShell shell;
    CHECK_EQ(shell.run("set_sched RR"), "[OK]: Política de planificación: RR (quantum = 2).\n");
    CHECK_EQ(shell.run("set_sched RR 4"), "[OK]: Política de planificación: RR (quantum = 4).\n");
    CHECK_EQ(shell.run("set_sched SRTF"), "[OK]: Política de planificación: SRTF (apropiativo).\n");
    CHECK(contains(shell.run("set_sched RR 0"), "El quantum debe ser un entero mayor que 0."));
    CHECK(contains(shell.run("set_sched RR dos"), "[ERROR DE TIPO]: El quantum debe ser un número entero."));
    CHECK(contains(shell.run("set_sched SRTF 3"), "SRTF no utiliza quantum."));
    CHECK(contains(shell.run("set_sched rr"), "Sugerencia: el nombre distingue mayúsculas; escriba 'RR'."));
    CHECK(contains(shell.run("set_sched"), "[ERROR DE SINTAXIS]: Parámetros insuficientes."));
    CHECK_EQ(shell.run("set_speed 500"), "[OK]: Velocidad del reloj: 500 ms por tick.\n");
    CHECK(contains(shell.run("set_speed 10"), "ms_por_tick debe estar entre 50 y 5000 ms."));
    CHECK(contains(shell.run("set_speed 9000"), "[ERROR DE PARAMETRO]"));
    CHECK(contains(shell.run("set_speed rapido"), "[ERROR DE TIPO]"));
    CHECK_EQ(shell.kernel.snapshot().msPerTick, 500);
}

TEST_CASE("CLI: comandos desconocidos, comillas sin cerrar y líneas vacías") {
    TestShell shell;
    CHECK(contains(shell.run("fork 3"), "[ERROR]: Comando no reconocido: 'fork'."));
    CHECK(contains(shell.run("PS"), "¿Quiso decir 'ps'?"));
    CHECK_EQ(shell.run("load \"sin cerrar"), "[ERROR DE SINTAXIS]: Comillas sin cerrar.\n");
    CHECK_EQ(shell.run("   "), "");
    CHECK_EQ(shell.run("# comentario de un guion"), "");
}

TEST_CASE("CLI: formato de ps y pcb antes de iniciar el motor") {
    TestShell shell;
    shell.run("create Proceso_A 5 2");
    shell.run("create Proceso_B 3 1");
    const std::string ps = shell.run("ps");
    CHECK(contains(ps, "PID     NOMBRE          ESTADO        BURST RESTANTE    PRIORIDAD\n"));
    CHECK(contains(ps, "1       Proceso_A       READY         5s                2\n"));
    CHECK(contains(ps, "2       Proceso_B       READY         3s                1\n"));
    CHECK(contains(ps, "Cola de LISTOS: Proceso_A -> Proceso_B"));
    CHECK(contains(ps, "Motor: EN PAUSA"));
    CHECK_EQ(shell.run("pcb 1"),
             "==================================================\n"
             "        CONTROL BLOCK DE PROCESO (PCB) - PID: 1\n"
             "==================================================\n"
             "Nombre del Proceso : Proceso_A\n"
             "Estado Actual      : READY\n"
             "Prioridad          : 2\n"
             "Tiempo de Llegada  : 0 ticks\n"
             "Ráfaga Inicial     : 5 ticks\n"
             "Ráfaga Restante    : 5 ticks\n"
             "Tiempo de Espera   : 0 ticks\n"
             "Tiempo de Retorno  : N/A (En cola de listos)\n"
             "Tiempo de Respuesta: N/A (Aún no ha usado la CPU)\n"
             "==================================================\n");
}

TEST_CASE("CLI: start, pause y exit") {
    TestShell shell;
    CHECK(contains(shell.run("start"), "Esperando procesos..."));
    CHECK_EQ(shell.run("start"), "[INFO]: El motor de scheduling ya está en ejecución.\n");
    CHECK(contains(shell.run("pause"), "[OK]: Reloj de CPU en pausa en t=0."));
    CHECK_EQ(shell.run("pause"), "[INFO]: El motor de scheduling ya está en pausa.\n");
    CHECK(shell.cli.execute("help"));
    CHECK(!shell.cli.execute("exit"));  // exit indica que la shell debe terminar
}

TEST_CASE("CLI + Hilo Motor: Escenario 1 completo y salida de gantt, stats y pcb") {
    TestShell shell;
    shell.run("set_sched RR 2");
    shell.run("create P1 5 2");
    shell.run("create P2 3 1");
    shell.run("create P3 2 3");
    runEngineToCompletion(shell.kernel);
    CHECK_EQ(shell.run("gantt"),
             "DIAGRAMA DE GANTT (0 - 10 ticks):\n"
             "[0-2s: P1] -> [2-4s: P2] -> [4-6s: P3] -> [6-8s: P1] -> [8-9s: P2] -> [9-10s: P1]\n");
    CHECK_EQ(shell.run("stats"),
             "ESTADÍSTICAS DEL SISTEMA:\n"
             "- Procesos Finalizados        : 3 / 3\n"
             "- Tiempo Promedio de Espera   : 5.00 ticks\n"
             "- Tiempo Promedio de Retorno  : 8.33 ticks\n"
             "- Tiempo Promedio de Respuesta: 2.00 ticks\n"
             "- Uso de CPU                  : 100.0%\n"
             "- Tiempo Total Simulado       : 10 ticks\n");
    const std::string pcb = shell.run("pcb 2");
    CHECK(contains(pcb, "Estado Actual      : TERMINATED\n"));
    CHECK(contains(pcb, "Tiempo de Espera   : 6 ticks\n"));
    CHECK(contains(pcb, "Tiempo de Retorno  : 9 ticks\n"));
    CHECK(contains(pcb, "Tiempo de Respuesta: 2 ticks\n"));
}
