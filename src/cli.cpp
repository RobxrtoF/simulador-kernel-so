/**
 * @file cli.cpp
 * @brief Esqueleto del Hilo CLI: valida la arquitectura de dos hilos.
 *
 * Por ahora solo controla el reloj (start, pause) y la salida (exit). El
 * intérprete completo de comandos se integra en el módulo de la CLI.
 */
#include "cli.hpp"

#include <iostream>
#include <string>

#include "parser.hpp"

namespace sim {

void* cliThreadMain(void* arg) {
    auto* args = static_cast<CliThreadArgs*>(arg);
    Kernel& kernel = *args->kernel;

    std::cout << "Simulador de Kernel - esqueleto de concurrencia (comandos: start, pause, exit)\n";
    std::string line;
    while (std::cout << "> " << std::flush && std::getline(std::cin, line)) {
        const std::string command = trim(line);
        if (command == "exit") break;
        if (command == "start") {
            std::cout << (kernel.start() ? "[OK]: Motor iniciado.\n" : "[INFO]: El motor ya está en ejecución.\n");
        } else if (command == "pause") {
            std::cout << (kernel.pause() ? "[OK]: Motor en pausa.\n" : "[INFO]: El motor ya está en pausa.\n");
        } else if (!command.empty()) {
            std::cout << "[INFO]: Comando aún no disponible: " << command << '\n';
        }
    }
    kernel.requestShutdown();
    return nullptr;
}

}  // namespace sim
