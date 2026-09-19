#ifndef SALTYSD_FS_H
#define SALTYSD_FS_H

#define FS_NAME_CHARS    0x106
#define FS_ATTR_DIR      1
#define FS_SHORT_WRITE   ((int)0xE0000001)

typedef struct {
    unsigned short name[FS_NAME_CHARS];
    char short_name[0x0A];
    char short_ext[4];
    unsigned char valid;
    unsigned char reserved;
    unsigned int attributes;
    unsigned long long size;
} fs_entry;

int fs_open(void);
void fs_close(void);

int fs_dir_open(unsigned int *dir, const unsigned short *path);
int fs_dir_read(unsigned int dir, fs_entry *entries, unsigned int max, unsigned int *read);
void fs_dir_close(unsigned int dir);

int fs_file_exists(const unsigned short *path);
int fs_file_create_empty(const unsigned short *path);
int fs_file_delete(const unsigned short *path);
int fs_file_rename(const unsigned short *from, const unsigned short *to);

unsigned int fs_path_from_ascii(unsigned short *out, unsigned int max, const char *s);

int fs_file_open_read(unsigned int *file, const unsigned short *path);
int fs_file_create_write(unsigned int *file, const unsigned short *path);
int fs_file_read(unsigned int file, unsigned int offset, void *buf, unsigned int size, unsigned int *read);
int fs_file_write(unsigned int file, unsigned int offset, const void *buf, unsigned int size);
void fs_file_close(unsigned int file);

#endif
