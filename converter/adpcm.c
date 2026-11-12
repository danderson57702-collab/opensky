// IMA ADPCM encoder for .skmus music (built and loaded by extract_music.py).
// Must stay bit-exact with the decoder in engine/src/music.c.

#include <stdint.h>

static const int16_t STEPS[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80,
	88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
	598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
	3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635,
	13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t INDEX_ADJUST[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

// Encodes `frames` samples of one channel (stride `stride` int16s apart) into blocks of
// block_frames samples: per block a 4-byte header (int16 predictor, u8 step index, u8 0)
// followed by block_frames / 2 bytes of nibbles (low nibble first). Blocks of all
// channels are interleaved by the caller: out points at this channel's first block and
// blocks are `block_stride` bytes apart. Missing samples at the end encode as silence.
void encode_channel(const int16_t *pcm, int stride, int frames, int block_frames, uint8_t *out,
                    int block_stride) {
	int pred = 0, index = 0;
	int blocks = (frames + block_frames - 1) / block_frames;
	for (int b = 0; b < blocks; b++) {
		uint8_t *o = out + (long)b * block_stride;
		o[0] = (uint8_t)(pred & 0xff);
		o[1] = (uint8_t)((pred >> 8) & 0xff);
		o[2] = (uint8_t)index;
		o[3] = 0;
		uint8_t *nib = o + 4;
		for (int i = 0; i < block_frames; i++) {
			int f = b * block_frames + i;
			int sample = f < frames ? pcm[(long)f * stride] : 0;
			int step = STEPS[index];
			int diff = sample - pred, code = 0;
			if (diff < 0) {
				code = 8;
				diff = -diff;
			}
			int vpdiff = step >> 3;
			if (diff >= step) {
				code |= 4;
				diff -= step;
				vpdiff += step;
			}
			if (diff >= step >> 1) {
				code |= 2;
				diff -= step >> 1;
				vpdiff += step >> 1;
			}
			if (diff >= step >> 2) {
				code |= 1;
				vpdiff += step >> 2;
			}
			pred += (code & 8) ? -vpdiff : vpdiff;
			if (pred > 32767) pred = 32767;
			if (pred < -32768) pred = -32768;
			index += INDEX_ADJUST[code & 7];
			if (index < 0) index = 0;
			if (index > 88) index = 88;
			if (i & 1)
				nib[i >> 1] |= (uint8_t)(code << 4);
			else
				nib[i >> 1] = (uint8_t)code;
		}
	}
}

// Reference decoder (for the converter's round-trip check); same layout as above.
void decode_channel(const uint8_t *in, int block_stride, int frames, int block_frames, int16_t *pcm,
                    int stride) {
	int blocks = (frames + block_frames - 1) / block_frames;
	for (int b = 0; b < blocks; b++) {
		const uint8_t *o = in + (long)b * block_stride;
		int pred = (int16_t)(o[0] | (o[1] << 8)), index = o[2];
		for (int i = 0; i < block_frames; i++) {
			int code = (o[4 + (i >> 1)] >> ((i & 1) * 4)) & 15;
			int step = STEPS[index];
			int vpdiff = step >> 3;
			if (code & 4) vpdiff += step;
			if (code & 2) vpdiff += step >> 1;
			if (code & 1) vpdiff += step >> 2;
			pred += (code & 8) ? -vpdiff : vpdiff;
			if (pred > 32767) pred = 32767;
			if (pred < -32768) pred = -32768;
			index += INDEX_ADJUST[code & 7];
			if (index < 0) index = 0;
			if (index > 88) index = 88;
			int f = b * block_frames + i;
			if (f < frames)
				pcm[(long)f * stride] = (int16_t)pred;
		}
	}
}
