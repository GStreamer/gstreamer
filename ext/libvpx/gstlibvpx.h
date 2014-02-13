/*
 * gstlibvpx.h - GStreamer/libvpx glue
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#ifndef GST_LIBVPX_H
#define GST_LIBVPX_H

#include <stdint.h>
#include <stdbool.h>

typedef struct _vp8_bool_decoder        vp8_bool_decoder;
typedef struct _vp8_bool_decoder_state  vp8_bool_decoder_state;

struct _vp8_bool_decoder {
    uintptr_t   private[16];
};

struct _vp8_bool_decoder_state {
    uint8_t     range; /* Current "range" value (<= 255) */
    uint8_t     value; /* Current "value" value */
    uint8_t     count; /* Number of bits shifted out of value (<= 7) */
};

bool
vp8_bool_decoder_init (vp8_bool_decoder * bd, const uint8_t * buf,
    unsigned int buf_size);

int
vp8_bool_decoder_read (vp8_bool_decoder * bd, uint8_t prob);

int
vp8_bool_decoder_read_literal (vp8_bool_decoder * bd, int bits);

unsigned int
vp8_bool_decoder_get_pos (vp8_bool_decoder * bd);

void
vp8_bool_decoder_get_state (vp8_bool_decoder * bd,
    vp8_bool_decoder_state * state);

void
vp8_init_token_update_probs (uint8_t probs[4][8][3][11]);

void
vp8_init_default_token_probs (uint8_t probs[4][8][3][11]);

void
vp8_init_mv_update_probs (uint8_t probs[2][19]);

void
vp8_init_default_mv_probs (uint8_t probs[2][19]);

void
vp8_init_default_intra_mode_probs (uint8_t y_probs[4], uint8_t uv_probs[3]);

void
vp8_init_default_inter_mode_probs (uint8_t y_probs[4], uint8_t uv_probs[3]);

#endif /* GST_LIBVPX_H */
