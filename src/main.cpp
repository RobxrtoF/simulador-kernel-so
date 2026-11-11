/**
 * @file main.cpp
 * @brief Punto de entrada: crea el Kernel y lanza los dos hilos POSIX del simulador.
 *
 * @code
 *   main ──┬── pthread_create ──> Hilo Motor de Scheduling (engineThreadMain)
 *          └── pthread_create ──> Hilo CLI (cliThreadMain)
 * @endcode
 * El hilo principal espera (pthread_join) a que la CLI termine con `exit` o con
 * el fin de la entrada, ordena el apagado del motor, lo espera y libera la
 * memoria dinámica de los PCB.
 */
#include <pthread.h>
#include <unistd.h>

#include <csignal>

#include <cstring>
#include <iostream>

#include "cli.hpp"
#include "engine.hpp"
#include "gui.hpp"
#include "kernel.hpp"

namespace {

/**
 * Ctrl+C (SIGINT) no debe tumbar la simulación: se informa cómo salir y la shell
 * sigue. Solo usa write(), que es seguro dentro de un manejador de señales. Con
 * SA_RESTART, la lectura de la consola (getline) se reanuda sola tras la señal.
 */
void onInterrupt(int) {
    static const char kMessage[] = "\n[INFO]: Ctrl+C no cierra la simulación. Escriba 'exit' para salir.\n> ";
    const ssize_t written = write(STDOUT_FILENO, kMessage, sizeof kMessage - 1);
    (void)written;
}

void installInterruptHandler() {
    struct sigaction action {};
    action.sa_handler = onInterrupt;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &action, nullptr);
}

}  // namespace

int main() {
    installInterruptHandler();  // antes de crear los hilos: todos heredan el manejador

    sim::Kernel kernel;  // estado compartido (mutex + variable de condición)

    pthread_t engineThread;
    int rc = pthread_create(&engineThread, nullptr, sim::engineThreadMain, &kernel);
    if (rc != 0) {
        std::cerr << "[ERROR]: No se pudo crear el hilo del motor: " << std::strerror(rc) << '\n';
        return 1;
    }

    sim::CliThreadArgs cliArgs{&kernel};
    pthread_t cliThread;
    rc = pthread_create(&cliThread, nullptr, sim::cliThreadMain, &cliArgs);
    if (rc != 0) {
        std::cerr << "[ERROR]: No se pudo crear el hilo de la CLI: " << std::strerror(rc) << '\n';
        kernel.requestShutdown();
        pthread_join(engineThread, nullptr);
        return 1;
    }

    // El hilo principal queda libre para la ventana gráfica (bonus). En macOS
    // una ventana solo puede crearse y atenderse desde el hilo principal, así
    // que la CLI no la abre: la pide con Kernel::requestGui() y este hilo, que
    // dormía en pthread_cond_wait, la atiende hasta que se cierre.
    // waitForGuiRequest() devuelve false cuando la CLI termina (exit).
    while (kernel.waitForGuiRequest()) {
        sim::runGui(kernel);
        kernel.notifyGuiClosed();
    }

    pthread_join(cliThread, nullptr);     // la CLI termina con `exit` o fin de la entrada
    kernel.requestShutdown();             // despierta al motor y le pide terminar
    pthread_join(engineThread, nullptr);  // espera a que el motor salga de su bucle

    const std::size_t released = kernel.releaseAll();
    std::cout << "[OK]: Hilos detenidos y memoria liberada (" << released << " PCB). Hasta luego.\n";
    return 0;
}
