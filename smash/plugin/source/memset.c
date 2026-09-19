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
