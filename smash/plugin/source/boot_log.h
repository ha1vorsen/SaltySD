#ifndef SALTYSD_BOOT_LOG_H
#define SALTYSD_BOOT_LOG_H

#include "plgldr.h"

#define BOOT_LOG_PATH_MAX (PLUGIN_PATH_MAX + 32)

typedef struct {
    char marker[BOOT_LOG_PATH_MAX];
    char log[BOOT_LOG_PATH_MAX];
} boot_log_paths;

int boot_log_paths_resolve(boot_log_paths *paths);

#endif
