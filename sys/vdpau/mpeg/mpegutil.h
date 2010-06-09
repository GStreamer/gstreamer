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
typedef struct MPEGSeqExtHdr MPEGSeqExtHdr;
typedef struct MPEGPictureHdr MPEGPictureHdr;
typedef struct MPEGPictureExt MPEGPictureExt;
typedef struct MPEGGop MPEGGop;
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
  /* Pixel-Aspect Ratio from DAR code via set_par_from_dar */
  guint par_w, par_h;
  /* Width and Height of the video */
  guint16 width, height;
  /* Framerate */
  guint fps_n, fps_d;

  guint32 bitrate;
  guint16 vbv_buffer;

  guint8 constrained_parameters_flag;
  
  guint8 intra_quantizer_matrix[64];
  guint8 non_intra_quantizer_matrix[64];
};

struct MPEGSeqExtHdr
{

  /* mpeg2 decoder profile */
  guint8 profile;
  /* mpeg2 decoder level */
  guint8 level;

  guint8 progressive;
  guint8 chroma_format;
  
  guint8 horiz_size_ext, vert_size_ext;

  guint16 bitrate_ext;
  guint8 fps_n_ext, fps_d_ext;
  
};

struct MPEGPictureHdr
{
  guint16 tsn;
  guint8 pic_type;
  guint16 vbv_delay;
  
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
  guint8 alternate_scan;
  guint8 repeat_first_field;
  guint8 chroma_420_type;
  guint8 progressive_frame;
};

struct MPEGGop
{
  guint8 drop_frame_flag;

  guint8 hour, minute, second, frame;

  guint8 closed_gop;
  guint8 broken_gop;
};

struct MPEGQuantMatrix
{
  guint8 intra_quantizer_matrix[64];
  guint8 non_intra_quantizer_matrix[64];
};

gboolean mpeg_util_parse_sequence_hdr (MPEGSeqHdr *hdr, GstBuffer *buffer);

gboolean mpeg_util_parse_sequence_extension (MPEGSeqExtHdr *hdr,
    GstBuffer *buffer);

gboolean mpeg_util_parse_picture_hdr (MPEGPictureHdr * hdr, GstBuffer *buffer);

gboolean mpeg_util_parse_picture_coding_extension (MPEGPictureExt *ext,
    GstBuffer *buffer);

gboolean mpeg_util_parse_gop (MPEGGop * gop, GstBuffer *buffer);

gboolean mpeg_util_parse_quant_matrix (MPEGQuantMatrix * qm, GstBuffer *buffer);

#endif

