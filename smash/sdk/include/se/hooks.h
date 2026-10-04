#ifndef SALTYSD_SE_HOOKS_H
#define SALTYSD_SE_HOOKS_H

#include <se/types.h>

#define SE_HOOK_EXCLUSIVE 0x00000001u
#define SE_HOOK_CHAINABLE 0x00000002u

typedef se_u32 se_hook_handle;

typedef struct {
    se_u32 target_id;
    void *handler;
    se_u32 flags;
    se_s32 priority;
} se_hook_request;

typedef struct {
    se_hook_handle handle;
    void *next;
    void *original;
} se_hook_result;

#endif
