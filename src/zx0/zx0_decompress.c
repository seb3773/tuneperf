/* ZX0 forward-buffer decompressor for stubs (no malloc) */
#include <stddef.h>
#include "zx0_decompress.h"

#define INITIAL_OFFSET 1

typedef struct {
    const unsigned char *src;
    const unsigned char *src_end;
    int bit_mask;
    int bit_value;
    int backtrack;
    int last_byte;
} BitReader;

static inline int rd_byte(BitReader *br) {
    return br->last_byte = *br->src++;
}

static inline int rd_bit(BitReader *br) {
    if (br->backtrack) {
        br->backtrack = 0;
        return br->last_byte & 1;
    }
    br->bit_mask >>= 1;
    if (!br->bit_mask) {
        br->bit_mask = 128;
        br->bit_value = rd_byte(br);
    }
    return (br->bit_value & br->bit_mask) ? 1 : 0;
}

static inline int rd_elias_gamma(BitReader *br, int inverted) {
    int value = 1;
    while (!rd_bit(br)) {
        value = (value << 1) | (rd_bit(br) ^ inverted);
    }
    return value;
}

int zx0_decompress_to(const unsigned char *in, int in_size,
                      unsigned char *out, int out_max)
{
    (void)in_size;
    (void)out_max;
    BitReader br;
    br.src = in;
    br.bit_mask = 0;
    br.bit_value = 0;
    br.backtrack = 0;
    br.last_byte = 0;

    int last_offset = INITIAL_OFFSET;
    int out_pos = 0;
    int length;

    for (;;) {
        /* COPY_LITERALS */
        length = rd_elias_gamma(&br, 0);
        for (int i = 0; i < length; i++) {
            out[out_pos++] = (unsigned char)rd_byte(&br);
        }
        if (rd_bit(&br)) {
            goto new_offset;
        }

        /* COPY_FROM_LAST_OFFSET */
        length = rd_elias_gamma(&br, 0);
        for (int i = 0; i < length; i++) {
            out[out_pos] = out[out_pos - last_offset];
            out_pos++;
        }
        if (!rd_bit(&br)) {
            continue; /* back to COPY_LITERALS */
        }

    new_offset:
        /* COPY_FROM_NEW_OFFSET */
        for (;;) {
            last_offset = rd_elias_gamma(&br, 1);
            if (last_offset == 256) {
                return out_pos; /* EOF */
            }
            last_offset = last_offset * 128 - (rd_byte(&br) >> 1);
            br.backtrack = 1;
            length = rd_elias_gamma(&br, 0) + 1;
            for (int i = 0; i < length; i++) {
                out[out_pos] = out[out_pos - last_offset];
                out_pos++;
            }
            if (!rd_bit(&br)) break; /* back to COPY_LITERALS */
        }
    }
}
