#ifndef SALTYSD_SE_PACKAGE_H
#define SALTYSD_SE_PACKAGE_H

#include <se/types.h>

#define SE_PACKAGE_MAGIC 0x32414553u /* SEA2 */
#define SE_PACKAGE_VERSION 0x00020000u

#define SE_NOTE_PACKAGE 0x200u
#define SE_FEATURE_HOST_ABI 2u

#define SE_RELATION_OPTIONAL 0x00000001u
#define SE_ORDER_BEFORE 1u
#define SE_ORDER_AFTER 2u

#define SE_IMPORT_ABS32 1u
#define SE_IMPORT_ARM_VENEER 2u

typedef struct {
    se_u16 major;
    se_u16 minor;
    se_u16 patch;
    se_u16 reserved;
} se_wire_version;

typedef struct {
    se_u32 offset;
    se_u32 count;
} se_wire_table;

typedef struct {
    se_u32 magic;
    se_u16 header_size;
    se_u16 flags;
    se_u32 total_size;
    se_u32 strings_offset;
    se_u32 strings_size;
    se_u32 package_id;
    se_u32 display_name;
    se_u32 author;
    se_wire_version version;
    se_u16 abi_min_major;
    se_u16 abi_min_minor;
    se_u16 abi_max_major;
    se_u16 abi_max_minor;
    se_u32 capabilities_lo;
    se_u32 capabilities_hi;
    se_wire_table builds;
    se_wire_table variants;
    se_wire_table dependencies;
    se_wire_table conflicts;
    se_wire_table ordering;
    se_wire_table imports;
    se_wire_table hooks;
} se_package_note_v2;

typedef struct {
    se_u32 build_id;
    se_u32 title_id;
    se_u16 game_major;
    se_u16 game_minor;
    se_u16 region;
    se_u16 reserved;
    se_u32 variant;
    se_u8 code_sha256[32];
} se_build_record_v2;

typedef struct {
    se_u32 variant_id;
    se_u32 first_segment;
    se_u32 segment_count;
    se_u32 first_signature;
    se_u32 signature_count;
    se_u32 init_segment;
    se_u32 init_offset;
    se_u32 cro_loaded_segment;
    se_u32 cro_loaded_offset;
} se_variant_record_v2;

typedef struct {
    se_u32 package_id;
    se_wire_version minimum;
    se_wire_version maximum;
    se_u32 flags;
} se_dependency_record_v2;

typedef struct {
    se_u32 package_id;
} se_conflict_record_v2;

typedef struct {
    se_u32 package_id;
    se_u32 kind;
} se_order_record_v2;

typedef struct {
    se_u32 symbol_id;
    se_u32 segment;
    se_u32 offset;
    se_u32 kind;
} se_import_record_v2;

typedef struct {
    se_u32 target_id;
    se_u32 handler_segment;
    se_u32 handler_offset;
    se_u32 flags;
    se_s32 priority;
    se_u32 reserved;
} se_hook_record_v2;

#endif
