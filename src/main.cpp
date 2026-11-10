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

#include <cstring>
#include <iostream>

#include "cli.hpp"
#include "engine.hpp"
#include "kernel.hpp"

int main() {
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

    pthread_join(cliThread, nullptr);     // la CLI termina con `exit` o fin de la entrada
    kernel.requestShutdown();             // despierta al motor y le pide terminar
    pthread_join(engineThread, nullptr);  // espera a que el motor salga de su bucle

    const std::size_t released = kernel.releaseAll();
    std::cout << "[OK]: Hilos detenidos y memoria liberada (" << released << " PCB). Hasta luego.\n";
    return 0;
}
