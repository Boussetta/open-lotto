#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/random.h>

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

    /* 1) Try Linux getrandom() */
    if (getrandom(&seed, sizeof(seed), 0) == sizeof(seed))
        return seed ^ rdtsc();

    /* 2) Fallback: /dev/urandom */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, &seed, sizeof(seed));
        close(fd);
        return seed ^ rdtsc();
    }

    /* 3) Final fallback: time + jitter */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    seed = ((uint64_t)ts.tv_sec << 32) ^
           (uint64_t)ts.tv_nsec ^
           rdtsc();

    return seed;
}
