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

#ifndef __GST_FFMPEG_CODECMAP_H__
#define __GST_FFMPEG_CODECMAP_H__

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>

/*
 * _codecid_is_image() returns TRUE for image formats
 */
gboolean
gst_ffmpeg_codecid_is_image (enum AVCodecID codec_id);

/*
 * _codecid_to_caps () gets the GstCaps that belongs to
 * a certain CodecID for a pad with compressed data.
 */

GstCaps *
gst_ffmpeg_codecid_to_caps   (enum AVCodecID    codec_id,
                              AVCodecContext *context,
                              gboolean        encode);

/*
 * _codectype_to_caps () gets the GstCaps that belongs to
 * a certain AVMediaType for a pad with uncompressed data.
 */

GstCaps *
gst_ffmpeg_codectype_to_audio_caps (AVCodecContext *context,
                              enum AVCodecID codec_id,
				    gboolean encode,
				    AVCodec *codec);
GstCaps *
gst_ffmpeg_codectype_to_video_caps (AVCodecContext *context,
                              enum AVCodecID codec_id,
				    gboolean encode,
				    const AVCodec *codec);

/*
 * caps_to_codecid () transforms a GstCaps that belongs to
 * a pad for compressed data to (optionally) a filled-in
 * context and a codecID.
 */

enum AVCodecID
gst_ffmpeg_caps_to_codecid (const GstCaps  *caps,
                            AVCodecContext *context);

/*
 * caps_with_codecid () transforms a GstCaps for a known codec
 * ID into a filled-in context.
 */

void
gst_ffmpeg_caps_with_codecid (enum AVCodecID    codec_id,
                              enum AVMediaType  codec_type,
                              const GstCaps  *caps,
                              AVCodecContext *context);

/*
 * caps_with_codectype () transforms a GstCaps that belongs to
 * a pad for uncompressed data to a filled-in context.
 */

void
gst_ffmpeg_caps_with_codectype (enum AVMediaType  type,
                                const GstCaps  *caps,
                                AVCodecContext *context);

void
gst_ffmpeg_videoinfo_to_context (GstVideoInfo *info,
				 AVCodecContext *context);

void
gst_ffmpeg_audioinfo_to_context (GstAudioInfo *info,
				 AVCodecContext *context);

GstVideoFormat gst_ffmpeg_pixfmt_to_videoformat (enum AVPixelFormat pixfmt);
enum AVPixelFormat gst_ffmpeg_videoformat_to_pixfmt (GstVideoFormat format);

GstAudioFormat gst_ffmpeg_smpfmt_to_audioformat (enum AVSampleFormat sample_fmt,
                                                 GstAudioLayout * layout);

/*
 * _formatid_to_caps () is meant for muxers/demuxers, it
 * transforms a name (ffmpeg way of ID'ing these, why don't
 * they have unique numerical IDs?) to the corresponding
 * caps belonging to that mux-format
 */

GstCaps *
gst_ffmpeg_formatid_to_caps (const gchar *format_name);

/*
 * _formatid_get_codecids () can be used to get the codecIDs
 * (AV_CODEC_ID_NONE-terminated list) that fit that specific
 * output format.
 */

gboolean
gst_ffmpeg_formatid_get_codecids (const gchar *format_name,
                                  enum AVCodecID ** video_codec_list,
                                  enum AVCodecID ** audio_codec_list,
				  AVOutputFormat * plugin);

#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
gboolean
gst_ffmpeg_channel_layout_to_gst (const AVChannelLayout * channel_layout, gint channels,
    GstAudioChannelPosition * pos);
#else
gboolean
gst_ffmpeg_channel_layout_to_gst (guint64 channel_layout, gint channels,
    GstAudioChannelPosition * pos);
#endif

#endif /* __GST_FFMPEG_CODECMAP_H__ */
