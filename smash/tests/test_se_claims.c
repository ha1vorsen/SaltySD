#include <assert.h>

#include "se/claims.h"

static void pending_attempt(se_claims *claims, u32 start, u32 end)
{
    u32 mark = se_claims_mark(claims);
    assert(se_claims_add(claims, start, end));

    /* failed retention: pre-attempt ledger */
    se_claims_restore(claims, mark);
}

int main(void)
{
    se_claim_range storage[4];
    se_claims claims;
    se_claims_init(&claims, storage, 4);

    assert(se_claims_add(&claims, 0x1000, 0x1010));
    pending_attempt(&claims, 0x2000, 0x2010);
    assert(claims.count == 1);
    assert(!se_claims_overlap(&claims, 0x2000, 0x2010));

    /* repeated failure idempotence */
    pending_attempt(&claims, 0x2000, 0x2010);
    assert(claims.count == 1);

    /* later retry claimability */
    assert(se_claims_add(&claims, 0x2000, 0x2010));
    assert(claims.count == 2);
    assert(se_claims_overlap(&claims, 0x2008, 0x2020));

    /* owner-local rollback */
    pending_attempt(&claims, 0x3000, 0x3010);
    assert(claims.count == 2);
    assert(!se_claims_overlap(&claims, 0x3000, 0x3010));
    assert(!se_claims_add(&claims, 0x4000, 0x4000));

    return 0;
}
