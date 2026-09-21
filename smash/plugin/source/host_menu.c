#include "common.h"
#include "status.h"
#include "draw.h"
#include "fs.h"
#include "mods.h"
#include "update.h"
#include "version.h"
#include "input.h"

typedef unsigned char      u8;
typedef unsigned int       u32;
typedef unsigned long long u64;

#define GSP_CLIENT_OFFS      0x1C
#define GSP_TOP_INFO_OFFS    0x5C
#define GSP_BOTTOM_INFO_OFFS 0x60

#define APPLET_CLOSE_A       0x06
#define APPLET_CLOSE_B       0x09
#define APPLET_SLEEP_REPLY   0x0B
#define APPLET_HOME_A        0x0C
#define APPLET_HOME_B        0x0D
#define SLEEP_REPLY_LATER    1

#define POLL_NS              16000000ull
#define REDRAW_POLLS         6
#define RESTART_DELAY_NS     1000000000ull
#define PROGRESS_STEP        0x4000

#define BG_RGB               0x10141C
#define TEXT_RGB             0xE0E0E0
#define TITLE_RGB            0x40C0C0
#define CURSOR_RGB           0xFFD040
#define DIM_RGB              0x808890

#define MOD_ROWS             20
#define MOD_ROW_Y            24
#define MOD_ROW_H            10
#define MOD_NAME_SHOWN       40

enum { EXIT_NONE, EXIT_BACK, EXIT_HOME, EXIT_SLEEP, EXIT_CLOSE };
enum { ITEM_MODS, ITEM_REBUILD, ITEM_STABLE, ITEM_DIRTY, ITEM_BACK, ITEM_COUNT };
enum { VIEW_MAIN, VIEW_MODS, VIEW_CONFIRM, VIEW_BUSY, VIEW_CODE, VIEW_OFFER,
       VIEW_INSTALL_CONFIRM, VIEW_REBUILD, VIEW_COUNT };

static const char *const item_names[ITEM_COUNT] = {
    "Mods", "Rebuild mod index", "Check for updates (stable)",
    "Check for updates (dirty)", "Back",
};

typedef struct {
    screen top, bottom;
    int has_top, has_bottom;
    u32 view;
    u32 cursor;
    u32 mod_cursor;
    u32 mod_first;
    char code[4];
    u32 code_pos;
    const char *status;
    const char *status2;
} menu;

static int (*const restart_app)(const void *param, u32 size) = (void *)restart_app_ADDR;

static char title_buf[64];
static char status_buf[64];
static char status2_buf[64];
static update_check last_check;

typedef struct {
    char *buf;
    u32 len;
} text;

static void put_str(text *t, const char *s)
{
    while (*s && t->len < 63)
        t->buf[t->len++] = *s++;
    t->buf[t->len] = 0;
}

static void put_dec(text *t, u32 v)
{
    char digits[10];
    u32 n = 0;
    do {
        digits[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    while (n && t->len < 63)
        t->buf[t->len++] = digits[--n];
    t->buf[t->len] = 0;
}

static void put_hex(text *t, u32 v)
{
    put_str(t, "0x");
    for (int shift = 28; shift >= 0 && t->len < 63; shift -= 4)
        t->buf[t->len++] = "0123456789ABCDEF"[(v >> shift) & 0xF];
    t->buf[t->len] = 0;
}

//max is the width the caller can draw, not a name length: the mark has to
//fit inside it or it lands past the edge of the screen and is never seen.
static void put_name(text *t, const unsigned short *name, u32 max)
{
    u32 len = 0;
    while (name[len])
        len++;

    u32 show = len;
    if (len > max)
        show = max > 3 ? max - 3 : 0;

    for (u32 i = 0; i < show && t->len < 63; i++)
        t->buf[t->len++] = name[i] >= 0x20 && name[i] <= 0x7E ? (char)name[i] : '?';
    t->buf[t->len] = 0;
    if (len > max)
        put_str(t, "...");
}

static void sleep_ns(u64 ns)
{
    register u32 lo __asm__("r0") = (u32)ns;
    register u32 hi __asm__("r1") = (u32)(ns >> 32);
    __asm__ volatile("svc 0x0A" : "+r"(lo), "+r"(hi) :: "r2", "r3", "r12", "memory");
}

static u32 applet_pending(void)
{
    volatile u8 *state = (volatile u8 *)applet_state_ADDR;
    if (state[APPLET_HOME_A] || state[APPLET_HOME_B])
        return EXIT_HOME;
    if (state[APPLET_SLEEP_REPLY] == SLEEP_REPLY_LATER)
        return EXIT_SLEEP;
    if (state[APPLET_CLOSE_A] || state[APPLET_CLOSE_B])
        return EXIT_CLOSE;
    return EXIT_NONE;
}

static void set_status(menu *m, const char *line, const char *line2)
{
    m->status = line;
    m->status2 = line2;
}

static void draw(const menu *m);

static void paint_main(u8 *fb, const screen *s, const menu *m)
{
    draw_text(fb, s, 8, 8, title_buf, TITLE_RGB);
    for (u32 i = 0; i < ITEM_COUNT; i++) {
        int on = i == m->cursor;
        draw_text(fb, s, 8, 32 + i * 12, on ? ">" : " ", CURSOR_RGB);
        draw_text(fb, s, 24, 32 + i * 12, item_names[i], on ? CURSOR_RGB : TEXT_RGB);
    }
}

static void paint_mods(u8 *fb, const screen *s, const menu *m)
{
    char line[64];
    text t = { line, 0 };
    put_str(&t, "Mods ");
    if (mods_count > MOD_ROWS) {
        u32 last = m->mod_first + MOD_ROWS;
        put_dec(&t, m->mod_first + 1);
        put_str(&t, "-");
        put_dec(&t, last > mods_count ? mods_count : last);
        put_str(&t, " of ");
        put_dec(&t, mods_count);
    } else {
        put_str(&t, "(");
        put_dec(&t, mods_count);
        put_str(&t, ")");
    }
    draw_text(fb, s, 8, 8, line, TITLE_RGB);

    if (!mods_count)
        draw_text(fb, s, 8, MOD_ROW_Y, "No mod folders in /saltysd/smash", DIM_RGB);

    for (u32 row = 0; row < MOD_ROWS && m->mod_first + row < mods_count; row++) {
        u32 i = m->mod_first + row;
        const mod_entry *mod = &mods[i];
        int on = i == m->mod_cursor;
        u32 y = MOD_ROW_Y + row * MOD_ROW_H;
        u32 rgb = on ? CURSOR_RGB : mod->wanted ? TEXT_RGB : DIM_RGB;

        if (mod->wanted != mod->enabled)
            draw_text(fb, s, 8, y, "*", CURSOR_RGB);
        if (on)
            draw_text(fb, s, 16, y, ">", CURSOR_RGB);
        draw_text(fb, s, mod->wanted ? 40 : 32, y, mod->wanted ? "[On]" : "[Off]", rgb);

        t.len = 0;
        put_name(&t, mod->name, MOD_NAME_SHOWN);
        draw_text(fb, s, 80, y, line, rgb);
    }
}

static void paint_code(u8 *fb, const screen *s, const menu *m)
{
    draw_text(fb, s, 8, 8, "Nightly builds", TITLE_RGB);
    draw_text(fb, s, 8, 32, "Enter today's code:", TEXT_RGB);
    for (u32 i = 0; i < 4; i++) {
        char digit[2] = { m->code[i], 0 };
        int on = i == m->code_pos;
        draw_text(fb, s, 24 + i * 16, 52, digit, on ? CURSOR_RGB : TEXT_RGB);
        if (on)
            draw_text(fb, s, 24 + i * 16, 62, "^", CURSOR_RGB);
    }
}

//The top screen would otherwise keep showing the menu behind a blocking step.
static void paint_busy(u8 *fb, const screen *s, const menu *m)
{
    draw_text(fb, s, 8, 8, title_buf, TITLE_RGB);
    draw_text(fb, s, 8, 32, m->status ? m->status : "Working...", TEXT_RGB);
    if (m->status2)
        draw_text(fb, s, 8, 44, m->status2, DIM_RGB);
}

static void hint_main(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "Up/Down: Highlight  A: Select  B: Back", DIM_RGB);
}

static void hint_mods(u8 *fb, const screen *s, const menu *m)
{
    char line[64];
    text t = { line, 0 };

    (void)m;
    draw_text(fb, s, 8, 8, "Up/Down: Move  A: On/Off", DIM_RGB);
    draw_text(fb, s, 8, 20, "START: Apply  B: Back/Discard", DIM_RGB);
    put_dec(&t, mods_changes());
    put_str(&t, " Change(s) not applied");
    draw_text(fb, s, 8, 44, line, TEXT_RGB);
}

static void hint_confirm(u8 *fb, const screen *s, const menu *m)
{
    char line[64];
    text t = { line, 0 };

    (void)m;
    put_str(&t, "Write ");
    put_dec(&t, mods_changes());
    put_str(&t, " change(s) and restart?");
    draw_text(fb, s, 8, 8, line, TEXT_RGB);
    draw_text(fb, s, 8, 20, "A: Yes  B: No", CURSOR_RGB);
}

static void hint_code(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "Up/Down: Digit  Left/Right: Move", DIM_RGB);
    draw_text(fb, s, 8, 20, "A: Check  B: Back", DIM_RGB);
}

static void hint_offer(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "A: Install  B: Back", CURSOR_RGB);
}

static void hint_install_confirm(u8 *fb, const screen *s, const menu *m)
{
    char line[64];
    text t = { line, 0 };

    (void)m;
    put_str(&t, "Install ");
    put_str(&t, last_check.identity);
    put_str(&t, " and restart?");
    draw_text(fb, s, 8, 8, line, TEXT_RGB);
    draw_text(fb, s, 8, 20, "A: Yes  B: No", CURSOR_RGB);
}

static void restart(menu *m)
{
    set_status(m, 0, 0);
    m->view = VIEW_BUSY;
    draw(m);
    sleep_ns(RESTART_DELAY_NS);

    fs_close();
    int res = restart_app(0, 0);
    saltysd_status.restart_result = res;

    text t = { status_buf, 0 };
    put_str(&t, "Restart failed: ");
    put_hex(&t, (u32)res);
    set_status(m, status_buf, 0);
}

static void fs_failed(menu *m, const char *what, int res)
{
    text t = { status_buf, 0 };
    put_str(&t, what);
    put_str(&t, " failed: ");
    put_hex(&t, (u32)res);
    saltysd_status.last_fs_result = res;
    set_status(m, status_buf, 0);
}

static void open_mods(menu *m)
{
    int res = fs_open();
    if (res < 0) {
        fs_failed(m, "SD access", res);
        return;
    }
    res = mods_load();
    if (res < 0) {
        fs_close();
        fs_failed(m, "Reading /saltysd/smash", res);
        return;
    }

    m->view = VIEW_MODS;
    m->mod_cursor = m->mod_first = 0;
    set_status(m, 0, 0);
    if (mods_skipped) {
        text t = { status_buf, 0 };
        put_dec(&t, mods_skipped);
        put_str(&t, " folder(s) not shown: name too long or over 62");
        set_status(m, status_buf, 0);
    }
}

static void apply_mods(menu *m)
{
    mods_apply_result r;
    mods_apply(&r);

    text t = { status_buf, 0 };
    put_str(&t, "Applied ");
    put_dec(&t, r.applied);
    set_status(m, status_buf, 0);
    if (r.failed) {
        put_str(&t, ", failed ");
        put_dec(&t, r.failed);
        text t2 = { status2_buf, 0 };
        put_name(&t2, r.first_failed->name, 40);
        put_str(&t2, " ");
        put_hex(&t2, (u32)r.first_error);
        m->status2 = status2_buf;
    }
    if (r.applied) {
        int res = mods_drop_index();
        if (res < 0)
            saltysd_status.last_fs_result = res;
        restart(m);
    }
}

static const char *const net_stage_names[] = {
    "ok", "no Wi-Fi", "http:C", "context", "request", "status", "too big", "receive", "aborted",
};

static void put_net_stage(text *t, const net_result *net)
{
    put_str(t, net->stage < sizeof(net_stage_names) / sizeof(net_stage_names[0]) ?
               net_stage_names[net->stage] : "?");
}

static void put_net_detail(text *t, const net_result *net)
{
    if (net->http_status) {
        put_str(t, "HTTP ");
        put_dec(t, net->http_status);
    } else {
        put_hex(t, (u32)net->result);
    }
}

static void check_updates(menu *m, u32 channel)
{
    set_status(m, "Checking for updates...", 0);
    draw(m);

    update_check *r = &last_check;
    update_check_run(r, channel, m->code);
    m->view = VIEW_MAIN;

    text t = { status_buf, 0 };
    text t2 = { status2_buf, 0 };
    switch (r->outcome) {
    case UPDATE_CURRENT:
        put_str(&t, "Up to date");
        put_str(&t2, "Server's build: ");
        put_str(&t2, r->identity);
        break;
    case UPDATE_AVAILABLE:
        if (channel == CHANNEL_DIRTY)
            put_str(&t, "Dirty build available: ");
        else if (SALTYSD_IS_DIRTY)
            put_str(&t, "Leave dirty: install ");
        else
            put_str(&t, "Update available: ");
        put_str(&t, r->identity);
        put_str(&t2, r->file);
        put_str(&t2, " (");
        put_dec(&t2, r->file_size);
        put_str(&t2, " bytes)");
        m->view = VIEW_OFFER;
        break;
    case UPDATE_NET_FAILED:
        if (r->net.stage == NET_NO_WIFI) {
            put_str(&t, "No Wi-Fi connection");
            break;
        }
        put_str(&t, r->net_file ? "manifest.sig: " : "manifest.txt: ");
        put_net_stage(&t, &r->net);
        put_net_detail(&t2, &r->net);
        break;
    case UPDATE_CODE_REJECTED:
        put_str(&t, "Invalid code");
        break;
    case UPDATE_BAD_FORMAT:
        put_str(&t, "Manifest not understood");
        break;
    case UPDATE_UNKNOWN_KEY:
        put_str(&t, "Manifest key ");
        put_dec(&t, r->key_id);
        put_str(&t, " is unknown");
        break;
    case UPDATE_KEY_NOT_ALLOWED:
        put_str(&t, "Key ");
        put_dec(&t, r->key_id);
        put_str(&t, " may not sign this channel");
        break;
    case UPDATE_WRONG_CHANNEL:
        put_str(&t, "Invalid channel");
        break;
    case UPDATE_BAD_SIGNATURE:
        put_str(&t, "Invalid update. Aborting install.");
        break;
    default:
        put_str(&t, "No update for your region");
        break;
    }
    set_status(m, status_buf, t2.len ? status2_buf : 0);
}

typedef struct {
    menu *m;
    u32 shown;
} progress_state;

static void show_progress(void *ctx, u32 done, u32 total)
{
    progress_state *p = ctx;
    if (done != total && done - p->shown < PROGRESS_STEP)
        return;
    p->shown = done;

    text t = { status_buf, 0 };
    put_str(&t, "Downloading ");
    put_dec(&t, done);
    put_str(&t, " / ");
    put_dec(&t, total);
    set_status(p->m, status_buf, 0);
    draw(p->m);
}

static const char *const install_stage_names[] = {
    "done", "plugin path", "plugin path shape", "SD access", "create .new", "download", "write",
    "size", "hash", "read-back", "backup", "swap (old file restored)", "swap (NOT restored)",
};

static void install_update(menu *m)
{
    progress_state p = { m, 0 };
    update_install_result r;
    m->view = VIEW_BUSY;
    if (update_install(&last_check, &r, show_progress, &p)) {
        restart(m);
        m->view = VIEW_MAIN;
        return;
    }

    m->view = VIEW_MAIN;
    text t = { status_buf, 0 };
    text t2 = { status2_buf, 0 };
    put_str(&t, "Install failed: ");
    put_str(&t, r.stage < sizeof(install_stage_names) / sizeof(install_stage_names[0]) ?
                install_stage_names[r.stage] : "?");
    if (r.stage == INSTALL_DOWNLOAD) {
        put_net_stage(&t2, &r.net);
        put_str(&t2, " ");
        put_net_detail(&t2, &r.net);
    } else {
        put_hex(&t2, (u32)r.result);
    }
    set_status(m, status_buf, status2_buf);
}

#if SALTYSD_IS_DIRTY
static void run_gate(menu *m)
{
    if (update_gate(&last_check) != GATE_EXPIRED)
        return;
    saltysd_status.gate_expired++;

    set_status(m, "Dirty access expired. Installing stable...", 0);
    m->view = VIEW_BUSY;
    draw(m);
    update_check_run(&last_check, CHANNEL_STABLE, 0);
    m->view = VIEW_MAIN;
    if (last_check.outcome != UPDATE_AVAILABLE) {
        set_status(m, "Dirty access expired; stable not reachable", 0);
        return;
    }
    install_update(m);
}
#endif

static void move_mod_cursor(menu *m, int down)
{
    if (!mods_count)
        return;
    if (down)
        m->mod_cursor = m->mod_cursor + 1 == mods_count ? 0 : m->mod_cursor + 1;
    else
        m->mod_cursor = m->mod_cursor ? m->mod_cursor - 1 : mods_count - 1;

    if (m->mod_cursor < m->mod_first)
        m->mod_first = m->mod_cursor;
    else if (m->mod_cursor >= m->mod_first + MOD_ROWS)
        m->mod_first = m->mod_cursor - MOD_ROWS + 1;
}

static u32 press_main(menu *m, u32 pressed)
{
    if (pressed & KEY_B)
        return EXIT_BACK;
    if (pressed & KEY_UP)
        m->cursor = m->cursor ? m->cursor - 1 : ITEM_COUNT - 1;
    if (pressed & KEY_DOWN)
        m->cursor = (m->cursor + 1) % ITEM_COUNT;
    if (!(pressed & KEY_A))
        return EXIT_NONE;

    switch (m->cursor) {
    case ITEM_MODS:
        open_mods(m);
        return EXIT_NONE;
    case ITEM_REBUILD:
        m->view = VIEW_REBUILD;
        set_status(m, 0, 0);
        return EXIT_NONE;
    case ITEM_STABLE:
        check_updates(m, CHANNEL_STABLE);
        return EXIT_NONE;
    case ITEM_DIRTY:
        update_load_code(m->code);
        m->code_pos = 0;
        m->view = VIEW_CODE;
        set_status(m, 0, 0);
        return EXIT_NONE;
    default:
        return EXIT_BACK;
    }
}

static u32 press_mods(menu *m, u32 pressed)
{
    if (pressed & KEY_B) {
        fs_close();
        m->view = VIEW_MAIN;
        set_status(m, 0, 0);
        return EXIT_NONE;
    }
    if (pressed & KEY_UP)
        move_mod_cursor(m, 0);
    if (pressed & KEY_DOWN)
        move_mod_cursor(m, 1);
    if ((pressed & KEY_A) && mods_count) {
        mods[m->mod_cursor].wanted ^= 1;
        set_status(m, 0, 0);
    }
    if (pressed & KEY_START) {
        if (mods_changes())
            m->view = VIEW_CONFIRM;
        else
            set_status(m, "Nothing to apply: no mods changed.", 0);
    }
    return EXIT_NONE;
}

static u32 press_confirm(menu *m, u32 pressed)
{
    if (pressed & KEY_A) {
        m->view = VIEW_MODS;
        apply_mods(m);
    } else if (pressed & KEY_B) {
        m->view = VIEW_MODS;
    }
    return EXIT_NONE;
}

//A failed restart leaves the busy view up; any press returns to the menu,
//keeping the error on screen.
static u32 press_busy(menu *m, u32 pressed)
{
    (void)pressed;
    m->view = VIEW_MAIN;
    return EXIT_NONE;
}

static u32 press_code(menu *m, u32 pressed)
{
    char *d = &m->code[m->code_pos];
    if (pressed & KEY_UP)
        *d = *d == '9' ? '0' : *d + 1;
    if (pressed & KEY_DOWN)
        *d = *d == '0' ? '9' : *d - 1;
    if (pressed & KEY_LEFT)
        m->code_pos = m->code_pos ? m->code_pos - 1 : 3;
    if (pressed & KEY_RIGHT)
        m->code_pos = (m->code_pos + 1) % 4;
    if (pressed & KEY_B) {
        m->view = VIEW_MAIN;
        set_status(m, 0, 0);
    } else if (pressed & KEY_A) {
        check_updates(m, CHANNEL_DIRTY);
    }
    return EXIT_NONE;
}

static u32 press_offer(menu *m, u32 pressed)
{
    if (pressed & KEY_A)
        m->view = VIEW_INSTALL_CONFIRM;
    else if (pressed & KEY_B)
        m->view = VIEW_MAIN;
    return EXIT_NONE;
}

static u32 press_install_confirm(menu *m, u32 pressed)
{
    if (pressed & KEY_A)
        install_update(m);
    else if (pressed & KEY_B)
        m->view = VIEW_OFFER;
    return EXIT_NONE;
}

static void hint_rebuild(u8 *fb, const screen *s, const menu *m)
{
    (void)m;
    draw_text(fb, s, 8, 8, "Rebuild cache and restart?", TEXT_RGB);
    draw_text(fb, s, 8, 20, "The next boot might take several minutes", DIM_RGB);
    draw_text(fb, s, 8, 32, "A: Yes  B: No", CURSOR_RGB);
}

static void rebuild_index(menu *m)
{
    int res = fs_open();
    if (res < 0) {
        fs_failed(m, "SD access", res);
        return;
    }

    res = mods_drop_index();
    if (res < 0) {
        fs_close();
        fs_failed(m, "Removing the index", res);
        return;
    }

    restart(m);
}

static u32 press_rebuild(menu *m, u32 pressed)
{
    if (pressed & KEY_A) {
        m->view = VIEW_MAIN;
        rebuild_index(m);
    } else if (pressed & KEY_B) {
        m->view = VIEW_MAIN;
    }
    return EXIT_NONE;
}

typedef struct {
    void (*top)(u8 *fb, const screen *s, const menu *m);
    void (*bottom)(u8 *fb, const screen *s, const menu *m);
    u32  (*press)(menu *m, u32 pressed);
} view_def;

static const view_def views[VIEW_COUNT] = {
    [VIEW_MAIN]            = { paint_main, hint_main,            press_main },
    [VIEW_MODS]            = { paint_mods, hint_mods,            press_mods },
    [VIEW_CONFIRM]         = { paint_mods, hint_confirm,         press_confirm },
    [VIEW_BUSY]            = { paint_busy, 0,                    press_busy },
    [VIEW_CODE]            = { paint_code, hint_code,            press_code },
    [VIEW_OFFER]           = { paint_main, hint_offer,           press_offer },
    [VIEW_INSTALL_CONFIRM] = { paint_main, hint_install_confirm, press_install_confirm },
    [VIEW_REBUILD]         = { paint_main, hint_rebuild,         press_rebuild },
};

static void paint_top(u8 *fb, const menu *m)
{
    const screen *s = &m->top;
    clear(fb, s, BG_RGB);
    views[m->view].top(fb, s, m);
}

static void paint_bottom(u8 *fb, const menu *m)
{
    const screen *s = &m->bottom;
    clear(fb, s, BG_RGB);
    if (views[m->view].bottom)
        views[m->view].bottom(fb, s, m);
    if (m->status)
        draw_text(fb, s, 8, 68, m->status, TEXT_RGB);
    if (m->status2)
        draw_text(fb, s, 8, 80, m->status2, TEXT_RGB);
}

static void draw(const menu *m)
{
    if (m->has_top) {
        paint_top(m->top.left, m);
        if (m->top.right)
            paint_top(m->top.right, m);
    }
    if (m->has_bottom)
        paint_bottom(m->bottom.left, m);
    flush_data_cache();
}

static u32 run_loop(menu *m)
{
    u32 held = input_held();
    u32 polls = 0;
    int dirty = 1;

    for (;;) {
        u32 pending = applet_pending();
        if (pending)
            return pending;

        u32 now = input_held();
        u32 pressed = now & ~held;
        held = now;

        if (m->view >= VIEW_COUNT)
            m->view = VIEW_MAIN;

        if (pressed) {
            u32 reason = views[m->view].press(m, pressed);
            if (reason)
                return reason;
            dirty = 1;
        }

        //A queued GPU transfer can still land after the first draw.
        if (polls++ % REDRAW_POLLS == 0 || dirty) {
            draw(m);
            dirty = 0;
        }
        sleep_ns(POLL_NS);
    }
}

static void make_title(void)
{
    text t = { title_buf, 0 };
    put_str(&t, "SaltySD ");
    put_str(&t, SALTYSD_IDENTITY);
#ifdef SALTYSD_UPDATE_TEST
    put_str(&t, "  TEST BUILD");
#endif
}

void host_menu_run(void)
{
    u32 client = *(u32 *)(gsp_state_ADDR + GSP_CLIENT_OFFS);
    if (!client || !input_open())
        return;

    menu m;
    m.view = VIEW_MAIN;
    m.cursor = ITEM_MODS;
    m.mod_cursor = m.mod_first = 0;
    m.code[0] = m.code[1] = m.code[2] = m.code[3] = '0';
    m.code_pos = 0;
    m.status = m.status2 = 0;
    m.has_top = find_screen(&m.top, *(u32 *)(client + GSP_TOP_INFO_OFFS), TOP_WIDTH);
    m.has_bottom = find_screen(&m.bottom, *(u32 *)(client + GSP_BOTTOM_INFO_OFFS), BOTTOM_WIDTH);
    if (!m.has_top && !m.has_bottom)
        return;
    make_title();

    saltysd_status.menu_top_fb = m.has_top ? (u32)m.top.left : 0;
    saltysd_status.menu_top_fb_right = m.has_top ? (u32)m.top.right : 0;
    saltysd_status.menu_bottom_fb = m.has_bottom ? (u32)m.bottom.left : 0;
    saltysd_status.menu_formats = (m.has_top ? m.top.format : 0xFF) << 8 |
                                  (m.has_bottom ? m.bottom.format : 0xFF);

#if SALTYSD_IS_DIRTY
    run_gate(&m);
#endif

    u32 reason = run_loop(&m);
    fs_close();
    saltysd_status.menu_exit_reason = reason;
    if (reason != EXIT_BACK)
        return;

    //A button still held here would reach the main menu.
    while (input_held() & (KEY_A | KEY_B))
        sleep_ns(POLL_NS);
}
