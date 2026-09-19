#include "fs.h"
#include "ipc.h"

#include "types.h"

#define ARCHIVE_SDMC     9
#define PATH_EMPTY       1
#define PATH_UTF16       4
#define OPEN_READ        1
#define OPEN_WRITE       2
#define OPEN_CREATE      4
#define WRITE_FLUSH      1

static u32 fs_handle;
static u64 sdmc;

static u32 path_bytes(const u16 *path)
{
    u32 n = 0;
    while (path[n])
        n++;
    return (n + 1) * 2;
}

int fs_open(void)
{
    int res = ipc_service(&fs_handle, "fs:USER");
    if (res < 0) {
        fs_handle = 0;
        return res;
    }

    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x08010002;
    cmd[1] = IPC_DESC_PID;
    res = ipc_request(fs_handle);

    if (res >= 0) {
        static const char empty[1] = { 0 };
        cmd = ipc_cmdbuf();
        cmd[0] = 0x080C00C2;
        cmd[1] = ARCHIVE_SDMC;
        cmd[2] = PATH_EMPTY;
        cmd[3] = 1;
        cmd[4] = IPC_DESC_STATIC(1, 0);
        cmd[5] = (u32)empty;
        res = ipc_request(fs_handle);
        if (res >= 0)
            sdmc = cmd[2] | (u64)cmd[3] << 32;
    }

    if (res < 0)
        fs_close();
    return res;
}

void fs_close(void)
{
    if (sdmc) {
        u32 *cmd = ipc_cmdbuf();
        cmd[0] = 0x080E0080;
        cmd[1] = (u32)sdmc;
        cmd[2] = (u32)(sdmc >> 32);
        ipc_request(fs_handle);
        sdmc = 0;
    }
    if (fs_handle)
        ipc_close(fs_handle);
    fs_handle = 0;
}

int fs_dir_open(u32 *dir, const u16 *path)
{
    u32 *cmd = ipc_cmdbuf();
    u32 size = path_bytes(path);
    cmd[0] = 0x080B0102;
    cmd[1] = (u32)sdmc;
    cmd[2] = (u32)(sdmc >> 32);
    cmd[3] = PATH_UTF16;
    cmd[4] = size;
    cmd[5] = IPC_DESC_STATIC(size, 0);
    cmd[6] = (u32)path;
    int res = ipc_request(fs_handle);
    if (res >= 0)
        *dir = cmd[3];
    return res;
}

int fs_dir_read(u32 dir, fs_entry *entries, u32 max, u32 *read)
{
    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x08010042;
    cmd[1] = max;
    cmd[2] = IPC_DESC_BUF_W(max * sizeof(fs_entry));
    cmd[3] = (u32)entries;
    int res = ipc_request(dir);
    *read = res >= 0 ? cmd[2] : 0;
    return res;
}

void fs_dir_close(u32 dir)
{
    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x08020000;
    ipc_request(dir);
    ipc_close(dir);
}

int fs_file_create_empty(const u16 *path)
{
    u32 *cmd = ipc_cmdbuf();
    u32 size = path_bytes(path);
    cmd[0] = 0x08080202;
    cmd[1] = 0;
    cmd[2] = (u32)sdmc;
    cmd[3] = (u32)(sdmc >> 32);
    cmd[4] = PATH_UTF16;
    cmd[5] = size;
    cmd[6] = 0;
    cmd[7] = 0;
    cmd[8] = 0;
    cmd[9] = IPC_DESC_STATIC(size, 0);
    cmd[10] = (u32)path;
    return ipc_request(fs_handle);
}

int fs_file_delete(const u16 *path)
{
    u32 *cmd = ipc_cmdbuf();
    u32 size = path_bytes(path);
    cmd[0] = 0x08040142;
    cmd[1] = 0;
    cmd[2] = (u32)sdmc;
    cmd[3] = (u32)(sdmc >> 32);
    cmd[4] = PATH_UTF16;
    cmd[5] = size;
    cmd[6] = IPC_DESC_STATIC(size, 0);
    cmd[7] = (u32)path;
    return ipc_request(fs_handle);
}

u32 fs_path_from_ascii(u16 *out, u32 max, const char *s)
{
    u32 n = 0;
    while (s[n] && n + 1 < max) {
        out[n] = (unsigned char)s[n];
        n++;
    }
    out[n] = 0;
    return s[n] == 0;
}

static int open_file(u32 *file, const u16 *path, u32 flags)
{
    u32 *cmd = ipc_cmdbuf();
    u32 size = path_bytes(path);
    cmd[0] = 0x080201C2;
    cmd[1] = 0;
    cmd[2] = (u32)sdmc;
    cmd[3] = (u32)(sdmc >> 32);
    cmd[4] = PATH_UTF16;
    cmd[5] = size;
    cmd[6] = flags;
    cmd[7] = 0;
    cmd[8] = IPC_DESC_STATIC(size, 0);
    cmd[9] = (u32)path;
    int res = ipc_request(fs_handle);
    if (res >= 0)
        *file = cmd[3];
    return res;
}

int fs_file_open_read(u32 *file, const u16 *path)
{
    return open_file(file, path, OPEN_READ);
}

int fs_file_create_write(u32 *file, const u16 *path)
{
    return open_file(file, path, OPEN_WRITE | OPEN_CREATE);
}

int fs_file_read(u32 file, u32 offset, void *buf, u32 size, u32 *read)
{
    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x080200C2;
    cmd[1] = offset;
    cmd[2] = 0;
    cmd[3] = size;
    cmd[4] = IPC_DESC_BUF_W(size);
    cmd[5] = (u32)buf;
    int res = ipc_request(file);
    *read = res >= 0 ? cmd[2] : 0;
    return res;
}

int fs_file_write(u32 file, u32 offset, const void *buf, u32 size)
{
    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x08030102;
    cmd[1] = offset;
    cmd[2] = 0;
    cmd[3] = size;
    cmd[4] = WRITE_FLUSH;
    cmd[5] = IPC_DESC_BUF_R(size);
    cmd[6] = (u32)buf;
    int res = ipc_request(file);
    if (res >= 0 && cmd[2] != size)
        res = FS_SHORT_WRITE;
    return res;
}

void fs_file_close(u32 file)
{
    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x08080000;
    ipc_request(file);
    ipc_close(file);
}

int fs_file_exists(const u16 *path)
{
    u32 file;
    int res = open_file(&file, path, OPEN_READ);
    if (res >= 0)
        fs_file_close(file);
    return res < 0 ? res : 0;
}

int fs_file_rename(const u16 *from, const u16 *to)
{
    u32 *cmd = ipc_cmdbuf();
    u32 from_size = path_bytes(from), to_size = path_bytes(to);
    cmd[0] = 0x08050244;
    cmd[1] = 0;
    cmd[2] = (u32)sdmc;
    cmd[3] = (u32)(sdmc >> 32);
    cmd[4] = PATH_UTF16;
    cmd[5] = from_size;
    cmd[6] = (u32)sdmc;
    cmd[7] = (u32)(sdmc >> 32);
    cmd[8] = PATH_UTF16;
    cmd[9] = to_size;
    cmd[10] = IPC_DESC_STATIC(from_size, 1);
    cmd[11] = (u32)from;
    cmd[12] = IPC_DESC_STATIC(to_size, 2);
    cmd[13] = (u32)to;
    return ipc_request(fs_handle);
}
