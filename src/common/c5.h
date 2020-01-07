#ifndef __C5_H__
#define __C5_H__

#include <stdint.h>

#define C5_PAD 64

/** note that input and output buffers must be at least C5_PAD bytes longer than
    otherwise required.
**/
uint32_t
uc5_length (const uint8_t *_in, int _inlen);

void
uc5 (const uint8_t *_in, int _inlen, uint8_t *_out, int *_outlen);

void
c5 (const uint8_t *_in, int _inlen, uint8_t *_out, int *_outlen);

#endif //__C5_H__
