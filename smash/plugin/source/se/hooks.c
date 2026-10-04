#include "hooks_internal.h"

#include "se/targets_internal.h"

#define SE_HOOKS_MAX 32

typedef struct {
    se_package_handle owner;
    se_u32 target_id;
    volatile se_u32 *slot;
    se_u32 original;
    se_u32 handler;
    se_u16 generation;
    se_u8 active;
} managed_hook;

static managed_hook hooks[SE_HOOKS_MAX];
static se_u16 generations[SE_HOOKS_MAX];

static se_hook_handle make_handle(se_u32 slot)
{
    return ((se_u32)generations[slot] << 16) | (slot + 1);
}

static void maintain_data_cache(void)
{
#if defined(__arm__)
    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
#endif
}

void se_hooks_reset(void)
{
    for (se_u32 i = 0; i < SE_HOOKS_MAX; i++) {
        hooks[i].active = 0;
        generations[i]++;
        if (!generations[i])
            generations[i] = 1;
    }
}

se_error se_hooks_install_pointer(se_package_handle owner, se_u32 target_id,
                                  volatile se_u32 *slot, se_u32 handler,
                                  se_u32 flags, se_hook_result *result)
{
    if (!owner || !slot || !handler || !result || flags != SE_HOOK_EXCLUSIVE)
        return SE_ERROR_HOOK_UNSUPPORTED;
    for (se_u32 i = 0; i < SE_HOOKS_MAX; i++)
        if (hooks[i].active && hooks[i].target_id == target_id)
            return SE_ERROR_HOOK_CONFLICT;
    se_u32 free_slot = SE_HOOKS_MAX;
    for (se_u32 i = 0; i < SE_HOOKS_MAX; i++)
        if (!hooks[i].active) {
            free_slot = i;
            break;
        }
    if (free_slot == SE_HOOKS_MAX)
        return SE_ERROR_HOOK_FULL;

    managed_hook *hook = &hooks[free_slot];
    hook->owner = owner;
    hook->target_id = target_id;
    hook->slot = slot;
    hook->original = *slot;
    hook->handler = handler;
    hook->generation = generations[free_slot];
    hook->active = 1;
    *slot = handler;
    maintain_data_cache();
    result->handle = make_handle(free_slot);
    result->next = (void *)(uintptr_t)hook->original;
    result->original = (void *)(uintptr_t)hook->original;
    return PLUGIN_OK;
}

se_error se_hooks_install(se_package_handle owner, const se_hook_request *request,
                          se_hook_result *result)
{
    if (!request || !result)
        return SE_ERROR_HOOK_UNSUPPORTED;
    se_target target;
    if (se_targets_resolve(request->target_id, &target) < 0 ||
        (target.kind != SE_TARGET_POINTER_SLOT && target.kind != SE_TARGET_TABLE) ||
        target.span != 4 || !se_targets_expected_matches(request->target_id))
        return SE_ERROR_HOOK_UNSUPPORTED;
    uintptr_t handler = (uintptr_t)request->handler;
    if (!handler || handler > 0xFFFFFFFFu)
        return SE_ERROR_HOOK_UNSUPPORTED;
    return se_hooks_install_pointer(owner, request->target_id,
                                    (volatile se_u32 *)(uintptr_t)target.address,
                                    (se_u32)handler, request->flags, result);
}

se_error se_hooks_remove(se_package_handle owner, se_hook_handle handle)
{
    se_u32 encoded_slot = handle & 0xFFFFu;
    se_u16 generation = handle >> 16;
    if (!encoded_slot || encoded_slot > SE_HOOKS_MAX)
        return SE_ERROR_HOOK_UNSUPPORTED;
    managed_hook *hook = &hooks[encoded_slot - 1];
    if (!hook->active || hook->owner != owner || hook->generation != generation)
        return SE_ERROR_HOOK_UNSUPPORTED;
    if (*hook->slot != hook->handler)
        return SE_ERROR_HOOK_TARGET_CHANGED;
    *hook->slot = hook->original;
    maintain_data_cache();
    hook->active = 0;
    generations[encoded_slot - 1]++;
    if (!generations[encoded_slot - 1])
        generations[encoded_slot - 1] = 1;
    return PLUGIN_OK;
}
