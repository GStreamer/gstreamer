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

#ifndef __GST_FFMPEG_CODECMAP_H__
#define __GST_FFMPEG_CODECMAP_H__

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <libavcodec/avcodec.h>
#endif
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>

/*
 * _codecid_to_caps () gets the GstCaps that belongs to
 * a certain CodecID for a pad with compressed data.
 */

GstCaps *
gst_ffmpeg_codecid_to_caps   (enum CodecID    codec_id,
                              AVCodecContext *context,
                              gboolean        encode);

/*
 * _codectype_to_caps () gets the GstCaps that belongs to
 * a certain AVMediaType for a pad with uncompressed data.
 */

GstCaps *
gst_ffmpeg_codectype_to_caps (enum AVMediaType  codec_type,
                              AVCodecContext *context, 
                              enum CodecID codec_id,
                              gboolean encode);
GstCaps *
gst_ffmpeg_codectype_to_audio_caps (AVCodecContext *context, 
                              enum CodecID codec_id,
				    gboolean encode,
				    AVCodec *codec);
GstCaps *
gst_ffmpeg_codectype_to_video_caps (AVCodecContext *context, 
                              enum CodecID codec_id,
				    gboolean encode,
				    AVCodec *codec);

/*
 * caps_to_codecid () transforms a GstCaps that belongs to
 * a pad for compressed data to (optionally) a filled-in
 * context and a codecID.
 */

enum CodecID
gst_ffmpeg_caps_to_codecid (const GstCaps  *caps,
                            AVCodecContext *context);

/*
 * caps_with_codecid () transforms a GstCaps for a known codec
 * ID into a filled-in context.
 */

void
gst_ffmpeg_caps_with_codecid (enum CodecID    codec_id,
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

/*
 * _formatid_to_caps () is meant for muxers/demuxers, it
 * transforms a name (ffmpeg way of ID'ing these, why don't
 * they have unique numerical IDs?) to the corresponding
 * caps belonging to that mux-format
 */

GstCaps *
gst_ffmpeg_formatid_to_caps (const gchar *format_name);

GstVideoFormat
gst_ffmpeg_pixfmt_to_video_format (enum PixelFormat pix_fmt);

/* Convert a FFMPEG Pixel Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

GstCaps *
gst_ffmpeg_pixfmt_to_caps (enum PixelFormat pix_fmt, AVCodecContext * context, enum CodecID codec_id);

/*
 * _formatid_get_codecids () can be used to get the codecIDs
 * (CODEC_ID_NONE-terminated list) that fit that specific
 * output format.
 */

gboolean
gst_ffmpeg_formatid_get_codecids (const gchar *format_name,
                                  enum CodecID ** video_codec_list,
                                  enum CodecID ** audio_codec_list,
				  AVOutputFormat * plugin);


gboolean
gst_ffmpeg_channel_layout_to_gst (AVCodecContext * context,
    GstAudioChannelPosition * pos);

#endif /* __GST_FFMPEG_CODECMAP_H__ */
