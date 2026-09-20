#ifndef ALIFE_RNG_H
#define ALIFE_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Serializable state for the xoshiro256** pseudorandom number generator. */
typedef struct {
    uint64_t state[4];
} AlifeRng;

/**
 * Initializes a pseudorandom stream from a 64-bit seed.
 *
 * Every seed, including zero, produces a valid deterministic stream.
 *
 * @param rng Generator to initialize. A null pointer is ignored.
 * @param seed Stream seed.
 */
void alife_rng_seed(AlifeRng *rng, uint64_t seed);

/**
 * Generates an unsigned 64-bit value.
 *
 * @param rng Initialized generator.
 * @return Next value in the stream, or zero when `rng` is null.
 */
uint64_t alife_rng_u64(AlifeRng *rng);

/**
 * Generates a uniformly distributed floating-point value in [0, 1).
 *
 * @param rng Initialized generator.
 * @return Next value in the stream, or 0.0 when `rng` is null.
 */
double alife_rng_unit(AlifeRng *rng);

/**
 * Generates a uniformly distributed value in [-1, 1).
 *
 * @param rng Initialized generator.
 * @return Next symmetric value, or 0.0 when `rng` is null.
 */
double alife_rng_symmetric(AlifeRng *rng);

/**
 * Generates an unbiased unsigned value in [0, upper_bound).
 *
 * Rejection sampling avoids modulo bias. An upper bound of zero returns zero
 * without advancing the generator.
 *
 * @param rng Initialized generator.
 * @param upper_bound Exclusive upper bound.
 * @return Next bounded value, or zero for invalid input.
 */
uint64_t alife_rng_bounded(AlifeRng *rng, uint64_t upper_bound);

/* Short compatibility names for embedders that use the module directly. */
void rng_seed(AlifeRng *rng, uint64_t seed);
uint64_t rng_u64(AlifeRng *rng);
double rng_unit(AlifeRng *rng);
double rng_symmetric(AlifeRng *rng, double magnitude);
uint64_t rng_bounded(AlifeRng *rng, uint64_t upper_bound);

#ifdef __cplusplus
}
#endif

#endif
