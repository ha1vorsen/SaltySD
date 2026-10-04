#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "texture_filter.h"

static void write_u32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static unsigned read_u32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
           ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void make_bch(unsigned char *bch, unsigned size)
{
    unsigned index;

    memset(bch, 0, size);
    bch[0] = 'B';
    bch[1] = 'C';
    bch[2] = 'H';
    write_u32(bch + 0x08, 0x44);
    write_u32(bch + 0x10, 0x100);
    write_u32(bch + 0x14, 0x180);
    write_u32(bch + 0x44 + 0x24, 0x80);
    write_u32(bch + 0x44 + 0x28, 1);
    write_u32(bch + 0xc4, 0xa0);
    write_u32(bch + 0xe4, 0);
    write_u32(bch + 0xe8, 12);

    write_u32(bch + 0x100, 0x00400040);
    write_u32(bch + 0x104, 0x000f0082);
    write_u32(bch + 0x108, 0);
    write_u32(bch + 0x10c, 0x00040084);
    write_u32(bch + 0x110, 0);
    write_u32(bch + 0x114, 0x000f0085);
    write_u32(bch + 0x118, 0x0d);
    write_u32(bch + 0x11c, 0x000f008e);
    write_u32(bch + 0x128, 1);
    write_u32(bch + 0x12c, 0x000f023d);
    for (index = 0x180; index < size; ++index)
        bch[index] = 0xaa;
}

static void test_corpus_file(const char *path)
{
    FILE *input = fopen(path, "rb");
    unsigned char *data;
    long length;
    unsigned main_offset;
    unsigned texture_count;

    assert(input);
    assert(fseek(input, 0, SEEK_END) == 0);
    length = ftell(input);
    assert(length > 0 && (unsigned long)length <= 0xfffffffful);
    assert(fseek(input, 0, SEEK_SET) == 0);
    data = (unsigned char *)malloc((size_t)length);
    assert(data);
    assert(fread(data, 1, (size_t)length, input) == (size_t)length);
    assert(fclose(input) == 0);
    main_offset = read_u32(data + 0x08);
    texture_count = read_u32(data + main_offset + 0x28);
    assert(texture_count > 0);
    assert(suppress_c08_h08_bch_textures(
               "model/fighter/test/body/c08/normal.bch", data,
               (unsigned)length) == (int)texture_count);
    assert(read_u32(data + main_offset + 0x28) == texture_count);
    free(data);
}

int main(int argc, char **argv)
{
    unsigned char bch[0x1180];
    unsigned char untouched[0x1180];
    unsigned index;

    make_bch(bch, sizeof(bch));
    assert(suppress_c08_h08_bch_textures(
               "model/fighter/mario/body/c08/normal.bch", bch, sizeof(bch)) == 1);
    assert(read_u32(bch + 0x44 + 0x28) == 1);
    assert(read_u32(bch + 0x100) == 0x00080008);
    assert((read_u32(bch + 0x118) & 0xf) == 0xa);
    for (index = 0x180; index < 0x1a0; ++index)
        assert(bch[index] == 0);
    assert(bch[0x1a0] == 0xaa);

    make_bch(untouched, sizeof(untouched));
    assert(!suppress_c08_h08_bch_textures(
        "model/fighter/mario/body/c07/normal.bch", untouched,
        sizeof(untouched)));
    assert(read_u32(untouched + 0x100) == 0x00400040);
    assert(!suppress_c08_h08_bch_textures(
        "model/fighter/mario/body/c08/normal.mbn", untouched,
        sizeof(untouched)));
    assert(!suppress_c08_h08_bch_textures(
        "model/fighter/mario/throwsweat/c08/normal.bch", untouched,
        sizeof(untouched)));
    assert(!suppress_c08_h08_bch_textures(
        "model/fighter/mario/body/c080/normal.bch", untouched,
        sizeof(untouched)));

    make_bch(bch, sizeof(bch));
    write_u32(bch + 0xe8, 0xffffffffu);
    assert(!suppress_c08_h08_bch_textures(
        "model/fighter/mario/body/h08/metal_tex.bch", bch, sizeof(bch)));
    assert(read_u32(bch + 0x100) == 0x00400040);
    for (index = 1; index < (unsigned)argc; ++index)
        test_corpus_file(argv[index]);
    return 0;
}
