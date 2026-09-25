/* Writes SaltySD's game-side patches into the running game before it starts. */

#include "types.h"

typedef struct {
    u32       addr;
    u32       len;
    const u8 *want;
    const u8 *orig;
} SaltyPatch;

typedef struct {
    u32       addr;
    u32       len;
    const u8 *v12;
    const u8 *stock;
} LegacyRun;

typedef struct {
    const LegacyRun *runs;
    u32              count;
} LegacyVariant;

#include <patches.h>
#include "status.h"

//Only release builds carry the v1.2 table.
#if __has_include(<legacy.h>)
#include <legacy.h>
#if SALTYSD_LEGACY_TITLE_ID != SALTYSD_TITLE_ID
#error "legacy.h was generated for another region"
#endif
#define NUM_LEGACY (sizeof(legacy_variants) / sizeof(legacy_variants[0]))
#endif

#define NUM_PATCHES (sizeof(saltysd_patches) / sizeof(saltysd_patches[0]))

volatile saltysd_status_t saltysd_status = { SALTYSD_MAGIC, 0, 0, 0, 0, 0, 0, 0 };

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

//nullifies stock SaltySD v1.2 before patching. doesn't catch every case of a person using an older build, but catches most of them
static int undo_legacy(void)
{
#ifdef NUM_LEGACY
    int partial = 0;

    for (u32 v = 0; v < NUM_LEGACY; v++) {
        const LegacyVariant *lv = &legacy_variants[v];
        u32 matched = 0;

        for (u32 i = 0; i < lv->count; i++)
            if (mem_eq((const volatile u8 *)lv->runs[i].addr, lv->runs[i].v12, lv->runs[i].len))
                matched++;

        if (matched == lv->count) {
            for (u32 i = 0; i < lv->count; i++) {
                volatile u8 *at = (volatile u8 *)lv->runs[i].addr;
                const u8 *stock = lv->runs[i].stock;

                for (u32 b = 0; b < lv->runs[i].len; b++)
                    at[b] = stock[b];
            }
            saltysd_status.legacy = v + 1;
            return 1;
        }

        if (matched)
            partial = 1;
    }

    if (partial) {
        saltysd_status.legacy = ~0u;
        return -1;
    }
#endif
    return 0;
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
    u32 verified = 0;

    for (u32 i = 0; i < NUM_PATCHES; i++)
        if (mem_eq((const volatile u8 *)saltysd_patches[i].addr,
                   saltysd_patches[i].want, saltysd_patches[i].len))
            verified++;

    return verified;
}

void plugin_main(void)
{
    saltysd_status.stage = 1;
    saltysd_status.patches = NUM_PATCHES;

    int undone = undo_legacy();
    if (undone < 0) {
        saltysd_status.refused = 1;
        return;
    }

    if (check_all()) {
        saltysd_status.applied = apply_all();
        saltysd_status.stage = 2;
    } else {
        saltysd_status.refused = 1;
        if (!undone)
            return;
    }

    //Hardware fetches stale instructions until these run.
    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
    __asm__ volatile ("svc 0x94" ::: "r0", "r1", "r2", "r3", "r12", "memory");

    if (saltysd_status.refused)
        return;

    saltysd_status.verified = verify_all();
    saltysd_status.stage = 3;
}
