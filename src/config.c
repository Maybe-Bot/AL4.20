#include "alife/config.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    KEY_SEED = 0,
    KEY_INPUT_SIZE,
    KEY_HIDDEN_SIZE,
    KEY_COMMUNICATION_SIZE,
    KEY_INITIAL_POPULATION,
    KEY_CAPACITY_BYTES,
    KEY_MATURITY_AGE,
    KEY_REPRODUCTION_RAMP_TICKS,
    KEY_REPRODUCTION_BASE_PROBABILITY,
    KEY_REPRODUCTION_MAX_PROBABILITY,
    KEY_MUTATION_PROBABILITY,
    KEY_MUTATION_MAGNITUDE,
    KEY_PLASTICITY_LIMIT,
    KEY_PLASTICITY_DECAY,
    KEY_FOOLSDAY_SLEEP_DEATH_PROBABILITY,
    KEY_OFF_MIN_DURATION_TICKS,
    KEY_OFF_MAX_DURATION_TICKS,
    KEY_STATE_TRANSITION_THRESHOLD,
    KEY_CALENDAR_START_YEAR,
    KEY_CALENDAR_START_MONTH,
    KEY_CALENDAR_START_DAY,
    KEY_TICKS_PER_DAY,
    KEY_TICK_COUNT,
    KEY_SUMMARY_INTERVAL,
    KEY_CHECKPOINT_INTERVAL,
    KEY_CHECKPOINT_PATH,
    KEY_EVENT_LOG_PATH,
    KEY_LOGGING_LEVEL,
    KEY_MAX_ABS_WEIGHT,
    KEY_COUNT
} ConfigKey;

typedef struct {
    const char *name;
    ConfigKey key;
} KeyDefinition;

static const KeyDefinition KEY_DEFINITIONS[] = {
    {"seed", KEY_SEED},
    {"input_size", KEY_INPUT_SIZE},
    {"hidden_size", KEY_HIDDEN_SIZE},
    {"communication_size", KEY_COMMUNICATION_SIZE},
    {"initial_population", KEY_INITIAL_POPULATION},
    {"capacity_bytes", KEY_CAPACITY_BYTES},
    {"maturity_age", KEY_MATURITY_AGE},
    {"reproduction_ramp_ticks", KEY_REPRODUCTION_RAMP_TICKS},
    {"reproduction_base_probability", KEY_REPRODUCTION_BASE_PROBABILITY},
    {"reproduction_max_probability", KEY_REPRODUCTION_MAX_PROBABILITY},
    {"mutation_probability", KEY_MUTATION_PROBABILITY},
    {"mutation_magnitude", KEY_MUTATION_MAGNITUDE},
    {"plasticity_limit", KEY_PLASTICITY_LIMIT},
    {"plasticity_decay", KEY_PLASTICITY_DECAY},
    {"foolsday_sleep_death_probability",
     KEY_FOOLSDAY_SLEEP_DEATH_PROBABILITY},
    {"off_min_duration_ticks", KEY_OFF_MIN_DURATION_TICKS},
    {"off_max_duration_ticks", KEY_OFF_MAX_DURATION_TICKS},
    {"state_transition_threshold", KEY_STATE_TRANSITION_THRESHOLD},
    {"calendar_start_year", KEY_CALENDAR_START_YEAR},
    {"calendar_start_month", KEY_CALENDAR_START_MONTH},
    {"calendar_start_day", KEY_CALENDAR_START_DAY},
    {"ticks_per_day", KEY_TICKS_PER_DAY},
    {"tick_count", KEY_TICK_COUNT},
    {"summary_interval", KEY_SUMMARY_INTERVAL},
    {"checkpoint_interval", KEY_CHECKPOINT_INTERVAL},
    {"checkpoint_path", KEY_CHECKPOINT_PATH},
    {"event_log_path", KEY_EVENT_LOG_PATH},
    {"logging_level", KEY_LOGGING_LEVEL},
    {"max_abs_weight", KEY_MAX_ABS_WEIGHT},
};

static bool set_error(char *error, const size_t error_size,
                      const char *format, ...) {
    va_list arguments;

    if (error != NULL && error_size > 0U) {
        va_start(arguments, format);
        (void)vsnprintf(error, error_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static void clear_error(char *error, const size_t error_size) {
    if (error != NULL && error_size > 0U) {
        error[0] = '\0';
    }
}

static char *trim(char *text) {
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text) != 0) {
        ++text;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1]) != 0) {
        --end;
    }
    *end = '\0';
    return text;
}

static bool find_key(const char *name, ConfigKey *key) {
    size_t index;

    for (index = 0U;
         index < sizeof(KEY_DEFINITIONS) / sizeof(KEY_DEFINITIONS[0]);
         ++index) {
        if (strcmp(name, KEY_DEFINITIONS[index].name) == 0) {
            *key = KEY_DEFINITIONS[index].key;
            return true;
        }
    }
    return false;
}

static bool parse_u64(const char *text, uint64_t *value) {
    char *end;
    uintmax_t parsed;

    if (text[0] == '\0' || text[0] == '-') {
        return false;
    }

    errno = 0;
    end = NULL;
    parsed = strtoumax(text, &end, 10);
    if (errno == ERANGE || end == text || end == NULL || *end != '\0' ||
        parsed > UINT64_MAX) {
        return false;
    }

    *value = (uint64_t)parsed;
    return true;
}

static bool parse_u32(const char *text, uint32_t *value) {
    uint64_t parsed;

    if (!parse_u64(text, &parsed) || parsed > UINT32_MAX) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_double_value(const char *text, double *value) {
    char *end;
    double parsed;

    if (text[0] == '\0') {
        return false;
    }

    errno = 0;
    end = NULL;
    parsed = strtod(text, &end);
    if (errno == ERANGE || end == text || end == NULL || *end != '\0' ||
        !isfinite(parsed)) {
        return false;
    }

    *value = parsed;
    return true;
}

static bool parse_path(const char *text, char destination[ALIFE_CONFIG_PATH_MAX],
                       char *reason, const size_t reason_size) {
    const char *start = text;
    size_t length = strlen(text);

    if (length >= 1U && (text[0] == '\'' || text[0] == '"')) {
        const char quote = text[0];

        if (length < 2U || text[length - 1U] != quote) {
            return set_error(reason, reason_size,
                             "path has an unmatched quote");
        }
        start = text + 1;
        length -= 2U;
    }

    if (length >= ALIFE_CONFIG_PATH_MAX) {
        return set_error(reason, reason_size,
                         "path must contain at most %u characters",
                         (unsigned int)(ALIFE_CONFIG_PATH_MAX - 1U));
    }

    (void)memcpy(destination, start, length);
    destination[length] = '\0';
    return true;
}

static bool assign_value(AlifeConfig *config, const ConfigKey key,
                         const char *value, char *reason,
                         const size_t reason_size) {
    bool valid = false;

    switch (key) {
        case KEY_SEED:
            valid = parse_u64(value, &config->seed);
            break;
        case KEY_INPUT_SIZE:
            valid = parse_u32(value, &config->input_size);
            break;
        case KEY_HIDDEN_SIZE:
            valid = parse_u32(value, &config->hidden_size);
            break;
        case KEY_COMMUNICATION_SIZE:
            valid = parse_u32(value, &config->communication_size);
            break;
        case KEY_INITIAL_POPULATION:
            valid = parse_u32(value, &config->initial_population);
            break;
        case KEY_CAPACITY_BYTES:
            valid = parse_u64(value, &config->capacity_bytes);
            break;
        case KEY_MATURITY_AGE:
            valid = parse_u64(value, &config->maturity_age);
            break;
        case KEY_REPRODUCTION_RAMP_TICKS:
            valid = parse_u64(value, &config->reproduction_ramp_ticks);
            break;
        case KEY_REPRODUCTION_BASE_PROBABILITY:
            valid = parse_double_value(
                value, &config->reproduction_base_probability);
            break;
        case KEY_REPRODUCTION_MAX_PROBABILITY:
            valid = parse_double_value(value,
                                       &config->reproduction_max_probability);
            break;
        case KEY_MUTATION_PROBABILITY:
            valid = parse_double_value(value, &config->mutation_probability);
            break;
        case KEY_MUTATION_MAGNITUDE:
            valid = parse_double_value(value, &config->mutation_magnitude);
            break;
        case KEY_PLASTICITY_LIMIT:
            valid = parse_double_value(value, &config->plasticity_limit);
            break;
        case KEY_PLASTICITY_DECAY:
            valid = parse_double_value(value, &config->plasticity_decay);
            break;
        case KEY_FOOLSDAY_SLEEP_DEATH_PROBABILITY:
            valid = parse_double_value(
                value, &config->foolsday_sleep_death_probability);
            break;
        case KEY_OFF_MIN_DURATION_TICKS:
            valid = parse_u64(value, &config->off_min_duration_ticks);
            break;
        case KEY_OFF_MAX_DURATION_TICKS:
            valid = parse_u64(value, &config->off_max_duration_ticks);
            break;
        case KEY_STATE_TRANSITION_THRESHOLD:
            valid = parse_double_value(
                value, &config->state_transition_threshold);
            break;
        case KEY_CALENDAR_START_YEAR:
            valid = parse_u32(value, &config->calendar_start_year);
            break;
        case KEY_CALENDAR_START_MONTH:
            valid = parse_u32(value, &config->calendar_start_month);
            break;
        case KEY_CALENDAR_START_DAY:
            valid = parse_u32(value, &config->calendar_start_day);
            break;
        case KEY_TICKS_PER_DAY:
            valid = parse_u64(value, &config->ticks_per_day);
            break;
        case KEY_TICK_COUNT:
            valid = parse_u64(value, &config->tick_count);
            break;
        case KEY_SUMMARY_INTERVAL:
            valid = parse_u64(value, &config->summary_interval);
            break;
        case KEY_CHECKPOINT_INTERVAL:
            valid = parse_u64(value, &config->checkpoint_interval);
            break;
        case KEY_CHECKPOINT_PATH:
            return parse_path(value, config->checkpoint_path, reason,
                              reason_size);
        case KEY_EVENT_LOG_PATH:
            return parse_path(value, config->event_log_path, reason,
                              reason_size);
        case KEY_LOGGING_LEVEL:
            if (strcmp(value, "error") == 0 || strcmp(value, "0") == 0) {
                config->logging_level = ALIFE_LOG_ERROR;
                valid = true;
            } else if (strcmp(value, "summary") == 0 ||
                       strcmp(value, "1") == 0) {
                config->logging_level = ALIFE_LOG_SUMMARY;
                valid = true;
            } else if (strcmp(value, "events") == 0 ||
                       strcmp(value, "2") == 0) {
                config->logging_level = ALIFE_LOG_EVENTS;
                valid = true;
            }
            break;
        case KEY_MAX_ABS_WEIGHT:
            valid = parse_double_value(value, &config->max_abs_weight);
            break;
        case KEY_COUNT:
            break;
    }

    if (!valid) {
        return set_error(reason, reason_size, "value '%s' is invalid", value);
    }
    return true;
}

static bool is_leap_year(const uint32_t year) {
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static uint32_t days_in_month(const uint32_t year, const uint32_t month) {
    static const uint8_t DAYS[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                   31U, 31U, 30U, 31U, 30U, 31U};

    if (month < 1U || month > 12U) {
        return 0U;
    }
    if (month == 2U && is_leap_year(year)) {
        return 29U;
    }
    return DAYS[month - 1U];
}

static uint64_t fingerprint_bytes(uint64_t hash, const void *data,
                                  const size_t size) {
    const unsigned char *bytes = data;
    size_t index;

    for (index = 0U; index < size; ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t fingerprint_u64(uint64_t hash, const uint64_t value) {
    unsigned int shift;

    for (shift = 0U; shift < 64U; shift += 8U) {
        const unsigned char byte = (unsigned char)(value >> shift);

        hash = fingerprint_bytes(hash, &byte, sizeof(byte));
    }
    return hash;
}

static uint64_t fingerprint_double(uint64_t hash, double value) {
    unsigned char representation[sizeof(value)];

    /* Give positive and negative zero the same semantic fingerprint. */
    if (value == 0.0) {
        value = 0.0;
    }
    (void)memcpy(representation, &value, sizeof(representation));
    return fingerprint_bytes(hash, representation, sizeof(representation));
}

static size_t bounded_path_length(
    const char path[ALIFE_CONFIG_PATH_MAX]) {
    size_t length = 0U;

    while (length < ALIFE_CONFIG_PATH_MAX && path[length] != '\0') {
        ++length;
    }
    return length;
}

static uint64_t fingerprint_path(uint64_t hash,
                                 const char path[ALIFE_CONFIG_PATH_MAX]) {
    const size_t length = bounded_path_length(path);

    hash = fingerprint_u64(hash, (uint64_t)length);
    return fingerprint_bytes(hash, path, length);
}

void alife_config_defaults(AlifeConfig *config) {
    if (config == NULL) {
        return;
    }

    (void)memset(config, 0, sizeof(*config));
    config->seed = UINT64_C(1);
    config->input_size = 8U;
    config->hidden_size = 24U;
    config->communication_size = 2U;
    config->initial_population = 2U;
    config->capacity_bytes = UINT64_C(67108864);
    config->maturity_age = UINT64_C(1000);
    config->reproduction_ramp_ticks = UINT64_C(9000);
    config->reproduction_base_probability = 0.01;
    config->reproduction_max_probability = 0.50;
    config->mutation_probability = 0.01;
    config->mutation_magnitude = 0.10;
    config->plasticity_limit = 0.05;
    config->plasticity_decay = 0.999;
    config->foolsday_sleep_death_probability = 0.0;
    config->off_min_duration_ticks = UINT64_C(10);
    config->off_max_duration_ticks = UINT64_C(100000);
    config->state_transition_threshold = 0.5;
    config->calendar_start_year = 2026U;
    config->calendar_start_month = 1U;
    config->calendar_start_day = 1U;
    config->ticks_per_day = UINT64_C(1000);
    config->tick_count = UINT64_C(100000);
    config->summary_interval = UINT64_C(1000);
    config->checkpoint_interval = UINT64_C(10000);
    (void)memcpy(config->checkpoint_path, "alife.chk",
                 sizeof("alife.chk"));
    (void)memcpy(config->event_log_path, "events.jsonl",
                 sizeof("events.jsonl"));
    config->logging_level = ALIFE_LOG_SUMMARY;
    config->max_abs_weight = 4.0;
}

bool alife_config_validate(const AlifeConfig *config, char *error,
                           const size_t error_size) {
    uint32_t month_days;
    uint64_t parameter_count;

    clear_error(error, error_size);
    if (config == NULL) {
        return set_error(error, error_size, "configuration is null");
    }
    if (config->input_size < 6U || config->input_size > 32U) {
        return set_error(error, error_size,
                         "input_size must be between 6 and 32");
    }
    if (config->hidden_size < 4U || config->hidden_size > 64U) {
        return set_error(error, error_size,
                         "hidden_size must be between 4 and 64");
    }
    if (config->communication_size < 1U ||
        config->communication_size > 8U) {
        return set_error(error, error_size,
                         "communication_size must be between 1 and 8");
    }
    parameter_count =
        (uint64_t)config->hidden_size *
            ((uint64_t)config->input_size +
             (uint64_t)config->communication_size) +
        (uint64_t)config->hidden_size * (uint64_t)config->hidden_size +
        (uint64_t)config->hidden_size +
        ((uint64_t)config->communication_size + UINT64_C(6)) *
            (uint64_t)config->hidden_size +
        ((uint64_t)config->communication_size + UINT64_C(6)) +
        UINT64_C(2) * (uint64_t)config->hidden_size;
    if (parameter_count < UINT64_C(500) ||
        parameter_count > UINT64_C(2000)) {
        return set_error(error, error_size,
                         "neural architecture must contain between 500 and "
                         "2000 heritable parameters (configured: %" PRIu64 ")",
                         parameter_count);
    }
    if (config->initial_population != 2U) {
        return set_error(error, error_size,
                         "initial_population must be exactly 2");
    }
    if (config->capacity_bytes == UINT64_C(0)) {
        return set_error(error, error_size,
                         "capacity_bytes must be greater than zero");
    }
    if (config->maturity_age == UINT64_C(0)) {
        return set_error(error, error_size,
                         "maturity_age must be greater than zero");
    }
    if (config->reproduction_ramp_ticks == UINT64_C(0)) {
        return set_error(
            error, error_size,
            "reproduction_ramp_ticks must be greater than zero");
    }
    if (config->reproduction_ramp_ticks == UINT64_MAX ||
        config->maturity_age >
            UINT64_MAX - config->reproduction_ramp_ticks - UINT64_C(1)) {
        return set_error(error, error_size,
                         "maturity_age and reproduction_ramp_ticks are too large");
    }
    if (!isfinite(config->reproduction_base_probability) ||
        config->reproduction_base_probability < 0.0 ||
        config->reproduction_base_probability > 1.0) {
        return set_error(
            error, error_size,
            "reproduction_base_probability must be between 0 and 1");
    }
    if (!isfinite(config->reproduction_max_probability) ||
        config->reproduction_max_probability < 0.0 ||
        config->reproduction_max_probability > 1.0) {
        return set_error(
            error, error_size,
            "reproduction_max_probability must be between 0 and 1");
    }
    if (config->reproduction_base_probability >
        config->reproduction_max_probability) {
        return set_error(
            error, error_size,
            "reproduction_base_probability must not exceed "
            "reproduction_max_probability");
    }
    if (!isfinite(config->mutation_probability) ||
        config->mutation_probability < 0.0 ||
        config->mutation_probability > 1.0) {
        return set_error(error, error_size,
                         "mutation_probability must be between 0 and 1");
    }
    if (!isfinite(config->max_abs_weight) ||
        config->max_abs_weight < 1.0e-6 ||
        config->max_abs_weight > 1000.0 ||
        config->max_abs_weight > (double)FLT_MAX) {
        return set_error(error, error_size,
                         "max_abs_weight must be between 0.000001 and 1000");
    }
    if (!isfinite(config->mutation_magnitude) ||
        config->mutation_magnitude < 0.0 ||
        config->mutation_magnitude > config->max_abs_weight) {
        return set_error(
            error, error_size,
            "mutation_magnitude must be between 0 and max_abs_weight");
    }
    if (!isfinite(config->plasticity_limit) ||
        config->plasticity_limit < 0.0 ||
        config->plasticity_limit > config->max_abs_weight) {
        return set_error(
            error, error_size,
            "plasticity_limit must be between 0 and max_abs_weight");
    }
    if (!isfinite(config->plasticity_decay) ||
        config->plasticity_decay < 0.0 || config->plasticity_decay > 1.0) {
        return set_error(error, error_size,
                         "plasticity_decay must be between 0 and 1");
    }
    if (!isfinite(config->foolsday_sleep_death_probability) ||
        config->foolsday_sleep_death_probability < 0.0 ||
        config->foolsday_sleep_death_probability > 1.0) {
        return set_error(
            error, error_size,
            "foolsday_sleep_death_probability must be between 0 and 1");
    }
    if (config->off_min_duration_ticks == 0U) {
        return set_error(error, error_size,
                         "off_min_duration_ticks must be greater than zero");
    }
    if (config->off_max_duration_ticks < config->off_min_duration_ticks) {
        return set_error(error, error_size,
                         "off_max_duration_ticks must not be less than "
                         "off_min_duration_ticks");
    }
    if (!isfinite(config->state_transition_threshold) ||
        config->state_transition_threshold < 0.0 ||
        config->state_transition_threshold >= 1.0) {
        return set_error(error, error_size,
                         "state_transition_threshold must be at least 0 and "
                         "less than 1");
    }
    if (config->calendar_start_year < 1U ||
        config->calendar_start_year > 9999U) {
        return set_error(error, error_size,
                         "calendar_start_year must be between 1 and 9999");
    }
    month_days = days_in_month(config->calendar_start_year,
                               config->calendar_start_month);
    if (month_days == 0U) {
        return set_error(error, error_size,
                         "calendar_start_month must be between 1 and 12");
    }
    if (config->calendar_start_day < 1U ||
        config->calendar_start_day > month_days) {
        return set_error(error, error_size,
                         "calendar_start_day is invalid for the configured "
                         "year and month");
    }
    if (config->ticks_per_day == UINT64_C(0)) {
        return set_error(error, error_size,
                         "ticks_per_day must be greater than zero");
    }
    if (config->tick_count == UINT64_C(0)) {
        return set_error(error, error_size,
                         "tick_count must be greater than zero");
    }
    if (config->tick_count > UINT64_MAX - config->off_max_duration_ticks) {
        return set_error(error, error_size,
                         "tick_count and off_max_duration_ticks are too large");
    }
    if (config->summary_interval == UINT64_C(0)) {
        return set_error(error, error_size,
                         "summary_interval must be greater than zero");
    }
    if (config->checkpoint_interval == UINT64_C(0)) {
        return set_error(error, error_size,
                         "checkpoint_interval must be greater than zero");
    }
    if (memchr(config->checkpoint_path, '\0', ALIFE_CONFIG_PATH_MAX) == NULL ||
        config->checkpoint_path[0] == '\0') {
        return set_error(error, error_size,
                         "checkpoint_path must be a nonempty path shorter than "
                         "256 bytes");
    }
    if (memchr(config->event_log_path, '\0', ALIFE_CONFIG_PATH_MAX) == NULL ||
        config->event_log_path[0] == '\0') {
        return set_error(error, error_size,
                         "event_log_path must be a nonempty path shorter than "
                         "256 bytes");
    }
    if (strcmp(config->checkpoint_path, config->event_log_path) == 0) {
        return set_error(error, error_size,
                         "checkpoint_path and event_log_path must differ");
    }
    if (config->logging_level != ALIFE_LOG_ERROR &&
        config->logging_level != ALIFE_LOG_SUMMARY &&
        config->logging_level != ALIFE_LOG_EVENTS) {
        return set_error(error, error_size,
                         "logging_level must be error, summary, or events");
    }
    return true;
}

bool alife_config_load(AlifeConfig *config, const char *path, char *error,
                       const size_t error_size) {
    AlifeConfig loaded;
    FILE *file;
    char line[1024];
    uint64_t seen = UINT64_C(0);
    size_t line_number = 0U;

    clear_error(error, error_size);
    if (config == NULL) {
        return set_error(error, error_size,
                         "cannot load into a null configuration");
    }
    if (path == NULL || path[0] == '\0') {
        return set_error(error, error_size,
                         "configuration path must not be empty");
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return set_error(error, error_size, "cannot open '%s': %s", path,
                         strerror(errno));
    }

    alife_config_defaults(&loaded);
    while (fgets(line, (int)sizeof(line), file) != NULL) {
        char *assignment;
        char *comment;
        char *equals;
        char *key_text;
        char *value_text;
        char reason[256];
        ConfigKey key;
        uint64_t key_bit;

        ++line_number;
        if (strchr(line, '\n') == NULL && feof(file) == 0) {
            int character;

            do {
                character = fgetc(file);
            } while (character != '\n' && character != EOF);
            (void)fclose(file);
            return set_error(error, error_size,
                             "%s:%zu: line exceeds 1023 characters", path,
                             line_number);
        }

        comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        assignment = trim(line);
        if (assignment[0] == '\0') {
            continue;
        }

        equals = strchr(assignment, '=');
        if (equals == NULL) {
            (void)fclose(file);
            return set_error(error, error_size,
                             "%s:%zu: expected a key = value assignment", path,
                             line_number);
        }
        *equals = '\0';
        key_text = trim(assignment);
        value_text = trim(equals + 1);
        if (key_text[0] == '\0' || value_text[0] == '\0') {
            (void)fclose(file);
            return set_error(error, error_size,
                             "%s:%zu: key and value must not be empty", path,
                             line_number);
        }
        if (!find_key(key_text, &key)) {
            (void)fclose(file);
            return set_error(error, error_size, "%s:%zu: unknown key '%s'",
                             path, line_number, key_text);
        }

        key_bit = UINT64_C(1) << (unsigned int)key;
        if ((seen & key_bit) != UINT64_C(0)) {
            (void)fclose(file);
            return set_error(error, error_size,
                             "%s:%zu: duplicate key '%s'", path, line_number,
                             key_text);
        }
        reason[0] = '\0';
        if (!assign_value(&loaded, key, value_text, reason, sizeof(reason))) {
            (void)fclose(file);
            return set_error(error, error_size, "%s:%zu: %s: %s", path,
                             line_number, key_text, reason);
        }
        seen |= key_bit;
    }

    if (ferror(file) != 0) {
        const int saved_errno = errno;

        (void)fclose(file);
        return set_error(error, error_size, "cannot read '%s': %s", path,
                         strerror(saved_errno));
    }
    if (fclose(file) != 0) {
        return set_error(error, error_size, "cannot close '%s': %s", path,
                         strerror(errno));
    }

    {
        char validation_error[256];

        if (!alife_config_validate(&loaded, validation_error,
                                   sizeof(validation_error))) {
            return set_error(error, error_size, "%s: %s", path,
                             validation_error);
        }
    }

    *config = loaded;
    return true;
}

bool alife_config_equal(const AlifeConfig *left, const AlifeConfig *right) {
    if (left == right) {
        return true;
    }
    if (left == NULL || right == NULL) {
        return false;
    }

    return left->seed == right->seed &&
           left->input_size == right->input_size &&
           left->hidden_size == right->hidden_size &&
           left->communication_size == right->communication_size &&
           left->initial_population == right->initial_population &&
           left->capacity_bytes == right->capacity_bytes &&
           left->maturity_age == right->maturity_age &&
           left->reproduction_ramp_ticks == right->reproduction_ramp_ticks &&
           left->reproduction_base_probability ==
               right->reproduction_base_probability &&
           left->reproduction_max_probability ==
               right->reproduction_max_probability &&
           left->mutation_probability == right->mutation_probability &&
           left->mutation_magnitude == right->mutation_magnitude &&
           left->plasticity_limit == right->plasticity_limit &&
           left->plasticity_decay == right->plasticity_decay &&
           left->foolsday_sleep_death_probability ==
               right->foolsday_sleep_death_probability &&
           left->off_min_duration_ticks == right->off_min_duration_ticks &&
           left->off_max_duration_ticks == right->off_max_duration_ticks &&
           left->state_transition_threshold ==
               right->state_transition_threshold &&
           left->calendar_start_year == right->calendar_start_year &&
           left->calendar_start_month == right->calendar_start_month &&
           left->calendar_start_day == right->calendar_start_day &&
           left->ticks_per_day == right->ticks_per_day &&
           left->tick_count == right->tick_count &&
           left->summary_interval == right->summary_interval &&
           left->checkpoint_interval == right->checkpoint_interval &&
           strncmp(left->checkpoint_path, right->checkpoint_path,
                   ALIFE_CONFIG_PATH_MAX) == 0 &&
           strncmp(left->event_log_path, right->event_log_path,
                   ALIFE_CONFIG_PATH_MAX) == 0 &&
           left->logging_level == right->logging_level &&
           left->max_abs_weight == right->max_abs_weight;
}

uint64_t alife_config_fingerprint(const AlifeConfig *config) {
    uint64_t hash = UINT64_C(14695981039346656037);

    if (config == NULL) {
        return UINT64_C(0);
    }

    hash = fingerprint_u64(hash, config->seed);
    hash = fingerprint_u64(hash, (uint64_t)config->input_size);
    hash = fingerprint_u64(hash, (uint64_t)config->hidden_size);
    hash = fingerprint_u64(hash, (uint64_t)config->communication_size);
    hash = fingerprint_u64(hash, (uint64_t)config->initial_population);
    hash = fingerprint_u64(hash, config->capacity_bytes);
    hash = fingerprint_u64(hash, config->maturity_age);
    hash = fingerprint_u64(hash, config->reproduction_ramp_ticks);
    hash = fingerprint_double(hash, config->reproduction_base_probability);
    hash = fingerprint_double(hash, config->reproduction_max_probability);
    hash = fingerprint_double(hash, config->mutation_probability);
    hash = fingerprint_double(hash, config->mutation_magnitude);
    hash = fingerprint_double(hash, config->plasticity_limit);
    hash = fingerprint_double(hash, config->plasticity_decay);
    hash = fingerprint_double(hash,
                              config->foolsday_sleep_death_probability);
    hash = fingerprint_u64(hash, config->off_min_duration_ticks);
    hash = fingerprint_u64(hash, config->off_max_duration_ticks);
    hash = fingerprint_double(hash, config->state_transition_threshold);
    hash = fingerprint_u64(hash, (uint64_t)config->calendar_start_year);
    hash = fingerprint_u64(hash, (uint64_t)config->calendar_start_month);
    hash = fingerprint_u64(hash, (uint64_t)config->calendar_start_day);
    hash = fingerprint_u64(hash, config->ticks_per_day);
    hash = fingerprint_u64(hash, config->tick_count);
    hash = fingerprint_u64(hash, config->summary_interval);
    hash = fingerprint_u64(hash, config->checkpoint_interval);
    hash = fingerprint_path(hash, config->checkpoint_path);
    hash = fingerprint_path(hash, config->event_log_path);
    hash = fingerprint_u64(hash, (uint64_t)config->logging_level);
    return fingerprint_double(hash, config->max_abs_weight);
}

void config_defaults(AlifeConfig *config) {
    alife_config_defaults(config);
}

bool config_load(AlifeConfig *config, const char *path, char *error,
                 const size_t error_size) {
    return alife_config_load(config, path, error, error_size);
}

bool config_validate(const AlifeConfig *config, char *error,
                     const size_t error_size) {
    return alife_config_validate(config, error, error_size);
}

bool config_equal(const AlifeConfig *left, const AlifeConfig *right) {
    return alife_config_equal(left, right);
}
