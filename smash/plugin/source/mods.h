#ifndef SALTYSD_MODS_H
#define SALTYSD_MODS_H

#define MODS_MAX       62
#define MOD_NAME_CHARS 0x40

typedef struct {
    unsigned short name[MOD_NAME_CHARS];
    unsigned char enabled;
    unsigned char wanted;
} mod_entry;

typedef struct {
    unsigned int applied;
    unsigned int failed;
    int first_error;
    const mod_entry *first_failed;
} mods_apply_result;

extern mod_entry mods[MODS_MAX];
extern unsigned int mods_count;
extern unsigned int mods_skipped;

int mods_load(void);
unsigned int mods_changes(void);
void mods_apply(mods_apply_result *out);

#endif
