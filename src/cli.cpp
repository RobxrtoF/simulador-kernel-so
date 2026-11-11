/**
 * @file cli.cpp
 * @brief Shell interactiva: parsing, validación estricta y despliegue de resultados.
 *
 * Categorías de error (formato del enunciado):
 *   [ERROR DE SINTAXIS]   cantidad de parámetros incorrecta o comillas sin cerrar
 *   [ERROR DE TIPO]       se esperaba un número entero y se recibió otra cosa
 *   [ERROR DE PARAMETRO]  valor fuera de rango, algoritmo desconocido, PID inexistente
 *   [ERROR DE ARCHIVO]    el archivo de `load` no existe o no se puede leer
 *   [ERROR DE FORMATO]    el archivo de `load` tiene líneas mal formadas
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

#include "batch_loader.hpp"
#include "gantt.hpp"
#include "gui.hpp"
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
         "Crea un proceso (prioridad 1-5, defecto 3) y lo ingresa a LISTOS.", "create P1 5 2", &Cli::cmdCreate},
        {"load", 1, 1, "load <archivo>",
         "Carga por lotes una traza: NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME.", "load workload_test.txt",
         &Cli::cmdLoad},
        {"ps", 0, 0, "ps", "Tabla con el estado actual de todos los procesos.", nullptr, &Cli::cmdPs},
        {"pcb", 1, 1, "pcb <PID>", "Detalle del Process Control Block de un proceso.", "pcb 1", &Cli::cmdPcb},
        {"set_sched", 1, 2, "set_sched <RR|SRTF> [quantum]",
         "Cambia la política (RR: quantum > 0, defecto 2). Se puede usar en caliente.",
         "set_sched RR 2  o  set_sched SRTF", &Cli::cmdSetSched},
        {"set_aging", 1, 1, "set_aging <ticks>",
         "Envejecimiento de SRTF contra la inanición (0 = desactivado, defecto).", "set_aging 2",
         &Cli::cmdSetAging},
        {"set_speed", 1, 1, "set_speed <ms_por_tick>",
         "Duración real de cada tick de CPU (50 a 5000 ms; defecto 1000).", "set_speed 500", &Cli::cmdSetSpeed},
        {"start", 0, 0, "start", "Inicia o reanuda el reloj de CPU (Hilo Motor).", nullptr, &Cli::cmdStart},
        {"pause", 0, 0, "pause", "Pausa el reloj de CPU para inspeccionar el sistema.", nullptr, &Cli::cmdPause},
        {"block", 1, 1, "block <PID>", "Bloquea un proceso READY o RUNNING (estado BLOCKED).", "block 1",
         &Cli::cmdBlock},
        {"unblock", 1, 1, "unblock <PID>", "Devuelve un proceso bloqueado a la cola de LISTOS.", "unblock 1",
         &Cli::cmdUnblock},
        {"gantt", 0, 0, "gantt", "Diagrama de Gantt acumulado hasta el instante actual.", nullptr, &Cli::cmdGantt},
        {"stats", 0, 0, "stats", "Métricas globales: espera, retorno, respuesta y uso de CPU.", nullptr,
         &Cli::cmdStats},
        {"gui", 0, 0, "gui", "Abre la interfaz gráfica en tiempo real (bonus; compilar con 'make gui').", nullptr,
         &Cli::cmdGui},
        {"help", 0, 0, "help", "Muestra este manual rápido.", nullptr, &Cli::cmdHelp},
        {"exit", 0, 0, "exit", "Detiene los hilos, libera la memoria y cierra la simulación.", nullptr, &Cli::cmdExit},
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

void Cli::cmdLoad(const Args& args) {
    // `load workload_test.txt` (como en el enunciado) también busca en workloads/.
    const std::string path = resolveWorkloadPath(args[0]);
    if (path != args[0]) {
        out_ << "[INFO]: '" << args[0] << "' no está en el directorio actual; se usa '" << path << "'.\n";
    }
    const LoadResult result = parseWorkloadFile(path);

    if (!result.fileOpened) {
        out_ << "[ERROR DE ARCHIVO]: " << result.errors.front() << '\n';
        return;
    }
    if (!result.ok()) {
        out_ << "[ERROR DE FORMATO]: El archivo '" << path << "' tiene " << result.errors.size()
             << " error(es); no se cargó ningún proceso:\n";
        const std::size_t shown = std::min<std::size_t>(result.errors.size(), 10);
        for (std::size_t i = 0; i < shown; ++i) out_ << "  - " << result.errors[i] << '\n';
        if (result.errors.size() > shown) out_ << "  ... y " << (result.errors.size() - shown) << " más.\n";
        out_ << "Formato esperado por línea: NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME\n";
        return;
    }

    long baseTick = 0;
    const std::vector<PCB> created = kernel_.loadProcesses(result.processes, baseTick);
    std::size_t nameWidth = 0;
    for (const PCB& pcb : created) nameWidth = std::max(nameWidth, displayWidth(pcb.name));

    out_ << "[OK]: " << created.size() << " proceso(s) cargado(s) desde '" << path
         << "' (llegadas relativas a t=" << baseTick << "):\n";
    for (const PCB& pcb : created) {
        out_ << "  PID " << padRight(std::to_string(pcb.pid), 3) << ' ' << padRight(pcb.name, nameWidth)
             << "  ráfaga=" << pcb.burstTotal << "  prioridad=" << pcb.priority << "  llegada=t"
             << pcb.arrivalTime << "  -> " << stateName(pcb.state) << '\n';
    }
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

void Cli::cmdSetAging(const Args& args) {
    long ticks = 0;
    if (!readInteger(args[0], "El intervalo de envejecimiento debe ser un número entero de ticks.", ticks)) return;
    if (ticks < 0) {
        out_ << "[ERROR DE PARAMETRO]: El intervalo de envejecimiento debe ser un entero mayor o igual que 0 "
                "(0 = desactivado).\n";
        return;
    }
    kernel_.setAging(static_cast<int>(ticks));

    const SystemSnapshot snap = kernel_.snapshot();
    if (ticks == 0) {
        out_ << "[OK]: Envejecimiento desactivado (SRTF puro).";
    } else {
        out_ << "[OK]: Envejecimiento activado: por cada " << ticks << (ticks == 1 ? " tick" : " ticks")
             << " de espera, la ráfaga efectiva del proceso baja 1 tick.";
    }
    if (snap.policyKind == PolicyKind::SRTF) {
        out_ << " Política: " << snap.policy << ".\n";
    } else {
        out_ << " Se aplica con SRTF (política actual: " << snap.policy << ").\n";
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

void Cli::cmdBlock(const Args& args) {
    int pid = 0;
    if (!readPid(args[0], pid)) return;
    std::string error;
    if (!kernel_.blockProcess(pid, error)) {
        out_ << "[ERROR DE PARAMETRO]: " << error << '\n';
        return;
    }
    out_ << "[OK]: Proceso PID " << pid << " bloqueado (BLOCKED). Use 'unblock " << pid
         << "' para devolverlo a LISTOS.\n";
}

void Cli::cmdUnblock(const Args& args) {
    int pid = 0;
    if (!readPid(args[0], pid)) return;
    std::string error;
    if (!kernel_.unblockProcess(pid, error)) {
        out_ << "[ERROR DE PARAMETRO]: " << error << '\n';
        return;
    }
    out_ << "[OK]: Proceso PID " << pid << " desbloqueado: vuelve a la cola de LISTOS (READY).\n";
}

void Cli::cmdGantt(const Args&) {
    const SystemSnapshot snap = kernel_.snapshot();
    out_ << renderGantt(snap.gantt, snap.clock, snap.cpuIdleWithPendingWork);
}

void Cli::cmdStats(const Args&) { out_ << renderStats(kernel_.snapshot().stats); }

void Cli::cmdGui(const Args&) {
    if (!guiAvailable()) {
        out_ << "[INFO]: Este ejecutable se compiló sin la interfaz gráfica (bonus). "
                "Compile con 'make gui' (requiere raylib) y vuelva a ejecutar ./simulador.\n";
        return;
    }
    // La ventana la abre el hilo principal: aquí solo se le avisa (ver main.cpp).
    if (!kernel_.requestGui()) {
        out_ << "[INFO]: La interfaz gráfica ya está abierta.\n";
        return;
    }
    out_ << "[OK]: Abriendo la interfaz gráfica. La consola sigue activa; cierre la ventana cuando quiera.\n";
}

void Cli::cmdHelp(const Args&) {
    std::size_t width = 0;
    for (const Command& command : commands()) width = std::max(width, displayWidth(command.usage));
    out_ << "MANUAL RÁPIDO DE LA CLI\n";
    for (const Command& command : commands()) {
        out_ << "  " << padRight(command.usage, width + 2) << command.description << '\n';
        // Los comandos con parámetros muestran un ejemplo listo para copiar, alineado con la descripción.
        if (command.example != nullptr) {
            out_ << "  " << std::string(width + 2, ' ') << "Ej.: " << command.example << '\n';
        }
    }
    out_ << "Notas:\n"
         << "  - El tiempo se mide en ticks del reloj simulado (no en tiempo real).\n"
         << "  - Prioridad: 1 = más alta, 5 = más baja.\n"
         << "  - Archivo de traza: una línea por proceso, campos separados por coma;\n"
         << "    las líneas que empiezan con # son comentarios.\n"
         << "  - Errores de sintaxis o de tipo no detienen la simulación.\n";
}

void Cli::cmdExit(const Args&) {
    exitRequested_ = true;
    out_ << "[INFO]: Deteniendo el motor de scheduling y cerrando la simulación...\n";
}

}  // namespace sim
