/*
 *  gstvaapiutils_h26x_priv.h - H.26x related utilities
 *
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne
 *  Copyright (C) 2017 Intel Corporation
 *    Author: Hyunjun Ko <zzoon@igalia.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_UTILS_H26X_PRIV_H
#define GST_VAAPI_UTILS_H26X_PRIV_H

#include <gst/base/gstbitwriter.h>

G_BEGIN_DECLS

/* Default CPB length (in milliseconds) */
#define DEFAULT_CPB_LENGTH 1500

/* Scale factor for CPB size (HRD cpb_size_scale: min = 4) */
#define SX_CPB_SIZE 4

/* Scale factor for bitrate (HRD bit_rate_scale: min = 6) */
#define SX_BITRATE 6

/* Define default rate control mode ("constant-qp") */
#define DEFAULT_RATECONTROL GST_VAAPI_RATECONTROL_CQP

/* ------------------------------------------------------------------------- */
/* --- H.264/265 Bitstream Writer                                            --- */
/* ------------------------------------------------------------------------- */

#define WRITE_UINT32(bs, val, nbits)                            \
  G_STMT_START {                                                \
    if (!gst_bit_writer_put_bits_uint32 (bs, val, nbits)) {     \
      GST_WARNING ("failed to write uint32, nbits: %d", nbits); \
      goto bs_error;                                            \
    }                                                           \
  } G_STMT_END

#define WRITE_UE(bs, val)                       \
  G_STMT_START {                                \
    if (!bs_write_ue (bs, val)) {               \
      GST_WARNING ("failed to write ue(v)");    \
      goto bs_error;                            \
    }                                           \
  } G_STMT_END

#define WRITE_SE(bs, val)                       \
  G_STMT_START {                                \
    if (!bs_write_se (bs, val)) {               \
      GST_WARNING ("failed to write se(v)");    \
      goto bs_error;                            \
    }                                           \
  } G_STMT_END

G_GNUC_INTERNAL
gboolean
bs_write_ue (GstBitWriter * bs, guint32 value);

G_GNUC_INTERNAL
gboolean
bs_write_se (GstBitWriter * bs, gint32 value);

/* Write nal unit, applying emulation prevention bytes */
G_GNUC_INTERNAL
gboolean
gst_vaapi_utils_h26x_write_nal_unit (GstBitWriter * bs, guint8 * nal, guint nal_size);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_H26X_PRIV_H */
