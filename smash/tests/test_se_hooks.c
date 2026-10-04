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
    return 0;
}
