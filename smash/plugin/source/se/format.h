#ifndef SALTYSD_SE_FORMAT_H
#define SALTYSD_SE_FORMAT_H

#include <se/host.h>
#include <se/package.h>

#include "se/errors.h"

#define SE_V2_BUILDS_MAX 16u
#define SE_V2_VARIANTS_MAX 16u
#define SE_V2_DEPENDENCIES_MAX 64u
#define SE_V2_CONFLICTS_MAX 64u
#define SE_V2_ORDERING_MAX 64u
#define SE_V2_IMPORTS_MAX 128u
#define SE_V2_HOOKS_MAX 64u

typedef struct {
    const se_u8 *data;
    se_u32 size;
    se_package_note_v2 header;
    const char *package_id;
    const char *display_name;
    const char *author;
} se_package_view_v2;

typedef struct {
    se_build_record_v2 build;
    se_variant_record_v2 variant;
    se_u32 build_index;
    se_u32 variant_index;
} se_build_selection_v2;

se_error se_format_parse_v2(const se_u8 *data, se_u32 size, se_package_view_v2 *out);
const char *se_format_string_v2(const se_package_view_v2 *view, se_u32 offset);
se_error se_format_select_build_v2(const se_package_view_v2 *view,
                                   const se_build_identity *identity,
                                   se_build_selection_v2 *out);
se_error se_format_check_host_v2(const se_package_view_v2 *view,
                                 const se_host_header *host);

#endif
