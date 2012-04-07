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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */

#ifndef __GST_FFMPEGVIDENC_H__
#define __GST_FFMPEGVIDENC_H__

G_BEGIN_DECLS

typedef struct _GstFFMpegVidEnc GstFFMpegVidEnc;

struct _GstFFMpegVidEnc
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  AVCodecContext *context;
  AVFrame *picture;
  gboolean opened;
  gboolean discont;

  /* cache */
  gulong bitrate;
  gint me_method;
  gint gop_size;
  gulong buffer_size;
  gulong rtp_payload_size;

  guint8 *working_buf;
  gulong working_buf_size;

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

  /* for b-frame delay handling */
  GQueue *delay;

  /* other settings are copied over straight,
   * include a context here, rather than copy-and-past it from avcodec.h */
  AVCodecContext config;

  gboolean force_keyframe;
};

typedef struct _GstFFMpegVidEncClass GstFFMpegVidEncClass;

struct _GstFFMpegVidEncClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
  GstCaps *sinkcaps;
};

#define GST_TYPE_FFMPEGVIDENC \
  (gst_ffmpegvidenc_get_type())
#define GST_FFMPEGVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGVIDENC,GstFFMpegVidEnc))
#define GST_FFMPEGVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGVIDENC,GstFFMpegVidEncClass))
#define GST_IS_FFMPEGVIDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGVIDENC))
#define GST_IS_FFMPEGVIDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGVIDENC))

G_END_DECLS

#endif /* __GST_FFMPEGVIDENC_H__ */
