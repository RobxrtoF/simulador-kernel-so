/**
 * @file cli.cpp
 * @brief Shell interactiva: parsing, validación estricta y despliegue de resultados.
 *
 * Categorías de error (formato del enunciado):
 *   [ERROR DE SINTAXIS]   cantidad de parámetros incorrecta o comillas sin cerrar
 *   [ERROR DE TIPO]       se esperaba un número entero y se recibió otra cosa
 *   [ERROR DE PARAMETRO]  valor fuera de rango, algoritmo desconocido, PID inexistente
 *   [ERROR]               comando no reconocido o error inesperado
 * Ningún error termina el programa: se informa y se solicita la siguiente entrada.
 */
#include "cli.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>
#include <sstream>

#include "gantt.hpp"
#include "metrics.hpp"
#include "parser.hpp"
#include "report.hpp"

namespace sim {

namespace {

constexpr int kMinMsPerTick = 50;    ///< Límite inferior de set_speed.
constexpr int kMaxMsPerTick = 5000;  ///< Límite superior de set_speed.

std::string toLower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string toUpper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

// ============================== Hilo CLI ==============================

void* cliThreadMain(void* arg) {
    auto* args = static_cast<CliThreadArgs*>(arg);
    Cli cli(*args->kernel, std::cout);
    // Si la entrada no es una terminal (archivo o tubería), se repite cada
    // comando en la salida para que la transcripción se pueda leer.
    cli.run(std::cin, isatty(STDIN_FILENO) == 0);
    args->kernel->requestShutdown();
    return nullptr;
}

// ============================ Tabla de comandos ============================

const std::vector<Cli::Command>& Cli::commands() {
    static const std::vector<Command> table = {
        {"create", 2, 3, "create <nombre> <burst_time> [prioridad]",
         "Crea un proceso (prioridad 1-5, defecto 3) y lo ingresa a LISTOS.", &Cli::cmdCreate},
        {"ps", 0, 0, "ps", "Tabla con el estado actual de todos los procesos.", &Cli::cmdPs},
        {"pcb", 1, 1, "pcb <PID>", "Detalle del Process Control Block de un proceso.", &Cli::cmdPcb},
        {"set_sched", 1, 2, "set_sched <RR|SRTF> [quantum]",
         "Cambia la política (RR: quantum > 0, defecto 2). Se puede usar en caliente.", &Cli::cmdSetSched},
        {"set_speed", 1, 1, "set_speed <ms_por_tick>",
         "Duración real de cada tick de CPU (50 a 5000 ms; defecto 1000).", &Cli::cmdSetSpeed},
        {"start", 0, 0, "start", "Inicia o reanuda el reloj de CPU (Hilo Motor).", &Cli::cmdStart},
        {"pause", 0, 0, "pause", "Pausa el reloj de CPU para inspeccionar el sistema.", &Cli::cmdPause},
        {"gantt", 0, 0, "gantt", "Diagrama de Gantt acumulado hasta el instante actual.", &Cli::cmdGantt},
        {"stats", 0, 0, "stats", "Métricas globales: espera, retorno, respuesta y uso de CPU.",
         &Cli::cmdStats},
        {"gui", 0, 0, "gui", "Interfaz gráfica (bonus opcional; no incluida en esta versión).", &Cli::cmdGui},
        {"help", 0, 0, "help", "Muestra este manual rápido.", &Cli::cmdHelp},
        {"exit", 0, 0, "exit", "Detiene los hilos, libera la memoria y cierra la simulación.", &Cli::cmdExit},
    };
    return table;
}

const Cli::Command* Cli::findCommand(const std::string& name) {
    for (const Command& command : commands()) {
        if (name == command.name) return &command;
    }
    return nullptr;
}

// ============================== Bucle principal ==============================

Cli::Cli(Kernel& kernel, std::ostream& out) : kernel_(kernel), out_(out) {}

void Cli::printBanner() {
    const SystemSnapshot snap = kernel_.snapshot();
    const std::string line(58, '=');
    out_ << line << '\n'
         << "   SIMULADOR DE KERNEL - Shell & Scheduling Engine\n"
         << "   Proyecto Sistemas Operativos - Fase I (UCAB)\n"
         << line << '\n'
         << "Política: " << snap.policy << " | Reloj: " << snap.msPerTick << " ms/tick (en pausa)\n"
         << "Escriba 'help' para ver los comandos disponibles.\n\n";
}

void Cli::run(std::istream& in, bool echoCommands) {
    printBanner();
    std::string line;
    while (!exitRequested_) {
        out_ << "> " << std::flush;
        if (!std::getline(in, line)) {
            out_ << "\n[INFO]: Fin de la entrada. Cerrando la simulación...\n";
            break;
        }
        if (echoCommands) out_ << line << '\n';
        execute(line);
        out_ << std::flush;
    }
}

bool Cli::execute(const std::string& line) {
    try {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') return true;  // línea vacía o comentario

        const TokenizeResult parsed = tokenize(trimmed);
        if (!parsed.ok) {
            out_ << "[ERROR DE SINTAXIS]: " << parsed.error << '\n';
            return true;
        }

        const std::string& name = parsed.tokens.front();
        const Command* command = findCommand(name);
        if (command == nullptr) {
            out_ << "[ERROR]: Comando no reconocido: '" << name << "'.";
            if (const Command* similar = findCommand(toLower(name))) {
                out_ << " ¿Quiso decir '" << similar->name << "'?";
            }
            out_ << " Escriba 'help' para ver los comandos disponibles.\n";
            return true;
        }

        const Args args(parsed.tokens.begin() + 1, parsed.tokens.end());
        if (args.size() < command->minArgs) {
            syntaxError(*command, "Parámetros insuficientes.");
        } else if (args.size() > command->maxArgs) {
            syntaxError(*command, command->maxArgs == 0
                                      ? "El comando '" + std::string(command->name) + "' no recibe parámetros."
                                      : std::string("Demasiados parámetros."));
        } else {
            (this->*(command->handler))(args);
        }
    } catch (const std::exception& error) {
        // Red de seguridad: la shell nunca debe caerse por una entrada inesperada.
        out_ << "[ERROR]: Error inesperado al ejecutar el comando: " << error.what() << '\n';
    }
    return !exitRequested_;
}

// ============================ Validaciones comunes ============================

void Cli::syntaxError(const Command& command, const std::string& reason) {
    out_ << "[ERROR DE SINTAXIS]: " << reason << "\nUso correcto: " << command.usage << '\n';
}

bool Cli::readInteger(const std::string& text, const std::string& typeError, long& value) {
    switch (parseInteger(text, value)) {
        case IntParse::Ok:
            return true;
        case IntParse::NotANumber:
            out_ << "[ERROR DE TIPO]: " << typeError << '\n';
            return false;
        case IntParse::OutOfRange:
            out_ << "[ERROR DE PARAMETRO]: El valor '" << text << "' está fuera de rango.\n";
            return false;
    }
    return false;
}

bool Cli::readPid(const std::string& text, int& pid) {
    long value = 0;
    const IntParse result = parseInteger(text, value);
    if (result == IntParse::NotANumber) {
        out_ << "[ERROR DE TIPO]: El PID debe ser un número entero positivo.\n";
        return false;
    }
    if (result == IntParse::OutOfRange || value <= 0) {
        out_ << "[ERROR DE PARAMETRO]: El PID debe ser un número entero positivo.\n";
        return false;
    }
    pid = static_cast<int>(value);
    return true;
}

// ================================= Comandos =================================

void Cli::cmdCreate(const Args& args) {
    const std::string& name = args[0];
    std::string reason;
    if (!isValidProcessName(name, reason)) {
        out_ << "[ERROR DE PARAMETRO]: " << reason << '\n';
        return;
    }

    long burst = 0;
    if (!readInteger(args[1], "El burst_time debe ser un número entero.", burst)) return;
    if (burst <= 0) {
        out_ << "[ERROR DE PARAMETRO]: El burst_time debe ser un entero mayor que 0.\n";
        return;
    }

    long priority = kDefaultPriority;
    if (args.size() == 3) {
        if (!readInteger(args[2], "La prioridad debe ser un número entero.", priority)) return;
        if (priority < kMinPriority || priority > kMaxPriority) {
            out_ << "[ERROR DE PARAMETRO]: La prioridad debe estar entre " << kMinPriority << " y "
                 << kMaxPriority << ".\n";
            return;
        }
    }

    const PCB pcb = kernel_.createProcess(name, burst, static_cast<int>(priority));
    out_ << "[OK]: Proceso '" << pcb.name << "' creado con PID " << pcb.pid << " (ráfaga: " << pcb.burstTotal
         << ", prioridad: " << pcb.priority << ", llegada: t=" << pcb.arrivalTime << ") -> "
         << stateName(pcb.state) << ".\n";
}

void Cli::cmdPs(const Args&) { out_ << renderProcessTable(kernel_.snapshot()); }

void Cli::cmdPcb(const Args& args) {
    int pid = 0;
    if (!readPid(args[0], pid)) return;
    PCB pcb(0, "", 0, kDefaultPriority, 0);
    if (!kernel_.findProcess(pid, pcb)) {
        out_ << "[ERROR DE PARAMETRO]: No existe un proceso con PID " << pid << ".\n";
        return;
    }
    out_ << renderPcb(pcb);
}

void Cli::cmdSetSched(const Args& args) {
    const std::string& algorithm = args[0];

    if (algorithm == "RR") {
        long quantum = kDefaultQuantum;
        if (args.size() == 2) {
            if (!readInteger(args[1], "El quantum debe ser un número entero.", quantum)) return;
            if (quantum <= 0) {
                out_ << "[ERROR DE PARAMETRO]: El quantum debe ser un entero mayor que 0.\n";
                return;
            }
        }
        kernel_.setPolicy(PolicyKind::RoundRobin, static_cast<int>(quantum));
        out_ << "[OK]: Política de planificación: " << kernel_.snapshot().policy << ".\n";
        return;
    }

    if (algorithm == "SRTF") {
        if (args.size() == 2) {
            out_ << "[ERROR DE SINTAXIS]: SRTF no utiliza quantum.\nUso correcto: set_sched SRTF\n";
            return;
        }
        kernel_.setPolicy(PolicyKind::SRTF, kDefaultQuantum);
        out_ << "[OK]: Política de planificación: " << kernel_.snapshot().policy << ".\n";
        return;
    }

    out_ << "[ERROR DE PARAMETRO]: Algoritmo no reconocido. Algoritmos válidos: RR, SRTF.\n";
    const std::string upper = toUpper(algorithm);
    if (upper == "RR" || upper == "SRTF") {
        out_ << "Sugerencia: el nombre distingue mayúsculas; escriba '" << upper << "'.\n";
    }
}

void Cli::cmdSetSpeed(const Args& args) {
    long ms = 0;
    if (!readInteger(args[0], "ms_por_tick debe ser un número entero de milisegundos.", ms)) return;
    if (ms < kMinMsPerTick || ms > kMaxMsPerTick) {
        out_ << "[ERROR DE PARAMETRO]: ms_por_tick debe estar entre " << kMinMsPerTick << " y " << kMaxMsPerTick
             << " ms.\n";
        return;
    }
    kernel_.setSpeed(static_cast<int>(ms));
    out_ << "[OK]: Velocidad del reloj: " << ms << " ms por tick.\n";
}

void Cli::cmdStart(const Args&) {
    const SystemSnapshot before = kernel_.snapshot();  // en pausa el reloj no avanza
    if (!kernel_.start()) {
        out_ << "[INFO]: El motor de scheduling ya está en ejecución.\n";
        return;
    }
    const bool pending = std::any_of(before.processes.begin(), before.processes.end(),
                                     [](const PCB& pcb) { return !pcb.isFinished(); });
    out_ << "[OK]: Motor de scheduling en ejecución desde t=" << before.clock << " | Política: " << before.policy
         << " | " << before.msPerTick << " ms/tick.";
    if (!pending) out_ << " Esperando procesos...";
    out_ << '\n';
}

void Cli::cmdPause(const Args&) {
    if (!kernel_.pause()) {
        out_ << "[INFO]: El motor de scheduling ya está en pausa.\n";
        return;
    }
    out_ << "[OK]: Reloj de CPU en pausa en t=" << kernel_.snapshot().clock
         << ". Use 'start' para reanudar.\n";
}

void Cli::cmdGantt(const Args&) {
    const SystemSnapshot snap = kernel_.snapshot();
    out_ << renderGantt(snap.gantt, snap.clock, snap.cpuIdleWithPendingWork);
}

void Cli::cmdStats(const Args&) { out_ << renderStats(kernel_.snapshot().stats); }

void Cli::cmdGui(const Args&) {
    out_ << "[INFO]: La interfaz gráfica es un bonus opcional y no está incluida en esta versión.\n";
}

void Cli::cmdHelp(const Args&) {
    std::size_t width = 0;
    for (const Command& command : commands()) width = std::max(width, displayWidth(command.usage));
    out_ << "MANUAL RÁPIDO DE LA CLI\n";
    for (const Command& command : commands()) {
        out_ << "  " << padRight(command.usage, width + 2) << command.description << '\n';
    }
    out_ << "Notas:\n"
         << "  - El tiempo se mide en ticks del reloj simulado (no en tiempo real).\n"
         << "  - Prioridad: 1 = más alta, 5 = más baja.\n"
         << "  - Errores de sintaxis o de tipo no detienen la simulación.\n";
}

void Cli::cmdExit(const Args&) {
    exitRequested_ = true;
    out_ << "[INFO]: Deteniendo el motor de scheduling y cerrando la simulación...\n";
}

}  // namespace sim
