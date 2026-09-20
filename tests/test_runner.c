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
    world.organisms[0].age = config.maturity_age;
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
    world.organisms[0].age = config.maturity_age;
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
    world.organisms[0].age = config.maturity_age;
    world.organisms[1].age = config.maturity_age;
    make_reproduction_request(&world, parent_a);
    make_reproduction_request(&world, parent_b);
    child_id = world.next_id;
    EXPECT_CALL(alife_try_birth(&world, parent_a, parent_b, true, error,
                                sizeof(error)),
                error);
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
    world.organisms[0].age = UINT64_C(100);
    world.organisms[1].age = UINT64_C(10);
    world.config.capacity_bytes = (uint64_t)alife_organism_size(&world);

    alife_enforce_capacity(&world);
    EXPECT_TRUE(world.count == 1U);
    EXPECT_TRUE(alife_find_organism(&world, oldest_id) == NULL);
    EXPECT_TRUE(alife_find_organism(&world, younger_id) != NULL);
    EXPECT_TRUE(world.population_bytes <= world.config.capacity_bytes);
    EXPECT_TRUE(world.total_deaths == UINT64_C(1));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_april_first_prevents_execution(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    uint64_t executions_before;
    size_t attempt;

    prepare_config(&config);
    config.calendar_start_year = 2028U;
    config.calendar_start_month = 3U;
    config.calendar_start_day = 31U;
    config.ticks_per_day = UINT64_C(1);
    config.april_1_behavior = ALIFE_APRIL_TERMINATE;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    executions_before = UINT64_C(0);
    for (attempt = 0U;
         attempt < 2U && !(world.month == 4 && world.day == 1);
         ++attempt) {
        executions_before = world.total_executions;
        EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    }
    EXPECT_TRUE(world.year == 2028);
    EXPECT_TRUE(world.month == 4);
    EXPECT_TRUE(world.day == 1);
    EXPECT_TRUE(world.total_executions == executions_before);
    EXPECT_TRUE(world.count == 0U);
    EXPECT_TRUE(world.total_deaths == UINT64_C(2));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_april_first_cannot_be_bypassed(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};
    float *genome;

    prepare_config(&config);
    config.calendar_start_year = 2027U;
    config.calendar_start_month = 4U;
    config.calendar_start_day = 1U;
    config.ticks_per_day = UINT64_C(1);
    config.april_1_behavior = ALIFE_APRIL_TERMINATE;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);
    genome = alife_organism_genome_mut(&world, world.organisms[0].id);
    EXPECT_TRUE(genome != NULL);
    genome[0] = NAN;
    world.organisms[0].reproduction_output = INFINITY;
    world.organisms[0].acceptance_output = INFINITY;

    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(world.count == 0U);
    EXPECT_TRUE(world.total_executions == UINT64_C(0));
    EXPECT_TRUE(world.total_deaths == UINT64_C(2));

cleanup:
    alife_world_destroy(&world);
    return test_result_value;
}

static enum test_result test_april_second_reseeds_population(void)
{
    enum test_result test_result_value = TEST_PASS;
    AlifeConfig config;
    AlifeWorld world = {0};
    char error[TEST_ERROR_SIZE] = {0};

    prepare_config(&config);
    config.calendar_start_year = 2028U;
    config.calendar_start_month = 3U;
    config.calendar_start_day = 31U;
    config.ticks_per_day = UINT64_C(1);
    config.april_1_behavior = ALIFE_APRIL_RESEED;
    EXPECT_CALL(alife_world_init(&world, &config, error, sizeof(error)), error);

    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(world.month == 4);
    EXPECT_TRUE(world.day == 1);
    EXPECT_TRUE(world.count == 0U);
    EXPECT_TRUE(world.pending_reseed);
    EXPECT_TRUE(world.total_executions == UINT64_C(0));

    EXPECT_CALL(alife_world_step(&world, error, sizeof(error)), error);
    EXPECT_TRUE(world.month == 4);
    EXPECT_TRUE(world.day == 2);
    EXPECT_TRUE(world.count == 2U);
    EXPECT_TRUE(!world.pending_reseed);
    EXPECT_TRUE(world.total_births == UINT64_C(4));
    EXPECT_TRUE(world.total_executions == UINT64_C(2));

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
    world.organisms[0].age = config.maturity_age;
    world.organisms[1].age = config.maturity_age;
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

    for (step = 0U; step < 32U; ++step) {
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
    world.organisms[0].age = config.maturity_age;
    world.organisms[1].age = config.maturity_age;
    make_reproduction_request(&world, parent_a);
    make_reproduction_request(&world, parent_b);
    child_id = world.next_id;

    EXPECT_CALL(alife_try_birth(&world, parent_a, parent_b, true, error,
                                sizeof(error)),
                error);
    child_genome = alife_organism_genome(&world, child_id);
    first_genome = alife_organism_genome_mut(&world, parent_a);
    EXPECT_TRUE(child_genome != NULL);
    EXPECT_TRUE(first_genome != NULL);
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

    for (step = 0U; step < 12U; ++step) {
        EXPECT_CALL(alife_world_step(&uninterrupted, error, sizeof(error)),
                    error);
        EXPECT_CALL(alife_world_step(&resumed, error, sizeof(error)), error);
    }
    EXPECT_TRUE(alife_world_hash(&uninterrupted) == alife_world_hash(&resumed));

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
        {"April 1 prevents execution", test_april_first_prevents_execution},
        {"April 1 cannot be bypassed", test_april_first_cannot_be_bypassed},
        {"April 2 reseeds the population", test_april_second_reseeds_population},
        {"pairing requires consent and opportunity",
         test_pair_requires_consent_and_opportunity},
        {"invalid neural values are rejected",
         test_invalid_neural_values_are_rejected},
        {"identical seeds are reproducible",
         test_identical_seeds_are_reproducible},
        {"mutation stays within bounds", test_mutation_stays_within_bounds},
        {"checkpoint resume preserves state",
         test_checkpoint_resume_preserves_state},
        {"checkpoint version is enforced",
         test_checkpoint_version_is_enforced},
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
