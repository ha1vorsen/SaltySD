#ifndef SALTYSD_IPC_H
#define SALTYSD_IPC_H

#define IPC_DESC_PID          0x20
#define IPC_DESC_BUF_R(size)  (((size) << 4) | 0xA)
#define IPC_DESC_BUF_W(size)  (((size) << 4) | 0xC)
#define IPC_DESC_STATIC(size, id) (((size) << 14) | ((id) << 10) | 2)

unsigned int *ipc_cmdbuf(void);
int ipc_request(unsigned int handle);
void ipc_close(unsigned int handle);
int ipc_connect_port(unsigned int *out, const char *port);
int ipc_service(unsigned int *out, const char *name);

#endif
