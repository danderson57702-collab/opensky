#ifndef ADPCM_H
#define ADPCM_H

#include <stdint.h>

// IMA ADPCM, bit-exact with converter/adpcm.c. A channel block is a 4-byte header
// (s16 predictor, u8 step index, u8 0) and then frames / 2 bytes of 4-bit codes.
// Decodes one channel block into every `stride`-th int16 of out.
void adpcm_decode_block(const uint8_t *in, int frames, int16_t *out, int stride);

#endif
