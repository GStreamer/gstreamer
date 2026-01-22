/* GStreamer
 * Copyright (C) 2021 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
 * Copyright (C) 2026 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
 *     Author: Stéphane Cerveau <scerveau@igalia.com>
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

#pragma once

#include <gst/codecparsers/gsth265parser.h>
#include <gst/video/gstvideoencoder.h>

#include "gsth26xgopmapper.h"

G_BEGIN_DECLS

typedef struct _GstH265Encoder GstH265Encoder;
typedef struct _GstH265EncoderClass GstH265EncoderClass;
typedef struct _GstH265EncoderFrame GstH265EncoderFrame;

typedef struct _GstH265LevelDescriptor GstH265LevelDescriptor;

/**
 * GstH265LevelDescriptor:
 * @name: level identifier string
 * @level_idc: the #GstH265Level
 * @max_luma_ps: max luma picture size
 * @max_cpb_tier_main: max coded picture buffer size in main tier
 * @max_cpb_tier_high: max coded picture buffer size in high tier
 * @max_slice_segments_per_picture: max slice segmentes per picture
 * @max_tile_rows: max tile rows
 * @max_tile_columns:: max tile columns
 * @max_luma_sr: max luma sample rate
 * @max_br_tier_main: max bitrate in main tier
 * @max_br_tier_high: max bitrate in high tier
 * @min_cr_main: max compression ratio in main tier
 * @min_cr_high: max compression ratio in high tier
 *
 * Since: 1.30
 */
struct _GstH265LevelDescriptor
{
  const gchar *name;
  GstH265Level level_idc;

  /* Table A.8. */
  guint32 max_luma_ps;
  guint32 max_cpb_tier_main;
  guint32 max_cpb_tier_high;
  guint32 max_slice_segments_per_picture;
  guint32 max_tile_rows;
  guint32 max_tile_columns;

  /* Table A.9. */
  guint32 max_luma_sr;
  guint32 max_br_tier_main;
  guint32 max_br_tier_high;
  guint32 min_cr_main;
  guint32 min_cr_high;
};

/**
 * GstH265Tier:
 * @GST_H265_TIER_MAIN: tier main
 * @GST_H265_TIER_HIGH: tier high
 *
 * Since: 1.30
 */
typedef enum
{
  GST_H265_TIER_AUTO,
  GST_H265_TIER_MAIN,
  GST_H265_TIER_HIGH,
  GST_H265_TIER_INVALID,
} GstH265Tier;

#define GST_TYPE_H265_ENCODER            (gst_h265_encoder_get_type())
#define GST_H265_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_H265_ENCODER, GstH265Encoder))
#define GST_H265_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_H265_ENCODER, GstH265EncoderClass))
#define GST_H265_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_H265_ENCODER, GstH265EncoderClass))
#define GST_IS_H265_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_H265_ENCODER))
#define GST_IS_H265_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_H265_ENCODER))

_GLIB_DEFINE_AUTOPTR_CHAINUP (GstH265Encoder, GstVideoEncoder)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstH265EncoderClass, g_type_class_unref)

/**
 * GstH265Encoder:
 *
 * Opaque #GstH265Encoder data structure.
 *
 * Since: 1.30
 */
struct _GstH265Encoder
{
  /*< private >*/
  GstVideoEncoder parent_instance;
};

/**
 * GstH265EncoderClass:
 *
 * The opaque #GstH265EncoderClass data structure.
 *
 * Since: 1.30
 */
struct _GstH265EncoderClass
{
  /*< private >*/
  GstVideoEncoderClass parent_class;

  /**
   * GstH265EncoderClass::negotiate:
   * @encoder: a #GstH265Encoder
   * @in_state: (transfer none): the input #GstVideoCodecState
   * @profile: (out) (type gint): the negotiated profile
   * @level: (out) (type gint): the negotiated level
   *
   * Optional. Allows the subclass to negotiate downstream the @profile and
   * @level. The default implementation will choose the most advanced profile
   * allowed. If the callee returns @level to zero, it will be guessed later.
   *
   * Returns: #GstFlowReturn accordingly
   *
   * Since: 1.30
   */
  GstFlowReturn      (*negotiate)                            (GstH265Encoder * encoder,
                                                              GstVideoCodecState * in_state,
                                                              GstH265Profile * profile,
                                                              GstH265Level * level,
                                                              GstH265Tier * tier);

  /**
   * GstH265EncoderClass::new_sequence:
   * @encoder: a #GstH265Encoder
   * @in_state: (transfer none): the input #GstVideoCodecState
   * @ptl:  the proposed @GstH265ProfileTierLevel
   *
   * Optional. Allows the subclass to open a session with the hardware
   * accelerator given the stream properties, such as video info (from
   * @in_state), and @ptl (#GstH265ProfileTierLevel) and to verify the
   * accelerator limitations. The callee ought return a valid @ptl. Normally the
   * callee should check and update `.tier_flag` and `.level_idc` members in
   * @ptl. If `.tier_flag` is 0xff it means auto and it must be updated.
   *
   *
   * The callee could use gst_h265_get_level_descriptor() to fetch a correct
   * level. Also, it could use gst_h265_profile_tier_level_get_profile() to get
   * the GStreamer's profile.
   *
   * Returns: #GstFlowReturn accordingly
   *
   * Since: 1.30
   */
  GstFlowReturn      (*new_sequence)                         (GstH265Encoder * encoder,
                                                              GstVideoCodecState * in_state,
                                                              GstH265ProfileTierLevel * ptl);

  /**
   * GstH265EncoderClass::new_parameters:
   * @encoder: a #GstH265Encoder
   * @vps: (transfer none): a #GstH265VPS
   * @sps: (transfer none): a #GstH265SPS
   * @pps: (transfer none): a #GstH265PPS
   *
   * Called when configuration changes and H.265 parameters change. The subclass
   * can modify them, carefully, according to the accelerator limitations, and
   * transfer them to their own structures. In particular the subclass have to
   * define the profile and its related @vps, @sps parameters. The method is expected
   * to call gst_video_encoder_set_output(), if needed, to (re)negotiate
   * downstream.
   *
   * Returns: #GstFlowReturn accordingly
   *
   * Since: 1.30
   */
  GstFlowReturn      (*new_parameters)                       (GstH265Encoder * encoder,
                                                              GstH265VPS * vps,
                                                              GstH265SPS * sps,
                                                              GstH265PPS * pps);

  /**
   * GstH265EncoderClass::new_output:
   * @encoder: a #GstH265Encoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   * @h265_frame: (transfer none): a #GstH265EncoderFrame
   *
   * Optional. Called whenever a new #GstH265EncoderFrame is created. Subclass
   * can set implementation specific user data on #GstH265EncoderFrame via
   * gst_h265_encoder_frame_set_user_data()
   *
   * Returns: #GstFlowReturn accordingly
   *
   * Since: 1.30
   */
  GstFlowReturn      (*new_output)                           (GstH265Encoder * encoder,
                                                              GstVideoCodecFrame * frame,
                                                              GstH265EncoderFrame * h265_frame);

  /**
   * GstH265EncoderClass::encode_frame:
   * @encoder: a #GstH265Encoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   * @h265_frame: (transfer none): a #GstH265EncoderFrame
   * @slice_hdr: (transfer none): a #GstH265SliceHdr
   * @list0: (transfer none) (element-type GstH265EncoderFrame): a list of
   *   reference #GstH265EncoderFrame pointers
   * @list1: (transfer none) (element-type GstH265EncoderFrame): a list of
   *   reference #GstH265EncoderFrame pointers
   *
   * Provide the frame to be encoded with the reference lists. If the
   * accelerated haven't completed the encoding, the callee can return
   * @GST_FLOW_OUTPUT_NOT_READY
   *
   * Returns: #GstFlowReturn accordingly
   *
   * Since: 1.30
   */
  GstFlowReturn      (*encode_frame)                         (GstH265Encoder * encoder,
                                                              GstVideoCodecFrame * frame,
                                                              GstH265EncoderFrame * h265_frame,
                                                              GstH265SliceHdr * slice_hdr,
                                                              GArray * list0,
                                                              GArray * list1);

  /**
   * GstH265EncoderClass::prepare_output:
   * @encoder: a #GstH265Encoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   *
   * Optional. It's called before pushing @frame downstream. It's intended to
   * add metadata, and prepend other units, to @frame and its user's data.
   *
   * Returns: #GstFlowReturn accordingly
   *
   * Since: 1.30
   */
  GstFlowReturn       (*prepare_output)                      (GstH265Encoder * encoder,
                                                              GstVideoCodecFrame * frame);

  /**
   * GstH265EncoderClass::reset:
   * @encoder: a #GstH265Encoder
   *
   * Optional. It's called when resetting the global state of the encoder.
   * Allows the subclass to re-initialize its internal variables.
   *
   * Since: 1.30
   */
  void                (*reset)                               (GstH265Encoder * encoder);

  /*< private > */
  gpointer padding[GST_PADDING_LARGE];
};

GType                gst_h265_encoder_get_type               (void);

void                 gst_h265_encoder_set_max_num_references (GstH265Encoder * self,
                                                              guint list0,
                                                              guint list1);
void                 gst_h265_encoder_set_b_pyramid_supported (GstH265Encoder * self,
                                                              gboolean supported);
void                 gst_h265_encoder_set_preferred_output_delay
                                                             (GstH265Encoder * self,
                                                              guint delay);

void                gst_h265_encoder_set_surface_size        (GstH265Encoder * self,
                                                              gint width,
                                                              gint height);

gboolean             gst_h265_encoder_is_live                (GstH265Encoder * self);

gboolean             gst_h265_encoder_reconfigure            (GstH265Encoder * self,
                                                              gboolean force);

guint32              gst_h265_encoder_get_idr_period         (GstH265Encoder * self);

guint32              gst_h265_encoder_get_num_b_frames       (GstH265Encoder * self);

gboolean             gst_h265_encoder_gop_is_b_pyramid       (GstH265Encoder * self);

gboolean             gst_h265_encoder_generate_gop_structure (GstH265Encoder * self,
                                                              guint * dpb_size);

const GstH265LevelDescriptor *
                     gst_h265_get_level_descriptor           (const GstH265ProfileTierLevel * ptl,
                                                              guint64 bitrate,
                                                              GstVideoInfo * in_info,
                                                              int n_slices,
                                                              int tile_rows,
                                                              int tile_cols,
                                                              int max_dec_frame_buffering);

gsize                gst_h265_calculate_coded_size           (const GstH265SPS * sps,
                                                              guint num_slices);

/* H265 encoder frame */

#define GST_TYPE_H265_ENCODER_FRAME    (gst_h265_encoder_frame_get_type ())
#define GST_IS_H265_ENCODER_FRAME(obj) (GST_IS_MINI_OBJECT_TYPE (obj, GST_TYPE_H265_ENCODER_FRAME))
#define GST_H265_ENCODER_FRAME(obj)    ((GstH265EncoderFrame *)obj)

/**
 * GstH265EncoderFrame:
 *
 * Represents a frame that is going to be encoded with H.265
 *
 * Since: 1.30
 */
struct _GstH265EncoderFrame
{
  /*< private >*/
  GstMiniObject parent;

  GstH26XGOP gop;

  gboolean last_frame;
  gint poc;

  gpointer user_data;
  GDestroyNotify user_data_destroy_notify;
};

GType                gst_h265_encoder_frame_get_type         (void);

GstH265EncoderFrame *gst_h265_encoder_frame_new              (void) G_GNUC_WARN_UNUSED_RESULT;

void                 gst_h265_encoder_frame_set_user_data    (GstH265EncoderFrame * frame,
                                                              gpointer user_data,
                                                              GDestroyNotify notify);

static inline gpointer
gst_h265_encoder_frame_get_user_data (GstH265EncoderFrame * frame)
{
  return frame->user_data;
}

static inline GstH265EncoderFrame *
gst_h265_encoder_frame_ref (GstH265EncoderFrame * frame)
{
  return (GstH265EncoderFrame *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (frame));
}

static inline void
gst_h265_encoder_frame_unref (void * frame)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (frame));
}

G_END_DECLS
