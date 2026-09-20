#include "internal.h"
#include "alife/reward.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIFE_MAX_INPUTS 40U
#define ALIFE_MAX_HIDDEN 64U
#define ALIFE_MAX_COMMUNICATION 8U
#define ALIFE_PLASTIC_RATE_SCALE 0.05
#define ALIFE_SIGNIFICANT_PLASTICITY 0.01
#define ALIFE_SLEEP_OSCILLATOR_X 0U
#define ALIFE_SLEEP_OSCILLATOR_Y 1U

void alife_set_error(char *error, size_t error_size, const char *format, ...) {
    va_list arguments;

    if (error == NULL || error_size == 0U) {
        return;
    }
    va_start(arguments, format);
    (void)vsnprintf(error, error_size, format, arguments);
    va_end(arguments);
}

static double clamp_double(double value, double minimum, double maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

bool alife_layout_create(const AlifeConfig *config, AlifeLayout *layout,
                         char *error, size_t error_size) {
    size_t cursor = 0U;
    size_t hidden;
    size_t inputs;
    size_t communication;

    if (config == NULL || layout == NULL) {
        alife_set_error(error, error_size, "The layout arguments must not be null.");
        return false;
    }
    hidden = (size_t)config->hidden_size;
    communication = (size_t)config->communication_size;
    inputs = (size_t)config->input_size + communication +
             ALIFE_PRIVATE_COURTSHIP_INPUTS;
    if (hidden > ALIFE_MAX_HIDDEN || inputs > ALIFE_MAX_INPUTS ||
        communication > ALIFE_MAX_COMMUNICATION) {
        alife_set_error(error, error_size, "The neural dimensions exceed runtime limits.");
        return false;
    }

    memset(layout, 0, sizeof(*layout));
    layout->input_weights = cursor;
    cursor += hidden * inputs;
    layout->recurrent_weights = cursor;
    layout->recurrent_count = hidden * hidden;
    cursor += layout->recurrent_count;
    layout->hidden_biases = cursor;
    cursor += hidden;
    layout->output_weights = cursor;
    layout->output_count = communication + ALIFE_NONCOMMUNICATION_OUTPUTS;
    cursor += layout->output_count * hidden;
    layout->output_biases = cursor;
    cursor += layout->output_count;
    layout->plastic_rates = cursor;
    cursor += hidden;
    layout->plastic_decays = cursor;
    cursor += hidden;
    layout->genome_count = cursor;
    if (cursor < ALIFE_MIN_GENOME_PARAMETERS ||
        cursor > ALIFE_MAX_GENOME_PARAMETERS) {
        alife_set_error(error, error_size,
                        "The architecture has %zu genome parameters; use %u to %u.",
                        cursor, ALIFE_MIN_GENOME_PARAMETERS,
                        ALIFE_MAX_GENOME_PARAMETERS);
        return false;
    }
    layout->plastic_state = cursor;
    cursor += layout->recurrent_count;
    layout->hidden_state = cursor;
    cursor += hidden;
    layout->inbox = cursor;
    cursor += communication;
    layout->outbox = cursor;
    cursor += communication;
    layout->slot_floats = cursor;
    return true;
}

size_t alife_organism_size(const AlifeWorld *world) {
    if (world == NULL) {
        return 0U;
    }
    return sizeof(AlifeOrganism) + world->layout.slot_floats * sizeof(float);
}

float *alife_slot(AlifeWorld *world, size_t index) {
    return world->data + index * world->layout.slot_floats;
}

const float *alife_slot_const(const AlifeWorld *world, size_t index) {
    return world->data + index * world->layout.slot_floats;
}

bool alife_reserve(AlifeWorld *world, size_t needed, char *error,
                   size_t error_size) {
    size_t capacity;
    size_t slot_bytes;
    AlifeOrganism *new_organisms;
    float *new_data;
    uint64_t *new_scratch;

    if (needed <= world->slot_capacity && needed <= world->scratch_capacity) {
        return true;
    }
    capacity = world->slot_capacity == 0U ? 4U : world->slot_capacity;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2U) {
            alife_set_error(error, error_size, "The population allocation is too large.");
            return false;
        }
        capacity *= 2U;
    }
    if (world->layout.slot_floats > SIZE_MAX / sizeof(*new_data)) {
        alife_set_error(error, error_size, "The neural-state slot is too large.");
        return false;
    }
    slot_bytes = world->layout.slot_floats * sizeof(*new_data);
    if (capacity > SIZE_MAX / sizeof(*new_organisms) ||
        capacity > SIZE_MAX / sizeof(*new_scratch) ||
        (slot_bytes != 0U && capacity > SIZE_MAX / slot_bytes)) {
        alife_set_error(error, error_size, "The neural-state allocation is too large.");
        return false;
    }
    new_organisms = malloc(capacity * sizeof(*new_organisms));
    new_data = malloc(capacity * slot_bytes);
    new_scratch = malloc(capacity * sizeof(*new_scratch));
    if (new_organisms == NULL || new_data == NULL || new_scratch == NULL) {
        free(new_organisms);
        free(new_data);
        free(new_scratch);
        alife_set_error(error, error_size,
                        "Could not allocate space for %zu organisms.", capacity);
        return false;
    }
    if (world->count > 0U) {
        memcpy(new_organisms, world->organisms,
               world->count * sizeof(*new_organisms));
        memcpy(new_data, world->data,
               world->count * world->layout.slot_floats * sizeof(*new_data));
        memcpy(new_scratch, world->scratch_ids,
               world->count * sizeof(*new_scratch));
    }
    free(world->organisms);
    free(world->data);
    free(world->scratch_ids);
    world->organisms = new_organisms;
    world->data = new_data;
    world->scratch_ids = new_scratch;
    world->slot_capacity = capacity;
    world->scratch_capacity = capacity;
    return true;
}

static int days_in_month(int32_t year, int32_t month) {
    static const int days[] = {31, 28, 31, 30, 31, 30,
                               31, 31, 30, 31, 30, 31};
    int result = days[month - 1];
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;

    if (month == 2 && leap) {
        result = 29;
    }
    return result;
}

static void advance_date(AlifeWorld *world) {
    ++world->day;
    if (world->day > days_in_month(world->year, world->month)) {
        world->day = 1;
        ++world->month;
        if (world->month > 12) {
            world->month = 1;
            ++world->year;
        }
    }
}

static bool open_event_log(AlifeWorld *world, const char *mode,
                           char *error, size_t error_size) {
    if (world->config.logging_level < ALIFE_LOG_SUMMARY ||
        world->config.event_log_path[0] == '\0') {
        return true;
    }
    world->event_log = fopen(world->config.event_log_path, mode);
    if (world->event_log == NULL) {
        alife_set_error(error, error_size, "Could not open event log '%s': %s.",
                        world->config.event_log_path, strerror(errno));
        return false;
    }
    (void)setvbuf(world->event_log, NULL, _IOFBF, 64U * 1024U);
    return true;
}

static float bounded_gene(const AlifeWorld *world, double value) {
    return (float)clamp_double(value, -world->config.max_abs_weight,
                              world->config.max_abs_weight);
}

static void seed_sleep_rhythm(const AlifeWorld *world, float *genome) {
    const size_t hidden = (size_t)world->config.hidden_size;
    const size_t input_count = (size_t)world->config.input_size +
                               (size_t)world->config.communication_size +
                               ALIFE_PRIVATE_COURTSHIP_INPUTS;
    const size_t communication = (size_t)world->config.communication_size;
    const size_t x = ALIFE_SLEEP_OSCILLATOR_X;
    const size_t y = ALIFE_SLEEP_OSCILLATOR_Y;
    const double angle = 0.14;
    const double gain = 1.12;
    const size_t sleep_output = communication + ALIFE_OUTPUT_SLEEP;
    const size_t wake_output = communication + ALIFE_OUTPUT_WAKE;
    const size_t off_output = communication + ALIFE_OUTPUT_OFF;
    size_t i;

    for (i = 0U; i < input_count; ++i) {
        genome[world->layout.input_weights + x * input_count + i] *= 0.05F;
        genome[world->layout.input_weights + y * input_count + i] *= 0.05F;
    }
    for (i = 0U; i < hidden; ++i) {
        genome[world->layout.recurrent_weights + x * hidden + i] = 0.0F;
        genome[world->layout.recurrent_weights + y * hidden + i] = 0.0F;
        genome[world->layout.output_weights + sleep_output * hidden + i] = 0.0F;
        genome[world->layout.output_weights + wake_output * hidden + i] = 0.0F;
        genome[world->layout.output_weights + off_output * hidden + i] *= 0.25F;
    }
    genome[world->layout.recurrent_weights + x * hidden + x] =
        bounded_gene(world, gain * cos(angle));
    genome[world->layout.recurrent_weights + x * hidden + y] =
        bounded_gene(world, -gain * sin(angle));
    genome[world->layout.recurrent_weights + y * hidden + x] =
        bounded_gene(world, gain * sin(angle));
    genome[world->layout.recurrent_weights + y * hidden + y] =
        bounded_gene(world, gain * cos(angle));

    genome[world->layout.hidden_biases + x] = bounded_gene(
        world, 0.035 + 0.02 * (double)genome[world->layout.hidden_biases + x]);
    genome[world->layout.hidden_biases + y] = bounded_gene(
        world, 0.005 + 0.02 * (double)genome[world->layout.hidden_biases + y]);
    genome[world->layout.plastic_rates + x] = 0.0F;
    genome[world->layout.plastic_rates + y] = 0.0F;

    genome[world->layout.output_weights + sleep_output * hidden + x] =
        bounded_gene(world, 3.0);
    genome[world->layout.output_weights + wake_output * hidden + x] =
        bounded_gene(world, -3.0);
    genome[world->layout.output_biases + sleep_output] = 0.0F;
    genome[world->layout.output_biases + wake_output] = 0.0F;
    genome[world->layout.output_biases + off_output] = bounded_gene(world, -0.75);
}

static void initialize_genome(AlifeWorld *world, float *genome) {
    size_t i;
    size_t hidden = (size_t)world->config.hidden_size;
    size_t communication = (size_t)world->config.communication_size;
    double scale = 1.0 / sqrt((double)hidden);
    double plastic_scale;

    if (scale > world->config.max_abs_weight) {
        scale = world->config.max_abs_weight;
    }
    plastic_scale = world->config.max_abs_weight < 0.1 ?
                    world->config.max_abs_weight : 0.1;

    for (i = 0U; i < world->layout.genome_count; ++i) {
        genome[i] = (float)(alife_rng_symmetric(&world->rng) * scale);
    }
    for (i = 0U; i < hidden; ++i) {
        genome[world->layout.plastic_rates + i] =
            (float)(alife_rng_symmetric(&world->rng) * plastic_scale);
        genome[world->layout.plastic_decays + i] = 0.0F;
    }
    for (i = 0U; i < communication; ++i) {
        genome[world->layout.output_biases + i] = 0.0F;
    }
    genome[world->layout.output_biases + communication] =
        (float)(world->config.max_abs_weight < 0.5 ?
                world->config.max_abs_weight : 0.5);
    genome[world->layout.output_biases + communication + 1U] =
        genome[world->layout.output_biases + communication];
    for (i = communication + ALIFE_OUTPUT_SLEEP;
         i < communication + ALIFE_NONCOMMUNICATION_OUTPUTS; ++i) {
        genome[world->layout.output_biases + i] = 0.0F;
    }
    seed_sleep_rhythm(world, genome);
}

static bool is_sleep_rhythm_gene(const AlifeWorld *world, size_t position) {
    const size_t hidden = (size_t)world->config.hidden_size;
    const size_t communication = (size_t)world->config.communication_size;
    const size_t sleep_weight = world->layout.output_weights +
        (communication + ALIFE_OUTPUT_SLEEP) * hidden + ALIFE_SLEEP_OSCILLATOR_X;
    const size_t wake_weight = world->layout.output_weights +
        (communication + ALIFE_OUTPUT_WAKE) * hidden + ALIFE_SLEEP_OSCILLATOR_X;

    return position == world->layout.recurrent_weights ||
           position == world->layout.recurrent_weights + 1U ||
           position == world->layout.recurrent_weights + hidden ||
           position == world->layout.recurrent_weights + hidden + 1U ||
           position == world->layout.hidden_biases + ALIFE_SLEEP_OSCILLATOR_X ||
           position == world->layout.hidden_biases + ALIFE_SLEEP_OSCILLATOR_Y ||
           position == sleep_weight || position == wake_weight;
}

static bool mutate_related_rhythm_gene(AlifeWorld *world, float *genome,
                                       uint64_t *mutations) {
    const size_t x_bias = world->layout.hidden_biases +
                          ALIFE_SLEEP_OSCILLATOR_X;
    const size_t y_bias = world->layout.hidden_biases +
                          ALIFE_SLEEP_OSCILLATOR_Y;
    const size_t position = alife_rng_bounded(&world->rng, 2U) == 0U ?
                            x_bias : y_bias;
    const double magnitude = world->config.mutation_magnitude;
    const double direction = alife_rng_bounded(&world->rng, 2U) == 0U ?
                             -1.0 : 1.0;
    const double size = magnitude * (0.1 + 0.1 * alife_rng_unit(&world->rng));
    const float old_value = genome[position];
    double value;

    if (magnitude == 0.0) {
        return false;
    }
    value = clamp_double((double)old_value + direction * size,
                         -world->config.max_abs_weight,
                         world->config.max_abs_weight);
    if ((float)value == old_value) {
        value = clamp_double((double)old_value - direction * size,
                             -world->config.max_abs_weight,
                             world->config.max_abs_weight);
    }
    genome[position] = (float)value;
    if (genome[position] != old_value) {
        ++(*mutations);
        return true;
    }
    return false;
}

static bool append_organism(AlifeWorld *world, const float *genome,
                            uint64_t parent_a, uint64_t parent_b,
                            uint64_t generation, uint64_t mutations,
                            char *error, size_t error_size) {
    AlifeOrganism *organism;
    float *slot;
    size_t i;
    size_t organism_bytes = alife_organism_size(world);

    if (world->count == SIZE_MAX ||
        (organism_bytes != 0U &&
         world->count + 1U > UINT64_MAX / (uint64_t)organism_bytes)) {
        alife_set_error(error, error_size, "The population byte count would overflow.");
        return false;
    }
    if (!alife_reserve(world, world->count + 1U, error, error_size)) {
        return false;
    }
    organism = &world->organisms[world->count];
    memset(organism, 0, sizeof(*organism));
    organism->id = world->next_id++;
    organism->parent_a = parent_a;
    organism->parent_b = parent_b;
    organism->birth_tick = world->tick;
    organism->generation = generation;
    organism->mutations = mutations;
    organism->birth_year = world->year;
    organism->birth_month = world->month;
    organism->birth_day = world->day;
    organism->state = ALIFE_STATE_AWAKE;
    organism->last_awake_tick = world->tick;
    organism->last_foolsday_roll_year = INT32_MIN;
    slot = alife_slot(world, world->count);
    memset(slot, 0, world->layout.slot_floats * sizeof(*slot));
    memcpy(slot, genome, world->layout.genome_count * sizeof(*slot));
    for (i = 0U; i < (size_t)world->config.hidden_size; ++i) {
        slot[world->layout.hidden_state + i] =
            (float)tanh((double)genome[world->layout.hidden_biases + i]);
    }
    ++world->count;
    ++world->total_births;
    world->total_mutations += mutations;
    world->population_bytes =
        (uint64_t)world->count * (uint64_t)organism_bytes;
    alife_log_birth(world, organism);
    return true;
}

static bool seed_population(AlifeWorld *world, char *error, size_t error_size) {
    float *base;
    float *related;
    size_t i;
    uint64_t mutations = 0U;
    double probability = world->config.mutation_probability;
    bool rhythm_mutated = false;

    base = malloc(world->layout.genome_count * sizeof(*base));
    related = malloc(world->layout.genome_count * sizeof(*related));
    if (base == NULL || related == NULL) {
        free(base);
        free(related);
        alife_set_error(error, error_size, "Could not allocate seed genomes.");
        return false;
    }
    initialize_genome(world, base);
    memcpy(related, base, world->layout.genome_count * sizeof(*related));
    if (probability < 0.02) {
        probability = 0.02;
    }
    for (i = 0U; i < world->layout.genome_count; ++i) {
        if (alife_rng_unit(&world->rng) < probability) {
            float old_value = related[i];
            double value = (double)related[i] +
                alife_rng_symmetric(&world->rng) * world->config.mutation_magnitude;
            related[i] = (float)clamp_double(value,
                                             -world->config.max_abs_weight,
                                             world->config.max_abs_weight);
            if (related[i] != old_value) {
                ++mutations;
                if (is_sleep_rhythm_gene(world, i)) {
                    rhythm_mutated = true;
                }
            }
        }
    }
    if (!rhythm_mutated) {
        (void)mutate_related_rhythm_gene(world, related, &mutations);
    }
    if (mutations == 0U) {
        size_t position = (size_t)alife_rng_bounded(
            &world->rng, (uint64_t)world->layout.genome_count);
        if (related[position] != 0.0F) {
            related[position] = -related[position];
        } else {
            related[position] = (float)(0.5 * world->config.max_abs_weight);
        }
        mutations = 1U;
    }
    if (!alife_genome_validate(world, base, error, error_size) ||
        !alife_genome_validate(world, related, error, error_size)) {
        free(base);
        free(related);
        return false;
    }
    if (!append_organism(world, base, 0U, 0U, 0U, 0U, error, error_size) ||
        !append_organism(world, related, 0U, 0U, 0U, mutations,
                         error, error_size)) {
        free(base);
        free(related);
        return false;
    }
    free(base);
    free(related);
    return true;
}

bool alife_setup_world(AlifeWorld *world, const AlifeConfig *config,
                       const char *log_mode, bool add_seeds,
                       char *error, size_t error_size) {
    if (world == NULL || config == NULL) {
        alife_set_error(error, error_size, "The world arguments must not be null.");
        return false;
    }
    memset(world, 0, sizeof(*world));
    if (!alife_config_validate(config, error, error_size)) {
        return false;
    }
    world->config = *config;
    if (!alife_layout_create(config, &world->layout, error, error_size)) {
        return false;
    }
    if ((uint64_t)alife_organism_size(world) >
        config->capacity_bytes / (uint64_t)config->initial_population) {
        alife_set_error(error, error_size,
                        "Capacity must hold the two seed organisms (%zu bytes each).",
                        alife_organism_size(world));
        return false;
    }
    world->scratch_hidden = malloc((size_t)config->hidden_size *
                                   sizeof(*world->scratch_hidden));
    world->scratch_genome = malloc(world->layout.genome_count *
                                   sizeof(*world->scratch_genome));
    if (world->scratch_hidden == NULL || world->scratch_genome == NULL) {
        free(world->scratch_hidden);
        free(world->scratch_genome);
        world->scratch_hidden = NULL;
        world->scratch_genome = NULL;
        alife_set_error(error, error_size, "Could not allocate neural scratch space.");
        return false;
    }
    alife_rng_seed(&world->rng, config->seed);
    world->next_id = 1U;
    world->year = (int32_t)config->calendar_start_year;
    world->month = (int32_t)config->calendar_start_month;
    world->day = (int32_t)config->calendar_start_day;
    if (!open_event_log(world, log_mode, error, error_size)) {
        free(world->scratch_hidden);
        free(world->scratch_genome);
        world->scratch_hidden = NULL;
        world->scratch_genome = NULL;
        return false;
    }
    world->initialized = true;
    if (add_seeds && !seed_population(world, error, error_size)) {
        alife_world_destroy(world);
        return false;
    }
    return true;
}

bool alife_world_init(AlifeWorld *world, const AlifeConfig *config,
                      char *error, size_t error_size) {
    if (!alife_setup_world(world, config, "w", false, error, error_size)) {
        return false;
    }
    alife_log_run_start(world);
    if (!seed_population(world, error, error_size)) {
        alife_world_destroy(world);
        return false;
    }
    return true;
}

static size_t find_index(const AlifeWorld *world, uint64_t id);

static void cancel_courtship(AlifeWorld *world, uint64_t organism_id,
                             const char *reason) {
    AlifeOrganism *organism = alife_find_organism_mut(world, organism_id);
    AlifeOrganism *partner;
    uint64_t partner_id;
    uint64_t progress;

    if (organism == NULL || organism->courtship_partner_id == 0U) {
        return;
    }
    partner_id = organism->courtship_partner_id;
    progress = organism->courtship_progress;
    partner = alife_find_organism_mut(world, partner_id);
    organism->courtship_partner_id = 0U;
    organism->courtship_progress = 0U;
    organism->courtship_input = 0.0F;
    if (partner != NULL && partner->courtship_partner_id == organism_id) {
        partner->courtship_partner_id = 0U;
        partner->courtship_progress = 0U;
        partner->courtship_input = 0.0F;
    }
    if (world->active_courtships > 0U) {
        --world->active_courtships;
    }
    ++world->courtships_failed;
    alife_log_courtship(world, "courtship_failed", organism_id, partner_id,
                        progress, reason);
}

static void remove_at(AlifeWorld *world, size_t index, AlifeDeathCause cause) {
    size_t last = world->count - 1U;
    uint64_t id = world->organisms[index].id;

    if (world->organisms[index].courtship_partner_id != 0U) {
        cancel_courtship(world, id, "death");
        index = find_index(world, id);
        last = world->count - 1U;
    }

    alife_log_death(world, &world->organisms[index], cause);
    ++world->total_deaths;
    if (index != last) {
        world->organisms[index] = world->organisms[last];
        memcpy(alife_slot(world, index), alife_slot(world, last),
               world->layout.slot_floats * sizeof(float));
    }
    --world->count;
    world->population_bytes =
        (uint64_t)world->count * (uint64_t)alife_organism_size(world);
}

void alife_world_destroy(AlifeWorld *world) {
    size_t i;

    if (world == NULL) {
        return;
    }
    if (world->initialized) {
        for (i = 0U; i < world->count; ++i) {
            alife_log_death(world, &world->organisms[i], ALIFE_DEATH_SHUTDOWN);
            ++world->total_deaths;
        }
        world->count = 0U;
        world->population_bytes = 0U;
        alife_log_run_end(world);
    }
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

static size_t find_index(const AlifeWorld *world, uint64_t id) {
    size_t i;

    for (i = 0U; i < world->count; ++i) {
        if (world->organisms[i].id == id) {
            return i;
        }
    }
    return SIZE_MAX;
}

const AlifeOrganism *alife_find_organism(const AlifeWorld *world, uint64_t id) {
    size_t index;

    if (world == NULL) {
        return NULL;
    }
    index = find_index(world, id);
    return index == SIZE_MAX ? NULL : &world->organisms[index];
}

AlifeOrganism *alife_find_organism_mut(AlifeWorld *world, uint64_t id) {
    size_t index;

    if (world == NULL) {
        return NULL;
    }
    index = find_index(world, id);
    return index == SIZE_MAX ? NULL : &world->organisms[index];
}

const char *alife_lifecycle_state_name(AlifeLifecycleState state) {
    switch (state) {
        case ALIFE_STATE_AWAKE:
            return "awake";
        case ALIFE_STATE_ASLEEP:
            return "asleep";
        case ALIFE_STATE_OFF:
            return "off";
        default:
            return "invalid";
    }
}

static void clear_public_outputs(AlifeWorld *world, size_t index) {
    AlifeOrganism *organism = &world->organisms[index];
    float *slot = alife_slot(world, index);
    size_t i;

    organism->reproduction_output = 0.0F;
    organism->acceptance_output = 0.0F;
    organism->courtship_output = 0.0F;
    organism->courtship_input = 0.0F;
    organism->sent_message_this_tick = false;
    for (i = 0U; i < (size_t)world->config.communication_size; ++i) {
        slot[world->layout.inbox + i] = 0.0F;
        slot[world->layout.outbox + i] = 0.0F;
    }
}

static bool transition_at(AlifeWorld *world, size_t index,
                          AlifeLifecycleState requested_state,
                          uint64_t requested_off_duration,
                          char *error, size_t error_size) {
    AlifeOrganism *organism = &world->organisms[index];
    AlifeLifecycleState previous_state = organism->state;
    uint64_t duration = requested_off_duration;
    bool count_transition;
    bool allowed = false;

    if (previous_state == ALIFE_STATE_AWAKE &&
        requested_state == ALIFE_STATE_ASLEEP) {
        allowed = true;
    } else if (previous_state == ALIFE_STATE_ASLEEP &&
               requested_state == ALIFE_STATE_AWAKE) {
        allowed = true;
    } else if (previous_state == ALIFE_STATE_ASLEEP &&
               requested_state == ALIFE_STATE_OFF) {
        allowed = true;
        if (duration < world->config.off_min_duration_ticks) {
            duration = world->config.off_min_duration_ticks;
        }
        if (duration > world->config.off_max_duration_ticks) {
            duration = world->config.off_max_duration_ticks;
        }
        if (duration > UINT64_MAX - world->tick) {
            alife_set_error(error, error_size,
                            "The requested off timer would overflow.");
            return false;
        }
    } else if (previous_state == ALIFE_STATE_OFF &&
               requested_state == ALIFE_STATE_ASLEEP &&
               world->tick >= organism->back_on_tick) {
        allowed = true;
        duration = 0U;
    }
    if (!allowed) {
        alife_set_error(error, error_size, "Transition from %s to %s is not allowed.",
                        alife_lifecycle_state_name(previous_state),
                        alife_lifecycle_state_name(requested_state));
        return false;
    }

    organism->state = requested_state;
    organism->back_on_tick = requested_state == ALIFE_STATE_OFF ?
                             world->tick + duration : 0U;
    if (requested_state != ALIFE_STATE_AWAKE) {
        if (organism->courtship_partner_id != 0U) {
            uint64_t id = organism->id;
            cancel_courtship(world, id,
                             requested_state == ALIFE_STATE_ASLEEP ?
                             "sleep" : "off");
            organism = alife_find_organism_mut(world, id);
        }
        clear_public_outputs(world, index);
    } else {
        organism->last_awake_tick = world->tick;
    }
    count_transition = world->total_state_transitions < UINT64_MAX;
    if (count_transition) {
        ++world->total_state_transitions;
    }
    if (previous_state == ALIFE_STATE_AWAKE &&
        requested_state == ALIFE_STATE_ASLEEP &&
        count_transition &&
        world->awake_to_asleep_transitions < UINT64_MAX) {
        ++world->awake_to_asleep_transitions;
    } else if (previous_state == ALIFE_STATE_ASLEEP &&
               requested_state == ALIFE_STATE_AWAKE &&
               count_transition &&
               world->asleep_to_awake_transitions < UINT64_MAX) {
        ++world->asleep_to_awake_transitions;
    } else if (previous_state == ALIFE_STATE_ASLEEP &&
               requested_state == ALIFE_STATE_OFF &&
               count_transition &&
               world->asleep_to_off_transitions < UINT64_MAX) {
        ++world->asleep_to_off_transitions;
    }
    alife_log_state_transition(world, organism, previous_state,
                               requested_state == ALIFE_STATE_OFF ?
                               requested_off_duration : 0U);
    return true;
}

bool alife_transition_request(AlifeWorld *world, uint64_t organism_id,
                              AlifeLifecycleState requested_state,
                              uint64_t requested_off_duration,
                              char *error, size_t error_size) {
    size_t index;

    if (world == NULL || !world->initialized) {
        alife_set_error(error, error_size, "The world is not initialized.");
        return false;
    }
    index = find_index(world, organism_id);
    if (index == SIZE_MAX) {
        alife_set_error(error, error_size, "The organism is not living.");
        return false;
    }
    return transition_at(world, index, requested_state, requested_off_duration,
                         error, error_size);
}

float *alife_organism_genome_mut(AlifeWorld *world, uint64_t id) {
    size_t index;

    if (world == NULL) {
        return NULL;
    }
    index = find_index(world, id);
    return index == SIZE_MAX ? NULL : alife_slot(world, index);
}

const float *alife_organism_genome(const AlifeWorld *world, uint64_t id) {
    size_t index;

    if (world == NULL) {
        return NULL;
    }
    index = find_index(world, id);
    return index == SIZE_MAX ? NULL : alife_slot_const(world, index);
}

bool alife_genome_validate(const AlifeWorld *world, const float *genome,
                           char *error, size_t error_size) {
    size_t i;

    if (world == NULL || genome == NULL) {
        alife_set_error(error, error_size, "The genome must not be null.");
        return false;
    }
    for (i = 0U; i < world->layout.genome_count; ++i) {
        if (!isfinite((double)genome[i]) ||
            fabs((double)genome[i]) > world->config.max_abs_weight) {
            alife_set_error(error, error_size,
                            "Genome parameter %zu is not finite or exceeds its bound.", i);
            return false;
        }
    }
    return true;
}

bool alife_organism_state_valid(const AlifeWorld *world, size_t index) {
    const float *slot = alife_slot_const(world, index);
    const AlifeOrganism *organism = &world->organisms[index];
    size_t i;

    if (organism->id == 0U ||
        organism->state < ALIFE_STATE_AWAKE ||
        organism->state > ALIFE_STATE_OFF ||
        !isfinite((double)organism->reproduction_output) ||
        !isfinite((double)organism->acceptance_output) ||
        !isfinite((double)organism->sleep_output) ||
        !isfinite((double)organism->wake_output) ||
        !isfinite((double)organism->off_output) ||
        !isfinite((double)organism->off_duration_output) ||
        !isfinite((double)organism->courtship_output) ||
        !isfinite((double)organism->courtship_input) ||
        fabs((double)organism->reproduction_output) > 1.0001 ||
        fabs((double)organism->acceptance_output) > 1.0001 ||
        fabs((double)organism->sleep_output) > 1.0001 ||
        fabs((double)organism->wake_output) > 1.0001 ||
        fabs((double)organism->off_output) > 1.0001 ||
        fabs((double)organism->off_duration_output) > 1.0001 ||
        fabs((double)organism->courtship_output) > 1.0001 ||
        fabs((double)organism->courtship_input) > 1.0001 ||
        (organism->state == ALIFE_STATE_OFF &&
         organism->back_on_tick <= world->tick)) {
        return false;
    }
    if (!alife_genome_validate(world, slot, NULL, 0U)) {
        return false;
    }
    for (i = world->layout.plastic_state; i < world->layout.hidden_state; ++i) {
        size_t edge = i - world->layout.plastic_state;
        double effective =
            (double)slot[world->layout.recurrent_weights + edge] +
            (double)slot[i];
        if (!isfinite((double)slot[i]) ||
            fabs((double)slot[i]) > world->config.plasticity_limit + 1.0e-6 ||
            !isfinite(effective) ||
            fabs(effective) > world->config.max_abs_weight + 1.0e-6) {
            return false;
        }
    }
    for (i = world->layout.hidden_state; i < world->layout.slot_floats; ++i) {
        if (!isfinite((double)slot[i]) || fabs((double)slot[i]) > 1.0001) {
            return false;
        }
    }
    return true;
}

double alife_reproduction_probability(const AlifeWorld *world,
                                      const AlifeOrganism *organism) {
    double progress;

    if (world == NULL || organism == NULL ||
        organism->biological_age / ALIFE_BIOLOGICAL_AGE_SCALE <
            world->config.maturity_age) {
        return 0.0;
    }
    if (world->config.reproduction_ramp_ticks == 0U) {
        return world->config.reproduction_max_probability;
    }
    progress = ((double)organism->biological_age /
                (double)ALIFE_BIOLOGICAL_AGE_SCALE -
                (double)world->config.maturity_age) /
               (double)world->config.reproduction_ramp_ticks;
    progress = clamp_double(progress, 0.0, 1.0);
    return world->config.reproduction_base_probability +
           progress * (world->config.reproduction_max_probability -
                       world->config.reproduction_base_probability);
}

bool alife_reproduction_eligible(const AlifeWorld *world,
                                 const AlifeOrganism *organism) {
    return world != NULL && organism != NULL && !world->stopped &&
           !(world->month == 4 && world->day == 1) &&
           organism->state == ALIFE_STATE_AWAKE &&
           organism->biological_age / ALIFE_BIOLOGICAL_AGE_SCALE >=
               world->config.maturity_age &&
           organism->reproduction_output > 0.0F &&
           organism->acceptance_output > 0.0F;
}

static void record_attempt(AlifeWorld *world, AlifeOrganism *first,
                           AlifeOrganism *second) {
    ++world->total_reproduction_attempts;
    if (first != NULL) {
        ++first->reproduction_attempts;
    }
    if (second != NULL && second != first) {
        ++second->reproduction_attempts;
    }
}

static bool complete_birth(AlifeWorld *world, uint64_t parent_a_id,
                           uint64_t parent_b_id, char *error,
                           size_t error_size) {
    size_t first_index;
    size_t second_index;
    AlifeOrganism *first;
    AlifeOrganism *second;
    float *child;
    const float *first_genome;
    const float *second_genome;
    size_t i;
    uint64_t mutations = 0U;
    uint64_t generation;
    bool result;

    if (world == NULL || !world->initialized) {
        alife_set_error(error, error_size, "The world is not initialized.");
        return false;
    }
    first_index = find_index(world, parent_a_id);
    second_index = find_index(world, parent_b_id);
    first = first_index == SIZE_MAX ? NULL : &world->organisms[first_index];
    second = second_index == SIZE_MAX ? NULL : &world->organisms[second_index];
    if (parent_a_id == parent_b_id) {
        alife_log_reproduction(world, parent_a_id, parent_b_id, false,
                               "parents_not_distinct");
        alife_set_error(error, error_size, "Reproduction requires two distinct organisms.");
        return false;
    }
    if (first == NULL || second == NULL) {
        alife_log_reproduction(world, parent_a_id, parent_b_id, false,
                               "parent_not_living");
        alife_set_error(error, error_size, "Both parents must be living organisms.");
        return false;
    }
    if (!alife_reproduction_eligible(world, first) ||
        !alife_reproduction_eligible(world, second)) {
        alife_log_reproduction(world, parent_a_id, parent_b_id, false,
                               "parent_not_eligible");
        alife_set_error(error, error_size, "Both parents must be mature and consenting.");
        return false;
    }
    if ((uint64_t)alife_organism_size(world) > world->config.capacity_bytes) {
        alife_log_reproduction(world, parent_a_id, parent_b_id, false,
                               "insufficient_capacity");
        alife_set_error(error, error_size, "Capacity cannot hold an offspring.");
        return false;
    }
    child = world->scratch_genome;
    first_genome = alife_slot_const(world, first_index);
    second_genome = alife_slot_const(world, second_index);
    for (i = 0U; i < world->layout.genome_count; ++i) {
        double value;
        uint64_t choice = alife_rng_bounded(&world->rng, 20U);
        if (choice < 2U) {
            value = 0.5 * ((double)first_genome[i] + (double)second_genome[i]);
        } else if ((choice & 1U) == 0U) {
            value = (double)first_genome[i];
        } else {
            value = (double)second_genome[i];
        }
        if (alife_rng_unit(&world->rng) < world->config.mutation_probability) {
            value += alife_rng_symmetric(&world->rng) *
                     world->config.mutation_magnitude;
            ++mutations;
        }
        child[i] = (float)clamp_double(value,
                                       -world->config.max_abs_weight,
                                       world->config.max_abs_weight);
    }
    if (!alife_genome_validate(world, child, error, error_size)) {
        alife_log_reproduction(world, parent_a_id, parent_b_id, false,
                               "invalid_offspring");
        return false;
    }
    generation = first->generation > second->generation ?
                 first->generation + 1U : second->generation + 1U;
    if (!alife_reserve(world, world->count + 1U, error, error_size)) {
        alife_log_reproduction(world, parent_a_id, parent_b_id, false,
                               "allocation_failed");
        return false;
    }
    alife_log_reproduction(world, parent_a_id, parent_b_id, true, "approved");
    result = append_organism(world, child, parent_a_id, parent_b_id,
                             generation, mutations, error, error_size);
    if (!result) {
        alife_log_reproduction(world, parent_a_id, parent_b_id, false,
                               "allocation_failed");
        return false;
    }
    first = alife_find_organism_mut(world, parent_a_id);
    second = alife_find_organism_mut(world, parent_b_id);
    if (first != NULL) {
        ++first->successful_reproductions;
    }
    if (second != NULL) {
        ++second->successful_reproductions;
    }
    alife_enforce_capacity(world);
    return true;
}

bool alife_try_birth(AlifeWorld *world, uint64_t parent_a_id,
                     uint64_t parent_b_id, bool opportunity_granted,
                     char *error, size_t error_size) {
    AlifeOrganism *first;
    AlifeOrganism *second;

    if (world == NULL || !world->initialized) {
        alife_set_error(error, error_size, "The world is not initialized.");
        return false;
    }
    first = alife_find_organism_mut(world, parent_a_id);
    second = alife_find_organism_mut(world, parent_b_id);
    record_attempt(world, first, second);
    if (parent_a_id == parent_b_id || first == NULL || second == NULL) {
        alife_set_error(error, error_size,
                        "Courtship requires two distinct living organisms.");
        return false;
    }
    if (!opportunity_granted) {
        alife_set_error(error, error_size,
                        "The substrate did not grant an opportunity.");
        return false;
    }
    if (first->courtship_partner_id != 0U ||
        second->courtship_partner_id != 0U) {
        alife_set_error(error, error_size,
                        "Both organisms must be available for courtship.");
        return false;
    }
    if (!alife_reproduction_eligible(world, first) ||
        !alife_reproduction_eligible(world, second)) {
        alife_set_error(error, error_size,
                        "Both organisms must be mature and consenting.");
        return false;
    }
    first->courtship_partner_id = second->id;
    second->courtship_partner_id = first->id;
    first->courtship_progress = 0U;
    second->courtship_progress = 0U;
    ++world->active_courtships;
    ++world->courtships_started;
    alife_log_courtship(world, "courtship_started", first->id, second->id,
                        0U, "started");
    return true;
}

void alife_enforce_capacity(AlifeWorld *world) {
    while (world != NULL && world->count > 0U &&
           world->population_bytes > world->config.capacity_bytes) {
        size_t oldest = 0U;
        size_t i;
        for (i = 1U; i < world->count; ++i) {
            if (world->organisms[i].chronological_age >
                    world->organisms[oldest].chronological_age ||
                (world->organisms[i].chronological_age ==
                     world->organisms[oldest].chronological_age &&
                 world->organisms[i].id < world->organisms[oldest].id)) {
                oldest = i;
            }
        }
        remove_at(world, oldest, ALIFE_DEATH_CAPACITY);
    }
}

static void build_inputs(const AlifeWorld *world, size_t index, float *inputs) {
    const AlifeOrganism *organism = &world->organisms[index];
    const float *slot = alife_slot_const(world, index);
    size_t input_size = (size_t)world->config.input_size;
    size_t communication = (size_t)world->config.communication_size;
    size_t i;
    double scale = (double)(world->config.maturity_age +
                            world->config.reproduction_ramp_ticks + 1U);
    double day_fraction = ((double)(world->month - 1) * 31.0 +
                           (double)(world->day - 1)) / 372.0;

    for (i = 0U; i < input_size + communication +
                    ALIFE_PRIVATE_COURTSHIP_INPUTS; ++i) {
        inputs[i] = 0.0F;
    }
    inputs[0] = (float)clamp_double(
        ((double)organism->biological_age /
         (double)ALIFE_BIOLOGICAL_AGE_SCALE) / scale, 0.0, 1.0);
    inputs[1] = (float)clamp_double(
        ((double)organism->reward / (double)ALIFE_BIOLOGICAL_AGE_SCALE) /
        scale, 0.0, 1.0);
    inputs[2] = organism->biological_age / ALIFE_BIOLOGICAL_AGE_SCALE >=
                world->config.maturity_age ? 1.0F : 0.0F;
    inputs[3] = (float)alife_reproduction_probability(world, organism);
    inputs[4] = world->config.capacity_bytes == 0U ? 0.0F :
        (float)((double)world->population_bytes /
                (double)world->config.capacity_bytes);
    inputs[5] = (float)day_fraction;
    if (input_size > 6U) {
        inputs[6] = (float)((double)world->month / 12.0);
    }
    if (input_size > 7U) {
        inputs[7] = (float)((double)world->day / 31.0);
    }
    for (i = 0U; i < communication; ++i) {
        inputs[input_size + i] = slot[world->layout.inbox + i];
    }
    inputs[input_size + communication] = organism->courtship_input;
}

static void execute_organism(AlifeWorld *world, size_t index, bool asleep) {
    size_t hidden = (size_t)world->config.hidden_size;
    size_t input_count = (size_t)world->config.input_size +
                         (size_t)world->config.communication_size +
                         ALIFE_PRIVATE_COURTSHIP_INPUTS;
    size_t communication = (size_t)world->config.communication_size;
    float inputs[ALIFE_MAX_INPUTS];
    float *slot = alife_slot(world, index);
    float *state = slot + world->layout.hidden_state;
    float *delta = slot + world->layout.plastic_state;
    size_t i;
    size_t j;
    double plastic_magnitude = 0.0;

    if (asleep) {
        memset(inputs, 0, input_count * sizeof(*inputs));
    } else {
        build_inputs(world, index, inputs);
    }
    for (i = 0U; i < hidden; ++i) {
        double activation = (double)slot[world->layout.hidden_biases + i];
        if (!asleep) {
            for (j = 0U; j < input_count; ++j) {
                activation += (double)slot[world->layout.input_weights +
                                          i * input_count + j] *
                              (double)inputs[j];
            }
        }
        for (j = 0U; j < hidden; ++j) {
            size_t edge = i * hidden + j;
            activation += ((double)slot[world->layout.recurrent_weights + edge] +
                           (double)delta[edge]) * (double)state[j];
        }
        world->scratch_hidden[i] = (float)tanh(activation);
    }
    for (i = 0U; i < world->layout.output_count; ++i) {
        double activation = (double)slot[world->layout.output_biases + i];
        for (j = 0U; j < hidden; ++j) {
            activation += (double)slot[world->layout.output_weights +
                                      i * hidden + j] *
                          (double)world->scratch_hidden[j];
        }
        if (i < communication) {
            slot[world->layout.outbox + i] = (float)tanh(activation);
        } else {
            float output = (float)tanh(activation);
            switch (i - communication) {
                case ALIFE_OUTPUT_REPRODUCTION:
                    world->organisms[index].reproduction_output = output;
                    break;
                case ALIFE_OUTPUT_ACCEPTANCE:
                    world->organisms[index].acceptance_output = output;
                    break;
                case ALIFE_OUTPUT_SLEEP:
                    world->organisms[index].sleep_output = output;
                    break;
                case ALIFE_OUTPUT_WAKE:
                    world->organisms[index].wake_output = output;
                    break;
                case ALIFE_OUTPUT_OFF:
                    world->organisms[index].off_output = output;
                    break;
                case ALIFE_OUTPUT_OFF_DURATION:
                    world->organisms[index].off_duration_output = output;
                    break;
                case ALIFE_OUTPUT_COURTSHIP_SIGNAL:
                    world->organisms[index].courtship_output = output;
                    break;
                default:
                    break;
            }
        }
    }
    if (asleep) {
        for (i = 0U; i < hidden; ++i) {
            double rate = ALIFE_PLASTIC_RATE_SCALE *
                          tanh((double)slot[world->layout.plastic_rates + i]);
            double decay = clamp_double(
                world->config.plasticity_decay +
                0.01 * tanh((double)slot[world->layout.plastic_decays + i]),
                0.0, 1.0);
            for (j = 0U; j < hidden; ++j) {
                size_t edge = i * hidden + j;
                double old_value = (double)delta[edge];
                double value = decay * old_value + rate * (double)state[j] *
                               (double)world->scratch_hidden[i];
                double base = (double)slot[world->layout.recurrent_weights + edge];
                value = clamp_double(value, -world->config.plasticity_limit,
                                     world->config.plasticity_limit);
                value = clamp_double(value,
                                     -world->config.max_abs_weight - base,
                                     world->config.max_abs_weight - base);
                delta[edge] = (float)value;
                plastic_magnitude += fabs(value - old_value);
            }
        }
    }
    memcpy(state, world->scratch_hidden, hidden * sizeof(*state));
    ++world->organisms[index].executions;
    ++world->total_executions;
    if (asleep && plastic_magnitude >= ALIFE_SIGNIFICANT_PLASTICITY) {
        uint64_t *changes =
            &world->organisms[index].significant_weight_changes;
        if (*changes < UINT64_MAX) {
            ++(*changes);
        }
        if (*changes != 0U && ((*changes & (*changes - 1U)) == 0U)) {
            alife_log_plasticity(world, &world->organisms[index],
                                 plastic_magnitude);
        }
    }
}

static void deliver_communication(AlifeWorld *world) {
    double sums[ALIFE_MAX_COMMUNICATION] = {0.0};
    size_t communication = (size_t)world->config.communication_size;
    size_t senders = 0U;
    size_t i;
    size_t j;

    for (i = 0U; i < world->count; ++i) {
        const float *slot;
        if (!world->organisms[i].sent_message_this_tick) {
            continue;
        }
        slot = alife_slot_const(world, i);
        ++senders;
        for (j = 0U; j < communication; ++j) {
            sums[j] += (double)slot[world->layout.outbox + j];
        }
    }
    for (i = 0U; i < world->count; ++i) {
        float *slot = alife_slot(world, i);
        for (j = 0U; j < communication; ++j) {
            size_t peers = senders -
                (world->organisms[i].sent_message_this_tick ? 1U : 0U);
            if (world->organisms[i].state != ALIFE_STATE_AWAKE || peers == 0U) {
                slot[world->layout.inbox + j] = 0.0F;
            } else {
                slot[world->layout.inbox + j] = (float)(
                    (sums[j] - (world->organisms[i].sent_message_this_tick ?
                     (double)slot[world->layout.outbox + j] : 0.0)) /
                    (double)peers);
            }
        }
    }
}

static void process_courtships(AlifeWorld *world) {
    size_t i = 0U;
    char ignored[128];

    while (i < world->count) {
        AlifeOrganism *first = &world->organisms[i];
        AlifeOrganism *second;
        uint64_t first_id;
        uint64_t second_id;

        if (first->courtship_partner_id == 0U ||
            first->id > first->courtship_partner_id) {
            ++i;
            continue;
        }
        first_id = first->id;
        second_id = first->courtship_partner_id;
        second = alife_find_organism_mut(world, second_id);
        if (second == NULL || second->courtship_partner_id != first_id) {
            cancel_courtship(world, first_id, "ineligible");
            ++i;
            continue;
        }
        if (!alife_reproduction_eligible(world, first) ||
            !alife_reproduction_eligible(world, second)) {
            const char *reason =
                first->state == ALIFE_STATE_ASLEEP ||
                second->state == ALIFE_STATE_ASLEEP ? "sleep" :
                first->state == ALIFE_STATE_OFF ||
                second->state == ALIFE_STATE_OFF ? "off" :
                first->reproduction_output <= 0.0F ||
                first->acceptance_output <= 0.0F ||
                second->reproduction_output <= 0.0F ||
                second->acceptance_output <= 0.0F ?
                "consent_withdrawn" : "ineligible";
            cancel_courtship(world, first_id, reason);
            ++i;
            continue;
        }
        first->courtship_input = second->courtship_output;
        second->courtship_input = first->courtship_output;
        ++first->courtship_progress;
        second->courtship_progress = first->courtship_progress;
        if (first->courtship_progress >=
            world->config.courtship_duration_ticks) {
            uint64_t progress = first->courtship_progress;
            first->courtship_partner_id = 0U;
            first->courtship_progress = 0U;
            first->courtship_input = 0.0F;
            second->courtship_partner_id = 0U;
            second->courtship_progress = 0U;
            second->courtship_input = 0.0F;
            --world->active_courtships;
            ++world->courtships_completed;
            alife_log_courtship(world, "courtship_completed", first_id,
                                second_id, progress, "completed");
            if (complete_birth(world, first_id, second_id, ignored,
                               sizeof(ignored))) {
                ++world->courtship_births;
            }
        }
        ++i;
    }
}

static void process_reproduction(AlifeWorld *world) {
    size_t candidates = 0U;
    size_t i;
    char ignored[128];

    for (i = 0U; i < world->count; ++i) {
        AlifeOrganism *organism = &world->organisms[i];
        if (organism->courtship_partner_id == 0U &&
            alife_reproduction_eligible(world, organism) &&
            alife_rng_unit(&world->rng) <
                alife_reproduction_probability(world, organism)) {
            world->scratch_ids[candidates++] = organism->id;
        }
    }
    for (i = candidates; i > 1U; --i) {
        size_t other = (size_t)alife_rng_bounded(&world->rng, (uint64_t)i);
        uint64_t temporary = world->scratch_ids[i - 1U];
        world->scratch_ids[i - 1U] = world->scratch_ids[other];
        world->scratch_ids[other] = temporary;
    }
    for (i = 0U; i + 1U < candidates; i += 2U) {
        (void)alife_try_birth(world, world->scratch_ids[i],
                              world->scratch_ids[i + 1U], true,
                              ignored, sizeof(ignored));
    }
}

static uint64_t off_duration_from_output(const AlifeWorld *world, float output) {
    uint64_t minimum = world->config.off_min_duration_ticks;
    uint64_t span = world->config.off_max_duration_ticks - minimum;
    const uint64_t scale = (uint64_t)UINT32_MAX;
    double fraction = ((double)output + 1.0) * 0.5;
    uint64_t scaled;
    uint64_t quotient;
    uint64_t remainder;
    uint64_t mapped;

    fraction = clamp_double(fraction, 0.0, 1.0);
    scaled = (uint64_t)(fraction * (double)UINT32_MAX + 0.5);
    quotient = span / scale;
    remainder = span % scale;
    mapped = quotient * scaled +
             (remainder * scaled + scale / 2U) / scale;
    if (mapped > span) {
        mapped = span;
    }
    return minimum + mapped;
}

static void process_off_timers(AlifeWorld *world) {
    size_t i;
    char ignored[128];

    for (i = 0U; i < world->count; ++i) {
        if (world->organisms[i].state == ALIFE_STATE_OFF &&
            world->tick >= world->organisms[i].back_on_tick) {
            (void)transition_at(world, i, ALIFE_STATE_ASLEEP, 0U,
                                ignored, sizeof(ignored));
        }
    }
}

static void process_foolsday(AlifeWorld *world) {
    size_t i = 0U;

    if (world->month != 4 || world->day != 1) {
        return;
    }
    while (i < world->count) {
        AlifeOrganism *organism = &world->organisms[i];
        if (organism->state == ALIFE_STATE_AWAKE) {
            remove_at(world, i, ALIFE_DEATH_FOOLSDAY_AWAKE);
        } else if (organism->state == ALIFE_STATE_ASLEEP &&
                   organism->last_foolsday_roll_year != world->year) {
            double roll = alife_rng_unit(&world->rng);
            bool survived = roll >=
                world->config.foolsday_sleep_death_probability;
            organism->last_foolsday_roll_year = world->year;
            alife_log_foolsday_sleep_roll(world, organism, roll, survived);
            if (survived) {
                ++i;
            } else {
                remove_at(world, i, ALIFE_DEATH_FOOLSDAY_SLEEP);
            }
        } else {
            ++i;
        }
    }
}

static bool execute_lifecycle(AlifeWorld *world, size_t index) {
    AlifeOrganism *organism = &world->organisms[index];
    char ignored[128];

    organism->sent_message_this_tick = false;
    if (organism->state == ALIFE_STATE_OFF) {
        return true;
    }
    if (organism->state == ALIFE_STATE_AWAKE) {
        execute_organism(world, index, false);
        organism->sent_message_this_tick = true;
        if ((double)organism->sleep_output >
            world->config.state_transition_threshold) {
            (void)transition_at(world, index, ALIFE_STATE_ASLEEP, 0U,
                                ignored, sizeof(ignored));
        }
        return true;
    }

    clear_public_outputs(world, index);
    execute_organism(world, index, true);
    clear_public_outputs(world, index);
    if ((double)organism->off_output > world->config.state_transition_threshold &&
        organism->off_output >= organism->wake_output) {
        uint64_t duration = off_duration_from_output(
            world, organism->off_duration_output);
        (void)transition_at(world, index, ALIFE_STATE_OFF, duration,
                            ignored, sizeof(ignored));
    } else if ((double)organism->wake_output >
               world->config.state_transition_threshold) {
        (void)transition_at(world, index, ALIFE_STATE_AWAKE, 0U,
                            ignored, sizeof(ignored));
        if (world->month == 4 && world->day == 1) {
            remove_at(world, index, ALIFE_DEATH_FOOLSDAY_AWAKE);
            return false;
        }
    }
    return true;
}

static bool periodic_work(AlifeWorld *world, char *error, size_t error_size) {
    if (world->config.summary_interval > 0U &&
        world->tick % world->config.summary_interval == 0U) {
        alife_log_summary(world);
    }
    if (world->config.checkpoint_interval > 0U &&
        world->tick % world->config.checkpoint_interval == 0U &&
        world->config.checkpoint_path[0] != '\0') {
        if (!alife_world_save(world, world->config.checkpoint_path,
                              error, error_size)) {
            return false;
        }
    }
    return true;
}

static bool off_metadata_valid(const AlifeWorld *world, size_t index) {
    const AlifeOrganism *organism = &world->organisms[index];

    return organism->id != 0U && organism->state == ALIFE_STATE_OFF &&
           organism->back_on_tick > world->tick;
}

static uint64_t biological_increment(const AlifeWorld *world,
                                     AlifeLifecycleState state) {
    double rate = state == ALIFE_STATE_AWAKE ? world->config.awake_age_rate :
                  state == ALIFE_STATE_ASLEEP ? world->config.sleep_age_rate :
                  world->config.off_age_rate;

    return (uint64_t)(rate * (double)ALIFE_BIOLOGICAL_AGE_SCALE + 0.5);
}

static void process_dormancy_timeouts(AlifeWorld *world) {
    uint64_t limit = world->config.max_without_awake_days *
                     world->config.ticks_per_day;
    size_t i = 0U;

    while (i < world->count) {
        AlifeOrganism *organism = &world->organisms[i];
        if (organism->state != ALIFE_STATE_AWAKE &&
            world->tick > organism->last_awake_tick &&
            world->tick - organism->last_awake_tick > limit) {
            remove_at(world, i, ALIFE_DEATH_DORMANCY_TIMEOUT);
        } else {
            ++i;
        }
    }
}

bool alife_world_step(AlifeWorld *world, char *error, size_t error_size) {
    size_t i;
    uint64_t first_new_id;

    if (world == NULL || !world->initialized) {
        alife_set_error(error, error_size, "The world is not initialized.");
        return false;
    }
    if (world->stopped) {
        return true;
    }
    if (world->tick == UINT64_MAX) {
        alife_set_error(error, error_size, "The simulation tick counter is exhausted.");
        return false;
    }
    if (world->tick > 0U &&
        world->tick % world->config.ticks_per_day == 0U) {
        if (world->year == 9999 && world->month == 12 && world->day == 31) {
            alife_set_error(error, error_size,
                            "The simulated calendar exceeded year 9999.");
            return false;
        }
        advance_date(world);
    }
    process_off_timers(world);
    process_foolsday(world);
    process_dormancy_timeouts(world);

    i = 0U;
    while (i < world->count) {
        bool state_valid = world->organisms[i].state == ALIFE_STATE_OFF ?
            off_metadata_valid(world, i) : alife_organism_state_valid(world, i);

        if (!state_valid) {
            remove_at(world, i, ALIFE_DEATH_INVALID_STATE);
        } else {
            bool remains = execute_lifecycle(world, i);
            if (!remains) {
                continue;
            }
            state_valid = world->organisms[i].state == ALIFE_STATE_OFF ?
                off_metadata_valid(world, i) :
                alife_organism_state_valid(world, i);
            if (!state_valid) {
                remove_at(world, i, ALIFE_DEATH_INVALID_STATE);
            } else {
                ++i;
            }
        }
    }
    deliver_communication(world);
    process_courtships(world);
    first_new_id = world->next_id;
    process_reproduction(world);
    alife_enforce_capacity(world);
    for (i = 0U; i < world->count; ++i) {
        if (world->organisms[i].id < first_new_id) {
            AlifeRewardContext reward_context;
            reward_context.tick = world->tick;
            uint64_t increment = biological_increment(
                world, world->organisms[i].state);
            reward_context.age = world->organisms[i].biological_age;
            reward_context.biological_age_increment = increment;
            reward_context.accumulated_reward = world->organisms[i].reward;
            reward_context.survived_tick = true;
            world->organisms[i].reward +=
                alife_reward_calculate(&reward_context);
            ++world->organisms[i].chronological_age;
            world->organisms[i].biological_age += increment;
            if (world->organisms[i].state == ALIFE_STATE_AWAKE) {
                world->organisms[i].last_awake_tick = world->tick;
            }
        }
    }
    ++world->tick;
    return periodic_work(world, error, error_size);
}

bool alife_world_run(AlifeWorld *world, char *error, size_t error_size) {
    while (world->tick < world->config.tick_count && !world->stopped) {
        if (!alife_world_step(world, error, error_size)) {
            return false;
        }
    }
    if (world->config.summary_interval > 0U &&
        world->tick % world->config.summary_interval != 0U) {
        alife_log_summary(world);
    }
    return true;
}
