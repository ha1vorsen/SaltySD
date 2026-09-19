#include "common.h"
#include "status.h"

typedef unsigned char u8;
typedef unsigned int  u32;

#define CRO_MAGIC_OFFS      0x80
#define CRO_NAME_OFFS       0x84
#define CRO_CODE_OFFS       0xB0
#define CRO_CODE_SIZE_OFFS  0xB4
#define CRO_MAGIC_LOADED    0x304F5243u
#define CRO_MAGIC_FIXED     0x44584946u

#define STATE_NOTICES       0x6F
#define STATE_MAIN_MENU     0x3

#define ISLAND_MENU_TRAMP   (cro_fighter_new_ADDR + 0x3C4 + 0x220 + 0xC)

#define SIG_WORDS           15
#define SIG_SITE_OFFS       0x2C
#define BL_MASK             0xFF000000u
#define BL_AL               0xEB000000u

static const u32 menu_sig[SIG_WORDS] = {
    0xE28400A0, BL_AL,      0xE58D0000, 0xE3A00003, 0xE58D0004, 0xE28D2004,
    0xE1A0100D, 0xE28400D0, BL_AL,      0xE5901000, 0xE2840058, BL_AL,
    0xE2840058, BL_AL,      0xE350004A,
};

u32 saltysd_menu_orig;

void host_menu_run(void);

static int is_menu(const char *name)
{
    return name[0] == 'm' && name[1] == 'e' && name[2] == 'n' && name[3] == 'u' && !name[4];
}

static int sig_at(const u32 *at)
{
    for (u32 i = 0; i < SIG_WORDS; i++) {
        u32 mask = menu_sig[i] == BL_AL ? BL_MASK : 0xFFFFFFFFu;
        if ((at[i] & mask) != menu_sig[i])
            return 0;
    }
    return 1;
}

static u32 bl_target(u32 site, u32 word)
{
    int imm = (int)(word << 8) >> 8;
    return site + 8 + (u32)(imm * 4);
}

static u32 bl_to(u32 site, u32 target)
{
    return BL_AL | (((target - (site + 8)) >> 2) & 0x00FFFFFFu);
}

static void install(u32 base)
{
    u32 code = *(u32 *)(base + CRO_CODE_OFFS);
    u32 size = *(u32 *)(base + CRO_CODE_SIZE_OFFS);
    if (size < SIG_WORDS * 4)
        return;

    u32 found = 0, matches = 0;
    for (u32 at = code; at <= code + size - SIG_WORDS * 4; at += 4) {
        if (sig_at((const u32 *)at)) {
            found = at;
            matches++;
        }
    }
    if (matches != 1)
        return;

    u32 site = found + SIG_SITE_OFFS;
    volatile u32 *word = (volatile u32 *)site;
    u32 hook = bl_to(site, ISLAND_MENU_TRAMP);

    if (*word == hook)
        return;

    int dist = (int)(ISLAND_MENU_TRAMP - (site + 8));
    if (dist < -0x2000000 || dist >= 0x2000000)
        return;

    saltysd_menu_orig = bl_target(site, *word);
    *word = hook;
    saltysd_status.menu_site = site;

    __asm__ volatile ("svc 0x92" ::: "r0", "r1", "r2", "r3", "r12", "memory");
    __asm__ volatile ("svc 0x94" ::: "r0", "r1", "r2", "r3", "r12", "memory");
}

void *saltysd_cro_loaded(void *request)
{
    if (!request)
        return request;

    u32 base = *(u32 *)request;
    if (!base)
        return request;

    u32 magic = *(u32 *)(base + CRO_MAGIC_OFFS);
    if (magic != CRO_MAGIC_LOADED && magic != CRO_MAGIC_FIXED)
        return request;

    u32 name = *(u32 *)(base + CRO_NAME_OFFS);
    if (name < base)
        name += base;

    if (is_menu((const char *)name))
        install(base);

    return request;
}

u32 saltysd_menu_state(u32 state)
{
    if (state != STATE_NOTICES)
        return state;

    saltysd_status.menu_opens++;
    host_menu_run();
    return STATE_MAIN_MENU;
}
