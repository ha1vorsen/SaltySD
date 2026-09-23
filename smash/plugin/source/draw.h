#ifndef SALTYSD_DRAW_H
#define SALTYSD_DRAW_H

#define SCREEN_HEIGHT 240
#define TOP_WIDTH     400
#define BOTTOM_WIDTH  320
#define GLYPH         8

enum { FMT_RGBA8, FMT_BGR8, FMT_RGB565, FMT_RGB5A1, FMT_RGBA4 };

typedef struct {
    unsigned int stride;
    unsigned int format;
    unsigned int width;
} screen;

void clear(unsigned char *fb, const screen *s, unsigned int rgb);
void draw_text(unsigned char *fb, const screen *s, unsigned int x, unsigned int y,
               const char *text, unsigned int rgb);

#endif
