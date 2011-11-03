/* Gstreamer
 * Copyright (C) <2011> Intel Corporation
 * Copyright (C) <2011> Collabora Ltd.
 * Copyright (C) <2011> Thibault Saunier <thibault.saunier@collabora.com>
 *
 * From bad/sys/vdpau/mpeg/mpegutil.c:
 *   Copyright (C) <2007> Jan Schmidt <thaytan@mad.scientist.com>
 *   Copyright (C) <2009> Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef __GST_MPEG_VIDEO_UTILS_H__
#define __GST_MPEG_VIDEO_UTILS_H__

#ifndef GST_USE_UNSTABLE_API
#warning "The Mpeg video parsing library is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

/**
 * GstMpegVideoPacketTypeCode:
 * @GST_MPEG_VIDEO_PACKET_PICTURE: Picture packet starting code
 * @GST_MPEG_VIDEO_PACKET_SLICE_MIN: Slice min packet starting code
 * @GST_MPEG_VIDEO_PACKET_SLICE_MAX: Slice max packet starting code
 * @GST_MPEG_VIDEO_PACKET_USER_DATA: User data packet starting code
 * @GST_MPEG_VIDEO_PACKET_SEQUENCE : Sequence packet starting code
 * @GST_MPEG_VIDEO_PACKET_EXTENSION: Extension packet starting code
 * @GST_MPEG_VIDEO_PACKET_SEQUENCE_END: Sequence end packet code
 * @GST_MPEG_VIDEO_PACKET_GOP: Group of Picture packet starting code
 * @GST_MPEG_VIDEO_PACKET_NONE: None packet code
 *
 * Indicates the type of MPEG packet
 */
typedef enum {
  GST_MPEG_VIDEO_PACKET_PICTURE      = 0x00,
  GST_MPEG_VIDEO_PACKET_SLICE_MIN    = 0x01,
  GST_MPEG_VIDEO_PACKET_SLICE_MAX    = 0xaf,
  GST_MPEG_VIDEO_PACKET_USER_DATA    = 0xb2,
  GST_MPEG_VIDEO_PACKET_SEQUENCE     = 0xb3,
  GST_MPEG_VIDEO_PACKET_EXTENSION    = 0xb5,
  GST_MPEG_VIDEO_PACKET_SEQUENCE_END = 0xb7,
  GST_MPEG_VIDEO_PACKET_GOP          = 0xb8,
  GST_MPEG_VIDEO_PACKET_NONE         = 0xff
} GstMpegVideoPacketTypeCode;

/**
 * GST_MPEG_VIDEO_PACKET_IS_SLICE:
 * @typecode: The MPEG video packet type code
 *
 * Checks whether a packet type code is a slice.
 *
 * Returns: %TRUE if the packet type code corresponds to a slice,
 * else %FALSE.
 */
#define GST_MPEG_VIDEO_PACKET_IS_SLICE(typecode) ((typecode) >= GST_MPEG_VIDEO_PACKET_SLICE_MIN && \
						  (typecode) <= GST_MPEG_VIDEO_PACKET_SLICE_MAX)

/**
 * GstMpegVideoPacketExtensionCode:
 * @GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE: Sequence extension code
 * @GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE_DISPLAY: Sequence Display extension code
 * @GST_MPEG_VIDEO_PACKET_EXT_QUANT_MATRIX: Quantization Matrix extension code
 * @GST_MPEG_VIDEO_PACKET_EXT_PICTURE: Picture coding extension
 *
 * Indicates what type of packets are in this block, some are mutually
 * exclusive though - ie, sequence packs are accumulated separately. GOP &
 * Picture may occur together or separately.
 */
typedef enum {
  GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE         = 0x01,
  GST_MPEG_VIDEO_PACKET_EXT_SEQUENCE_DISPLAY = 0x02,
  GST_MPEG_VIDEO_PACKET_EXT_QUANT_MATRIX     = 0x03,
  GST_MPEG_VIDEO_PACKET_EXT_PICTURE          = 0x08
} GstMpegVideoPacketExtensionCode;

/**
 * GstMpegVideoLevel:
 * @GST_MPEG_VIDEO_LEVEL_LOW: Low level (LL)
 * @GST_MPEG_VIDEO_LEVEL_MAIN: Main level (ML)
 * @GST_MPEG_VIDEO_LEVEL_HIGH_1440: High 1440 level (H-14)
 * @GST_MPEG_VIDEO_LEVEL_HIGH: High level (HL)
 *
 * Mpeg-2 Levels.
 **/
typedef enum {
 GST_MPEG_VIDEO_LEVEL_HIGH      = 0x04,
 GST_MPEG_VIDEO_LEVEL_HIGH_1440 = 0x06,
 GST_MPEG_VIDEO_LEVEL_MAIN      = 0x08,
 GST_MPEG_VIDEO_LEVEL_LOW       = 0x0a
} GstMpegVideoLevel;

/**
 * GstMpegVideoProfile:
 * @GST_MPEG_VIDEO_PROFILE_422: 4:2:2 profile (422)
 * @GST_MPEG_VIDEO_PROFILE_HIGH: High profile (HP)
 * @GST_MPEG_VIDEO_PROFILE_SPATIALLY_SCALABLE: Spatially Scalable profile (Spatial)
 * @GST_MPEG_VIDEO_PROFILE_SNR_SCALABLE: SNR Scalable profile (SNR)
 * @GST_MPEG_VIDEO_PROFILE_MAIN: Main profile (MP)
 * @GST_MPEG_VIDEO_PROFILE_SIMPLE: Simple profile (SP)
 *
 * Mpeg-2 Profiles.
 **/
typedef enum {
  GST_MPEG_VIDEO_PROFILE_422                 = 0x00,
  GST_MPEG_VIDEO_PROFILE_HIGH                = 0x01,
  GST_MPEG_VIDEO_PROFILE_SPATIALLY_SCALABLE  = 0x02,
  GST_MPEG_VIDEO_PROFILE_SNR_SCALABLE        = 0x03,
  GST_MPEG_VIDEO_PROFILE_MAIN                = 0x04,
  GST_MPEG_VIDEO_PROFILE_SIMPLE              = 0x05
} GstMpegVideoProfile;

/**
 * GstMpegVideoChromaFormat:
 * @GST_MPEG_VIDEO_CHROMA_RES: Invalid (reserved for future use)
 * @GST_MPEG_VIDEO_CHROMA_420: 4:2:0 subsampling
 * @GST_MPEG_VIDEO_CHROMA_422: 4:2:2 subsampling
 * @GST_MPEG_VIDEO_CHROMA_444: 4:4:4 (non-subsampled)
 *
 * Chroma subsampling type.
 */
typedef enum {
  GST_MPEG_VIDEO_CHROMA_RES = 0x00,
  GST_MPEG_VIDEO_CHROMA_420 = 0x01,
  GST_MPEG_VIDEO_CHROMA_422 = 0x02,
  GST_MPEG_VIDEO_CHROMA_444 = 0x03,
} GstMpegVideoChromaFormat;

/**
 * GstMpegVideoPictureType:
 * @GST_MPEG_VIDEO_PICTURE_TYPE_I: Intra-coded (I) frame
 * @GST_MPEG_VIDEO_PICTURE_TYPE_P: Predictive-codec (P) frame
 * @GST_MPEG_VIDEO_PICTURE_TYPE_B: Bidirectionally predictive-coded (B) frame
 * @GST_MPEG_VIDEO_PICTURE_TYPE_D: D frame
 *
 * Picture type.
 */
typedef enum {
  GST_MPEG_VIDEO_PICTURE_TYPE_I = 0x01,
  GST_MPEG_VIDEO_PICTURE_TYPE_P = 0x02,
  GST_MPEG_VIDEO_PICTURE_TYPE_B = 0x03,
  GST_MPEG_VIDEO_PICTURE_TYPE_D = 0x04
} GstMpegVideoPictureType;

/**
 * GstMpegVideoPictureStructure:
 * @GST_MPEG_VIDEO_PICTURE_STRUCTURE_TOP_FIELD: Top field
 * @GST_MPEG_VIDEO_PICTURE_STRUCTURE_BOTTOM_FIELD: Bottom field
 * @GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME: Frame picture
 *
 * Picture structure type.
 */
typedef enum {
    GST_MPEG_VIDEO_PICTURE_STRUCTURE_TOP_FIELD    = 0x01,
    GST_MPEG_VIDEO_PICTURE_STRUCTURE_BOTTOM_FIELD = 0x02,
    GST_MPEG_VIDEO_PICTURE_STRUCTURE_FRAME        = 0x03
} GstMpegVideoPictureStructure;

typedef struct _GstMpegVideoSequenceHdr     GstMpegVideoSequenceHdr;
typedef struct _GstMpegVideoSequenceExt     GstMpegVideoSequenceExt;
typedef struct _GstMpegVideoPictureHdr      GstMpegVideoPictureHdr;
typedef struct _GstMpegVideoGop             GstMpegVideoGop;
typedef struct _GstMpegVideoPictureExt      GstMpegVideoPictureExt;
typedef struct _GstMpegVideoQuantMatrixExt  GstMpegVideoQuantMatrixExt;
typedef struct _GstMpegVideoTypeOffsetSize  GstMpegVideoTypeOffsetSize;

/**
 * GstMpegVideoSequenceHdr:
 * @width: Width of each frame
 * @height: Height of each frame
 * @par_w: Calculated Pixel Aspect Ratio width
 * @par_h: Calculated Pixel Aspect Ratio height
 * @fps_n: Calculated Framrate nominator
 * @fps_d: Calculated Framerate denominator
 * @bitrate_value: Value of the bitrate as is in the stream (400bps unit)
 * @bitrate: the real bitrate of the Mpeg video stream in bits per second, 0 if VBR stream
 * @constrained_parameters_flag: %TRUE if this stream uses contrained parameters.
 * @intra_quantizer_matrix: intra-quantization table
 * @non_intra_quantizer_matrix: non-intra quantization table
 *
 * The Mpeg2 Video Sequence Header structure.
 */
struct _GstMpegVideoSequenceHdr
{
  guint16 width, height;
  guint8  aspect_ratio_info;
  guint8  frame_rate_code;
  guint32 bitrate_value;
  guint16 vbv_buffer_size_value;

  guint8  constrained_parameters_flag;

  guint8  intra_quantizer_matrix[64];
  guint8  non_intra_quantizer_matrix[64];

  /* Calculated values */
  guint   par_w, par_h;
  guint   fps_n, fps_d;
  guint   bitrate;
};

/**
 * GstMpegVideoSequenceExt:
 * @profile: mpeg2 decoder profil
 * @level: mpeg2 decoder level
 * @progressive: %TRUE if the frames are progressive %FALSE otherwize
 * @chroma_format: indicates the chrominance format
 * @horiz_size_ext: Horizontal size
 * @vert_size_ext: Vertical size
 * @bitrate_ext: The bitrate
 * @vbv_buffer_size_extension: Vbv vuffer size
 * @low_delay: %TRUE if the sequence doesn't contain any B-pictures, %FALSE
 * otherwize
 * @fps_n_ext: Framerate nominator code
 * @fps_d_ext: Framerate denominator code
 *
 * The Mpeg2 Video Sequence Extension structure.
 **/
struct _GstMpegVideoSequenceExt
{
  /* mpeg2 decoder profile */
  guint8 profile;
  /* mpeg2 decoder level */
  guint8 level;

  guint8 progressive;
  guint8 chroma_format;

  guint8 horiz_size_ext, vert_size_ext;

  guint16 bitrate_ext;
  guint8 vbv_buffer_size_extension;
  guint8 low_delay;
  guint8 fps_n_ext, fps_d_ext;

};

/**
 * GstMpegVideoQuantMatrixExt:
 * @load_intra_quantiser_matrix:
 * @intra_quantiser_matrix:
 * @load_non_intra_quantiser_matrix:
 * @non_intra_quantiser_matrix:
 * @load_chroma_intra_quantiser_matrix:
 * @chroma_intra_quantiser_matrix:
 * @load_chroma_non_intra_quantiser_matrix:
 * @chroma_non_intra_quantiser_matrix:
 *
 * The Quant Matrix Extension structure
 */
struct _GstMpegVideoQuantMatrixExt
{
 guint8 load_intra_quantiser_matrix;
 guint8 intra_quantiser_matrix[64];
 guint8 load_non_intra_quantiser_matrix;
 guint8 non_intra_quantiser_matrix[64];
 guint8 load_chroma_intra_quantiser_matrix;
 guint8 chroma_intra_quantiser_matrix[64];
 guint8 load_chroma_non_intra_quantiser_matrix;
 guint8 chroma_non_intra_quantiser_matrix[64];
};

/**
 * GstMpegVideoPictureHdr:
 * @tsn: Temporal Sequence Number
 * @pic_type: Type of the frame
 * @full_pel_forward_vector: the full pel forward flag of
 *  the frame: 0 or 1.
 * @full_pel_backward_vector: the full pel backward flag
 *  of the frame: 0 or 1.
 * @f_code: F code
 *
 * The Mpeg2 Video Picture Header structure.
 */
struct _GstMpegVideoPictureHdr
{
  guint16 tsn;
  guint8 pic_type;

  guint8 full_pel_forward_vector, full_pel_backward_vector;

  guint8 f_code[2][2];
};

/**
 * GstMpegVideoPictureExt:
 * @intra_dc_precision: Intra DC precision
 * @picture_structure: Structure of the picture
 * @top_field_first: Top field first
 * @frame_pred_frame_dct: Frame
 * @concealment_motion_vectors: Concealment Motion Vectors
 * @q_scale_type: Q Scale Type
 * @intra_vlc_format: Intra Vlc Format
 * @alternate_scan: Alternate Scan
 * @repeat_first_field: Repeat First Field
 * @chroma_420_type: Chroma 420 Type
 * @progressive_frame: %TRUE if the frame is progressive %FALSE otherwize
 *
 * The Mpeg2 Video Picture Extension structure.
 */
struct _GstMpegVideoPictureExt
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
  guint8 composite_display;
  guint8 v_axis;
  guint8 field_sequence;
  guint8 sub_carrier;
  guint8 burst_amplitude;
  guint8 sub_carrier_phase;
};

/**
 * GstMpegVideoGop:
 * @drop_frame_flag: Drop Frame Flag
 * @hour: Hour (0-23)
 * @minute: Minute (O-59)
 * @second: Second (0-59)
 * @frame: Frame (0-59)
 * @closed_gop: Closed Gop
 * @broken_link: Broken link
 *
 * The Mpeg Video Group of Picture structure.
 */
struct _GstMpegVideoGop
{
  guint8 drop_frame_flag;

  guint8 hour, minute, second, frame;

  guint8 closed_gop;
  guint8 broken_link;
};

/**
 * GstMpegVideoTypeOffsetSize:
 * @type: the type of the packet that start at @offset
 * @offset: the offset of the packet start in bytes, it is the exact, start of the packet, no sync code included
 * @size: The size in bytes of the packet or -1 if the end wasn't found. It is the exact size of the packet, no sync code included
 *
 * A structure that contains the type of a packet, its offset and its size
 */
struct _GstMpegVideoTypeOffsetSize
{
  guint8 type;
  guint  offset;
  gint   size;
};

GList   *gst_mpeg_video_parse                         (const guint8 * data, gsize size, guint offset);

gboolean gst_mpeg_video_parse_sequence_header         (GstMpegVideoSequenceHdr * params,
                                                       const guint8 * data, gsize size, guint offset);

gboolean gst_mpeg_video_parse_picture_header          (GstMpegVideoPictureHdr* hdr,
                                                       const guint8 * data, gsize size, guint offset);

gboolean gst_mpeg_video_parse_picture_extension       (GstMpegVideoPictureExt *ext,
                                                       const guint8 * data, gsize size, guint offset);

gboolean gst_mpeg_video_parse_gop                     (GstMpegVideoGop * gop,
                                                       const guint8 * data, gsize size, guint offset);

gboolean gst_mpeg_video_parse_sequence_extension      (GstMpegVideoSequenceExt * seqext,
                                                       const guint8 * data, gsize size, guint offset);

gboolean gst_mpeg_video_parse_quant_matrix_extension  (GstMpegVideoQuantMatrixExt * quant,
                                                       const guint8 * data, gsize size, guint offset);

G_END_DECLS

#endif
