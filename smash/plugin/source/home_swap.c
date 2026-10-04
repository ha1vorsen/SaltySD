#include "types.h"
#include "ipc.h"
#include "common.h"

/* local 3GX ABI mirror; no platform SDK dependency */
#define PLG_WAIT           (-1)
#define PLG_OK             0
#define PLG_ABOUT_TO_SWAP  3
#define PLG_GATE_LOCK      0x80000000u
#define PLG_GATE_ACTIVE    0x7fffffffu
#define THREADVARS_MAGIC   0x21545624u

typedef struct {
    u32 magic;
    u32 version;
    u32 heap_va;
    u32 heap_size;
    u32 exe_size;
    u32 is_default;
    volatile int *event;
    volatile int *reply;
} PluginHeader;

extern int saltysd_svc_create_thread(u32 *out, void (*entry)(u32), u32 arg,
                                     u32 *stack_top, int priority, int core);
extern void saltysd_svc_sleep(u64 ns);
extern int saltysd_svc_arbitrate(u32 arbiter, u32 address, int action,
                                 int value, u64 timeout);
extern void *get_thread_tls(void);
extern int saltysd_gate_try_lock(volatile u32 *gate);

static u8 home_stack[0x1000] __attribute__((aligned(8)));

static volatile u32 *home_gate(void)
{
    return (volatile u32 *)saltysd_home_gate_ADDR;
}

static void gate_lock(void)
{
    volatile u32 *gate = home_gate();
    u32 old;

    /* no admission gap between adjacent loads */
    while (!saltysd_gate_try_lock(gate))
        saltysd_svc_sleep(1000000);

    old = *gate;
    while ((old & PLG_GATE_ACTIVE) != 0) {
        saltysd_svc_sleep(1000000);
        old = *gate;
    }
}

static void gate_unlock(void)
{
    *home_gate() = 0;
}

static int plgldr_arbiter(u32 *out)
{
    u32 port;
    int res = ipc_connect_port(&port, "plg:ldr");
    if (res < 0)
        return res;
    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x00090000;
    res = ipc_request(port);
    if (res >= 0)
        *out = cmd[3];
    ipc_close(port);
    return res;
}

static void home_worker(u32 ignored)
{
    (void)ignored;
    *(volatile u32 *)get_thread_tls() = THREADVARS_MAGIC;

    u32 arbiter;
    if (plgldr_arbiter(&arbiter) < 0)
        return;

    PluginHeader *header = (PluginHeader *)0x07000000;
    volatile int *event = header->event;
    volatile int *reply = header->reply;
    if (!event || !reply)
        return;

    for (;;) {
        if (*event != PLG_ABOUT_TO_SWAP) {
            saltysd_svc_sleep(1000000);
            continue;
        }

        gate_lock();

        /* wait state before swap acknowledgement */
        while (*reply != PLG_WAIT)
            saltysd_svc_sleep(1000000);
        *reply = PLG_OK;
        *event = PLG_WAIT;
        saltysd_svc_arbitrate(arbiter, (u32)reply, 0, 1, 0);

        while (*event < PLG_OK)
            saltysd_svc_sleep(1000000);
        gate_unlock();
    }
}

void saltysd_home_swap_start(void)
{
    u32 thread;
    saltysd_svc_create_thread(&thread, home_worker, 0,
                              (u32 *)(home_stack + sizeof(home_stack)),
                              0x3f, -1);
}
