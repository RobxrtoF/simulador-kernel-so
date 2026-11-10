/**
 * @file test_batch_loader.cpp
 * @brief Pruebas de la carga por lotes: formato, validación y llegadas relativas.
 */
#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "batch_loader.hpp"
#include "cli.hpp"
#include "engine.hpp"
#include "gantt.hpp"
#include "kernel.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {

/// Archivo temporal que se borra solo al destruirse.
class TempFile {
public:
    explicit TempFile(const std::string& content) {
        char pattern[] = "/tmp/traza_XXXXXX";
        const int fd = mkstemp(pattern);
        path_ = pattern;
        if (fd >= 0) {
            const ssize_t written = write(fd, content.data(), content.size());
            (void)written;
            close(fd);
        }
    }
    ~TempFile() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

LoadResult parseText(const std::string& text) {
    std::istringstream in(text);
    return parseWorkload(in);
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

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

const char* const kScenario2 =
    "# NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME\n"
    "Job_A, 4, 2, 0\n"
    "Job_B, 2, 1, 1\n"
    "Job_C, 1, 3, 3\n";

}  // namespace

TEST_CASE("Lotes: lee el archivo del Escenario 2 del enunciado") {
    const LoadResult r = parseText(kScenario2);
    CHECK(r.ok());
    CHECK_EQ(r.processes.size(), static_cast<std::size_t>(3));
    if (r.processes.size() == 3) {
        CHECK_EQ(r.processes[0].name, "Job_A");
        CHECK_EQ(r.processes[0].burst, 4L);
        CHECK_EQ(r.processes[0].priority, 2);
        CHECK_EQ(r.processes[0].arrival, 0L);
        CHECK_EQ(r.processes[2].name, "Job_C");
        CHECK_EQ(r.processes[2].arrival, 3L);
        CHECK_EQ(r.processes[2].line, 4);
    }
}

TEST_CASE("Lotes: tolera comentarios, líneas vacías, CRLF, BOM y cabecera sin #") {
    const LoadResult r = parseText(
        "\xEF\xBB\xBFNOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME\r\n"
        "\r\n"
        "   # comentario con sangría\r\n"
        "  Job_X ,  5 , 1 , 2  \r\n");
    CHECK(r.ok());
    CHECK_EQ(r.processes.size(), static_cast<std::size_t>(1));
    if (!r.processes.empty()) {
        CHECK_EQ(r.processes[0].name, "Job_X");
        CHECK_EQ(r.processes[0].burst, 5L);
    }
}

TEST_CASE("Lotes: reporta cada línea inválida con su número y no carga nada") {
    const LoadResult r = parseText(
        "Job_OK, 3, 2, 0\n"
        "Job_SinLlegada, 4, 1\n"
        "Job_Texto, cuatro, 2, 1\n"
        "Job_Prioridad, 2, 9, 1\n"
        "Job Con Espacios, 2, 2, 2\n"
        "Job_Negativo, 2, 2, -1\n"
        "Job_Cero, 0, 3, 0\n");
    CHECK(!r.ok());
    CHECK_EQ(r.errors.size(), static_cast<std::size_t>(6));
    if (r.errors.size() == 6) {
        CHECK(contains(r.errors[0], "línea 2: se esperaban 4 campos"));
        CHECK(contains(r.errors[1], "línea 3: BURST_TIME 'cuatro' no es un número entero."));
        CHECK(contains(r.errors[2], "línea 4: PRIORIDAD debe estar entre 1 y 5"));
        CHECK(contains(r.errors[3], "línea 5: NOMBRE 'Job Con Espacios' no es válido"));
        CHECK(contains(r.errors[4], "línea 6: ARRIVAL_TIME debe ser mayor o igual que 0"));
        CHECK(contains(r.errors[5], "línea 7: BURST_TIME debe ser mayor que 0"));
    }
}

TEST_CASE("Lotes: archivo vacío, inexistente o que no es un archivo regular") {
    const LoadResult empty = parseText("# solo comentarios\n\n");
    CHECK(!empty.ok());
    CHECK(contains(empty.errors.front(), "no contiene procesos"));

    const LoadResult missing = parseWorkloadFile("/ruta/que/no/existe.txt");
    CHECK(!missing.fileOpened);
    CHECK(contains(missing.errors.front(), "No se encontró el archivo '/ruta/que/no/existe.txt'"));

    const LoadResult directory = parseWorkloadFile("/tmp");
    CHECK(!directory.fileOpened);
    CHECK(contains(directory.errors.front(), "no es un archivo regular"));
}

TEST_CASE("Lotes: las llegadas son relativas al instante de la carga") {
    Kernel kernel;
    kernel.createProcess("Previo", 3, 3);
    runEngineToCompletion(kernel);  // el reloj queda en t=3
    std::istringstream in("Tarde_A, 1, 3, 0\nTarde_B, 1, 3, 2\n");
    const LoadResult r = parseWorkload(in);
    long base = -1;
    const std::vector<PCB> created = kernel.loadProcesses(r.processes, base);
    CHECK_EQ(base, 3L);
    CHECK_EQ(created.size(), static_cast<std::size_t>(2));
    if (created.size() == 2) {
        CHECK_EQ(created[0].arrivalTime, 3L);
        CHECK_EQ(std::string(stateName(created[0].state)), "READY");
        CHECK_EQ(created[1].arrivalTime, 5L);
        CHECK_EQ(std::string(stateName(created[1].state)), "NEW");
    }
}

TEST_CASE("CLI load: Escenario 2 completo con SRTF (Gantt exacto del enunciado)") {
    TempFile trace(kScenario2);
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);
    cli.execute("set_sched SRTF");
    out.str("");
    cli.execute("load " + trace.path());
    const std::string loaded = out.str();
    CHECK(contains(loaded, "[OK]: 3 proceso(s) cargado(s)"));
    CHECK(contains(loaded, "Job_A  ráfaga=4  prioridad=2  llegada=t0  -> READY"));
    CHECK(contains(loaded, "Job_B  ráfaga=2  prioridad=1  llegada=t1  -> NEW"));
    runEngineToCompletion(kernel);
    out.str("");
    cli.execute("gantt");
    CHECK_EQ(out.str(),
             "DIAGRAMA DE GANTT (0 - 7 ticks):\n"
             "[0-1s: Job_A] -> [1-3s: Job_B] -> [3-4s: Job_C] -> [4-7s: Job_A]\n");
}

TEST_CASE("CLI load: errores de archivo y de formato no detienen la shell") {
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);
    CHECK(cli.execute("load no_existe.txt"));
    CHECK(contains(out.str(), "[ERROR DE ARCHIVO]: No se encontró el archivo 'no_existe.txt'"));
    TempFile bad("Job_OK, 3, 2, 0\nJob_Mal, x, 2, 0\n");
    out.str("");
    CHECK(cli.execute("load " + bad.path()));
    CHECK(contains(out.str(), "[ERROR DE FORMATO]"));
    CHECK(contains(out.str(), "línea 2: BURST_TIME 'x' no es un número entero."));
    CHECK(kernel.snapshot().processes.empty());  // validación previa: no se cargó nada
    out.str("");
    cli.execute("load");
    CHECK(contains(out.str(), "Uso correcto: load <archivo>"));
}

TEST_CASE("Lotes: un nombre simple que no existe se busca en la carpeta de trazas") {
    TempFile trace(kScenario2);                                   // /tmp/traza_XXXXXX
    const std::string name = trace.path().substr(trace.path().rfind('/') + 1);
    CHECK_EQ(resolveWorkloadPath(name, "/tmp"), trace.path());    // como 'load workload_test.txt'
    CHECK_EQ(resolveWorkloadPath(trace.path(), "/nada"), trace.path());  // existe: tal cual
    CHECK_EQ(resolveWorkloadPath("no_existe.txt", "/tmp"), std::string("no_existe.txt"));
    CHECK_EQ(resolveWorkloadPath("otra/" + name, "/tmp"), "otra/" + name);  // ruta explícita: sin buscar
    CHECK_EQ(resolveWorkloadPath("/tmp", "/tmp"), std::string("/tmp"));    // directorio: lo rechaza el parser
}
