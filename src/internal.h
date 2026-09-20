#ifndef ALIFE_INTERNAL_H
#define ALIFE_INTERNAL_H

#include "alife/simulation.h"

#include <stdarg.h>

void alife_set_error(char *error, size_t error_size, const char *format, ...);
float *alife_slot(AlifeWorld *world, size_t index);
const float *alife_slot_const(const AlifeWorld *world, size_t index);
bool alife_reserve(AlifeWorld *world, size_t needed, char *error,
                   size_t error_size);
bool alife_setup_world(AlifeWorld *world, const AlifeConfig *config,
                       const char *log_mode, bool add_seeds,
                       char *error, size_t error_size);
bool alife_organism_state_valid(const AlifeWorld *world, size_t index);
void alife_log_run_start(AlifeWorld *world);
void alife_log_run_end(AlifeWorld *world);
void alife_log_birth(AlifeWorld *world, const AlifeOrganism *organism);
void alife_log_reproduction(AlifeWorld *world, uint64_t parent_a,
                            uint64_t parent_b, bool approved,
                            const char *reason);
void alife_log_death(AlifeWorld *world, const AlifeOrganism *organism,
                     AlifeDeathCause cause);
void alife_log_plasticity(AlifeWorld *world, const AlifeOrganism *organism,
                          double magnitude);
void alife_log_summary(AlifeWorld *world);
void alife_log_checkpoint(AlifeWorld *world, const char *path);
void alife_log_reseed(AlifeWorld *world);
const char *alife_death_cause_name(AlifeDeathCause cause);

#endif
