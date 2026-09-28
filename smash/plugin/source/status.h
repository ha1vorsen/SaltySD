#ifndef SALTYSD_STATUS_H
#define SALTYSD_STATUS_H

#define SALTYSD_MAGIC 0x534C5447u /* 'SLTG' */

typedef struct {
    unsigned int magic;
    unsigned int stage;
    unsigned int patches;
    unsigned int applied;
    unsigned int verified;
    unsigned int refused;
    unsigned int menu_site;
    unsigned int tetra_opens;
    unsigned int tetra_top_fb;
    unsigned int tetra_top_fb_right;
    unsigned int tetra_bottom_fb;
    unsigned int tetra_formats;
    unsigned int tetra_exit_reason;
    unsigned int mods_listed;
    unsigned int mods_changed;
    int last_fs_result;
    int restart_result;
    unsigned int update_outcome;
    unsigned int update_net;
    int update_result;
    unsigned int install_stage;
    int install_result;
    unsigned int gate_expired;
    unsigned int legacy;
    unsigned int plugins_listed;
    unsigned int plugins_applied;
    unsigned int plugins_refused;
    int plugins_result;
    unsigned int plugins_incompatible;
} saltysd_status_t;

extern volatile saltysd_status_t saltysd_status;

#endif
