#ifndef ALIFE_CONFIG_H
#define ALIFE_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum length of a configured path, including its null terminator. */
#define ALIFE_CONFIG_PATH_MAX 256U

/** Controls how much runtime information is written. */
typedef enum {
    ALIFE_LOG_ERROR = 0,
    ALIFE_LOG_SUMMARY = 1,
    ALIFE_LOG_EVENTS = 2
} AlifeLoggingLevel;

/** Contains all user-configurable experiment parameters. */
typedef struct {
    uint64_t seed;
    uint32_t input_size;
    uint32_t hidden_size;
    uint32_t communication_size;
    uint32_t initial_population;
    uint64_t capacity_bytes;
    uint64_t maturity_age;
    uint64_t reproduction_ramp_ticks;
    uint64_t courtship_duration_ticks;
    double awake_age_rate;
    double sleep_age_rate;
    double off_age_rate;
    uint64_t max_without_awake_days;
    double reproduction_base_probability;
    double reproduction_max_probability;
    double mutation_probability;
    double mutation_magnitude;
    double plasticity_limit;
    double plasticity_decay;
    double foolsday_sleep_death_probability;
    uint64_t off_min_duration_ticks;
    uint64_t off_max_duration_ticks;
    double state_transition_threshold;
    uint32_t calendar_start_year;
    uint32_t calendar_start_month;
    uint32_t calendar_start_day;
    uint64_t ticks_per_day;
    uint64_t tick_count;
    uint64_t summary_interval;
    uint64_t checkpoint_interval;
    char checkpoint_path[ALIFE_CONFIG_PATH_MAX];
    char event_log_path[ALIFE_CONFIG_PATH_MAX];
    AlifeLoggingLevel logging_level;
    double max_abs_weight;
} AlifeConfig;

/**
 * Initializes a configuration with conservative experiment defaults.
 *
 * @param config Configuration to initialize. A null pointer is ignored.
 */
void alife_config_defaults(AlifeConfig *config);

/**
 * Loads, validates, and atomically replaces a configuration.
 *
 * The file uses one `key = value` assignment per line. Whitespace is ignored
 * around keys and values, and `#` starts a comment. The loader begins from the
 * standard defaults, so a file can override only the values it needs. Unknown
 * and duplicate keys are errors. If loading fails, `config` is unchanged.
 *
 * @param config Destination configuration.
 * @param path Path to the configuration file.
 * @param error Buffer that receives a human-readable error. May be null when
 *     `error_size` is zero.
 * @param error_size Size of `error` in bytes.
 * @return true on success; otherwise, false.
 */
bool alife_config_load(AlifeConfig *config, const char *path, char *error,
                       size_t error_size);

/**
 * Validates all configuration invariants.
 *
 * @param config Configuration to validate.
 * @param error Buffer that receives a human-readable error. May be null when
 *     `error_size` is zero.
 * @param error_size Size of `error` in bytes.
 * @return true when the configuration is valid; otherwise, false.
 */
bool alife_config_validate(const AlifeConfig *config, char *error,
                           size_t error_size);

/**
 * Compares two configurations field by field.
 *
 * @param left First configuration.
 * @param right Second configuration.
 * @return true when every field is equal; otherwise, false.
 */
bool alife_config_equal(const AlifeConfig *left, const AlifeConfig *right);

/**
 * Computes a deterministic fingerprint of a configuration.
 *
 * The fingerprint detects configuration mismatches in logs and checkpoints.
 * It is not a cryptographic hash.
 *
 * @param config Configuration to fingerprint.
 * @return Noncryptographic 64-bit fingerprint, or zero when `config` is null.
 */
uint64_t alife_config_fingerprint(const AlifeConfig *config);

/* Short compatibility names for embedders that use the module directly. */
void config_defaults(AlifeConfig *config);
bool config_load(AlifeConfig *config, const char *path, char *error,
                 size_t error_size);
bool config_validate(const AlifeConfig *config, char *error,
                     size_t error_size);
bool config_equal(const AlifeConfig *left, const AlifeConfig *right);

#ifdef __cplusplus
}
#endif

#endif
