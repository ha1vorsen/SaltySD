#ifndef SALTYSD_PLUGINS_CRO_H
#define SALTYSD_PLUGINS_CRO_H

#include "types.h"
#include "se/errors.h"

#define PLUGIN_IMAGE_SIZE 0x10000
#define PLUGIN_SIGS_MAX 64
#define PLUGIN_SEGS_MAX 256
#define PLUGIN_TARGETS_MAX 64
#define PLUGIN_FIXUP_BRANCH 1
#define PLUGIN_FIXUP_ABS32 2
#define PLUGIN_BRANCH_REACH 0x800000

typedef struct {
    u32 name_off;
    u32 name_len;
} cro_target_record;

typedef struct {
    u32 target;
    u32 len;
    u32 anchor;
    u32 anchor_at;
    u32 pattern_off;
    u32 mask_off;
    u32 match;
    u32 cached_off;
    u32 cached;
} cro_signature_record;

typedef struct {
    u32 target;
    u32 signature;
    int offset;
    u32 size;
    u32 data_off;
    u32 original_off;
} cro_segment_record;

typedef struct {
    u32 segment;
    u32 at;
    u32 kind;
    u32 signature;
    int offset;
} cro_fixup_record;

typedef struct cro_plugin {
    struct cro_plugin *next;
    u32 size;
    u32 num_targets;
    u32 num_signatures;
    u32 num_segments;
    u32 num_fixups;
    u32 targets_off;
    u32 signatures_off;
    u32 segments_off;
    u32 fixups_off;
} cro_plugin;

extern u8 plugins_image[PLUGIN_IMAGE_SIZE];

void plugins_cro_register(cro_plugin *plugin);
void plugins_cro_loaded(u32 base);

#endif
