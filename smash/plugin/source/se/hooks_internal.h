#ifndef SALTYSD_SE_HOOKS_INTERNAL_H
#define SALTYSD_SE_HOOKS_INTERNAL_H

#include <se/host.h>

#include "se/errors.h"

void se_hooks_reset(void);
se_error se_hooks_install_pointer(se_package_handle owner, se_u32 target_id,
                                  volatile se_u32 *slot, se_u32 handler,
                                  se_u32 flags, se_hook_result *result);
se_error se_hooks_install(se_package_handle owner, const se_hook_request *request,
                          se_hook_result *result);
se_error se_hooks_remove(se_package_handle owner, se_hook_handle hook);

#endif
