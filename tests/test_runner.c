#include "alife/simulation.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Keep the test runner dependency-free for minimal C11 environments. */

#define TEST_ERROR_SIZE 256U
#define TEST_EVENT_PATH "build/test-events.jsonl"
#define TEST_CHECKPOINT_PATH "build/test-checkpoint.chk"

enum test_result {
    TEST_PASS,
    TEST_FAIL
};

typedef enum test_result (*test_function)(void);

struct test_case {
    const char *name;
    test_function run;
};

static size_t assertion_count;

#define EXPECT_TRUE(expression)                                                 \
    do {                                                                        \
        ++assertion_count;                                                      \
        if (!(expression)) {                                                    \
            (void)fprintf(stderr, "%s:%d: expectation failed: %s\n",          \
                          __FILE__, __LINE__, #expression);                     \
            test_result_value = TEST_FAIL;                                      \
            goto cleanup;                                                       \
        }                                                                       \
    } while (false)

#define EXPECT_CALL(expression, error_buffer)                                  \
    do {                                                                        \
        ++assertion_count;                                                      \
        if (!(expression)) {                                                    \
            (void)fprintf(stderr, "%s:%d: call failed: %s: %s\n",             \
                          __FILE__, __LINE__, #expression, (error_buffer));      \
            test_result_value = TEST_FAIL;                                      \
            goto cleanup;                                                       \
        }                                                                       \
    } while (false)

static void prepare_config(AlifeConfig *config)
{
    alife_config_defaults(config);
    config->seed = UINT64_C(0x4a17c0de);
    config->tick_count = UINT64_C(100);
    config->summary_interval = UINT64_C(1000000);
    config->checkpoint_interval = UINT64_C(1000000);
    config->logging_level = ALIFE_LOG_ERROR;
    (void)snprintf(config->event_log_path, sizeof(config->event_log_path),
                   "%s", TEST_EVENT_PATH);
    (void)snprintf(config->checkpoint_path, sizeof(config->checkpoint_path),
                   "%s", TEST_CHECKPOINT_PATH);
}

static void make_reproduction_request(AlifeWorld *world, const uint64_t id)
{
    AlifeOrganism *organism = alife_find_organism_mut(world, id);

    if (organism != NULL) {
        organism->reproduction_output = 1.0F;
        organism->acceptance_output = 1.0F;
    }
}

static void force_reproduction_consent(AlifeWorld *world, const uint64_t id)
{
    float *genome = alife_organism_genome_mut(world, id);
    size_t hidden = (size_t)world->config.hidden_size;
    size_t communication = (size_t)world->config.communication_size;
    size_t output;
    size_t hidden_index;

    if (genome == NULL) {
        return;
    }
    for (output = communication + ALIFE_OUTPUT_REPRODUCTION;
         output <= communication + ALIFE_OUTPUT_ACCEPTANCE; ++output) {
        genome[world->layout.output_biases + output] = 4.0F;
        for (hidden_index = 0U; hidden_index < hidden; ++hidden_index) {
            genome[world->layout.output_weights + output * hidden +
                   hidden_index] = 0.0F;
        }
    }
    make_reproduction_request(world, id);
}

static void set_controls(AlifeWorld *world, const uint64_t id,
                         const float sleep_value, const float wake_value,
                         const float off_value, const float duration_value)
{
    float *genome = alife_organism_genome_mut(world, id);
    const size_t communication = (size_t)world->config.communication_size;
    const size_t hidden = (size_t)world->config.hidden_size;
    const float values[ALIFE_PRIVATE_CONTROL_OUTPUTS] = {
        sleep_value, wake_value, off_value, duration_value
    };
    size_t control;
    size_t hidden_index;

    if (genome == NULL) {
        return;
    }
    for (control = 0U; control < ALIFE_PRIVATE_CONTROL_OUTPUTS; ++control) {
        const size_t output = communication + ALIFE_OUTPUT_SLEEP + control;

        genome[world->layout.output_biases + output] = values[control];
        for (hidden_index = 0U; hidden_index < hidden; ++hidden_index) {
            genome[world->layout.output_weights + output * hidden +
                   hidden_index] = 0.0F;
        }
    }
}

static double plastic_state_sum(const AlifeWorld *world, const uint64_t id)
{
    const float *slot = alife_organism_genome(world, id);
    size_t edge;
    double total = 0.0;

    if (slot == NULL) {
        return 0.0;
    }
    for (edge = 0U; edge < world->layout.recurrent_count; ++edge) {
        total += fabs((double)slot[world->layout.plastic_state + edge]);
    }
    return total;
}

static void prepare_plastic_activity(AlifeWorld *world, const uint64_t id)
{
    float *slot = alife_organism_genome_mut(world, id);
    size_t hidden_index;

    if (slot == NULL) {
        return;
    }
    for (hidden_index = 0U;
         hidden_index < (size_t)world->config.hidden_size; ++hidden_index) {
        slot[world->layout.hidden_biases + hidden_index] = 1.0F;
        slot[world->layout.plastic_rates + hidden_index] = 1.0F;
        slot[world->layout.hidden_state + hidden_index] = 0.5F;
    }
}

static enum test_result test_immature_organisms_cannot_reproduce(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t parent_a;
    uint64_t parent_b;

    prepare_config(&config);
    config.maturity_age = UINT64_C(50);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    EXPECT_TRUE(world.count == 2U);
    parent_a = world.organisms[0].id;
    parent_b = world.organisms[1].id;
    make_reproduction_request(&world, parent_a);
    make_reproduction_request(&world, parent_b);

    EXPECT_TRUE(!alife_reproduction_eligible(
        &world, alife_find_organism(&world, parent_a)));
    EXPECT_TRUE(!alife_try_birth(&world, parent_a, parent_b, true, error,
                                 sizeof(error)));
    EXPECT_TRUE(world.count == 2U);
    EXPECT_TRUE(world.total_births == 2U);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_example_configurations_are_valid(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig small;
    AlifeConfig long_run;
    AlifeConfig invalid;
    char error[TEST_ERROR_SIZE] = {0};

    EXPECT_CALL(alife_config_load(&small, "configs/small.conf", error,
                                  sizeof(error)),
                error);
    EXPECT_CALL(alife_config_load(&long_run, "configs/long-24h.conf", error,
                                  sizeof(error)),
                error);
    EXPECT_TRUE(small.initial_population == 2U);
    EXPECT_TRUE(long_run.initial_population == 2U);
    EXPECT_TRUE(long_run.tick_count / long_run.ticks_per_day ==
                UINT64_C(250));
    invalid = small;
    invalid.foolsday_sleep_death_probability = 1.01;
    EXPECT_TRUE(!alife_config_validate(&invalid, error, sizeof(error)));
    invalid = small;
    invalid.off_min_duration_ticks = invalid.off_max_duration_ticks + 1U;
    EXPECT_TRUE(!alife_config_validate(&invalid, error, sizeof(error)));
    invalid = small;
    invalid.state_transition_threshold = 1.0;
    EXPECT_TRUE(!alife_config_validate(&invalid, error, sizeof(error)));

cleanup:
    return test_result_value;
}

static enum test_result test_reproduction_requires_distinct_parents(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t parent_id;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    parent_id = world.organisms[0].id;
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    make_reproduction_request(&world, parent_id);

    EXPECT_TRUE(!alife_try_birth(&world, parent_id, parent_id, true, error,
                                 sizeof(error)));
    EXPECT_TRUE(world.count == 2U);
    EXPECT_TRUE(world.next_id == UINT64_C(3));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_single_parent_cannot_create_offspring(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t parent_id;
    uint64_t missing_id;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    parent_id = world.organisms[0].id;
    missing_id = world.next_id + UINT64_C(1000);
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    make_reproduction_request(&world, parent_id);

    EXPECT_TRUE(!alife_try_birth(&world, parent_id, missing_id, true, error,
                                 sizeof(error)));
    EXPECT_TRUE(world.count == 2U);
    EXPECT_TRUE(world.total_births == 2U);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_offspring_respects_neural_size_limits(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeConfig oversized;
    AlifeLayout rejected_layout;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t parent_a;
    uint64_t parent_b;
    uint64_t child_id;
    const float *child_genome;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    EXPECT_TRUE(world.layout.genome_count >= ALIFE_MIN_GENOME_PARAMETERS);
    EXPECT_TRUE(world.layout.genome_count <= ALIFE_MAX_GENOME_PARAMETERS);

    parent_a = world.organisms[0].id;
    parent_b = world.organisms[1].id;
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    world.organisms[1].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    config.courtship_duration_ticks = 1U;
    world.config.courtship_duration_ticks = 1U;
    force_reproduction_consent(&world, parent_a);
    force_reproduction_consent(&world, parent_b);
    set_controls(&world, parent_a, -4.0F, -4.0F, -4.0F, -1.0F);
    set_controls(&world, parent_b, -4.0F, -4.0F, -4.0F, -1.0F);
    child_id = world.next_id;
    EXPECT_CALL(alife_try_birth(&world, parent_a, parent_b, true, error,
                sizeof(error)),
                error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(world.count == 3U);
    child_genome = alife_organism_genome(&world, child_id);
    EXPECT_TRUE(child_genome != NULL);
    EXPECT_TRUE(alife_genome_validate(&world, child_genome, error,
                                      sizeof(error)));

    oversized = config;
    oversized.hidden_size = 64U;
    EXPECT_TRUE(!alife_layout_create(&oversized, &rejected_layout, error,
                                     sizeof(error)));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_capacity_removes_oldest_organism(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t oldest_id;
    uint64_t younger_id;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    oldest_id = world.organisms[0].id;
    younger_id = world.organisms[1].id;
    EXPECT_CALL(alife_transition_request(&world, oldest_id, ALIFE_STATE_ASLEEP,
                                         0U, error, sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&world, oldest_id, ALIFE_STATE_OFF,
                                         config.off_min_duration_ticks, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&world, younger_id, ALIFE_STATE_ASLEEP,
                                         0U, error, sizeof(error)), error);
    world.organisms[0].age = UINT64_C(100);
    world.organisms[1].age = UINT64_C(10);
    world.config.capacity_bytes = (uint64_t)alife_organism_size(&world);

    alife_enforce_capacity(&world);
    EXPECT_TRUE(world.count == 1U);
    EXPECT_TRUE(alife_find_organism(&world, oldest_id) == NULL);
    EXPECT_TRUE(alife_find_organism(&world, younger_id) != NULL);
    EXPECT_TRUE(alife_find_organism(&world, younger_id)->state ==
                ALIFE_STATE_ASLEEP);
    EXPECT_TRUE(world.population_bytes <= world.config.capacity_bytes);
    EXPECT_TRUE(world.total_deaths == UINT64_C(1));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_initial_and_offspring_states_are_awake(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t child_id;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    EXPECT_TRUE(world.organisms[0].state == ALIFE_STATE_AWAKE);
    EXPECT_TRUE(world.organisms[1].state == ALIFE_STATE_AWAKE);
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    world.organisms[1].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    world.config.courtship_duration_ticks = 1U;
    force_reproduction_consent(&world, world.organisms[0].id);
    force_reproduction_consent(&world, world.organisms[1].id);
    set_controls(&world, world.organisms[0].id,
                 -4.0F, -4.0F, -4.0F, -1.0F);
    set_controls(&world, world.organisms[1].id,
                 -4.0F, -4.0F, -4.0F, -1.0F);
    child_id = world.next_id;
    EXPECT_CALL(alife_try_birth(&world, world.organisms[0].id,
                                world.organisms[1].id, true, error,
                                sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, child_id)->state == ALIFE_STATE_AWAKE);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_seeded_neural_sleep_rhythm(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t ids[2];
    uint64_t first_sleep[2] = {UINT64_MAX, UINT64_MAX};
    uint64_t first_wake[2] = {UINT64_MAX, UINT64_MAX};
    uint64_t awake_samples = 0U;
    uint64_t asleep_samples = 0U;
    size_t step;
    size_t organism_index;

    prepare_config(&config);
    config.tick_count = UINT64_C(300);
    config.maturity_age = UINT64_C(1000000);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    ids[0] = world.organisms[0].id;
    ids[1] = world.organisms[1].id;

    for (step = 0U; step < 240U; ++step) {
        EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
        for (organism_index = 0U; organism_index < 2U; ++organism_index) {
            const AlifeOrganism *organism =
                alife_find_organism(&world, ids[organism_index]);

            EXPECT_TRUE(organism != NULL);
            if (organism->state == ALIFE_STATE_ASLEEP &&
                first_sleep[organism_index] == UINT64_MAX) {
                first_sleep[organism_index] = world.tick;
            }
            if (first_sleep[organism_index] != UINT64_MAX &&
                organism->state == ALIFE_STATE_AWAKE &&
                first_wake[organism_index] == UINT64_MAX) {
                first_wake[organism_index] = world.tick;
            }
        }
        if (alife_find_organism(&world, ids[0])->state == ALIFE_STATE_AWAKE) {
            ++awake_samples;
        } else if (alife_find_organism(&world, ids[0])->state ==
                   ALIFE_STATE_ASLEEP) {
            ++asleep_samples;
        }
    }
    EXPECT_TRUE(first_sleep[0] != UINT64_MAX);
    EXPECT_TRUE(first_sleep[1] != UINT64_MAX);
    EXPECT_TRUE(first_wake[0] != UINT64_MAX);
    EXPECT_TRUE(first_wake[1] != UINT64_MAX);
    EXPECT_TRUE(awake_samples >= UINT64_C(60));
    EXPECT_TRUE(asleep_samples >= UINT64_C(60));
    EXPECT_TRUE(first_sleep[0] != first_sleep[1] ||
                first_wake[0] != first_wake[1]);
    EXPECT_TRUE(world.awake_to_asleep_transitions > 0U);
    EXPECT_TRUE(world.asleep_to_awake_transitions > 0U);
    EXPECT_TRUE(world.asleep_to_off_transitions == 0U);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_seeded_rhythm_is_heritable_genome_data(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    const float *first;
    const float *second;
    size_t hidden;
    size_t communication;
    size_t sleep_weight;
    size_t wake_weight;
    size_t parameter;
    bool genomes_differ = false;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    first = alife_organism_genome(&world, world.organisms[0].id);
    second = alife_organism_genome(&world, world.organisms[1].id);
    hidden = (size_t)config.hidden_size;
    communication = (size_t)config.communication_size;
    sleep_weight = world.layout.output_weights +
        (communication + ALIFE_OUTPUT_SLEEP) * hidden;
    wake_weight = world.layout.output_weights +
        (communication + ALIFE_OUTPUT_WAKE) * hidden;
    EXPECT_TRUE(first != NULL);
    EXPECT_TRUE(second != NULL);
    EXPECT_TRUE(world.layout.recurrent_weights + hidden + 1U <
                world.layout.genome_count);
    EXPECT_TRUE(sleep_weight < world.layout.genome_count);
    EXPECT_TRUE(wake_weight < world.layout.genome_count);
    EXPECT_TRUE(first[world.layout.recurrent_weights] > 0.0F);
    EXPECT_TRUE(first[world.layout.recurrent_weights + 1U] < 0.0F);
    EXPECT_TRUE(first[world.layout.recurrent_weights + hidden] > 0.0F);
    EXPECT_TRUE(first[sleep_weight] > 0.0F);
    EXPECT_TRUE(first[wake_weight] < 0.0F);
    for (parameter = 0U; parameter < world.layout.genome_count; ++parameter) {
        if (first[parameter] != second[parameter]) {
            genomes_differ = true;
            break;
        }
    }
    EXPECT_TRUE(genomes_differ);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_no_substrate_sleep_timer(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t id;
    size_t step;

    prepare_config(&config);
    config.tick_count = UINT64_C(200);
    config.maturity_age = UINT64_C(1000000);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    id = world.organisms[0].id;
    set_controls(&world, id, -4.0F, -4.0F, -4.0F, -1.0F);
    for (step = 0U; step < 160U; ++step) {
        EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
        EXPECT_TRUE(alife_find_organism(&world, id)->state == ALIFE_STATE_AWAKE);
        EXPECT_TRUE(alife_find_organism(&world, id)->back_on_tick == 0U);
    }
    EXPECT_TRUE(alife_find_organism(&world, id)->executions == UINT64_C(160));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_sleep_rhythm_is_reachable_across_seeds(void)
{
    enum test_result test_result_value = TEST_PASS;
    static const uint64_t seeds[] = {
        UINT64_C(1), UINT64_C(2), UINT64_C(3), UINT64_C(4), UINT64_C(5)
    };
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    size_t seed_index;

    for (seed_index = 0U;
         seed_index < sizeof(seeds) / sizeof(seeds[0]); ++seed_index) {
        AlifeConfig config;
        size_t step;

        prepare_config(&config);
        config.seed = seeds[seed_index];
        config.tick_count = UINT64_C(160);
        config.maturity_age = UINT64_C(1000000);
        EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)),
                    error);
        for (step = 0U; step < 140U; ++step) {
            EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
        }
        EXPECT_TRUE(world.awake_to_asleep_transitions > 0U);
        EXPECT_TRUE(world.asleep_to_awake_transitions > 0U);
        alife_world_destroy(&world);
    }

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_transition_graph_and_control_outputs(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t id;

    prepare_config(&config);
    config.off_min_duration_ticks = UINT64_C(3);
    config.off_max_duration_ticks = UINT64_C(7);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    id = world.organisms[0].id;
    EXPECT_TRUE(!alife_transition_request(&world, id, ALIFE_STATE_OFF, 3U,
                                          error, sizeof(error)));
    set_controls(&world, id, 4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, id)->state == ALIFE_STATE_ASLEEP);
    EXPECT_TRUE(!alife_transition_request(&world, id, ALIFE_STATE_ASLEEP, 0U,
                                          error, sizeof(error)));
    set_controls(&world, id, -4.0F, 4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, id)->state == ALIFE_STATE_AWAKE);
    EXPECT_CALL(alife_transition_request(&world, id, ALIFE_STATE_ASLEEP, 0U,
                                         error, sizeof(error)), error);
    set_controls(&world, id, -4.0F, -4.0F, 4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, id)->state == ALIFE_STATE_OFF);
    EXPECT_TRUE(alife_find_organism(&world, id)->back_on_tick == world.tick - 1U +
                config.off_min_duration_ticks);
    EXPECT_TRUE(!alife_transition_request(&world, id, ALIFE_STATE_AWAKE, 0U,
                                          error, sizeof(error)));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_sleep_and_off_disable_external_behavior(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t inactive_id;
    uint64_t awake_id;
    uint64_t executions;
    float *inactive_slot;
    const float *awake_slot;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    inactive_id = world.organisms[0].id;
    awake_id = world.organisms[1].id;
    EXPECT_CALL(alife_transition_request(&world, inactive_id, ALIFE_STATE_ASLEEP,
                                         0U, error, sizeof(error)), error);
    set_controls(&world, inactive_id, -4.0F, -4.0F, -4.0F, -1.0F);
    inactive_slot = alife_organism_genome_mut(&world, inactive_id);
    inactive_slot[world.layout.outbox] = 1.0F;
    world.organisms[0].sent_message_this_tick = true;
    make_reproduction_request(&world, inactive_id);
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    EXPECT_TRUE(!alife_reproduction_eligible(&world, &world.organisms[0]));
    EXPECT_TRUE(!alife_try_birth(&world, inactive_id, awake_id, true, error,
                                 sizeof(error)));
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    awake_slot = alife_organism_genome(&world, awake_id);
    EXPECT_TRUE(awake_slot[world.layout.inbox] == 0.0F);
    EXPECT_TRUE(alife_find_organism(&world, inactive_id)->reproduction_output ==
                0.0F);

    EXPECT_CALL(alife_transition_request(&world, inactive_id, ALIFE_STATE_OFF,
                                         config.off_min_duration_ticks, error,
                                         sizeof(error)), error);
    executions = alife_find_organism(&world, inactive_id)->executions;
    inactive_slot = alife_organism_genome_mut(&world, inactive_id);
    inactive_slot[world.layout.outbox] = 1.0F;
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, inactive_id)->executions == executions);
    EXPECT_TRUE(!alife_reproduction_eligible(
        &world, alife_find_organism(&world, inactive_id)));
    awake_slot = alife_organism_genome(&world, awake_id);
    EXPECT_TRUE(awake_slot[world.layout.inbox] == 0.0F);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_plasticity_occurs_only_during_sleep(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t id;
    double before;
    double asleep_value;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    id = world.organisms[0].id;
    set_controls(&world, id, -4.0F, -4.0F, -4.0F, -1.0F);
    prepare_plastic_activity(&world, id);
    before = plastic_state_sum(&world, id);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(plastic_state_sum(&world, id) == before);
    EXPECT_CALL(alife_transition_request(&world, id, ALIFE_STATE_ASLEEP, 0U,
                                         error, sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    asleep_value = plastic_state_sum(&world, id);
    EXPECT_TRUE(asleep_value > before);
    EXPECT_CALL(alife_transition_request(&world, id, ALIFE_STATE_OFF,
                                         config.off_min_duration_ticks, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(plastic_state_sum(&world, id) == asleep_value);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_off_timer_returns_to_sleep(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t id;
    uint64_t execution_count;

    prepare_config(&config);
    config.off_min_duration_ticks = UINT64_C(2);
    config.off_max_duration_ticks = UINT64_C(2);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    id = world.organisms[0].id;
    EXPECT_CALL(alife_transition_request(&world, id, ALIFE_STATE_ASLEEP, 0U,
                                         error, sizeof(error)), error);
    set_controls(&world, id, -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_transition_request(&world, id, ALIFE_STATE_OFF, 2U,
                                         error, sizeof(error)), error);
    execution_count = alife_find_organism(&world, id)->executions;
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, id)->state == ALIFE_STATE_OFF);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, id)->state == ALIFE_STATE_ASLEEP);
    EXPECT_TRUE(alife_find_organism(&world, id)->executions == execution_count + 1U);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_foolsday_state_rules(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t awake_id;
    uint64_t asleep_id;

    prepare_config(&config);
    config.calendar_start_month = 4U;
    config.calendar_start_day = 1U;
    config.ticks_per_day = UINT64_C(10);
    config.foolsday_sleep_death_probability = 0.0;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    awake_id = world.organisms[0].id;
    asleep_id = world.organisms[1].id;
    EXPECT_CALL(alife_transition_request(&world, asleep_id, ALIFE_STATE_ASLEEP,
                                         0U, error, sizeof(error)), error);
    set_controls(&world, asleep_id, -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, awake_id) == NULL);
    EXPECT_TRUE(alife_find_organism(&world, asleep_id) != NULL);
    EXPECT_TRUE(alife_find_organism(&world, asleep_id)->executions == 1U);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_foolsday_sleep_probability_and_once_only(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig survive_config;
    AlifeConfig die_config;
    AlifeWorld survive = {0};
    AlifeWorld die = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t survivor_id;
    uint64_t rng_after_roll[4];

    prepare_config(&survive_config);
    survive_config.calendar_start_month = 4U;
    survive_config.calendar_start_day = 1U;
    survive_config.ticks_per_day = UINT64_C(10);
    survive_config.foolsday_sleep_death_probability = 0.0;
    die_config = survive_config;
    die_config.foolsday_sleep_death_probability = 1.0;
    EXPECT_CALL(alife_world_init(&survive, &survive_config, error,
                                 sizeof(error)), error);
    survivor_id = survive.organisms[0].id;
    EXPECT_CALL(alife_transition_request(&survive, survivor_id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&survive, survive.organisms[1].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    set_controls(&survive, survivor_id, -4.0F, -4.0F, -4.0F, -1.0F);
    set_controls(&survive, survive.organisms[1].id,
                 -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&survive, error, sizeof(error)), error);
    (void)memcpy(rng_after_roll, survive.rng.state, sizeof(rng_after_roll));
    EXPECT_CALL(alife_world_step(&survive, error, sizeof(error)), error);
    EXPECT_TRUE(memcmp(rng_after_roll, survive.rng.state,
                       sizeof(rng_after_roll)) == 0);
    EXPECT_TRUE(survive.count == 2U);

    EXPECT_CALL(alife_world_init(&die, &die_config, error, sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&die, die.organisms[0].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&die, die.organisms[1].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&die, error, sizeof(error)), error);
    EXPECT_TRUE(die.count == 0U);

cleanup:
    alife_world_destroy(&survive);
    alife_world_destroy(&die);
    return test_result_value;
}

static enum test_result test_foolsday_wake_dies_before_awake_execution(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t id;

    prepare_config(&config);
    config.calendar_start_month = 4U;
    config.calendar_start_day = 1U;
    config.ticks_per_day = UINT64_C(10);
    config.foolsday_sleep_death_probability = 0.0;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    id = world.organisms[0].id;
    EXPECT_CALL(alife_transition_request(&world, id, ALIFE_STATE_ASLEEP, 0U,
                                         error, sizeof(error)), error);
    set_controls(&world, id, -4.0F, 4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_transition_request(&world, world.organisms[1].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    set_controls(&world, world.organisms[1].id,
                 -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, id) == NULL);
    EXPECT_TRUE(world.total_executions == UINT64_C(2));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_age_and_reward_advance_in_all_states(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t awake_id;
    uint64_t inactive_id;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    awake_id = world.organisms[0].id;
    inactive_id = world.organisms[1].id;
    set_controls(&world, awake_id, -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_transition_request(&world, inactive_id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    set_controls(&world, inactive_id, -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, awake_id)->age == UINT64_C(1));
    EXPECT_TRUE(alife_find_organism(&world, inactive_id)->age == UINT64_C(1));
    EXPECT_CALL(alife_transition_request(&world, inactive_id, ALIFE_STATE_OFF,
                                         config.off_min_duration_ticks, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, inactive_id)->age == UINT64_C(2));
    EXPECT_TRUE(alife_find_organism(&world, inactive_id)->reward ==
                UINT64_C(620000));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_foolsday_off_survival_and_expiry(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t off_id;

    prepare_config(&config);
    config.calendar_start_month = 4U;
    config.calendar_start_day = 1U;
    config.ticks_per_day = UINT64_C(10);
    config.off_min_duration_ticks = UINT64_C(1);
    config.off_max_duration_ticks = UINT64_C(1);
    config.foolsday_sleep_death_probability = 0.0;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    off_id = world.organisms[0].id;
    EXPECT_CALL(alife_transition_request(&world, off_id, ALIFE_STATE_ASLEEP,
                                         0U, error, sizeof(error)), error);
    set_controls(&world, off_id, -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_transition_request(&world, off_id, ALIFE_STATE_OFF, 1U,
                                         error, sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, off_id)->state == ALIFE_STATE_OFF);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, off_id)->state == ALIFE_STATE_ASLEEP);
    EXPECT_TRUE(alife_find_organism(&world, off_id)->last_foolsday_roll_year ==
                world.year);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_pair_requires_consent_and_opportunity(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t parent_a;
    uint64_t parent_b;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    parent_a = world.organisms[0].id;
    parent_b = world.organisms[1].id;
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    world.organisms[1].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    make_reproduction_request(&world, parent_a);
    world.organisms[1].reproduction_output = 1.0F;
    world.organisms[1].acceptance_output = 0.0F;

    EXPECT_TRUE(!alife_try_birth(&world, parent_a, parent_b, true, error,
                                 sizeof(error)));
    EXPECT_TRUE(world.count == 2U);
    make_reproduction_request(&world, parent_b);
    EXPECT_TRUE(!alife_try_birth(&world, parent_a, parent_b, false, error,
                                 sizeof(error)));
    EXPECT_TRUE(world.count == 2U);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_courtship_is_exclusive_and_sustained(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t first;
    uint64_t second;
    size_t step;

    prepare_config(&config);
    config.courtship_duration_ticks = 3U;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    first = world.organisms[0].id;
    second = world.organisms[1].id;
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    world.organisms[1].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    force_reproduction_consent(&world, first);
    force_reproduction_consent(&world, second);
    set_controls(&world, first, -4.0F, -4.0F, -4.0F, -1.0F);
    set_controls(&world, second, -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_try_birth(&world, first, second, true, error,
                                sizeof(error)), error);
    EXPECT_TRUE(world.count == 2U);
    EXPECT_TRUE(alife_find_organism(&world, first)->courtship_partner_id ==
                second);
    EXPECT_TRUE(alife_find_organism(&world, second)->courtship_partner_id ==
                first);
    EXPECT_TRUE(!alife_try_birth(&world, first, second, true, error,
                                 sizeof(error)));
    for (step = 0U; step < 2U; ++step) {
        EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
        EXPECT_TRUE(world.count == 2U);
    }
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(world.count == 3U);
    EXPECT_TRUE(world.courtships_completed == 1U);
    EXPECT_TRUE(world.courtship_births == 1U);
    EXPECT_TRUE(alife_find_organism(&world, first)->courtship_partner_id == 0U);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_biological_rates_and_dormancy_timeout(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t sleeper;

    prepare_config(&config);
    config.ticks_per_day = 1U;
    config.max_without_awake_days = 1U;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    sleeper = world.organisms[0].id;
    EXPECT_CALL(alife_transition_request(&world, sleeper, ALIFE_STATE_ASLEEP,
                                         0U, error, sizeof(error)), error);
    set_controls(&world, sleeper, -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, sleeper)->chronological_age == 1U);
    EXPECT_TRUE(alife_find_organism(&world, sleeper)->biological_age ==
                UINT64_C(600000));
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(alife_find_organism(&world, sleeper) == NULL);

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_invalid_neural_values_are_rejected(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    float *genome;
    uint64_t invalid_id;
    uint64_t healthy_id;

    prepare_config(&config);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    invalid_id = world.organisms[0].id;
    healthy_id = world.organisms[1].id;
    genome = alife_organism_genome_mut(&world, invalid_id);
    EXPECT_TRUE(genome != NULL);
    EXPECT_TRUE(alife_genome_validate(&world, genome, error, sizeof(error)));

    genome[0] = NAN;
    EXPECT_TRUE(!alife_genome_validate(&world, genome, error, sizeof(error)));
    genome[0] = INFINITY;
    EXPECT_TRUE(!alife_genome_validate(&world, genome, error, sizeof(error)));
    genome[0] = NAN;
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(world.count == 1U);
    EXPECT_TRUE(alife_find_organism(&world, invalid_id) == NULL);
    EXPECT_TRUE(alife_find_organism(&world, healthy_id) != NULL);
    EXPECT_TRUE(world.total_deaths == UINT64_C(1));
    EXPECT_TRUE(world.total_executions == UINT64_C(1));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_identical_seeds_are_reproducible(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld first = {0};
    AlifeWorld second = {0};
    char error[TEST_ERROR_SIZE] = {0};
    size_t step;

    prepare_config(&config);
    config.maturity_age = UINT64_C(1000000);
    EXPECT_CALL(alife_world_init(&first, &config, error, sizeof(error)), error);
    EXPECT_CALL(alife_world_init(&second, &config, error, sizeof(error)), error);
    EXPECT_TRUE(alife_world_hash(&first) == alife_world_hash(&second));

    for (step = 0U; step < 90U; ++step) {
        EXPECT_CALL(alife_world_step(&first, error, sizeof(error)), error);
        EXPECT_CALL(alife_world_step(&second, error, sizeof(error)), error);
        EXPECT_TRUE(alife_world_hash(&first) == alife_world_hash(&second));
    }

cleanup:
    alife_world_destroy(&first);
    alife_world_destroy(&second);
    return test_result_value;
}

static enum test_result test_mutation_stays_within_bounds(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t parent_a;
    uint64_t parent_b;
    uint64_t child_id;
    float *first_genome;
    float *second_genome;
    const float *child_genome;
    size_t parameter;
    size_t oscillator_parameter;

    prepare_config(&config);
    config.mutation_probability = 1.0;
    config.mutation_magnitude = 0.025;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    parent_a = world.organisms[0].id;
    parent_b = world.organisms[1].id;
    first_genome = alife_organism_genome_mut(&world, parent_a);
    second_genome = alife_organism_genome_mut(&world, parent_b);
    EXPECT_TRUE(first_genome != NULL);
    EXPECT_TRUE(second_genome != NULL);
    (void)memcpy(second_genome, first_genome,
                 world.layout.genome_count * sizeof(*first_genome));
    world.organisms[0].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    world.organisms[1].biological_age =
        config.maturity_age * ALIFE_BIOLOGICAL_AGE_SCALE;
    world.config.courtship_duration_ticks = 1U;
    force_reproduction_consent(&world, parent_a);
    force_reproduction_consent(&world, parent_b);
    set_controls(&world, parent_a, -4.0F, -4.0F, -4.0F, -1.0F);
    set_controls(&world, parent_b, -4.0F, -4.0F, -4.0F, -1.0F);
    child_id = world.next_id;

    EXPECT_CALL(alife_try_birth(&world, parent_a, parent_b, true, error,
                                sizeof(error)),
                error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    child_genome = alife_organism_genome(&world, child_id);
    first_genome = alife_organism_genome_mut(&world, parent_a);
    EXPECT_TRUE(child_genome != NULL);
    EXPECT_TRUE(first_genome != NULL);
    oscillator_parameter = world.layout.recurrent_weights;
    EXPECT_TRUE(child_genome[oscillator_parameter] !=
                first_genome[oscillator_parameter]);
    for (parameter = 0U; parameter < world.layout.genome_count; ++parameter) {
        const double change = fabs((double)child_genome[parameter] -
                                   (double)first_genome[parameter]);

        EXPECT_TRUE(change <= config.mutation_magnitude + 0.000001);
    }
    EXPECT_TRUE(alife_genome_validate(&world, child_genome, error,
                                      sizeof(error)));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_checkpoint_resume_preserves_state(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld uninterrupted = {0};
    AlifeWorld split = {0};
    AlifeWorld resumed = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t checkpoint_hash;
    size_t step;

    prepare_config(&config);
    config.maturity_age = UINT64_C(1000000);
    (void)remove(TEST_CHECKPOINT_PATH);
    EXPECT_CALL(alife_world_init(&uninterrupted, &config, error,
                                 sizeof(error)),
                error);
    EXPECT_CALL(alife_world_init(&split, &config, error, sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&uninterrupted,
                                         uninterrupted.organisms[0].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&uninterrupted,
                                         uninterrupted.organisms[0].id,
                                         ALIFE_STATE_OFF, 50U, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&split, split.organisms[0].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&split, split.organisms[0].id,
                                         ALIFE_STATE_OFF, 50U, error,
                                         sizeof(error)), error);
    for (step = 0U; step < 7U; ++step) {
        EXPECT_CALL(alife_world_step(&uninterrupted, error, sizeof(error)),
                    error);
        EXPECT_CALL(alife_world_step(&split, error, sizeof(error)), error);
    }
    EXPECT_TRUE(alife_world_hash(&uninterrupted) == alife_world_hash(&split));
    checkpoint_hash = alife_world_hash(&split);
    EXPECT_CALL(alife_world_save(&split, TEST_CHECKPOINT_PATH, error,
                                 sizeof(error)),
                error);
    alife_world_destroy(&split);
    EXPECT_CALL(alife_world_load(&resumed, &config, TEST_CHECKPOINT_PATH, error,
                                 sizeof(error)),
                error);
    EXPECT_TRUE(alife_world_hash(&resumed) == checkpoint_hash);
    EXPECT_TRUE(resumed.organisms[0].state == ALIFE_STATE_OFF);
    EXPECT_TRUE(resumed.organisms[0].back_on_tick == UINT64_C(50));
    EXPECT_TRUE(resumed.organisms[1].state != ALIFE_STATE_OFF);

    for (step = 0U; step < 80U; ++step) {
        EXPECT_CALL(alife_world_step(&uninterrupted, error, sizeof(error)),
                    error);
        EXPECT_CALL(alife_world_step(&resumed, error, sizeof(error)), error);
    }
    EXPECT_TRUE(alife_world_hash(&uninterrupted) == alife_world_hash(&resumed));
    EXPECT_TRUE(resumed.asleep_to_awake_transitions > 0U);

cleanup:
    alife_world_destroy(&uninterrupted);
    alife_world_destroy(&split);
    alife_world_destroy(&resumed);
    (void)remove(TEST_CHECKPOINT_PATH);
    return test_result_value;
}

static enum test_result test_checkpoint_version_is_enforced(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    AlifeWorld rejected = {0};
    char error[TEST_ERROR_SIZE] = {0};
    FILE *checkpoint = NULL;
    uint32_t incompatible_version = UINT32_MAX;
    int close_result;

    prepare_config(&config);
    (void)remove(TEST_CHECKPOINT_PATH);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    EXPECT_CALL(alife_world_save(&world, TEST_CHECKPOINT_PATH, error,
                                 sizeof(error)),
                error);
    checkpoint = fopen(TEST_CHECKPOINT_PATH, "r+b");
    EXPECT_TRUE(checkpoint != NULL);
    EXPECT_TRUE(fseek(checkpoint, 8L, SEEK_SET) == 0);
    EXPECT_TRUE(fwrite(&incompatible_version, sizeof(incompatible_version),
                       1U, checkpoint) == 1U);
    close_result = fclose(checkpoint);
    checkpoint = NULL;
    EXPECT_TRUE(close_result == 0);

    EXPECT_TRUE(!alife_world_load(&rejected, &config, TEST_CHECKPOINT_PATH,
                                  error, sizeof(error)));
    EXPECT_TRUE(strstr(error, "version") != NULL);

cleanup:
    if (checkpoint != NULL) {
        (void)fclose(checkpoint);
    }
    alife_world_destroy(&world);
    alife_world_destroy(&rejected);
    (void)remove(TEST_CHECKPOINT_PATH);
    return test_result_value;
}

static enum test_result test_lifecycle_observability_records(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    char contents[8192] = {0};
    FILE *log_file = NULL;
    size_t bytes_read;

    prepare_config(&config);
    config.calendar_start_month = 4U;
    config.calendar_start_day = 1U;
    config.ticks_per_day = UINT64_C(10);
    config.summary_interval = UINT64_C(1);
    config.logging_level = ALIFE_LOG_EVENTS;
    (void)remove(TEST_EVENT_PATH);
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&world, world.organisms[0].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    set_controls(&world, world.organisms[0].id,
                 -4.0F, -4.0F, -4.0F, -1.0F);
    EXPECT_CALL(alife_transition_request(&world, world.organisms[1].id,
                                         ALIFE_STATE_ASLEEP, 0U, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_transition_request(&world, world.organisms[1].id,
                                         ALIFE_STATE_OFF,
                                         config.off_min_duration_ticks, error,
                                         sizeof(error)), error);
    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    log_file = fopen(TEST_EVENT_PATH, "rb");
    EXPECT_TRUE(log_file != NULL);
    bytes_read = fread(contents, 1U, sizeof(contents) - 1U, log_file);
    contents[bytes_read] = '\0';
    EXPECT_TRUE(strstr(contents, "\"event\":\"state_transition\"") != NULL);
    EXPECT_TRUE(strstr(contents, "\"event\":\"foolsday_sleep_roll\"") !=
                NULL);
    EXPECT_TRUE(strstr(contents,
                       "\"awake\":0,\"asleep\":1,\"off\":1") != NULL);
    EXPECT_TRUE(strstr(contents, "\"state_transitions\":3") != NULL);
    EXPECT_TRUE(strstr(contents, "\"awake_to_asleep\":2") != NULL);

cleanup:
    if (log_file != NULL) {
        (void)fclose(log_file);
    }
    alife_world_destroy(&world);
    (void)remove(TEST_EVENT_PATH);
    return test_result_value;
}

int main(void)
{
    static const struct test_case tests[] = {
        {"example configurations are valid",
         test_example_configurations_are_valid},
        {"immature organisms cannot reproduce",
         test_immature_organisms_cannot_reproduce},
        {"reproduction requires distinct parents",
         test_reproduction_requires_distinct_parents},
        {"one parent cannot create offspring",
         test_single_parent_cannot_create_offspring},
        {"offspring respects neural size limits",
         test_offspring_respects_neural_size_limits},
        {"capacity removes the oldest organism",
         test_capacity_removes_oldest_organism},
        {"seeds and offspring begin awake",
         test_initial_and_offspring_states_are_awake},
        {"seeded neural rhythm reaches sleep and wake",
         test_seeded_neural_sleep_rhythm},
        {"seeded rhythm is ordinary heritable genome data",
         test_seeded_rhythm_is_heritable_genome_data},
        {"sleep has no substrate timer", test_no_substrate_sleep_timer},
        {"sleep rhythm is reachable across seeds",
         test_sleep_rhythm_is_reachable_across_seeds},
        {"transition graph and control outputs are enforced",
         test_transition_graph_and_control_outputs},
        {"sleep and off disable external behavior",
         test_sleep_and_off_disable_external_behavior},
        {"plasticity occurs only during sleep",
         test_plasticity_occurs_only_during_sleep},
        {"off timer returns only to sleep",
         test_off_timer_returns_to_sleep},
        {"Fool's Day applies state-specific rules",
         test_foolsday_state_rules},
        {"Fool's Day sleep risk is evaluated once",
         test_foolsday_sleep_probability_and_once_only},
        {"waking on Fool's Day is fatal before awake execution",
         test_foolsday_wake_dies_before_awake_execution},
        {"off survives Fool's Day and expires into sleep",
         test_foolsday_off_survival_and_expiry},
        {"age and reward advance in every state",
         test_age_and_reward_advance_in_all_states},
        {"pairing requires consent and opportunity",
         test_pair_requires_consent_and_opportunity},
        {"courtship is exclusive and sustained",
         test_courtship_is_exclusive_and_sustained},
        {"biological rates and dormancy timeout are enforced",
         test_biological_rates_and_dormancy_timeout},
        {"invalid neural values are rejected",
         test_invalid_neural_values_are_rejected},
        {"identical seeds are reproducible",
         test_identical_seeds_are_reproducible},
        {"mutation stays within bounds", test_mutation_stays_within_bounds},
        {"checkpoint resume preserves state",
         test_checkpoint_resume_preserves_state},
        {"checkpoint version is enforced",
         test_checkpoint_version_is_enforced},
        {"lifecycle events and state summaries are observable",
         test_lifecycle_observability_records},
    };
    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    size_t failures = 0U;
    size_t index;

    (void)remove(TEST_EVENT_PATH);
    for (index = 0U; index < test_count; ++index) {
        const enum test_result result = tests[index].run();

        if (result == TEST_PASS) {
            (void)printf("ok %zu - %s\n", index + 1U, tests[index].name);
        } else {
            ++failures;
            (void)printf("not ok %zu - %s\n", index + 1U,
                         tests[index].name);
        }
    }
    (void)remove(TEST_EVENT_PATH);

    (void)printf("1..%zu\n", test_count);
    (void)printf("# %zu assertions, %zu failures\n", assertion_count,
                 failures);

    return failures == 0U ? 0 : 1;
}
