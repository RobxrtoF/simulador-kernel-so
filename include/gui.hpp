/**
 * @file gui.hpp
 * @brief Interfaz gráfica en tiempo real (bonus): ventana auxiliar del comando `gui`.
 *
 * La ventana muestra la tabla de procesos, la CPU, la cola de LISTOS, las
 * métricas y el diagrama de Gantt mientras el Hilo Motor ejecuta, y permite
 * iniciar/pausar el reloj y cambiar la política, el quantum, el
 * envejecimiento y la velocidad.
 *
 * Hilos: la ventana se atiende en el HILO PRINCIPAL (en macOS es obligatorio
 * que la ventana viva allí). El Hilo CLI solo la pide con Kernel::requestGui();
 * main(), que dormía en Kernel::waitForGuiRequest(), llama a runGui().
 *
 * Sincronización: la ventana no toca el estado directamente. En cada cuadro
 * toma una copia con Kernel::snapshot() y actúa con los mismos métodos que la
 * CLI (start, pause, setPolicy, setAging, setSpeed), todos bajo el mutex.
 *
 * Compilación: la interfaz es opcional. Se incluye con `make gui` (o
 * `cmake -DSIM_GUI=ON`), que definen SIM_WITH_GUI y enlazan raylib. Sin esa
 * opción el proyecto compila igual y `gui` explica cómo activarla.
 */
#ifndef SIM_GUI_HPP
#define SIM_GUI_HPP

namespace sim {

class Kernel;

/// true si este ejecutable se compiló con la interfaz gráfica.
bool guiAvailable();

/**
 * @brief Abre la ventana y la atiende hasta que el usuario la cierre o se pida el apagado.
 *
 * Debe llamarse desde el hilo principal. Si la ventana no se puede crear (por
 * ejemplo, no hay entorno gráfico) informa el motivo y vuelve de inmediato.
 */
void runGui(Kernel& kernel);

}  // namespace sim

#endif  // SIM_GUI_HPP
