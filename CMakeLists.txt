# =============================================================================
#  Simulador de Kernel y Gestor de Procesos en Consola - build alternativo (CMake)
#
#    cmake -S . -B build-cmake
#    cmake --build build-cmake
#    ctest --test-dir build-cmake --output-on-failure
#
#  Con la interfaz gráfica (bonus):
#    cmake -S . -B build-gui -DSIM_GUI=ON     (usa raylib si está instalada;
#    cmake --build build-gui                   si no, la descarga y la compila)
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(SimuladorKernel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Hilos POSIX: equivale a compilar y enlazar con -pthread.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Interfaz gráfica opcional (bonus). Se resuelve ANTES de fijar las advertencias
# del proyecto para que no se apliquen al código de raylib.
option(SIM_GUI "Compilar la interfaz gráfica del comando gui (requiere raylib)" OFF)
if(SIM_GUI)
  find_package(raylib QUIET)
  if(NOT raylib_FOUND)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
    FetchContent_MakeAvailable(raylib)
  endif()
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)

# Toda la lógica (excepto main.cpp) va en una biblioteca que reutilizan
# el ejecutable y las pruebas.
file(GLOB SIM_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(FILTER SIM_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_executable(simulador src/main.cpp)
target_include_directories(simulador PRIVATE include)
target_link_libraries(simulador PRIVATE Threads::Threads)

if(SIM_SOURCES)
  add_library(simcore STATIC ${SIM_SOURCES})
  target_include_directories(simcore PUBLIC include)
  target_link_libraries(simcore PUBLIC Threads::Threads)
  target_link_libraries(simulador PRIVATE simcore)
  if(SIM_GUI)
    target_compile_definitions(simcore PUBLIC SIM_WITH_GUI)
    target_link_libraries(simcore PUBLIC raylib)
    if(APPLE)
      target_link_libraries(simcore PUBLIC objc)  # la ventana desactiva App Nap (ver src/gui.cpp)
    endif()
  endif()
endif()

enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(run_tests ${TEST_SOURCES})
target_include_directories(run_tests PRIVATE include tests)
target_link_libraries(run_tests PRIVATE Threads::Threads)
if(SIM_SOURCES)
  target_link_libraries(run_tests PRIVATE simcore)
endif()
# Desde la raíz del repositorio, como `make test`: algunas pruebas leen workloads/.
add_test(NAME pruebas_unitarias COMMAND run_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
