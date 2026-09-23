#ifndef SALTYSD_DISPLAY_H
#define SALTYSD_DISPLAY_H

#include "draw.h"

typedef struct {
    volatile unsigned int *header;
    unsigned int width;
} display_screen;

typedef struct {
    screen layout;
    unsigned char *left;
    unsigned char *right;
    unsigned int slot;
    unsigned int snapshot;
} display_frame;

typedef struct {
    display_screen top;
    display_screen bottom;
    int has_top;
    int has_bottom;
} display;

int display_open(display *out);
int display_begin(const display *d, display_frame *top, display_frame *bottom);
int display_present(const display *d, const display_frame *top,
                    const display_frame *bottom);

#endif
