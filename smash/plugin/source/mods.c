#include "mods.h"
#include "fs.h"
#include "status.h"

#include "types.h"

#define READ_BATCH 16
#define MOD_PATH_CHARS 0x101

mod_entry mods[MODS_MAX];
u32 mods_count;
u32 mods_skipped;

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

static void marker_path(const mod_entry *m)
{
    u32 at = append_ascii(0, "/saltysd/smash/");
    at = append(at, m->name);
    append_ascii(at, "/is.disabled");
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

static int name_less(const u16 *a, const u16 *b)
{
    for (;; a++, b++) {
        u8 ca = (u8)*a, cb = (u8)*b;
        if (ca != cb)
            return ca < cb;
        if (!ca)
            return 0;
    }
}

static void insert(const u16 *name)
{
    u32 at = 0;
    while (at < mods_count && !name_less(name, mods[at].name))
        at++;

    for (u32 k = mods_count; k > at; k--) {
        for (u32 c = 0; c < MOD_NAME_CHARS; c++)
            mods[k].name[c] = mods[k - 1].name[c];
        mods[k].enabled = mods[k - 1].enabled;
    }

    u32 c = 0;
    for (; name[c]; c++)
        mods[at].name[c] = name[c];
    mods[at].name[c] = 0;
    mods_count++;
}

int mods_load(void)
{
    mods_count = mods_skipped = 0;

    append_ascii(0, "/saltysd/smash");
    u32 dir;
    int res = fs_dir_open(&dir, path);
    if (res < 0)
        return res;

    u32 read;
    do {
        res = fs_dir_read(dir, batch, READ_BATCH, &read);
        for (u32 i = 0; res >= 0 && i < read; i++) {
            if (!(batch[i].attributes & FS_ATTR_DIR))
                continue;

            u32 len = 0;
            while (batch[i].name[len])
                len++;
            if (len >= MOD_NAME_CHARS || mods_count == MODS_MAX) {
                mods_skipped++;
                continue;
            }
            insert(batch[i].name);
        }
    } while (res >= 0 && read == READ_BATCH);
    fs_dir_close(dir);

    for (u32 i = 0; i < mods_count; i++) {
        marker_path(&mods[i]);
        mods[i].enabled = fs_file_exists(path) < 0;
        mods[i].wanted = mods[i].enabled;
    }

    saltysd_status.mods_listed = mods_count;
    return res;
}

u32 mods_changes(void)
{
    u32 n = 0;
    for (u32 i = 0; i < mods_count; i++)
        n += mods[i].wanted != mods[i].enabled;
    return n;
}

void mods_apply(mods_apply_result *out)
{
    out->applied = out->failed = 0;
    out->first_error = 0;
    out->first_failed = 0;

    for (u32 i = 0; i < mods_count; i++) {
        mod_entry *m = &mods[i];
        if (m->wanted == m->enabled)
            continue;

        marker_path(m);
        int res = m->wanted ? fs_file_delete(path) : fs_file_create_empty(path);
        saltysd_status.last_fs_result = res;
        if (res < 0) {
            if (!out->failed++) {
                out->first_error = res;
                out->first_failed = m;
            }
            continue;
        }
        m->enabled = m->wanted;
        out->applied++;
    }
    saltysd_status.mods_changed += out->applied;
}
