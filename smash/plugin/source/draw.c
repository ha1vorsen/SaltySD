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

static u32 bytes_per_pixel(u32 format)
{
    return format == FMT_RGBA8 ? 4 : format == FMT_BGR8 ? 3 : 2;
}

static void put_pixel(u8 *at, u32 format, u32 rgb)
{
    u32 r = rgb >> 16, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    u32 v;
    switch (format) {
    case FMT_RGBA8:
        at[0] = 0xFF; at[1] = b; at[2] = g; at[3] = r;
        return;
    case FMT_BGR8:
        at[0] = b; at[1] = g; at[2] = r;
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
    at[0] = v;
    at[1] = v >> 8;
}

void clear(u8 *fb, const screen *s, u32 rgb)
{
    u32 bpp = bytes_per_pixel(s->format);
    for (u32 x = 0; x < s->width; x++) {
        u8 *column = fb + x * s->stride;
        for (u32 y = 0; y < SCREEN_HEIGHT; y++)
            put_pixel(column + y * bpp, s->format, rgb);
    }
}

//Columns run bottom to top, so screen row y is at 239 - y.
static void draw_char(u8 *fb, const screen *s, u32 x, u32 y, char c, u32 rgb)
{
    if ((u8)c < FONT_FIRST || (u8)c > FONT_LAST || x + GLYPH > s->width || y + GLYPH > SCREEN_HEIGHT)
        return;

    const u8 *glyph = font8x8[(u8)c - FONT_FIRST];
    u32 bpp = bytes_per_pixel(s->format);
    for (u32 row = 0; row < GLYPH; row++)
        for (u32 col = 0; col < GLYPH; col++)
            if (glyph[row] & (1 << col))
                put_pixel(fb + (x + col) * s->stride + (SCREEN_HEIGHT - 1 - y - row) * bpp,
                          s->format, rgb);
}

void draw_text(u8 *fb, const screen *s, u32 x, u32 y, const char *text, u32 rgb)
{
    for (; *text; text++, x += GLYPH)
        draw_char(fb, s, x, y, *text, rgb);
}
