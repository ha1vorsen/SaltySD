#include <assert.h>
#include <string.h>

#include "se/format.h"

typedef struct {
    se_package_note_v2 header;
    se_build_record_v2 build;
    se_variant_record_v2 variant;
    char strings[64];
} fixture;

static void make_fixture(fixture *value)
{
    memset(value, 0, sizeof(*value));
    value->header.magic = SE_PACKAGE_MAGIC;
    value->header.header_size = sizeof(value->header);
    value->header.total_size = sizeof(*value);
    value->header.builds.offset = sizeof(value->header);
    value->header.builds.count = 1;
    value->header.variants.offset = sizeof(value->header) + sizeof(value->build);
    value->header.variants.count = 1;
    value->header.strings_offset = sizeof(value->header) + sizeof(value->build) +
                                   sizeof(value->variant);
    value->header.strings_size = sizeof(value->strings);
    strcpy(value->strings + 1, "org.saltysd.test");
    strcpy(value->strings + 18, "Test package");
    strcpy(value->strings + 31, "SALT team");
    value->header.package_id = 1;
    value->header.display_name = 18;
    value->header.author = 31;
    value->header.version.major = 1;
    value->header.abi_min_major = 1;
    value->header.abi_max_major = 1;
    value->header.abi_max_minor = 2;
    value->header.capabilities_lo = SE_CAP_LOG;
    value->build.build_id = 0x1170001;
    value->build.title_id = 0x000EDF00;
    value->build.game_major = 1;
    value->build.game_minor = 1;
    value->build.region = 1;
    value->build.variant = 7;
    for (unsigned int i = 0; i < 32; i++)
        value->build.code_sha256[i] = (se_u8)i;
    value->variant.variant_id = 7;
    value->variant.segment_count = 1;
    value->variant.signature_count = 1;
    value->variant.init_segment = SE_INDEX_NONE;
    value->variant.cro_loaded_segment = SE_INDEX_NONE;
}

int main(void)
{
    fixture value;
    make_fixture(&value);
    se_package_view_v2 view;
    assert(se_format_parse_v2((const se_u8 *)&value, sizeof(value), &view) == PLUGIN_OK);
    assert(strcmp(view.package_id, "org.saltysd.test") == 0);

    se_host_header host = { SE_HOST_MAGIC, 1, 1, sizeof(se_host_v1), SE_CAP_LOG, 0 };
    assert(se_format_check_host_v2(&view, &host) == PLUGIN_OK);
    host.capabilities_lo = 0;
    assert(se_format_check_host_v2(&view, &host) == SE_ERROR_CAPABILITY_UNSUPPORTED);

    se_build_identity identity;
    memset(&identity, 0, sizeof(identity));
    identity.title_id = value.build.title_id;
    identity.game_major = value.build.game_major;
    identity.game_minor = value.build.game_minor;
    identity.region = value.build.region;
    identity.build_id = value.build.build_id;
    memcpy(identity.code_sha256, value.build.code_sha256, 32);
    se_build_selection_v2 selection;
    assert(se_format_select_build_v2(&view, &identity, &selection) == PLUGIN_OK);
    assert(selection.variant.variant_id == 7);
    identity.code_sha256[0] ^= 1;
    assert(se_format_select_build_v2(&view, &identity, &selection) == SE_ERROR_WRONG_BUILD);

    make_fixture(&value);
    value.header.strings_offset = value.header.builds.offset;
    assert(se_format_parse_v2((const se_u8 *)&value, sizeof(value), &view) ==
           PLUGIN_INCOMPATIBLE);
    make_fixture(&value);
    value.header.total_size--;
    assert(se_format_parse_v2((const se_u8 *)&value, sizeof(value), &view) ==
           PLUGIN_INCOMPATIBLE);
    make_fixture(&value);
    value.strings[1] = (char)0xC0;
    assert(se_format_parse_v2((const se_u8 *)&value, sizeof(value), &view) ==
           PLUGIN_INCOMPATIBLE);
    return 0;
}
