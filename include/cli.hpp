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

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

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

/**
 * @brief Intérprete de comandos de la shell.
 *
 * Se puede usar sin hilos (por ejemplo en las pruebas) llamando a execute()
 * con una línea y leyendo la salida del std::ostream recibido.
 */
class Cli {
public:
    Cli(Kernel& kernel, std::ostream& out);

    /// Muestra la presentación inicial.
    void printBanner();

    /**
     * @brief Bucle principal: muestra el prompt, lee y ejecuta comandos.
     * @param echoCommands Repite cada comando leído (útil si la entrada es un archivo).
     */
    void run(std::istream& in, bool echoCommands);

    /**
     * @brief Interpreta y ejecuta una línea.
     * @return false si el comando fue `exit` (la shell debe terminar).
     */
    bool execute(const std::string& line);

private:
    using Args = std::vector<std::string>;

    /// Entrada de la tabla de comandos.
    struct Command {
        const char* name;                     ///< Palabra clave.
        std::size_t minArgs;                  ///< Parámetros obligatorios.
        std::size_t maxArgs;                  ///< Parámetros máximos (obligatorios + opcionales).
        const char* usage;                    ///< Sintaxis mostrada en errores y en help.
        const char* description;              ///< Explicación para help.
        void (Cli::*handler)(const Args&);    ///< Método que ejecuta el comando.
    };

    static const std::vector<Command>& commands();
    static const Command* findCommand(const std::string& name);

    // Un método por comando (los parámetros ya tienen la cantidad correcta).
    void cmdCreate(const Args& args);
    void cmdLoad(const Args& args);
    void cmdPs(const Args& args);
    void cmdPcb(const Args& args);
    void cmdSetSched(const Args& args);
    void cmdSetAging(const Args& args);
    void cmdSetSpeed(const Args& args);
    void cmdStart(const Args& args);
    void cmdPause(const Args& args);
    void cmdBlock(const Args& args);
    void cmdUnblock(const Args& args);
    void cmdGantt(const Args& args);
    void cmdStats(const Args& args);
    void cmdGui(const Args& args);
    void cmdHelp(const Args& args);
    void cmdExit(const Args& args);

    // Validación de valores; si fallan, informan el error y devuelven false.
    bool readInteger(const std::string& text, const std::string& typeError, long& value);
    bool readPid(const std::string& text, int& pid);
    void syntaxError(const Command& command, const std::string& reason);

    Kernel& kernel_;
    std::ostream& out_;
    bool exitRequested_ = false;
};

}  // namespace sim

#endif  // SIM_CLI_HPP
