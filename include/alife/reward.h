#ifndef ALIFE_REWARD_H
#define ALIFE_REWARD_H

#include <stdbool.h>
#include <stdint.h>

/** Describes one substrate-owned reward decision. */
typedef struct {
    uint64_t tick;
    uint64_t age;
    uint64_t accumulated_reward;
    bool survived_tick;
} AlifeRewardContext;

/**
 * Calculates the version 1 reward for one organism tick.
 *
 * Keeping reward calculation behind this interface lets later experiments
 * add external rewards without changing neural execution or population code.
 *
 * @param context Substrate-owned facts about the completed tick.
 * @return Reward to add to the organism's accumulated reward.
 */
uint64_t alife_reward_calculate(const AlifeRewardContext *context);

#endif
