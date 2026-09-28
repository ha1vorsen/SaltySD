#include "plugins.h"
#include "fs.h"
#include "status.h"
#include "common.h"

#include "types.h"

#define PLUGINS_ROOT      "/luma/titles/smash/engine"
#define PLUGIN_BYTES_MAX  0x10000
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
#define SEA_VERSION       1
#define SEA_VERSION_SIZE  4
#define NOTE_SIGNATURES   3
#define NOTE_PLACEMENT    4
#define NOTE_FIXUPS       5

#define SIGS_MAX          64
#define SIG_BYTES_MAX     64
#define SEGS_MAX          CLAIMS_MAX
#define PLACEMENT_SIZE    8
#define FIXUP_SIZE        20
#define FIXUP_BRANCH      1
#define FIXUP_ABS32       2
#define BRANCH_REACH      0x800000

enum {
    PLUGIN_OK,
    PLUGIN_READ,
    PLUGIN_TOO_BIG,
    PLUGIN_EMPTY,
    PLUGIN_NOT_ELF,
    PLUGIN_BAD_SEGMENT,
    PLUGIN_NO_ORIGINAL,
    PLUGIN_MISMATCH,
    PLUGIN_CONFLICT,
    PLUGIN_FULL,
    PLUGIN_INCOMPATIBLE,
    PLUGIN_MULTIPLE,
    PLUGIN_NO_SIGNATURE,
    PLUGIN_SIG_NOT_FOUND,
    PLUGIN_SIG_AMBIGUOUS,
    PLUGIN_BAD_FIXUP,
};

typedef struct {
    u32 start;
    u32 end;
} claim;

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
} signature;

typedef struct {
    u8 *data;
    u32 size;
    u32 addr;
} placed;

static toggle_entry entries[PLUGINS_MAX];
toggle_list plugins = { PLUGINS_ROOT, entries, PLUGINS_MAX, 0, 0 };

static u8 image[PLUGIN_BYTES_MAX];
static fs_entry batch[READ_BATCH];
static u16 path[PATH_CHARS];

static claim claims[CLAIMS_MAX];
static u32 num_claims;

static signature sigs[SIGS_MAX];
static u32 num_sigs;
static u32 buckets[256][SIGS_MAX / 32];
static placed segs[SEGS_MAX];
static u32 num_segs;

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

static int taken(u32 start, u32 end)
{
    u32 island = cro_fighter_new_ADDR + ISLAND_OFFS;
    if (overlaps(start, end, island, island + ISLAND_SIZE))
        return 1;
    for (u32 i = 0; i < num_reserved; i++)
        if (overlaps(start, end, reserved[i].addr, reserved[i].addr + reserved[i].len))
            return 1;
    for (u32 i = 0; i < num_claims; i++)
        if (overlaps(start, end, claims[i].start, claims[i].end))
            return 1;
    return 0;
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
            return 0;
        if (seg.size < ELF_NOTE_SIZE + NOTE_OWNER_SIZE)
            return 0;

        const u8 *note = f + seg.offset;
        const u8 *owner = note + ELF_NOTE_SIZE;
        u32 room = seg.size - ELF_NOTE_SIZE - NOTE_OWNER_SIZE;
        if (rd32(note + N_NAMESZ) != NOTE_OWNER_SIZE)
            return 0;
        if (!same_bytes(owner, NOTE_OWNER, NOTE_OWNER_SIZE))
            return 0;
        if (rd32(note + N_DESCSZ) > room)
            return 0;
        if (rd32(note + N_TYPE) != type)
            continue;
        if (found)
            return 0;

        *desc = owner + NOTE_OWNER_SIZE;
        *desc_len = rd32(note + N_DESCSZ);
        found = 1;
    }
    return found;
}

static int check_version(const u8 *f, u32 len, const elf_header *hdr)
{
    const u8 *desc;
    u32 desc_len;
    if (!find_note(f, len, hdr, NOTE_SEA_VERSION, &desc, &desc_len))
        return PLUGIN_INCOMPATIBLE;
    if (desc_len != SEA_VERSION_SIZE || rd32(desc) != SEA_VERSION)
        return PLUGIN_INCOMPATIBLE;
    return PLUGIN_OK;
}

static int read_signatures(const u8 *desc, u32 len)
{
    if (len < 4)
        return PLUGIN_NO_SIGNATURE;
    num_sigs = rd32(desc);
    if (!num_sigs || num_sigs > SIGS_MAX)
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
        for (u32 k = 0; k < SIGS_MAX / 32; k++)
            buckets[b][k] = 0;
    for (u32 i = 0; i < num_sigs; i++) {
        sigs[i].count = 0;
        buckets[sigs[i].anchor & 0xFF][i >> 5] |= 1u << (i & 31);
    }

    for (u32 at = CODE_START; at + 4 <= image_end; at += 4) {
        u32 word = *(const volatile u32 *)at;
        for (u32 k = 0; k < SIGS_MAX / 32; k++) {
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
        if (num_segs == SEGS_MAX)
            return PLUGIN_FULL;
        if (!in_file(num_segs * PLACEMENT_SIZE, PLACEMENT_SIZE, desc_len))
            return PLUGIN_NO_SIGNATURE;

        const u8 *p = desc + num_segs * PLACEMENT_SIZE;
        u32 sig = rd32(p);
        if (sig >= num_sigs)
            return PLUGIN_NO_SIGNATURE;
        u32 addr = sigs[sig].match + rd32(p + 4);
        if (addr < CODE_START || addr > image_end || seg.size > image_end - addr)
            return PLUGIN_BAD_SEGMENT;

        segs[num_segs].data = f + seg.offset;
        segs[num_segs].size = seg.size;
        segs[num_segs].addr = addr;
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

        u8 *word = segs[seg].data + at;
        u32 site = segs[seg].addr + at;
        u32 target = sigs[sig].match + rd32(r + 16);
        if (kind == FIXUP_ABS32) {
            wr32(word, target);
            continue;
        }
        if (kind != FIXUP_BRANCH || (site & 3) || (target & 3))
            return PLUGIN_BAD_FIXUP;

        u32 insn = rd32(word);
        if (((insn >> 25) & 7) != 5 || (insn >> 28) == 0xF)
            return PLUGIN_BAD_FIXUP;
        int disp = (int)(target - (site + 8)) >> 2;
        if (disp < -BRANCH_REACH || disp >= BRANCH_REACH)
            return PLUGIN_BAD_FIXUP;
        wr32(word, (insn & 0xFF000000) | ((u32)disp & 0xFFFFFF));
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

static int check_file(u8 *f, u32 len)
{
    elf_header hdr;
    int res = read_header(f, len, &hdr);
    if (res)
        return res;

    res = check_version(f, len, &hdr);
    if (res)
        return res;

    const u8 *orig;
    u32 orig_len;
    if (!find_note(f, len, &hdr, NOTE_ORIGINAL, &orig, &orig_len))
        return PLUGIN_NO_ORIGINAL;

    const u8 *sig_desc, *place_desc, *fix_desc;
    u32 sig_len, place_len, fix_len;
    if (!find_note(f, len, &hdr, NOTE_SIGNATURES, &sig_desc, &sig_len) ||
        !find_note(f, len, &hdr, NOTE_PLACEMENT, &place_desc, &place_len))
        return PLUGIN_NO_SIGNATURE;
    int has_fixups = find_note(f, len, &hdr, NOTE_FIXUPS, &fix_desc, &fix_len);

    res = read_signatures(sig_desc, sig_len);
    if (res)
        return res;
    res = locate();
    if (res)
        return res;
    res = place(f, len, &hdr, place_desc, place_len);
    if (res)
        return res;
    if (has_fixups) {
        res = apply_fixups(fix_desc, fix_len);
        if (res)
            return res;
    }

    u32 used = 0;
    for (u32 i = 0; i < num_segs; i++) {
        u32 addr = segs[i].addr, size = segs[i].size;
        if (size > orig_len - used)
            return PLUGIN_NO_ORIGINAL;
        if (taken(addr, addr + size))
            return PLUGIN_CONFLICT;
        if (!matches_memory(addr, orig + used, size))
            return PLUGIN_MISMATCH;
        used += size;

        if (num_claims == CLAIMS_MAX)
            return PLUGIN_FULL;
        claims[num_claims].start = addr;
        claims[num_claims].end = addr + size;
        num_claims++;
    }

    if (used != orig_len)
        return PLUGIN_NO_ORIGINAL;
    return PLUGIN_OK;
}

static void write_file(void)
{
    for (u32 i = 0; i < num_segs; i++) {
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

            if (batch[i].size > PLUGIN_BYTES_MAX)
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

static int load_one(const toggle_entry *e)
{
    u32 len = 0;
    int code = read_folder(e, &len);
    if (code)
        return code;

    u32 mark = num_claims;
    code = check_file(image, len);
    if (code) {
        num_claims = mark;
        return code;
    }

    write_file();
    return PLUGIN_OK;
}

void plugins_load(const SaltPatch *table, u32 count, u32 code_end)
{
    reserved = table;
    num_reserved = count;
    image_end = code_end;
    num_claims = 0;

    int res = fs_open();
    if (res < 0) {
        saltysd_status.plugins_result = res;
        return;
    }

    res = toggles_load(&plugins);
    saltysd_status.plugins_listed = plugins.count;
    if (res < 0)
        saltysd_status.plugins_result = res;

    for (u32 i = 0; i < plugins.count; i++) {
        if (!plugins.entries[i].enabled)
            continue;

        int code = load_one(&plugins.entries[i]);
        if (code) {
            saltysd_status.plugins_refused++;
            saltysd_status.plugins_result = code;
            if (code == PLUGIN_INCOMPATIBLE)
                saltysd_status.plugins_incompatible++;
        } else {
            saltysd_status.plugins_applied++;
        }
    }

    fs_close();
}
