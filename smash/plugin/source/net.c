#include "net.h"
#include "ipc.h"

#include "types.h"

#define METHOD_GET       1
#define HTTP_OK          200
#define RECEIVE_PENDING  ((int)0xD840A02B)
#define RECEIVE_CHUNK    0x1000

static const char user_agent_name[] = "User-Agent";
static const char user_agent[] = "SaltySD";

static u32 strsize(const char *s)
{
    u32 n = 0;
    while (s[n])
        n++;
    return n + 1;
}

int net_wifi_connected(int *result)
{
    u32 ac;
    int res = ipc_service(&ac, "ac:u");
    if (res >= 0) {
        u32 *cmd = ipc_cmdbuf();
        cmd[0] = 0x000D0000;
        res = ipc_request(ac);
        ipc_close(ac);
        if (res >= 0) {
            *result = 0;
            return cmd[2] != 0;
        }
    }
    *result = res;
    return 0;
}

static int context_call(u32 session, u32 header, u32 context)
{
    u32 *cmd = ipc_cmdbuf();
    cmd[0] = header;
    cmd[1] = context;
    return ipc_request(session);
}

static int fail(net_result *out, u32 stage, int res)
{
    out->stage = stage;
    out->result = res;
    return 0;
}

static u8 chunk_buf[RECEIVE_CHUNK];

static int request(net_result *out, u32 *session, u32 context, net_sink sink, void *ctx, u32 max)
{
    int res = ipc_service(session, "http:C");
    if (res < 0)
        return fail(out, NET_SERVICE, res);

    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x00080042;
    cmd[1] = context;
    cmd[2] = IPC_DESC_PID;
    res = ipc_request(*session);
    if (res >= 0)
        res = context_call(*session, 0x000E0040, context);
    if (res >= 0) {
        cmd = ipc_cmdbuf();
        cmd[0] = 0x001100C4;
        cmd[1] = context;
        cmd[2] = sizeof(user_agent_name);
        cmd[3] = sizeof(user_agent);
        cmd[4] = IPC_DESC_STATIC(sizeof(user_agent_name), 3);
        cmd[5] = (u32)user_agent_name;
        cmd[6] = IPC_DESC_BUF_R(sizeof(user_agent));
        cmd[7] = (u32)user_agent;
        res = ipc_request(*session);
    }
    if (res >= 0)
        res = context_call(*session, 0x00090040, context);
    if (res < 0)
        return fail(out, NET_REQUEST, res);

    res = context_call(*session, 0x00220040, context);
    if (res < 0)
        return fail(out, NET_STATUS, res);
    out->http_status = ipc_cmdbuf()[2];
    if (out->http_status != HTTP_OK)
        return fail(out, NET_STATUS, 0);

    res = context_call(*session, 0x00060040, context);
    if (res >= 0 && ipc_cmdbuf()[3] > max)
        return fail(out, NET_TOO_BIG, 0);

    u32 got = 0;
    for (;;) {
        cmd = ipc_cmdbuf();
        cmd[0] = 0x000B0082;
        cmd[1] = context;
        cmd[2] = RECEIVE_CHUNK;
        cmd[3] = IPC_DESC_BUF_W(RECEIVE_CHUNK);
        cmd[4] = (u32)chunk_buf;
        res = ipc_request(*session);
        if (res < 0 && res != RECEIVE_PENDING)
            return fail(out, NET_RECEIVE, res);
        int more = res == RECEIVE_PENDING;

        if (context_call(*session, 0x00060040, context) < 0)
            return fail(out, NET_RECEIVE, res);
        u32 total = ipc_cmdbuf()[2];
        if (total < got || total - got > RECEIVE_CHUNK)
            return fail(out, NET_RECEIVE, 0);
        if (total > max)
            return fail(out, NET_TOO_BIG, 0);
        if (total > got && !sink(ctx, chunk_buf, total - got))
            return fail(out, NET_ABORTED, 0);
        got = total;
        if (!more)
            break;
    }

    out->size = got;
    return 1;
}

typedef struct {
    u8 *buf;
    u32 used;
} memory_sink;

static int to_memory(void *ctx, const void *data, u32 size)
{
    memory_sink *m = ctx;
    const u8 *p = data;
    for (u32 i = 0; i < size; i++)
        m->buf[m->used++] = p[i];
    return 1;
}

void net_fetch(net_result *out, const char *url, net_sink sink, void *ctx, u32 max)
{
    out->stage = NET_OK;
    out->result = 0;
    out->http_status = 0;
    out->size = 0;

    int res;
    if (!net_wifi_connected(&res)) {
        fail(out, NET_NO_WIFI, res);
        return;
    }

    u32 main;
    res = ipc_service(&main, "http:C");
    if (res < 0) {
        fail(out, NET_SERVICE, res);
        return;
    }

    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x00010044;
    cmd[1] = 0;
    cmd[2] = IPC_DESC_PID;
    cmd[4] = 0;
    cmd[5] = 0;
    res = ipc_request(main);
    if (res < 0) {
        fail(out, NET_SERVICE, res);
        ipc_close(main);
        return;
    }

    u32 size = strsize(url);
    cmd = ipc_cmdbuf();
    cmd[0] = 0x00020082;
    cmd[1] = size;
    cmd[2] = METHOD_GET;
    cmd[3] = IPC_DESC_BUF_R(size);
    cmd[4] = (u32)url;
    res = ipc_request(main);
    if (res < 0) {
        fail(out, NET_CONTEXT, res);
        ipc_close(main);
        return;
    }
    u32 context = ipc_cmdbuf()[2];

    u32 session = 0;
    request(out, &session, context, sink, ctx, max);
    if (session)
        ipc_close(session);

    context_call(main, 0x00030040, context);
    ipc_close(main);
}

void net_get(net_result *out, const char *url, void *buf, u32 max)
{
    memory_sink m = { buf, 0 };
    net_fetch(out, url, to_memory, &m, max);
}
