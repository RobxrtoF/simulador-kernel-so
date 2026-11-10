/**
 * @file cli.hpp
 * @brief Shell interactiva de gestión (Hilo CLI).
 *
 * El Hilo CLI lee comandos, los valida de forma estricta y despliega
 * resultados. Nunca modifica el estado directamente: todo pasa por el Kernel,
 * que protege cada acceso con su mutex. Por eso la CLI no se bloquea ni se cae
 * mientras el Hilo Motor ejecuta procesos.
 */
#ifndef SIM_CLI_HPP
#define SIM_CLI_HPP

#include "kernel.hpp"

namespace sim {

/// Argumentos que recibe el Hilo CLI a través de pthread_create.
struct CliThreadArgs {
    Kernel* kernel;  ///< Estado compartido con el Hilo Motor.
};

/**
 * @brief Función de entrada del Hilo CLI (firma requerida por pthread_create).
 *
 * Lee comandos de la entrada estándar hasta `exit` o fin de la entrada y, al
 * terminar, pide el apagado del motor.
 */
void* cliThreadMain(void* args);

}  // namespace sim

#endif  // SIM_CLI_HPP
