#ifndef SALTYSD_MODS_H
#define SALTYSD_MODS_H

#include "toggles.h"

#define MODS_MAX       62
#define MODS_INDEX_DIR  "/saltysd"
#define MODS_INDEX_STEM ".saltysd-"

extern toggle_list mods;

int mods_drop_index(void);

#endif
