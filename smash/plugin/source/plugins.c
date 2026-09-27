#include "plugins.h"
#include "fs.h"
#include "status.h"
#include "common.h"

#include "types.h"

#define PLUGINS_ROOT      "/luma/titles/smash/plugins"
#define PLUGIN_BYTES_MAX  0x10000
#define PLUGIN_FILES_MAX  8
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

static toggle_entry entries[PLUGINS_MAX];
toggle_list plugins = { PLUGINS_ROOT, entries, PLUGINS_MAX, 0, 0 };

static u8 image[PLUGIN_BYTES_MAX];
static u32 file_start[PLUGIN_FILES_MAX];
static u32 file_size[PLUGIN_FILES_MAX];
static fs_entry batch[READ_BATCH];
static u16 path[PATH_CHARS];

static claim claims[CLAIMS_MAX];
static u32 num_claims;

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

static int find_original(const u8 *f, u32 len, const elf_header *hdr, const u8 **desc, u32 *desc_len)
{
    int found = 0;
    for (u32 i = 0; i < hdr->phnum; i++) {
        elf_segment seg;
        read_segment(f, hdr, i, &seg);
        if (seg.type != PT_NOTE)
            continue;
        if (found)
            return PLUGIN_NO_ORIGINAL;
        if (!in_file(seg.offset, seg.size, len))
            return PLUGIN_NO_ORIGINAL;
        if (seg.size < ELF_NOTE_SIZE + NOTE_OWNER_SIZE)
            return PLUGIN_NO_ORIGINAL;

        const u8 *note = f + seg.offset;
        const u8 *owner = note + ELF_NOTE_SIZE;
        u32 room = seg.size - ELF_NOTE_SIZE - NOTE_OWNER_SIZE;
        if (rd32(note + N_NAMESZ) != NOTE_OWNER_SIZE)
            return PLUGIN_NO_ORIGINAL;
        if (rd32(note + N_TYPE) != NOTE_ORIGINAL)
            return PLUGIN_NO_ORIGINAL;
        if (rd32(note + N_DESCSZ) > room)
            return PLUGIN_NO_ORIGINAL;
        if (!same_bytes(owner, NOTE_OWNER, NOTE_OWNER_SIZE))
            return PLUGIN_NO_ORIGINAL;

        *desc = owner + NOTE_OWNER_SIZE;
        *desc_len = rd32(note + N_DESCSZ);
        found = 1;
    }

    if (!found)
        return PLUGIN_NO_ORIGINAL;
    return PLUGIN_OK;
}

static int check_segment(const elf_segment *seg, u32 len)
{
    if (!seg->size)
        return PLUGIN_BAD_SEGMENT;
    if (seg->size != seg->memsize)
        return PLUGIN_BAD_SEGMENT;
    if (!in_file(seg->offset, seg->size, len))
        return PLUGIN_BAD_SEGMENT;
    if (seg->vaddr < CODE_START || seg->vaddr > image_end)
        return PLUGIN_BAD_SEGMENT;
    if (seg->size > image_end - seg->vaddr)
        return PLUGIN_BAD_SEGMENT;
    return PLUGIN_OK;
}

static int matches_memory(u32 vaddr, const u8 *orig, u32 size)
{
    const volatile u8 *at = (const volatile u8 *)vaddr;
    for (u32 b = 0; b < size; b++)
        if (at[b] != orig[b])
            return 0;
    return 1;
}

static int check_file(const u8 *f, u32 len)
{
    elf_header hdr;
    int res = read_header(f, len, &hdr);
    if (res)
        return res;

    const u8 *orig;
    u32 orig_len;
    res = find_original(f, len, &hdr, &orig, &orig_len);
    if (res)
        return res;

    u32 used = 0;
    for (u32 i = 0; i < hdr.phnum; i++) {
        elf_segment seg;
        read_segment(f, &hdr, i, &seg);
        if (seg.type != PT_LOAD)
            continue;

        res = check_segment(&seg, len);
        if (res)
            return res;
        if (seg.size > orig_len - used)
            return PLUGIN_NO_ORIGINAL;
        if (taken(seg.vaddr, seg.vaddr + seg.size))
            return PLUGIN_CONFLICT;
        if (!matches_memory(seg.vaddr, orig + used, seg.size))
            return PLUGIN_MISMATCH;
        used += seg.size;

        if (num_claims == CLAIMS_MAX)
            return PLUGIN_FULL;
        claims[num_claims].start = seg.vaddr;
        claims[num_claims].end = seg.vaddr + seg.size;
        num_claims++;
    }

    if (!used)
        return PLUGIN_BAD_SEGMENT;
    if (used != orig_len)
        return PLUGIN_NO_ORIGINAL;
    return PLUGIN_OK;
}

static void write_file(const u8 *f, u32 len)
{
    elf_header hdr;
    if (read_header(f, len, &hdr))
        return;

    for (u32 i = 0; i < hdr.phnum; i++) {
        elf_segment seg;
        read_segment(f, &hdr, i, &seg);
        if (seg.type != PT_LOAD)
            continue;

        const u8 *src = f + seg.offset;
        volatile u8 *at = (volatile u8 *)seg.vaddr;
        for (u32 b = 0; b < seg.size; b++)
            at[b] = src[b];
    }
}

static int is_elf_name(const u16 *name)
{
    const char *ext = ".elf";
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

static int read_one(const u16 *name, u32 folder_len, u32 size, u32 *files, u32 *used)
{
    u32 at = folder_len;
    path[at++] = '/';
    for (u32 c = 0; name[c]; c++) {
        if (at == PATH_CHARS - 1)
            return PLUGIN_READ;
        path[at++] = name[c];
    }
    path[at] = 0;

    if (*files == PLUGIN_FILES_MAX)
        return PLUGIN_TOO_BIG;
    if (*used + size > PLUGIN_BYTES_MAX)
        return PLUGIN_TOO_BIG;

    u32 file, read = 0;
    int res = fs_file_open_read(&file, path);
    if (res < 0)
        return PLUGIN_READ;
    res = fs_file_read(file, 0, image + *used, size, &read);
    fs_file_close(file);
    if (res < 0 || read != size)
        return PLUGIN_READ;

    file_start[*files] = *used;
    file_size[*files] = size;
    (*files)++;
    *used = align4(*used + size);
    return PLUGIN_OK;
}

static int read_folder(const toggle_entry *e, u32 *files)
{
    u32 folder_len = toggles_folder_path(&plugins, e, path, PATH_CHARS);
    u32 dir;
    if (fs_dir_open(&dir, path) < 0)
        return PLUGIN_READ;

    u32 used = 0, read;
    int code = PLUGIN_OK;
    *files = 0;
    do {
        if (fs_dir_read(dir, batch, READ_BATCH, &read) < 0) {
            code = PLUGIN_READ;
            break;
        }
        for (u32 i = 0; i < read && !code; i++) {
            if (batch[i].attributes & FS_ATTR_DIR)
                continue;
            if (!is_elf_name(batch[i].name))
                continue;
            if (batch[i].size > PLUGIN_BYTES_MAX)
                code = PLUGIN_TOO_BIG;
            else
                code = read_one(batch[i].name, folder_len, (u32)batch[i].size, files, &used);
        }
    } while (!code && read == READ_BATCH);
    fs_dir_close(dir);

    if (!code && !*files)
        code = PLUGIN_EMPTY;
    return code;
}

static int load_one(const toggle_entry *e)
{
    u32 files;
    int code = read_folder(e, &files);
    if (code)
        return code;

    u32 mark = num_claims;
    for (u32 i = 0; i < files && !code; i++)
        code = check_file(image + file_start[i], file_size[i]);
    if (code) {
        num_claims = mark;
        return code;
    }

    for (u32 i = 0; i < files; i++)
        write_file(image + file_start[i], file_size[i]);
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
        } else {
            saltysd_status.plugins_applied++;
        }
    }

    fs_close();
}
