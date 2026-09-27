#ifndef SALTYSD_PLUGINS_H
#define SALTYSD_PLUGINS_H

#include "salt_patch.h"
#include "toggles.h"

#define PLUGINS_MAX 62

extern toggle_list plugins;

void plugins_load(const SaltPatch *table, u32 count, u32 code_end);

#endif
