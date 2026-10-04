#ifndef SALTYSD_PLUGINS_H
#define SALTYSD_PLUGINS_H

#include "salt_patch.h"
#include "toggles.h"

#define PLUGINS_MAX 62

extern toggle_list plugins;

void plugins_load(const SaltPatch *table, u32 count, u32 code_end);
void plugins_cro_prepare(void);
void plugins_cro_loaded(u32 base);
void plugins_lifecycle_cro_loaded(const char *name, u32 base);

#endif
