#include <stddef.h>

#include <se/host.h>

#define ENTRY(section_name) __attribute__((section(section_name), noinline, used))

ENTRY(".no_hitboxes") void no_hitboxes(void)
{
}

ENTRY(".se_init") int se_plugin_init(const se_host_v1 *host, se_package_handle owner)
{
    const size_t hook_end = offsetof(se_host_v1, remove_hook) + sizeof(host->remove_hook);
    if (!host || host->header.magic != SE_HOST_MAGIC ||
        host->header.abi_major != SE_HOST_ABI_MAJOR ||
        host->header.structure_size < hook_end ||
        !(host->header.capabilities_lo & SE_CAP_HOOKS))
        return -1;

    const se_hook_request request = {
        SE_TARGET_SMASH_ANIMCMD_HITTEST_HITBOX_SLOT,
        no_hitboxes,
        SE_HOOK_EXCLUSIVE,
        0,
    };
    se_hook_result result;
    return host->install_hook(owner, &request, &result);
}
