#include "texture_filter.h"

#define CAVE __attribute__((section(".cave_texture_filter"), noinline, used))

#define PICA_TEXUNIT0_SIZE 0x0082u
#define PICA_TEXUNIT0_ADDRESS 0x0085u
#define PICA_TEXUNIT0_TYPE 0x008eu
#define PICA_L4 0xau
#define BLACK_TEXTURE_SIZE 32u
#define BLACK_TEXTURE_DIMENSIONS 0x00080008u

struct texture_command {
    unsigned char *size_parameter;
    unsigned char *address_parameter;
    unsigned char *type_parameter;
    unsigned address;
    unsigned dimensions;
    unsigned format;
};

static unsigned read_u32(const unsigned char *data)
{
    return (unsigned)data[0] | ((unsigned)data[1] << 8) |
           ((unsigned)data[2] << 16) | ((unsigned)data[3] << 24);
}

static void write_u32(unsigned char *data, unsigned value)
{
    data[0] = (unsigned char)value;
    data[1] = (unsigned char)(value >> 8);
    data[2] = (unsigned char)(value >> 16);
    data[3] = (unsigned char)(value >> 24);
}

static int add_in_bounds(unsigned base, unsigned length, unsigned limit,
                         unsigned *end)
{
    if (base > limit || length > limit - base)
        return 0;
    *end = base + length;
    return 1;
}

static int is_target_bch(const char *path)
{
    const char *end;

    if (!path)
        return 0;
    end = path;
    while (*end)
        ++end;
    if (end - path < 4 || end[-4] != '.' || end[-3] != 'b' ||
        end[-2] != 'c' || end[-1] != 'h')
        return 0;
    for (; path < end; ++path) {
        if (end - path >= 10 && path[0] == '/' && path[1] == 'b' &&
            path[2] == 'o' && path[3] == 'd' && path[4] == 'y' &&
            path[5] == '/' && (path[6] == 'c' || path[6] == 'h') &&
            path[7] == '0' && path[8] == '8' && path[9] == '/')
            return 1;
    }
    return 0;
}

static int record_parameter(struct texture_command *texture, unsigned reg,
                            unsigned mask, unsigned char *parameter)
{
    if (reg != PICA_TEXUNIT0_SIZE && reg != PICA_TEXUNIT0_ADDRESS &&
        reg != PICA_TEXUNIT0_TYPE)
        return 1;
    if (mask != 0xfu)
        return 0;
    if (reg == PICA_TEXUNIT0_SIZE) {
        texture->size_parameter = parameter;
        texture->dimensions = read_u32(parameter);
    } else if (reg == PICA_TEXUNIT0_ADDRESS) {
        texture->address_parameter = parameter;
        texture->address = read_u32(parameter);
    } else {
        texture->type_parameter = parameter;
        texture->format = read_u32(parameter) & 0xfu;
    }
    return 1;
}

static int read_texture_command(unsigned char *commands, unsigned words,
                                struct texture_command *texture)
{
    unsigned bytes;
    unsigned position = 0;

    if (words > 0x3fffffffu)
        return 0;
    bytes = words * 4u;
    texture->size_parameter = 0;
    texture->address_parameter = 0;
    texture->type_parameter = 0;
    texture->address = 0;
    texture->dimensions = 0;
    texture->format = 0xffffffffu;

    while (position + 8u <= bytes) {
        unsigned header = read_u32(commands + position + 4u);
        unsigned reg = header & 0xffffu;
        unsigned mask = (header >> 16) & 0xfu;
        unsigned extra = (header >> 20) & 0x7ffu;
        unsigned consecutive = header >> 31;
        unsigned index;

        if (!record_parameter(texture, reg, mask, commands + position))
            return 0;
        position += 8u;
        for (index = 0; index < extra; ++index) {
            if (position + 4u > bytes)
                return 0;
            if (consecutive)
                ++reg;
            if (!record_parameter(texture, reg, mask, commands + position))
                return 0;
            position += 4u;
        }
        position = (position + 7u) & ~7u;
    }
    return texture->size_parameter && texture->address_parameter &&
           texture->type_parameter &&
           texture->dimensions && texture->format <= 0xdu;
}

static int original_texture_size(const struct texture_command *texture,
                                 unsigned *size)
{
    unsigned width = texture->dimensions >> 16;
    unsigned height = texture->dimensions & 0xffffu;
    unsigned pixels;
    unsigned bits;

    if (!width || !height || width > 0xffffffffu / height)
        return 0;
    pixels = width * height;
    if (texture->format == 0)
        bits = 32;
    else if (texture->format == 1)
        bits = 24;
    else if (texture->format <= 6)
        bits = 16;
    else if (texture->format <= 9 || texture->format == 13)
        bits = 8;
    else
        bits = 4;
    if (pixels > (0xffffffffu - 7u) / bits)
        return 0;
    *size = (pixels * bits + 7u) / 8u;
    return *size >= BLACK_TEXTURE_SIZE;
}

static int process_texture(unsigned char *data, unsigned data_size,
                           unsigned gpu_offset, unsigned payload_offset,
                           unsigned entry_offset, int apply)
{
    struct texture_command texture;
    unsigned command_offset;
    unsigned command_words;
    unsigned command_end;
    unsigned payload;
    unsigned original_size;
    unsigned payload_end;
    unsigned index;

    if (!add_in_bounds(entry_offset, 8u, data_size, &command_end))
        return 0;
    command_offset = read_u32(data + entry_offset);
    command_words = read_u32(data + entry_offset + 4u);
    if (command_offset > 0xffffffffu - gpu_offset)
        return 0;
    command_offset += gpu_offset;
    if (command_words > 0x3fffffffu ||
        !add_in_bounds(command_offset, command_words * 4u, data_size,
                       &command_end) ||
        !read_texture_command(data + command_offset, command_words, &texture) ||
        !original_texture_size(&texture, &original_size) ||
        texture.address > 0xffffffffu - payload_offset)
        return 0;
    payload = payload_offset + texture.address;
    if (!add_in_bounds(payload, original_size, data_size, &payload_end))
        return 0;

    if (!apply)
        return 1;
    write_u32(texture.size_parameter, BLACK_TEXTURE_DIMENSIONS);
    write_u32(texture.type_parameter,
              (read_u32(texture.type_parameter) & ~0xfu) | PICA_L4);
    for (index = 0; index < BLACK_TEXTURE_SIZE; ++index)
        data[payload + index] = 0;
    return 1;
}

CAVE int suppress_c08_h08_bch_textures(const char *path, void *locked_data,
                                        unsigned data_size)
{
    unsigned char *data = (unsigned char *)locked_data;
    unsigned main_offset;
    unsigned gpu_offset;
    unsigned payload_offset;
    unsigned table_offset;
    unsigned texture_count;
    unsigned table_end;
    unsigned index;

    if (!is_target_bch(path) || !data || data_size < 0x70u ||
        data[0] != 'B' || data[1] != 'C' || data[2] != 'H' || data[3] != 0)
        return 0;

    main_offset = read_u32(data + 0x08);
    gpu_offset = read_u32(data + 0x10);
    payload_offset = read_u32(data + 0x14);
    if (!add_in_bounds(main_offset, 0x2cu, data_size, &table_end))
        return 0;
    table_offset = read_u32(data + main_offset + 0x24);
    texture_count = read_u32(data + main_offset + 0x28);
    if (!texture_count || table_offset > 0xffffffffu - main_offset)
        return 0;
    table_offset += main_offset;
    if (texture_count > 0x3fffffffu ||
        !add_in_bounds(table_offset, texture_count * 4u, data_size, &table_end))
        return 0;

    for (index = 0; index < texture_count; ++index) {
        unsigned relative = read_u32(data + table_offset + index * 4u);
        if (relative > 0xffffffffu - main_offset ||
            !process_texture(data, data_size, gpu_offset, payload_offset,
                             main_offset + relative, 0))
            return 0;
    }
    for (index = 0; index < texture_count; ++index) {
        unsigned relative = read_u32(data + table_offset + index * 4u);
        process_texture(data, data_size, gpu_offset, payload_offset,
                        main_offset + relative, 1);
    }
    return (int)texture_count;
}
