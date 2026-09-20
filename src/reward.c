#include "alife/reward.h"

#include <stddef.h>

uint64_t alife_reward_calculate(const AlifeRewardContext *context) {
    if (context == NULL || !context->survived_tick) {
        return 0U;
    }
    return 1U;
}
