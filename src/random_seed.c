#include "log.h"
#include <fcntl.h>
#include <stdint.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

/**
 * @file random_seed.c
 * @brief High-entropy seed generation for the PCG32 random number generator.
 *
 * This module provides a robust entropy source that combines multiple entropy
 * sources to generate cryptographically strong seeds suitable for lottery draws.
 * The seed generation uses a fallback strategy:
 *   1. getrandom() - Linux kernel entropy (preferred)
 *   2. /dev/urandom - Linux character device entropy
 *   3. Time-based jitter - Monotonic clock + RDTSC/clock_gettime
 *
 * Each entropy path is XORed with a timing jitter source (rdtsc or clock)
 * to further increase randomness and prevent predictability.
 */

/**
 * @brief Read CPU TSC (Time Stamp Counter) or monotonic clock for jitter.
 *
 * On x86_64 systems, uses the RDTSC instruction for ultra-high resolution
 * timing. On other architectures, falls back to CLOCK_MONOTONIC.
 * This is used as a jitter source to supplement entropy and prevent
 * timing-based prediction attacks.
 *
 * @return uint64_t - TSC value or monotonic timestamp XOR'd with nanoseconds
 */
#if defined(__x86_64__)
static inline uint64_t rdtsc(void)
{
    unsigned hi, lo;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t) hi << 32) | lo;
}
#else
static inline uint64_t rdtsc(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_nsec ^ (uint64_t) ts.tv_sec;
}
#endif

/**
 * @brief Generate a cryptographically strong seed for lottery RNG.
 *
 * Attempts to obtain high-quality entropy from the system and combines it
 * with timing jitter (TSC or monotonic clock). The seed is generated using
 * a priority fallback approach:
 *
 *   - First try: getrandom() syscall (recommended, strong entropy)
 *   - Second try: /dev/urandom (fallback, good entropy)
 *   - Third try: Time-based fallback (weak entropy, last resort)
 *
 * Each successful entropy source is XOR'd with rdtsc/clock jitter to
 * further randomize the seed and prevent predictable patterns.
 *
 * @return uint64_t - Generated seed value suitable for PCG32 initialization
 *
 * @note This function logs the entropy source selected for debugging purposes.
 * @note All entropy sources are independent and can be used safely.
 */
uint64_t generate_strong_seed(void)
{
    uint64_t seed = 0;

    /* 1) Try Linux getrandom() - Best option */
    log_debug("Attempting to get entropy from getrandom()");
    if (getrandom(&seed, sizeof(seed), 0) == sizeof(seed))
    {
        log_debug("Successfully obtained entropy from getrandom()");
        return seed ^ rdtsc();
    }

    /* 2) Fallback: /dev/urandom */
    log_debug("getrandom() failed, falling back to /dev/urandom");
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0)
    {
        ssize_t n = read(fd, &seed, sizeof(seed));
        close(fd);

        if (n == (ssize_t) sizeof(seed))
        {
            log_debug("Successfully obtained entropy from /dev/urandom");
            return seed ^ rdtsc();
        }

        log_warn("/dev/urandom returned insufficient entropy, using time-based fallback");
    }

    /* 3) Final fallback: time + jitter */
    log_warn("Both getrandom() and /dev/urandom failed, using time-based fallback");
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    /* Combine timestamp components and jitter for weak entropy fallback */
    seed = (((uint64_t) ts.tv_sec) << 32) ^ (uint64_t) ts.tv_nsec ^ rdtsc();

    return seed;
}
