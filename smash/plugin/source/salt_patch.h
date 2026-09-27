#ifndef SALTYSD_SALT_PATCH_H
#define SALTYSD_SALT_PATCH_H

#include "types.h"

typedef struct {
    u32       addr;
    u32       len;
    const u8 *want;
    const u8 *orig;
} SaltPatch;

#endif
