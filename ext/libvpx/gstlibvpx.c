/*
 * gstlibvpx.c - GStreamer/libvpx glue
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include <string.h>
#include <assert.h>
#include <vp8/common/entropy.h>
#include <vp8/common/entropymv.h>
#include <vp8/common/default_coef_probs.h>
#include <vp8/decoder/dboolhuff.h>
#include "gstlibvpx.h"

#define BOOL_DECODER_CAST(bd) \
  ((BOOL_DECODER *)(&(bd)->private[1]))

bool
vp8_bool_decoder_init (vp8_bool_decoder * bd, const uint8_t * buf,
    unsigned int buf_size)
{
  assert ((sizeof (*bd) - sizeof (bd->private[0])) >= sizeof (BOOL_DECODER));

  bd->private[0] = (uintptr_t)buf;
  return vp8dx_start_decode (BOOL_DECODER_CAST (bd), buf, buf_size,
              NULL, NULL) == 0;
}

int
vp8_bool_decoder_read (vp8_bool_decoder * bd, uint8_t prob)
{
  return vp8dx_decode_bool (BOOL_DECODER_CAST (bd), prob);
}

int
vp8_bool_decoder_read_literal (vp8_bool_decoder * bd, int bits)
{
  return vp8_decode_value (BOOL_DECODER_CAST (bd), bits);
}

unsigned int
vp8_bool_decoder_get_pos (vp8_bool_decoder * bd_)
{
  BOOL_DECODER *const bd = BOOL_DECODER_CAST (bd_);

  return ((uintptr_t)bd->user_buffer - bd_->private[0]) * 8 - (8 + bd->count);
}

void
vp8_bool_decoder_get_state (vp8_bool_decoder * bd_,
    vp8_bool_decoder_state * state)
{
  BOOL_DECODER *const bd = BOOL_DECODER_CAST (bd_);

  if (bd->count < 0)
    vp8dx_bool_decoder_fill (bd);

  state->range = bd->range;
  state->value = (uint8_t) ((bd->value) >> (VP8_BD_VALUE_SIZE - 8));
  state->count = (8 + bd->count) % 8;
}

void
vp8_init_token_update_probs (uint8_t
    probs[BLOCK_TYPES][COEF_BANDS][PREV_COEF_CONTEXTS][ENTROPY_NODES])
{
  memcpy (probs, vp8_coef_update_probs, sizeof (vp8_coef_update_probs));
}

void
vp8_init_default_token_probs (uint8_t
    probs[BLOCK_TYPES][COEF_BANDS][PREV_COEF_CONTEXTS][ENTROPY_NODES])
{
  memcpy (probs, default_coef_probs, sizeof (default_coef_probs));
}

void
vp8_init_mv_update_probs (uint8_t probs[2][MVPcount])
{
  memcpy (probs[0], vp8_mv_update_probs[0].prob,
      sizeof (vp8_mv_update_probs[0].prob));
  memcpy (probs[1], vp8_mv_update_probs[1].prob,
      sizeof (vp8_mv_update_probs[1].prob));
}

void
vp8_init_default_mv_probs (uint8_t probs[2][MVPcount])
{
  memcpy (probs[0], vp8_default_mv_context[0].prob,
      sizeof (vp8_default_mv_context[0].prob));
  memcpy (probs[1], vp8_default_mv_context[1].prob,
      sizeof (vp8_default_mv_context[1].prob));
}

void
vp8_init_default_intra_mode_probs (uint8_t y_probs[VP8_YMODES-1],
    uint8_t uv_probs[VP8_UV_MODES-1])
{
  extern const uint8_t vp8_kf_ymode_prob[VP8_YMODES-1];
  extern const uint8_t vp8_kf_uv_mode_prob[VP8_UV_MODES-1];

  memcpy (y_probs, vp8_kf_ymode_prob, sizeof (vp8_kf_ymode_prob));
  memcpy (uv_probs, vp8_kf_uv_mode_prob, sizeof (vp8_kf_uv_mode_prob));
}

void
vp8_init_default_inter_mode_probs (uint8_t y_probs[VP8_YMODES-1],
    uint8_t uv_probs[VP8_UV_MODES-1])
{
  extern const uint8_t vp8_ymode_prob[VP8_YMODES-1];
  extern const uint8_t vp8_uv_mode_prob[VP8_UV_MODES-1];

  memcpy (y_probs, vp8_ymode_prob, sizeof (vp8_ymode_prob));
  memcpy (uv_probs, vp8_uv_mode_prob, sizeof (vp8_uv_mode_prob));
}
