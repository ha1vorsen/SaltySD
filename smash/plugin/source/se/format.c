#include "format.h"

typedef struct {
    se_u32 start;
    se_u32 end;
} wire_range;

_Static_assert(sizeof(se_package_note_v2) == 112, "SEA 2 package header layout changed");
_Static_assert(sizeof(se_build_record_v2) == 52, "SEA 2 build record layout changed");
_Static_assert(sizeof(se_variant_record_v2) == 36, "SEA 2 variant record layout changed");
_Static_assert(sizeof(se_dependency_record_v2) == 24, "SEA 2 dependency layout changed");
_Static_assert(sizeof(se_conflict_record_v2) == 4, "SEA 2 conflict layout changed");
_Static_assert(sizeof(se_order_record_v2) == 8, "SEA 2 order layout changed");
_Static_assert(sizeof(se_import_record_v2) == 16, "SEA 2 import layout changed");
_Static_assert(sizeof(se_hook_record_v2) == 24, "SEA 2 hook layout changed");

static void copy_bytes(void *target, const void *source, se_u32 size)
{
    se_u8 *out = target;
    const se_u8 *in = source;
    for (se_u32 i = 0; i < size; i++)
        out[i] = in[i];
}

static int bytes_equal(const void *left, const void *right, se_u32 size)
{
    const se_u8 *a = left;
    const se_u8 *b = right;
    for (se_u32 i = 0; i < size; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int table_range(se_wire_table table, se_u32 item_size, se_u32 maximum,
                       se_u32 total, wire_range *out)
{
    if (table.count > maximum || (table.offset & 3))
        return 0;
    if (!table.count) {
        if (table.offset)
            return 0;
        out->start = out->end = 0;
        return 1;
    }
    if (table.count > 0xFFFFFFFFu / item_size)
        return 0;
    se_u32 bytes = table.count * item_size;
    if (table.offset > total || bytes > total - table.offset)
        return 0;
    out->start = table.offset;
    out->end = table.offset + bytes;
    return 1;
}

static int ranges_disjoint(const wire_range *ranges, se_u32 count)
{
    for (se_u32 a = 0; a < count; a++) {
        if (ranges[a].start == ranges[a].end)
            continue;
        for (se_u32 b = a + 1; b < count; b++) {
            if (ranges[b].start == ranges[b].end)
                continue;
            if (ranges[a].start < ranges[b].end && ranges[b].start < ranges[a].end)
                return 0;
        }
    }
    return 1;
}

static int valid_utf8(const se_u8 *text, se_u32 room)
{
    se_u32 at = 0;
    while (at < room && text[at]) {
        se_u8 lead = text[at++];
        if (lead < 0x80)
            continue;
        se_u32 continuation;
        se_u32 value;
        if (lead >= 0xC2 && lead <= 0xDF) {
            continuation = 1;
            value = lead & 0x1Fu;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            continuation = 2;
            value = lead & 0x0Fu;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            continuation = 3;
            value = lead & 7u;
        } else {
            return 0;
        }
        if (continuation > room - at)
            return 0;
        for (se_u32 i = 0; i < continuation; i++) {
            se_u8 next = text[at++];
            if ((next & 0xC0u) != 0x80u)
                return 0;
            value = (value << 6) | (next & 0x3Fu);
        }
        if ((continuation == 2 && value < 0x800) ||
            (continuation == 3 && value < 0x10000) ||
            value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF))
            return 0;
    }
    return at < room && text[at] == 0;
}

const char *se_format_string_v2(const se_package_view_v2 *view, se_u32 offset)
{
    if (offset >= view->header.strings_size)
        return 0;
    const se_u8 *text = view->data + view->header.strings_offset + offset;
    if (!valid_utf8(text, view->header.strings_size - offset))
        return 0;
    return (const char *)text;
}

static int validate_string_references(const se_package_view_v2 *view)
{
    if (!view->header.package_id || !view->header.display_name ||
        !se_format_string_v2(view, view->header.package_id) ||
        !se_format_string_v2(view, view->header.display_name) ||
        !se_format_string_v2(view, view->header.author))
        return 0;

    const se_dependency_record_v2 *dependencies =
        (const se_dependency_record_v2 *)(view->data + view->header.dependencies.offset);
    for (se_u32 i = 0; i < view->header.dependencies.count; i++) {
        if (!dependencies[i].package_id ||
            !se_format_string_v2(view, dependencies[i].package_id) ||
            dependencies[i].minimum.reserved || dependencies[i].maximum.reserved ||
            (dependencies[i].flags & ~SE_RELATION_OPTIONAL))
            return 0;
        for (se_u32 other = i + 1; other < view->header.dependencies.count; other++)
            if (dependencies[i].package_id == dependencies[other].package_id)
                return 0;
    }
    const se_conflict_record_v2 *conflicts =
        (const se_conflict_record_v2 *)(view->data + view->header.conflicts.offset);
    for (se_u32 i = 0; i < view->header.conflicts.count; i++) {
        if (!conflicts[i].package_id || !se_format_string_v2(view, conflicts[i].package_id))
            return 0;
        for (se_u32 other = i + 1; other < view->header.conflicts.count; other++)
            if (conflicts[i].package_id == conflicts[other].package_id)
                return 0;
    }
    const se_order_record_v2 *ordering =
        (const se_order_record_v2 *)(view->data + view->header.ordering.offset);
    for (se_u32 i = 0; i < view->header.ordering.count; i++) {
        if (!ordering[i].package_id || !se_format_string_v2(view, ordering[i].package_id) ||
            (ordering[i].kind != SE_ORDER_BEFORE && ordering[i].kind != SE_ORDER_AFTER))
            return 0;
        for (se_u32 other = i + 1; other < view->header.ordering.count; other++)
            if (ordering[i].package_id == ordering[other].package_id)
                return 0;
    }
    return 1;
}

static int validate_variants(const se_package_view_v2 *view)
{
    const se_variant_record_v2 *variants =
        (const se_variant_record_v2 *)(view->data + view->header.variants.offset);
    for (se_u32 i = 0; i < view->header.variants.count; i++) {
        const se_variant_record_v2 *variant = &variants[i];
        if (!variant->segment_count || !variant->signature_count ||
            variant->segment_count > 256 || variant->signature_count > 64 ||
            variant->first_segment > 256 - variant->segment_count ||
            variant->first_signature > 64 - variant->signature_count)
            return 0;
        if (variant->init_segment != SE_INDEX_NONE &&
            (variant->init_segment < variant->first_segment ||
             variant->init_segment >= variant->first_segment + variant->segment_count))
            return 0;
        if (variant->cro_loaded_segment != SE_INDEX_NONE &&
            (variant->cro_loaded_segment < variant->first_segment ||
             variant->cro_loaded_segment >= variant->first_segment + variant->segment_count))
            return 0;
        if ((variant->init_segment == SE_INDEX_NONE && variant->init_offset) ||
            (variant->cro_loaded_segment == SE_INDEX_NONE && variant->cro_loaded_offset))
            return 0;
        for (se_u32 other = i + 1; other < view->header.variants.count; other++)
            if (variant->variant_id == variants[other].variant_id)
                return 0;
    }
    return 1;
}

static int validate_builds(const se_package_view_v2 *view)
{
    const se_build_record_v2 *builds =
        (const se_build_record_v2 *)(view->data + view->header.builds.offset);
    const se_variant_record_v2 *variants =
        (const se_variant_record_v2 *)(view->data + view->header.variants.offset);
    for (se_u32 i = 0; i < view->header.builds.count; i++) {
        int found = 0;
        for (se_u32 v = 0; v < view->header.variants.count; v++)
            found += builds[i].variant == variants[v].variant_id;
        if (found != 1 || builds[i].reserved)
            return 0;
        for (se_u32 other = i + 1; other < view->header.builds.count; other++)
            if (builds[i].build_id == builds[other].build_id &&
                builds[i].title_id == builds[other].title_id &&
                builds[i].game_major == builds[other].game_major &&
                builds[i].game_minor == builds[other].game_minor &&
                builds[i].region == builds[other].region &&
                bytes_equal(builds[i].code_sha256, builds[other].code_sha256, 32))
                return 0;
    }
    return 1;
}

se_error se_format_parse_v2(const se_u8 *data, se_u32 size, se_package_view_v2 *out)
{
    if (!data || !out || size < sizeof(se_package_note_v2))
        return PLUGIN_INCOMPATIBLE;
    copy_bytes(&out->header, data, sizeof(out->header));
    out->data = data;
    out->size = size;
    if (out->header.magic != SE_PACKAGE_MAGIC || out->header.flags ||
        out->header.header_size != sizeof(se_package_note_v2) ||
        out->header.total_size != size || (out->header.strings_offset & 3) ||
        !out->header.strings_size || out->header.strings_offset > size ||
        out->header.strings_size > size - out->header.strings_offset)
        return PLUGIN_INCOMPATIBLE;

    wire_range ranges[9];
    ranges[0].start = 0;
    ranges[0].end = out->header.header_size;
    if (!table_range(out->header.builds, sizeof(se_build_record_v2), SE_V2_BUILDS_MAX,
                     size, &ranges[1]) ||
        !table_range(out->header.variants, sizeof(se_variant_record_v2), SE_V2_VARIANTS_MAX,
                     size, &ranges[2]) ||
        !table_range(out->header.dependencies, sizeof(se_dependency_record_v2),
                     SE_V2_DEPENDENCIES_MAX, size, &ranges[3]) ||
        !table_range(out->header.conflicts, sizeof(se_conflict_record_v2),
                     SE_V2_CONFLICTS_MAX, size, &ranges[4]) ||
        !table_range(out->header.ordering, sizeof(se_order_record_v2),
                     SE_V2_ORDERING_MAX, size, &ranges[5]) ||
        !table_range(out->header.imports, sizeof(se_import_record_v2),
                     SE_V2_IMPORTS_MAX, size, &ranges[6]) ||
        !table_range(out->header.hooks, sizeof(se_hook_record_v2),
                     SE_V2_HOOKS_MAX, size, &ranges[7]))
        return PLUGIN_INCOMPATIBLE;
    ranges[8].start = out->header.strings_offset;
    ranges[8].end = out->header.strings_offset + out->header.strings_size;
    if (!ranges_disjoint(ranges, 9) || !out->header.builds.count ||
        !out->header.variants.count || data[out->header.strings_offset] != 0)
        return PLUGIN_INCOMPATIBLE;

    out->package_id = se_format_string_v2(out, out->header.package_id);
    out->display_name = se_format_string_v2(out, out->header.display_name);
    out->author = se_format_string_v2(out, out->header.author);
    if (out->header.version.reserved ||
        !validate_string_references(out) || !validate_variants(out) || !validate_builds(out))
        return PLUGIN_INCOMPATIBLE;

    const se_import_record_v2 *imports =
        (const se_import_record_v2 *)(out->data + out->header.imports.offset);
    for (se_u32 i = 0; i < out->header.imports.count; i++) {
        if (imports[i].kind != SE_IMPORT_ABS32 && imports[i].kind != SE_IMPORT_ARM_VENEER)
            return PLUGIN_INCOMPATIBLE;
        for (se_u32 other = i + 1; other < out->header.imports.count; other++)
            if (imports[i].segment == imports[other].segment &&
                imports[i].offset == imports[other].offset)
                return PLUGIN_INCOMPATIBLE;
    }
    const se_hook_record_v2 *hooks =
        (const se_hook_record_v2 *)(out->data + out->header.hooks.offset);
    for (se_u32 i = 0; i < out->header.hooks.count; i++) {
        if (hooks[i].reserved ||
            (hooks[i].flags != SE_HOOK_EXCLUSIVE && hooks[i].flags != SE_HOOK_CHAINABLE))
            return PLUGIN_INCOMPATIBLE;
        for (se_u32 other = i + 1; other < out->header.hooks.count; other++)
            if (hooks[i].target_id == hooks[other].target_id)
                return PLUGIN_INCOMPATIBLE;
    }
    return PLUGIN_OK;
}

se_error se_format_select_build_v2(const se_package_view_v2 *view,
                                   const se_build_identity *identity,
                                   se_build_selection_v2 *out)
{
    const se_build_record_v2 *builds =
        (const se_build_record_v2 *)(view->data + view->header.builds.offset);
    se_u32 match = SE_INDEX_NONE;
    for (se_u32 i = 0; i < view->header.builds.count; i++) {
        const se_build_record_v2 *build = &builds[i];
        if (build->build_id == identity->build_id && build->title_id == identity->title_id &&
            build->game_major == identity->game_major &&
            build->game_minor == identity->game_minor && build->region == identity->region &&
            bytes_equal(build->code_sha256, identity->code_sha256, 32)) {
            if (match != SE_INDEX_NONE)
                return SE_ERROR_WRONG_BUILD;
            match = i;
        }
    }
    if (match == SE_INDEX_NONE)
        return SE_ERROR_WRONG_BUILD;

    const se_variant_record_v2 *variants =
        (const se_variant_record_v2 *)(view->data + view->header.variants.offset);
    se_u32 variant = SE_INDEX_NONE;
    for (se_u32 i = 0; i < view->header.variants.count; i++)
        if (variants[i].variant_id == builds[match].variant) {
            if (variant != SE_INDEX_NONE)
                return SE_ERROR_WRONG_BUILD;
            variant = i;
        }
    if (variant == SE_INDEX_NONE)
        return SE_ERROR_WRONG_BUILD;
    copy_bytes(&out->build, &builds[match], sizeof(out->build));
    copy_bytes(&out->variant, &variants[variant], sizeof(out->variant));
    out->build_index = match;
    out->variant_index = variant;
    return PLUGIN_OK;
}

static int abi_less(se_u16 major, se_u16 minor, se_u16 other_major, se_u16 other_minor)
{
    return major < other_major || (major == other_major && minor < other_minor);
}

se_error se_format_check_host_v2(const se_package_view_v2 *view,
                                 const se_host_header *host)
{
    if (!host || host->magic != SE_HOST_MAGIC ||
        abi_less(host->abi_major, host->abi_minor,
                 view->header.abi_min_major, view->header.abi_min_minor) ||
        abi_less(view->header.abi_max_major, view->header.abi_max_minor,
                 host->abi_major, host->abi_minor))
        return SE_ERROR_ABI_UNSUPPORTED;
    if ((view->header.capabilities_lo & ~host->capabilities_lo) ||
        (view->header.capabilities_hi & ~host->capabilities_hi))
        return SE_ERROR_CAPABILITY_UNSUPPORTED;
    return PLUGIN_OK;
}
