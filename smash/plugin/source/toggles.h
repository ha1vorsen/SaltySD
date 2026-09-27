#ifndef SALTYSD_TOGGLES_H
#define SALTYSD_TOGGLES_H

#define TOGGLE_NAME_CHARS 0x40
#define TOGGLE_MARKER     "is.disabled"

typedef struct {
    unsigned short name[TOGGLE_NAME_CHARS];
    unsigned char enabled;
    unsigned char wanted;
} toggle_entry;

typedef struct {
    const char *root;
    toggle_entry *entries;
    unsigned int max;
    unsigned int count;
    unsigned int skipped;
} toggle_list;

typedef struct {
    unsigned int applied;
    unsigned int failed;
    int first_error;
    const toggle_entry *first_failed;
} toggles_apply_result;

int toggles_load(toggle_list *list);
unsigned int toggles_changes(const toggle_list *list);
void toggles_apply(toggle_list *list, toggles_apply_result *out);
unsigned int toggles_folder_path(const toggle_list *list, const toggle_entry *entry,
                                 unsigned short *out, unsigned int max);

#endif
