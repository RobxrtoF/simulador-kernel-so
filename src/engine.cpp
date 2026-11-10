/**
 * @file engine.cpp
 * @brief Bucle del Hilo Motor de Scheduling.
 */
#include "engine.hpp"

#include "kernel.hpp"

namespace sim {

void* engineThreadMain(void* arg) {
    Kernel* kernel = static_cast<Kernel*>(arg);
    // runNextTick() duerme en pthread_cond_wait mientras no haya nada que hacer,
    // ejecuta un tick con el mutex tomado y devuelve false al pedir el apagado.
    while (kernel->runNextTick()) {
        kernel->sleepOneTick();  // retardo real (set_speed) sin bloquear a la CLI
    }
    return nullptr;
}

}  // namespace sim
