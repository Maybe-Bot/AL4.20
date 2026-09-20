#include "internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static bool events_enabled(const AlifeWorld *world) {
    return world->event_log != NULL && world->config.logging_level >= ALIFE_LOG_EVENTS;
}

static bool summaries_enabled(const AlifeWorld *world) {
    return world->event_log != NULL && world->config.logging_level >= ALIFE_LOG_SUMMARY;
}

static void event_prefix(AlifeWorld *world, const char *event) {
    (void)fprintf(world->event_log,
                  "{\"event\":\"%s\",\"tick\":%" PRIu64
                  ",\"year\":%" PRId32 ",\"month\":%" PRId32
                  ",\"day\":%" PRId32,
                  event, world->tick, world->year, world->month, world->day);
}

static void write_json_string(FILE *file, const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;

    (void)fputc('"', file);
    while (*cursor != 0U) {
        switch (*cursor) {
            case '"':
                (void)fputs("\\\"", file);
                break;
            case '\\':
                (void)fputs("\\\\", file);
                break;
            case '\n':
                (void)fputs("\\n", file);
                break;
            case '\r':
                (void)fputs("\\r", file);
                break;
            case '\t':
                (void)fputs("\\t", file);
                break;
            default:
                if (*cursor < 0x20U) {
                    (void)fprintf(file, "\\u%04x", (unsigned int)*cursor);
                } else {
                    (void)fputc((int)*cursor, file);
                }
                break;
        }
        ++cursor;
    }
    (void)fputc('"', file);
}

const char *alife_death_cause_name(AlifeDeathCause cause) {
    switch (cause) {
        case ALIFE_DEATH_CAPACITY:
            return "capacity";
        case ALIFE_DEATH_APRIL_1:
            return "april_1";
        case ALIFE_DEATH_INVALID_STATE:
            return "invalid_state";
        case ALIFE_DEATH_SHUTDOWN:
            return "shutdown";
    }
    return "unknown";
}

void alife_log_run_start(AlifeWorld *world) {
    if (!summaries_enabled(world)) {
        return;
    }
    event_prefix(world, "run_start");
    (void)fprintf(world->event_log,
                  ",\"log_version\":1,\"seed\":%" PRIu64
                  ",\"config_fingerprint\":%" PRIu64 ",\"resumed\":%s"
                  "}\n",
                  world->config.seed,
                  alife_config_fingerprint(&world->config),
                  world->tick == 0U ? "false" : "true");
}

void alife_log_run_end(AlifeWorld *world) {
    if (!summaries_enabled(world)) {
        return;
    }
    event_prefix(world, "run_end");
    (void)fprintf(world->event_log,
                  ",\"population\":%zu,\"births\":%" PRIu64
                  ",\"deaths\":%" PRIu64 "}\n",
                  world->count, world->total_births, world->total_deaths);
    (void)fflush(world->event_log);
}

void alife_log_birth(AlifeWorld *world, const AlifeOrganism *organism) {
    if (!summaries_enabled(world)) {
        return;
    }
    event_prefix(world, "birth");
    (void)fprintf(world->event_log,
                  ",\"organism_id\":%" PRIu64 ",\"parent_a\":%" PRIu64
                  ",\"parent_b\":%" PRIu64 ",\"generation\":%" PRIu64
                  ",\"organism_bytes\":%zu,\"genome_parameters\":%zu"
                  ",\"mutations\":%" PRIu64 "}\n",
                  organism->id, organism->parent_a, organism->parent_b,
                  organism->generation, alife_organism_size(world),
                  world->layout.genome_count, organism->mutations);
}

void alife_log_reproduction(AlifeWorld *world, uint64_t parent_a,
                            uint64_t parent_b, bool approved,
                            const char *reason) {
    if (!events_enabled(world)) {
        return;
    }
    event_prefix(world, "reproduction_attempt");
    (void)fprintf(world->event_log,
                  ",\"parent_a\":%" PRIu64 ",\"parent_b\":%" PRIu64
                  ",\"approved\":%s,\"reason\":\"%s\"}\n",
                  parent_a, parent_b, approved ? "true" : "false", reason);
}

void alife_log_death(AlifeWorld *world, const AlifeOrganism *organism,
                     AlifeDeathCause cause) {
    if (!summaries_enabled(world)) {
        return;
    }
    event_prefix(world, "death");
    (void)fprintf(world->event_log,
                  ",\"organism_id\":%" PRIu64 ",\"cause\":\"%s\""
                  ",\"birth_tick\":%" PRIu64 ",\"age\":%" PRIu64
                  ",\"reward\":%" PRIu64 ",\"generation\":%" PRIu64
                  ",\"reproduction_attempts\":%" PRIu64
                  ",\"successful_reproduction\":%" PRIu64
                  ",\"mutations\":%" PRIu64
                  ",\"significant_weight_changes\":%" PRIu64 "}\n",
                  organism->id, alife_death_cause_name(cause),
                  organism->birth_tick, organism->age, organism->reward,
                  organism->generation, organism->reproduction_attempts,
                  organism->successful_reproductions, organism->mutations,
                  organism->significant_weight_changes);
}

void alife_log_plasticity(AlifeWorld *world, const AlifeOrganism *organism,
                          double magnitude) {
    if (!events_enabled(world)) {
        return;
    }
    event_prefix(world, "plasticity");
    (void)fprintf(world->event_log,
                  ",\"organism_id\":%" PRIu64
                  ",\"absolute_change\":%.9g}\n",
                  organism->id, magnitude);
}

void alife_log_summary(AlifeWorld *world) {
    if (world->config.logging_level >= ALIFE_LOG_SUMMARY) {
        (void)fprintf(stderr,
                      "tick=%" PRIu64 " date=%04" PRId32 "-%02" PRId32
                      "-%02" PRId32 " population=%zu mass=%" PRIu64
                      " parameters=%zu"
                      " births=%" PRIu64 " deaths=%" PRIu64 "\n",
                      world->tick, world->year, world->month, world->day,
                      world->count, world->population_bytes,
                      world->count * world->layout.genome_count,
                      world->total_births, world->total_deaths);
    }
    if (!summaries_enabled(world)) {
        return;
    }
    event_prefix(world, "summary");
    (void)fprintf(world->event_log,
                  ",\"population\":%zu,\"population_bytes\":%" PRIu64
                  ",\"population_genome_parameters\":%zu"
                  ",\"births\":%" PRIu64 ",\"deaths\":%" PRIu64
                  ",\"reproduction_attempts\":%" PRIu64
                  ",\"mutations\":%" PRIu64 "}\n",
                  world->count, world->population_bytes,
                  world->count * world->layout.genome_count,
                  world->total_births,
                  world->total_deaths, world->total_reproduction_attempts,
                  world->total_mutations);
    (void)fflush(world->event_log);
}

void alife_log_checkpoint(AlifeWorld *world, const char *path) {
    struct stat information;
    uintmax_t bytes = 0U;

    if (!summaries_enabled(world)) {
        return;
    }
    if (stat(path, &information) == 0 && information.st_size > 0) {
        bytes = (uintmax_t)information.st_size;
    }
    event_prefix(world, "checkpoint");
    (void)fprintf(world->event_log,
                  ",\"checkpoint_version\":%u,\"config_fingerprint\":%" PRIu64
                  ",\"checkpoint_tick\":%" PRIu64
                  ",\"checkpoint_bytes\":%" PRIuMAX
                  ",\"population_bytes\":%" PRIu64 ",\"path\":",
                  ALIFE_CHECKPOINT_VERSION,
                  alife_config_fingerprint(&world->config), world->tick, bytes,
                  world->population_bytes);
    write_json_string(world->event_log, path);
    (void)fprintf(world->event_log, ",\"population\":%zu}\n", world->count);
    (void)fflush(world->event_log);
}

void alife_log_reseed(AlifeWorld *world) {
    if (!summaries_enabled(world)) {
        return;
    }
    event_prefix(world, "reseed");
    (void)fprintf(world->event_log, ",\"population\":%zu}\n", world->count);
}
