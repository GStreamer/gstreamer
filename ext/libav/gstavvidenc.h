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

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */

#ifndef __GST_FFMPEGVIDENC_H__
#define __GST_FFMPEGVIDENC_H__

G_BEGIN_DECLS

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>
#include <libavcodec/avcodec.h>

typedef struct _GstFFMpegVidEnc GstFFMpegVidEnc;

struct _GstFFMpegVidEnc
{
  GstVideoEncoder parent;

  GstVideoCodecState *input_state;

  AVCodecContext *context;
  AVFrame *picture;
  gboolean opened;
  gboolean discont;

  /* cache */
  gint bitrate;
  gint me_method;
  gint gop_size;
  gint buffer_size;
  gint rtp_payload_size;
  gint compliance;
  gint max_threads;

  guint8 *working_buf;
  gsize working_buf_size;

  /* settings with some special handling */
  guint pass;
  gfloat quantizer;
  gchar *filename;
  guint lmin;
  guint lmax;
  gint max_key_interval;
  gboolean interlaced;

  /* statistics file */
  FILE *file;

  /* other settings are copied over straight,
   * include a context here, rather than copy-and-past it from avcodec.h */
  AVCodecContext config;
};

typedef struct _GstFFMpegVidEncClass GstFFMpegVidEncClass;

struct _GstFFMpegVidEncClass
{
  GstVideoEncoderClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

G_END_DECLS

#endif /* __GST_FFMPEGVIDENC_H__ */
