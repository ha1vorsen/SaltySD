#ifndef SALTYSD_UPDATE_H
#define SALTYSD_UPDATE_H

#include "net.h"

enum {
    UPDATE_CURRENT,
    UPDATE_AVAILABLE,
    UPDATE_NET_FAILED,
    UPDATE_CODE_REJECTED,
    UPDATE_BAD_FORMAT,
    UPDATE_UNKNOWN_KEY,
    UPDATE_KEY_NOT_ALLOWED,
    UPDATE_WRONG_CHANNEL,
    UPDATE_BAD_SIGNATURE,
    UPDATE_NO_FILE,
};

typedef struct {
    unsigned int channel;
    char code[4];
    char base[48];
    unsigned int outcome;
    net_result net;
    unsigned int net_file;
    unsigned int key_id;
    char identity[48];
    char file[64];
    unsigned int file_size;
    unsigned char file_hash[64];
} update_check;

void update_check_run(update_check *out, unsigned int channel, const char code[4]);

int update_load_code(char code[4]);

enum { GATE_PASS, GATE_EXPIRED, GATE_ERROR };

unsigned int update_gate(update_check *out);

enum {
    INSTALL_DONE,
    INSTALL_NO_PATH,
    INSTALL_ODD_PATH,
    INSTALL_SD,
    INSTALL_CREATE,
    INSTALL_DOWNLOAD,
    INSTALL_WRITE,
    INSTALL_SIZE,
    INSTALL_HASH,
    INSTALL_READBACK,
    INSTALL_BACKUP,
    INSTALL_SWAP,
    INSTALL_SWAP_STUCK,
};

typedef struct {
    unsigned int stage;
    int result;
    net_result net;
} update_install_result;

typedef void (*update_progress)(void *ctx, unsigned int done, unsigned int total);

int update_install(const update_check *check, update_install_result *out, update_progress progress, void *ctx);

#endif
