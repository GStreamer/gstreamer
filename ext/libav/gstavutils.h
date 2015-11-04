/* GStreamer
 * Copyright (C) <2009> Edward Hervey <bilboed@bilboed.com>
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

#ifndef __GST_FFMPEG_UTILS_H__
#define __GST_FFMPEG_UTILS_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>

#include <gst/gst.h>

/*
 *Get the size of an picture
 */
int
gst_ffmpeg_avpicture_get_size (int pix_fmt, int width, int height);

/*
 * Fill in pointers in an AVPicture, aligned by 4 (required by X).
 */

int
gst_ffmpeg_avpicture_fill (AVPicture * picture,
                           uint8_t *   ptr,
                           enum AVPixelFormat pix_fmt,
                           int         width,
                           int         height);

/*
 * Convert from/to a GStreamer <-> FFMpeg timestamp.
 */
static inline guint64
gst_ffmpeg_time_ff_to_gst (gint64 pts, AVRational base)
{
  guint64 out;

  if (pts == AV_NOPTS_VALUE){
    out = GST_CLOCK_TIME_NONE;
  } else {
    AVRational bq = { 1, GST_SECOND };
    out = av_rescale_q (pts, base, bq);
  }

  return out;
}

static inline gint64
gst_ffmpeg_time_gst_to_ff (guint64 time, AVRational base)
{
  gint64 out;

  if (!GST_CLOCK_TIME_IS_VALID (time) || base.num == 0) {
    out = AV_NOPTS_VALUE;
  } else {
    AVRational bq = { 1, GST_SECOND };
    out = av_rescale_q (time, bq, base);
  }

  return out;
}

void 
gst_ffmpeg_init_pix_fmt_info(void);

int
gst_ffmpeg_auto_max_threads(void);

const gchar *
gst_ffmpeg_get_codecid_longname (enum AVCodecID codec_id);

gint
av_smp_format_depth(enum AVSampleFormat smp_fmt);

GstBuffer *
new_aligned_buffer (gint size);

#endif /* __GST_FFMPEG_UTILS_H__ */
