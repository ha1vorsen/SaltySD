#include "claims.h"

static int ranges_overlap(u32 start, u32 end, u32 other_start, u32 other_end)
{
    return start < other_end && other_start < end;
}

void se_claims_init(se_claims *claims, se_claim_range *storage, u32 capacity)
{
    claims->ranges = storage;
    claims->count = 0;
    claims->capacity = capacity;
}

u32 se_claims_mark(const se_claims *claims)
{
    return claims->count;
}

void se_claims_restore(se_claims *claims, u32 mark)
{
    if (mark <= claims->count)
        claims->count = mark;
}

int se_claims_overlap(const se_claims *claims, u32 start, u32 end)
{
    for (u32 i = 0; i < claims->count; i++) {
        const se_claim_range *range = &claims->ranges[i];
        if (ranges_overlap(start, end, range->start, range->end))
            return 1;
    }
    return 0;
}

int se_claims_add(se_claims *claims, u32 start, u32 end)
{
    if (start >= end || claims->count == claims->capacity)
        return 0;
    claims->ranges[claims->count].start = start;
    claims->ranges[claims->count].end = end;
    claims->count++;
    return 1;
}
