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
#ifndef __GST_FFMPEGAUDDEC_H__
#define __GST_FFMPEGAUDDEC_H__

G_BEGIN_DECLS

#include <gst/gst.h>
#include <libavcodec/avcodec.h>

#define MAX_TS_MASK 0xff

/* for each incomming buffer we keep all timing info in a structure like this.
 * We keep a circular array of these structures around to store the timing info.
 * The index in the array is what we pass as opaque data (to pictures) and
 * pts (to parsers) so that ffmpeg can remember them for us. */
typedef struct
{
  gint idx;
  GstClockTime dts;
  GstClockTime pts;
  GstClockTime duration;
  gint64 offset;
} GstTSInfo;

typedef struct _GstFFMpegAudDec GstFFMpegAudDec;
struct _GstFFMpegAudDec
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  /* decoding */
  AVCodecContext *context;
  gboolean opened;

  /* current output format */
  gint channels, samplerate, depth;
  GstAudioChannelPosition ffmpeg_layout[64], gst_layout[64];

  gboolean discont;
  gboolean clear_ts;

  /* for tracking DTS/PTS */
  GstClockTime next_out;

  /* parsing */
  gboolean turnoff_parser;      /* used for turning off aac raw parsing
                                 * See bug #566250 */
  AVCodecParserContext *pctx;
  GstBuffer *pcache;

  /* clipping segment */
  GstSegment segment;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;

  /* reverse playback queue */
  GList *queued;

  /* prevent reopening the decoder on GST_EVENT_CAPS when caps are same as last time. */
  GstCaps *last_caps;
};

typedef struct _GstFFMpegAudDecClass GstFFMpegAudDecClass;

struct _GstFFMpegAudDecClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegauddec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegAudDec))
#define GST_FFMPEGAUDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegAudDecClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGAUDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

G_END_DECLS

#endif
