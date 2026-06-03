#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/random.h>
#include "log.h"

#if defined(__x86_64__)
static inline uint64_t rdtsc(void)
{
    unsigned hi, lo;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
static inline uint64_t rdtsc(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec;
}
#endif

uint64_t generate_strong_seed(void)
{
    uint64_t seed = 0;

    /* 1) Try Linux getrandom() - Best option */
    log_debug("Attempting to get entropy from getrandom()");
    if (getrandom(&seed, sizeof(seed), 0) == sizeof(seed)) {
        log_debug("Successfully obtained entropy from getrandom()");
        return seed ^ rdtsc();
    }

    /* 2) Fallback: /dev/urandom */
    log_debug("getrandom() failed, falling back to /dev/urandom");
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, &seed, sizeof(seed));
        close(fd);

        if (n == (ssize_t)sizeof(seed)) {
            log_debug("Successfully obtained entropy from /dev/urandom");
            return seed ^ rdtsc();
        }

        log_warn("/dev/urandom returned insufficient entropy, using time-based fallback");
    }

    /* 3) Final fallback: time + jitter */
    log_warn("Both getrandom() and /dev/urandom failed, using time-based fallback");
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    seed = ((uint64_t)ts.tv_sec << 32) ^
           (uint64_t)ts.tv_nsec ^
           rdtsc();

    return seed;
}
