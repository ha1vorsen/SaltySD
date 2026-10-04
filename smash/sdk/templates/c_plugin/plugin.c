#include <se/host.h>

extern const se_host_v1 *se_get_host_import(se_u32 requested_major);

int se_plugin_init(const se_host_v1 *host, se_package_handle owner)
{
    if (!host || host->header.magic != SE_HOST_MAGIC ||
        host->header.abi_major != SE_HOST_ABI_MAJOR)
        return -1;
    static const char message[] = "SEA 2 C plugin initialized";
    if (host->header.structure_size >= sizeof(se_host_v1) &&
        (host->header.capabilities_lo & SE_CAP_LOG))
        host->log(owner, 1, message, sizeof(message) - 1);
    return 0;
}
