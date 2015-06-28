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

#ifndef __GST_FFMPEGAUDENC_H__
#define __GST_FFMPEGAUDENC_H__

G_BEGIN_DECLS

#include <gst/gst.h>
#include <gst/audio/gstaudioencoder.h>
#include <libavcodec/avcodec.h>

typedef struct _GstFFMpegAudEnc GstFFMpegAudEnc;

struct _GstFFMpegAudEnc
{
  GstAudioEncoder parent;

  AVCodecContext *context;
  gboolean opened;

  /* cache */
  gint bitrate;
  gint rtp_payload_size;
  gint compliance;

  /* other settings are copied over straight,
   * include a context here, rather than copy-and-past it from avcodec.h */
  AVCodecContext config;

  AVFrame *frame;

  GstAudioChannelPosition ffmpeg_layout[64];
  gboolean needs_reorder;
};

typedef struct _GstFFMpegAudEncClass GstFFMpegAudEncClass;

struct _GstFFMpegAudEncClass
{
  GstAudioEncoderClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

#define GST_TYPE_FFMPEGAUDENC \
  (gst_ffmpegaudenc_get_type())
#define GST_FFMPEGAUDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGAUDENC,GstFFMpegAudEnc))
#define GST_FFMPEGAUDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGAUDENC,GstFFMpegAudEncClass))
#define GST_IS_FFMPEGAUDENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGAUDENC))
#define GST_IS_FFMPEGAUDENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGAUDENC))

G_END_DECLS

#endif /* __GST_FFMPEGAUDENC_H__ */
