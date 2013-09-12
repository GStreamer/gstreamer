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

G_BEGIN_DECLS

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <libavcodec/avcodec.h>

typedef struct _GstFFMpegVidDec GstFFMpegVidDec;
struct _GstFFMpegVidDec
{
  GstVideoDecoder parent;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  /* decoding */
  AVCodecContext *context;
  AVFrame *picture;
  gint stride[AV_NUM_DATA_POINTERS];
  gboolean opened;

  /* current context */
  enum PixelFormat ctx_pix_fmt;
  gint ctx_width;
  gint ctx_height;
  gint ctx_par_n;
  gint ctx_par_d;
  gint ctx_ticks;
  gint ctx_time_d;
  gint ctx_time_n;
  gint ctx_interlaced;
  GstBuffer *palette;

  guint8 *padded;
  guint padded_size;

  gboolean current_dr;          /* if direct rendering is enabled */

  /* some properties */
  enum AVDiscard skip_frame;
  gint lowres;
  gboolean direct_rendering;
  gboolean debug_mv;
  int max_threads;

  gboolean is_realvideo;

  GstCaps *last_caps;
};

typedef struct _GstFFMpegVidDecClass GstFFMpegVidDecClass;

struct _GstFFMpegVidDecClass
{
  GstVideoDecoderClass parent_class;

  AVCodec *in_plugin;
};

#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegviddec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegVidDec))
#define GST_FFMPEGVIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegVidDecClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGVIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

G_END_DECLS

#endif
