/**
 * @file engine.cpp
 * @brief Bucle del Hilo Motor de Scheduling.
 */
#include "engine.hpp"

#include <exception>
#include <iostream>

#include "kernel.hpp"

namespace sim {

void* engineThreadMain(void* arg) {
    Kernel* kernel = static_cast<Kernel*>(arg);
    // runNextTick() duerme en pthread_cond_wait mientras no haya nada que hacer,
    // ejecuta un tick con el mutex tomado y devuelve false al pedir el apagado.
    // Una excepción que escapara de la función del hilo llamaría a std::terminate y
    // cerraría todo el programa. Se atrapa aquí: el motor se detiene, pero la consola
    // sigue respondiendo y el usuario puede consultar el estado y salir con exit.
    try {
        while (kernel->runNextTick()) {
            kernel->sleepOneTick();  // retardo real (set_speed) sin bloquear a la CLI
        }
    } catch (const std::exception& error) {
        std::cerr << "\n[ERROR]: El motor de scheduling se detuvo por un error inesperado: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "\n[ERROR]: El motor de scheduling se detuvo por un error inesperado.\n";
    }
    return nullptr;
}

}  // namespace sim
