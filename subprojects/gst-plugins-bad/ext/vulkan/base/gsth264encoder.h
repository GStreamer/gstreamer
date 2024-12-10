/* GStreamer
 * Copyright (C) 2021 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
 * Copyright (C) 2023 Michael Grzeschik <m.grzeschik@pengutronix.de>
 * Copyright (C) 2021, 2025 Igalia, S.L.
 *     Author: Stéphane Cerveau <scerveau@igalia.com>
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#include <gst/codecparsers/gsth264parser.h>
#include <gst/video/gstvideoencoder.h>

G_BEGIN_DECLS

typedef struct _GstH264Encoder GstH264Encoder;
typedef struct _GstH264EncoderClass GstH264EncoderClass;
typedef struct _GstH264EncoderFrame GstH264EncoderFrame;
typedef struct _GstH264GOPFrame GstH264GOPFrame;

typedef struct _GstH264LevelDescriptor GstH264LevelDescriptor;

/**
 * GstH264LevelDescriptor:
 * @name: level identifier string
 * @level_idc: the #GstH264Level
 * @max_mbps: maximum macroblock processing rate (mb/s)
 * @max_fs: maximum frame size (mb)
 * @max_dpb_mps: maximum decoded picture buffer size (mb)
 * @max_br: maximum bitrate (bits/s)
 * @max_cpb: maximum CPB size
 * @min_cr: minimum compression ration
 *
 * Since: 1.28
 */
struct _GstH264LevelDescriptor
{
  const gchar *name;
  GstH264Level level_idc;
  guint32 max_mbps;
  guint32 max_fs;
  guint32 max_dpb_mbs;
  guint32 max_br;
  guint32 max_cpb;
  guint32 min_cr;
};

#define GST_TYPE_H264_ENCODER            (gst_h264_encoder_get_type())
#define GST_H264_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_H264_ENCODER, GstH264Encoder))
#define GST_H264_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_H264_ENCODER, GstH264EncoderClass))
#define GST_H264_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_H264_ENCODER, GstH264EncoderClass))
#define GST_IS_H264_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_H264_ENCODER))
#define GST_IS_H264_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_H264_ENCODER))

_GLIB_DEFINE_AUTOPTR_CHAINUP (GstH264Encoder, GstVideoEncoder)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstH264EncoderClass, g_type_class_unref)

struct _GstH264Encoder
{
  GstVideoEncoder parent_instance;
};

/**
 * GstH264EncoderClass:
 *
 * The opaque #GstH264EncoderClass data structure.
 *
 * Since: 1.28
 */
struct _GstH264EncoderClass
{
  GstVideoEncoderClass parent_class;

  /**
   * GstH264Encoder::negotiate:
   * @encoder: a #GstH264Encoder
   * @in_state: (transfer none): the input #GstVideoCodecState
   * @profile: (out): the negotiated profile
   * @level: (out): the negotiated level
   *
   * Optional. Allows the subclass to negotiate downstream the @profile and
   * @level. The default implementation will choose the most advanced profile
   * allowed. If the callee returns @level to zero, it will be guessed later.
   *
   * Since: 1.28
   */
  GstFlowReturn      (*negotiate)                            (GstH264Encoder * encoder,
                                                              GstVideoCodecState * in_state,
                                                              GstH264Profile * profile,
                                                              GstH264Level * level);

  /**
   * GstH264Encoder::new_sequence:
   * @encoder: a #GstH264Encoder
   * @in_state: (transfer none): the input #GstVideoCodecState
   * @profile:  the negotiated profile
   * @level: (out): the negotiated level
   *
   * Optional. Allows the subclass to open a session with the hardware
   * accelerator given the stream properties, such as video info (from
   * @in_state), @profile and @level, and to verify the accelerator limitations.
   * If the callee returns @level to zero, it will be guessed later.
   *
   * Since: 1.28
   */
  GstFlowReturn      (*new_sequence)                         (GstH264Encoder * encoder,
                                                              GstVideoCodecState * in_state,
                                                              GstH264Profile profile,
                                                              GstH264Level * level);

  /**
   * GstH264Encoder::new_parameters:
   * @encoder: a #GstH264Encoder
   * @input_state: (transfer none): the input #GstVideoCodecState
   * @sps: (transfer none): a #GstH264SPS
   * @pps: (transfer none): a #GstH264PPS
   *
   * Called when configuration changes and H.264 parameters change. The subclass
   * can modify them, carefully, according to the accelerator limitations, and
   * transfer them to their own structures. In particular the subclass have to
   * define the profile and its related @sps parameters. The method is expected
   * to call gst_video_encoder_set_output(), if needed, to (re)negotiate
   * downstream.
   *
   * Since: 1.28
   */
  GstFlowReturn      (*new_parameters)                       (GstH264Encoder * encoder,
                                                              GstH264SPS * sps,
                                                              GstH264PPS * pps);

  /**
   * GstH264EncoderClass::new_output:
   * @encoder: a #GstH264Encoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   * @h264_frame: (transfer none): a #GstH264EncoderFrame
   *
   * Optional. Called whenever a new #GstH264EncoderFrame is created. Subclass
   * can set implementation specific user data on #GstH264EncoderFrame via
   * gst_h264_encoder_frame_set_user_data()
   *
   * Since: 1.28
   */
  GstFlowReturn      (*new_output)                           (GstH264Encoder * encoder,
                                                              GstVideoCodecFrame * frame,
                                                              GstH264EncoderFrame * h264_frame);

  /**
   * GstH264EncoderClass::encode_frame:
   * @encoder: a #GstH264Encoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   * @h264_frame: (transfer none): a #GstH264EncoderFrame
   * @slice_hdr: (transfer none): a #GstH264SliceHdr
   * @list0: (transfer none) (element-type GstH264EncoderFrame): a list of
   *   reference #GstH264EncoderFrame pointers
   * @list1: (transfer none) (element-type GstH264EncoderFrame): a list of
   *   reference #GstH264EncoderFrame pointers
   *
   * Provide the frame to be encoded with the reference lists. If the
   * accelerated haven't completed the encoding, the callee can return
   * @GST_FLOW_OUTPUT_NOT_READY
   *
   * Since: 1.28
   */
  GstFlowReturn      (*encode_frame)                         (GstH264Encoder * encoder,
                                                              GstVideoCodecFrame * frame,
                                                              GstH264EncoderFrame * h264_frame,
                                                              GstH264SliceHdr * slice_hdr,
                                                              GArray * list0,
                                                              GArray * list1);

  /**
   * GstH264EncoderClass::prepare_output:
   * @encoder: a #GstH264Encoder
   * @frame: (transfer none): a #GstVideoCodecFrame
   *
   * Optional. It's called before pushing @frame downstream. It's intended to
   * add metadata, and prepend other units, to @frame and its user's data.
   *
   * Since: 1.28
   */
  GstFlowReturn       (*prepare_output)                      (GstH264Encoder * encoder,
                                                              GstVideoCodecFrame * frame);

  /**
   * GstH264EncoderClass::reset:
   * @encoder: a #GstH264Encoder
   *
   * Optional. It's called when resetting the global state of the encoder.
   * Allows the subclass to re-initialize its internal variables.
   *
   * Since: 1.28
   */
  void                (*reset)                               (GstH264Encoder * encoder);

  /*< private > */
  gpointer padding[GST_PADDING_LARGE];
};

GType                gst_h264_encoder_get_type               (void);

void                 gst_h264_encoder_set_max_num_references (GstH264Encoder * self,
                                                              guint list0,
                                                              guint list1);

void                 gst_h264_encoder_set_preferred_output_delay
                                                             (GstH264Encoder * self,
                                                              guint delay);

gboolean             gst_h264_encoder_is_live                (GstH264Encoder * self);

gboolean             gst_h264_encoder_reconfigure            (GstH264Encoder * self,
                                                              gboolean force);

guint32              gst_h264_encoder_get_idr_period         (GstH264Encoder * self);

guint32              gst_h264_encoder_get_num_b_frames       (GstH264Encoder * self);

gboolean             gst_h264_encoder_gop_is_b_pyramid       (GstH264Encoder * self);

const GstH264LevelDescriptor *gst_h264_get_level_descriptor  (GstH264Profile profile,
                                                              guint64 bitrate,
                                                              GstVideoInfo * in_info,
                                                              int max_dec_frame_buffering);

guint                gst_h264_get_cpb_nal_factor             (GstH264Profile profile);

gsize                gst_h264_calculate_coded_size           (GstH264SPS * sps,
                                                              guint num_slices);

/* H264 encoder frame */

#define GST_TYPE_H264_ENCODER_FRAME    (gst_h264_encoder_frame_get_type ())
#define GST_IS_H264_ENCODER_FRAME(obj) (GST_IS_MINI_OBJECT_TYPE (obj, GST_TYPE_H264_ENCODE_FRAME))
#define GST_H264_ENCODER_FRAME(obj)    ((GstH264EncoderFrame *)obj)

/**
 * GstH264GOPFrame:
 *
 * Description of an H.264 frame in the Group Of Pictures (GOP).
 *
 * Since: 1.28
 */
struct _GstH264GOPFrame
{
  /*< private >*/
  GstH264SliceType slice_type;
  gboolean is_ref;
  guint8 pyramid_level;

  /* Only for b pyramid */
  gint left_ref_poc_diff;
  gint right_ref_poc_diff;
};

/**
 * GstH264EncoderFrame:
 *
 * Represents a frame that is going to be encoded with H.264
 *
 * Since: 1.28
 */
struct _GstH264EncoderFrame
{
  GstMiniObject parent;

  /*< private >*/
  GstH264GOPFrame type;

  /* Number of ref frames within current GOP. H264's frame number. */
  guint16 gop_frame_num;
  gboolean last_frame;
  gint poc;
  guint32 idr_pic_id;
  gboolean force_idr;

  /* The pic_num will be marked as unused_for_reference, which is replaced by
   * this frame. -1 if we do not need to care about it explicitly. */
  gint32 unused_for_reference_pic_num;

  gpointer user_data;
  GDestroyNotify user_data_destroy_notify;
};

GType                gst_h264_encoder_frame_get_type         (void);

GstH264EncoderFrame *gst_h264_encoder_frame_new              (void);

void                 gst_h264_encoder_frame_set_user_data    (GstH264EncoderFrame * frame,
                                                              gpointer user_data,
                                                              GDestroyNotify notify);

static inline gpointer
gst_h264_encoder_frame_get_user_data (GstH264EncoderFrame * frame)
{
  return frame->user_data;
}

static inline GstH264EncoderFrame *
gst_h264_encode_frame_ref (GstH264EncoderFrame * frame)
{
  return (GstH264EncoderFrame *) gst_mini_object_ref (GST_MINI_OBJECT_CAST (frame));
}

static inline void
gst_h264_encoder_frame_unref (void * frame)
{
  gst_mini_object_unref (GST_MINI_OBJECT_CAST (frame));
}

G_END_DECLS
