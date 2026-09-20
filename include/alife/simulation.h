#ifndef ALIFE_SIMULATION_H
#define ALIFE_SIMULATION_H

#include "alife/config.h"
#include "alife/rng.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ALIFE_CHECKPOINT_VERSION 4U
#define ALIFE_BIOLOGICAL_AGE_SCALE UINT64_C(1000000)
#define ALIFE_MIN_GENOME_PARAMETERS 500U
#define ALIFE_MAX_GENOME_PARAMETERS 2000U
#define ALIFE_PRIVATE_CONTROL_OUTPUTS 4U
#define ALIFE_NONCOMMUNICATION_OUTPUTS 7U
#define ALIFE_PRIVATE_COURTSHIP_INPUTS 1U

typedef enum {
    ALIFE_STATE_AWAKE = 0,
    ALIFE_STATE_ASLEEP = 1,
    ALIFE_STATE_OFF = 2
} AlifeLifecycleState;

typedef enum {
    ALIFE_OUTPUT_REPRODUCTION = 0,
    ALIFE_OUTPUT_ACCEPTANCE = 1,
    ALIFE_OUTPUT_SLEEP = 2,
    ALIFE_OUTPUT_WAKE = 3,
    ALIFE_OUTPUT_OFF = 4,
    ALIFE_OUTPUT_OFF_DURATION = 5,
    ALIFE_OUTPUT_COURTSHIP_SIGNAL = 6
} AlifeOutputOffset;

typedef enum {
    ALIFE_DEATH_CAPACITY = 0,
    ALIFE_DEATH_FOOLSDAY_AWAKE = 1,
    ALIFE_DEATH_FOOLSDAY_SLEEP = 2,
    ALIFE_DEATH_INVALID_STATE = 3,
    ALIFE_DEATH_SHUTDOWN = 4,
    ALIFE_DEATH_DORMANCY_TIMEOUT = 5
} AlifeDeathCause;

typedef struct {
    size_t input_weights;
    size_t recurrent_weights;
    size_t hidden_biases;
    size_t output_weights;
    size_t output_biases;
    size_t plastic_rates;
    size_t plastic_decays;
    size_t genome_count;
    size_t plastic_state;
    size_t hidden_state;
    size_t inbox;
    size_t outbox;
    size_t slot_floats;
    size_t recurrent_count;
    size_t output_count;
} AlifeLayout;

typedef struct {
    uint64_t id;
    uint64_t parent_a;
    uint64_t parent_b;
    uint64_t birth_tick;
    union {
        uint64_t chronological_age;
        uint64_t age; /* Deprecated compatibility name. */
    };
    uint64_t biological_age;
    uint64_t reward;
    uint64_t generation;
    uint64_t reproduction_attempts;
    uint64_t successful_reproductions;
    uint64_t mutations;
    uint64_t significant_weight_changes;
    uint64_t executions;
    int32_t birth_year;
    int32_t birth_month;
    int32_t birth_day;
    AlifeLifecycleState state;
    uint64_t back_on_tick;
    uint64_t last_awake_tick;
    uint64_t courtship_partner_id;
    uint64_t courtship_progress;
    int32_t last_foolsday_roll_year;
    float reproduction_output;
    float acceptance_output;
    float sleep_output;
    float wake_output;
    float off_output;
    float off_duration_output;
    float courtship_output;
    float courtship_input;
    bool sent_message_this_tick;
} AlifeOrganism;

typedef struct {
    AlifeConfig config;
    AlifeLayout layout;
    AlifeRng rng;
    AlifeOrganism *organisms;
    float *data;
    uint64_t *scratch_ids;
    float *scratch_hidden;
    float *scratch_genome;
    size_t count;
    size_t slot_capacity;
    size_t scratch_capacity;
    uint64_t tick;
    uint64_t next_id;
    uint64_t total_births;
    uint64_t total_deaths;
    uint64_t total_reproduction_attempts;
    uint64_t total_mutations;
    uint64_t total_executions;
    uint64_t total_state_transitions;
    uint64_t awake_to_asleep_transitions;
    uint64_t asleep_to_awake_transitions;
    uint64_t asleep_to_off_transitions;
    uint64_t active_courtships;
    uint64_t courtships_started;
    uint64_t courtships_failed;
    uint64_t courtships_completed;
    uint64_t courtship_births;
    uint64_t population_bytes;
    int32_t year;
    int32_t month;
    int32_t day;
    bool stopped;
    bool initialized;
    FILE *event_log;
} AlifeWorld;

bool alife_layout_create(const AlifeConfig *config, AlifeLayout *layout,
                         char *error, size_t error_size);
size_t alife_organism_size(const AlifeWorld *world);

bool alife_world_init(AlifeWorld *world, const AlifeConfig *config,
                      char *error, size_t error_size);
void alife_world_destroy(AlifeWorld *world);
bool alife_world_step(AlifeWorld *world, char *error, size_t error_size);
bool alife_world_run(AlifeWorld *world, char *error, size_t error_size);

const AlifeOrganism *alife_find_organism(const AlifeWorld *world, uint64_t id);
AlifeOrganism *alife_find_organism_mut(AlifeWorld *world, uint64_t id);
float *alife_organism_genome_mut(AlifeWorld *world, uint64_t id);
const float *alife_organism_genome(const AlifeWorld *world, uint64_t id);
bool alife_genome_validate(const AlifeWorld *world, const float *genome,
                           char *error, size_t error_size);

double alife_reproduction_probability(const AlifeWorld *world,
                                      const AlifeOrganism *organism);
bool alife_reproduction_eligible(const AlifeWorld *world,
                                 const AlifeOrganism *organism);
const char *alife_lifecycle_state_name(AlifeLifecycleState state);
bool alife_transition_request(AlifeWorld *world, uint64_t organism_id,
                              AlifeLifecycleState requested_state,
                              uint64_t requested_off_duration,
                              char *error, size_t error_size);
bool alife_try_birth(AlifeWorld *world, uint64_t parent_a_id,
                     uint64_t parent_b_id, bool opportunity_granted,
                     char *error, size_t error_size);
void alife_enforce_capacity(AlifeWorld *world);

bool alife_world_save(AlifeWorld *world, const char *path,
                      char *error, size_t error_size);
bool alife_world_load(AlifeWorld *world, const AlifeConfig *config,
                      const char *path, char *error, size_t error_size);
bool alife_checkpoint_inspect(const char *path, FILE *output,
                              char *error, size_t error_size);
uint64_t alife_world_hash(const AlifeWorld *world);

#endif
