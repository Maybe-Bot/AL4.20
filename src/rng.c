#include "alife/rng.h"

#include <stddef.h>

static uint64_t rotate_left(const uint64_t value, const unsigned int shift) {
    return (value << shift) | (value >> (64U - shift));
}

static uint64_t splitmix64_next(uint64_t *state) {
    uint64_t value;

    *state += UINT64_C(0x9e3779b97f4a7c15);
    value = *state;
    value = (value ^ (value >> 30U)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

void alife_rng_seed(AlifeRng *rng, const uint64_t seed) {
    uint64_t splitmix_state;
    size_t index;

    if (rng == NULL) {
        return;
    }

    splitmix_state = seed;
    for (index = 0U; index < 4U; ++index) {
        rng->state[index] = splitmix64_next(&splitmix_state);
    }

    /* xoshiro256** reserves the all-zero state. */
    if ((rng->state[0] | rng->state[1] | rng->state[2] | rng->state[3]) ==
        UINT64_C(0)) {
        rng->state[0] = UINT64_C(1);
    }
}

uint64_t alife_rng_u64(AlifeRng *rng) {
    uint64_t result;
    uint64_t temporary;

    if (rng == NULL) {
        return UINT64_C(0);
    }

    result = rotate_left(rng->state[1] * UINT64_C(5), 7U) * UINT64_C(9);
    temporary = rng->state[1] << 17U;

    rng->state[2] ^= rng->state[0];
    rng->state[3] ^= rng->state[1];
    rng->state[1] ^= rng->state[2];
    rng->state[0] ^= rng->state[3];
    rng->state[2] ^= temporary;
    rng->state[3] = rotate_left(rng->state[3], 45U);

    return result;
}

double alife_rng_unit(AlifeRng *rng) {
    const uint64_t value = alife_rng_u64(rng) >> 11U;

    return (double)value * 0x1.0p-53;
}

double alife_rng_symmetric(AlifeRng *rng) {
    if (rng == NULL) {
        return 0.0;
    }
    return (2.0 * alife_rng_unit(rng)) - 1.0;
}

uint64_t alife_rng_bounded(AlifeRng *rng, const uint64_t upper_bound) {
    uint64_t value;
    uint64_t threshold;

    if (rng == NULL || upper_bound == UINT64_C(0)) {
        return UINT64_C(0);
    }

    threshold = (UINT64_C(0) - upper_bound) % upper_bound;
    do {
        value = alife_rng_u64(rng);
    } while (value < threshold);

    return value % upper_bound;
}

void rng_seed(AlifeRng *rng, const uint64_t seed) {
    alife_rng_seed(rng, seed);
}

uint64_t rng_u64(AlifeRng *rng) {
    return alife_rng_u64(rng);
}

double rng_unit(AlifeRng *rng) {
    return alife_rng_unit(rng);
}

double rng_symmetric(AlifeRng *rng, const double magnitude) {
    if (!(magnitude > 0.0)) {
        return 0.0;
    }
    return alife_rng_symmetric(rng) * magnitude;
}

uint64_t rng_bounded(AlifeRng *rng, const uint64_t upper_bound) {
    return alife_rng_bounded(rng, upper_bound);
}
