#ifndef SALTYSD_SE_TARGETS_INTERNAL_H
#define SALTYSD_SE_TARGETS_INTERNAL_H

#include <se/host.h>

typedef struct {
    se_u32 id;
    se_u32 address;
    se_u32 span;
    se_u16 kind;
    se_u16 expected_size;
    se_u8 expected[16];
    const char *name;
} se_registry_target;

void se_targets_build_identity(se_build_identity *out);
int se_targets_resolve(se_u32 target_id, se_target *out);
int se_targets_expected_matches(se_u32 target_id);

#endif
