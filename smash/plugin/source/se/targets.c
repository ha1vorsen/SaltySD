#include "targets_internal.h"

#include <se_targets.generated.h>

static void copy_bytes(void *target, const void *source, se_u32 size)
{
    se_u8 *out = target;
    const se_u8 *in = source;
    for (se_u32 i = 0; i < size; i++)
        out[i] = in[i];
}

void se_targets_build_identity(se_build_identity *out)
{
    out->title_id = SE_RUNTIME_TITLE_ID;
    out->game_major = SE_RUNTIME_GAME_MAJOR;
    out->game_minor = SE_RUNTIME_GAME_MINOR;
    out->region = SE_RUNTIME_REGION;
    out->reserved = 0;
    out->build_id = SE_RUNTIME_BUILD_ID;
    out->registry_revision = SE_RUNTIME_REGISTRY_REVISION;
    copy_bytes(out->code_sha256, se_runtime_code_sha256, sizeof(out->code_sha256));
}

int se_targets_resolve(se_u32 target_id, se_target *out)
{
    for (se_u32 i = 0; i < sizeof(se_registry_targets) / sizeof(se_registry_targets[0]); i++) {
        const se_registry_target *entry = &se_registry_targets[i];
        if (entry->id != target_id)
            continue;
        out->id = entry->id;
        out->address = entry->address;
        out->span = entry->span;
        out->kind = entry->kind;
        out->flags = 0;
        return 0;
    }
    return -1;
}

int se_targets_expected_matches(se_u32 target_id)
{
    for (se_u32 i = 0; i < sizeof(se_registry_targets) / sizeof(se_registry_targets[0]); i++) {
        const se_registry_target *entry = &se_registry_targets[i];
        if (entry->id != target_id || !entry->expected_size ||
            entry->expected_size > entry->span)
            continue;
        const volatile se_u8 *actual = (const volatile se_u8 *)(uintptr_t)entry->address;
        for (se_u32 byte = 0; byte < entry->expected_size; byte++)
            if (actual[byte] != entry->expected[byte])
                return 0;
        return 1;
    }
    return 0;
}
