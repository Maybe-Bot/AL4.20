#include "internal.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char checkpoint_magic[8] = {
    'A', 'L', '4', '2', '0', 'C', 'P', '\0'
};

static bool checkpoint_date_valid(int32_t year, int32_t month, int32_t day);

static bool transition_counts_valid(uint64_t total, uint64_t awake_to_asleep,
                                    uint64_t asleep_to_awake,
                                    uint64_t asleep_to_off) {
    return awake_to_asleep <= total &&
           asleep_to_awake <= total - awake_to_asleep &&
           asleep_to_off <= total - awake_to_asleep - asleep_to_awake;
}

static bool write_bytes(FILE *file, const void *value, size_t size) {
    return fwrite(value, 1U, size, file) == size;
}

static bool read_bytes(FILE *file, void *value, size_t size) {
    return fread(value, 1U, size, file) == size;
}

#define DEFINE_IO(type, suffix)                                                \
    static bool write_##suffix(FILE *file, type value) {                       \
        return write_bytes(file, &value, sizeof(value));                       \
    }                                                                           \
    static bool read_##suffix(FILE *file, type *value) {                        \
        return read_bytes(file, value, sizeof(*value));                        \
    }

DEFINE_IO(uint8_t, u8)
DEFINE_IO(uint32_t, u32)
DEFINE_IO(int32_t, i32)
DEFINE_IO(uint64_t, u64)
DEFINE_IO(float, f32)

static bool write_header(FILE *file, const AlifeWorld *world) {
    uint32_t endian = UINT32_C(0x01020304);
    size_t i;

    if (!write_bytes(file, checkpoint_magic, sizeof(checkpoint_magic)) ||
        !write_u32(file, ALIFE_CHECKPOINT_VERSION) ||
        !write_u32(file, endian) ||
        !write_u32(file, (uint32_t)sizeof(float)) ||
        !write_u64(file, alife_config_fingerprint(&world->config)) ||
        !write_u64(file, (uint64_t)world->layout.genome_count) ||
        !write_u64(file, (uint64_t)world->layout.slot_floats) ||
        !write_u64(file, (uint64_t)world->count) ||
        !write_u64(file, world->tick) ||
        !write_u64(file, world->next_id) ||
        !write_u64(file, world->total_births) ||
        !write_u64(file, world->total_deaths) ||
        !write_u64(file, world->total_reproduction_attempts) ||
        !write_u64(file, world->total_mutations) ||
        !write_u64(file, world->total_executions) ||
        !write_u64(file, world->total_state_transitions) ||
        !write_u64(file, world->awake_to_asleep_transitions) ||
        !write_u64(file, world->asleep_to_awake_transitions) ||
        !write_u64(file, world->asleep_to_off_transitions) ||
        !write_i32(file, world->year) ||
        !write_i32(file, world->month) ||
        !write_i32(file, world->day) ||
        !write_u8(file, world->stopped ? 1U : 0U)) {
        return false;
    }
    for (i = 0U; i < 4U; ++i) {
        if (!write_u64(file, world->rng.state[i])) {
            return false;
        }
    }
    return true;
}

static bool write_organism(FILE *file, const AlifeOrganism *organism) {
    return write_u64(file, organism->id) &&
           write_u64(file, organism->parent_a) &&
           write_u64(file, organism->parent_b) &&
           write_u64(file, organism->birth_tick) &&
           write_u64(file, organism->age) &&
           write_u64(file, organism->reward) &&
           write_u64(file, organism->generation) &&
           write_u64(file, organism->reproduction_attempts) &&
           write_u64(file, organism->successful_reproductions) &&
           write_u64(file, organism->mutations) &&
           write_u64(file, organism->significant_weight_changes) &&
           write_u64(file, organism->executions) &&
           write_i32(file, organism->birth_year) &&
           write_i32(file, organism->birth_month) &&
           write_i32(file, organism->birth_day) &&
           write_u32(file, (uint32_t)organism->state) &&
           write_u64(file, organism->back_on_tick) &&
           write_i32(file, organism->last_foolsday_roll_year) &&
           write_f32(file, organism->reproduction_output) &&
           write_f32(file, organism->acceptance_output) &&
           write_f32(file, organism->sleep_output) &&
           write_f32(file, organism->wake_output) &&
           write_f32(file, organism->off_output) &&
           write_f32(file, organism->off_duration_output);
}

bool alife_world_save(AlifeWorld *world, const char *path,
                      char *error, size_t error_size) {
    char temporary[512];
    FILE *file;
    size_t i;
    bool okay = true;
    int length;

    if (world == NULL || !world->initialized || path == NULL || path[0] == '\0') {
        alife_set_error(error, error_size, "A checkpoint path and initialized world are required.");
        return false;
    }
    if (!checkpoint_date_valid(world->year, world->month, world->day) ||
        world->total_deaths > world->total_births ||
        (uint64_t)world->count !=
            world->total_births - world->total_deaths ||
        world->next_id == 0U || world->next_id != world->total_births + 1U ||
        world->population_bytes !=
            (uint64_t)world->count * (uint64_t)alife_organism_size(world) ||
        !transition_counts_valid(world->total_state_transitions,
                                 world->awake_to_asleep_transitions,
                                 world->asleep_to_awake_transitions,
                                 world->asleep_to_off_transitions) ||
        (world->rng.state[0] | world->rng.state[1] |
         world->rng.state[2] | world->rng.state[3]) == 0U) {
        alife_set_error(error, error_size,
                        "The world contains invalid substrate state.");
        return false;
    }
    for (i = 0U; i < world->count; ++i) {
        if (!alife_organism_state_valid(world, i)) {
            alife_set_error(error, error_size,
                            "The world contains invalid organism state.");
            return false;
        }
    }
    length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        alife_set_error(error, error_size, "The checkpoint path is too long.");
        return false;
    }
    file = fopen(temporary, "wb");
    if (file == NULL) {
        alife_set_error(error, error_size, "Could not open checkpoint '%s': %s.",
                        temporary, strerror(errno));
        return false;
    }
    okay = write_header(file, world);
    for (i = 0U; okay && i < world->count; ++i) {
        okay = write_organism(file, &world->organisms[i]);
        if (okay) {
            okay = write_bytes(file, alife_slot_const(world, i),
                               world->layout.slot_floats * sizeof(float));
        }
    }
    if (fflush(file) != 0 || ferror(file) != 0) {
        okay = false;
    }
    if (fclose(file) != 0) {
        okay = false;
    }
    if (!okay) {
        (void)remove(temporary);
        alife_set_error(error, error_size, "Could not write checkpoint '%s'.", path);
        return false;
    }
    if (rename(temporary, path) != 0) {
        (void)remove(temporary);
        alife_set_error(error, error_size, "Could not replace checkpoint '%s': %s.",
                        path, strerror(errno));
        return false;
    }
    alife_log_checkpoint(world, path);
    return true;
}

typedef struct {
    uint32_t version;
    uint64_t fingerprint;
    uint64_t genome_count;
    uint64_t slot_floats;
    uint64_t count;
    uint64_t tick;
    uint64_t next_id;
    uint64_t births;
    uint64_t deaths;
    uint64_t attempts;
    uint64_t mutations;
    uint64_t executions;
    uint64_t state_transitions;
    uint64_t awake_to_asleep;
    uint64_t asleep_to_awake;
    uint64_t asleep_to_off;
    int32_t year;
    int32_t month;
    int32_t day;
    uint8_t stopped;
    uint64_t rng[4];
} CheckpointHeader;

static bool read_header(FILE *file, CheckpointHeader *header,
                        char *error, size_t error_size) {
    unsigned char magic[8];
    uint32_t endian;
    uint32_t float_size;
    size_t i;

    memset(header, 0, sizeof(*header));
    if (!read_bytes(file, magic, sizeof(magic)) ||
        memcmp(magic, checkpoint_magic, sizeof(magic)) != 0) {
        alife_set_error(error, error_size, "The file is not an AL4.20 checkpoint.");
        return false;
    }
    if (!read_u32(file, &header->version) ||
        !read_u32(file, &endian) || !read_u32(file, &float_size) ||
        !read_u64(file, &header->fingerprint) ||
        !read_u64(file, &header->genome_count) ||
        !read_u64(file, &header->slot_floats) ||
        !read_u64(file, &header->count) ||
        !read_u64(file, &header->tick) ||
        !read_u64(file, &header->next_id) ||
        !read_u64(file, &header->births) ||
        !read_u64(file, &header->deaths) ||
        !read_u64(file, &header->attempts) ||
        !read_u64(file, &header->mutations) ||
        !read_u64(file, &header->executions) ||
        !read_u64(file, &header->state_transitions) ||
        !read_u64(file, &header->awake_to_asleep) ||
        !read_u64(file, &header->asleep_to_awake) ||
        !read_u64(file, &header->asleep_to_off) ||
        !read_i32(file, &header->year) ||
        !read_i32(file, &header->month) ||
        !read_i32(file, &header->day) ||
        !read_u8(file, &header->stopped)) {
        alife_set_error(error, error_size, "The checkpoint header is truncated.");
        return false;
    }
    for (i = 0U; i < 4U; ++i) {
        if (!read_u64(file, &header->rng[i])) {
            alife_set_error(error, error_size, "The checkpoint RNG state is truncated.");
            return false;
        }
    }
    if (header->version != ALIFE_CHECKPOINT_VERSION) {
        alife_set_error(error, error_size,
                        "Checkpoint version %" PRIu32 " is not supported.",
                        header->version);
        return false;
    }
    if (endian != UINT32_C(0x01020304) || float_size != sizeof(float)) {
        alife_set_error(error, error_size,
                        "The checkpoint uses an incompatible numeric representation.");
        return false;
    }
    return true;
}

static bool read_organism(FILE *file, AlifeOrganism *organism) {
    uint32_t state;
    bool okay;

    memset(organism, 0, sizeof(*organism));
    okay = read_u64(file, &organism->id) &&
           read_u64(file, &organism->parent_a) &&
           read_u64(file, &organism->parent_b) &&
           read_u64(file, &organism->birth_tick) &&
           read_u64(file, &organism->age) &&
           read_u64(file, &organism->reward) &&
           read_u64(file, &organism->generation) &&
           read_u64(file, &organism->reproduction_attempts) &&
           read_u64(file, &organism->successful_reproductions) &&
           read_u64(file, &organism->mutations) &&
           read_u64(file, &organism->significant_weight_changes) &&
           read_u64(file, &organism->executions) &&
           read_i32(file, &organism->birth_year) &&
           read_i32(file, &organism->birth_month) &&
           read_i32(file, &organism->birth_day) &&
           read_u32(file, &state) &&
           read_u64(file, &organism->back_on_tick) &&
           read_i32(file, &organism->last_foolsday_roll_year) &&
           read_f32(file, &organism->reproduction_output) &&
           read_f32(file, &organism->acceptance_output) &&
           read_f32(file, &organism->sleep_output) &&
           read_f32(file, &organism->wake_output) &&
           read_f32(file, &organism->off_output) &&
           read_f32(file, &organism->off_duration_output);
    if (okay) {
        organism->state = (AlifeLifecycleState)state;
    }
    return okay;
}

static void abandon_world(AlifeWorld *world) {
    if (world->event_log != NULL) {
        (void)fclose(world->event_log);
    }
    free(world->organisms);
    free(world->data);
    free(world->scratch_ids);
    free(world->scratch_hidden);
    free(world->scratch_genome);
    memset(world, 0, sizeof(*world));
}

static bool checkpoint_date_valid(int32_t year, int32_t month, int32_t day) {
    static const int32_t month_days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int32_t maximum;
    bool leap;

    if (year < 1 || year > 9999 || month < 1 || month > 12) {
        return false;
    }
    maximum = month_days[month - 1];
    leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap) {
        maximum = 29;
    }
    return day >= 1 && day <= maximum;
}

bool alife_world_load(AlifeWorld *world, const AlifeConfig *config,
                      const char *path, char *error, size_t error_size) {
    FILE *file;
    CheckpointHeader header;
    size_t i;
    int trailing;

    if (path == NULL || path[0] == '\0') {
        alife_set_error(error, error_size, "A checkpoint path is required.");
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        alife_set_error(error, error_size, "Could not open checkpoint '%s': %s.",
                        path, strerror(errno));
        return false;
    }
    if (!read_header(file, &header, error, error_size)) {
        (void)fclose(file);
        return false;
    }
    if (!alife_setup_world(world, config, "a", false, error, error_size)) {
        (void)fclose(file);
        return false;
    }
    if (header.fingerprint != alife_config_fingerprint(config) ||
        header.genome_count != (uint64_t)world->layout.genome_count ||
        header.slot_floats != (uint64_t)world->layout.slot_floats) {
        alife_set_error(error, error_size,
                        "The checkpoint does not match the supplied configuration.");
        (void)fclose(file);
        abandon_world(world);
        return false;
    }
    if (!checkpoint_date_valid(header.year, header.month, header.day) ||
        header.stopped > 1U ||
        header.next_id == 0U || header.next_id != header.births + 1U ||
        header.tick > config->tick_count || header.deaths > header.births ||
        header.count != header.births - header.deaths ||
        !transition_counts_valid(header.state_transitions,
                                 header.awake_to_asleep,
                                 header.asleep_to_awake,
                                 header.asleep_to_off) ||
        (header.rng[0] | header.rng[1] | header.rng[2] | header.rng[3]) == 0U) {
        alife_set_error(error, error_size,
                        "The checkpoint contains invalid substrate state.");
        (void)fclose(file);
        abandon_world(world);
        return false;
    }
    if (header.count > (uint64_t)SIZE_MAX ||
        header.count > config->capacity_bytes /
                       (uint64_t)alife_organism_size(world) ||
        !alife_reserve(world, (size_t)header.count, error, error_size)) {
        if (error != NULL && error_size > 0U && error[0] == '\0') {
            alife_set_error(error, error_size, "The checkpoint population is invalid.");
        }
        (void)fclose(file);
        abandon_world(world);
        return false;
    }
    world->count = (size_t)header.count;
    for (i = 0U; i < world->count; ++i) {
        if (!read_organism(file, &world->organisms[i]) ||
            !read_bytes(file, alife_slot(world, i),
                        world->layout.slot_floats * sizeof(float))) {
            alife_set_error(error, error_size, "The checkpoint organism data is truncated.");
            (void)fclose(file);
            abandon_world(world);
            return false;
        }
        if (!alife_organism_state_valid(world, i) ||
            !checkpoint_date_valid(world->organisms[i].birth_year,
                                   world->organisms[i].birth_month,
                                   world->organisms[i].birth_day) ||
            world->organisms[i].id >= header.next_id ||
            world->organisms[i].birth_tick > header.tick ||
            world->organisms[i].reward != world->organisms[i].age ||
            (world->organisms[i].state == ALIFE_STATE_OFF &&
             (world->organisms[i].back_on_tick <= header.tick ||
              world->organisms[i].back_on_tick >
                  config->tick_count + config->off_max_duration_ticks)) ||
            (world->organisms[i].state != ALIFE_STATE_OFF &&
             world->organisms[i].back_on_tick != 0U) ||
            (world->organisms[i].last_foolsday_roll_year != INT32_MIN &&
             (world->organisms[i].last_foolsday_roll_year < 1 ||
              world->organisms[i].last_foolsday_roll_year > header.year)) ||
            world->organisms[i].mutations >
                (uint64_t)world->layout.genome_count ||
            world->organisms[i].significant_weight_changes >
                world->organisms[i].executions ||
            world->organisms[i].successful_reproductions >
                world->organisms[i].reproduction_attempts ||
            ((world->organisms[i].parent_a == 0U ||
              world->organisms[i].parent_b == 0U) &&
             (world->organisms[i].parent_a != 0U ||
              world->organisms[i].parent_b != 0U)) ||
            (world->organisms[i].parent_a == 0U &&
             world->organisms[i].generation != 0U) ||
            (world->organisms[i].parent_a != 0U &&
             (world->organisms[i].generation == 0U ||
              world->organisms[i].parent_a == world->organisms[i].parent_b ||
              world->organisms[i].parent_a >= world->organisms[i].id ||
              world->organisms[i].parent_b >= world->organisms[i].id))) {
            alife_set_error(error, error_size, "The checkpoint contains invalid organism state.");
            (void)fclose(file);
            abandon_world(world);
            return false;
        }
        {
            size_t previous;
            for (previous = 0U; previous < i; ++previous) {
                if (world->organisms[previous].id == world->organisms[i].id) {
                    alife_set_error(error, error_size,
                                    "The checkpoint contains duplicate organism IDs.");
                    (void)fclose(file);
                    abandon_world(world);
                    return false;
                }
            }
        }
    }
    trailing = fgetc(file);
    if (trailing != EOF || ferror(file) != 0) {
        alife_set_error(error, error_size, "The checkpoint has unexpected trailing data.");
        (void)fclose(file);
        abandon_world(world);
        return false;
    }
    (void)fclose(file);
    world->tick = header.tick;
    world->next_id = header.next_id;
    world->total_births = header.births;
    world->total_deaths = header.deaths;
    world->total_reproduction_attempts = header.attempts;
    world->total_mutations = header.mutations;
    world->total_executions = header.executions;
    world->total_state_transitions = header.state_transitions;
    world->awake_to_asleep_transitions = header.awake_to_asleep;
    world->asleep_to_awake_transitions = header.asleep_to_awake;
    world->asleep_to_off_transitions = header.asleep_to_off;
    world->year = header.year;
    world->month = header.month;
    world->day = header.day;
    world->stopped = header.stopped != 0U;
    for (i = 0U; i < 4U; ++i) {
        world->rng.state[i] = header.rng[i];
    }
    world->population_bytes =
        (uint64_t)world->count * (uint64_t)alife_organism_size(world);
    alife_log_run_start(world);
    return true;
}

bool alife_checkpoint_inspect(const char *path, FILE *output,
                              char *error, size_t error_size) {
    FILE *file;
    CheckpointHeader header;

    if (path == NULL || output == NULL) {
        alife_set_error(error, error_size, "A checkpoint path and output stream are required.");
        return false;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        alife_set_error(error, error_size, "Could not open checkpoint '%s': %s.",
                        path, strerror(errno));
        return false;
    }
    if (!read_header(file, &header, error, error_size)) {
        (void)fclose(file);
        return false;
    }
    (void)fclose(file);
    (void)fprintf(output,
                  "{\"checkpoint_version\":%" PRIu32
                  ",\"config_fingerprint\":%" PRIu64
                  ",\"tick\":%" PRIu64 ",\"population\":%" PRIu64
                  ",\"date\":\"%04" PRId32 "-%02" PRId32 "-%02" PRId32
                  "\",\"births\":%" PRIu64 ",\"deaths\":%" PRIu64
                  ",\"state_transitions\":%" PRIu64 "}\n",
                  header.version, header.fingerprint, header.tick, header.count,
                  header.year, header.month, header.day,
                  header.births, header.deaths, header.state_transitions);
    return true;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
    const unsigned char *bytes = data;
    size_t i;

    for (i = 0U; i < size; ++i) {
        hash ^= (uint64_t)bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

#define HASH_FIELD(hash, object, field) \
    ((hash) = hash_bytes((hash), &(object)->field, sizeof((object)->field)))

uint64_t alife_world_hash(const AlifeWorld *world) {
    uint64_t hash = UINT64_C(1469598103934665603);
    uint64_t fingerprint;
    size_t i;

    if (world == NULL || !world->initialized) {
        return 0U;
    }
    fingerprint = alife_config_fingerprint(&world->config);
    hash = hash_bytes(hash, &fingerprint, sizeof(fingerprint));
    HASH_FIELD(hash, world, tick);
    HASH_FIELD(hash, world, next_id);
    HASH_FIELD(hash, world, count);
    HASH_FIELD(hash, world, total_births);
    HASH_FIELD(hash, world, total_deaths);
    HASH_FIELD(hash, world, total_reproduction_attempts);
    HASH_FIELD(hash, world, total_mutations);
    HASH_FIELD(hash, world, total_executions);
    HASH_FIELD(hash, world, total_state_transitions);
    HASH_FIELD(hash, world, awake_to_asleep_transitions);
    HASH_FIELD(hash, world, asleep_to_awake_transitions);
    HASH_FIELD(hash, world, asleep_to_off_transitions);
    HASH_FIELD(hash, world, year);
    HASH_FIELD(hash, world, month);
    HASH_FIELD(hash, world, day);
    HASH_FIELD(hash, world, stopped);
    hash = hash_bytes(hash, world->rng.state, sizeof(world->rng.state));
    for (i = 0U; i < world->count; ++i) {
        const AlifeOrganism *organism = &world->organisms[i];
        HASH_FIELD(hash, organism, id);
        HASH_FIELD(hash, organism, parent_a);
        HASH_FIELD(hash, organism, parent_b);
        HASH_FIELD(hash, organism, birth_tick);
        HASH_FIELD(hash, organism, age);
        HASH_FIELD(hash, organism, reward);
        HASH_FIELD(hash, organism, generation);
        HASH_FIELD(hash, organism, reproduction_attempts);
        HASH_FIELD(hash, organism, successful_reproductions);
        HASH_FIELD(hash, organism, mutations);
        HASH_FIELD(hash, organism, significant_weight_changes);
        HASH_FIELD(hash, organism, executions);
        HASH_FIELD(hash, organism, birth_year);
        HASH_FIELD(hash, organism, birth_month);
        HASH_FIELD(hash, organism, birth_day);
        HASH_FIELD(hash, organism, state);
        HASH_FIELD(hash, organism, back_on_tick);
        HASH_FIELD(hash, organism, last_foolsday_roll_year);
        HASH_FIELD(hash, organism, reproduction_output);
        HASH_FIELD(hash, organism, acceptance_output);
        HASH_FIELD(hash, organism, sleep_output);
        HASH_FIELD(hash, organism, wake_output);
        HASH_FIELD(hash, organism, off_output);
        HASH_FIELD(hash, organism, off_duration_output);
        hash = hash_bytes(hash, alife_slot_const(world, i),
                          world->layout.slot_floats * sizeof(float));
    }
    return hash;
}
