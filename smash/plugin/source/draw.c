#include "draw.h"
#include "font8x8.h"

typedef unsigned char u8;
typedef unsigned int  u32;

#define FB_ENTRY_OFFS  0x4
#define FB_ENTRY_SIZE  0x1C
#define FB_LEFT_OFFS   0x4
#define FB_RIGHT_OFFS  0x8
#define FB_STRIDE_OFFS 0xC
#define FB_FORMAT_OFFS 0x10

void flush_data_cache(void)
{
    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
}

int find_screen(screen *out, u32 info, u32 width)
{
    if (!info)
        return 0;

    u32 entry = info + FB_ENTRY_OFFS + (*(volatile u8 *)info & 1) * FB_ENTRY_SIZE;
    out->left = *(u8 **)(entry + FB_LEFT_OFFS);
    out->right = *(u8 **)(entry + FB_RIGHT_OFFS);
    out->stride = *(u32 *)(entry + FB_STRIDE_OFFS);
    out->format = *(u32 *)(entry + FB_FORMAT_OFFS) & 7;
    out->width = width;

    if (out->right == out->left)
        out->right = 0;
    return out->left && out->stride && out->format <= FMT_RGBA4;
}

typedef struct {
    u8  b[4];
    u32 bpp;
} pixel;

static void pack_pixel(pixel *p, u32 format, u32 rgb)
{
    u32 r = rgb >> 16, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    u32 v;
    switch (format) {
    case FMT_RGBA8:
        p->b[0] = 0xFF; p->b[1] = b; p->b[2] = g; p->b[3] = r;
        p->bpp = 4;
        return;
    case FMT_BGR8:
        p->b[0] = b; p->b[1] = g; p->b[2] = r;
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
    p->b[0] = v;
    p->b[1] = v >> 8;
    p->bpp = 2;
}

static void store_pixel(u8 *at, const pixel *p)
{
    at[0] = p->b[0];
    at[1] = p->b[1];
    if (p->bpp > 2) {
        at[2] = p->b[2];
        if (p->bpp > 3)
            at[3] = p->b[3];
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
