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

/* Introduced since ffmpeg version 4.3
 *
 * Note: Not all ffmpeg encoders seem to be reusable after flushing/draining.
 * So if ffmpeg encoder doesn't support it, we should reopen encoding session.
 *
 * Before ffmpeg 4.3, avcodec_flush_buffers() was implemented in
 * libavcodec/decodec.c but it was moved to libavcodec/utils.c and it would be
 * accepted if encoder supports AV_CODEC_CAP_ENCODER_FLUSH flag.
 * That implies that avcodec_flush_buffers() wasn't intended to be working
 * properly for encoders.
 */
#ifndef AV_CODEC_CAP_ENCODER_FLUSH
/*
 * This encoder can be flushed using avcodec_flush_buffers(). If this flag is
 * not set, the encoder must be closed and reopened to ensure that no frames
 * remain pending.
 */
#define AV_CODEC_CAP_ENCODER_FLUSH   (1 << 21)
#endif

/*
 *Get the size of an picture
 */
int
gst_ffmpeg_avpicture_get_size (int pix_fmt, int width, int height);

/*
 * Fill in pointers in an AVFrame, aligned by 4 (required by X).
 */

int
gst_ffmpeg_avpicture_fill (AVFrame * picture,
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

/**
 * GstAvCodecCompliance:
 *
 * Since: 1.22
 */
typedef enum
{
  GST_AV_CODEC_COMPLIANCE_AUTO = G_MAXINT,
  GST_AV_CODEC_COMPLIANCE_VERY_STRICT = FF_COMPLIANCE_VERY_STRICT,
  GST_AV_CODEC_COMPLIANCE_STRICT = FF_COMPLIANCE_STRICT,
  GST_AV_CODEC_COMPLIANCE_NORMAL = FF_COMPLIANCE_NORMAL,
  GST_AV_CODEC_COMPLIANCE_UNOFFICIAL = FF_COMPLIANCE_UNOFFICIAL,
  GST_AV_CODEC_COMPLIANCE_EXPERIMENTAL = FF_COMPLIANCE_EXPERIMENTAL,
} GstAvCodecCompliance;

#define GST_TYPE_AV_CODEC_COMPLIANCE (gst_av_codec_compliance_get_type())
GType gst_av_codec_compliance_get_type (void);


#endif /* __GST_FFMPEG_UTILS_H__ */
