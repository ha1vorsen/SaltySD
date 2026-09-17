/* Writes SaltySD's game-side patches into the running game before it starts. */

typedef unsigned char u8;
typedef unsigned int  u32;

typedef struct {
    u32       addr;
    u32       len;
    const u8 *want;
    const u8 *orig;
} SaltyPatch;

#include "patches.h"

#define NUM_PATCHES (sizeof(saltysd_patches) / sizeof(saltysd_patches[0]))

#define SALTYSD_MAGIC 0x534C5447u /* 'SLTG' */

volatile struct {
    u32 magic;
    u32 stage;
    u32 patches;
    u32 applied;
    u32 verified;
    u32 refused;
} saltysd_status = { SALTYSD_MAGIC, 0, 0, 0, 0, 0 };

static int mem_eq(const volatile u8 *a, const u8 *b, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

//Half a SaltySD is worse than none, so an unrecognised game gets nothing.
static int check_all(void)
{
    for (u32 i = 0; i < NUM_PATCHES; i++) {
        const volatile u8 *at = (const volatile u8 *)saltysd_patches[i].addr;
        if (mem_eq(at, saltysd_patches[i].want, saltysd_patches[i].len))
            continue;
        if (!mem_eq(at, saltysd_patches[i].orig, saltysd_patches[i].len))
            return 0;
    }
    return 1;
}

static u32 apply_all(void)
{
    u32 applied = 0;

    for (u32 i = 0; i < NUM_PATCHES; i++) {
        volatile u8 *at = (volatile u8 *)saltysd_patches[i].addr;
        const u8 *want = saltysd_patches[i].want;

        for (u32 b = 0; b < saltysd_patches[i].len; b++)
            at[b] = want[b];

        applied++;
    }

    return applied;
}

static u32 verify_all(void)
{
    u32 ok = 0;

    for (u32 i = 0; i < NUM_PATCHES; i++)
        if (mem_eq((const volatile u8 *)saltysd_patches[i].addr,
                   saltysd_patches[i].want, saltysd_patches[i].len))
            ok++;

    return ok;
}

void plugin_main(void)
{
    saltysd_status.stage = 1;
    saltysd_status.patches = NUM_PATCHES;

    if (!check_all()) {
        saltysd_status.refused = 1;
        return;
    }

    saltysd_status.applied = apply_all();
    saltysd_status.stage = 2;

    //Hardware fetches stale instructions until these run.
    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
    __asm__ volatile ("svc 0x94" ::: "r0", "r1", "r2", "r3", "r12", "memory");

    saltysd_status.verified = verify_all();
    saltysd_status.stage = 3;
}
