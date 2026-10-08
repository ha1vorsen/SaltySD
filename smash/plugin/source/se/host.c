#include "host_internal.h"

#include "common.h"
#include "content.h"
#include "fs.h"
#include "toggles.h"
#include "se/diagnostics.h"
#include "se/hooks_internal.h"
#include "se/targets_internal.h"

#define HOST_ALLOCATIONS_MAX 64
#define HOST_FILES_MAX 16
#define HOST_HOOK_DECLARATIONS_MAX 64
#define HOST_PATH_CHARS 0x101

typedef struct {
    void *base;
    void *aligned;
    se_package_handle owner;
    u32 size;
} host_allocation;

typedef struct {
    u32 game_handle;
    se_package_handle owner;
    u8 used;
} host_file;

typedef struct {
    se_package_handle owner;
    se_u32 target_id;
    se_u32 handler;
    se_u32 flags;
} host_hook_declaration;

static host_allocation allocations[HOST_ALLOCATIONS_MAX];
static host_file files[HOST_FILES_MAX];
static host_hook_declaration hook_declarations[HOST_HOOK_DECLARATIONS_MAX];
static u32 hook_declaration_count;
static u16 folders[SE_PLAN_PACKAGES_MAX][TOGGLE_NAME_CHARS];
static u8 package_known[SE_PLAN_PACKAGES_MAX];
static u16 file_path[HOST_PATH_CHARS];
static u32 open_files;

static void *(*game_alloc)(u32 size) = (void *)liballoc_ADDR;
static void (*game_free)(void *memory) = (void *)libdealloc_ADDR;

static int owner_index(se_package_handle owner)
{
    if (!owner || owner > SE_PLAN_PACKAGES_MAX || !package_known[owner - 1])
        return -1;
    return (int)(owner - 1);
}

static void host_log(se_package_handle owner, se_u32 level,
                     const char *bytes, se_u32 length)
{
    (void)bytes;
    se_diagnostics_emit(owner ? (u16)(owner - 1) : SE_PLAN_NONE,
                        SE_PHASE_RUNTIME, PLUGIN_OK, SE_PLAN_NONE, level, length);
}

static int host_get_build_identity(se_build_identity *out, se_u32 out_size)
{
    if (!out || out_size < sizeof(*out))
        return -1;
    se_targets_build_identity(out);
    return 0;
}

static int host_resolve_target(se_u32 target_id, se_target *out, se_u32 out_size)
{
    if (!out || out_size < sizeof(*out))
        return -1;
    return se_targets_resolve(target_id, out);
}

static void *host_alloc(se_package_handle owner, se_u32 size,
                        se_u32 alignment, se_u32 arena)
{
    if (owner_index(owner) < 0 || !size || arena || !alignment ||
        (alignment & (alignment - 1)) || alignment > 0x1000 ||
        size > 0xFFFFFFFFu - alignment)
        return 0;
    u32 slot = HOST_ALLOCATIONS_MAX;
    for (u32 i = 0; i < HOST_ALLOCATIONS_MAX; i++)
        if (!allocations[i].base) {
            slot = i;
            break;
        }
    if (slot == HOST_ALLOCATIONS_MAX)
        return 0;
    void *base = game_alloc(size + alignment - 1);
    if (!base)
        return 0;
    u32 address = ((u32)base + alignment - 1) & ~(alignment - 1);
    allocations[slot].base = base;
    allocations[slot].aligned = (void *)address;
    allocations[slot].owner = owner;
    allocations[slot].size = size;
    return (void *)address;
}

static int host_free(se_package_handle owner, void *allocation)
{
    for (u32 i = 0; i < HOST_ALLOCATIONS_MAX; i++) {
        if (allocations[i].aligned != allocation || allocations[i].owner != owner)
            continue;
        game_free(allocations[i].base);
        allocations[i].base = 0;
        allocations[i].aligned = 0;
        allocations[i].owner = 0;
        allocations[i].size = 0;
        return 0;
    }
    return -1;
}

static int safe_relative_path(const char *path)
{
    if (!path || !*path || *path == '/' || *path == '\\')
        return 0;
    int component_start = 1;
    u32 dots = 0;
    for (u32 i = 0; path[i]; i++) {
        u8 c = (u8)path[i];
        if (c < 0x20 || c >= 0x7F || c == ':' || c == '\\')
            return 0;
        if (c == '/') {
            if (component_start || dots == 1 || dots == 2)
                return 0;
            component_start = 1;
            dots = 0;
        } else {
            if (component_start && c == '.')
                dots++;
            else
                dots = 0;
            component_start = 0;
        }
    }
    return !component_start && dots != 1 && dots != 2;
}

static u32 append_ascii(u16 *out, u32 capacity, u32 at, const char *text)
{
    while (*text && at + 1 < capacity)
        out[at++] = (u8)*text++;
    out[at] = 0;
    return at;
}

static u32 append_utf16(u16 *out, u32 capacity, u32 at, const u16 *text)
{
    while (*text && at + 1 < capacity)
        out[at++] = *text++;
    out[at] = 0;
    return at;
}

static int host_open_read(se_package_handle owner, const char *relative_path,
                          se_file_handle *out)
{
    int package = owner_index(owner);
    if (package < 0 || !out || !safe_relative_path(relative_path))
        return -1;
    u32 slot = HOST_FILES_MAX;
    for (u32 i = 0; i < HOST_FILES_MAX; i++)
        if (!files[i].used) {
            slot = i;
            break;
        }
    if (slot == HOST_FILES_MAX)
        return -1;
    if (!open_files && fs_open() < 0)
        return -1;
    u32 at = append_ascii(file_path, HOST_PATH_CHARS, 0, SALTYSD_ENGINE_ROOT "/");
    at = append_utf16(file_path, HOST_PATH_CHARS, at, folders[package]);
    at = append_ascii(file_path, HOST_PATH_CHARS, at, "/");
    append_ascii(file_path, HOST_PATH_CHARS, at, relative_path);
    u32 game_handle;
    int result = fs_file_open_read(&game_handle, file_path);
    if (result < 0) {
        if (!open_files)
            fs_close();
        return result;
    }
    files[slot].game_handle = game_handle;
    files[slot].owner = owner;
    files[slot].used = 1;
    open_files++;
    *out = slot + 1;
    return 0;
}

static int host_read(se_file_handle file, se_u32 offset, void *buffer,
                     se_u32 size, se_u32 *read_size)
{
    if (!file || file > HOST_FILES_MAX || !files[file - 1].used || !buffer || !read_size)
        return -1;
    u32 actual = 0;
    int result = fs_file_read(files[file - 1].game_handle, offset, buffer, size, &actual);
    *read_size = actual;
    return result;
}

static void host_close(se_file_handle file)
{
    if (!file || file > HOST_FILES_MAX || !files[file - 1].used)
        return;
    fs_file_close(files[file - 1].game_handle);
    files[file - 1].used = 0;
    if (open_files)
        open_files--;
    if (!open_files)
        fs_close();
}

static void close_file_slot(u32 slot)
{
    fs_file_close(files[slot].game_handle);
    files[slot].used = 0;
    files[slot].owner = 0;
    if (open_files)
        open_files--;
}

static int host_emit_diagnostic(se_package_handle owner, se_u32 code,
                                se_u32 detail_a, se_u32 detail_b)
{
    int package = owner_index(owner);
    if (package < 0)
        return -1;
    se_diagnostics_emit((u16)package, SE_PHASE_RUNTIME, (se_error)code,
                        SE_PLAN_NONE, detail_a, detail_b);
    return 0;
}

static int host_install_hook(se_package_handle owner,
                             const se_hook_request *request, se_hook_result *result)
{
    if (!request)
        return SE_ERROR_HOOK_UNSUPPORTED;
    se_u32 handler = (se_u32)(uintptr_t)request->handler;
    for (u32 i = 0; i < hook_declaration_count; i++) {
        const host_hook_declaration *declared = &hook_declarations[i];
        if (declared->owner == owner && declared->target_id == request->target_id &&
            declared->handler == handler && declared->flags == request->flags)
            return se_hooks_install(owner, request, result);
    }
    return SE_ERROR_HOOK_UNSUPPORTED;
}

static int host_remove_hook(se_package_handle owner, se_hook_handle hook)
{
    return se_hooks_remove(owner, hook);
}

static const se_host_v1 host = {
    { SE_HOST_MAGIC, SE_HOST_ABI_MAJOR, SE_HOST_ABI_MINOR, sizeof(se_host_v1),
      SE_CAP_LOG | SE_CAP_BUILD_IDENTITY | SE_CAP_TARGETS | SE_CAP_ALLOC |
      SE_CAP_FILES | SE_CAP_DIAGNOSTICS | SE_CAP_HOOKS,
      0 },
    host_log,
    host_get_build_identity,
    host_resolve_target,
    host_alloc,
    host_free,
    host_open_read,
    host_read,
    host_close,
    host_emit_diagnostic,
    host_install_hook,
    host_remove_hook,
};

const se_host_v1 *se_get_host(se_u32 requested_major)
{
    return requested_major == SE_HOST_ABI_MAJOR ? &host : 0;
}

void se_host_reset(void)
{
    for (u32 i = 0; i < SE_PLAN_PACKAGES_MAX; i++)
        if (package_known[i])
            se_hooks_remove_owner(i + 1);
    u32 closed = 0;
    for (u32 i = 0; i < HOST_FILES_MAX; i++)
        if (files[i].used) {
            close_file_slot(i);
            closed = 1;
        }
    if (closed && !open_files)
        fs_close();
    for (u32 i = 0; i < HOST_ALLOCATIONS_MAX; i++) {
        if (allocations[i].base)
            game_free(allocations[i].base);
        allocations[i].base = allocations[i].aligned = 0;
        allocations[i].owner = 0;
        allocations[i].size = 0;
    }
    for (u32 i = 0; i < SE_PLAN_PACKAGES_MAX; i++)
        package_known[i] = 0;
    open_files = 0;
    hook_declaration_count = 0;
    se_hooks_reset();
}

se_error se_host_quiesce_owner(se_package_handle owner)
{
    u32 closed = 0;
    for (u32 i = 0; i < HOST_FILES_MAX; i++)
        if (files[i].used && files[i].owner == owner) {
            close_file_slot(i);
            closed = 1;
        }
    if (closed && !open_files)
        fs_close();

    u32 out = 0;
    for (u32 i = 0; i < hook_declaration_count; i++)
        if (hook_declarations[i].owner != owner)
            hook_declarations[out++] = hook_declarations[i];
    hook_declaration_count = out;
    return se_hooks_remove_owner(owner);
}

void se_host_release_owner(se_package_handle owner)
{
    for (u32 i = 0; i < HOST_ALLOCATIONS_MAX; i++) {
        if (!allocations[i].base || allocations[i].owner != owner)
            continue;
        game_free(allocations[i].base);
        allocations[i].base = allocations[i].aligned = 0;
        allocations[i].owner = 0;
        allocations[i].size = 0;
    }
}

se_u32 se_host_hook_mark(void)
{
    return hook_declaration_count;
}

void se_host_hook_restore(se_u32 mark)
{
    if (mark <= hook_declaration_count)
        hook_declaration_count = mark;
}

int se_host_allow_hook(se_package_handle owner, se_u32 target_id,
                       se_u32 handler, se_u32 flags)
{
    if (owner_index(owner) < 0 || !target_id || !handler ||
        flags != SE_HOOK_EXCLUSIVE ||
        hook_declaration_count == HOST_HOOK_DECLARATIONS_MAX)
        return -1;
    host_hook_declaration *declaration =
        &hook_declarations[hook_declaration_count++];
    declaration->owner = owner;
    declaration->target_id = target_id;
    declaration->handler = handler;
    declaration->flags = flags;
    return 0;
}

void se_host_register_package(se_package_handle handle, const u16 *folder_name)
{
    int package = (int)handle - 1;
    if (package < 0 || package >= SE_PLAN_PACKAGES_MAX)
        return;
    u32 at = 0;
    while (folder_name[at] && at + 1 < TOGGLE_NAME_CHARS) {
        folders[package][at] = folder_name[at];
        at++;
    }
    folders[package][at] = 0;
    package_known[package] = 1;
}
