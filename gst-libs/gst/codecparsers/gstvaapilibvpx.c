/*
 * gstvaapilibvpx.c - libvpx wrapper for gstreamer-vaapi
 *
 * Copyright (C) 2014 Intel Corporation
 *   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstvp8rangedecoder.h"
#include "vp8utils.h"
#include "gstlibvpx.h"

#define BOOL_DECODER_CAST(rd) \
  ((vp8_bool_decoder *)(&(rd)->_gst_reserved[0]))

#define BOOL_DECODER_STATE_CAST(s) \
  ((vp8_bool_decoder_state *)(s))

gboolean
gst_vp8_range_decoder_init (GstVp8RangeDecoder * rd, const guchar * buf,
    guint buf_size)
{
  vp8_bool_decoder *const bd = BOOL_DECODER_CAST (rd);

  g_return_val_if_fail (sizeof (rd->_gst_reserved) >= sizeof (*bd), FALSE);

  rd->buf = buf;
  rd->buf_size = buf_size;
  return vp8_bool_decoder_init (bd, buf, buf_size);
}

gint
gst_vp8_range_decoder_read (GstVp8RangeDecoder * rd, guint8 prob)
{
  return vp8_bool_decoder_read (BOOL_DECODER_CAST (rd), prob);
}

gint
gst_vp8_range_decoder_read_literal (GstVp8RangeDecoder * rd, gint bits)
{
  return vp8_bool_decoder_read_literal (BOOL_DECODER_CAST (rd), bits);
}

guint
gst_vp8_range_decoder_get_pos (GstVp8RangeDecoder * rd)
{
  return vp8_bool_decoder_get_pos (BOOL_DECODER_CAST (rd));
}

void
gst_vp8_range_decoder_get_state (GstVp8RangeDecoder * rd,
    GstVp8RangeDecoderState * state)
{
  vp8_bool_decoder_get_state (BOOL_DECODER_CAST (rd),
      BOOL_DECODER_STATE_CAST (state));
}

void
gst_vp8_token_update_probs_init (GstVp8TokenProbs * probs)
{
  vp8_init_token_update_probs (probs->prob);
}

void
gst_vp8_token_probs_init_defaults (GstVp8TokenProbs * probs)
{
  vp8_init_default_token_probs (probs->prob);
}

void
gst_vp8_mv_update_probs_init (GstVp8MvProbs * probs)
{
  vp8_init_mv_update_probs (probs->prob);
}

void
gst_vp8_mv_probs_init_defaults (GstVp8MvProbs * probs)
{
  vp8_init_default_mv_probs (probs->prob);
}

void
gst_vp8_mode_probs_init_defaults (GstVp8ModeProbs * probs, gboolean key_frame)
{
  if (key_frame) {
    vp8_init_default_intra_mode_probs (probs->y_prob, probs->uv_prob);
  } else {
    vp8_init_default_inter_mode_probs (probs->y_prob, probs->uv_prob);
  }
}
