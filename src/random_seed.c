/* SPDX-FileCopyrightText: 2025 Wissem Boussetta
 * SPDX-License-Identifier: MIT
 */

#ifdef _WIN32
#define _CRT_RAND_S /* enables rand_s() (RtlGenRandom-backed CSPRNG) */
#endif

#include "log.h"
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/random.h> /* getrandom() on Linux, getentropy() on macOS */
#include <unistd.h>
#endif

/**
 * @file random_seed.c
 * @brief High-entropy seed generation for the PCG32 random number generator.
 *
 * Seeds come from the operating system CSPRNG, using the first source that
 * succeeds:
 *   1. getrandom() on Linux, getentropy() on macOS, rand_s() on Windows
 *   2. /dev/urandom (POSIX)
 *   3. Clock-based fallback (weak; logged as a warning)
 *
 * The OS value is XORed with a CPU timestamp counter reading. That cannot
 * weaken an OS-provided seed and adds a little variation to the weak fallback.
 */

/**
 * @brief Read CPU TSC (Time Stamp Counter) or system timer for jitter.
 *
 * On x86_64 systems, uses the RDTSC instruction for ultra-high resolution
 * timing. On other architectures or Windows, falls back to system clock.
 * This is used as a jitter source to supplement entropy and prevent
 * timing-based prediction attacks.
 *
 * @return uint64_t - TSC value or system timer timestamp
 */
#if defined(__x86_64__) && !defined(_WIN32)
static inline uint64_t rdtsc(void)
{
    unsigned hi, lo;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
/* Windows or non-x86 fallback */
static inline uint64_t rdtsc(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_nsec ^ (uint64_t)ts.tv_sec;
}
#endif

#ifndef _WIN32
/** @brief Read exactly len bytes from /dev/urandom, retrying on EINTR and short reads. */
static int read_urandom(void *buf, size_t len)
{
    int fd;
    do
    {
        fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return -1;

    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < len)
    {
        ssize_t n = read(fd, p + got, len - got);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
        {
            close(fd);
            return -1;
        }
        got += (size_t)n;
    }
    close(fd);
    return 0;
}

/** @brief Fill buf from the kernel CSPRNG, retrying on EINTR and short reads. */
static int os_random(void *buf, size_t len)
{
#if defined(__APPLE__)
    return getentropy(buf, len) == 0 ? 0 : -1;
#else
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < len)
    {
        ssize_t n = getrandom(p + got, len - got, 0);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
#endif
}
#endif

/**
 * @brief Generate a strong 64-bit seed using the best available entropy source.
 *
 * @return uint64_t - Seed suitable for PCG32 initialization.
 */
uint64_t generate_strong_seed(void)
{
    uint64_t seed = 0;

#ifdef _WIN32
    unsigned int lo = 0, hi = 0;
    if (rand_s(&lo) == 0 && rand_s(&hi) == 0)
    {
        seed = ((uint64_t)hi << 32) | (uint64_t)lo;
        return seed ^ rdtsc();
    }
    log_warn("rand_s() failed, using weak time-based seed");
#else
    if (os_random(&seed, sizeof(seed)) == 0)
        return seed ^ rdtsc();

    log_debug("OS random API failed, falling back to /dev/urandom");
    if (read_urandom(&seed, sizeof(seed)) == 0)
        return seed ^ rdtsc();

    log_warn("No OS entropy source available, using weak time-based seed");
#endif

    /* Last resort: clock + timestamp counter (predictable; not for anything security-sensitive) */
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    seed = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^ rdtsc();
    return seed;
}
