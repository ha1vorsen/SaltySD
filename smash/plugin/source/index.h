#ifndef SALTYSD_INDEX_H
#define SALTYSD_INDEX_H

#include "types.h"

#define SALTYSD_INDEX_PATH "sd:/saltysd/.saltysd-"
#define SALTYSD_INDEX_MAGIC 0x58444953u /* 'SIDX' */

typedef struct __attribute__((__packed__)) {
    u32 id;
    u32 size;
    u8 root;
    u8 pad[3];
} idx_override;

typedef struct __attribute__((__packed__)) {
    u32 tree_offset;
    u32 text;
} idx_string;

typedef struct __attribute__((__packed__)) {
    u32 slot;
    u32 tree_offset;
} idx_ext;

typedef struct __attribute__((__packed__)) {
    u32 pos;
    u32 string_offs;
    u32 size;
    u32 flags;
    u8 root;
    u8 pad[3];
} idx_insert;

typedef struct __attribute__((__packed__)) {
    u32 text;
    u8 root;
    u8 pad[3];
} idx_named;

typedef struct __attribute__((__packed__)) {
    u32 magic;
    u32 total_size;

    u32 build;         /* fnv1a over SALTYSD_IDENTITY */
    u32 title;         /* SALTYSD_TITLE_ID */
    u32 mods;          /* fnv1a over every mod folder and its state */
    u32 entry_reserve; /* every offset below assumes this prologue */
    u32 tree_entries;  /* resourceentry_amt before SaltySD touched it */
    u32 tree_entry_size;
    u32 tree_string_size;
    u32 tree_timestamp; /* the game's own field, not the published map */

    u32 num_roots; /* u32 text offsets, the loose root first */
    u32 roots_off;
    u32 num_revokes; /* u32 entry ids */
    u32 revokes_off;
    u32 num_overrides; /* idx_override */
    u32 overrides_off;
    u32 num_strings; /* idx_string */
    u32 strings_off;
    u32 num_exts; /* idx_ext */
    u32 exts_off;
    u32 num_inserts; /* idx_insert, ascending by pos */
    u32 inserts_off;
    u32 num_named; /* idx_named */
    u32 named_off;

    u32 text_off;
    u32 text_size;
} idx_header;

#endif
