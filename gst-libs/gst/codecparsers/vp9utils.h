/*
 *  Copyright (c) 2010 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef __VP9_QUANT_H__
#define __VP9_QUANT_H__

#include <stdint.h>
#include <glib.h>

#define MAXQ 255
#define QINDEX_RANGE 256
#define QINDEX_BITS 8

G_GNUC_INTERNAL
int16_t gst_vp9_dc_quant(int qindex, int delta, int bit_depth);

G_GNUC_INTERNAL
int16_t gst_vp9_ac_quant(int qindex, int delta, int bit_depth);


#endif //__VP9_QUANT_H__
