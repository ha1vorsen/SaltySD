#pragma once

#include <stdint.h>

void melee_seed(void *fighter);
void melee_decay(void *fighter);
void melee_status_record(void *fighter, uint32_t status, uint32_t caller);
int melee_block_redodge(void *fighter);
int melee_override_velocity(void *module, float *out_xy);
int melee_landing_only(void *fighter);
