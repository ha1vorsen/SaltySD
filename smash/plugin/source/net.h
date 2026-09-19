#ifndef SALTYSD_NET_H
#define SALTYSD_NET_H

enum {
    NET_OK,
    NET_NO_WIFI,
    NET_SERVICE,
    NET_CONTEXT,
    NET_REQUEST,
    NET_STATUS,
    NET_TOO_BIG,
    NET_RECEIVE,
    NET_ABORTED,
};

typedef struct {
    unsigned int stage;
    int result;
    unsigned int http_status;
    unsigned int size;
} net_result;

int net_wifi_connected(int *result);
typedef int (*net_sink)(void *ctx, const void *data, unsigned int size);

void net_fetch(net_result *out, const char *url, net_sink sink, void *ctx, unsigned int max);
void net_get(net_result *out, const char *url, void *buf, unsigned int max);

#endif
