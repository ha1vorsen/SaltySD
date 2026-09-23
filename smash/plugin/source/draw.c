#include "draw.h"
#include "font8x8.h"

#include "types.h"

typedef struct {
    u8  bytes[4];
    u32 bpp;
} pixel;

static void pack_pixel(pixel *p, u32 format, u32 rgb)
{
    u32 r = rgb >> 16, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    u32 v;
    switch (format) {
    case FMT_RGBA8:
        p->bytes[0] = 0xFF; p->bytes[1] = b; p->bytes[2] = g; p->bytes[3] = r;
        p->bpp = 4;
        return;
    case FMT_BGR8:
        p->bytes[0] = b; p->bytes[1] = g; p->bytes[2] = r;
        p->bpp = 3;
        return;
    case FMT_RGB565:
        v = (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
        break;
    case FMT_RGB5A1:
        v = (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | 1;
        break;
    default:
        v = (r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | 0xF;
        break;
    }
    p->bytes[0] = v;
    p->bytes[1] = v >> 8;
    p->bpp = 2;
}

static void store_pixel(u8 *at, const pixel *p)
{
    at[0] = p->bytes[0];
    at[1] = p->bytes[1];
    if (p->bpp > 2) {
        at[2] = p->bytes[2];
        if (p->bpp > 3)
            at[3] = p->bytes[3];
    }
}

void clear(u8 *fb, const screen *s, u32 rgb)
{
    pixel p;
    pack_pixel(&p, s->format, rgb);
    for (u32 x = 0; x < s->width; x++) {
        u8 *column = fb + x * s->stride;
        for (u32 y = 0; y < SCREEN_HEIGHT; y++)
            store_pixel(column + y * p.bpp, &p);
    }
}

//Columns run bottom to top, so screen row y is at 239 - y.
static void draw_char(u8 *fb, const screen *s, u32 x, u32 y, char c, const pixel *p)
{
    if ((u8)c < FONT_FIRST || (u8)c > FONT_LAST || x + GLYPH > s->width || y + GLYPH > SCREEN_HEIGHT)
        return;

    const u8 *glyph = font8x8[(u8)c - FONT_FIRST];
    for (u32 row = 0; row < GLYPH; row++)
        for (u32 col = 0; col < GLYPH; col++)
            if (glyph[row] & (1 << col))
                store_pixel(fb + (x + col) * s->stride + (SCREEN_HEIGHT - 1 - y - row) * p->bpp,
                            p);
}

void draw_text(u8 *fb, const screen *s, u32 x, u32 y, const char *text, u32 rgb)
{
    pixel p;
    pack_pixel(&p, s->format, rgb);
    for (; *text; text++, x += GLYPH)
        draw_char(fb, s, x, y, *text, &p);
}
