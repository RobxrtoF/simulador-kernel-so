/**
 * @file test_robustez.cpp
 * @brief Entradas «hostiles»: texto pegado desde un PDF, caracteres invisibles,
 *        valores extremos y basura binaria. La shell nunca debe caerse.
 */
#include <sstream>
#include <string>

#include "batch_loader.hpp"
#include "cli.hpp"
#include "kernel.hpp"
#include "parser.hpp"
#include "test_framework.hpp"

using namespace sim;

namespace {

const std::string kZeroWidth = "\xE2\x80\x8B";  // U+200B, viene en el PDF del enunciado
const std::string kNbsp = "\xC2\xA0";           // U+00A0, espacio duro

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string run(Cli& cli, std::ostringstream& out, const std::string& line) {
    out.str("");
    out.clear();
    cli.execute(line);
    return out.str();
}

}  // namespace

TEST_CASE("Robustez: normalizeInput limpia lo que se pega desde un PDF o Word") {
    CHECK_EQ(normalizeInput("Job_A, 4, 2, 0" + kZeroWidth), "Job_A, 4, 2, 0");
    CHECK_EQ(normalizeInput("create" + kNbsp + "P1" + kNbsp + "5"), "create P1 5");
    CHECK_EQ(normalizeInput("\xEF\xBB\xBF" "ps"), "ps");                                   // BOM
    CHECK_EQ(normalizeInput("load \xE2\x80\x9Ctraza.txt\xE2\x80\x9D"), "load \"traza.txt\"");  // “ ”
    CHECK_EQ(normalizeInput("a\xE2\x80\x8C" "b\xE2\x80\x8D" "c\xE2\x81\xA0"), "abc");      // ZWNJ, ZWJ, WJ
    CHECK_EQ(normalizeInput("x\xE2\x80\x83y\xE3\x80\x80z"), "x y z");                      // em space, ideográfico
    CHECK_EQ(normalizeInput("Ñandú_Ácido 🚀"), "Ñandú_Ácido 🚀");                            // lo demás no se toca
    CHECK_EQ(normalizeInput("\xE2\x80"), "\xE2\x80");                                       // UTF-8 truncado: intacto
    CHECK_EQ(normalizeInput(""), "");
}

TEST_CASE("Robustez: la traza copiada del PDF del enunciado (con U+200B) se carga") {
    std::istringstream pdf("# NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME" + kZeroWidth + "\n" +
                           "Job_A, 4, 2, 0" + kZeroWidth + "\n" + "Job_B, 2, 1, 1" + kZeroWidth + "\n" +
                           "Job_C, 1, 3, 3\n");
    const LoadResult result = parseWorkload(pdf);
    CHECK(result.ok());
    CHECK_EQ(result.processes.size(), static_cast<std::size_t>(3));
    if (result.processes.size() == 3) {
        CHECK_EQ(result.processes[0].arrival, 0L);
        CHECK_EQ(result.processes[1].arrival, 1L);
    }
}

TEST_CASE("Robustez: comandos con caracteres invisibles se interpretan bien") {
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);
    CHECK(contains(run(cli, out, "create P1 5 2" + kZeroWidth), "[OK]: Proceso 'P1' creado con PID 1"));
    CHECK(contains(run(cli, out, "create" + kNbsp + "P2" + kNbsp + "3"), "[OK]: Proceso 'P2' creado con PID 2"));
    CHECK(contains(run(cli, out, kZeroWidth + "ps" + kZeroWidth), "1      P1"));
    CHECK(contains(run(cli, out, "set_sched RR 2" + kZeroWidth), "[OK]: Política de planificación: RR (quantum = 2)."));
}

TEST_CASE("Robustez: valores extremos y basura no detienen la shell") {
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);
    const char* const hostiles[] = {
        "create P1 2147483648", "create P1 99999999999999999999999", "create P1 0x10", "create P1 1e3",
        "create P1 5,0", "create P1 5 2.5", "pcb 99999999999999", "set_sched RR -1", "set_speed -50",
        "set_aging 99999999999", "load /dev/zero", "load .", "$(rm -rf /)", "\x1b[2J", "\x7f\x7f",
        "create \x01\x02 3", "\xff\xfe\xfd", "unblock 1",
    };
    for (const char* line : hostiles) {
        CHECK(cli.execute(line));  // nunca pide salir
        CHECK(contains(out.str(), "[ERROR"));
        out.str("");
    }
    CHECK(cli.execute(std::string(200000, 'x')));                  // línea gigante
    CHECK(contains(out.str(), "[ERROR]: Comando no reconocido"));
    out.str("");
    CHECK(cli.execute("create " + std::string(100000, 'A') + " 5"));  // nombre gigante
    CHECK(contains(out.str(), "no puede superar"));
    CHECK(kernel.snapshot().processes.empty());                    // nada de lo anterior creó procesos
}

TEST_CASE("Robustez: los valores máximos válidos se aceptan sin desbordar") {
    Kernel kernel;
    std::ostringstream out;
    Cli cli(kernel, out);
    CHECK(contains(run(cli, out, "create Enorme 2147483647 5"), "[OK]"));
    CHECK(contains(run(cli, out, "set_sched RR 2147483647"), "RR (quantum = 2147483647)"));
    CHECK(contains(run(cli, out, "set_aging 2147483647"), "[OK]"));
    CHECK(contains(run(cli, out, "pcb 1"), "Ráfaga Inicial     : 2147483647 ticks"));
    CHECK_EQ(kernel.snapshot().quantum, 2147483647);
}
