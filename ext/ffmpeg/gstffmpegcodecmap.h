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
#include <ffmpeg/avcodec.h>
#endif
#include <gst/gst.h>

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
 * a certain CodecType for a pad with uncompressed data.
 */

GstCaps *
gst_ffmpeg_codectype_to_caps (enum CodecType  codec_type,
                              AVCodecContext *context);

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
			      enum CodecType  codec_type,
			      const GstCaps  *caps,
			      AVCodecContext *context);

/*
 * caps_with_codectype () transforms a GstCaps that belongs to
 * a pad for uncompressed data to a filled-in context.
 */

void
gst_ffmpeg_caps_with_codectype (enum CodecType  type,
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

/*
 * Since FFMpeg has such really cool and useful descriptions
 * of its codecs, we use our own...
 */

G_CONST_RETURN gchar *
gst_ffmpeg_get_codecid_longname (enum CodecID codec_id);

/*
 * FFMPEG debugging function; maybe move to a different file.
 */

GST_DEBUG_CATEGORY_EXTERN (ffmpeg_debug);

static void
gst_ffmpeg_log_callback (void * ptr, int level, const char * fmt, va_list vl);

#endif /* __GST_FFMPEG_CODECMAP_H__ */

