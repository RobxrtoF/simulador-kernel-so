/**
 * @file engine.hpp
 * @brief Hilo Motor de Scheduling (CPU simulada).
 */
#ifndef SIM_ENGINE_HPP
#define SIM_ENGINE_HPP

namespace sim {

/**
 * @brief Función de entrada del Hilo Motor, con la firma que exige pthread_create.
 *
 * Bucle: espera a que haya trabajo y el reloj esté en marcha, ejecuta un tick
 * bajo el mutex (Kernel::runNextTick) y luego hace el retardo real del tick sin
 * el mutex (Kernel::sleepOneTick). Termina cuando se pide el apagado.
 *
 * @param kernel Puntero a sim::Kernel (compartido con el Hilo CLI).
 * @return Siempre nullptr.
 */
void* engineThreadMain(void* kernel);

}  // namespace sim

#endif  // SIM_ENGINE_HPP
