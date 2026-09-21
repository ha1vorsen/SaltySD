//GCC emits memset calls and there is no libc.

typedef unsigned int size_t;

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memset(void *dest, int c, size_t n)
{
    unsigned char *p = dest;
    while (n--)
        *p++ = (unsigned char)c;
    return dest;
}

__attribute__((optimize("no-tree-loop-distribute-patterns")))
void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dest;
}
