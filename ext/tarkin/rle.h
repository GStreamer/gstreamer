#ifndef __RLE_H
#define __RLE_H

#include <string.h>
#include <assert.h>
#include "mem.h"
#include "bitcoder.h"
#include "golomb.h"

#if defined(RLECODER)

#define OUTPUT_BIT(rlecoder,bit)          rlecoder_write_bit(rlecoder,bit)
#define INPUT_BIT(rlecoder)               rlecoder_read_bit(rlecoder)
#define OUTPUT_BIT_DIRECT(coder,bit)      bitcoder_write_bit(&(coder)->bitcoder,bit)
#define INPUT_BIT_DIRECT(rlecoder)        bitcoder_read_bit(&(rlecoder)->bitcoder)
#define ENTROPY_CODER                     RLECoderState
#define ENTROPY_ENCODER_INIT(coder,limit) rlecoder_encoder_init(coder,limit)
#define ENTROPY_ENCODER_DONE(coder)       rlecoder_encoder_done(coder)
#define ENTROPY_ENCODER_FLUSH(coder)      rlecoder_encoder_flush(coder)
#define ENTROPY_DECODER_INIT(coder,bitstream,limit) \
   rlecoder_decoder_init(coder,bitstream,limit)
#define ENTROPY_DECODER_DONE(coder)	/* nothing to do ... */
#define ENTROPY_CODER_BITSTREAM(coder)    ((coder)->bitcoder.bitstream)
#define ENTROPY_CODER_EOS(coder)          ((coder)->bitcoder.eos)

#define ENTROPY_CODER_SYMBOL(coder)          ((coder)->symbol)
#define ENTROPY_CODER_RUNLENGTH(coder)    ((coder)->count)
#define ENTROPY_CODER_SKIP(coder,skip)    do { (coder)->count -= skip; } while (0)
#endif




typedef struct
{
  int symbol;
  uint32_t count;		/*  have seen count symbol's         */
  BitCoderState bitcoder;
  GolombAdaptiveCoderState golomb_state[2];	/* 2 states for 2 symbols... */
  int have_seen_1;
} RLECoderState;



/*
 *   bit should be 0 or 1 !!!
 */
static inline void
rlecoder_write_bit (RLECoderState * s, int bit)
{
  assert (bit == 0 || bit == 1);

  if (s->symbol == -1) {
    s->symbol = bit & 1;
    s->count = 1;
    s->have_seen_1 = bit;
    bitcoder_write_bit (&s->bitcoder, bit);
  }

  if (s->symbol != bit) {
    golombcoder_encode_number (&s->golomb_state[s->symbol],
	&s->bitcoder, s->count);
    s->symbol = ~s->symbol & 1;
    s->have_seen_1 = 1;
    s->count = 1;
  } else
    s->count++;
}

static inline int
rlecoder_read_bit (RLECoderState * s)
{
  if (s->count == 0) {
    s->symbol = ~s->symbol & 1;
    s->count = golombcoder_decode_number (&s->golomb_state[s->symbol],
	&s->bitcoder);
    if (s->bitcoder.eos) {
      s->symbol = 0;
      s->count = ~0;
    }
  }
  s->count--;
  return (s->symbol);
}


int coder_id = 0;
FILE *file = NULL;

static inline void
rlecoder_encoder_init (RLECoderState * s, uint32_t limit)
{
  bitcoder_encoder_init (&s->bitcoder, limit);
  s->symbol = -1;
  s->have_seen_1 = 0;
  s->golomb_state[0].count = 0;
  s->golomb_state[1].count = 0;
  s->golomb_state[0].bits = 5 << 3;
  s->golomb_state[1].bits = 5 << 3;
}


/**
 *  once you called this, you better should not encode any more symbols ...
 */
static inline uint32_t
rlecoder_encoder_flush (RLECoderState * s)
{
  if (s->symbol == -1 || !s->have_seen_1)
    return 0;

  golombcoder_encode_number (&s->golomb_state[s->symbol],
      &s->bitcoder, s->count);
  return bitcoder_flush (&s->bitcoder);
}


static inline void
rlecoder_decoder_init (RLECoderState * s, uint8_t * bitstream, uint32_t limit)
{
  bitcoder_decoder_init (&s->bitcoder, bitstream, limit);
  s->golomb_state[0].count = 0;
  s->golomb_state[1].count = 0;
  s->golomb_state[0].bits = 5 << 3;
  s->golomb_state[1].bits = 5 << 3;
  s->symbol = bitcoder_read_bit (&s->bitcoder);
  s->count = golombcoder_decode_number (&s->golomb_state[s->symbol],
      &s->bitcoder) - 1;
  if (s->bitcoder.eos) {
    s->symbol = 0;
    s->count = ~0;
  }
}


static inline void
rlecoder_encoder_done (RLECoderState * s)
{
  bitcoder_encoder_done (&s->bitcoder);
}


#endif
