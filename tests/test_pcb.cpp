/**
 * @file test_pcb.cpp
 * @brief Pruebas del PCB y de la tabla de procesos.
 */
#include <string>

#include "pcb.hpp"
#include "process_table.hpp"
#include "test_framework.hpp"

using namespace sim;

TEST_CASE("PCB: un proceso nuevo empieza en estado NEW con la ráfaga completa") {
    PCB p(1, "Proceso_A", 5, 2, 0);
    CHECK_EQ(p.pid, 1);
    CHECK_EQ(p.name, "Proceso_A");
    CHECK_EQ(std::string(stateName(p.state)), "NEW");
    CHECK_EQ(p.burstTotal, 5L);
    CHECK_EQ(p.burstRemaining, 5L);
    CHECK_EQ(p.waitingTime, 0L);
    CHECK(!p.hasStarted());
    CHECK(!p.isFinished());
}

TEST_CASE("PCB: tiempos de retorno, respuesta y ráfaga ejecutada") {
    PCB p(7, "X", 4, 3, 2);
    p.startTime = 5;
    p.burstRemaining = 1;
    CHECK_EQ(p.executedTime(), 3L);
    CHECK_EQ(p.responseTime(), 3L);  // 5 - 2
    p.burstRemaining = 0;
    p.finishTime = 11;
    p.state = ProcessState::Terminated;
    CHECK(p.isFinished());
    CHECK_EQ(p.turnaroundTime(), 9L);  // 11 - 2
}

TEST_CASE("PCB: etiquetas de estado según el formato del enunciado") {
    CHECK_EQ(std::string(stateName(ProcessState::New)), "NEW");
    CHECK_EQ(std::string(stateName(ProcessState::Ready)), "READY");
    CHECK_EQ(std::string(stateName(ProcessState::Running)), "RUNNING");
    CHECK_EQ(std::string(stateName(ProcessState::Blocked)), "BLOCKED");
    CHECK_EQ(std::string(stateName(ProcessState::Terminated)), "TERMINATED");
}

TEST_CASE("Tabla de procesos: PID autoincremental y búsqueda") {
    ProcessTable table;
    PCB& a = table.create("A", 3, 1, 0);
    PCB& b = table.create("B", 2, 5, 4);
    CHECK_EQ(a.pid, 1);
    CHECK_EQ(b.pid, 2);
    CHECK_EQ(table.size(), static_cast<std::size_t>(2));
    CHECK(table.find(1) == &a);
    CHECK(table.find(2) == &b);
    CHECK(table.find(3) == nullptr);
    CHECK(table.find(0) == nullptr);
}

TEST_CASE("Tabla de procesos: conteo de finalizados y liberación de memoria") {
    ProcessTable table;
    table.create("A", 1, 3, 0);
    table.create("B", 1, 3, 0);
    CHECK(table.hasUnfinished());
    CHECK_EQ(table.countFinished(), static_cast<std::size_t>(0));
    table.find(1)->state = ProcessState::Terminated;
    table.find(2)->state = ProcessState::Terminated;
    CHECK(!table.hasUnfinished());
    CHECK_EQ(table.countFinished(), static_cast<std::size_t>(2));
    CHECK_EQ(table.snapshot().size(), static_cast<std::size_t>(2));
    CHECK_EQ(table.clear(), static_cast<std::size_t>(2));
    CHECK_EQ(table.size(), static_cast<std::size_t>(0));
    // Los PID no se reutilizan después de liberar.
    CHECK_EQ(table.create("C", 1, 3, 0).pid, 3);
}
