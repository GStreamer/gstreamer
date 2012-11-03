/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef __GST_MPEG4UTIL_H__
#define __GST_MPEG4UTIL_H__

#include <gst/gst.h>
#include <gst/base/gstbitreader.h>

#define CHECK_ALLOWED(val, min, max) { \
  if (val < min || val > max) { \
    GST_WARNING ("value not in allowed range. value: %d, range %d-%d", \
                     val, min, max); \
    goto error; \
  } \
}

#define READ_UINT8(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint8 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint8, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT16(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint16 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint16, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT32(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint32 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint32, nbits: %d", nbits); \
    goto error; \
  } \
}

#define READ_UINT64(reader, val, nbits) { \
  if (!gst_bit_reader_get_bits_uint64 (reader, &val, nbits)) { \
    GST_WARNING ("failed to read uint64, nbits: %d", nbits); \
    goto error; \
  } \
}

#define SKIP(reader, nbits) { \
  if (!gst_bit_reader_skip (reader, nbits)) { \
    GST_WARNING ("failed to skip nbits: %d", nbits); \
    goto error; \
  } \
}

typedef struct _Mpeg4VisualObjectSequence Mpeg4VisualObjectSequence;
typedef struct _Mpeg4VisualObject Mpeg4VisualObject;
typedef struct _Mpeg4VideoObjectLayer Mpeg4VideoObjectLayer;
typedef struct _Mpeg4GroupofVideoObjectPlane Mpeg4GroupofVideoObjectPlane;
typedef struct _Mpeg4VideoObjectPlane Mpeg4VideoObjectPlane;

#define MPEG4_PACKET_VOL_MIN 0x20
#define MPEG4_PACKET_VOL_MAX 0x2f

#define MPEG4_PACKET_VOS     0xb0
#define MPEG4_PACKET_EVOS    0xb1
#define MPEG4_PACKET_GOV     0xb3
#define MPEG4_PACKET_VO      0xb5
#define MPEG4_PACKET_VOP     0xb6

#define I_VOP 0x0
#define P_VOP 0x1
#define B_VOP 0x2
#define S_VOP 0x3

struct _Mpeg4VisualObjectSequence {
  guint8 profile_and_level_indication;
};

struct _Mpeg4VisualObject {
  guint8 verid;
  guint8 priority;
  guint8 type;
};

struct _Mpeg4VideoObjectLayer {
  guint8 random_accesible_vol;
  guint8 video_object_type_indication;

  guint8 is_object_layer_identifier;
  /* if is_object_layer_identifier */
  guint8 verid;
  guint8 priority;

  guint8 par_n;
  guint8 par_d;

  guint8 chroma_format;
  guint8 low_delay;
  guint8 vbv_parameters;
  /* if vbv_parameters */
  guint32 bit_rate;
  guint32 vbv_buffer_size;

  guint8 shape;

  guint16 vop_time_increment_resolution;
  guint8 vop_time_increment_bits;
  guint8 fixed_vop_rate;
  /* if fixed_vop_rate */
  guint16 fixed_vop_time_increment;

  guint16 width;
  guint16 height;
  guint8 interlaced;
  guint8 obmc_disable;
  
  guint8 sprite_enable;

  guint8 quant_precision;
  guint8 bits_per_pixel;

  guint8 quant_type;
  guint8 intra_quant_mat[64];
  guint8 non_intra_quant_mat[64];

  guint8 quarter_sample;
  guint8 complexity_estimation_disable;
  guint8 resync_marker_disable;
};

struct _Mpeg4GroupofVideoObjectPlane {
  guint8 hours;
  guint8 minutes;
  guint8 seconds;
  
  guint8 closed;
  guint8 broken_link;
};

struct _Mpeg4VideoObjectPlane {
  guint8 coding_type;
  guint8 modulo_time_base;
  guint16 time_increment;

  guint8 coded;
  guint8 rounding_type;
  guint8 intra_dc_vlc_thr;

  guint8 top_field_first;
  guint8 alternate_vertical_scan_flag;

  guint16 quant;

  guint8 fcode_forward;
  guint8 fcode_backward;
};

gboolean mpeg4_util_parse_VOP (GstBuffer *buf, Mpeg4VideoObjectLayer *vol, Mpeg4VideoObjectPlane *vop);
gboolean mpeg4_util_parse_GOV (GstBuffer *buf, Mpeg4GroupofVideoObjectPlane *gov);
gboolean mpeg4_util_parse_VOL (GstBuffer *buf, Mpeg4VisualObject *vo, Mpeg4VideoObjectLayer *vol);
gboolean mpeg4_util_parse_VO (GstBuffer *buf, Mpeg4VisualObject *vo);
gboolean mpeg4_util_parse_VOS (GstBuffer *buf, Mpeg4VisualObjectSequence *vos);

#endif /* __GST_MPEG4UTIL_H__ */