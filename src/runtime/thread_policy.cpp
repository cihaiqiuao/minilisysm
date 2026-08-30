#include "minilisysm/runtime/thread_policy.hpp"

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace lisysm {

bool set_current_thread_affinity(int cpu, std::string* error) {
    if (cpu < 0) {
        return true;
    }
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        if (error != nullptr) {
            *error = std::strerror(rc);
        }
        return false;
    }
    return true;
#else
    if (error != nullptr) {
        *error = "CPU affinity is only implemented on Linux";
    }
    return false;
#endif
}

bool set_current_thread_nice(int nice_value, std::string* error) {
#if defined(__linux__)
    errno = 0;
    if (setpriority(PRIO_PROCESS, 0, nice_value) != 0) {
        if (error != nullptr) {
            *error = std::strerror(errno);
        }
        return false;
    }
    return true;
#else
    (void)nice_value;
    if (error != nullptr) {
        *error = "nice is only implemented on Linux";
    }
    return false;
#endif
}

} // namespace lisysm
