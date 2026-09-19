#include "common.h"
#include "status.h"

typedef unsigned char      u8;
typedef unsigned int       u32;
typedef unsigned long long u64;

#define GSP_CLIENT_OFFS      0x1C
#define GSP_TOP_INFO_OFFS    0x5C
#define GSP_BOTTOM_INFO_OFFS 0x60
#define FB_ENTRY_OFFS        0x4
#define FB_ENTRY_SIZE        0x1C
#define FB_LEFT_OFFS         0x4
#define FB_RIGHT_OFFS        0x8
#define FB_STRIDE_OFFS       0xC
#define FB_FORMAT_OFFS       0x10

#define HID_PAD_OFFS         0x4
#define PAD_INDEX_WORD       4
#define PAD_ENTRY_WORD       10
#define PAD_ENTRY_WORDS      4
#define KEY_B                (1u << 1)

#define SCREEN_HEIGHT        240
#define TOP_WIDTH            400
#define BOTTOM_WIDTH         320
#define BORDER               4

#define POLL_NS              16000000ull
#define REDRAW_POLLS         6

enum { FMT_RGBA8, FMT_BGR8, FMT_RGB565, FMT_RGB5A1, FMT_RGBA4 };

typedef struct {
    u8 *left;
    u8 *right;
    u32 stride;
    u32 format;
    u32 width;
} screen;

static void sleep_ns(u64 ns)
{
    register u32 lo __asm__("r0") = (u32)ns;
    register u32 hi __asm__("r1") = (u32)(ns >> 32);
    __asm__ volatile("svc 0x0A" : "+r"(lo), "+r"(hi) :: "r2", "r3", "r12", "memory");
}

static void flush_data_cache(void)
{
    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
}

static u32 keys_held(volatile u32 *pad)
{
    u32 index = pad[PAD_INDEX_WORD];
    if (index > 7)
        index = 7;
    return pad[PAD_ENTRY_WORD + index * PAD_ENTRY_WORDS];
}

static int find_screen(screen *out, u32 info, u32 width)
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

static void put_pixel(u8 *at, u32 format, u32 r, u32 g, u32 b)
{
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

static void fill(u8 *fb, const screen *s)
{
    u32 bpp = bytes_per_pixel(s->format);
    for (u32 x = 0; x < s->width; x++) {
        u8 *column = fb + x * s->stride;
        for (u32 y = 0; y < SCREEN_HEIGHT; y++) {
            int edge = x < BORDER || x >= s->width - BORDER ||
                       y < BORDER || y >= SCREEN_HEIGHT - BORDER;
            if (edge)
                put_pixel(column + y * bpp, s->format, 0xFF, 0xFF, 0xFF);
            else
                put_pixel(column + y * bpp, s->format, 0x00, 0x80, 0x80);
        }
    }
}

static void draw(const screen *top, int has_top, const screen *bottom, int has_bottom)
{
    if (has_top) {
        fill(top->left, top);
        if (top->right)
            fill(top->right, top);
    }
    if (has_bottom)
        fill(bottom->left, bottom);
    flush_data_cache();
}

void host_menu_run(void)
{
    u32 client = *(u32 *)(gsp_state_ADDR + GSP_CLIENT_OFFS);
    volatile u32 *pad = *(volatile u32 **)(hid_object_ADDR + HID_PAD_OFFS);
    if (!client || !pad)
        return;

    screen top, bottom;
    int has_top = find_screen(&top, *(u32 *)(client + GSP_TOP_INFO_OFFS), TOP_WIDTH);
    int has_bottom = find_screen(&bottom, *(u32 *)(client + GSP_BOTTOM_INFO_OFFS), BOTTOM_WIDTH);
    if (!has_top && !has_bottom)
        return;

    saltysd_status.menu_top_fb = has_top ? (u32)top.left : 0;
    saltysd_status.menu_top_fb_right = has_top ? (u32)top.right : 0;
    saltysd_status.menu_bottom_fb = has_bottom ? (u32)bottom.left : 0;
    saltysd_status.menu_formats = (has_top ? top.format : 0xFF) << 8 |
                                  (has_bottom ? bottom.format : 0xFF);

    u32 polls = 0;
    for (int phase = 0; phase < 3; phase++) {
        for (;;) {
            if (polls++ % REDRAW_POLLS == 0)
                draw(&top, has_top, &bottom, has_bottom);

            int down = (keys_held(pad) & KEY_B) != 0;
            if (phase == 1 ? down : !down)
                break;
            sleep_ns(POLL_NS);
        }
    }
}
