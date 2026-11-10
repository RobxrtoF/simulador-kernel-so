/**
 * @file sync.hpp
 * @brief Utilidades de sincronización sobre la API de hilos POSIX.
 */
#ifndef SIM_SYNC_HPP
#define SIM_SYNC_HPP

#include <pthread.h>

namespace sim {

/**
 * @brief Cerrojo RAII sobre un pthread_mutex_t.
 *
 * El constructor llama a pthread_mutex_lock y el destructor a
 * pthread_mutex_unlock, de modo que el mutex se libera siempre al salir del
 * bloque, incluso si se produce una excepción o un `return` anticipado.
 *
 * @code
 *   {
 *       MutexGuard lock(mutex_);   // pthread_mutex_lock
 *       ...sección crítica...
 *   }                              // pthread_mutex_unlock
 * @endcode
 */
class MutexGuard {
public:
    explicit MutexGuard(pthread_mutex_t& mutex) : mutex_(mutex) { pthread_mutex_lock(&mutex_); }
    ~MutexGuard() { pthread_mutex_unlock(&mutex_); }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

private:
    pthread_mutex_t& mutex_;
};

}  // namespace sim

#endif  // SIM_SYNC_HPP
