#include "ipc.h"

#include "types.h"

#define IPC_COMMAND_WORD_OFFSET 0x20

u32 *ipc_cmdbuf(void)
{
    u32 *tls;
    __asm__ ("mrc p15, 0, %0, c13, c0, 3" : "=r"(tls));
    return tls + IPC_COMMAND_WORD_OFFSET;
}

static int svc_send(u32 handle)
{
    register u32 r0 __asm__("r0") = handle;
    __asm__ volatile ("svc 0x32" : "+r"(r0) :: "r1", "r2", "r3", "r12", "memory");
    return (int)r0;
}

void ipc_close(u32 handle)
{
    register u32 r0 __asm__("r0") = handle;
    __asm__ volatile ("svc 0x23" : "+r"(r0) :: "r1", "r2", "r3", "r12", "memory");
}

int ipc_connect_port(u32 *out, const char *port)
{
    register u32 r0 __asm__("r0") = 0;
    register const char *r1 __asm__("r1") = port;
    __asm__ volatile ("svc 0x2D" : "+r"(r0), "+r"(r1) :: "r2", "r3", "r12", "memory");
    *out = (u32)r1;
    return (int)r0;
}

int ipc_request(u32 handle)
{
    int res = svc_send(handle);
    return res < 0 ? res : (int)ipc_cmdbuf()[1];
}

int ipc_service(u32 *out, const char *name)
{
    u32 srv;
    int res = ipc_connect_port(&srv, "srv:");
    if (res < 0)
        return res;

    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x00010002;
    cmd[1] = IPC_DESC_PID;
    res = ipc_request(srv);

    if (res >= 0) {
        u32 len = 0;
        u32 words[2] = { 0, 0 };
        while (len < 8 && name[len]) {
            words[len / 4] |= (u32)(unsigned char)name[len] << (len % 4 * 8);
            len++;
        }
        cmd = ipc_cmdbuf();
        cmd[0] = 0x00050100;
        cmd[1] = words[0];
        cmd[2] = words[1];
        cmd[3] = len;
        cmd[4] = 0;
        res = ipc_request(srv);
        if (res >= 0)
            *out = cmd[3];
    }

    ipc_close(srv);
    return res;
}
