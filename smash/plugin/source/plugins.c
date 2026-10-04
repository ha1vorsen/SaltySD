#include "plugins.h"
#include "plugins_cro.h"
#include "fs.h"
#include "status.h"
#include "common.h"
#include "se/claims.h"
#include "se/diagnostics.h"
#include "se/format.h"
#include "se/host_internal.h"
#include "se/plan.h"
#include "se/targets_internal.h"
#include "monocypher.h"

#include "types.h"

#define PLUGINS_ROOT      "/luma/titles/smash/engine"
#define CLAIMS_MAX        256
#define READ_BATCH        8
#define PATH_CHARS        0x101

#define CODE_START        0x100000
#define ISLAND_OFFS       0x3C4
#define ISLAND_SIZE       0x400

#define ELF_HEADER_SIZE   52
#define ELF_PHDR_SIZE     32
#define ELF_NOTE_SIZE     12
#define ELFCLASS32        1
#define ELFDATA2LSB       1
#define EV_CURRENT        1
#define ET_EXEC           2
#define EM_ARM            40
#define PT_LOAD           1
#define PT_NOTE           4

#define EI_CLASS          4
#define EI_DATA           5
#define EI_VERSION        6
#define E_TYPE            16
#define E_MACHINE         18
#define E_PHOFF           28
#define E_PHENTSIZE       42
#define E_PHNUM           44

#define P_TYPE            0
#define P_OFFSET          4
#define P_VADDR           8
#define P_FILESZ          16
#define P_MEMSZ           20

#define N_NAMESZ          0
#define N_DESCSZ          4
#define N_TYPE            8

#define NOTE_OWNER        "SaltySD"
#define NOTE_OWNER_SIZE   8
#define NOTE_ORIGINAL     1
#define NOTE_SEA_VERSION  2
#define NOTE_SIGNATURES   3
#define NOTE_PLACEMENT    4
#define NOTE_FIXUPS       5
#define NOTE_FEATURES     6
#define NOTE_TARGETS      7
#define NOTE_PACKAGE      SE_NOTE_PACKAGE
#define FEATURE_CRO       1
#define SEA_VERSION       0x00010002
#define SEA2_VERSION      SE_PACKAGE_VERSION

#define SIG_BYTES_MAX     64
#define PLACEMENT_SIZE    8
#define FIXUP_SIZE        20

typedef struct {
    u32 phoff;
    u32 phnum;
} elf_header;

typedef struct {
    u32 type;
    u32 offset;
    u32 vaddr;
    u32 size;
    u32 memsize;
} elf_segment;

typedef struct {
    const u8 *pattern;
    const u8 *mask;
    u32 len;
    u32 anchor;
    u32 anchor_at;
    u32 match;
    u32 count;
    u32 target;
} signature;

typedef struct {
    u8 *data;
    const u8 *original;
    u32 size;
    u32 addr;
    u32 signature;
    int offset;
    u32 target;
} placed;

typedef struct {
    const u8 *name;
    u32 len;
} sea_target;

typedef struct {
    u8 digest[32];
    u32 file_size;
    u32 signature_count;
    u32 matches[PLUGIN_SIGS_MAX];
    u16 plan_index;
    u8 sea2;
    u8 pending;
    se_variant_record_v2 variant;
    u32 init_address;
    u32 cro_loaded_address;
    u8 initialized;
} boot_package;

typedef struct {
    int planning;
    u16 owner;
    const u32 *matches;
    u32 match_count;
    se_plan *plan;
} check_options;

static toggle_entry entries[PLUGINS_MAX];
static u8 cro_pending[PLUGINS_MAX];
static u32 cro_pending_count;
static boot_package boot_packages[PLUGINS_MAX];
static se_plan boot_plan;
toggle_list plugins = { PLUGINS_ROOT, entries, PLUGINS_MAX, 0, 0 };

u8 plugins_image[PLUGIN_IMAGE_SIZE];
#define image plugins_image
static fs_entry batch[READ_BATCH];
static u16 path[PATH_CHARS];

static se_claim_range claim_storage[CLAIMS_MAX];
static se_claims claims;

static signature sigs[PLUGIN_SIGS_MAX];
static u32 num_sigs;
static u32 buckets[256][PLUGIN_SIGS_MAX / 32];
static placed segs[PLUGIN_SEGS_MAX];
static u32 num_segs;
static sea_target targets[PLUGIN_TARGETS_MAX];
static u32 num_targets;

static void *(*game_alloc)(u32 size) = (void *)liballoc_ADDR;

static const SaltPatch *reserved;
static u32 num_reserved;
static u32 image_end;

static u32 rd16(const u8 *p)
{
    return p[0] | p[1] << 8;
}

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

static u32 align4(u32 n)
{
    return (n + 3) & ~3u;
}

static u16 lower(u16 c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static int overlaps(u32 start, u32 end, u32 other_start, u32 other_end)
{
    return start < other_end && other_start < end;
}

static int in_file(u32 offset, u32 size, u32 file_len)
{
    return offset <= file_len && size <= file_len - offset;
}

static int has_elf_magic(const u8 *f)
{
    return f[0] == 0x7F && f[1] == 'E' && f[2] == 'L' && f[3] == 'F';
}

static int read_header(const u8 *f, u32 len, elf_header *hdr)
{
    if (len < ELF_HEADER_SIZE || !has_elf_magic(f))
        return PLUGIN_NOT_ELF;
    if (f[EI_CLASS] != ELFCLASS32)
        return PLUGIN_NOT_ELF;
    if (f[EI_DATA] != ELFDATA2LSB)
        return PLUGIN_NOT_ELF;
    if (f[EI_VERSION] != EV_CURRENT)
        return PLUGIN_NOT_ELF;
    if (rd16(f + E_TYPE) != ET_EXEC)
        return PLUGIN_NOT_ELF;
    if (rd16(f + E_MACHINE) != EM_ARM)
        return PLUGIN_NOT_ELF;
    if (rd16(f + E_PHENTSIZE) != ELF_PHDR_SIZE)
        return PLUGIN_NOT_ELF;

    hdr->phoff = rd32(f + E_PHOFF);
    hdr->phnum = rd16(f + E_PHNUM);
    if (!hdr->phnum)
        return PLUGIN_NOT_ELF;
    if (!in_file(hdr->phoff, hdr->phnum * ELF_PHDR_SIZE, len))
        return PLUGIN_NOT_ELF;
    return PLUGIN_OK;
}

static void read_segment(const u8 *f, const elf_header *hdr, u32 index, elf_segment *seg)
{
    const u8 *ph = f + hdr->phoff + index * ELF_PHDR_SIZE;
    seg->type = rd32(ph + P_TYPE);
    seg->offset = rd32(ph + P_OFFSET);
    seg->vaddr = rd32(ph + P_VADDR);
    seg->size = rd32(ph + P_FILESZ);
    seg->memsize = rd32(ph + P_MEMSZ);
}

static int same_bytes(const u8 *a, const char *b, u32 n)
{
    for (u32 i = 0; i < n; i++)
        if (a[i] != (u8)b[i])
            return 0;
    return 1;
}

static int find_note(const u8 *f, u32 len, const elf_header *hdr, u32 type,
                     const u8 **desc, u32 *desc_len)
{
    int found = 0;
    for (u32 i = 0; i < hdr->phnum; i++) {
        elf_segment seg;
        read_segment(f, hdr, i, &seg);
        if (seg.type != PT_NOTE)
            continue;
        if (!in_file(seg.offset, seg.size, len))
            return -1;
        if (seg.size < ELF_NOTE_SIZE + NOTE_OWNER_SIZE)
            return -1;

        const u8 *note = f + seg.offset;
        const u8 *owner = note + ELF_NOTE_SIZE;
        u32 room = seg.size - ELF_NOTE_SIZE - NOTE_OWNER_SIZE;
        if (rd32(note + N_NAMESZ) != NOTE_OWNER_SIZE)
            return -1;
        if (!same_bytes(owner, NOTE_OWNER, NOTE_OWNER_SIZE))
            return -1;
        if (rd32(note + N_DESCSZ) > room)
            return -1;
        if (rd32(note + N_TYPE) != type)
            continue;
        if (found)
            return -1;

        *desc = owner + NOTE_OWNER_SIZE;
        *desc_len = rd32(note + N_DESCSZ);
        found = 1;
    }
    return found;
}

static int read_signatures(const u8 *desc, u32 len)
{
    if (len < 4)
        return PLUGIN_NO_SIGNATURE;
    num_sigs = rd32(desc);
    if (!num_sigs || num_sigs > PLUGIN_SIGS_MAX)
        return PLUGIN_NO_SIGNATURE;

    u32 at = 4;
    for (u32 i = 0; i < num_sigs; i++) {
        if (!in_file(at, 4, len))
            return PLUGIN_NO_SIGNATURE;
        u32 n = rd32(desc + at);
        at += 4;
        if (n < 4 || n > SIG_BYTES_MAX || (n & 3) || !in_file(at, 2 * n, len))
            return PLUGIN_NO_SIGNATURE;

        signature *s = &sigs[i];
        s->pattern = desc + at;
        s->mask = desc + at + n;
        s->len = n;
        s->anchor_at = n;
        for (u32 w = 0; w < n; w += 4) {
            if (rd32(s->mask + w) == 0xFFFFFFFF) {
                s->anchor = rd32(s->pattern + w);
                s->anchor_at = w;
                break;
            }
        }
        if (s->anchor_at == n)
            return PLUGIN_NO_SIGNATURE;
        at += align4(2 * n);
    }

    if (at != len)
        return PLUGIN_NO_SIGNATURE;
    return PLUGIN_OK;
}

static int read_features(const u8 *f, u32 len, const elf_header *hdr, int *has_cro)
{
    const u8 *desc;
    u32 desc_len;
    int found = find_note(f, len, hdr, NOTE_FEATURES, &desc, &desc_len);
    if (found < 0 || (found && (desc_len & 3)))
        return PLUGIN_INCOMPATIBLE;
    *has_cro = 0;
    if (!found)
        return PLUGIN_OK;
    for (u32 at = 0; at < desc_len; at += 4) {
        u32 feature = rd32(desc + at);
        if (feature != FEATURE_CRO || *has_cro)
            return PLUGIN_INCOMPATIBLE;
        *has_cro = 1;
    }
    return PLUGIN_OK;
}

/* absent version note: SEA 1.1 compatibility */
static int read_version(const u8 *f, u32 len, const elf_header *hdr, u32 *version)
{
    const u8 *desc;
    u32 desc_len;
    int found = find_note(f, len, hdr, NOTE_SEA_VERSION, &desc, &desc_len);
    if (found < 0 || (found && desc_len != 4))
        return PLUGIN_INCOMPATIBLE;
    *version = found ? rd32(desc) : 0x00010001u;
    if (*version != 0x00010001u && *version != SEA_VERSION && *version != SEA2_VERSION)
        return PLUGIN_INCOMPATIBLE;
    return PLUGIN_OK;
}

static int read_targets(const u8 *f, u32 len, const elf_header *hdr, int has_cro)
{
    const u8 *desc;
    u32 desc_len;
    int found = find_note(f, len, hdr, NOTE_TARGETS, &desc, &desc_len);
    if (found < 0)
        return PLUGIN_BAD_TARGET;
    if (!has_cro) {
        if (found)
            return PLUGIN_BAD_TARGET;
        num_targets = 0;
        for (u32 i = 0; i < num_sigs; i++)
            sigs[i].target = 0;
        return PLUGIN_OK;
    }
    if (!found || desc_len < 4)
        return PLUGIN_BAD_TARGET;

    num_targets = rd32(desc);
    if (!num_targets || num_targets > PLUGIN_TARGETS_MAX)
        return PLUGIN_BAD_TARGET;
    u32 at = 4;
    for (u32 i = 0; i < num_targets; i++) {
        if (!in_file(at, 4, desc_len))
            return PLUGIN_BAD_TARGET;
        u32 name_len = rd32(desc + at);
        at += 4;
        if (!name_len || !in_file(at, name_len, desc_len))
            return PLUGIN_BAD_TARGET;
        for (u32 c = 0; c < name_len; c++)
            if (!desc[at + c] || desc[at + c] >= 0x80)
                return PLUGIN_BAD_TARGET;
        targets[i].name = desc + at;
        targets[i].len = name_len;
        at += align4(name_len);
        if (at > desc_len)
            return PLUGIN_BAD_TARGET;
    }
    if (desc_len - at != num_sigs * 4)
        return PLUGIN_BAD_TARGET;
    int used = 0;
    for (u32 i = 0; i < num_sigs; i++) {
        sigs[i].target = rd32(desc + at + i * 4);
        if (sigs[i].target > num_targets)
            return PLUGIN_BAD_TARGET;
        if (sigs[i].target)
            used = 1;
    }
    return used ? PLUGIN_OK : PLUGIN_BAD_TARGET;
}

static int sig_matches(const signature *s, u32 start)
{
    const volatile u8 *at = (const volatile u8 *)start;
    for (u32 i = 0; i < s->len; i++)
        if ((at[i] ^ s->pattern[i]) & s->mask[i])
            return 0;
    return 1;
}

static int locate(void)
{
    for (u32 b = 0; b < 256; b++)
        for (u32 k = 0; k < PLUGIN_SIGS_MAX / 32; k++)
            buckets[b][k] = 0;
    for (u32 i = 0; i < num_sigs; i++) {
        if (sigs[i].target)
            continue;
        sigs[i].count = 0;
        buckets[sigs[i].anchor & 0xFF][i >> 5] |= 1u << (i & 31);
    }

    for (u32 at = CODE_START; at + 4 <= image_end; at += 4) {
        u32 word = *(const volatile u32 *)at;
        for (u32 k = 0; k < PLUGIN_SIGS_MAX / 32; k++) {
            u32 bits = buckets[word & 0xFF][k];
            while (bits) {
                u32 i = (k << 5) | lowest_bit(bits);
                bits &= bits - 1;

                signature *s = &sigs[i];
                if (word != s->anchor || at - CODE_START < s->anchor_at)
                    continue;
                u32 start = at - s->anchor_at;
                if (s->len > image_end - start || !sig_matches(s, start))
                    continue;
                if (!s->count++)
                    s->match = start;
            }
        }
    }

    for (u32 i = 0; i < num_sigs; i++) {
        if (sigs[i].target)
            continue;
        if (!sigs[i].count)
            return PLUGIN_SIG_NOT_FOUND;
        if (sigs[i].count > 1)
            return PLUGIN_SIG_AMBIGUOUS;
    }
    return PLUGIN_OK;
}

static int place(u8 *f, u32 len, const elf_header *hdr, const u8 *desc, u32 desc_len)
{
    num_segs = 0;
    for (u32 i = 0; i < hdr->phnum; i++) {
        elf_segment seg;
        read_segment(f, hdr, i, &seg);
        if (seg.type != PT_LOAD)
            continue;

        if (!seg.size || seg.size != seg.memsize || !in_file(seg.offset, seg.size, len))
            return PLUGIN_BAD_SEGMENT;
        if (num_segs == PLUGIN_SEGS_MAX)
            return PLUGIN_FULL;
        if (!in_file(num_segs * PLACEMENT_SIZE, PLACEMENT_SIZE, desc_len))
            return PLUGIN_NO_SIGNATURE;

        const u8 *p = desc + num_segs * PLACEMENT_SIZE;
        u32 sig = rd32(p);
        if (sig >= num_sigs)
            return PLUGIN_NO_SIGNATURE;
        int offset = (int)rd32(p + 4);
        u32 target = sigs[sig].target;
        u32 addr = 0;
        if (!target) {
            addr = sigs[sig].match + (u32)offset;
            if (addr < CODE_START || addr > image_end || seg.size > image_end - addr)
                return PLUGIN_BAD_SEGMENT;
        }

        segs[num_segs].data = f + seg.offset;
        segs[num_segs].original = 0;
        segs[num_segs].size = seg.size;
        segs[num_segs].addr = addr;
        segs[num_segs].signature = sig;
        segs[num_segs].offset = offset;
        segs[num_segs].target = target;
        num_segs++;
    }

    if (!num_segs)
        return PLUGIN_BAD_SEGMENT;
    if (desc_len != num_segs * PLACEMENT_SIZE)
        return PLUGIN_NO_SIGNATURE;
    return PLUGIN_OK;
}

static int apply_fixups(const u8 *desc, u32 len)
{
    if (len < 4)
        return PLUGIN_BAD_FIXUP;
    u32 count = rd32(desc);
    if (count > (len - 4) / FIXUP_SIZE || len != 4 + count * FIXUP_SIZE)
        return PLUGIN_BAD_FIXUP;

    for (u32 i = 0; i < count; i++) {
        const u8 *r = desc + 4 + i * FIXUP_SIZE;
        u32 seg = rd32(r), at = rd32(r + 4), kind = rd32(r + 8), sig = rd32(r + 12);
        if (seg >= num_segs || sig >= num_sigs || (at & 3) || !in_file(at, 4, segs[seg].size))
            return PLUGIN_BAD_FIXUP;

        u32 source_target = segs[seg].target, dest_target = sigs[sig].target;
        if ((!source_target && dest_target) ||
            (source_target && dest_target && source_target != dest_target))
            return PLUGIN_BAD_FIXUP;

        u8 *word = segs[seg].data + at;
        if (kind == PLUGIN_FIXUP_ABS32) {
            if (!source_target)
                wr32(word, sigs[sig].match + rd32(r + 16));
            continue;
        }
        if (kind != PLUGIN_FIXUP_BRANCH)
            return PLUGIN_BAD_FIXUP;
        u32 insn = rd32(word);
        if (((insn >> 25) & 7) != 5 || (insn >> 28) == 0xF)
            return PLUGIN_BAD_FIXUP;
        if (source_target)
            continue;

        u32 site = segs[seg].addr + at;
        u32 target = sigs[sig].match + rd32(r + 16);
        if ((site & 3) || (target & 3))
            return PLUGIN_BAD_FIXUP;
        int disp = (int)(target - (site + 8)) >> 2;
        if (disp < -PLUGIN_BRANCH_REACH || disp >= PLUGIN_BRANCH_REACH)
            return PLUGIN_BAD_FIXUP;
        wr32(word, (insn & 0xFF000000) | ((u32)disp & 0xFFFFFF));
    }
    return PLUGIN_OK;
}

static int apply_host_imports(const u8 *f, u32 len, const elf_header *elf, u32 version)
{
    if (version != SEA2_VERSION)
        return PLUGIN_OK;
    const u8 *desc;
    u32 desc_len;
    if (find_note(f, len, elf, NOTE_PACKAGE, &desc, &desc_len) <= 0)
        return PLUGIN_INCOMPATIBLE;
    se_package_view_v2 view;
    int code = se_format_parse_v2(desc, desc_len, &view);
    if (code)
        return code;
    const se_import_record_v2 *imports =
        (const se_import_record_v2 *)(view.data + view.header.imports.offset);
    for (u32 i = 0; i < view.header.imports.count; i++) {
        const se_import_record_v2 *import = &imports[i];
        if (import->segment >= num_segs || segs[import->segment].target)
            return PLUGIN_BAD_FIXUP;
        u32 address;
        if (import->symbol_id == SE_HOST_IMPORT_GET_HOST)
            address = (u32)se_get_host;
        else
            return PLUGIN_BAD_FIXUP;
        if (import->kind == SE_IMPORT_ABS32) {
            if ((import->offset & 3) ||
                !in_file(import->offset, 4, segs[import->segment].size))
                return PLUGIN_BAD_FIXUP;
            wr32(segs[import->segment].data + import->offset, address);
        } else if (import->kind == SE_IMPORT_ARM_VENEER) {
            if ((import->offset & 3) ||
                !in_file(import->offset, 12, segs[import->segment].size))
                return PLUGIN_BAD_FIXUP;
            u8 *veneer = segs[import->segment].data + import->offset;
            if (rd32(veneer) != 0xE59FC000u || rd32(veneer + 4) != 0xE12FFF1Cu ||
                rd32(veneer + 8) != 0)
                return PLUGIN_BAD_FIXUP;
            wr32(veneer + 8, address);
        } else {
            return PLUGIN_BAD_FIXUP;
        }
    }
    return PLUGIN_OK;
}

static int matches_memory(u32 addr, const u8 *orig, u32 size)
{
    const volatile u8 *at = (const volatile u8 *)addr;
    for (u32 b = 0; b < size; b++)
        if (at[b] != orig[b])
            return 0;
    return 1;
}

static void copy_bytes(u8 *dest, const u8 *src, u32 size)
{
    for (u32 i = 0; i < size; i++)
        dest[i] = src[i];
}

static int retain_cro(const u8 *fix_desc, u32 fix_len, cro_plugin **out)
{
    u32 needed[PLUGIN_SIGS_MAX / 32] = { 0, 0 };
    u32 cro_segments = 0;
    for (u32 i = 0; i < num_segs; i++) {
        if (!segs[i].target)
            continue;
        cro_segments++;
        needed[segs[i].signature >> 5] |= 1u << (segs[i].signature & 31);
    }
    if (!cro_segments) {
        *out = 0;
        return PLUGIN_BAD_TARGET;
    }

    u32 cro_fixups = 0;
    u32 fix_count = fix_desc ? rd32(fix_desc) : 0;
    for (u32 i = 0; i < fix_count; i++) {
        const u8 *src = fix_desc + 4 + i * FIXUP_SIZE;
        u32 segment = rd32(src), signature = rd32(src + 12);
        if (segs[segment].target) {
            cro_fixups++;
            needed[signature >> 5] |= 1u << (signature & 31);
        }
    }

    u32 at = align4(sizeof(cro_plugin));
    u32 targets_off = at;
    at += num_targets * sizeof(cro_target_record);
    u32 signatures_off = at;
    at += num_sigs * sizeof(cro_signature_record);
    u32 segments_off = at;
    at += num_segs * sizeof(cro_segment_record);
    u32 fixups_off = at;
    at += cro_fixups * sizeof(cro_fixup_record);
    for (u32 i = 0; i < num_targets; i++)
        at += targets[i].len;
    for (u32 i = 0; i < num_sigs; i++)
        if ((needed[i >> 5] & (1u << (i & 31))) && sigs[i].target)
            at += 2 * sigs[i].len;
    for (u32 i = 0; i < num_segs; i++)
        if (segs[i].target)
            at += 2 * segs[i].size;

    cro_plugin *plugin = game_alloc(at);
    if (!plugin)
        return PLUGIN_NO_MEMORY;
    for (u32 i = 0; i < at; i++)
        ((u8 *)plugin)[i] = 0;
    plugin->size = at;
    plugin->num_targets = num_targets;
    plugin->num_signatures = num_sigs;
    plugin->num_segments = num_segs;
    plugin->num_fixups = cro_fixups;
    plugin->targets_off = targets_off;
    plugin->signatures_off = signatures_off;
    plugin->segments_off = segments_off;
    plugin->fixups_off = fixups_off;

    u32 data_at = fixups_off + cro_fixups * sizeof(cro_fixup_record);
    cro_target_record *stored_targets = (cro_target_record *)((u8 *)plugin + targets_off);
    for (u32 i = 0; i < num_targets; i++) {
        stored_targets[i].name_off = data_at;
        stored_targets[i].name_len = targets[i].len;
        copy_bytes((u8 *)plugin + data_at, targets[i].name, targets[i].len);
        data_at += targets[i].len;
    }

    cro_signature_record *stored_sigs =
        (cro_signature_record *)((u8 *)plugin + signatures_off);
    for (u32 i = 0; i < num_sigs; i++) {
        if (!(needed[i >> 5] & (1u << (i & 31)))) {
            stored_sigs[i].target = ~0u;
            continue;
        }
        stored_sigs[i].target = sigs[i].target;
        stored_sigs[i].len = sigs[i].len;
        stored_sigs[i].anchor = sigs[i].anchor;
        stored_sigs[i].anchor_at = sigs[i].anchor_at;
        if (!sigs[i].target) {
            stored_sigs[i].match = sigs[i].match;
            continue;
        }
        stored_sigs[i].pattern_off = data_at;
        copy_bytes((u8 *)plugin + data_at, sigs[i].pattern, sigs[i].len);
        data_at += sigs[i].len;
        stored_sigs[i].mask_off = data_at;
        copy_bytes((u8 *)plugin + data_at, sigs[i].mask, sigs[i].len);
        data_at += sigs[i].len;
    }

    cro_segment_record *stored_segs =
        (cro_segment_record *)((u8 *)plugin + segments_off);
    for (u32 i = 0; i < num_segs; i++) {
        stored_segs[i].target = segs[i].target;
        stored_segs[i].signature = segs[i].signature;
        stored_segs[i].offset = segs[i].offset;
        stored_segs[i].size = segs[i].size;
        if (!segs[i].target)
            continue;
        stored_segs[i].data_off = data_at;
        copy_bytes((u8 *)plugin + data_at, segs[i].data, segs[i].size);
        data_at += segs[i].size;
        stored_segs[i].original_off = data_at;
        copy_bytes((u8 *)plugin + data_at, segs[i].original, segs[i].size);
        data_at += segs[i].size;
    }

    cro_fixup_record *stored_fixups =
        (cro_fixup_record *)((u8 *)plugin + fixups_off);
    u32 stored = 0;
    for (u32 i = 0; i < fix_count; i++) {
        const u8 *src = fix_desc + 4 + i * FIXUP_SIZE;
        u32 segment = rd32(src);
        if (!segs[segment].target)
            continue;
        stored_fixups[stored].segment = segment;
        stored_fixups[stored].at = rd32(src + 4);
        stored_fixups[stored].kind = rd32(src + 8);
        stored_fixups[stored].signature = rd32(src + 12);
        stored_fixups[stored].offset = (int)rd32(src + 16);
        stored++;
    }
    if (data_at != at || stored != cro_fixups ||
        (fix_desc && fix_len != 4 + fix_count * FIXUP_SIZE))
        return PLUGIN_BAD_FIXUP;
    *out = plugin;
    return PLUGIN_OK;
}

static int check_file(u8 *f, u32 len, cro_plugin **retained,
                      const check_options *options)
{
    *retained = 0;
    elf_header hdr;
    int res = read_header(f, len, &hdr);
    if (res)
        return res;

    u32 version;
    res = read_version(f, len, &hdr, &version);
    if (res)
        return res;
    (void)version;

    int has_cro;
    res = read_features(f, len, &hdr, &has_cro);
    if (res)
        return res;

    const u8 *orig;
    u32 orig_len;
    if (find_note(f, len, &hdr, NOTE_ORIGINAL, &orig, &orig_len) <= 0)
        return PLUGIN_NO_ORIGINAL;

    const u8 *sig_desc, *place_desc, *fix_desc;
    u32 sig_len, place_len, fix_len;
    if (find_note(f, len, &hdr, NOTE_SIGNATURES, &sig_desc, &sig_len) <= 0 ||
        find_note(f, len, &hdr, NOTE_PLACEMENT, &place_desc, &place_len) <= 0)
        return PLUGIN_NO_SIGNATURE;
    int has_fixups = find_note(f, len, &hdr, NOTE_FIXUPS, &fix_desc, &fix_len);
    if (has_fixups < 0)
        return PLUGIN_BAD_FIXUP;
    if (!has_fixups) {
        fix_desc = 0;
        fix_len = 0;
    }

    res = read_signatures(sig_desc, sig_len);
    if (res)
        return res;
    res = read_targets(f, len, &hdr, has_cro);
    if (res)
        return res;
    if (options->matches) {
        if (options->match_count != num_sigs)
            return PLUGIN_NO_SIGNATURE;
        for (u32 i = 0; i < num_sigs; i++)
            sigs[i].match = options->matches[i];
    } else {
        res = locate();
        if (res)
            return res;
    }
    res = place(f, len, &hdr, place_desc, place_len);
    if (res)
        return res;
    if (has_fixups) {
        res = apply_fixups(fix_desc, fix_len);
        if (res)
            return res;
    }

    res = apply_host_imports(f, len, &hdr, version);
    if (res)
        return res;

    u32 used = 0;
    for (u32 i = 0; i < num_segs; i++) {
        u32 addr = segs[i].addr, size = segs[i].size;
        if (used > orig_len || size > orig_len - used)
            return PLUGIN_NO_ORIGINAL;
        segs[i].original = orig + used;
        used += size;
        if (segs[i].target)
            continue;
        u32 island = cro_fighter_new_ADDR + ISLAND_OFFS;
        if (overlaps(addr, addr + size, island, island + ISLAND_SIZE))
            return PLUGIN_CONFLICT;
        for (u32 r = 0; r < num_reserved; r++)
            if (overlaps(addr, addr + size, reserved[r].addr,
                         reserved[r].addr + reserved[r].len))
                return PLUGIN_CONFLICT;
        for (u32 earlier = 0; earlier < i; earlier++)
            if (!segs[earlier].target &&
                overlaps(addr, addr + size, segs[earlier].addr,
                         segs[earlier].addr + segs[earlier].size))
                return PLUGIN_CONFLICT;
        if (!options->planning && se_claims_overlap(&claims, addr, addr + size))
            return PLUGIN_CONFLICT;
        if (!matches_memory(addr, segs[i].original, size))
            return PLUGIN_MISMATCH;
    }

    if (used != orig_len)
        return PLUGIN_NO_ORIGINAL;
    for (u32 i = 0; i < num_segs; i++) {
        if (segs[i].target)
            continue;
        if (options->planning) {
            if (!se_plan_add_claim(options->plan, options->owner,
                                   segs[i].addr, segs[i].addr + segs[i].size))
                return PLUGIN_FULL;
        } else if (!se_claims_add(&claims, segs[i].addr,
                                  segs[i].addr + segs[i].size)) {
            return PLUGIN_FULL;
        }
    }
    if (has_cro && !options->planning) {
        res = retain_cro(fix_desc, fix_len, retained);
        if (res)
            return res;
    }
    return PLUGIN_OK;
}

static void write_file(void)
{
    for (u32 i = 0; i < num_segs; i++) {
        if (segs[i].target)
            continue;
        const u8 *src = segs[i].data;
        volatile u8 *at = (volatile u8 *)segs[i].addr;
        for (u32 b = 0; b < segs[i].size; b++)
            at[b] = src[b];
    }
}

static int is_sea_name(const u16 *name)
{
    const char *ext = ".sea";
    u32 len = 0;
    while (name[len])
        len++;
    if (len <= 4)
        return 0;

    const u16 *tail = name + len - 4;
    for (u32 i = 0; i < 4; i++)
        if (lower(tail[i]) != (u8)ext[i])
            return 0;
    return 1;
}

static int find_sea(u32 dir, u32 folder_len, u32 *size)
{
    int found = 0;
    u32 read;
    do {
        if (fs_dir_read(dir, batch, READ_BATCH, &read) < 0)
            return PLUGIN_READ;
        for (u32 i = 0; i < read; i++) {
            if (batch[i].attributes & FS_ATTR_DIR)
                continue;
            if (!is_sea_name(batch[i].name))
                continue;
            if (found)
                return PLUGIN_MULTIPLE;
            found = 1;

            u32 at = folder_len;
            path[at++] = '/';
            for (u32 c = 0; batch[i].name[c]; c++) {
                if (at == PATH_CHARS - 1)
                    return PLUGIN_READ;
                path[at++] = batch[i].name[c];
            }
            path[at] = 0;

            if (batch[i].size > PLUGIN_IMAGE_SIZE)
                return PLUGIN_TOO_BIG;
            *size = (u32)batch[i].size;
        }
    } while (read == READ_BATCH);

    if (!found)
        return PLUGIN_EMPTY;
    return PLUGIN_OK;
}

static int read_folder(const toggle_entry *e, u32 *len)
{
    u32 folder_len = toggles_folder_path(&plugins, e, path, PATH_CHARS);
    u32 dir;
    if (fs_dir_open(&dir, path) < 0)
        return PLUGIN_READ;
    int code = find_sea(dir, folder_len, len);
    fs_dir_close(dir);
    if (code)
        return code;

    u32 file, read = 0;
    if (fs_file_open_read(&file, path) < 0)
        return PLUGIN_READ;
    int res = fs_file_read(file, 0, image, *len, &read);
    fs_file_close(file);
    if (res < 0 || read != *len)
        return PLUGIN_READ;
    return PLUGIN_OK;
}

static void make_legacy_id(char out[SE_PACKAGE_ID_CHARS], u32 index)
{
    const char prefix[] = "legacy.pkg";
    u32 at = 0;
    while (prefix[at]) {
        out[at] = prefix[at];
        at++;
    }
    out[at++] = '0' + (index / 10) % 10;
    out[at++] = '0' + index % 10;
    out[at] = 0;
}

static u16 add_fallback_package(u32 index, int enabled)
{
    char id[SE_PACKAGE_ID_CHARS];
    make_legacy_id(id, index);
    se_version version = { 1, 1, 0 };
    int added = se_plan_add_package(&boot_plan, id, version, (u16)index, enabled);
    if (added < 0)
        return SE_PLAN_NONE;
    boot_packages[index].plan_index = (u16)added;
    se_diagnostics_package((u16)added, id, id,
                           enabled ? SE_PACKAGE_DISCOVERED : SE_PACKAGE_DISABLED);
    return (u16)added;
}

static se_version plan_version(se_wire_version value)
{
    se_version result = { value.major, value.minor, value.patch };
    return result;
}

static int add_v2_relations(se_package_view_v2 *view, u16 owner)
{
    const se_dependency_record_v2 *dependencies =
        (const se_dependency_record_v2 *)(view->data + view->header.dependencies.offset);
    for (u32 i = 0; i < view->header.dependencies.count; i++) {
        const char *id = se_format_string_v2(view, dependencies[i].package_id);
        if (!se_plan_add_dependency(&boot_plan, owner, id,
                                    plan_version(dependencies[i].minimum),
                                    plan_version(dependencies[i].maximum),
                                    dependencies[i].flags & SE_RELATION_OPTIONAL))
            return PLUGIN_FULL;
    }
    const se_conflict_record_v2 *conflicts =
        (const se_conflict_record_v2 *)(view->data + view->header.conflicts.offset);
    for (u32 i = 0; i < view->header.conflicts.count; i++)
        if (!se_plan_add_conflict(&boot_plan, owner,
                                  se_format_string_v2(view, conflicts[i].package_id)))
            return PLUGIN_FULL;
    const se_order_record_v2 *ordering =
        (const se_order_record_v2 *)(view->data + view->header.ordering.offset);
    for (u32 i = 0; i < view->header.ordering.count; i++) {
        const char *id = se_format_string_v2(view, ordering[i].package_id);
        int ok = ordering[i].kind == SE_ORDER_BEFORE ?
            se_plan_add_before(&boot_plan, owner, id) :
            se_plan_add_after(&boot_plan, owner, id);
        if (!ok)
            return PLUGIN_FULL;
    }
    return PLUGIN_OK;
}

static int catalog_file(u32 index, u8 *f, u32 len)
{
    elf_header elf;
    int code = read_header(f, len, &elf);
    u32 version = 0;
    if (!code)
        code = read_version(f, len, &elf, &version);
    if (code || version != SEA2_VERSION) {
        u16 package = add_fallback_package(index, 1);
        if (package == SE_PLAN_NONE)
            return PLUGIN_FULL;
        if (code)
            return code;
        boot_packages[index].sea2 = 0;
        return PLUGIN_OK;
    }

    const u8 *desc;
    u32 desc_len;
    int found = find_note(f, len, &elf, NOTE_PACKAGE, &desc, &desc_len);
    se_package_view_v2 view;
    if (found <= 0 || se_format_parse_v2(desc, desc_len, &view) != PLUGIN_OK) {
        if (add_fallback_package(index, 1) == SE_PLAN_NONE)
            return PLUGIN_FULL;
        return PLUGIN_INCOMPATIBLE;
    }

    se_version package_version = plan_version(view.header.version);
    int added = se_plan_add_package(&boot_plan, view.package_id, package_version,
                                    (u16)index, 1);
    if (added < 0)
        return PLUGIN_FULL;
    u16 owner = (u16)added;
    boot_packages[index].plan_index = owner;
    boot_packages[index].sea2 = 1;
    se_diagnostics_package(owner, view.package_id, view.display_name, SE_PACKAGE_DISCOVERED);
    if (boot_plan.packages[owner].state == SE_PACKAGE_REFUSED)
        return boot_plan.packages[owner].error;

    const se_host_v1 *host = se_get_host(SE_HOST_ABI_MAJOR);
    code = se_format_check_host_v2(&view, &host->header);
    if (code)
        return code;
    se_build_identity identity;
    if (host->get_build_identity(&identity, sizeof(identity)) < 0)
        return SE_ERROR_WRONG_BUILD;
    se_build_selection_v2 selection;
    code = se_format_select_build_v2(&view, &identity, &selection);
    if (code)
        return code;
    boot_packages[index].variant = selection.variant;
    return add_v2_relations(&view, owner);
}

static void file_identity(u8 digest[32], const u8 *data, u32 size)
{
    crypto_blake2b(digest, 32, data, size);
}

static int same_identity(const u8 left[32], const u8 right[32])
{
    for (u32 i = 0; i < 32; i++)
        if (left[i] != right[i])
            return 0;
    return 1;
}

static se_diagnostic_phase phase_for_error(int code)
{
    if (code == SE_ERROR_WRONG_BUILD || code == SE_ERROR_ABI_UNSUPPORTED ||
        code == SE_ERROR_CAPABILITY_UNSUPPORTED)
        return SE_PHASE_BUILD;
    if (code >= SE_ERROR_INVALID_ID && code <= SE_ERROR_EXPLICIT_CONFLICT)
        return SE_PHASE_DEPENDENCY;
    if (code == PLUGIN_SIG_NOT_FOUND || code == PLUGIN_SIG_AMBIGUOUS ||
        code == PLUGIN_NO_SIGNATURE)
        return SE_PHASE_SIGNATURE;
    if (code == PLUGIN_CONFLICT || code == SE_ERROR_STATIC_COLLISION)
        return SE_PHASE_CLAIM;
    return SE_PHASE_PARSING;
}

static int plan_file(u32 index, u32 len)
{
    boot_package *package = &boot_packages[index];
    package->file_size = len;
    file_identity(package->digest, image, len);
    int code = catalog_file(index, image, len);
    if (code)
        return code;

    u32 claim_mark = boot_plan.claim_count;
    u32 hook_mark = se_host_hook_mark();
    cro_plugin *ignored = 0;
    check_options options = { 1, package->plan_index, 0, 0, &boot_plan };
    code = check_file(image, len, &ignored, &options);
    if (code) {
        boot_plan.claim_count = claim_mark;
        se_host_hook_restore(hook_mark);
        return code;
    }
    if (package->sea2 &&
        (package->variant.first_segment != 0 || package->variant.segment_count != num_segs ||
         package->variant.first_signature != 0 ||
         package->variant.signature_count != num_sigs)) {
        boot_plan.claim_count = claim_mark;
        se_host_hook_restore(hook_mark);
        return PLUGIN_INCOMPATIBLE;
    }
    if (package->sea2 && package->variant.init_segment != SE_INDEX_NONE) {
        u32 segment = package->variant.init_segment;
        if (segment >= num_segs || segs[segment].target ||
            package->variant.init_offset >= segs[segment].size) {
            boot_plan.claim_count = claim_mark;
            se_host_hook_restore(hook_mark);
            return PLUGIN_BAD_SEGMENT;
        }
        package->init_address = segs[segment].addr + package->variant.init_offset;
    }
    if (package->sea2 && package->variant.cro_loaded_segment != SE_INDEX_NONE) {
        u32 segment = package->variant.cro_loaded_segment;
        if (segment >= num_segs || segs[segment].target ||
            package->variant.cro_loaded_offset >= segs[segment].size) {
            boot_plan.claim_count = claim_mark;
            se_host_hook_restore(hook_mark);
            return PLUGIN_BAD_SEGMENT;
        }
        package->cro_loaded_address =
            segs[segment].addr + package->variant.cro_loaded_offset;
    }
    package->signature_count = num_sigs;
    for (u32 i = 0; i < num_sigs; i++)
        package->matches[i] = sigs[i].match;

    if (package->sea2) {
        elf_header elf;
        const u8 *desc;
        u32 desc_len;
        if (read_header(image, len, &elf) ||
            find_note(image, len, &elf, NOTE_PACKAGE, &desc, &desc_len) <= 0) {
            boot_plan.claim_count = claim_mark;
            se_host_hook_restore(hook_mark);
            return PLUGIN_INCOMPATIBLE;
        }
        se_package_view_v2 view;
        if (se_format_parse_v2(desc, desc_len, &view) != PLUGIN_OK) {
            boot_plan.claim_count = claim_mark;
            se_host_hook_restore(hook_mark);
            return PLUGIN_INCOMPATIBLE;
        }
        const se_hook_record_v2 *hooks =
            (const se_hook_record_v2 *)(view.data + view.header.hooks.offset);
        for (u32 i = 0; i < view.header.hooks.count; i++) {
            const se_hook_record_v2 *hook = &hooks[i];
            se_target target;
            if (hook->flags != SE_HOOK_EXCLUSIVE || hook->handler_segment >= num_segs ||
                segs[hook->handler_segment].target ||
                hook->handler_offset >= segs[hook->handler_segment].size ||
                se_targets_resolve(hook->target_id, &target) < 0 ||
                (target.kind != SE_TARGET_POINTER_SLOT && target.kind != SE_TARGET_TABLE) ||
                target.span != 4 || (target.address & 3)) {
                boot_plan.claim_count = claim_mark;
                se_host_hook_restore(hook_mark);
                return SE_ERROR_HOOK_UNSUPPORTED;
            }
            u32 handler = segs[hook->handler_segment].addr + hook->handler_offset;
            if ((handler & 3) ||
                !se_plan_add_claim(&boot_plan, package->plan_index,
                                   target.address, target.address + target.span) ||
                se_host_allow_hook(package->plan_index + 1, hook->target_id,
                                   handler, hook->flags) < 0) {
                boot_plan.claim_count = claim_mark;
                se_host_hook_restore(hook_mark);
                return PLUGIN_FULL;
            }
        }
    }
    return PLUGIN_OK;
}

static int commit_file(u32 index)
{
    boot_package *package = &boot_packages[index];
    u32 len = 0;
    int code = read_folder(&plugins.entries[index], &len);
    if (code)
        return code;
    u8 digest[32];
    file_identity(digest, image, len);
    if (len != package->file_size || !same_identity(digest, package->digest))
        return SE_ERROR_FILE_CHANGED;

    u32 mark = se_claims_mark(&claims);
    cro_plugin *retained = 0;
    check_options options = { 0, package->plan_index, package->matches,
                              package->signature_count, &boot_plan };
    code = check_file(image, len, &retained, &options);
    if (code) {
        se_claims_restore(&claims, mark);
        return code;
    }
    write_file();
    if (retained)
        plugins_cro_register(retained);
    return PLUGIN_OK;
}

static int package_id_equal(const char *left, const char *right)
{
    while (*left && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

static int find_plan_package(const char *id)
{
    for (u32 i = 0; i < boot_plan.package_count; i++)
        if (package_id_equal(boot_plan.packages[i].id, id))
            return (int)i;
    return -1;
}

static int dependencies_committed(u16 package)
{
    for (u32 i = 0; i < boot_plan.dependency_count; i++) {
        const se_plan_dependency *dependency = &boot_plan.dependencies[i];
        if (dependency->owner != package || dependency->optional)
            continue;
        int target = find_plan_package(dependency->target);
        if (target < 0 || boot_plan.packages[target].state == SE_PACKAGE_REFUSED)
            return -1;
        if (boot_plan.packages[target].state != SE_PACKAGE_APPLIED &&
            boot_plan.packages[target].state != SE_PACKAGE_ACTIVE)
            return 0;
    }
    return 1;
}

static void maintain_caches(void)
{
    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
    __asm__ volatile ("svc 0x94" ::: "r0", "r1", "r2", "r3", "r12", "memory");
}

static void initialize_ready(void)
{
    maintain_caches();
    const se_host_v1 *host = se_get_host(SE_HOST_ABI_MAJOR);
    for (u32 ordered = 0; ordered < boot_plan.order_count; ordered++) {
        u16 package = boot_plan.order[ordered];
        se_plan_package *planned = &boot_plan.packages[package];
        u32 index = planned->discovery_index;
        boot_package *record = &boot_packages[index];
        if (!record->sea2 || record->initialized || planned->state != SE_PACKAGE_APPLIED)
            continue;
        int dependencies = dependencies_committed(package);
        if (dependencies <= 0)
            continue;
        if (record->init_address) {
            se_init_fn init = (se_init_fn)record->init_address;
            int result = init(host, package + 1);
            if (result) {
                se_plan_refuse(&boot_plan, package, PLUGIN_INCOMPATIBLE, SE_PLAN_NONE);
                se_plan_propagate_refusals(&boot_plan);
                se_diagnostics_emit(package, SE_PHASE_INIT, PLUGIN_INCOMPATIBLE,
                                    SE_PLAN_NONE, (u32)result, 0);
                saltysd_status.plugins_result = result;
                continue;
            }
        }
        record->initialized = 1;
        planned->state = SE_PACKAGE_ACTIVE;
        se_diagnostics_state(package, SE_PACKAGE_ACTIVE);
    }
}

static void refresh_status(void)
{
    saltysd_status.plugins_applied = 0;
    saltysd_status.plugins_pending = 0;
    saltysd_status.plugins_refused = 0;
    saltysd_status.plugins_incompatible = 0;
    for (u32 i = 0; i < boot_plan.package_count; i++) {
        const se_plan_package *package = &boot_plan.packages[i];
        if (package->state == SE_PACKAGE_APPLIED || package->state == SE_PACKAGE_ACTIVE)
            saltysd_status.plugins_applied++;
        else if (package->state == SE_PACKAGE_PENDING)
            saltysd_status.plugins_pending++;
        else if (package->state == SE_PACKAGE_REFUSED) {
            saltysd_status.plugins_refused++;
            if (package->error == PLUGIN_INCOMPATIBLE ||
                package->error == SE_ERROR_ABI_UNSUPPORTED ||
                package->error == SE_ERROR_CAPABILITY_UNSUPPORTED)
                saltysd_status.plugins_incompatible++;
        }
    }
}

void plugins_cro_prepare(void)
{
    if (!cro_pending_count)
        return;
    int result = fs_open();
    if (result < 0) {
        saltysd_status.plugins_cro_result = result;
        return;
    }
    for (u32 ordered = 0; ordered < boot_plan.order_count; ordered++) {
        u16 package = boot_plan.order[ordered];
        u32 i = boot_plan.packages[package].discovery_index;
        if (!cro_pending[i] || dependencies_committed(package) <= 0)
            continue;
        int code = commit_file(i);
        if (code == PLUGIN_NO_MEMORY) {
            saltysd_status.plugins_cro_result = code;
            continue;
        }
        cro_pending[i] = 0;
        boot_packages[i].pending = 0;
        cro_pending_count--;
        if (code) {
            se_plan_refuse(&boot_plan, package, (se_error)code, SE_PLAN_NONE);
            se_plan_propagate_refusals(&boot_plan);
            se_diagnostics_emit(package, SE_PHASE_COMMIT, (se_error)code,
                                SE_PLAN_NONE, 0, 0);
            saltysd_status.plugins_cro_refused++;
            saltysd_status.plugins_cro_result = code;
        } else {
            boot_plan.packages[package].state = SE_PACKAGE_APPLIED;
            se_diagnostics_state(package, SE_PACKAGE_APPLIED);
        }
    }
    initialize_ready();
    refresh_status();
    fs_close();
}

void plugins_load(const SaltPatch *table, u32 count, u32 code_end)
{
    reserved = table;
    num_reserved = count;
    image_end = code_end;
    se_claims_init(&claims, claim_storage, CLAIMS_MAX);
    se_plan_init(&boot_plan);
    se_diagnostics_reset();
    se_host_reset();
    cro_pending_count = 0;
    for (u32 i = 0; i < PLUGINS_MAX; i++) {
        cro_pending[i] = 0;
        boot_packages[i].pending = 0;
        boot_packages[i].plan_index = SE_PLAN_NONE;
        boot_packages[i].init_address = 0;
        boot_packages[i].cro_loaded_address = 0;
        boot_packages[i].initialized = 0;
    }

    int result = fs_open();
    if (result < 0) {
        saltysd_status.plugins_result = result;
        return;
    }
    result = toggles_load(&plugins);
    saltysd_status.plugins_listed = plugins.count;
    if (result < 0)
        saltysd_status.plugins_result = result;

    for (u32 i = 0; i < plugins.count; i++) {
        se_host_register_package(i + 1, plugins.entries[i].name);
        if (!plugins.entries[i].enabled) {
            if (add_fallback_package(i, 0) == SE_PLAN_NONE)
                saltysd_status.plugins_result = PLUGIN_FULL;
            continue;
        }
        u32 len = 0;
        int code = read_folder(&plugins.entries[i], &len);
        if (!code)
            code = plan_file(i, len);
        if (boot_packages[i].plan_index == SE_PLAN_NONE)
            add_fallback_package(i, 1);
        if (code) {
            u16 package = boot_packages[i].plan_index;
            se_plan_refuse(&boot_plan, package, (se_error)code, SE_PLAN_NONE);
            se_diagnostics_emit(package, phase_for_error(code), (se_error)code,
                                SE_PLAN_NONE, 0, 0);
            saltysd_status.plugins_result = code;
        }
    }

    se_plan_run(&boot_plan);
    for (u32 i = 0; i < boot_plan.package_count; i++) {
        if (boot_plan.packages[i].state == SE_PACKAGE_REFUSED &&
            boot_plan.packages[i].error != PLUGIN_OK)
            se_diagnostics_emit((u16)i, phase_for_error(boot_plan.packages[i].error),
                                (se_error)boot_plan.packages[i].error,
                                boot_plan.packages[i].peer, 0, 0);
    }

    for (u32 ordered = 0; ordered < boot_plan.order_count; ordered++) {
        u16 package = boot_plan.order[ordered];
        if (boot_plan.packages[package].state != SE_PACKAGE_PLANNED)
            continue;
        u32 index = boot_plan.packages[package].discovery_index;
        int dependencies = dependencies_committed(package);
        if (dependencies < 0) {
            se_plan_refuse(&boot_plan, package, SE_ERROR_DEPENDENCY_REFUSED, SE_PLAN_NONE);
            continue;
        }
        if (!dependencies) {
            boot_plan.packages[package].state = SE_PACKAGE_PENDING;
            boot_packages[index].pending = 1;
            cro_pending[index] = 1;
            cro_pending_count++;
            se_diagnostics_state(package, SE_PACKAGE_PENDING);
            continue;
        }
        int code = commit_file(index);
        if (code == PLUGIN_NO_MEMORY) {
            boot_plan.packages[package].state = SE_PACKAGE_PENDING;
            boot_packages[index].pending = 1;
            cro_pending[index] = 1;
            cro_pending_count++;
            se_diagnostics_state(package, SE_PACKAGE_PENDING);
            continue;
        }
        if (code) {
            se_plan_refuse(&boot_plan, package, (se_error)code, SE_PLAN_NONE);
            se_plan_propagate_refusals(&boot_plan);
            se_diagnostics_emit(package, SE_PHASE_COMMIT, (se_error)code,
                                SE_PLAN_NONE, 0, 0);
            saltysd_status.plugins_result = code;
            continue;
        }
        boot_plan.packages[package].state = SE_PACKAGE_APPLIED;
        se_diagnostics_state(package, SE_PACKAGE_APPLIED);
    }
    initialize_ready();
    refresh_status();
    fs_close();
}

void plugins_lifecycle_cro_loaded(const char *name, u32 base)
{
    for (u32 ordered = 0; ordered < boot_plan.order_count; ordered++) {
        u16 package = boot_plan.order[ordered];
        se_plan_package *planned = &boot_plan.packages[package];
        boot_package *record = &boot_packages[planned->discovery_index];
        if (planned->state != SE_PACKAGE_ACTIVE || !record->cro_loaded_address)
            continue;
        se_cro_loaded_fn callback = (se_cro_loaded_fn)record->cro_loaded_address;
        callback(package + 1, name, base);
    }
}
