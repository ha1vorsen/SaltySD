#include "plugins_cro.h"
#include "plugins.h"
#include "status.h"

#define CRO_MAGIC_OFFS 0x80
#define CRO_NAME_OFFS 0x84
#define CRO_CODE_OFFS 0xB0
#define CRO_CODE_SIZE_OFFS 0xB4
#define CRO_MAGIC_LOADED 0x304F5243u
#define CRO_MAGIC_FIXED 0x44584946u

typedef struct {
    u32 start;
    u32 end;
} runtime_claim;

typedef struct {
    u8 *data;
    u32 size;
    u32 addr;
} runtime_segment;

static cro_plugin *first_plugin;
static cro_plugin *last_plugin;
static runtime_claim claims[PLUGIN_SEGS_MAX];
static u32 num_claims;
static runtime_segment placed[PLUGIN_SEGS_MAX];
static u32 buckets[256][PLUGIN_SIGS_MAX / 32];
static u32 counts[PLUGIN_SIGS_MAX];
static u32 matches[PLUGIN_SIGS_MAX];

static u32 rd32(const u8 *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24;
}

static void wr32(u8 *p, u32 v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static u32 lowest_bit(u32 x)
{
    return 31 - __builtin_clz(x & -x);
}

static int overlaps(u32 start, u32 end, u32 other_start, u32 other_end)
{
    return start < other_end && other_start < end;
}

static cro_target_record *plugin_targets(cro_plugin *plugin)
{
    return (cro_target_record *)((u8 *)plugin + plugin->targets_off);
}

static cro_signature_record *plugin_signatures(cro_plugin *plugin)
{
    return (cro_signature_record *)((u8 *)plugin + plugin->signatures_off);
}

static cro_segment_record *plugin_segments(cro_plugin *plugin)
{
    return (cro_segment_record *)((u8 *)plugin + plugin->segments_off);
}

static cro_fixup_record *plugin_fixups(cro_plugin *plugin)
{
    return (cro_fixup_record *)((u8 *)plugin + plugin->fixups_off);
}

static int same_name(const char *loaded, const u8 *stored, u32 len)
{
    for (u32 i = 0; i < len; i++)
        if ((u8)loaded[i] != stored[i])
            return 0;
    return !loaded[len];
}

static u32 find_target(cro_plugin *plugin, const char *name)
{
    cro_target_record *targets = plugin_targets(plugin);
    for (u32 i = 0; i < plugin->num_targets; i++)
        if (same_name(name, (u8 *)plugin + targets[i].name_off, targets[i].name_len))
            return i + 1;
    return 0;
}

static int sig_matches(cro_plugin *plugin, const cro_signature_record *sig, u32 start)
{
    const volatile u8 *at = (const volatile u8 *)start;
    const u8 *pattern = (u8 *)plugin + sig->pattern_off;
    const u8 *mask = (u8 *)plugin + sig->mask_off;
    for (u32 i = 0; i < sig->len; i++)
        if ((at[i] ^ pattern[i]) & mask[i])
            return 0;
    return 1;
}

static int locate(cro_plugin *plugin, u32 target, u32 code, u32 size)
{
    cro_signature_record *sigs = plugin_signatures(plugin);
    int scan = 0;
    for (u32 i = 0; i < plugin->num_signatures; i++) {
        if (sigs[i].target != target)
            continue;
        if (!sigs[i].cached) {
            scan = 1;
            continue;
        }
        u32 offset = sigs[i].cached_off;
        if (offset > size || sigs[i].len > size - offset ||
            !sig_matches(plugin, &sigs[i], code + offset))
            return PLUGIN_SIG_NOT_FOUND;
        sigs[i].match = code + offset;
    }
    if (!scan)
        return PLUGIN_OK;

    for (u32 b = 0; b < 256; b++)
        for (u32 k = 0; k < PLUGIN_SIGS_MAX / 32; k++)
            buckets[b][k] = 0;
    for (u32 i = 0; i < plugin->num_signatures; i++) {
        if (sigs[i].target != target)
            continue;
        counts[i] = 0;
        buckets[sigs[i].anchor & 0xFF][i >> 5] |= 1u << (i & 31);
    }

    for (u32 at = code; at + 4 <= code + size; at += 4) {
        u32 word = *(const volatile u32 *)at;
        for (u32 k = 0; k < PLUGIN_SIGS_MAX / 32; k++) {
            u32 bits = buckets[word & 0xFF][k];
            while (bits) {
                u32 i = (k << 5) | lowest_bit(bits);
                bits &= bits - 1;
                cro_signature_record *sig = &sigs[i];
                if (word != sig->anchor || at - code < sig->anchor_at)
                    continue;
                u32 start = at - sig->anchor_at;
                if (sig->len > code + size - start || !sig_matches(plugin, sig, start))
                    continue;
                if (!counts[i]++)
                    matches[i] = start;
            }
        }
    }

    for (u32 i = 0; i < plugin->num_signatures; i++) {
        if (sigs[i].target != target)
            continue;
        if (!counts[i])
            return PLUGIN_SIG_NOT_FOUND;
        if (counts[i] > 1)
            return PLUGIN_SIG_AMBIGUOUS;
        sigs[i].cached = 1;
        sigs[i].cached_off = matches[i] - code;
        sigs[i].match = matches[i];
    }
    return PLUGIN_OK;
}

static int prepare_segments(cro_plugin *plugin, u32 target, u32 code, u32 code_size)
{
    cro_signature_record *sigs = plugin_signatures(plugin);
    cro_segment_record *segs = plugin_segments(plugin);
    u32 used = 0;
    for (u32 i = 0; i < plugin->num_segments; i++) {
        placed[i].data = 0;
        placed[i].size = 0;
        placed[i].addr = 0;
        if (segs[i].target != target)
            continue;
        if (segs[i].signature >= plugin->num_signatures ||
            sigs[segs[i].signature].target != target)
            return PLUGIN_BAD_TARGET;
        u32 addr = sigs[segs[i].signature].match + (u32)segs[i].offset;
        if (addr < code || addr > code + code_size || segs[i].size > code + code_size - addr)
            return PLUGIN_BAD_SEGMENT;
        if (used > PLUGIN_IMAGE_SIZE || segs[i].size > PLUGIN_IMAGE_SIZE - used)
            return PLUGIN_TOO_BIG;
        placed[i].data = plugins_image + used;
        placed[i].size = segs[i].size;
        placed[i].addr = addr;
        const u8 *src = (u8 *)plugin + segs[i].data_off;
        for (u32 b = 0; b < segs[i].size; b++)
            plugins_image[used + b] = src[b];
        used += segs[i].size;
    }
    return PLUGIN_OK;
}

static int apply_fixups(cro_plugin *plugin, u32 target)
{
    cro_signature_record *sigs = plugin_signatures(plugin);
    cro_segment_record *segs = plugin_segments(plugin);
    cro_fixup_record *fixups = plugin_fixups(plugin);
    for (u32 i = 0; i < plugin->num_fixups; i++) {
        cro_fixup_record *fix = &fixups[i];
        if (fix->segment >= plugin->num_segments || segs[fix->segment].target != target)
            continue;
        if (fix->signature >= plugin->num_signatures || fix->at & 3 ||
            fix->at > placed[fix->segment].size || 4 > placed[fix->segment].size - fix->at)
            return PLUGIN_BAD_FIXUP;
        u8 *word = placed[fix->segment].data + fix->at;
        u32 site = placed[fix->segment].addr + fix->at;
        u32 dest = sigs[fix->signature].target;
        if (dest != 0 && dest != target)
            return PLUGIN_BAD_FIXUP;
        u32 target_addr = sigs[fix->signature].match + (u32)fix->offset;
        if (fix->kind == PLUGIN_FIXUP_ABS32) {
            wr32(word, target_addr);
            continue;
        }
        u32 insn = rd32(word);
        if (fix->kind != PLUGIN_FIXUP_BRANCH || site & 3 || target_addr & 3 ||
            ((insn >> 25) & 7) != 5 || (insn >> 28) == 0xF)
            return PLUGIN_BAD_FIXUP;
        int disp = (int)(target_addr - (site + 8)) >> 2;
        if (disp < -PLUGIN_BRANCH_REACH || disp >= PLUGIN_BRANCH_REACH)
            return PLUGIN_BAD_FIXUP;
        wr32(word, (insn & 0xFF000000) | ((u32)disp & 0xFFFFFF));
    }
    return PLUGIN_OK;
}

static int validate_segments(cro_plugin *plugin, u32 target)
{
    cro_segment_record *segs = plugin_segments(plugin);
    u32 mark = num_claims;
    for (u32 i = 0; i < plugin->num_segments; i++) {
        if (segs[i].target != target)
            continue;
        u32 start = placed[i].addr, end = start + placed[i].size;
        for (u32 c = 0; c < num_claims; c++)
            if (overlaps(start, end, claims[c].start, claims[c].end))
                return PLUGIN_CONFLICT;
        const volatile u8 *at = (const volatile u8 *)start;
        const u8 *original = (u8 *)plugin + segs[i].original_off;
        for (u32 b = 0; b < placed[i].size; b++)
            if (at[b] != original[b])
                return PLUGIN_MISMATCH;
        if (num_claims == PLUGIN_SEGS_MAX) {
            num_claims = mark;
            return PLUGIN_FULL;
        }
        claims[num_claims].start = start;
        claims[num_claims].end = end;
        num_claims++;
    }
    return PLUGIN_OK;
}

static void write_segments(cro_plugin *plugin, u32 target)
{
    cro_segment_record *segs = plugin_segments(plugin);
    for (u32 i = 0; i < plugin->num_segments; i++) {
        if (segs[i].target != target)
            continue;
        volatile u8 *at = (volatile u8 *)placed[i].addr;
        for (u32 b = 0; b < placed[i].size; b++)
            at[b] = placed[i].data[b];
    }
}

static int apply_one(cro_plugin *plugin, u32 target, u32 code, u32 size)
{
    u32 mark = num_claims;
    int result = locate(plugin, target, code, size);
    if (!result)
        result = prepare_segments(plugin, target, code, size);
    if (!result)
        result = apply_fixups(plugin, target);
    if (!result)
        result = validate_segments(plugin, target);
    if (result) {
        num_claims = mark;
        return result;
    }
    write_segments(plugin, target);
    return PLUGIN_OK;
}

void plugins_cro_register(cro_plugin *plugin)
{
    plugin->next = 0;
    if (last_plugin)
        last_plugin->next = plugin;
    else
        first_plugin = plugin;
    last_plugin = plugin;
}

void plugins_cro_loaded(u32 base)
{
    if (!base)
        return;
    u32 magic = *(const volatile u32 *)(base + CRO_MAGIC_OFFS);
    if (magic != CRO_MAGIC_LOADED && magic != CRO_MAGIC_FIXED)
        return;
    plugins_cro_prepare();
    u32 name_addr = *(const volatile u32 *)(base + CRO_NAME_OFFS);
    u32 code = *(const volatile u32 *)(base + CRO_CODE_OFFS);
    u32 size = *(const volatile u32 *)(base + CRO_CODE_SIZE_OFFS);
    if (name_addr < base)
        name_addr += base;
    if (code < base)
        code += base;

    num_claims = 0;
    u32 wrote = 0;
    for (cro_plugin *plugin = first_plugin; plugin; plugin = plugin->next) {
        u32 target = find_target(plugin, (const char *)name_addr);
        if (!target)
            continue;
        int result = apply_one(plugin, target, code, size);
        if (result) {
            saltysd_status.plugins_cro_refused++;
            saltysd_status.plugins_cro_result = result;
        } else {
            saltysd_status.plugins_cro_applied++;
            wrote = 1;
        }
    }
    if (wrote) {
        __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
        __asm__ volatile ("svc 0x94" ::: "r0", "r1", "r2", "r3", "r12", "memory");
    }
}
