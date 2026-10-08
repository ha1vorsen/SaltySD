#ifndef SALTYSD_SE_HOST_INTERNAL_H
#define SALTYSD_SE_HOST_INTERNAL_H

#include <se/host.h>

#include "se/errors.h"
#include "types.h"

void se_host_reset(void);
void se_host_register_package(se_package_handle handle, const u16 *folder_name);
se_error se_host_quiesce_owner(se_package_handle owner);
void se_host_release_owner(se_package_handle owner);
se_u32 se_host_hook_mark(void);
void se_host_hook_restore(se_u32 mark);
int se_host_allow_hook(se_package_handle owner, se_u32 target_id,
                       se_u32 handler, se_u32 flags);
const se_host_v1 *se_get_host(se_u32 requested_major);

#endif
