# =============================================================================
#  Simulador de Kernel y Gestor de Procesos en Consola - build alternativo (CMake)
#
#    cmake -S . -B build-cmake
#    cmake --build build-cmake
#    ctest --test-dir build-cmake --output-on-failure
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
endif()

enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
add_executable(run_tests ${TEST_SOURCES})
target_include_directories(run_tests PRIVATE include tests)
target_link_libraries(run_tests PRIVATE Threads::Threads)
if(SIM_SOURCES)
  target_link_libraries(run_tests PRIVATE simcore)
endif()
add_test(NAME pruebas_unitarias COMMAND run_tests)
