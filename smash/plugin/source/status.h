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
    unsigned int menu_opens;
    unsigned int menu_top_fb;
    unsigned int menu_top_fb_right;
    unsigned int menu_bottom_fb;
    unsigned int menu_formats;
} saltysd_status_t;

extern volatile saltysd_status_t saltysd_status;

#endif
