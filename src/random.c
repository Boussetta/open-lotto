#include "random.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* SplitMix64 for seeding */
static uint64_t splitmix64(uint64_t *x)
{
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* Try to get entropy from /dev/urandom */
uint64_t rng_seed(void)
{
    uint64_t seed = 0;
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        read(fd, &seed, sizeof(seed));
        close(fd);
        return seed;
    }

    /* fallback */
    return (uint64_t)time(NULL) ^ (uint64_t)getpid();
}

/* xoshiro256** */
uint64_t rng_next(uint64_t *state)
{
    uint64_t s = *state;

    /* scramble state using splitmix64 */
    s = splitmix64(&s);
    *state = s;

    /* return scrambled value */
    return s;
}
