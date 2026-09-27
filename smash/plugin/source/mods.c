#include "mods.h"
#include "fs.h"

#include "types.h"

#define READ_BATCH 16
#define MOD_PATH_CHARS 0x101

static toggle_entry entries[MODS_MAX];
toggle_list mods = { "/saltysd/smash", entries, MODS_MAX, 0, 0 };

static fs_entry batch[READ_BATCH];
static u16 path[MOD_PATH_CHARS];

static u32 append(u32 at, const u16 *s)
{
    while (*s && at < sizeof(path) / sizeof(path[0]) - 1)
        path[at++] = *s++;
    path[at] = 0;
    return at;
}

static u32 append_ascii(u32 at, const char *s)
{
    while (*s && at < sizeof(path) / sizeof(path[0]) - 1)
        path[at++] = (u8)*s++;
    path[at] = 0;
    return at;
}

int mods_drop_index(void)
{
    u32 dir;
    u16 root[MOD_PATH_CHARS];
    u32 at = 0;
    const char *s = MODS_INDEX_DIR;
    while (*s)
        root[at++] = (u8)*s++;
    root[at] = 0;

    int res = fs_dir_open(&dir, root);
    if (res < 0)
        return res;

    int first_error = 0;
    int removed = 0;
    for (;;) {
        u32 read = 0;
        res = fs_dir_read(dir, batch, READ_BATCH, &read);
        if (res < 0 || !read)
            break;

        for (u32 i = 0; i < read; i++) {
            if (batch[i].attributes & FS_ATTR_DIR)
                continue;

            const char *want = MODS_INDEX_STEM;
            u32 j = 0;
            while (want[j] && batch[i].name[j] == (u8)want[j])
                j++;
            if (want[j])
                continue;

            at = append_ascii(0, MODS_INDEX_DIR "/");
            append(at, batch[i].name);
            int one = fs_file_delete(path);
            if (one < 0) {
                if (!first_error)
                    first_error = one;
            } else {
                removed++;
            }
        }

        if (read < READ_BATCH)
            break;
    }

    fs_dir_close(dir);
    return first_error ? first_error : removed;
}
