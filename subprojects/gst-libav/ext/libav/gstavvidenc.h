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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <libavcodec/avcodec.h>

G_BEGIN_DECLS

typedef struct _GstFFMpegVidEnc GstFFMpegVidEnc;

struct _GstFFMpegVidEnc
{
  GstVideoEncoder parent;

  GstVideoCodecState *input_state;

  AVCodecContext *context;
  AVFrame *picture;
  GstClockTime pts_offset;
  gboolean opened;
  gboolean need_reopen;
  gboolean discont;
  guint pass;
  gfloat quantizer;

  /* statistics file */
  gchar *filename;
  FILE *file;

  /* cache */
  guint8 *working_buf;
  gsize working_buf_size;

  AVCodecContext *refcontext;
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
