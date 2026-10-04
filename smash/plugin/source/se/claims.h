#ifndef SALTYSD_SE_CLAIMS_H
#define SALTYSD_SE_CLAIMS_H

#include "types.h"

typedef struct {
    u32 start;
    u32 end;
} se_claim_range;

typedef struct {
    se_claim_range *ranges;
    u32 count;
    u32 capacity;
} se_claims;

void se_claims_init(se_claims *claims, se_claim_range *storage, u32 capacity);
u32 se_claims_mark(const se_claims *claims);
void se_claims_restore(se_claims *claims, u32 mark);
int se_claims_overlap(const se_claims *claims, u32 start, u32 end);
int se_claims_add(se_claims *claims, u32 start, u32 end);

#endif
