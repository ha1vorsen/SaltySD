#include "plgldr.h"
#include "ipc.h"

#include "types.h"

#define DESC_BUF_RW(size) (((size) << 4) | 0xE)

//plg:ldr allows one session at a time.
int plgldr_plugin_path(char path[PLUGIN_PATH_MAX])
{
    u32 port;
    int res = ipc_connect_port(&port, "plg:ldr");
    if (res < 0)
        return res;

    for (u32 i = 0; i < PLUGIN_PATH_MAX; i++)
        path[i] = 0;

    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x000A0002;
    cmd[1] = DESC_BUF_RW(PLUGIN_PATH_MAX - 1);
    cmd[2] = (u32)path;
    res = ipc_request(port);
    ipc_close(port);
    return res;
}
