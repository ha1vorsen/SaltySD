#include <assert.h>
#include <stdint.h>

#include "se/hooks_internal.h"

int main(void)
{
    volatile se_u32 slot = 0x00123456u;
    se_hook_result result;
    se_hooks_reset();
    assert(se_hooks_install_pointer(1, 7, &slot, 0x00700000u,
                                    SE_HOOK_EXCLUSIVE, &result) == PLUGIN_OK);
    assert(slot == 0x00700000u);
    assert((uintptr_t)result.original == 0x00123456u);
    assert(se_hooks_install_pointer(2, 7, &slot, 0x00700010u,
                                    SE_HOOK_EXCLUSIVE, &result) == SE_ERROR_HOOK_CONFLICT);
    assert(slot == 0x00700000u);
    assert(se_hooks_remove(2, result.handle) == SE_ERROR_HOOK_UNSUPPORTED);
    assert(se_hooks_remove(1, result.handle) == PLUGIN_OK);
    assert(slot == 0x00123456u);

    assert(se_hooks_install_pointer(1, 7, &slot, 0x00700000u,
                                    SE_HOOK_EXCLUSIVE, &result) == PLUGIN_OK);
    slot = 0x00DEAD00u;
    assert(se_hooks_remove(1, result.handle) == SE_ERROR_HOOK_TARGET_CHANGED);
    assert(slot == 0x00DEAD00u);
    se_hooks_reset();

    volatile se_u32 owned_a = 0x1000u, owned_b = 0x2000u, other = 0x3000u;
    assert(se_hooks_install_pointer(1, 8, &owned_a, 0x700010u,
                                    SE_HOOK_EXCLUSIVE, &result) == PLUGIN_OK);
    assert(se_hooks_install_pointer(1, 9, &owned_b, 0x700020u,
                                    SE_HOOK_EXCLUSIVE, &result) == PLUGIN_OK);
    assert(se_hooks_install_pointer(2, 10, &other, 0x700030u,
                                    SE_HOOK_EXCLUSIVE, &result) == PLUGIN_OK);
    assert(se_hooks_remove_owner(1) == PLUGIN_OK);
    assert(owned_a == 0x1000u && owned_b == 0x2000u && other == 0x700030u);

    assert(se_hooks_install_pointer(1, 11, &owned_a, 0x700040u,
                                    SE_HOOK_EXCLUSIVE, &result) == PLUGIN_OK);
    owned_a = 0xDEAD00u;
    assert(se_hooks_remove_owner(1) == SE_ERROR_HOOK_TARGET_CHANGED);
    assert(owned_a == 0xDEAD00u);
    return 0;
}
