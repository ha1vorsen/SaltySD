#include "update.h"
#include "update_keys.h"
#include "version.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "fs.h"
#include "plgldr.h"
#include "status.h"

#define CODE_SUFFIX ".code"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#ifdef SALTYSD_UPDATE_TEST
#define UPDATE_ROOT "http://127.0.0.1:8123/"
#else
#define UPDATE_ROOT "http://cdn.wavedash.ing/ctr/cross_c/"
#endif

#define MANIFEST_MAX 4096
#define SIG_SIZE     64
#define PLUGIN_MAX   0x100000
#define COPY_CHUNK   0x1000
#define HTTP_NOT_FOUND 404
#define PATH_REJECTED  ((int)0xE0000002)

static char manifest[MANIFEST_MAX + 1];
static u8 signature[SIG_SIZE + 1];

typedef struct {
    const char *at;
    const char *end;
} cursor;

static int is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static int next_line(cursor *c, cursor *line)
{
    if (c->at >= c->end)
        return 0;
    line->at = c->at;
    while (c->at < c->end && *c->at != '\n')
        c->at++;
    line->end = c->at;
    while (line->end > line->at && is_space(line->end[-1]))
        line->end--;
    if (c->at < c->end)
        c->at++;
    return 1;
}

static int next_word(cursor *line, cursor *word)
{
    while (line->at < line->end && is_space(*line->at))
        line->at++;
    if (line->at >= line->end)
        return 0;
    word->at = line->at;
    while (line->at < line->end && !is_space(*line->at))
        line->at++;
    word->end = line->at;
    return 1;
}

static int word_is(const cursor *w, const char *s)
{
    const char *p = w->at;
    while (*s && p < w->end && *p == *s) {
        p++;
        s++;
    }
    return !*s && p == w->end;
}

static int parse_dec(const cursor *w, u32 *out)
{
    u32 v = 0;
    if (w->at == w->end || w->end - w->at > 9)
        return 0;
    for (const char *p = w->at; p < w->end; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        v = v * 10 + (u32)(*p - '0');
    }
    *out = v;
    return 1;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_hex32(const char *p, const char *end, u32 *out)
{
    u32 v = 0;
    if (end - p != 8)
        return 0;
    for (; p < end; p++) {
        int d = hex_digit(*p);
        if (d < 0)
            return 0;
        v = v << 4 | (u32)d;
    }
    *out = v;
    return 1;
}

static int parse_hash(const cursor *w, u8 out[64])
{
    if (w->end - w->at != 128)
        return 0;
    for (u32 i = 0; i < 64; i++) {
        int hi = hex_digit(w->at[i * 2]), lo = hex_digit(w->at[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        out[i] = (u8)(hi << 4 | lo);
    }
    return 1;
}

static int name_ok(const cursor *w)
{
    for (const char *p = w->at; p < w->end; p++) {
        char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.' || c == '-'))
            return 0;
    }
    return w->at[0] != '.';
}

static int tid_listed(const cursor *w, u32 tid)
{
    const char *p = w->at;
    while (p < w->end) {
        const char *comma = p;
        while (comma < w->end && *comma != ',')
            comma++;
        u32 v;
        if (!parse_hex32(p, comma, &v))
            return 0;
        if (v == tid)
            return 1;
        p = comma + 1;
    }
    return 0;
}

static const update_key *find_key(u32 len, u32 *id)
{
    cursor c = { manifest, manifest + len }, line, word, value;
    *id = 0;
    while (next_line(&c, &line)) {
        if (next_word(&line, &word) && word_is(&word, "key") &&
            next_word(&line, &value) && parse_dec(&value, id)) {
            for (const update_key *k = update_keys; k->id; k++)
                if (k->id == *id)
                    return k;
            return 0;
        }
    }
    return 0;
}

static int parse_version(const char *p, const char *end, u32 v[3])
{
    for (u32 i = 0; i < 3; i++) {
        const char *dot = p;
        while (dot < end && *dot != '.')
            dot++;
        cursor part = { p, dot };
        if (!parse_dec(&part, &v[i]) || (i < 2 && dot == end) || (i == 2 && dot != end))
            return 0;
        p = dot + 1;
    }
    return 1;
}

static int commit_ok(const cursor *w)
{
    if (w->at == w->end || w->end - w->at > 40)
        return 0;
    for (const char *p = w->at; p < w->end; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || *p == '-'))
            return 0;
    return 1;
}

static int same_text(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void copy_word(char *out, const cursor *w)
{
    u32 n = (u32)(w->end - w->at);
    for (u32 i = 0; i < n; i++)
        out[i] = w->at[i];
    out[n] = 0;
}

static u32 parse(update_check *out, u32 len)
{
    cursor c = { manifest, manifest + len }, line, word;
    int have_identity = 0, have_channel = 0, have_file = 0;
    u32 server_version[3];

    if (!next_line(&c, &line) || !word_is(&line, "saltysd-manifest 1"))
        return UPDATE_BAD_FORMAT;

    while (next_line(&c, &line)) {
        if (!next_word(&line, &word))
            continue;
        if (word_is(&word, "version")) {
            cursor v;
            if (out->channel != CHANNEL_STABLE || !next_word(&line, &v) || v.end - v.at > 20 ||
                !parse_version(v.at, v.end, server_version))
                return UPDATE_BAD_FORMAT;
            copy_word(out->identity, &v);
            have_identity = 1;
        } else if (word_is(&word, "commit")) {
            cursor v;
            if (out->channel != CHANNEL_DIRTY || !next_word(&line, &v) || !commit_ok(&v))
                return UPDATE_BAD_FORMAT;
            copy_word(out->identity, &v);
            have_identity = 1;
        } else if (word_is(&word, "channel")) {
            cursor v;
            if (!next_word(&line, &v))
                return UPDATE_BAD_FORMAT;
            if (!word_is(&v, out->channel == CHANNEL_DIRTY ? "dirty" : "stable"))
                return UPDATE_WRONG_CHANNEL;
            have_channel = 1;
        } else if (word_is(&word, "file")) {
            cursor tids, name, size, hash;
            if (!next_word(&line, &tids) || !next_word(&line, &name) ||
                !next_word(&line, &size) || !next_word(&line, &hash))
                return UPDATE_BAD_FORMAT;
            if (!tid_listed(&tids, SALTYSD_TITLE_ID))
                continue;
            u32 n = (u32)(name.end - name.at);
            if (n == 0 || n >= sizeof(out->file) || !name_ok(&name) ||
                !parse_dec(&size, &out->file_size) || out->file_size == 0 ||
                out->file_size > PLUGIN_MAX || !parse_hash(&hash, out->file_hash))
                return UPDATE_BAD_FORMAT;
            copy_word(out->file, &name);
            have_file = 1;
        }
    }

    if (!have_identity || !have_channel)
        return UPDATE_BAD_FORMAT;

    int newer;
    if (out->channel == CHANNEL_STABLE) {
#if SALTYSD_IS_DIRTY
        newer = 1;
#else
        u32 own[3];
        const char *v = SALTYSD_VERSION;
        const char *end = v;
        while (*end)
            end++;
        parse_version(v, end, own);
        newer = server_version[0] != own[0] ? server_version[0] > own[0] :
                server_version[1] != own[1] ? server_version[1] > own[1] :
                server_version[2] > own[2];
#endif
    } else {
        newer = !SALTYSD_IS_DIRTY || !same_text(out->identity, SALTYSD_IDENTITY);
    }
    if (!newer)
        return UPDATE_CURRENT;
    return have_file ? UPDATE_AVAILABLE : UPDATE_NO_FILE;
}

static void build_base(update_check *out, const char code[4])
{
    const char *root = UPDATE_ROOT;
    const char *dir = out->channel == CHANNEL_DIRTY ? "dirty/" : "stable/";
    u32 n = 0;
    while (*root)
        out->base[n++] = *root++;
    while (*dir)
        out->base[n++] = *dir++;
    if (out->channel == CHANNEL_DIRTY) {
        for (u32 i = 0; i < 4; i++)
            out->base[n++] = code[i];
        out->base[n++] = '/';
    }
    out->base[n] = 0;
}

static char url[128];

static const char *make_url(const char *base, const char *name)
{
    u32 n = 0;
    while (*base && n < sizeof(url) - 1)
        url[n++] = *base++;
    while (*name && n < sizeof(url) - 1)
        url[n++] = *name++;
    url[n] = 0;
    return url;
}

static u32 check(update_check *out)
{
    net_get(&out->net, make_url(out->base, "manifest.txt"), manifest, MANIFEST_MAX);
    if (out->net.stage != NET_OK) {
        if (out->channel == CHANNEL_DIRTY && out->net.http_status == HTTP_NOT_FOUND)
            return UPDATE_CODE_REJECTED;
        return UPDATE_NET_FAILED;
    }
    u32 len = out->net.size;

    out->net_file = 1;
    net_get(&out->net, make_url(out->base, "manifest.sig"), signature, sizeof(signature));
    if (out->net.stage != NET_OK)
        return UPDATE_NET_FAILED;
    if (out->net.size != SIG_SIZE)
        return UPDATE_BAD_SIGNATURE;

    const update_key *key = find_key(len, &out->key_id);
    if (!key)
        return UPDATE_UNKNOWN_KEY;
    if (crypto_ed25519_check(signature, key->key, (const u8 *)manifest, len))
        return UPDATE_BAD_SIGNATURE;
    if (!(key->channels & (out->channel == CHANNEL_DIRTY ? KEY_DIRTY : KEY_STABLE)))
        return UPDATE_KEY_NOT_ALLOWED;

    return parse(out, len);
}

static int verified(u32 outcome)
{
    return outcome == UPDATE_CURRENT || outcome == UPDATE_AVAILABLE || outcome == UPDATE_NO_FILE;
}

static int save_code(const char code[4]);

void update_check_run(update_check *out, u32 channel, const char code[4])
{
    out->channel = channel;
    for (u32 i = 0; i < 4; i++)
        out->code[i] = code ? code[i] : '0';
    out->net_file = 0;
    out->key_id = 0;
    out->identity[0] = 0;
    out->file[0] = 0;
    out->file_size = 0;
    build_base(out, out->code);

    out->outcome = check(out);
    if (channel == CHANNEL_DIRTY && verified(out->outcome))
        save_code(out->code);
    saltysd_status.update_outcome = out->outcome;
    saltysd_status.update_net = out->net.stage << 24 | out->net_file << 16 | (out->net.http_status & 0xFFFF);
    saltysd_status.update_result = out->net.result;
}

static char plugin_path[PLUGIN_PATH_MAX];
static u16 path_now[PLUGIN_PATH_MAX];
static u16 path_new[PLUGIN_PATH_MAX + 4];
static u16 path_bak[PLUGIN_PATH_MAX + 4];
static u8 copy_buf[COPY_CHUNK];

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++)
            return 0;
    return 1;
}

static int path_ok(const char *p)
{
    u32 n = 0;
    while (p[n]) {
        if (p[n] < 0x20 || p[n] > 0x7E)
            return 0;
        n++;
    }
    if (n < 5 || n > 200 || !starts_with(p, "/luma/plugins/") || !starts_with(p + n - 4, ".3gx"))
        return 0;

    const char *rest = p + 14;
    if (starts_with(rest, "default.3gx") && !rest[11])
        return 1;
    for (u32 i = 0; i < 16; i++)
        if (hex_digit(rest[i]) < 0)
            return 0;
    if (rest[16] != '/')
        return 0;
    for (const char *q = rest + 17; *q; q++)
        if (*q == '/')
            return 0;
    return rest[17] != 0;
}

static void with_suffix(u16 *out, const u16 *path, const char *suffix)
{
    u32 n = 0;
    while (path[n]) {
        out[n] = path[n];
        n++;
    }
    while (*suffix)
        out[n++] = (u8)*suffix++;
    out[n] = 0;
}

typedef struct {
    u32 file;
    u32 written;
    int write_result;
    crypto_sha512_ctx hash;
    update_progress progress;
    void *ctx;
    u32 total;
} download;

static int to_file(void *ctx, const void *data, u32 size)
{
    download *d = ctx;
    int res = fs_file_write(d->file, d->written, data, size);
    if (res < 0) {
        d->write_result = res;
        return 0;
    }
    crypto_sha512_update(&d->hash, data, size);
    d->written += size;
    if (d->progress)
        d->progress(d->ctx, d->written, d->total);
    return 1;
}

static int fail(update_install_result *out, u32 stage, int res)
{
    out->stage = stage;
    out->result = res;
    return 0;
}

static int readback_ok(const update_check *check, update_install_result *out)
{
    u32 file;
    int res = fs_file_open_read(&file, path_new);
    if (res < 0)
        return fail(out, INSTALL_READBACK, res);

    crypto_sha512_ctx hash;
    crypto_sha512_init(&hash);
    u32 offset = 0, read;
    do {
        res = fs_file_read(file, offset, copy_buf, COPY_CHUNK, &read);
        if (res < 0)
            break;
        crypto_sha512_update(&hash, copy_buf, read);
        offset += read;
    } while (read == COPY_CHUNK && offset <= check->file_size);
    fs_file_close(file);
    if (res < 0)
        return fail(out, INSTALL_READBACK, res);

    u8 got[64];
    crypto_sha512_final(&hash, got);
    if (offset != check->file_size || crypto_verify64(got, check->file_hash))
        return fail(out, INSTALL_READBACK, 0);
    return 1;
}

static u16 path_code[PLUGIN_PATH_MAX + 8];

static int resolve_paths(void)
{
    int res = plgldr_plugin_path(plugin_path);
    if (res < 0)
        return res;
    if (!path_ok(plugin_path))
        return PATH_REJECTED;
    fs_path_from_ascii(path_now, PLUGIN_PATH_MAX, plugin_path);
    with_suffix(path_new, path_now, ".new");
    with_suffix(path_bak, path_now, ".bak");
    with_suffix(path_code, path_now, CODE_SUFFIX);
    return 0;
}

static int write_code(const char code[4])
{
    fs_file_delete(path_code);
    u32 file;
    int res = fs_file_create_write(&file, path_code);
    if (res < 0)
        return res;
    res = fs_file_write(file, 0, code, 4);
    fs_file_close(file);
    return res;
}

static int read_code(char code[4])
{
    char got[4];
    u32 file, read = 0;
    int ok = fs_file_open_read(&file, path_code) >= 0;
    if (ok) {
        ok = fs_file_read(file, 0, got, 4, &read) >= 0 && read == 4;
        fs_file_close(file);
    }
    for (u32 i = 0; ok && i < 4; i++)
        ok = got[i] >= '0' && got[i] <= '9';
    for (u32 i = 0; ok && i < 4; i++)
        code[i] = got[i];
    return ok;
}

static int save_code(const char code[4])
{
    int res = resolve_paths();
    if (res < 0)
        return res;
    res = fs_open();
    if (res >= 0) {
        char old[4];
        if (!read_code(old) || old[0] != code[0] || old[1] != code[1] || old[2] != code[2] || old[3] != code[3])
            res = write_code(code);
    }
    fs_close();
    return res;
}

int update_load_code(char code[4])
{
    int ok = resolve_paths() >= 0 && fs_open() >= 0 && read_code(code);
    fs_close();
    return ok;
}

u32 update_gate(update_check *out)
{
    char code[4];
    if (!update_load_code(code))
        return GATE_EXPIRED;
    update_check_run(out, CHANNEL_DIRTY, code);
    if (out->outcome == UPDATE_CODE_REJECTED)
        return GATE_EXPIRED;
    return verified(out->outcome) ? GATE_PASS : GATE_ERROR;
}

static int install(const update_check *check, update_install_result *out, update_progress progress, void *ctx)
{
    int res = resolve_paths();
    if (res == PATH_REJECTED)
        return fail(out, INSTALL_ODD_PATH, 0);
    if (res < 0)
        return fail(out, INSTALL_NO_PATH, res);

    res = fs_open();
    if (res < 0)
        return fail(out, INSTALL_SD, res);

    fs_file_delete(path_new);
    download d = { 0, 0, 0, { { 0 } }, progress, ctx, check->file_size };
    res = fs_file_create_write(&d.file, path_new);
    if (res < 0)
        return fail(out, INSTALL_CREATE, res);

    crypto_sha512_init(&d.hash);
    net_fetch(&out->net, make_url(check->base, check->file), to_file, &d, check->file_size);
    fs_file_close(d.file);

    u8 got[64];
    crypto_sha512_final(&d.hash, got);
    int ok = 0;
    if (d.write_result < 0)
        fail(out, INSTALL_WRITE, d.write_result);
    else if (out->net.stage != NET_OK)
        fail(out, INSTALL_DOWNLOAD, out->net.result);
    else if (d.written != check->file_size)
        fail(out, INSTALL_SIZE, (int)d.written);
    else if (crypto_verify64(got, check->file_hash))
        fail(out, INSTALL_HASH, 0);
    else
        ok = readback_ok(check, out);

    if (!ok) {
        fs_file_delete(path_new);
        return 0;
    }

    fs_file_delete(path_bak);
    res = fs_file_rename(path_now, path_bak);
    if (res < 0) {
        fs_file_delete(path_new);
        return fail(out, INSTALL_BACKUP, res);
    }
    res = fs_file_rename(path_new, path_now);
    if (res < 0) {
        int back = fs_file_rename(path_bak, path_now);
        return fail(out, back < 0 ? INSTALL_SWAP_STUCK : INSTALL_SWAP, res);
    }

    if (check->channel == CHANNEL_DIRTY)
        write_code(check->code);

    out->stage = INSTALL_DONE;
    return 1;
}

int update_install(const update_check *check, update_install_result *out, update_progress progress, void *ctx)
{
    out->stage = INSTALL_DONE;
    out->result = 0;
    out->net.stage = NET_OK;
    out->net.result = 0;
    out->net.http_status = 0;
    out->net.size = 0;

    int done = install(check, out, progress, ctx);
    fs_close();
    saltysd_status.install_stage = out->stage;
    saltysd_status.install_result = out->result;
    return done;
}
