#include "toggles.h"
#include "fs.h"

#include "types.h"

#define READ_BATCH 16
#define PATH_CHARS 0x101

static fs_entry batch[READ_BATCH];
static u16 path[PATH_CHARS];

static u32 append(u16 *out, u32 max, u32 at, const u16 *s)
{
    while (*s && at < max - 1)
        out[at++] = *s++;
    out[at] = 0;
    return at;
}

static u32 append_ascii(u16 *out, u32 max, u32 at, const char *s)
{
    while (*s && at < max - 1)
        out[at++] = (u8)*s++;
    out[at] = 0;
    return at;
}

u32 toggles_folder_path(const toggle_list *list, const toggle_entry *entry, u16 *out, u32 max)
{
    u32 at = append_ascii(out, max, 0, list->root);
    at = append_ascii(out, max, at, "/");
    return append(out, max, at, entry->name);
}

static void marker_path(const toggle_list *list, const toggle_entry *entry)
{
    u32 at = toggles_folder_path(list, entry, path, PATH_CHARS);
    append_ascii(path, PATH_CHARS, at, "/" TOGGLE_MARKER);
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

static void insert(toggle_list *list, const u16 *name)
{
    toggle_entry *e = list->entries;
    u32 at = 0;
    while (at < list->count && !name_less(name, e[at].name))
        at++;

    for (u32 k = list->count; k > at; k--) {
        for (u32 c = 0; c < TOGGLE_NAME_CHARS; c++)
            e[k].name[c] = e[k - 1].name[c];
        e[k].enabled = e[k - 1].enabled;
    }

    u32 c = 0;
    for (; name[c]; c++)
        e[at].name[c] = name[c];
    e[at].name[c] = 0;
    list->count++;
}

int toggles_load(toggle_list *list)
{
    list->count = list->skipped = 0;

    append_ascii(path, PATH_CHARS, 0, list->root);
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
            if (len >= TOGGLE_NAME_CHARS || list->count == list->max) {
                list->skipped++;
                continue;
            }
            insert(list, batch[i].name);
        }
    } while (res >= 0 && read == READ_BATCH);
    fs_dir_close(dir);

    for (u32 i = 0; i < list->count; i++) {
        marker_path(list, &list->entries[i]);
        list->entries[i].enabled = fs_file_exists(path) < 0;
        list->entries[i].wanted = list->entries[i].enabled;
    }

    return res;
}

u32 toggles_changes(const toggle_list *list)
{
    u32 n = 0;
    for (u32 i = 0; i < list->count; i++)
        n += list->entries[i].wanted != list->entries[i].enabled;
    return n;
}

void toggles_apply(toggle_list *list, toggles_apply_result *out)
{
    out->applied = out->failed = 0;
    out->first_error = 0;
    out->first_failed = 0;

    for (u32 i = 0; i < list->count; i++) {
        toggle_entry *e = &list->entries[i];
        if (e->wanted == e->enabled)
            continue;

        marker_path(list, e);
        int res;
        if (e->wanted)
            res = fs_file_delete(path);
        else
            res = fs_file_create_empty(path);
        if (res < 0) {
            if (!out->failed++) {
                out->first_error = res;
                out->first_failed = e;
            }
            continue;
        }
        e->enabled = e->wanted;
        out->applied++;
    }
}
