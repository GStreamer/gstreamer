/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#ifndef __GST_FFMPEGVIDDEC_H__
#define __GST_FFMPEGVIDDEC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <libavcodec/avcodec.h>

G_BEGIN_DECLS

#define GST_TYPE_FFMPEGVIDDEC           (gst_ffmpegviddec_get_type())
#define GST_FFMPEGVIDDEC(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGVIDDEC,GstFFMpegVidDec))
#define GST_FFMPEGVIDDEC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGVIDDEC,GstFFMpegVidDecClass))
#define GST_FFMPEGVIDDEC_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_FFMPEGVIDDEC, GstFFMpegVidDecClass))
#define GST_IS_FFMPEGVIDDEC(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGVIDDEC))
#define GST_IS_FFMPEGVIDDEC_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGVIDDEC))

/**
 * GstFFMpegVidDec:
 *
 * The #GstFFMpegVidDec data structure.
 *
 * Since: 1.22
 */
typedef struct _GstFFMpegVidDec GstFFMpegVidDec;

struct _GstFFMpegVidDec
{
  GstVideoDecoder parent;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  /* decoding */
  AVCodecContext *context;
  AVFrame *picture;
  GstVideoMultiviewMode picture_multiview_mode;
  GstVideoMultiviewFlags picture_multiview_flags;
  gint stride[AV_NUM_DATA_POINTERS];
  gboolean opened;

  /* current output pictures */
  enum AVPixelFormat pic_pix_fmt;
  gint pic_width;
  gint pic_height;
  gint pic_par_n;
  gint pic_par_d;
  gint pic_interlaced;
  /* GST_VIDEO_BUFFER_FLAG_RFF | GST_VIDEO_BUFFER_FLAG_TFF */
  gint pic_field_order;
  gboolean pic_field_order_changed;
  GstVideoMultiviewMode cur_multiview_mode;
  GstVideoMultiviewFlags cur_multiview_flags;
  /* current context */
  gint ctx_ticks;
  gint ctx_time_d;
  gint ctx_time_n;
  GstBuffer *palette;

  guint8 *padded;
  gint padded_size;

  /* some properties */
  enum AVDiscard skip_frame;
  gint lowres;
  gboolean direct_rendering;
  int max_threads;
  gboolean output_corrupt;
  guint thread_type;
  GstAvCodecCompliance std_compliance;

  GstCaps *last_caps;

  /* Internally used for direct rendering */
  GstBufferPool *internal_pool;
  gint pool_width;
  gint pool_height;
  enum AVPixelFormat pool_format;
  GstVideoInfo pool_info;
};

typedef struct _GstFFMpegVidDecClass GstFFMpegVidDecClass;

struct _GstFFMpegVidDecClass
{
  GstVideoDecoderClass parent_class;

  const AVCodec *in_plugin;
};

GType gst_ffmpegviddec_get_type (void);

G_END_DECLS

#endif
