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

#ifndef __GST_FFMPEG_H__
#define __GST_FFMPEG_H__

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_EXTERN (ffmpeg_debug);
#define GST_CAT_DEFAULT ffmpeg_debug

G_BEGIN_DECLS

extern gboolean gst_ffmpegdemux_register (GstPlugin * plugin);
extern gboolean gst_ffmpegauddec_register (GstPlugin * plugin);
extern gboolean gst_ffmpegviddec_register (GstPlugin * plugin);
extern gboolean gst_ffmpegaudenc_register (GstPlugin * plugin);
extern gboolean gst_ffmpegvidenc_register (GstPlugin * plugin);
extern gboolean gst_ffmpegmux_register (GstPlugin * plugin);
#if 0
extern gboolean gst_ffmpegscale_register (GstPlugin * plugin);
extern gboolean gst_ffmpegaudioresample_register (GstPlugin * plugin);
#endif
extern gboolean gst_ffmpegdeinterlace_register (GstPlugin * plugin);

int gst_ffmpeg_avcodec_open (AVCodecContext *avctx, AVCodec *codec);
int gst_ffmpeg_avcodec_close (AVCodecContext *avctx);
int gst_ffmpeg_av_find_stream_info(AVFormatContext *ic);

G_END_DECLS

/* use GST_FFMPEG URL_STREAMHEADER with URL_WRONLY if the first
 * buffer should be used as streamheader property on the pad's caps. */
#define GST_FFMPEG_URL_STREAMHEADER 16

#endif /* __GST_FFMPEG_H__ */
