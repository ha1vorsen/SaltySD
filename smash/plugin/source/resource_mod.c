#include "types.h"
typedef signed int s32;
typedef unsigned int size_t;
typedef int bool;
#define true  1
#define false 0
#define NULL  ((void *)0)

#include <stdarg.h>
#include "common.h"
#include "index.h"
#include "version.h"

#define SALTYSD_LOOSE_ROOT     "sd:/luma/titles/smash"
#define SALTYSD_SD_LOOSE_ROOT  "sdmc:/luma/titles/smash/"
#define SALTYSD_MOD_ROOT       "sd:/saltysd/smash"
#define SALTYSD_SD_MOD_ROOT    "sdmc:/saltysd/smash/"
#define SALTYSD_DISABLED_MARKER "is.disabled"
#define SALTYSD_MAX_MODS       62
#define SALTYSD_MAX_ROOTS      (SALTYSD_MAX_MODS + 1)
#define SALTYSD_MAX_MOD_NAME   0x40
//Work arrays are totals across all roots, not per-mod
#define SALTYSD_MAX_DIRS       0x1000
#define SALTYSD_MAX_FILES      0x4000
#define SALTYSD_MAX_PATH       0x101
#define SALTYSD_STRING_BLOCK_SIZE 0x2000
#define SALTYSD_STRING_BLOCK_MASK 0x1FFF
#define SALTYSD_DIRECTORY_BATCH 0x40
#define SALTYSD_ROOT_PATH_SIZE 0x80
#define SALTYSD_IFILE_HANDLE_SIZE 0x40
#define SALTYSD_FULL_NAME_SIZE 0x400
#define SALTYSD_MAX_EXT        0x10
#define SALTYSD_MAX_EXTENSIONS 0x3E

#define SALTYSD_CRO_DIR        "cro/"
#define SALTYSD_BGM_DIR        "sound/bgm/"
#define SALTYSD_BGM_STEM       "snd_bgm_"
#define SALTYSD_BGM_EXT        ".nus3bank"
#define SALTYSD_KIND_CRO       0
#define SALTYSD_KIND_BGM       1
#define SALTYSD_HEADER_FROM_SINGLETON 0x1C6D8
#define SALTYSD_HEADER_TIMESTAMP      0x14
#define SALTYSD_MAGIC          0x594D4C53 //'SLTY'
#define SALTYSD_ID_BIAS        2
#define SALTYSD_ID_SPACE       0x10000
#define SALTYSD_ENTRY_RESERVE  0x800
#define SALTYSD_INDEX_NAME     32
#define SALTYSD_INDEX_MAX      0x80000
#define SALTYSD_INDEX_TEXT     0x18000

typedef struct __attribute__((__packed__)) {
    u16 magic;
    u8 props;
    u8 pad;
    u32 contents_start;
    u32 contents_size;
    u32 entrysection_start;
    u32 entrysection_size;
    u32 timestamp;
    u32 compressed_size;
    u32 decompressed_size;
    u32 stringsection_start;
    u32 stringsection_size;
    u32 resourceentry_amt;

} rf_header;

typedef struct __attribute__((__packed__)) {
    u32 chunk_offs;
    u32 string_offs;
    u32 comp_size;
    u32 decomp_size;
    u32 timestamp;
    u32 flags;
} rf_entry;

typedef struct __attribute__((__packed__)) {
    u16 path[0x106];
    char shortpath[10];
    char pathext[4];
    u8 valid_path;
    u8 unk;
    u8 is_directory;
    u8 is_hidden;
    u8 is_archive;
    u8 is_readonly;
    u64 file_size;
} DirectoryEntry;

typedef struct {
    char *path;   //"sd:/..." as the boot-time scan walks it
    char *prefix; //"sdmc:/.../" as the load hooks spell it, trailing slash included
    char *name;   //mod folder name, NULL for the loose root
    u8 enabled;
} saltysd_root;

typedef struct {
    char *path; //root relative game path, ie "cro/fighter/falco"
    u8 root;    //winning root plus one, matching root_of
} saltysd_named;

static void (*memcpy)(void *dest, const void *src, size_t n) = (void *)memcpy_ADDR;
static void (*memmove)(void *dest, const void *src, size_t n) = (void *)memmove_ADDR;
static void *(*malloc)(size_t size) = (void *)liballoc_ADDR;
static void (*free)(void *ptr) = (void *)libdealloc_ADDR;
static void (*memclr)(void *ptr, size_t size) = (void *)memclr_ADDR;
static int (*strlen)(char *str) = (void *)strlen_ADDR;
static int (*strcmp)(const char *str1, const char *str2) = (void *)strcmp_ADDR;

static void copy_entry_path(u16 *dst, const DirectoryEntry *entry)
{
    memcpy(dst, entry, SALTYSD_MAX_PATH * sizeof(u16));
    dst[SALTYSD_MAX_PATH - 1] = 0;
}

#if SALTYSD_DEBUG
static int (*vsnprintf)(char *s, size_t n, const char *format,
                        va_list arg) = (void *)vsnprintf_ADDR;
#endif

static u32 (*IFile_Init)(void *handle) = (void *)IFile_Init_ADDR;
static u32 (*IFile_Open)(void *handle, char *path, u32 mode) = (void *)IFile_Open_ADDR;
static u32 (*IFile_Read)(void *handle, void *dest, size_t size,
                         u32 *bytes_read) = (void *)IFile_Read_ADDR;
static u32 (*IFile_GetSize)(void *handle) = (void *)IFile_GetSize_ADDR;
static u32 (*IFile_Close)(void *handle) = (void *)IFile_Close_ADDR;

static u16 *(*get_rf_struct)(u32 *id) = (void *)get_rf_struct_ADDR;

static void *(*crit_this)(void) = (void *)crit_this_ADDR;
static void *(*crit_init)(void *crit_inst) = (void *)crit_init_ADDR;
static u32 (*mount_sdmc)(char *mount_path) = (void *)mount_sdmc_ADDR;
static u32 (*unmount_path)(char *mount_path) = (void *)unmount_path_ADDR;

static u32 (*OpenDirectory)(void **handle, u16 *path) = (void *)OpenDirectory_ADDR;
static u32 (*ReadDirectory)(u32 *num_dirs, void *handle, void *out,
                            u32 num_entries_toload) = (void *)ReadDirectory_ADDR;
static u32 (*CloseDirectory)(void *handle) = (void *)CloseDirectory_ADDR;

int dumb_wcslen(u16 *str)
{
    u32 len = 0;
    while (1) {
        if (str[len] == 0)
            return len;
        len++;
    }
}

char *dumb_strncat(char *dest, char *src, u32 len)
{
    void *copyinto = dest + strlen(dest);
    memcpy(copyinto, src, len);
    *(u8 *)(copyinto + len) = 0;
    return dest;
}

char *dumb_strcat(char *dest, char *src)
{
    return dumb_strncat(dest, src, strlen(src));
}

char *dumb_strcpy(char *dest, char *src)
{
    dest[0] = 0;
    return dumb_strcat(dest, src);
}

char *dumb_strncpy(char *dest, char *src, size_t len)
{
    dest[0] = 0;
    return dumb_strncat(dest, src, len);
}

u16 *dumb_wcsncat(u16 *dest, u16 *src, u32 len)
{
    void *copyinto = dest + dumb_wcslen(dest);
    memcpy(copyinto, src, len * sizeof(u16));
    *(u16 *)(copyinto + (len * sizeof(u16))) = 0;
    return dest;
}

u16 *dumb_wcscat(u16 *dest, u16 *src)
{
    return dumb_wcsncat(dest, src, dumb_wcslen(src));
}

u16 *dumb_mbstowcs(u16 *dest, char *src)
{
    u32 count = 0;
    while (1) {
        dest[count] = src[count];
        if (src[count] == 0)
            break;
        count++;
    }
    return dest;
}

char *dumb_wcstombs(char *dest, u16 *src)
{
    u32 count = 0;
    while (1) {
        dest[count] = (u8)(src[count] & 0xFF);
        if (src[count] == 0)
            break;
        count++;
    }
    return dest;
}

char *dumb_wcstombsn(char *dest, u16 *src, u32 len)
{
    u32 count = 0;
    while (count < len - 1 && src[count]) {
        dest[count] = (u8)(src[count] & 0xFF);
        count++;
    }
    dest[count] = 0;
    return dest;
}

void copy_root(saltysd_root *dest, saltysd_root *src)
{
    dest->path = src->path;
    dest->prefix = src->prefix;
    dest->name = src->name;
    dest->enabled = src->enabled;
}

bool starts_with(char *str, char *prefix)
{
    while (*prefix) {
        if (*str++ != *prefix++)
            return false;
    }
    return true;
}

u32 len_to(char *str, char chr)
{
    u32 count = 0;
    while (1) {
        if (str[count] == 0)
            return -1;

        if (str[count++] == chr)
            break;
    }
    return count;
}

u32 last_index_of(char *str, char chr)
{
    u32 found = -1;
    for (u32 i = 0; str[i]; i++) {
        if (str[i] == chr)
            found = i;
    }
    return found;
}

u16 read_u16le(char *str)
{
    u16 value;
    memcpy(&value, str, sizeof(value));
    return value;
}

u32 count_chars(char *str, char chr)
{
    u32 i = 0;
    u32 count = 0;
    while (1) {
        if (str[i] == 0)
            break;

        if (str[i++] == chr)
            count++;
    }
    return count;
}

#if SALTYSD_DEBUG
//svc 0x3D takes the string in r0 and its length in r1; both are pinned here.
void debug_print(char *str)
{
    u32 n = strlen(str);
    register char *addr __asm__("r0") = str;
    register u32 len __asm__("r1") = n;
    __asm__ volatile("svc 0x3D" ::"r"(addr), "r"(len) : "memory");
}

void printf(char *format, ...)
{
    char *str = malloc(0x400);

    va_list argptr;
    va_start(argptr, format);
    vsnprintf(str, 0x400, format, argptr);
    va_end(argptr);

    dumb_strcat(str, "");
    debug_print(str);
    free(str);
}
#else

#define printf(...) ((void)0)
#endif

#define SALTYSD_LOG_PATH       "sd:/saltysd/smash/saltysd.log"
#define SALTYSD_LOG_SIZE       0x400
#define SALTYSD_OPEN_WRITE     2
#define SALTYSD_OPEN_CREATE    4
#define SALTYSD_WRITE_FLUSH    1
#define SALTYSD_CALIB_LOOPS    1000000

#define SALTYSD_FILE_STREAM    8
#define SALTYSD_FILE_WRITE     1
typedef u32 (*file_write_fn)(void *self, u32 *written, u64 offset, const void *buf, u32 size,
                             u32 flags);

static u64 ticks(void)
{
    //GetSystemTick returns the 64-bit counter in r0:r1.
    register u32 lo __asm__("r0");
    register u32 hi __asm__("r1");
    __asm__ volatile("svc 0x28" : "=r"(lo), "=r"(hi)::"r2", "r3", "r12", "memory");
    return ((u64)hi << 32) | lo;
}

static u32 calibrate(void)
{
    u32 n = SALTYSD_CALIB_LOOPS;
    u64 start = ticks();
    //Keep the calibration loop fixed; C optimization would erase the work.
    __asm__ volatile("1: subs %0, %0, #1\n\tbne 1b" : "+r"(n)::"cc");
    return (u32)(ticks() - start);
}

static u32 ticks_to_ms(u64 t)
{
    return (u32)(t >> 8) / 1047;
}

static void log_str(char *log, char *str)
{
    if (strlen(log) + strlen(str) < SALTYSD_LOG_SIZE - 1)
        dumb_strcat(log, str);
}

static void log_dec(char *log, u32 value)
{
    char digits[12];
    u32 at = sizeof(digits) - 1;
    digits[at] = 0;
    do {
        digits[--at] = '0' + value % 10;
        value /= 10;
    } while (value);
    log_str(log, &digits[at]);
}

static void log_hex(char *log, u32 value)
{
    char digits[9];
    for (int i = 0; i < 8; i++)
        digits[i] = "0123456789ABCDEF"[(value >> (28 - i * 4)) & 0xF];
    digits[8] = 0;
    log_str(log, digits);
}

static void log_phase(char *log, char *name, u64 t)
{
    log_str(log, name);
    log_str(log, " ");
    log_dec(log, ticks_to_ms(t));
    log_str(log, " ms (0x");
    log_hex(log, (u32)(t >> 32));
    log_hex(log, (u32)t);
    log_str(log, " ticks)\n");
}

static u32 fnv1a(u32 hash, const void *data, u32 size)
{
    const u8 *p = data;
    while (size--) {
        hash ^= *p++;
        hash *= 16777619u;
    }
    return hash;
}

static void write_log(char *log, void *ifile_handle)
{
    u32 len = strlen(log);
    while (len < SALTYSD_LOG_SIZE - 1)
        log[len++] = ' ';
    log[len++] = '\n';

    IFile_Init(ifile_handle);
    if (!IFile_Open(ifile_handle, SALTYSD_LOG_PATH, SALTYSD_OPEN_WRITE | SALTYSD_OPEN_CREATE))
        return;

    u32 stream = *(u32 *)(ifile_handle + 4) + SALTYSD_FILE_STREAM;
    void *file = (void *)(*(u32 *)stream & ~1u);
    if (file) {
        file_write_fn write = (file_write_fn)(*(u32 **)file)[SALTYSD_FILE_WRITE];
        u32 written = 0;
        write(file, &written, 0, log, len, SALTYSD_WRITE_FLUSH);
    }

    IFile_Close(ifile_handle);
}

typedef struct {
    bool on;
    u32 *revokes;
    u32 num_revokes, max_revokes;
    idx_override *overrides;
    u32 num_overrides, max_overrides;
    idx_string *strings;
    u32 num_strings, max_strings;
    idx_ext *exts;
    u32 num_exts, max_exts;
    idx_insert *inserts;
    u32 num_inserts, max_inserts;
    char *text;
    u32 text_len, text_max;
} idx_rec;

static u32 idx_text(idx_rec *r, char *str)
{
    u32 len = strlen(str) + 1;
    if (!r->on || r->text_len + len > r->text_max) {
        r->on = false;
        return 0;
    }

    u32 at = r->text_len;
    memcpy(r->text + at, str, len);
    r->text_len += len;
    return at;
}

static void idx_revoke(idx_rec *r, u32 id)
{
    if (!r->on || r->num_revokes >= r->max_revokes) {
        r->on = false;
        return;
    }

    r->revokes[r->num_revokes++] = id;
}

static void idx_override_add(idx_rec *r, u32 id, u32 size, u8 root)
{
    if (!r->on || r->num_overrides >= r->max_overrides) {
        r->on = false;
        return;
    }

    idx_override *o = &r->overrides[r->num_overrides++];
    o->id = id;
    o->size = size;
    o->root = root;
}

static void idx_string_add(idx_rec *r, u32 tree_offset, char *str)
{
    u32 text = idx_text(r, str);
    if (!r->on || r->num_strings >= r->max_strings) {
        r->on = false;
        return;
    }

    idx_string *e = &r->strings[r->num_strings++];
    e->tree_offset = tree_offset;
    e->text = text;
}

static void idx_ext_add(idx_rec *r, u32 slot, u32 tree_offset)
{
    if (!r->on || r->num_exts >= r->max_exts) {
        r->on = false;
        return;
    }

    idx_ext *e = &r->exts[r->num_exts++];
    e->slot = slot;
    e->tree_offset = tree_offset;
}

static void idx_insert_add(idx_rec *r, u32 pos, u32 string_offs, u32 size, u32 flags, u8 root)
{
    if (!r->on || r->num_inserts >= r->max_inserts) {
        r->on = false;
        return;
    }

    for (u32 i = 0; i < r->num_inserts; i++)
        if (r->inserts[i].pos >= pos)
            r->inserts[i].pos++;

    idx_insert *e = &r->inserts[r->num_inserts++];
    e->pos = pos;
    e->string_offs = string_offs;
    e->size = size;
    e->flags = flags;
    e->root = root;
}

#define SALTYSD_PUT_NO_OPEN    0xFFFFFFFF
#define SALTYSD_PUT_NO_STREAM  0xFFFFFFFE

static u32 file_put(char *path, void *buf, u32 len, void *ifile_handle)
{
    IFile_Init(ifile_handle);
    if (!IFile_Open(ifile_handle, path, SALTYSD_OPEN_WRITE | SALTYSD_OPEN_CREATE))
        return SALTYSD_PUT_NO_OPEN;

    u32 written = SALTYSD_PUT_NO_STREAM;
    u32 stream = *(u32 *)(ifile_handle + 4) + SALTYSD_FILE_STREAM;
    void *file = (void *)(*(u32 *)stream & ~1u);
    if (file) {
        file_write_fn write = (file_write_fn)(*(u32 **)file)[SALTYSD_FILE_WRITE];
        written = 0;
        write(file, &written, 0, buf, len, SALTYSD_WRITE_FLUSH);
    }

    IFile_Close(ifile_handle);
    return written;
}

static u32 file_get(char *path, void *buf, u32 max, void *ifile_handle)
{
    IFile_Init(ifile_handle);
    if (!IFile_Open(ifile_handle, path, 1))
        return 0;

    u32 size = IFile_GetSize(ifile_handle);
    if (size > max)
        size = max;

    u32 read = 0;
    if (size)
        IFile_Read(ifile_handle, buf, size, &read);

    IFile_Close(ifile_handle);
    return read;
}

typedef struct {
    u32 magic;
    u32 num_roots;
    u8 *root_of;
    saltysd_root *roots;
    u32 num_named;
    saltysd_named *named;
} saltysd_map;

static saltysd_map *saltysd_get_map(void)
{
    u32 singleton = *(u32 *)something_resource_lock_ADDR;
    if (!singleton)
        return NULL;

    u32 header = *(u32 *)(singleton + SALTYSD_HEADER_FROM_SINGLETON);
    if (!header)
        return NULL;

    saltysd_map *map = *(saltysd_map **)(header + SALTYSD_HEADER_TIMESTAMP);
    if (!map || map->magic != SALTYSD_MAGIC)
        return NULL;

    return map;
}

u32 saltysd_build_prefix(char *out, u32 id)
{
    saltysd_map *map = saltysd_get_map();
    if (!map)
        return 0;

    id &= 0xFFFF;
    for (int i = 0; i < 4 && id; i++) {
        u32 lookup = id;
        u16 source = *get_rf_struct(&lookup);
        if (!source || source == id)
            break;

        id = source;
    }

    u32 root = map->root_of[id & (SALTYSD_ID_SPACE - 1)];
    if (!root || root > map->num_roots)
        return 0;

    char *prefix = map->roots[root - 1].prefix;
    u32 len = strlen(prefix);

    memcpy(out, prefix, len);
    return len;
}

u32 saltysd_build_named_path(char *out, u32 out_size, u32 kind, char *name)
{
    saltysd_map *map = saltysd_get_map();
    if (!map || !map->num_named)
        return 0;

    u32 key_len = strlen(name) + (kind == SALTYSD_KIND_BGM
                                      ? sizeof(SALTYSD_BGM_DIR SALTYSD_BGM_STEM SALTYSD_BGM_EXT) - 1
                                      : sizeof(SALTYSD_CRO_DIR) - 1);
    if (key_len + 1 > out_size)
        return 0;

    out[0] = 0;
    if (kind == SALTYSD_KIND_BGM) {
        dumb_strcat(out, SALTYSD_BGM_DIR SALTYSD_BGM_STEM);
        dumb_strcat(out, name);
        dumb_strcat(out, SALTYSD_BGM_EXT);
    } else {
        dumb_strcat(out, SALTYSD_CRO_DIR);
        dumb_strcat(out, name);
    }

    u32 root = 0;
    for (u32 i = 0; i < map->num_named; i++) {
        if (!strcmp(map->named[i].path, out)) {
            root = map->named[i].root;
            break;
        }
    }

    if (!root || root > map->num_roots)
        return 0;

    char *prefix = map->roots[root - 1].prefix;
    u32 prefix_len = strlen(prefix);
    if (prefix_len + key_len + 1 > out_size)
        return 0;

    memmove(out + prefix_len, out, key_len + 1);
    memcpy(out, prefix, prefix_len);
    return prefix_len + key_len;
}

static void saltysd_mod_config(saltysd_root *mod, void *ifile_handle)
{
    char marker[SALTYSD_ROOT_PATH_SIZE + sizeof("/" SALTYSD_DISABLED_MARKER)];
    dumb_strcpy(marker, mod->path);
    dumb_strcat(marker, "/" SALTYSD_DISABLED_MARKER);

    IFile_Init(ifile_handle);
    mod->enabled = !IFile_Open(ifile_handle, marker, 1);
    if (!mod->enabled)
        IFile_Close(ifile_handle);
}

static void report_conflict(saltysd_root *roots, u8 a, u8 b, char *path)
{
    if (a == 0 || b == 0)
        return;

    printf("SaltySD conflict %s: %s and %s", path, roots[a].name, roots[b].name);
}

typedef int (*sort_cmp)(u32 a, u32 b, void *ctx);

static void sift_down(u32 *a, u32 root, u32 end, sort_cmp cmp, void *ctx)
{
    while (1) {
        u32 child = root * 2 + 1;
        if (child >= end)
            break;
        if (child + 1 < end && cmp(a[child], a[child + 1], ctx) < 0)
            child++;
        if (cmp(a[root], a[child], ctx) >= 0)
            break;

        u32 swap = a[root];
        a[root] = a[child];
        a[child] = swap;
        root = child;
    }
}

static void heap_sort(u32 *a, u32 n, sort_cmp cmp, void *ctx)
{
    if (n < 2)
        return;

    for (u32 i = n / 2; i-- > 0;)
        sift_down(a, i, n, cmp, ctx);

    for (u32 end = n - 1; end > 0; end--) {
        u32 swap = a[0];
        a[0] = a[end];
        a[end] = swap;
        sift_down(a, 0, end, cmp, ctx);
    }
}

static int cmp_string(u32 a, u32 b, void *ctx)
{
    return strcmp((char *)a, (char *)b);
}

typedef struct {
    char **files;
    u8 *roots;
} file_order;

static int cmp_file(u32 a, u32 b, void *ctx)
{
    file_order *order = ctx;
    int by_path = strcmp(order->files[a], order->files[b]);
    if (by_path)
        return by_path;
    if (order->roots[a] != order->roots[b])
        return order->roots[a] < order->roots[b] ? -1 : 1;
    return a < b ? -1 : (a > b);
}

static int find_string(u32 *sorted, u32 n, char *str)
{
    u32 lo = 0;
    u32 hi = n;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        int c = strcmp(str, (char *)sorted[mid]);
        if (!c)
            return mid;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return -1;
}

#define SALTYSD_NO_STRING 0xFFFFFFFF
static u32 string_alloc(u32 *cursor, u32 limit, u32 len)
{
    u32 at = *cursor;
    if ((at & SALTYSD_STRING_BLOCK_MASK) + len >= SALTYSD_STRING_BLOCK_SIZE)
        at = (at + SALTYSD_STRING_BLOCK_MASK) & (0xFFFFFFFF - SALTYSD_STRING_BLOCK_MASK);

    if (at + len + 1 > limit)
        return SALTYSD_NO_STRING;

    *cursor = at + len + 1;
    return at;
}

typedef struct {
    rf_header *header;
    rf_entry *entries;
    void **blocks;
    u32 string_block_count;
    void *extensions_block;
    u8 *root_table;
} idx_tree;

static int cmp_insert_pos(u32 a, u32 b, void *ctx)
{
    idx_insert *v = ctx;
    return v[a].pos < v[b].pos ? -1 : v[a].pos > v[b].pos ? 1 : 0;
}

static u32 idx_mod_hash(saltysd_root *roots, u32 num_roots)
{
    u32 hash = 2166136261u;
    for (u32 i = 0; i < num_roots; i++) {
        char *name = roots[i].name ? roots[i].name : "";
        hash = fnv1a(hash, name, strlen(name) + 1);
        hash = fnv1a(hash, &roots[i].enabled, sizeof(roots[i].enabled));
    }
    return hash;
}

static void idx_path(char *out, idx_header *key)
{
    u32 h = 2166136261u;
    h = fnv1a(h, &key->title, sizeof(key->title));
    h = fnv1a(h, &key->tree_entries, sizeof(key->tree_entries));
    h = fnv1a(h, &key->tree_entry_size, sizeof(key->tree_entry_size));
    h = fnv1a(h, &key->tree_string_size, sizeof(key->tree_string_size));
    h = fnv1a(h, &key->tree_timestamp, sizeof(key->tree_timestamp));

    dumb_strcpy(out, SALTYSD_INDEX_PATH);
    u32 at = strlen(out);
    for (u32 i = 0; i < 8; i++)
        out[at++] = "0123456789ABCDEF"[(h >> (28 - i * 4)) & 0xF];
    out[at] = 0;
}

static void idx_key(idx_header *h, rf_header *tree, saltysd_root *roots, u32 num_roots,
                    u32 entries_before, u32 entry_size_before, u32 string_size_before)
{
    h->build = fnv1a(2166136261u, SALTYSD_IDENTITY, strlen(SALTYSD_IDENTITY) + 1);
    h->title = SALTYSD_TITLE_ID;
    h->mods = idx_mod_hash(roots, num_roots);
    h->entry_reserve = SALTYSD_ENTRY_RESERVE;
    h->tree_entries = entries_before;
    h->tree_entry_size = entry_size_before;
    h->tree_string_size = string_size_before;
    h->tree_timestamp = tree->timestamp;
}

static u32 idx_write(idx_rec *r, idx_header *key, char *path, saltysd_root *roots, u32 num_roots,
                     saltysd_named *named, u32 num_named, void *ifile_handle)
{
    if (!r->on)
        return 0;

    u32 *named_text = num_named ? malloc(num_named * sizeof(u32)) : NULL;
    if (num_named && !named_text)
        return 0;

    for (u32 i = 0; i < num_named; i++)
        named_text[i] = idx_text(r, named[i].path);

    u32 *root_text = malloc(num_roots * sizeof(u32));
    if (!root_text) {
        free(named_text);
        return 0;
    }

    for (u32 i = 0; i < num_roots; i++)
        root_text[i] = idx_text(r, roots[i].name ? roots[i].name : "");

    if (!r->on) {
        free(named_text);
        free(root_text);
        return 0;
    }

    if (r->num_inserts > 1) {
        u32 *order = malloc(r->num_inserts * sizeof(u32));
        idx_insert *sorted = malloc(r->num_inserts * sizeof(idx_insert));
        if (!order || !sorted) {
            free(order);
            free(sorted);
            free(named_text);
            free(root_text);
            return 0;
        }

        for (u32 i = 0; i < r->num_inserts; i++)
            order[i] = i;

        heap_sort(order, r->num_inserts, cmp_insert_pos, r->inserts);

        for (u32 i = 0; i < r->num_inserts; i++)
            sorted[i] = r->inserts[order[i]];

        memcpy(r->inserts, sorted, r->num_inserts * sizeof(idx_insert));
        free(order);
        free(sorted);
    }

    idx_header h = *key;
    u32 at = sizeof(idx_header);
    h.magic = SALTYSD_INDEX_MAGIC;

    h.num_roots = num_roots;
    h.roots_off = at;
    at += num_roots * sizeof(u32);

    h.num_revokes = r->num_revokes;
    h.revokes_off = at;
    at += r->num_revokes * sizeof(u32);

    h.num_overrides = r->num_overrides;
    h.overrides_off = at;
    at += r->num_overrides * sizeof(idx_override);

    h.num_strings = r->num_strings;
    h.strings_off = at;
    at += r->num_strings * sizeof(idx_string);

    h.num_exts = r->num_exts;
    h.exts_off = at;
    at += r->num_exts * sizeof(idx_ext);

    h.num_inserts = r->num_inserts;
    h.inserts_off = at;
    at += r->num_inserts * sizeof(idx_insert);

    h.num_named = num_named;
    h.named_off = at;
    at += num_named * sizeof(idx_named);

    h.text_off = at;
    h.text_size = r->text_len;
    at += r->text_len;
    h.total_size = at;

    u8 *buf = malloc(at);
    if (!buf) {
        free(named_text);
        free(root_text);
        return 0;
    }

    memcpy(buf, &h, sizeof(h));
    memcpy(buf + h.roots_off, root_text, num_roots * sizeof(u32));
    memcpy(buf + h.revokes_off, r->revokes, r->num_revokes * sizeof(u32));
    memcpy(buf + h.overrides_off, r->overrides, r->num_overrides * sizeof(idx_override));
    memcpy(buf + h.strings_off, r->strings, r->num_strings * sizeof(idx_string));
    memcpy(buf + h.exts_off, r->exts, r->num_exts * sizeof(idx_ext));
    memcpy(buf + h.inserts_off, r->inserts, r->num_inserts * sizeof(idx_insert));

    idx_named *out = (idx_named *)(buf + h.named_off);
    for (u32 i = 0; i < num_named; i++) {
        out[i].text = named_text[i];
        out[i].root = named[i].root;
        out[i].pad[0] = 0;
        out[i].pad[1] = 0;
        out[i].pad[2] = 0;
    }

    memcpy(buf + h.text_off, r->text, r->text_len);

    u32 put = file_put(path, buf, at, ifile_handle);

    free(buf);
    free(named_text);
    free(root_text);
    return put;
}

static bool idx_usable(idx_header *h, u32 size, idx_header *key, idx_tree *t)
{
    if (size < sizeof(idx_header) || h->magic != SALTYSD_INDEX_MAGIC || h->total_size > size)
        return false;

    if (h->build != key->build || h->title != key->title || h->mods != key->mods ||
        h->entry_reserve != key->entry_reserve || h->tree_entries != key->tree_entries ||
        h->tree_entry_size != key->tree_entry_size ||
        h->tree_string_size != key->tree_string_size || h->tree_timestamp != key->tree_timestamp)
        return false;

    u32 end = h->total_size;
    if (h->roots_off + h->num_roots * sizeof(u32) > end ||
        h->revokes_off + h->num_revokes * sizeof(u32) > end ||
        h->overrides_off + h->num_overrides * sizeof(idx_override) > end ||
        h->strings_off + h->num_strings * sizeof(idx_string) > end ||
        h->exts_off + h->num_exts * sizeof(idx_ext) > end ||
        h->inserts_off + h->num_inserts * sizeof(idx_insert) > end ||
        h->named_off + h->num_named * sizeof(idx_named) > end || h->text_off + h->text_size > end)
        return false;

    if (h->num_inserts > h->entry_reserve ||
        h->tree_entries + h->num_inserts + SALTYSD_ID_BIAS > SALTYSD_ID_SPACE)
        return false;

    u8 *base = (u8 *)h;
    char *text = (char *)base + h->text_off;

    if (h->text_size && text[h->text_size - 1] != 0)
        return false;

    u32 *revokes = (u32 *)(base + h->revokes_off);
    for (u32 i = 0; i < h->num_revokes; i++)
        if (revokes[i] >= h->tree_entries)
            return false;

    idx_override *ov = (idx_override *)(base + h->overrides_off);
    for (u32 i = 0; i < h->num_overrides; i++)
        if (ov[i].id >= h->tree_entries || ov[i].root > SALTYSD_MAX_ROOTS)
            return false;

    u32 string_limit = t->string_block_count * SALTYSD_STRING_BLOCK_SIZE;
    idx_string *st = (idx_string *)(base + h->strings_off);
    for (u32 i = 0; i < h->num_strings; i++) {
        if (st[i].text >= h->text_size ||
            st[i].tree_offset / SALTYSD_STRING_BLOCK_SIZE >= t->string_block_count)
            return false;

        if (st[i].tree_offset + strlen(text + st[i].text) + 1 > string_limit)
            return false;
    }

    idx_ext *ex = (idx_ext *)(base + h->exts_off);
    for (u32 i = 0; i < h->num_exts; i++)
        if (ex[i].slot >= SALTYSD_MAX_EXTENSIONS || ex[i].tree_offset >= string_limit)
            return false;

    u32 total = h->tree_entries + h->num_inserts;
    idx_insert *in = (idx_insert *)(base + h->inserts_off);
    for (u32 i = 0; i < h->num_inserts; i++) {
        if (in[i].pos >= total || in[i].root > SALTYSD_MAX_ROOTS)
            return false;
        if (i && in[i].pos <= in[i - 1].pos)
            return false;
    }

    idx_named *nm = (idx_named *)(base + h->named_off);
    for (u32 i = 0; i < h->num_named; i++)
        if (nm[i].text >= h->text_size || nm[i].root > SALTYSD_MAX_ROOTS)
            return false;

    u32 *rt = (u32 *)(base + h->roots_off);
    for (u32 i = 0; i < h->num_roots; i++)
        if (rt[i] >= h->text_size)
            return false;

    return true;
}

static bool idx_apply(idx_header *h, idx_tree *t, saltysd_root *roots, u32 num_roots,
                      saltysd_named **out_named, u32 *out_num_named)
{
    u8 *base = (u8 *)h;
    char *text = (char *)base + h->text_off;

    if (h->num_roots != num_roots)
        return false;

    u32 *root_text = (u32 *)(base + h->roots_off);
    for (u32 i = 0; i < num_roots; i++) {
        char *name = roots[i].name ? roots[i].name : "";
        if (strcmp(name, text + root_text[i]))
            return false;
    }

    saltysd_named *named = NULL;
    if (h->num_named) {
        named = malloc(h->num_named * sizeof(saltysd_named));
        if (!named)
            return false;

        idx_named *nm = (idx_named *)(base + h->named_off);
        for (u32 i = 0; i < h->num_named; i++) {
            char *path = text + nm[i].text;
            named[i].path = malloc(strlen(path) + 1);
            if (!named[i].path) {
                for (u32 j = 0; j < i; j++)
                    free(named[j].path);

                free(named);
                return false;
            }

            dumb_strcpy(named[i].path, path);
            named[i].root = nm[i].root;
        }
    }

    idx_string *st = (idx_string *)(base + h->strings_off);
    for (u32 i = 0; i < h->num_strings; i++) {
        char *dest = (char *)t->blocks[st[i].tree_offset / SALTYSD_STRING_BLOCK_SIZE] +
                     (st[i].tree_offset & SALTYSD_STRING_BLOCK_MASK);
        dumb_strcpy(dest, text + st[i].text);
    }

    idx_ext *ex = (idx_ext *)(base + h->exts_off);
    for (u32 i = 0; i < h->num_exts; i++) {
        *(u32 *)(t->extensions_block + sizeof(u32) + ex[i].slot * sizeof(u32)) = ex[i].tree_offset;
        if (*(u32 *)t->extensions_block <= ex[i].slot)
            *(u32 *)t->extensions_block = ex[i].slot + 1;
    }

    u32 *revokes = (u32 *)(base + h->revokes_off);
    for (u32 i = 0; i < h->num_revokes; i++)
        t->entries[revokes[i]].string_offs &= 0xFFF00000;

    idx_override *ov = (idx_override *)(base + h->overrides_off);
    for (u32 i = 0; i < h->num_overrides; i++) {
        t->entries[ov[i].id].comp_size = ov[i].size;
        t->entries[ov[i].id].decomp_size = ov[i].size;
        t->entries[ov[i].id].flags |= 0x8000;
        t->root_table[ov[i].id + SALTYSD_ID_BIAS] = ov[i].root;
    }

    idx_insert *in = (idx_insert *)(base + h->inserts_off);
    u32 total = h->tree_entries + h->num_inserts;
    u32 src = h->tree_entries;
    u32 k = h->num_inserts;

    for (u32 dst = total; dst-- > 0;) {
        if (k && in[k - 1].pos == dst) {
            k--;
            t->entries[dst].chunk_offs = 0;
            t->entries[dst].string_offs = in[k].string_offs;
            t->entries[dst].comp_size = in[k].size;
            t->entries[dst].decomp_size = in[k].size;
            t->entries[dst].timestamp = 0;
            t->entries[dst].flags = in[k].flags;
            t->root_table[dst + SALTYSD_ID_BIAS] = in[k].root;
        } else if (src) {
            src--;
            t->entries[dst] = t->entries[src];
            t->root_table[dst + SALTYSD_ID_BIAS] = t->root_table[src + SALTYSD_ID_BIAS];
        }
    }

    for (u32 i = 0; i < h->num_inserts; i++)
        if (in[i].pos)
            t->entries[in[i].pos].chunk_offs = t->entries[in[i].pos - 1].chunk_offs;

    t->header->resourceentry_amt += h->num_inserts;
    t->header->entrysection_size += h->num_inserts * sizeof(rf_entry);

    *out_named = named;
    *out_num_named = h->num_named;
    return true;
}

void _main(rf_header *header, void *contents)
{
    u64 t_start = ticks();
    u32 calib_ticks = calibrate();
    u32 entries_before = header->resourceentry_amt;
    u32 entrysection_before = header->entrysection_size;

    //Room for new entries is carved out once and never grows
    const u32 ENTRY_RESERVE = SALTYSD_ENTRY_RESERVE;
    const u32 STRING_SHIFT = sizeof(rf_entry) * ENTRY_RESERVE;
    const u32 EXT_SHIFT = SALTYSD_STRING_BLOCK_SIZE * 0x8 * sizeof(u8);
    void *string_section_current =
        contents + (header->stringsection_start - header->contents_start);
    void *string_section_next = string_section_current + STRING_SHIFT;

    memmove(string_section_next, string_section_current, header->stringsection_size);
    memclr(string_section_current, STRING_SHIFT);
    header->stringsection_start += STRING_SHIFT;
    header->decompressed_size += STRING_SHIFT;
    header->contents_size += STRING_SHIFT;

    //Move extension chunk to make room for new strings
    memmove(string_section_next +
                (*(u32 *)string_section_next * SALTYSD_STRING_BLOCK_SIZE * sizeof(u8)) + EXT_SHIFT,
            string_section_next +
                (*(u32 *)string_section_next * SALTYSD_STRING_BLOCK_SIZE * sizeof(u8)),
            SALTYSD_STRING_BLOCK_SIZE);
    memclr(string_section_next +
               (*(u32 *)string_section_next * SALTYSD_STRING_BLOCK_SIZE * sizeof(u8)),
           EXT_SHIFT);
    header->stringsection_size += EXT_SHIFT;
    header->decompressed_size += EXT_SHIFT;
    header->contents_size += EXT_SHIFT;
    *(u32 *)string_section_next += (EXT_SHIFT / SALTYSD_STRING_BLOCK_SIZE);

    rf_entry(*entries)[] = contents + header->entrysection_start - header->contents_start;

    u32 string_block_count = *(u32 *)string_section_next;
    void *extensions_block = string_section_next + sizeof(u32) +
                             (SALTYSD_STRING_BLOCK_SIZE * string_block_count * sizeof(u8));
    void **blocks = malloc(string_block_count * sizeof(void *));
    for (int i = 0; i < string_block_count; i++) {
        blocks[i] =
            string_section_next + sizeof(u32) + (SALTYSD_STRING_BLOCK_SIZE * i * sizeof(u8));
    }

    u32 ext_capacity = *(u32 *)extensions_block;
    if (ext_capacity < SALTYSD_MAX_EXTENSIONS)
        ext_capacity = SALTYSD_MAX_EXTENSIONS;

    char **extensions = malloc(ext_capacity * sizeof(char *));
    for (int i = 0; i < *(u32 *)extensions_block; i++) {
        u32 offs = *(u32 *)(extensions_block + sizeof(u32) + i * sizeof(u32));
        char *string =
            blocks[offs / SALTYSD_STRING_BLOCK_SIZE] + (offs & SALTYSD_STRING_BLOCK_MASK);
        extensions[i] = string;
    }

    void *ifile_handle = malloc(SALTYSD_IFILE_HANDLE_SIZE);
    char *revoke_buf = NULL;
    char **revoked_files = NULL;
    u32 revoke_total_size = 0;
    int revoke_count = 0;
    crit_init(crit_this());
    mount_sdmc("sd:");

    u32 num_roots = 0;
    saltysd_root *roots = malloc(SALTYSD_MAX_ROOTS * sizeof(saltysd_root));

    roots[0].path = malloc(SALTYSD_ROOT_PATH_SIZE);
    dumb_strcpy(roots[0].path, SALTYSD_LOOSE_ROOT);
    roots[0].prefix = malloc(SALTYSD_ROOT_PATH_SIZE);
    dumb_strcpy(roots[0].prefix, SALTYSD_SD_LOOSE_ROOT);
    roots[0].name = NULL;
    roots[0].enabled = 1;
    num_roots = 1;

    {
        void *mod_entries = malloc(SALTYSD_DIRECTORY_BATCH * sizeof(DirectoryEntry));
        void *mod_handle;
        u16 *mod_root = malloc(SALTYSD_MAX_PATH * sizeof(u16));
        dumb_mbstowcs(mod_root, SALTYSD_MOD_ROOT);

        if ((OpenDirectory(&mod_handle, mod_root) & 0x80000000) == 0) {
            u32 found = 0;
            do {
                found = 0;
                ReadDirectory(&found, mod_handle, mod_entries, SALTYSD_DIRECTORY_BATCH);

                for (int j = 0; j < found && num_roots < SALTYSD_MAX_ROOTS; j++) {
                    DirectoryEntry *mod = mod_entries + j * sizeof(DirectoryEntry);
                    if (!mod->is_directory)
                        continue;

                    saltysd_root mod_root_rec;
                    u16 mod_path[SALTYSD_MAX_PATH];
                    copy_entry_path(mod_path, mod);

                    mod_root_rec.name = malloc(SALTYSD_MAX_MOD_NAME);
                    dumb_wcstombsn(mod_root_rec.name, mod_path, SALTYSD_MAX_MOD_NAME);

                    mod_root_rec.path = malloc(SALTYSD_ROOT_PATH_SIZE);
                    dumb_strcpy(mod_root_rec.path, SALTYSD_MOD_ROOT);
                    dumb_strcat(mod_root_rec.path, "/");
                    dumb_strcat(mod_root_rec.path, mod_root_rec.name);

                    mod_root_rec.prefix = malloc(SALTYSD_ROOT_PATH_SIZE);
                    dumb_strcpy(mod_root_rec.prefix, SALTYSD_SD_MOD_ROOT);
                    dumb_strcat(mod_root_rec.prefix, mod_root_rec.name);
                    dumb_strcat(mod_root_rec.prefix, "/");

                    saltysd_mod_config(&mod_root_rec, ifile_handle);
                    if (!mod_root_rec.enabled) {
                        printf("SaltySD mod disabled %s", mod_root_rec.name);
                        free(mod_root_rec.prefix);
                        free(mod_root_rec.path);
                        free(mod_root_rec.name);
                        continue;
                    }

                    u32 at = 1;
                    while (at < num_roots && strcmp(roots[at].name, mod_root_rec.name) < 0)
                        at++;

                    for (u32 k = num_roots; k > at; k--)
                        copy_root(&roots[k], &roots[k - 1]);

                    copy_root(&roots[at], &mod_root_rec);
                    printf("SaltySD mod root %s", roots[at].path);
                    num_roots++;
                }
            } while (found == SALTYSD_DIRECTORY_BATCH);

            CloseDirectory(mod_handle);
        }

        free(mod_root);
        free(mod_entries);
    }
    u64 t_roots = ticks();

    //One byte of root per resource id. Indexed by id rather than ordinal, so it
    //has to be shifted alongside the entries whenever a new one is inserted.
    u8 *root_table = malloc(SALTYSD_ID_SPACE);
    if (root_table)
        memclr(root_table, SALTYSD_ID_SPACE);

    idx_header key;
    idx_key(&key, header, roots, num_roots, entries_before, entrysection_before,
            header->stringsection_size);

    char index_path[SALTYSD_INDEX_NAME + sizeof(SALTYSD_INDEX_PATH)];
    idx_path(index_path, &key);

    if (root_table) {
        idx_header probe;
        u32 got = file_get(index_path, &probe, sizeof(probe), ifile_handle);

        if (got == sizeof(probe) && probe.magic == SALTYSD_INDEX_MAGIC &&
            probe.total_size >= sizeof(probe) && probe.total_size <= SALTYSD_INDEX_MAX) {
            u8 *index = malloc(probe.total_size);
            u32 size = index ? file_get(index_path, index, probe.total_size, ifile_handle) : 0;

            idx_tree tree;
            tree.header = header;
            tree.entries = (rf_entry *)entries;
            tree.blocks = blocks;
            tree.string_block_count = string_block_count;
            tree.extensions_block = extensions_block;
            tree.root_table = root_table;

            saltysd_named *named = NULL;
            u32 num_named = 0;

            if (size == probe.total_size && idx_usable((idx_header *)index, size, &key, &tree) &&
                idx_apply((idx_header *)index, &tree, roots, num_roots, &named, &num_named)) {
                u32 applied_entries = ((idx_header *)index)->num_inserts;
                u32 applied_files = ((idx_header *)index)->num_overrides;
                free(index);

                u64 t_applied = ticks();

                u32 hash = 2166136261u;
                u32 tree_sizes[4];
                tree_sizes[0] = header->resourceentry_amt;
                tree_sizes[1] = header->entrysection_size;
                tree_sizes[2] = header->stringsection_size;
                tree_sizes[3] = header->contents_size;
                hash = fnv1a(hash, tree_sizes, sizeof(tree_sizes));
                hash = fnv1a(hash, entries, header->resourceentry_amt * sizeof(rf_entry));
                hash = fnv1a(hash, string_section_next, header->stringsection_size);
                hash = fnv1a(hash, root_table, SALTYSD_ID_SPACE);
                for (u32 i = 0; i < num_named; i++) {
                    hash = fnv1a(hash, named[i].path, strlen(named[i].path) + 1);
                    hash = fnv1a(hash, &named[i].root, sizeof(named[i].root));
                }
                u64 t_hash = ticks();

                char *log = malloc(SALTYSD_LOG_SIZE);
                if (log) {
                    log[0] = 0;
                    log_str(log, "SaltySD boot log 1\nindexed roots ");
                    log_dec(log, num_roots);
                    log_str(log, " named ");
                    log_dec(log, num_named);
                    log_str(log, "\nentries ");
                    log_dec(log, entries_before);
                    log_str(log, " -> ");
                    log_dec(log, header->resourceentry_amt);
                    log_str(log, " added ");
                    log_dec(log, applied_entries);
                    log_str(log, " overrides ");
                    log_dec(log, applied_files);
                    log_str(log, "\ncalib ");
                    log_dec(log, calib_ticks);
                    log_str(log, " loops ");
                    log_dec(log, SALTYSD_CALIB_LOOPS);
                    log_str(log, " ticks\n");
                    log_phase(log, "roots ", t_roots - t_start);
                    log_phase(log, "index ", t_applied - t_roots);
                    log_phase(log, "total ", t_applied - t_start);
                    log_phase(log, "hash  ", t_hash - t_applied);
                    log_str(log, "tree ");
                    log_hex(log, hash);
                    log_str(log, "\n");
                    write_log(log, ifile_handle);
                    free(log);
                }

                saltysd_map *map = malloc(sizeof(saltysd_map));
                map->magic = SALTYSD_MAGIC;
                map->num_roots = num_roots;
                map->root_of = root_table;
                map->roots = roots;
                map->num_named = num_named;
                map->named = named;
                header->timestamp = (u32)map;

                free(extensions);
                free(blocks);
                free(ifile_handle);
                unmount_path("sd");
                return;
            }

            free(index);
        }
    }

    u32 num_directories = num_roots;
    u32 num_files = 0;
    u32 dirs_skipped = 0;
    u32 files_skipped = 0;
    void *dir_entries = malloc(SALTYSD_DIRECTORY_BATCH * sizeof(DirectoryEntry));
    void *dir_handle;

    u16 **dirs = malloc(SALTYSD_MAX_DIRS * sizeof(u16 *));
    u8 *dir_roots = malloc(SALTYSD_MAX_DIRS * sizeof(u8));
    char **files = malloc(SALTYSD_MAX_FILES * sizeof(char *));
    u32 *file_sizes = malloc(SALTYSD_MAX_FILES * sizeof(u32));
    u8 *file_roots = malloc(SALTYSD_MAX_FILES * sizeof(u8));

    for (int i = 0; i < num_roots; i++) {
        u16 *root_path = malloc(SALTYSD_MAX_PATH * sizeof(u16));
        dumb_mbstowcs(root_path, roots[i].path);
        dirs[i] = root_path;
        dir_roots[i] = i;
    }

    for (int i = 0; i < num_directories; i++) {
        u32 num_files_folders = 0;
        if ((OpenDirectory(&dir_handle, dirs[i]) & 0x80000000) == 0) {
            //ReadDirectory only hands back as many entries as we ask for and
            //then advances, so keep asking until it comes up short. Reading
            //one batch drops everything past the 0x40th entry of a folder.
            do {
                num_files_folders = 0;
                ReadDirectory(&num_files_folders, dir_handle, dir_entries, SALTYSD_DIRECTORY_BATCH);

                for (int j = 0; j < num_files_folders; j++) {
                    DirectoryEntry *dir_entry = dir_entries + j * sizeof(DirectoryEntry);
                    u16 entry_path[SALTYSD_MAX_PATH];
                    copy_entry_path(entry_path, dir_entry);

                    if (dir_entry->is_directory) {
                        //Checked before anything is allocated: the queue array
                        //and the path buffer are both fixed, so a subtree that
                        //will not fit is left unscanned and counted.
                        u32 dir_len = dumb_wcslen(dirs[i]) + 1 + dumb_wcslen(entry_path);
                        if (num_directories >= SALTYSD_MAX_DIRS || dir_len >= SALTYSD_MAX_PATH) {
                            dirs_skipped++;
                            continue;
                        }

                        u16 *new_dir = malloc(SALTYSD_MAX_PATH * sizeof(u16));
                        new_dir[0] = 0;

                        dumb_wcscat(new_dir, dirs[i]);
                        dumb_wcscat(new_dir, (u16 *)L"/");
                        dumb_wcscat(new_dir, entry_path);
                        dirs[num_directories] = new_dir;
                        dir_roots[num_directories] = dir_roots[i];
                        num_directories++;
                    } else {
                        u32 root_len = strlen(roots[dir_roots[i]].path) + 1;
                        u32 rel_len = dumb_wcslen(entry_path);
                        if (i >= num_roots)
                            rel_len += dumb_wcslen(dirs[i]) - root_len + 1;

                        if (rel_len >= SALTYSD_MAX_PATH) {
                            files_skipped++;
                            continue;
                        }

                        char *file = malloc(SALTYSD_MAX_PATH);
                        file[0] = 0;

                        if (i >= num_roots) {
                            dumb_wcstombs(file, dirs[i] + root_len);
                            dumb_strcat(file, "/");
                        }
                        dumb_wcstombs(file + strlen(file), entry_path);
                        //printf("List: %s", file);

                        if (i == 0) {
                            //A revoke name is at least "revoke.txt". Check
                            //that length before forming the suffix pointer.
                            u32 file_len = strlen(file);
                            if (file_len >= strlen("revoke.txt") && starts_with(file, "revoke") &&
                                !strcmp(file + file_len - strlen(".txt"), ".txt")) {
                                char *temp_real_path =
                                    malloc(SALTYSD_ROOT_PATH_SIZE + 1 + SALTYSD_MAX_PATH);
                                dumb_strcpy(temp_real_path, roots[dir_roots[i]].path);
                                dumb_strcat(temp_real_path, "/");
                                dumb_strcat(temp_real_path, file);
                                IFile_Init(ifile_handle);
                                if (IFile_Open(ifile_handle, temp_real_path, 1)) {
                                    u32 revoke_size = IFile_GetSize(ifile_handle);
                                    u32 revoke_read = 0;
                                    char *revoke_temp_buf = malloc(revoke_size + 1);
                                    IFile_Read(ifile_handle, revoke_temp_buf, revoke_size,
                                               &revoke_read);
                                    IFile_Close(ifile_handle);

                                    if (revoke_read > revoke_size)
                                        revoke_read = revoke_size;
                                    revoke_temp_buf[revoke_read] = 0;

                                    u32 revoke_held = revoke_buf ? strlen(revoke_buf) : 0;
                                    char *new_alloc = malloc(revoke_held + 1 + revoke_read + 1);
                                    new_alloc[0] = 0;
                                    if (revoke_buf) {
                                        dumb_strcpy(new_alloc, revoke_buf);
                                        free(revoke_buf);
                                    }
                                    revoke_buf = new_alloc;

                                    dumb_strcat(revoke_buf, "\n");
                                    dumb_strcat(revoke_buf, revoke_temp_buf);
                                    revoke_total_size = strlen(revoke_buf);
                                    free(revoke_temp_buf);
                                }
                                free(temp_real_path);
                                free(file);
                                continue;
                            }
                        }

                        //Checked here rather than at the top of the branch: a
                        //revoke list is read and then dropped without taking a
                        //slot, and a full array must not stop one being read.
                        if (num_files >= SALTYSD_MAX_FILES) {
                            files_skipped++;
                            free(file);
                            continue;
                        }

                        files[num_files] = file;
                        file_sizes[num_files] = dir_entry->file_size & 0xFFFFFFFF;
                        file_roots[num_files] = dir_roots[i];
                        num_files++;
                    }
                }
            } while (num_files_folders == SALTYSD_DIRECTORY_BATCH);

            CloseDirectory(dir_handle);
        }

        free(dirs[i]);
    }

    free(dirs);
    free(dir_entries);
    u64 t_walk = ticks();

    if (dirs_skipped)
        printf("SaltySD %x folders left unscanned: no room in the scan", dirs_skipped);
    if (files_skipped)
        printf("SaltySD %x files left out of the scan", files_skipped);

    //Parse revoked files
    if (revoke_buf) {
        revoke_count = 1;
        int revoke_active_count = 0;
        for (int i = 0; i < revoke_total_size; i++) {
            if (revoke_buf[i] == '\n')
                revoke_count++;
        }
        revoked_files = malloc(revoke_count * sizeof(char *));

        char *last_file = revoke_buf;
        for (int i = 0; i < revoke_total_size; i++) {
            if (revoke_buf[i] == '\n') {
                revoke_buf[i] = 0;

                //I don't know how these Windows newlines work, but they're annoying
                //and I hate them.
                //The buffer opens with the separator, so index 0 is a newline on
                //every parse and i-1 is off the front of the allocation.
                if (i > 0 && revoke_buf[i - 1] == '\r') {
                    revoke_buf[i - 1] = 0;
                }
                if (revoke_buf[i + 1] == '\r') {
                    revoke_buf[++i] = 0;
                }

                if (strlen(last_file) > 0)
                    revoked_files[revoke_active_count++] = last_file;
                last_file = &revoke_buf[i + 1];
            }
        }
        revoke_count = revoke_active_count;

        heap_sort((u32 *)revoked_files, revoke_count, cmp_string, NULL);
    }

    //Hold back the two channels that live outside the resource tree, before the
    //fixup pass can match them or the insertion pass register them. cro/ and
    //sound/bgm/ have no chunk to point at, so such an entry can never be served.
    u32 num_named = 0;
    for (int i = 0; i < num_files; i++) {
        if (files[i] &&
            (starts_with(files[i], SALTYSD_CRO_DIR) || starts_with(files[i], SALTYSD_BGM_DIR)))
            num_named++;
    }

    saltysd_named *named = num_named ? malloc(num_named * sizeof(saltysd_named)) : NULL;
    u32 named_count = 0;
    for (int i = 0; i < num_files; i++) {
        if (files[i] == NULL)
            continue;

        if (!starts_with(files[i], SALTYSD_CRO_DIR) && !starts_with(files[i], SALTYSD_BGM_DIR))
            continue;

        int at = -1;
        for (u32 j = 0; j < named_count; j++) {
            if (!strcmp(named[j].path, files[i])) {
                at = j;
                break;
            }
        }

        if (at < 0) {
            printf("SaltySD named %x %s", file_roots[i], files[i]);
            named[named_count].path = files[i];
            named[named_count].root = file_roots[i] + 1;
            named_count++;
        } else {
            report_conflict(roots, named[at].root - 1, file_roots[i], files[i]);

            if (file_roots[i] + 1 < named[at].root) {
                free(named[at].path);
                named[at].path = files[i];
                named[at].root = file_roots[i] + 1;
            } else
                free(files[i]);
        }

        files[i] = NULL;
    }
    num_named = named_count;
    u64 t_prep = ticks();

    u32 num_sorted = 0;
    for (int i = 0; i < num_files; i++) {
        if (files[i])
            num_sorted++;
    }

    u32 *sorted_order = num_sorted ? malloc(num_sorted * sizeof(u32)) : NULL;
    u32 *file_keys = num_sorted ? malloc(num_sorted * sizeof(u32)) : NULL;
    u32 *file_slots = num_sorted ? malloc(num_sorted * sizeof(u32)) : NULL;
    u32 num_keys = 0;
    {
        u32 at = 0;
        for (int i = 0; i < num_files; i++) {
            if (files[i])
                sorted_order[at++] = i;
        }

        file_order order;
        order.files = files;
        order.roots = file_roots;
        heap_sort(sorted_order, num_sorted, cmp_file, &order);
    }

    for (u32 g = 0; g < num_sorted;) {
        u32 end = g + 1;
        while (end < num_sorted && !strcmp(files[sorted_order[g]], files[sorted_order[end]]))
            end++;

        u32 winner = sorted_order[g];
        u32 slot = winner;
        for (u32 k = g + 1; k < end; k++) {
            u32 loser = sorted_order[k];
            report_conflict(roots, file_roots[winner], file_roots[loser], files[loser]);
            if (loser < slot)
                slot = loser;
        }

        if (slot != winner) {
            free(files[slot]);
            files[slot] = files[winner];
            file_sizes[slot] = file_sizes[winner];
            file_roots[slot] = file_roots[winner];
            files[winner] = NULL;
        }

        for (u32 k = g + 1; k < end; k++) {
            u32 loser = sorted_order[k];
            if (loser != slot) {
                free(files[loser]);
                files[loser] = NULL;
            }
        }

        file_keys[num_keys] = (u32)files[slot];
        file_slots[num_keys] = slot;
        num_keys++;
        g = end;
    }
    free(sorted_order);
    u64 t_sort = ticks();

    idx_rec rec;
    rec.num_revokes = rec.num_overrides = rec.num_strings = 0;
    rec.num_exts = rec.num_inserts = rec.text_len = 0;
    rec.max_revokes = revoke_count;
    rec.max_overrides = num_keys;
    rec.max_strings = SALTYSD_ENTRY_RESERVE + SALTYSD_MAX_EXTENSIONS;
    rec.max_exts = SALTYSD_MAX_EXTENSIONS;
    rec.max_inserts = SALTYSD_ENTRY_RESERVE;
    rec.text_max = SALTYSD_INDEX_TEXT;
    rec.revokes = rec.max_revokes ? malloc(rec.max_revokes * sizeof(u32)) : NULL;
    rec.overrides = rec.max_overrides ? malloc(rec.max_overrides * sizeof(idx_override)) : NULL;
    rec.strings = malloc(rec.max_strings * sizeof(idx_string));
    rec.exts = malloc(rec.max_exts * sizeof(idx_ext));
    rec.inserts = malloc(rec.max_inserts * sizeof(idx_insert));
    rec.text = malloc(rec.text_max);
    rec.on = (!rec.max_revokes || rec.revokes) && (!rec.max_overrides || rec.overrides) &&
             rec.strings && rec.exts && rec.inserts && rec.text;

    char *full_name = malloc(SALTYSD_FULL_NAME_SIZE);
    memclr(full_name, SALTYSD_FULL_NAME_SIZE);
    u32 last_str_addr = 0;
    for (int i = 0; i < header->resourceentry_amt; i++) {
        u32 string_offset_all = (*entries)[i].string_offs;
        u32 string_offset = string_offset_all & 0x000FFFFF;
        u8 extension = (string_offset_all >> 24);

        if (string_offset > last_str_addr)
            last_str_addr = string_offset;

        u8 nesting_level = (*entries)[i].flags & 0xFF;
        if (nesting_level <= 1)
            full_name[0] = 0;

        u8 levels = 1;
        for (int i = 0; i < SALTYSD_MAX_PATH; i++) {
            if (full_name[i] == 0x0)
                break;

            if (full_name[i] == '/')
                levels++;

            if (levels >= nesting_level) {
                full_name[i + 1] = 0x0;
                break;
            }
        }

        char *string = blocks[string_offset / SALTYSD_STRING_BLOCK_SIZE] +
                       (string_offset & SALTYSD_STRING_BLOCK_MASK);

        if (string_offset_all & 0x00800000) {
            u16 reference = read_u16le(string);
            u32 ref_len = (reference & 0x1f) + 4;
            u32 ref_reloff = (reference & 0xe0) >> 6 << 8 | (reference >> 8);
            u32 final_offset = string_offset - ref_reloff;
            char *ref_string = blocks[final_offset / SALTYSD_STRING_BLOCK_SIZE] +
                               (final_offset & SALTYSD_STRING_BLOCK_MASK);

            dumb_strncat(full_name, ref_string, ref_len);
            dumb_strcat(full_name, string + sizeof(u16));
        } else
            dumb_strcat(full_name, string);

        dumb_strcat(full_name, extensions[extension]);

        bool existing_revoked = false;

        //Check the file against revoked list
        if (revoked_files && find_string((u32 *)revoked_files, revoke_count, full_name) >= 0) {
            existing_revoked = true;
            (*entries)[i].string_offs &= 0xFFF00000; //If it's revoked, revoke its path name
            idx_revoke(&rec, i);
        }

        //If we have a file, adjust file sizes
        if (full_name[strlen(full_name) - 1] != '/') {
            int key = find_string(file_keys, num_keys, full_name);
            u32 winner = key >= 0 ? file_slots[key] : 0;

            if (key >= 0 && files[winner]) {
                if (!existing_revoked) {
                    //By overriding the compressed size, our files are forced into only one hook
                    (*entries)[i].comp_size = file_sizes[winner];
                    (*entries)[i].decomp_size = file_sizes[winner];
                    (*entries)[i].flags |= 0x8000;
                    root_table[i + SALTYSD_ID_BIAS] = file_roots[winner] + 1;
                    idx_override_add(&rec, i, file_sizes[winner], file_roots[winner] + 1);
                }

                files[winner] = NULL;
            }
        }
    }
    u64 t_match = ticks();

    for (u32 k = 0; k < num_keys; k++) {
        if (files[file_slots[k]] == NULL)
            free((char *)file_keys[k]);
    }
    free(file_keys);
    free(file_slots);

    //Add new files to RF
    u32 string_limit = string_block_count * SALTYSD_STRING_BLOCK_SIZE;
    u32 entries_added = 0;
    u32 entries_skipped = 0;
    last_str_addr = ((*entries)[header->resourceentry_amt - 1].string_offs & 0xFFFFF) + 0x80;
    for (int i = 0; i < num_files; i++) {
        if (files[i] == NULL)
            continue;

        //Check the file against our revoked list
        if (revoked_files && find_string((u32 *)revoked_files, revoke_count, files[i]) >= 0)
            continue;

        printf("Adding file %s", files[i]);

        u32 entry_to_shift = 1;
#if SALTYSD_DEBUG
        u8 entered_packed = 0;
#endif
        u8 level_target = 1;
        char *substr = malloc(SALTYSD_MAX_PATH);

        u32 seed_len = len_to(files[i], '/');
        if (seed_len == -1) {
            entry_to_shift = header->resourceentry_amt;

            dumb_strcpy(substr, files[i]);
            seed_len = strlen(substr);
        } else
            dumb_strncpy(substr, files[i], seed_len);

        for (; entry_to_shift < header->resourceentry_amt; entry_to_shift++) {
            u8 nesting_level = (*entries)[entry_to_shift].flags & 0xFF;

            //We're a file but the next object at the same level is a folder, break here
            if (level_target == nesting_level && len_to(substr, '/') == -1 &&
                (*entries)[entry_to_shift].flags & 0x200)
                break;

            //We're a file and the next entry isn't even at the same nesting level, break
            if (level_target != nesting_level && len_to(substr, '/') == -1)
                break;

            //We're a higher folder and we descended a folder, obviously this folder is new
            if (level_target > nesting_level && len_to(substr, '/') != -1)
                break;

            //Don't look at deeper folders while trying to find our current level
            if (level_target < nesting_level && len_to(substr, '/') != -1)
                continue;

            //We're a folder, pay no mind to the file order since files come before folders
            if (len_to(substr, '/') != -1 && !((*entries)[entry_to_shift].flags & 0x200))
                continue;

            u32 string_offset_all = (*entries)[entry_to_shift].string_offs;
            u32 string_offset = string_offset_all & 0x000FFFFF;

            char *string = blocks[string_offset / SALTYSD_STRING_BLOCK_SIZE] +
                           (string_offset & SALTYSD_STRING_BLOCK_MASK);

            if (string_offset_all & 0x00800000) {
                u16 reference = read_u16le(string);
                u32 ref_len = (reference & 0x1f) + 4;
                u32 ref_reloff = (reference & 0xe0) >> 6 << 8 | (reference >> 8);
                u32 final_offset = string_offset - ref_reloff;
                char *ref_string = blocks[final_offset / SALTYSD_STRING_BLOCK_SIZE] +
                                   (final_offset & SALTYSD_STRING_BLOCK_MASK);

                dumb_strncpy(full_name, ref_string, ref_len);
                dumb_strcat(full_name, string + sizeof(u16));
            } else
                dumb_strcpy(full_name, string);

            //Folder is part of our path, advance level target and look for next folder
            //or the spot to place our file
            if (!strcmp(full_name, substr) && len_to(substr, '/') != -1) {
                printf("%x %x %s", entry_to_shift, level_target, full_name);
                u32 len = len_to(files[i] + seed_len, '/');
                if (len != -1) {
                    dumb_strncpy(substr, files[i] + seed_len, len);
                    seed_len += len;
                } else {
                    dumb_strcpy(substr, files[i] + seed_len);
                    seed_len += strlen(substr);
                }

                if ((*entries)[entry_to_shift].flags & 0x1000) {
#if SALTYSD_DEBUG
                    entered_packed = 1;
#endif
                    printf("entered packed");
                }
                level_target++;
            } else if (strcmp(full_name, substr) > 0 && level_target != 1) {
                //Greater alphabetically, break here
                printf("larger %x %x %s", entry_to_shift, level_target, full_name);
                break;
            }
        }

        printf("entry comp %x %x %x", &(*entries)[entry_to_shift],
               &(*entries)[header->resourceentry_amt - 1],
               (u32) & (*entries)[header->resourceentry_amt - 1] - (u32) &
                   (*entries)[entry_to_shift]);
        u32 entries_to_make = count_chars(files[i] + seed_len - strlen(substr), '/') + 1;

        printf("adding %x entries after %x (%s)", entries_to_make, entry_to_shift,
               files[i] + seed_len - strlen(substr));

        char *tail = files[i] + seed_len - strlen(substr);
        u32 probe = last_str_addr;
        bool fits =
            entries_added + entries_to_make <= ENTRY_RESERVE &&
            header->resourceentry_amt + entries_to_make + SALTYSD_ID_BIAS <= SALTYSD_ID_SPACE;

        for (u32 j = 0; fits && j < entries_to_make; j++) {
            u32 seg = len_to(tail, '/');
            //The last component loses its extension before it is written, so
            //its whole length is an overestimate.
            u32 seg_len = (seg == -1) ? strlen(tail) : seg - 1;

            if (string_alloc(&probe, string_limit, seg_len) == SALTYSD_NO_STRING)
                fits = false;

            if (seg == -1)
                break;
            tail += seg;
        }

        //And one more string, in case the extension is one the table has not
        //seen yet, sized by the buffer it is read into.
        if (fits && string_alloc(&probe, string_limit, 0x10 - 1) == SALTYSD_NO_STRING)
            fits = false;

        if (!fits) {
            printf("SaltySD no room left, dropping %s", files[i]);
            entries_skipped++;
            free(substr);
            free(files[i]);
            files[i] = NULL;
            continue;
        }

        entries_added += entries_to_make;

        if (entry_to_shift != header->resourceentry_amt) {
            memmove(&(*entries)[entry_to_shift + entries_to_make], &(*entries)[entry_to_shift],
                    (u32) & (*entries)[header->resourceentry_amt] - (u32) &
                        (*entries)[entry_to_shift]);
            memmove(root_table + entry_to_shift + entries_to_make + SALTYSD_ID_BIAS,
                    root_table + entry_to_shift + SALTYSD_ID_BIAS,
                    header->resourceentry_amt - entry_to_shift);
        }

        memclr(&(*entries)[entry_to_shift], 0x18 * entries_to_make);
        memclr(root_table + entry_to_shift + SALTYSD_ID_BIAS, entries_to_make);

        //Create all our new folders
        for (int j = 0; j < entries_to_make - 1; j++) {
            u32 folder_str = string_alloc(&last_str_addr, string_limit, strlen(substr));
            char *new_str = blocks[folder_str / SALTYSD_STRING_BLOCK_SIZE] +
                            (folder_str & SALTYSD_STRING_BLOCK_MASK);
            dumb_strcpy(new_str, substr);
            printf("folder: %s", new_str);

            (*entries)[entry_to_shift].chunk_offs = (*entries)[entry_to_shift - 1].chunk_offs;
            (*entries)[entry_to_shift].string_offs = folder_str;
            (*entries)[entry_to_shift].comp_size = 0x80;
            (*entries)[entry_to_shift].decomp_size = 0x80;
            (*entries)[entry_to_shift].timestamp = 0;
            (*entries)[entry_to_shift].flags = 0x8000 | 0xA00 | level_target;
            idx_string_add(&rec, folder_str, substr);
            idx_insert_add(&rec, entry_to_shift, folder_str, 0x80, 0x8000 | 0xA00 | level_target,
                           0);
            header->resourceentry_amt++;
            header->entrysection_size += 0x18;
            entry_to_shift++;

            printf("%x %x %s", entry_to_shift - 1, level_target, substr);

            u32 len = len_to(files[i] + seed_len, '/');
            if (len != -1) {
                dumb_strncpy(substr, files[i] + seed_len, len);
                seed_len += len;
            } else {
                dumb_strcpy(substr, files[i] + seed_len);
            }
            level_target++;
        }

        u32 dot = last_index_of(substr, '.');
        char *file_ext = malloc(SALTYSD_MAX_EXT);
        //Read whether or not there was a dot; an empty extension is what an
        //extensionless file matches.
        file_ext[0] = 0;
        if (dot != -1 && dot != 0) {
            //The name is up to SALTYSD_MAX_PATH and this buffer is sixteen
            //bytes, so a longer suffix is truncated for extension lookup.
            u32 ext_len = strlen(&substr[dot]);
            if (ext_len > SALTYSD_MAX_EXT - 1)
                ext_len = SALTYSD_MAX_EXT - 1;

            dumb_strncpy(file_ext, &substr[dot], ext_len);
            substr[dot] = 0;
            seed_len += dot;
        }

        //Find our extension ID
        printf("%x %x %s", entry_to_shift, level_target, substr);
        u8 ext_num = 0;

        for (int j = 0; j < *(u32 *)extensions_block; j++) {
            if (!strcmp(extensions[j], file_ext)) {
                printf("extnum %x %s", j, extensions[j]);
                ext_num = j;
                break;
            }
        }

        //New extension...
        if (ext_num == 0 && file_ext[0] && *(u32 *)extensions_block < SALTYSD_MAX_EXTENSIONS) {
            u32 ext_str = string_alloc(&last_str_addr, string_limit, strlen(file_ext));
            char *new_str =
                blocks[ext_str / SALTYSD_STRING_BLOCK_SIZE] + (ext_str & SALTYSD_STRING_BLOCK_MASK);
            dumb_strcpy(new_str, file_ext);
            printf("adding ext %s", new_str);

            *(u32 *)(extensions_block + sizeof(u32) + *(u32 *)extensions_block * sizeof(u32)) =
                ext_str;
            ext_num = *(u32 *)extensions_block;
            idx_string_add(&rec, ext_str, file_ext);
            idx_ext_add(&rec, ext_num, ext_str);
            extensions[*(u32 *)extensions_block] = new_str;
            *(u32 *)extensions_block += 1;
        }

        free(file_ext);

        u32 file_str = string_alloc(&last_str_addr, string_limit, strlen(substr));
        char *new_str =
            blocks[file_str / SALTYSD_STRING_BLOCK_SIZE] + (file_str & SALTYSD_STRING_BLOCK_MASK);
        dumb_strcpy(new_str, substr);

        //Add new file entry
        (*entries)[entry_to_shift].chunk_offs = (*entries)[entry_to_shift - 1].chunk_offs;
        (*entries)[entry_to_shift].string_offs = file_str | ext_num << 24;
        (*entries)[entry_to_shift].comp_size = file_sizes[i];
        (*entries)[entry_to_shift].decomp_size = file_sizes[i];
        (*entries)[entry_to_shift].timestamp = 0;
        (*entries)[entry_to_shift].flags = 0x8000 | 0xC00 | level_target;
        root_table[entry_to_shift + SALTYSD_ID_BIAS] = file_roots[i] + 1;
        idx_string_add(&rec, file_str, substr);
        idx_insert_add(&rec, entry_to_shift, file_str | ext_num << 24, file_sizes[i],
                       0x8000 | 0xC00 | level_target, file_roots[i] + 1);
        header->resourceentry_amt++;
        header->entrysection_size += 0x18;

        printf("flags: %x, %s", (*entries)[entry_to_shift].flags,
               entered_packed ? "entered packed" : "didn't enter packed");

        free(substr);
        free(files[i]);
        files[i] = NULL;
    }

    if (entries_skipped)
        printf("SaltySD %x new files dropped: the insertion reserve is full", entries_skipped);
    u64 t_insert = ticks();

    u32 hash = 2166136261u;
    u32 tree_sizes[4];
    tree_sizes[0] = header->resourceentry_amt;
    tree_sizes[1] = header->entrysection_size;
    tree_sizes[2] = header->stringsection_size;
    tree_sizes[3] = header->contents_size;
    hash = fnv1a(hash, tree_sizes, sizeof(tree_sizes));
    hash = fnv1a(hash, entries, header->resourceentry_amt * sizeof(rf_entry));
    hash = fnv1a(hash, string_section_next, header->stringsection_size);
    hash = fnv1a(hash, root_table, SALTYSD_ID_SPACE);
    for (u32 i = 0; i < num_named; i++) {
        hash = fnv1a(hash, named[i].path, strlen(named[i].path) + 1);
        hash = fnv1a(hash, &named[i].root, sizeof(named[i].root));
    }
    u64 t_hash = ticks();

    u32 put = idx_write(&rec, &key, index_path, roots, num_roots, named, num_named, ifile_handle);
    bool indexed = rec.on;
    free(rec.revokes);
    free(rec.overrides);
    free(rec.strings);
    free(rec.exts);
    free(rec.inserts);
    free(rec.text);
    u64 t_index = ticks();

    char *log = malloc(SALTYSD_LOG_SIZE);
    log[0] = 0;
    log_str(log, "SaltySD boot log 1\nroots ");
    log_dec(log, num_roots);
    log_str(log, " files ");
    log_dec(log, num_files);
    log_str(log, " named ");
    log_dec(log, num_named);
    log_str(log, " revoke ");
    log_dec(log, revoke_count);
    log_str(log, "\nentries ");
    log_dec(log, entries_before);
    log_str(log, " -> ");
    log_dec(log, header->resourceentry_amt);
    log_str(log, " added ");
    log_dec(log, entries_added);
    log_str(log, " dropped ");
    log_dec(log, entries_skipped);
    log_str(log, "\ncalib ");
    log_dec(log, SALTYSD_CALIB_LOOPS);
    log_str(log, " loops ");
    log_dec(log, calib_ticks);
    log_str(log, " ticks\n");
    log_phase(log, "roots ", t_roots - t_start);
    log_phase(log, "walk  ", t_walk - t_roots);
    log_phase(log, "prep  ", t_prep - t_walk);
    log_phase(log, "sort  ", t_sort - t_prep);
    log_phase(log, "match ", t_match - t_sort);
    log_phase(log, "insert", t_insert - t_match);
    log_phase(log, "total ", t_insert - t_start);
    log_phase(log, "hash  ", t_hash - t_insert);
    log_phase(log, "index ", t_index - t_hash);
    log_str(log, "index ");
    log_str(log, index_path);
    log_str(log, " ");
    if (!indexed)
        log_str(log, "not recorded\n");
    else if (put == SALTYSD_PUT_NO_OPEN)
        log_str(log, "open failed\n");
    else if (put == SALTYSD_PUT_NO_STREAM)
        log_str(log, "no file object\n");
    else {
        log_dec(log, put);
        log_str(log, " bytes written\n");
    }
    log_str(log, "tree ");
    log_hex(log, hash);
    log_str(log, "\n");
    write_log(log, ifile_handle);
    free(log);

    free(full_name);
    free(files);
    free(extensions);
    free(blocks);
    free(ifile_handle);
    free(dir_roots);
    free(file_sizes);
    free(file_roots);
    saltysd_map *map = malloc(sizeof(saltysd_map));
    map->magic = SALTYSD_MAGIC;
    map->num_roots = num_roots;
    map->root_of = root_table;
    map->roots = roots;
    map->num_named = num_named;
    map->named = named;
    header->timestamp = (u32)map;

    unmount_path("sd");

    return;
}
