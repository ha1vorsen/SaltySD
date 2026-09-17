#include <3ds.h>
#include <stdarg.h>
#include "../../common.h"

#define SALTYSD_LOOSE_ROOT     "sd:/luma/titles/crs"
#define SALTYSD_SD_LOOSE_ROOT  "sdmc:/luma/titles/crs/"
#define SALTYSD_MOD_ROOT       "sd:/saltysd/smash"
#define SALTYSD_SD_MOD_ROOT    "sdmc:/saltysd/smash/"
#define SALTYSD_MAX_MODS       62
#define SALTYSD_MAX_ROOTS      (SALTYSD_MAX_MODS + 1)
#define SALTYSD_MAX_MOD_NAME   0x40
//Work arrays are totals across all roots, not per-mod
#define SALTYSD_MAX_DIRS       0x1000
#define SALTYSD_MAX_FILES      0x4000
#define SALTYSD_MAX_PATH       0x101
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

typedef struct __attribute__((__packed__))
{
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

typedef struct __attribute__((__packed__))
{
    u32 chunk_offs;
    u32 string_offs;
    u32 comp_size;
    u32 decomp_size;
    u32 timestamp;
    u32 flags;
} rf_entry;

typedef struct __attribute__((__packed__))
{
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

typedef struct
{
    char *path;     //"sd:/..." as the boot-time scan walks it
    char *prefix;   //"sdmc:/.../" as the load hooks spell it, trailing slash included
    char *name;     //mod folder name, NULL for the loose root
    u8 enabled;
} saltysd_root;

typedef struct
{
    char *path;     //root relative game path, ie "cro/fighter/falco"
    u8 root;        //winning root plus one, matching root_of
} saltysd_named;

static void (*memcpy)(void *dest, const void *src, size_t n) = (void*)memcpy_ADDR;
static void (*memmove)(void *dest, const void *src, size_t n) = (void*)memmove_ADDR;
static void* (*malloc)(size_t size) = (void*)liballoc_ADDR;
static void (*free)(void* ptr) = (void*)libdealloc_ADDR;
static void (*memclr)(void *ptr, size_t size) = (void*)memclr_ADDR;
static int (*strlen)(char *str) = (void*)strlen_ADDR;
static int (*strcmp)(const char *str1, const char *str2) = (void*)strcmp_ADDR;

static void copy_entry_path(u16 *dst, const DirectoryEntry *entry)
{
    memcpy(dst, entry, SALTYSD_MAX_PATH * sizeof(u16));
    dst[SALTYSD_MAX_PATH - 1] = 0;
}

#if SALTYSD_DEBUG
static int (*vsnprintf)(char * s, size_t n, const char * format, va_list arg ) = (void*)vsnprintf_ADDR;
#endif

static u32 (*IFile_Init)(void *handle) = (void*)IFile_Init_ADDR;
static u32 (*IFile_Open)(void *handle, char *path, u32 mode) = (void*)IFile_Open_ADDR;
static u32 (*IFile_Read)(void *handle, void *dest, size_t size, u32 *bytes_read) = (void*)IFile_Read_ADDR;
static u32 (*IFile_GetSize)(void *handle) = (void*)IFile_GetSize_ADDR;
static u32 (*IFile_Close)(void *handle) = (void*)IFile_Close_ADDR;

static u16* (*get_rf_struct)(u32 *id) = (void*)get_rf_struct_ADDR;

static void* (*crit_this)(void) = (void*)crit_this_ADDR;
static void* (*crit_init)(void* crit_inst) = (void*)crit_init_ADDR;
static u32 (*mount_sdmc)(char *mount_path) = (void*)mount_sdmc_ADDR;
static u32 (*unmount_path)(char *mount_path) = (void*)unmount_path_ADDR;

static u32 (*OpenDirectory)(void **handle, u16 *path) = (void*)OpenDirectory_ADDR;
static u32 (*ReadDirectory)(u32 *num_dirs, void *handle, void *out, u32 num_entries_toload) = (void*)ReadDirectory_ADDR;
static u32 (*CloseDirectory)(void *handle) = (void*)CloseDirectory_ADDR;

int dumb_wcslen(u16 *str)
{
    u32 len = 0;
    while(1)
    {
        if(str[len] == 0)
            return len;
        len++;
    }
}

char *dumb_strncat(char *dest, char *src, u32 len)
{
    void *copyinto = dest+strlen(dest);
    memcpy(copyinto, src, len);
    *(u8*)(copyinto+len) = 0;
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
    void *copyinto = dest+dumb_wcslen(dest);
    memcpy(copyinto, src, len*sizeof(u16));
    *(u16*)(copyinto+(len*sizeof(u16))) = 0;
    return dest;
}

u16 *dumb_wcscat(u16 *dest, u16 *src)
{
    return dumb_wcsncat(dest, src, dumb_wcslen(src));
}

u16 *dumb_mbstowcs(u16 *dest, char *src)
{
    u32 count = 0;
    while(1)
    {
        dest[count] = src[count];
        if(src[count] == 0)
            break;
        count++;
    }
    return dest;
}

char *dumb_wcstombs(char *dest, u16 *src)
{
    u32 count = 0;
    while(1)
    {
        dest[count] = (u8)(src[count] & 0xFF);
        if(src[count] == 0)
            break;
        count++;
    }
    return dest;
}

char *dumb_wcstombsn(char *dest, u16 *src, u32 len)
{
    u32 count = 0;
    while(count < len-1 && src[count])
    {
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
    while(*prefix)
    {
        if(*str++ != *prefix++)
            return false;
    }
    return true;
}

u32 len_to(char *str, char chr)
{
    u32 count = 0;
    while(1)
    {
        if(str[count] == 0)
            return -1;

        if(str[count++] == chr)
            break;
    }
    return count;
}

u32 last_index_of(char *str, char chr)
{
    u32 found = -1;
    for(u32 i = 0; str[i]; i++)
    {
        if(str[i] == chr)
            found = i;
    }
    return found;
}

u32 count_chars(char *str, char chr)
{
    u32 i = 0;
    u32 count = 0;
    while(1)
    {
        if(str[i] == 0)
            break;

        if(str[i++] == chr)
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
    __asm__ volatile("svc 0x3D" :: "r"(addr), "r"(len) : "memory");
}

void printf(char *format, ...)
{
    char *str = malloc(0x400);

    va_list argptr;
    va_start(argptr,format);
    vsnprintf(str, 0x400, format, argptr);
    va_end(argptr);

    dumb_strcat(str, "");
    debug_print(str);
    free(str);
}
#else

#define printf(...) ((void)0)
#endif

typedef struct
{
    u32 magic;
    u32 num_roots;
    u8 *root_of;
    saltysd_root *roots;
    u32 num_named;
    saltysd_named *named;
} saltysd_map;

static saltysd_map *saltysd_get_map(void)
{
    u32 singleton = *(u32*)something_resource_lock_ADDR;
    if(!singleton)
        return NULL;

    u32 header = *(u32*)(singleton + SALTYSD_HEADER_FROM_SINGLETON);
    if(!header)
        return NULL;

    saltysd_map *map = *(saltysd_map**)(header + SALTYSD_HEADER_TIMESTAMP);
    if(!map || map->magic != SALTYSD_MAGIC)
        return NULL;

    return map;
}

u32 saltysd_build_prefix(char *out, u32 id)
{
    saltysd_map *map = saltysd_get_map();
    if(!map)
        return 0;

    id &= 0xFFFF;
    for(int i = 0; i < 4 && id; i++)
    {
        u32 lookup = id;
        u16 source = *get_rf_struct(&lookup);
        if(!source || source == id)
            break;

        id = source;
    }

    u32 root = map->root_of[id & (SALTYSD_ID_SPACE-1)];
    if(!root || root > map->num_roots)
        return 0;

    char *prefix = map->roots[root-1].prefix;
    u32 len = strlen(prefix);

    memcpy(out, prefix, len);
    return len;
}

u32 saltysd_build_named_path(char *out, u32 out_size, u32 kind, char *name)
{
    saltysd_map *map = saltysd_get_map();
    if(!map || !map->num_named)
        return 0;

    u32 key_len = strlen(name) + (kind == SALTYSD_KIND_BGM
        ? sizeof(SALTYSD_BGM_DIR SALTYSD_BGM_STEM SALTYSD_BGM_EXT) - 1
        : sizeof(SALTYSD_CRO_DIR) - 1);
    if(key_len + 1 > out_size)
        return 0;

    out[0] = 0;
    if(kind == SALTYSD_KIND_BGM)
    {
        dumb_strcat(out, SALTYSD_BGM_DIR SALTYSD_BGM_STEM);
        dumb_strcat(out, name);
        dumb_strcat(out, SALTYSD_BGM_EXT);
    }
    else
    {
        dumb_strcat(out, SALTYSD_CRO_DIR);
        dumb_strcat(out, name);
    }

    u32 root = 0;
    for(u32 i = 0; i < map->num_named; i++)
    {
        if(!strcmp(map->named[i].path, out))
        {
            root = map->named[i].root;
            break;
        }
    }

    if(!root || root > map->num_roots)
        return 0;

    char *prefix = map->roots[root-1].prefix;
    u32 prefix_len = strlen(prefix);
    if(prefix_len + key_len + 1 > out_size)
        return 0;

    memmove(out + prefix_len, out, key_len + 1);
    memcpy(out, prefix, prefix_len);
    return prefix_len + key_len;
}

static void saltysd_mod_config(saltysd_root *mod)
{
    mod->enabled = 1;
}

static void report_conflict(saltysd_root *roots, u8 a, u8 b, char *path)
{
    if(a == 0 || b == 0)
        return;

    printf("SaltySD conflict %s: %s and %s", path, roots[a].name, roots[b].name);
}

#define SALTYSD_NO_STRING 0xFFFFFFFF
static u32 string_alloc(u32 *cursor, u32 limit, u32 len)
{
    u32 at = *cursor;
    if((at & 0x1FFF) + len >= 0x2000)
        at = (at + 0x1FFF) & (0xFFFFFFFF - 0x1FFF);

    if(at + len + 1 > limit)
        return SALTYSD_NO_STRING;

    *cursor = at + len + 1;
    return at;
}

void _main(rf_header* header, void *contents)
{
    //Room for new entries is carved out once and never grows
    const u32 ENTRY_RESERVE = 0x800;
    const u32 STRING_SHIFT = sizeof(rf_entry)*ENTRY_RESERVE;
    const u32 EXT_SHIFT = 0x2000*0x8*sizeof(u8);
    void *string_section_current = contents + (header->stringsection_start - header->contents_start);
    void *string_section_next = string_section_current + STRING_SHIFT;

    //Move string section to make room for new entries
    memmove(string_section_next, string_section_current, header->stringsection_size);
    memclr(string_section_current,STRING_SHIFT);
    header->stringsection_start += STRING_SHIFT;
    header->decompressed_size += STRING_SHIFT;
    header->contents_size += STRING_SHIFT;

    //Move extension chunk to make room for new strings
    memmove(string_section_next + (*(u32*)string_section_next * 0x2000 * sizeof(u8)) + EXT_SHIFT, string_section_next + (*(u32*)string_section_next * 0x2000 * sizeof(u8)), 0x2000);
    memclr(string_section_next + (*(u32*)string_section_next * 0x2000 * sizeof(u8)), EXT_SHIFT);
    header->stringsection_size += EXT_SHIFT;
    header->decompressed_size += EXT_SHIFT;
    header->contents_size += EXT_SHIFT;
    *(u32*)string_section_next += (EXT_SHIFT / 0x2000);

    //Set up our array of entries
    rf_entry (*entries)[] = contents + header->entrysection_start - header->contents_start;

    //Set up string blocks
    u32 block_size = *(u32*)string_section_next;
    void *extensions_block = string_section_next + sizeof(u32)  + (0x2000*block_size*sizeof(u8));
    void **blocks = malloc(block_size * sizeof(void*));
    for(int i = 0; i < block_size; i++)
    {
        blocks[i] = string_section_next + sizeof(u32) + (0x2000*i*sizeof(u8));
    }


    u32 ext_capacity = *(u32*)extensions_block;
    if(ext_capacity < SALTYSD_MAX_EXTENSIONS)
        ext_capacity = SALTYSD_MAX_EXTENSIONS;

    char **extensions = malloc(ext_capacity * sizeof(char*));
    for(int i = 0; i < *(u32*)extensions_block; i++)
    {
        u32 offs = *(u32*)(extensions_block + sizeof(u32) + i*sizeof(u32));
        char *string = blocks[offs / 0x2000] + (offs & 0x1FFF);
        extensions[i] = string;
    }

    void *ifile_handle = malloc(0x40);
    char *revoke_buf = NULL;
    char **revoked_files = NULL;
    u32 revoke_total_size = 0;
    int revoke_count = 0;
    crit_init(crit_this());
    mount_sdmc("sd:");


    u32 num_roots = 0;
    saltysd_root *roots = malloc(SALTYSD_MAX_ROOTS*sizeof(saltysd_root));

    roots[0].path = malloc(0x80);
    dumb_strcpy(roots[0].path, SALTYSD_LOOSE_ROOT);
    roots[0].prefix = malloc(0x80);
    dumb_strcpy(roots[0].prefix, SALTYSD_SD_LOOSE_ROOT);
    roots[0].name = NULL;
    roots[0].enabled = 1;
    num_roots = 1;

    {
        void *mod_entries = malloc(0x40*sizeof(DirectoryEntry));
        void *mod_handle;
        u16 *mod_root = malloc(0x101*sizeof(u16));
        dumb_mbstowcs(mod_root, SALTYSD_MOD_ROOT);

        if((OpenDirectory(&mod_handle, mod_root) & 0x80000000) == 0)
        {
            u32 found = 0;
            do
            {
                found = 0;
                ReadDirectory(&found, mod_handle, mod_entries, 0x40);

                for(int j = 0; j < found && num_roots < SALTYSD_MAX_ROOTS; j++)
                {
                    DirectoryEntry *mod = mod_entries + j*sizeof(DirectoryEntry);
                    if(!mod->is_directory)
                        continue;

                    saltysd_root mod_root_rec;
                    u16 mod_path[SALTYSD_MAX_PATH];
                    copy_entry_path(mod_path, mod);

                    mod_root_rec.name = malloc(SALTYSD_MAX_MOD_NAME);
                    dumb_wcstombsn(mod_root_rec.name, mod_path, SALTYSD_MAX_MOD_NAME);

                    mod_root_rec.path = malloc(0x80);
                    dumb_strcpy(mod_root_rec.path, SALTYSD_MOD_ROOT);
                    dumb_strcat(mod_root_rec.path, "/");
                    dumb_strcat(mod_root_rec.path, mod_root_rec.name);

                    mod_root_rec.prefix = malloc(0x80);
                    dumb_strcpy(mod_root_rec.prefix, SALTYSD_SD_MOD_ROOT);
                    dumb_strcat(mod_root_rec.prefix, mod_root_rec.name);
                    dumb_strcat(mod_root_rec.prefix, "/");

                    saltysd_mod_config(&mod_root_rec);
                    if(!mod_root_rec.enabled)
                    {
                        printf("SaltySD mod disabled %s", mod_root_rec.name);
                        free(mod_root_rec.prefix);
                        free(mod_root_rec.path);
                        free(mod_root_rec.name);
                        continue;
                    }

                    u32 at = 1;
                    while(at < num_roots && strcmp(roots[at].name, mod_root_rec.name) < 0)
                        at++;

                    for(u32 k = num_roots; k > at; k--)
                        copy_root(&roots[k], &roots[k-1]);

                    copy_root(&roots[at], &mod_root_rec);
                    printf("SaltySD mod root %s", roots[at].path);
                    num_roots++;
                }
            } while(found == 0x40);

            CloseDirectory(mod_handle);
        }

        free(mod_root);
        free(mod_entries);
    }

    //Iterate through every root for every single file
    u32 num_directories = num_roots;
    u32 num_files = 0;
    u32 dirs_skipped = 0;
    u32 files_skipped = 0;
    void *dir_entries = malloc(0x40*sizeof(DirectoryEntry));
    void *dir_handle;

    u16 **dirs = malloc(SALTYSD_MAX_DIRS*sizeof(u16*));
    u8 *dir_roots = malloc(SALTYSD_MAX_DIRS*sizeof(u8));
    char **files = malloc(SALTYSD_MAX_FILES*sizeof(char*));
    u32 *file_sizes = malloc(SALTYSD_MAX_FILES*sizeof(u32));
    u8 *file_roots = malloc(SALTYSD_MAX_FILES*sizeof(u8));

    for(int i = 0; i < num_roots; i++)
    {
        u16 *root_path = malloc(SALTYSD_MAX_PATH*sizeof(u16));
        dumb_mbstowcs(root_path, roots[i].path);
        dirs[i] = root_path;
        dir_roots[i] = i;
    }

    for(int i = 0; i < num_directories; i++)
    {
        u32 num_files_folders = 0;
        if((OpenDirectory(&dir_handle, dirs[i]) & 0x80000000) == 0)
        {
            //ReadDirectory only hands back as many entries as we ask for and
            //then advances, so keep asking until it comes up short. Reading
            //one batch drops everything past the 0x40th entry of a folder.
            do
            {
                num_files_folders = 0;
                ReadDirectory(&num_files_folders, dir_handle, dir_entries, 0x40);

                for(int j = 0; j < num_files_folders; j++)
                {
                    DirectoryEntry *dir_entry = dir_entries + j*sizeof(DirectoryEntry);
                    u16 entry_path[SALTYSD_MAX_PATH];
                    copy_entry_path(entry_path, dir_entry);

                    if(dir_entry->is_directory)
                    {
                        //Checked before anything is allocated: the queue array
                        //and the path buffer are both fixed, so a subtree that
                        //will not fit is left unscanned and counted.
                        u32 dir_len = dumb_wcslen(dirs[i]) + 1 + dumb_wcslen(entry_path);
                        if(num_directories >= SALTYSD_MAX_DIRS || dir_len >= SALTYSD_MAX_PATH)
                        {
                            dirs_skipped++;
                            continue;
                        }

                        u16 *new_dir = malloc(SALTYSD_MAX_PATH*sizeof(u16));
                        new_dir[0] = 0;

                        dumb_wcscat(new_dir, dirs[i]);
                        dumb_wcscat(new_dir, (u16*)L"/");
                        dumb_wcscat(new_dir, entry_path);
                        dirs[num_directories] = new_dir;
                        dir_roots[num_directories] = dir_roots[i];
                        num_directories++;
                    }
                    else
                    {
                        u32 root_len = strlen(roots[dir_roots[i]].path) + 1;
                        u32 rel_len = dumb_wcslen(entry_path);
                        if(i >= num_roots)
                            rel_len += dumb_wcslen(dirs[i]) - root_len + 1;

                        if(rel_len >= SALTYSD_MAX_PATH)
                        {
                            files_skipped++;
                            continue;
                        }

                        char *file = malloc(SALTYSD_MAX_PATH);
                        file[0] = 0;

                        if(i >= num_roots)
                        {
                            dumb_wcstombs(file, dirs[i]+root_len);
                            dumb_strcat(file, "/");
                        }
                        dumb_wcstombs(file+strlen(file), entry_path);
                        //printf("List: %s", file);

                        if(i == 0)
                        {
                            //A revoke name is at least "revoke.txt". Check
                            //that length before forming the suffix pointer.
                            u32 file_len = strlen(file);
                            if(file_len >= strlen("revoke.txt") &&
                               starts_with(file, "revoke") &&
                               !strcmp(file + file_len - strlen(".txt"), ".txt"))
                            {
                                char *temp_real_path = malloc(0x80 + 1 + SALTYSD_MAX_PATH);
                                dumb_strcpy(temp_real_path, roots[dir_roots[i]].path);
                                dumb_strcat(temp_real_path, "/");
                                dumb_strcat(temp_real_path, file);
                                IFile_Init(ifile_handle);
                                if(IFile_Open(ifile_handle, temp_real_path, 1))
                                {
                                    u32 revoke_size = IFile_GetSize(ifile_handle);
                                    u32 revoke_read = 0;
                                    char *revoke_temp_buf = malloc(revoke_size+1);
                                    IFile_Read(ifile_handle, revoke_temp_buf, revoke_size, &revoke_read);
                                    IFile_Close(ifile_handle);

                                    if(revoke_read > revoke_size)
                                        revoke_read = revoke_size;
                                    revoke_temp_buf[revoke_read] = 0;

                                    u32 revoke_held = revoke_buf ? strlen(revoke_buf) : 0;
                                    char *new_alloc = malloc(revoke_held+1+revoke_read+1);
                                    new_alloc[0] = 0;
                                    if(revoke_buf)
                                    {
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
                        if(num_files >= SALTYSD_MAX_FILES)
                        {
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
            } while(num_files_folders == 0x40);

            CloseDirectory(dir_handle);
        }

        free(dirs[i]);
    }

    free(dirs);
    free(dir_entries);

    if(dirs_skipped)
        printf("SaltySD %x folders left unscanned: no room in the scan", dirs_skipped);
    if(files_skipped)
        printf("SaltySD %x files left out of the scan", files_skipped);

    //Parse revoked files
    if(revoke_buf)
    {
        revoke_count = 1;
        int revoke_active_count = 0;
        for(int i = 0; i < revoke_total_size; i++)
        {
            if(revoke_buf[i] == '\n')
                revoke_count++;
        }
        revoked_files = malloc(revoke_count*sizeof(char*));

        char *last_file = revoke_buf;
        for(int i = 0; i < revoke_total_size; i++)
        {
            if(revoke_buf[i] == '\n')
            {
                revoke_buf[i] = 0;

                //I don't know how these Windows newlines work, but they're annoying
                //and I hate them.
                //The buffer opens with the separator, so index 0 is a newline on
                //every parse and i-1 is off the front of the allocation.
                if(i > 0 && revoke_buf[i-1] == '\r')
                {
                    revoke_buf[i-1] = 0;
                }
                if(revoke_buf[i+1] == '\r')
                {
                    revoke_buf[++i] = 0;
                }

                if(strlen(last_file) > 0)
                    revoked_files[revoke_active_count++] = last_file;
                last_file = &revoke_buf[i+1];
            }
        }
        revoke_count = revoke_active_count;
    }

    //Hold back the two channels that live outside the resource tree, before the
    //fixup pass can match them or the insertion pass register them. cro/ and
    //sound/bgm/ have no chunk to point at, so such an entry can never be served.
    u32 num_named = 0;
    for(int i = 0; i < num_files; i++)
    {
        if(files[i] && (starts_with(files[i], SALTYSD_CRO_DIR) ||
                        starts_with(files[i], SALTYSD_BGM_DIR)))
            num_named++;
    }

    saltysd_named *named = num_named ? malloc(num_named*sizeof(saltysd_named)) : NULL;
    u32 named_count = 0;
    for(int i = 0; i < num_files; i++)
    {
        if(files[i] == NULL)
            continue;

        if(!starts_with(files[i], SALTYSD_CRO_DIR) &&
           !starts_with(files[i], SALTYSD_BGM_DIR))
            continue;

        int at = -1;
        for(u32 j = 0; j < named_count; j++)
        {
            if(!strcmp(named[j].path, files[i]))
            {
                at = j;
                break;
            }
        }

        if(at < 0)
        {
            printf("SaltySD named %x %s", file_roots[i], files[i]);
            named[named_count].path = files[i];
            named[named_count].root = file_roots[i] + 1;
            named_count++;
        }
        else
        {
            report_conflict(roots, named[at].root - 1, file_roots[i], files[i]);

            if(file_roots[i] + 1 < named[at].root)
            {
                free(named[at].path);
                named[at].path = files[i];
                named[at].root = file_roots[i] + 1;
            }
            else
                free(files[i]);
        }

        files[i] = NULL;
    }
    num_named = named_count;

    //One byte of root per resource id. Indexed by id rather than ordinal, so it
    //has to be shifted alongside the entries whenever a new one is inserted.
    u8 *root_table = malloc(SALTYSD_ID_SPACE);
    memclr(root_table, SALTYSD_ID_SPACE);

    char *full_name = malloc(0x400);
    memclr(full_name, 0x400);
    u32 last_str_addr = 0;
    for(int i = 0; i < header->resourceentry_amt; i++)
    {
        u32 string_offset_all = (*entries)[i].string_offs;
        u32 string_offset = string_offset_all & 0x000FFFFF;
        u8 extension = (string_offset_all >> 24);

        if(string_offset > last_str_addr)
            last_str_addr = string_offset;

        u8 nesting_level = (*entries)[i].flags & 0xFF;
        if(nesting_level <= 1)
            full_name[0] = 0;

        u8 levels = 1;
        for(int i = 0; i < 0x101; i++)
        {
            if(full_name[i] == 0x0) break;

            if(full_name[i] == '/')
                levels++;

            if(levels >= nesting_level)
            {
                full_name[i+1] = 0x0;
                break;
            }
        }

        char *string = blocks[string_offset / 0x2000] + (string_offset & 0x1FFF);

        if(string_offset_all & 0x00800000)
        {
            u16 reference = *(u16*)string;
            u32 ref_len = (reference & 0x1f) + 4;
            u32 ref_reloff = (reference & 0xe0) >> 6 << 8 | (reference >> 8);
            u32 final_offset = string_offset - ref_reloff;
            char *ref_string = blocks[final_offset / 0x2000] + (final_offset & 0x1FFF);

            dumb_strncat(full_name, ref_string, ref_len);
            dumb_strcat(full_name, string+sizeof(u16));
        }
        else
            dumb_strcat(full_name, string);

        dumb_strcat(full_name, extensions[extension]);

        bool existing_revoked = false;

        //Check the file against revoked list
        if(revoked_files)
        {
            for(int j = 0; j < revoke_count; j++)
            {
                if(!strcmp(full_name, revoked_files[j]))
                {
                    existing_revoked = true;
                    break;
                }
            }

            if(existing_revoked)
            {
                (*entries)[i].string_offs &= 0xFFF00000; //If it's revoked, revoke its path name
            }
        }

        //If we have a file, adjust file sizes
        if(full_name[strlen(full_name)-1] != '/')
        {
            int winner = -1;

            for(int j = 0; j < num_files; j++)
            {
                if(files[j] == NULL)
                    continue;

                if(strcmp(full_name, files[j]))
                    continue;

                if(winner < 0)
                {
                    winner = j;
                    continue;
                }

                //More than one root supplying the same file. The loser is
                //dropped here so it cannot come back as a duplicate entry.
                report_conflict(roots, file_roots[winner], file_roots[j], full_name);

                if(file_roots[j] < file_roots[winner])
                {
                    free(files[winner]);
                    files[winner] = NULL;
                    winner = j;
                }
                else
                {
                    free(files[j]);
                    files[j] = NULL;
                }
            }

            if(winner >= 0)
            {
                if(!existing_revoked)
                {
                    //By overriding the compressed size, our files are forced into only one hook
                    (*entries)[i].comp_size = file_sizes[winner];
                    (*entries)[i].decomp_size = file_sizes[winner];
                    (*entries)[i].flags |= 0x8000;
                    root_table[i + SALTYSD_ID_BIAS] = file_roots[winner] + 1;
                }

                free(files[winner]);
                files[winner] = NULL;
            }
        }
    }

    //Whatever is left is a file the archives do not have at all. Two roots can
    //still offer the same new file, so collapse those before any become entries.
    for(int i = 0; i < num_files; i++)
    {
        if(files[i] == NULL)
            continue;

        for(int j = i+1; j < num_files; j++)
        {
            if(files[j] == NULL)
                continue;

            if(strcmp(files[i], files[j]))
                continue;

            report_conflict(roots, file_roots[i], file_roots[j], files[i]);

            if(file_roots[j] < file_roots[i])
            {
                free(files[i]);
                files[i] = files[j];
                file_sizes[i] = file_sizes[j];
                file_roots[i] = file_roots[j];
            }
            else
                free(files[j]);

            files[j] = NULL;
        }
    }

    //Add new files to RF
    u32 string_limit = block_size * 0x2000;
    u32 entries_added = 0;
    u32 entries_skipped = 0;
    last_str_addr = ((*entries)[header->resourceentry_amt-1].string_offs & 0xFFFFF) + 0x80;
    for(int i = 0; i < num_files; i++)
    {
        if(files[i] == NULL)
            continue;

        //Check the file against our revoked list
        if(revoked_files)
        {
            bool new_revoked = false;
            for(int j = 0; j < revoke_count; j++)
            {
                if(!strcmp(files[i], revoked_files[j]))
                {
                    new_revoked = true;
                    break;
                }
            }

            if(new_revoked)
                continue;
        }

        printf("Adding file %s", files[i]);

        u32 entry_to_shift = 1;
#if SALTYSD_DEBUG
        u8 entered_packed = 0;
#endif
        u8 level_target = 1;
        char *substr = malloc(0x101);

        u32 seed_len = len_to(files[i], '/');
        if(seed_len == -1)
        {
            entry_to_shift = header->resourceentry_amt;

            dumb_strcpy(substr, files[i]);
            seed_len = strlen(substr);
        }
        else
            dumb_strncpy(substr, files[i], seed_len);

        for(; entry_to_shift < header->resourceentry_amt; entry_to_shift++)
        {
            u8 nesting_level = (*entries)[entry_to_shift].flags & 0xFF;

            //We're a file but the next object at the same level is a folder, break here
            if(level_target == nesting_level && len_to(substr, '/') == -1 && (*entries)[entry_to_shift].flags & 0x200)
                break;

            //We're a file and the next entry isn't even at the same nesting level, break
            if(level_target != nesting_level && len_to(substr, '/') == -1)
                break;

            //We're a higher folder and we descended a folder, obviously this folder is new
            if(level_target > nesting_level && len_to(substr, '/') != -1)
                break;

            //Don't look at deeper folders while trying to find our current level
            if(level_target < nesting_level && len_to(substr, '/') != -1)
                continue;

            //We're a folder, pay no mind to the file order since files come before folders
            if(len_to(substr, '/') != -1 && !((*entries)[entry_to_shift].flags & 0x200))
                continue;

            u32 string_offset_all = (*entries)[entry_to_shift].string_offs;
            u32 string_offset = string_offset_all & 0x000FFFFF;

            char *string = blocks[string_offset / 0x2000] + (string_offset & 0x1FFF);

            if(string_offset_all & 0x00800000)
            {
                u16 reference = *(u16*)string;
                u32 ref_len = (reference & 0x1f) + 4;
                u32 ref_reloff = (reference & 0xe0) >> 6 << 8 | (reference >> 8);
                u32 final_offset = string_offset - ref_reloff;
                char *ref_string = blocks[final_offset / 0x2000] + (final_offset & 0x1FFF);

                dumb_strncpy(full_name, ref_string, ref_len);
                dumb_strcat(full_name, string+sizeof(u16));
            }
            else
                dumb_strcpy(full_name, string);

            //Folder is part of our path, advance level target and look for next folder
            //or the spot to place our file
            if(!strcmp(full_name, substr) && len_to(substr, '/') != -1)
            {
                printf("%x %x %s", entry_to_shift, level_target, full_name);
                u32 len = len_to(files[i]+seed_len, '/');
                if(len != -1)
                {
                    dumb_strncpy(substr, files[i]+seed_len, len);
                    seed_len += len;
                }
                else
                {
                    dumb_strcpy(substr, files[i]+seed_len);
                    seed_len += strlen(substr);
                }

                if((*entries)[entry_to_shift].flags & 0x1000)
                {
#if SALTYSD_DEBUG
                    entered_packed = 1;
#endif
                    printf("entered packed");
                }
                level_target++;
            }
            else if(strcmp(full_name, substr) > 0 && level_target != 1)
            {
                //Greater alphabetically, break here
                printf("larger %x %x %s", entry_to_shift, level_target, full_name);
                break;
            }
        }

        printf("entry comp %x %x %x", &(*entries)[entry_to_shift], &(*entries)[header->resourceentry_amt-1], (u32)&(*entries)[header->resourceentry_amt-1] - (u32)&(*entries)[entry_to_shift]);
        u32 entries_to_make = count_chars(files[i]+seed_len-strlen(substr), '/')+1;

        printf("adding %x entries after %x (%s)", entries_to_make, entry_to_shift, files[i]+seed_len-strlen(substr));

        char *tail = files[i]+seed_len-strlen(substr);
        u32 probe = last_str_addr;
        bool fits = entries_added + entries_to_make <= ENTRY_RESERVE
                 && header->resourceentry_amt + entries_to_make + SALTYSD_ID_BIAS <= SALTYSD_ID_SPACE;

        for(u32 j = 0; fits && j < entries_to_make; j++)
        {
            u32 seg = len_to(tail, '/');
            //The last component loses its extension before it is written, so
            //its whole length is an overestimate.
            u32 seg_len = (seg == -1) ? strlen(tail) : seg-1;

            if(string_alloc(&probe, string_limit, seg_len) == SALTYSD_NO_STRING)
                fits = false;

            if(seg == -1)
                break;
            tail += seg;
        }

        //And one more string, in case the extension is one the table has not
        //seen yet, sized by the buffer it is read into.
        if(fits && string_alloc(&probe, string_limit, 0x10-1) == SALTYSD_NO_STRING)
            fits = false;

        if(!fits)
        {
            printf("SaltySD no room left, dropping %s", files[i]);
            entries_skipped++;
            free(substr);
            free(files[i]);
            files[i] = NULL;
            continue;
        }

        entries_added += entries_to_make;

        if(entry_to_shift != header->resourceentry_amt)
        {
            memmove(&(*entries)[entry_to_shift+entries_to_make], &(*entries)[entry_to_shift], (u32)&(*entries)[header->resourceentry_amt] - (u32)&(*entries)[entry_to_shift]);
            memmove(root_table + entry_to_shift + entries_to_make + SALTYSD_ID_BIAS, root_table + entry_to_shift + SALTYSD_ID_BIAS, header->resourceentry_amt - entry_to_shift);
        }

        memclr(&(*entries)[entry_to_shift], 0x18*entries_to_make);
        memclr(root_table + entry_to_shift + SALTYSD_ID_BIAS, entries_to_make);

        //Create all our new folders
        for(int j = 0; j < entries_to_make-1; j++)
        {
            u32 folder_str = string_alloc(&last_str_addr, string_limit, strlen(substr));
            char *new_str = blocks[folder_str / 0x2000] + (folder_str & 0x1FFF);
            dumb_strcpy(new_str, substr);
            printf("folder: %s", new_str);

            (*entries)[entry_to_shift].chunk_offs = (*entries)[entry_to_shift-1].chunk_offs;
            (*entries)[entry_to_shift].string_offs = folder_str;
            (*entries)[entry_to_shift].comp_size = 0x80;
            (*entries)[entry_to_shift].decomp_size = 0x80;
            (*entries)[entry_to_shift].timestamp = 0;
            (*entries)[entry_to_shift].flags = 0x8000 | 0xA00 | level_target;
            header->resourceentry_amt++;
            header->entrysection_size += 0x18;
            entry_to_shift++;

            printf("%x %x %s", entry_to_shift-1, level_target, substr);

            u32 len = len_to(files[i]+seed_len, '/');
            if(len != -1)
            {
                dumb_strncpy(substr, files[i]+seed_len, len);
                seed_len += len;
            }
            else
            {
                dumb_strcpy(substr, files[i]+seed_len);
            }
            level_target++;
        }


        u32 dot = last_index_of(substr, '.');
        char *file_ext = malloc(SALTYSD_MAX_EXT);
        //Read whether or not there was a dot; an empty extension is what an
        //extensionless file matches.
        file_ext[0] = 0;
        if(dot != -1 && dot != 0)
        {
            //The name is up to SALTYSD_MAX_PATH and this buffer is sixteen
            //bytes, so a longer suffix is truncated for extension lookup.
            u32 ext_len = strlen(&substr[dot]);
            if(ext_len > SALTYSD_MAX_EXT-1)
                ext_len = SALTYSD_MAX_EXT-1;

            dumb_strncpy(file_ext, &substr[dot], ext_len);
            substr[dot] = 0;
            seed_len += dot;
        }

        //Find our extension ID
        printf("%x %x %s", entry_to_shift, level_target, substr);
        u8 ext_num = 0;

        for(int j = 0; j < *(u32*)extensions_block; j++)
        {
            if(!strcmp(extensions[j], file_ext))
            {
                printf("extnum %x %s", j, extensions[j]);
                ext_num = j;
                break;
            }
        }

        //New extension...
        if(ext_num == 0 && file_ext[0] && *(u32*)extensions_block < SALTYSD_MAX_EXTENSIONS)
        {
            u32 ext_str = string_alloc(&last_str_addr, string_limit, strlen(file_ext));
            char *new_str = blocks[ext_str / 0x2000] + (ext_str & 0x1FFF);
            dumb_strcpy(new_str, file_ext);
            printf("adding ext %s", new_str);

            *(u32*)(extensions_block + sizeof(u32) + *(u32*)extensions_block*sizeof(u32)) = ext_str;
            ext_num = *(u32*)extensions_block;
            extensions[*(u32*)extensions_block] = new_str;
            *(u32*)extensions_block += 1;
        }

        free(file_ext);

        u32 file_str = string_alloc(&last_str_addr, string_limit, strlen(substr));
        char *new_str = blocks[file_str / 0x2000] + (file_str & 0x1FFF);
        dumb_strcpy(new_str, substr);

        //Add new file entry
        (*entries)[entry_to_shift].chunk_offs = (*entries)[entry_to_shift-1].chunk_offs;
        (*entries)[entry_to_shift].string_offs = file_str | ext_num << 24;
        (*entries)[entry_to_shift].comp_size = file_sizes[i];
        (*entries)[entry_to_shift].decomp_size = file_sizes[i];
        (*entries)[entry_to_shift].timestamp = 0;
        (*entries)[entry_to_shift].flags = 0x8000 | 0xC00 | level_target;
        root_table[entry_to_shift + SALTYSD_ID_BIAS] = file_roots[i] + 1;
        header->resourceentry_amt++;
        header->entrysection_size += 0x18;

        printf("flags: %x, %s", (*entries)[entry_to_shift].flags, entered_packed ? "entered packed" : "didn't enter packed");

        free(substr);
        free(files[i]);
        files[i] = NULL;
    }

    if(entries_skipped)
        printf("SaltySD %x new files dropped: the insertion reserve is full", entries_skipped);

    free(full_name);
    free(files);
    free(extensions);
    free(blocks);
    free(ifile_handle);
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
