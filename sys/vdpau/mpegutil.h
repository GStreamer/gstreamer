/* GStreamer
 * Copyright (C) 2007 Jan Schmidt <thaytan@mad.scientist.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __MPEGUTIL_H__
#define __MPEGUTIL_H__

#include <gst/gst.h>

typedef struct MPEGSeqHdr MPEGSeqHdr;
typedef struct MPEGPictureHdr MPEGPictureHdr;
typedef struct MPEGPictureExt MPEGPictureExt;
typedef struct MPEGPictureGOP MPEGPictureGOP;
typedef struct MPEGQuantMatrix MPEGQuantMatrix;

/* Packet ID codes for different packet types we
 * care about */
#define MPEG_PACKET_PICTURE      0x00
#define MPEG_PACKET_SLICE_MIN    0x01
#define MPEG_PACKET_SLICE_MAX    0xaf
#define MPEG_PACKET_SEQUENCE     0xb3
#define MPEG_PACKET_EXTENSION    0xb5
#define MPEG_PACKET_SEQUENCE_END 0xb7
#define MPEG_PACKET_GOP          0xb8
#define MPEG_PACKET_NONE         0xff

/* Extension codes we care about */
#define MPEG_PACKET_EXT_SEQUENCE         0x01
#define MPEG_PACKET_EXT_SEQUENCE_DISPLAY 0x02
#define MPEG_PACKET_EXT_QUANT_MATRIX     0x03
#define MPEG_PACKET_EXT_PICTURE_CODING   0x08

/* frame types */
#define I_FRAME         1
#define P_FRAME         2
#define B_FRAME         3

struct MPEGSeqHdr
{
  /* 0 for unknown, else 1 or 2 */
  guint8 mpeg_version;

  /* Pixel-Aspect Ratio from DAR code via set_par_from_dar */
  gint par_w, par_h;
  /* Width and Height of the video */
  gint width, height;
  /* Framerate */
  gint fps_n, fps_d;

  /* mpeg2 decoder profile */
  gint profile;

  guint8 intra_quantizer_matrix[64];
  guint8 non_intra_quantizer_matrix[64];
};

struct MPEGPictureHdr
{
  guint8 pic_type;

  guint8 full_pel_forward_vector, full_pel_backward_vector;

  guint8 f_code[2][2];
};

struct MPEGPictureExt
{
  guint8 f_code[2][2];

  guint8 intra_dc_precision;
  guint8 picture_structure;
  guint8 top_field_first;
  guint8 frame_pred_frame_dct;
  guint8 concealment_motion_vectors;
  guint8 q_scale_type;
  guint8 intra_vlc_format;
};

struct MPEGPictureGOP
{
  guint8 drop_frame_flag;
  guint8 frame;

  GstClockTime timestamp;
};

struct MPEGQuantMatrix
{
  guint8 intra_quantizer_matrix[64];
  guint8 non_intra_quantizer_matrix[64];
};

gboolean mpeg_util_parse_sequence_hdr (MPEGSeqHdr *hdr, 
                                       guint8 *data, guint8 *end);

gboolean mpeg_util_parse_picture_hdr (MPEGPictureHdr * hdr, guint8 * data, guint8 * end);

gboolean mpeg_util_parse_picture_coding_extension (MPEGPictureExt *ext, guint8 *data, guint8 *end);

gboolean mpeg_util_parse_picture_gop (MPEGPictureGOP * gop, guint8 * data, guint8 * end);

gboolean mpeg_util_parse_quant_matrix (MPEGQuantMatrix * qm, guint8 * data, guint8 * end);

guint8 *mpeg_util_find_start_code (guint32 * sync_word, guint8 * cur, guint8 * end);
guint32 read_bits (guint8 * buf, gint start_bit, gint n_bits);

#endif
