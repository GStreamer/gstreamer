/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (c) 2002-2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>

#include "gstav.h"
#include "gstavcodecmap.h"

#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstdsd.h>
#include <gst/pbutils/codec-utils.h>

/* IMPORTANT: Keep this sorted by the ffmpeg channel masks */
static const struct
{
  guint64 ff;
  GstAudioChannelPosition gst;
} _ff_to_gst_layout[] = {
  {
      AV_CH_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
      AV_CH_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
      AV_CH_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
      AV_CH_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE1}, {
      AV_CH_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
      AV_CH_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
        AV_CH_FRONT_LEFT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
        AV_CH_FRONT_RIGHT_OF_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
      AV_CH_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
      AV_CH_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
      AV_CH_SIDE_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
      AV_CH_TOP_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_CENTER}, {
      AV_CH_TOP_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT}, {
      AV_CH_TOP_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER}, {
      AV_CH_TOP_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT}, {
      AV_CH_TOP_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT}, {
      AV_CH_TOP_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER}, {
      AV_CH_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT}, {
      AV_CH_STEREO_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
      AV_CH_STEREO_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}
};

static guint64
gst_ffmpeg_channel_positions_to_layout (GstAudioChannelPosition * pos,
    gint channels)
{
  gint i, j;
  guint64 ret = 0;
  gint channels_found = 0;

  if (!pos)
    return 0;

  if (channels == 1 && pos[0] == GST_AUDIO_CHANNEL_POSITION_MONO)
    return AV_CH_LAYOUT_MONO;

  for (i = 0; i < channels; i++) {
    for (j = 0; j < G_N_ELEMENTS (_ff_to_gst_layout); j++) {
      if (_ff_to_gst_layout[j].gst == pos[i]) {
        ret |= _ff_to_gst_layout[j].ff;
        channels_found++;
        break;
      }
    }
  }

  if (channels_found != channels)
    return 0;
  return ret;
}

gboolean
gst_ffmpeg_channel_layout_to_gst (guint64 channel_layout, gint channels,
    GstAudioChannelPosition * pos)
{
  guint nchannels = 0;
  gboolean none_layout = FALSE;

  if (channel_layout == 0 || channels > 64) {
    nchannels = channels;
    none_layout = TRUE;
  } else {
    guint i, j;

    /* Special path for mono, as AV_CH_LAYOUT_MONO is the same
     * as FRONT_CENTER but we distinguish between the two in
     * GStreamer
     */
    if (channels == 1 && channel_layout == AV_CH_LAYOUT_MONO) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
      return TRUE;
    }

    for (i = 0; i < 64; i++) {
      if ((channel_layout & (G_GUINT64_CONSTANT (1) << i)) != 0) {
        nchannels++;
      }
    }

    if (nchannels != channels) {
      GST_ERROR ("Number of channels is different (%u != %u)", channels,
          nchannels);
      nchannels = channels;
      none_layout = TRUE;
    } else {

      for (i = 0, j = 0; i < G_N_ELEMENTS (_ff_to_gst_layout); i++) {
        if ((channel_layout & _ff_to_gst_layout[i].ff) != 0) {
          pos[j++] = _ff_to_gst_layout[i].gst;

          if (_ff_to_gst_layout[i].gst == GST_AUDIO_CHANNEL_POSITION_NONE)
            none_layout = TRUE;
        }
      }

      if (j != nchannels) {
        GST_WARNING
            ("Unknown channels in channel layout - assuming NONE layout");
        none_layout = TRUE;
      }
    }
  }

  if (!none_layout
      && !gst_audio_check_valid_channel_positions (pos, nchannels, FALSE)) {
    GST_ERROR ("Invalid channel layout %" G_GUINT64_FORMAT
        " - assuming NONE layout", channel_layout);
    none_layout = TRUE;
  }

  if (none_layout) {
    if (nchannels == 1) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
    } else if (nchannels == 2) {
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    } else {
      guint i;

      for (i = 0; i < nchannels && i < 64; i++)
        pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    }
  }

  return TRUE;
}

static gboolean
_gst_value_list_contains (const GValue * list, const GValue * value)
{
  guint i, n;
  const GValue *tmp;

  n = gst_value_list_get_size (list);
  for (i = 0; i < n; i++) {
    tmp = gst_value_list_get_value (list, i);
    if (gst_value_compare (value, tmp) == GST_VALUE_EQUAL)
      return TRUE;
  }

  return FALSE;
}

static void
gst_ffmpeg_video_set_pix_fmts (GstCaps * caps, const enum AVPixelFormat *fmts)
{
  GValue va = { 0, };
  GValue v = { 0, };
  GstVideoFormat format;

  if (!fmts || fmts[0] == -1) {
    gint i;

    g_value_init (&va, GST_TYPE_LIST);
    g_value_init (&v, G_TYPE_STRING);
    for (i = 0; i <= AV_PIX_FMT_NB; i++) {
      format = gst_ffmpeg_pixfmt_to_videoformat (i);
      if (format == GST_VIDEO_FORMAT_UNKNOWN)
        continue;
      g_value_set_string (&v, gst_video_format_to_string (format));
      gst_value_list_append_value (&va, &v);
    }
    gst_caps_set_value (caps, "format", &va);
    g_value_unset (&v);
    g_value_unset (&va);
    return;
  }

  /* Only a single format */
  g_value_init (&va, GST_TYPE_LIST);
  g_value_init (&v, G_TYPE_STRING);
  while (*fmts != -1) {
    format = gst_ffmpeg_pixfmt_to_videoformat (*fmts);
    if (format != GST_VIDEO_FORMAT_UNKNOWN) {
      g_value_set_string (&v, gst_video_format_to_string (format));
      /* Only append values we don't have yet */
      if (!_gst_value_list_contains (&va, &v))
        gst_value_list_append_value (&va, &v);
    }
    fmts++;
  }
  if (gst_value_list_get_size (&va) == 1) {
    /* The single value is still in v */
    gst_caps_set_value (caps, "format", &v);
  } else if (gst_value_list_get_size (&va) > 1) {
    gst_caps_set_value (caps, "format", &va);
  }
  g_value_unset (&v);
  g_value_unset (&va);
}

/* this macro makes a caps width fixed or unfixed width/height
 * properties depending on whether we've got a context.
 *
 * See below for why we use this.
 *
 * We should actually do this stuff at the end, like in riff-media.c,
 * but I'm too lazy today. Maybe later.
 */
static GstCaps *
gst_ff_vid_caps_new (AVCodecContext * context, const AVCodec * codec,
    enum AVCodecID codec_id, gboolean encode, const char *mimetype,
    const char *fieldname, ...)
{
  GstCaps *caps = NULL;
  va_list var_args;
  gint i;

  GST_LOG ("context:%p, codec_id:%d, mimetype:%s", context, codec_id, mimetype);

  /* fixed, non probing context */
  if (context != NULL && context->width != -1) {
    gint num, denom;

    caps = gst_caps_new_simple (mimetype,
        "width", G_TYPE_INT, context->width,
        "height", G_TYPE_INT, context->height, NULL);

    num = context->framerate.num;
    denom = context->framerate.den;

    if (!denom) {
      GST_LOG ("invalid framerate: %d/0, -> %d/1", num, num);
      denom = 1;
    }
    if (gst_util_fraction_compare (num, denom, 1000, 1) > 0) {
      GST_LOG ("excessive framerate: %d/%d, -> 0/1", num, denom);
      num = 0;
      denom = 1;
    }
    GST_LOG ("setting framerate: %d/%d", num, denom);
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_FRACTION, num, denom, NULL);
  } else if (encode) {
    /* so we are after restricted caps in this case */
    switch (codec_id) {
      case AV_CODEC_ID_H261:
      {
        caps = gst_caps_new_simple (mimetype,
            "width", G_TYPE_INT, 352,
            "height", G_TYPE_INT, 288,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
        gst_caps_append (caps, gst_caps_new_simple (mimetype,
                "width", G_TYPE_INT, 176,
                "height", G_TYPE_INT, 144,
                "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL));
        break;
      }
      case AV_CODEC_ID_H263:
      {
        /* 128x96, 176x144, 352x288, 704x576, and 1408x1152. slightly reordered
         * because we want automatic negotiation to go as close to 320x240 as
         * possible. */
        const static gint widths[] = { 352, 704, 176, 1408, 128 };
        const static gint heights[] = { 288, 576, 144, 1152, 96 };
        GstCaps *temp;
        gint n_sizes = G_N_ELEMENTS (widths);

        caps = gst_caps_new_empty ();
        for (i = 0; i < n_sizes; i++) {
          temp = gst_caps_new_simple (mimetype,
              "width", G_TYPE_INT, widths[i],
              "height", G_TYPE_INT, heights[i],
              "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

          gst_caps_append (caps, temp);
        }
        break;
      }
      case AV_CODEC_ID_DVVIDEO:
      {
        static struct
        {
          const gchar *csp;
          gint width, height;
          gint par_n, par_d;
          gint framerate_n, framerate_d;
        } profiles[] = {
          {
              "Y41B", 720, 480, 8, 9, 30000, 1001}, {
              "Y41B", 720, 480, 32, 27, 30000, 1001}, {
              "Y42B", 720, 480, 8, 9, 30000, 1001}, {
              "Y42B", 720, 480, 32, 27, 30000, 1001}, {
              "I420", 720, 576, 16, 15, 25, 1}, {
              "I420", 720, 576, 64, 45, 25, 1}, {
              "Y41B", 720, 576, 16, 15, 25, 1}, {
              "Y41B", 720, 576, 64, 45, 25, 1}, {
              "Y42B", 720, 576, 16, 15, 25, 1}, {
              "Y42B", 720, 576, 64, 45, 25, 1}, {
              "Y42B", 1280, 1080, 1, 1, 30000, 1001}, {
              "Y42B", 1280, 1080, 3, 2, 30000, 1001}, {
              "Y42B", 1440, 1080, 1, 1, 25, 1}, {
              "Y42B", 1440, 1080, 4, 3, 25, 1}, {
              "Y42B", 960, 720, 1, 1, 60000, 1001}, {
              "Y42B", 960, 720, 4, 3, 60000, 1001}, {
              "Y42B", 960, 720, 1, 1, 50, 1}, {
              "Y42B", 960, 720, 4, 3, 50, 1},
        };
        GstCaps *temp;
        gint n_sizes = G_N_ELEMENTS (profiles);

        if (strcmp (mimetype, "video/x-raw") == 0) {
          caps = gst_caps_new_empty ();
          for (i = 0; i < n_sizes; i++) {
            temp = gst_caps_new_simple (mimetype,
                "format", G_TYPE_STRING, profiles[i].csp,
                "width", G_TYPE_INT, profiles[i].width,
                "height", G_TYPE_INT, profiles[i].height,
                "framerate", GST_TYPE_FRACTION, profiles[i].framerate_n,
                profiles[i].framerate_d, "pixel-aspect-ratio",
                GST_TYPE_FRACTION, profiles[i].par_n, profiles[i].par_d, NULL);

            gst_caps_append (caps, temp);
          }
        } else {
          caps = gst_caps_new_empty ();
          for (i = 0; i < n_sizes; i++) {
            temp = gst_caps_new_simple (mimetype,
                "width", G_TYPE_INT, profiles[i].width,
                "height", G_TYPE_INT, profiles[i].height,
                "framerate", GST_TYPE_FRACTION, profiles[i].framerate_n,
                profiles[i].framerate_d, "pixel-aspect-ratio",
                GST_TYPE_FRACTION, profiles[i].par_n, profiles[i].par_d, NULL);

            gst_caps_append (caps, temp);
          }
        }
        break;
      }
      case AV_CODEC_ID_DNXHD:
      {
        caps = gst_caps_new_simple (mimetype,
            "width", G_TYPE_INT, 1920,
            "height", G_TYPE_INT, 1080,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
        gst_caps_append (caps, gst_caps_new_simple (mimetype,
                "width", G_TYPE_INT, 1280,
                "height", G_TYPE_INT, 720,
                "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL));
        break;
      }
      default:
      {
        if (codec && codec->supported_framerates
            && codec->supported_framerates[0].num != 0
            && codec->supported_framerates[0].den != 0) {
          GValue va = { 0, };
          GValue v = { 0, };
          const AVRational *rates = codec->supported_framerates;

          if (rates[1].num == 0 && rates[1].den == 0) {
            caps =
                gst_caps_new_simple (mimetype, "framerate", GST_TYPE_FRACTION,
                rates[0].num, rates[0].den, NULL);
          } else {
            g_value_init (&va, GST_TYPE_LIST);
            g_value_init (&v, GST_TYPE_FRACTION);

            while (rates->num != 0 && rates->den != 0) {
              gst_value_set_fraction (&v, rates->num, rates->den);
              gst_value_list_append_value (&va, &v);
              rates++;
            }

            caps = gst_caps_new_simple (mimetype, NULL, NULL, NULL);
            gst_caps_set_value (caps, "framerate", &va);
            g_value_unset (&va);
            g_value_unset (&v);
          }

        } else {
          caps = gst_caps_new_empty_simple (mimetype);
        }

        break;
      }
    }
  }

  /* no fixed caps or special restrictions applied;
   * default unfixed setting */
  if (!caps) {
    GST_DEBUG ("Creating default caps");
    caps = gst_caps_new_empty_simple (mimetype);
  }

  va_start (var_args, fieldname);
  gst_caps_set_simple_valist (caps, fieldname, var_args);
  va_end (var_args);

  return caps;
}

static gint
get_nbits_set (guint64 n)
{
  gint i, x;

  x = 0;
  for (i = 0; i < 64; i++) {
    if ((n & (G_GUINT64_CONSTANT (1) << i)))
      x++;
  }

  return x;
}

static void
gst_ffmpeg_audio_set_sample_fmts (GstCaps * caps,
    const enum AVSampleFormat *fmts, gboolean always_interleaved)
{
  GValue va = { 0, };
  GValue vap = { 0, };
  GValue v = { 0, };
  GstAudioFormat format;
  GstAudioLayout layout;
  GstCaps *caps_copy = NULL;

  if (!fmts || fmts[0] == -1) {
    gint i;

    g_value_init (&va, GST_TYPE_LIST);
    g_value_init (&v, G_TYPE_STRING);
    for (i = 0; i <= AV_SAMPLE_FMT_DBL; i++) {
      format = gst_ffmpeg_smpfmt_to_audioformat (i, NULL);
      if (format == GST_AUDIO_FORMAT_UNKNOWN)
        continue;
      g_value_set_string (&v, gst_audio_format_to_string (format));
      gst_value_list_append_value (&va, &v);
    }
    gst_caps_set_value (caps, "format", &va);
    if (!always_interleaved) {
      g_value_init (&vap, GST_TYPE_LIST);
      g_value_set_string (&v, "interleaved");
      gst_value_list_append_value (&vap, &v);
      g_value_set_string (&v, "non-interleaved");
      gst_value_list_append_value (&vap, &v);
      gst_caps_set_value (caps, "layout", &vap);
      g_value_unset (&vap);
    } else {
      gst_caps_set_simple (caps, "layout", G_TYPE_STRING, "interleaved", NULL);
    }
    g_value_unset (&v);
    g_value_unset (&va);
    return;
  }

  g_value_init (&va, GST_TYPE_LIST);
  g_value_init (&vap, GST_TYPE_LIST);
  g_value_init (&v, G_TYPE_STRING);
  while (*fmts != -1) {
    format = gst_ffmpeg_smpfmt_to_audioformat (*fmts, &layout);
    if (format != GST_AUDIO_FORMAT_UNKNOWN) {
      g_value_set_string (&v, gst_audio_format_to_string (format));
      /* Only append values we don't have yet */
      if (layout == GST_AUDIO_LAYOUT_INTERLEAVED || always_interleaved) {
        if (!_gst_value_list_contains (&va, &v))
          gst_value_list_append_value (&va, &v);
      } else {
        if (!_gst_value_list_contains (&vap, &v))
          gst_value_list_append_value (&vap, &v);
      }
    }
    fmts++;
  }
  if (gst_value_list_get_size (&va) >= 1 && gst_value_list_get_size (&vap) >= 1) {
    caps_copy = gst_caps_copy (caps);
  }
  if (gst_value_list_get_size (&va) == 1) {
    gst_caps_set_value (caps, "format", gst_value_list_get_value (&va, 0));
    gst_caps_set_simple (caps, "layout", G_TYPE_STRING, "interleaved", NULL);
  } else if (gst_value_list_get_size (&va) > 1) {
    gst_caps_set_value (caps, "format", &va);
    gst_caps_set_simple (caps, "layout", G_TYPE_STRING, "interleaved", NULL);
  }
  if (gst_value_list_get_size (&vap) == 1) {
    gst_caps_set_value (caps_copy ? caps_copy : caps, "format",
        gst_value_list_get_value (&vap, 0));
    gst_caps_set_simple (caps_copy ? caps_copy : caps, "layout", G_TYPE_STRING,
        "non-interleaved", NULL);
  } else if (gst_value_list_get_size (&vap) > 1) {
    gst_caps_set_value (caps_copy ? caps_copy : caps, "format", &vap);
    gst_caps_set_simple (caps_copy ? caps_copy : caps, "layout", G_TYPE_STRING,
        "non-interleaved", NULL);
  }
  if (caps_copy) {
    gst_caps_append (caps, caps_copy);
  }
  g_value_unset (&v);
  g_value_unset (&va);
  g_value_unset (&vap);
}

/* same for audio - now with channels/sample rate
 */
static GstCaps *
gst_ff_aud_caps_new (AVCodecContext * context, AVCodec * codec,
    enum AVCodecID codec_id, gboolean encode, const char *mimetype,
    const char *fieldname, ...)
{
  GstCaps *caps = NULL;
  gint i;
  va_list var_args;

  /* fixed, non-probing context */
  if (context != NULL && context->channels != -1) {
    GstAudioChannelPosition pos[64];
    guint64 mask;

    caps = gst_caps_new_simple (mimetype,
        "rate", G_TYPE_INT, context->sample_rate,
        "channels", G_TYPE_INT, context->channels, NULL);

    if (context->channels > 1 &&
        gst_ffmpeg_channel_layout_to_gst (context->channel_layout,
            context->channels, pos) &&
        gst_audio_channel_positions_to_mask (pos, context->channels, FALSE,
            &mask)) {
      gst_caps_set_simple (caps, "channel-mask", GST_TYPE_BITMASK, mask, NULL);
    }
  } else if (encode) {
    gint maxchannels = 2;
    const gint *rates = NULL;
    gint n_rates = 0;

    /* so we must be after restricted caps in this case */
    switch (codec_id) {
      case AV_CODEC_ID_AAC:
      case AV_CODEC_ID_AAC_LATM:
      case AV_CODEC_ID_DTS:
        maxchannels = 6;
        break;
      case AV_CODEC_ID_MP2:
      {
        const static gint l_rates[] =
            { 48000, 44100, 32000, 24000, 22050, 16000 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case AV_CODEC_ID_EAC3:
      case AV_CODEC_ID_AC3:
      {
        const static gint l_rates[] = { 48000, 44100, 32000 };
        maxchannels = 6;
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case AV_CODEC_ID_ADPCM_G722:
      {
        const static gint l_rates[] = { 16000 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        maxchannels = 1;
        break;
      }
      case AV_CODEC_ID_ADPCM_G726:
      {
        const static gint l_rates[] = { 8000 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        maxchannels = 1;
        break;
      }
      case AV_CODEC_ID_ADPCM_SWF:
      {
        const static gint l_rates[] = { 11025, 22050, 44100 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case AV_CODEC_ID_ROQ_DPCM:
      {
        const static gint l_rates[] = { 22050 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case AV_CODEC_ID_AMR_NB:
      {
        const static gint l_rates[] = { 8000 };
        maxchannels = 1;
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case AV_CODEC_ID_AMR_WB:
      {
        const static gint l_rates[] = { 16000 };
        maxchannels = 1;
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case AV_CODEC_ID_DSD_LSBF:
      case AV_CODEC_ID_DSD_MSBF:
      case AV_CODEC_ID_DSD_LSBF_PLANAR:
      case AV_CODEC_ID_DSD_MSBF_PLANAR:
      {
        const static gint l_rates[] = {
          GST_DSD_MAKE_DSD_RATE_44x (64),
          GST_DSD_MAKE_DSD_RATE_48x (64),
          GST_DSD_MAKE_DSD_RATE_44x (128),
          GST_DSD_MAKE_DSD_RATE_48x (128),
          GST_DSD_MAKE_DSD_RATE_44x (256),
          GST_DSD_MAKE_DSD_RATE_48x (256),
          GST_DSD_MAKE_DSD_RATE_44x (512),
          GST_DSD_MAKE_DSD_RATE_48x (512),
          GST_DSD_MAKE_DSD_RATE_44x (1024),
          GST_DSD_MAKE_DSD_RATE_48x (1024),
          GST_DSD_MAKE_DSD_RATE_44x (2048),
          GST_DSD_MAKE_DSD_RATE_48x (2048),
        };
        /* There is no clearly defined maximum number of channels in DSD.
         * The DSF spec mentions a maximum of 6 channels, while the DSDIFF
         * spec mentions up to 65535 channels. DSDIFF stores DSD in an
         * interleaved, DSF in a planar fashion. But there is no reason
         * why some other format couldn't have more than 6 interleaved
         * channels for example. */
        maxchannels = 65535;
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      default:
        break;
    }

    /* regardless of encode/decode, open up channels if applicable */
    /* Until decoders/encoders expose the maximum number of channels
     * they support, we whitelist them here. */
    switch (codec_id) {
      case AV_CODEC_ID_WMAPRO:
      case AV_CODEC_ID_TRUEHD:
        maxchannels = 8;
        break;
      default:
        break;
    }

    if (codec && codec->channel_layouts) {
      const uint64_t *layouts = codec->channel_layouts;
      GstAudioChannelPosition pos[64];

      caps = gst_caps_new_empty ();
      while (*layouts) {
        gint nbits_set = get_nbits_set (*layouts);

        if (gst_ffmpeg_channel_layout_to_gst (*layouts, nbits_set, pos)) {
          guint64 mask;

          if (gst_audio_channel_positions_to_mask (pos, nbits_set, FALSE,
                  &mask)) {
            GstStructure *s =
                gst_structure_new (mimetype, "channels", G_TYPE_INT, nbits_set,
                NULL);

            /* No need to require a channel mask for mono or stereo */
            if (!(nbits_set == 1 && pos[0] == GST_AUDIO_CHANNEL_POSITION_MONO)
                && !(nbits_set == 2
                    && pos[0] == GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT
                    && pos[1] == GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT))
              gst_structure_set (s, "channel-mask", GST_TYPE_BITMASK, mask,
                  NULL);

            gst_caps_append_structure (caps, s);
          }
        }
        layouts++;
      }
    } else {
      if (maxchannels == 1)
        caps = gst_caps_new_simple (mimetype,
            "channels", G_TYPE_INT, maxchannels, NULL);
      else
        caps = gst_caps_new_simple (mimetype,
            "channels", GST_TYPE_INT_RANGE, 1, maxchannels, NULL);
    }

    if (n_rates) {
      GValue list = { 0, };

      g_value_init (&list, GST_TYPE_LIST);
      for (i = 0; i < n_rates; i++) {
        GValue v = { 0, };

        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, rates[i]);
        gst_value_list_append_value (&list, &v);
        g_value_unset (&v);
      }
      gst_caps_set_value (caps, "rate", &list);
      g_value_unset (&list);
    } else if (codec && codec->supported_samplerates
        && codec->supported_samplerates[0]) {
      GValue va = { 0, };
      GValue v = { 0, };

      if (!codec->supported_samplerates[1]) {
        gst_caps_set_simple (caps, "rate", G_TYPE_INT,
            codec->supported_samplerates[0], NULL);
      } else {
        const int *rates = codec->supported_samplerates;

        g_value_init (&va, GST_TYPE_LIST);
        g_value_init (&v, G_TYPE_INT);

        while (*rates) {
          g_value_set_int (&v, *rates);
          gst_value_list_append_value (&va, &v);
          rates++;
        }
        gst_caps_set_value (caps, "rate", &va);
        g_value_unset (&va);
        g_value_unset (&v);
      }
    } else {
      gst_caps_set_simple (caps, "rate", GST_TYPE_INT_RANGE, 4000, 96000, NULL);
    }
  } else {
    caps = gst_caps_new_empty_simple (mimetype);
  }

  va_start (var_args, fieldname);
  gst_caps_set_simple_valist (caps, fieldname, var_args);
  va_end (var_args);

  return caps;
}

/* Check if the given codec ID is an image format -- for now this is just
 * anything whose caps is image/... */
gboolean
gst_ffmpeg_codecid_is_image (enum AVCodecID codec_id)
{
  switch (codec_id) {
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_LJPEG:
    case AV_CODEC_ID_GIF:
    case AV_CODEC_ID_PPM:
    case AV_CODEC_ID_PBM:
    case AV_CODEC_ID_PCX:
    case AV_CODEC_ID_SGI:
    case AV_CODEC_ID_TARGA:
    case AV_CODEC_ID_TIFF:
    case AV_CODEC_ID_SUNRAST:
    case AV_CODEC_ID_BMP:
      return TRUE;

    default:
      return FALSE;
  }
}

/* Convert a FFMPEG codec ID and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * CodecID is primarily meant for compressed data GstCaps!
 *
 * encode is a special parameter. gstffmpegdec will say
 * FALSE, gstffmpegenc will say TRUE. The output caps
 * depends on this, in such a way that it will be very
 * specific, defined, fixed and correct caps for encoders,
 * yet very wide, "forgiving" caps for decoders. Example
 * for mp3: decode: audio/mpeg,mpegversion=1,layer=[1-3]
 * but encode: audio/mpeg,mpegversion=1,layer=3,bitrate=x,
 * rate=x,channels=x.
 */

GstCaps *
gst_ffmpeg_codecid_to_caps (enum AVCodecID codec_id,
    AVCodecContext * context, gboolean encode)
{
  GstCaps *caps = NULL;
  gboolean buildcaps = FALSE;

  GST_LOG ("codec_id:%d, context:%p, encode:%d", codec_id, context, encode);

  switch (codec_id) {
    case AV_CODEC_ID_MPEG1VIDEO:
      /* FIXME: bitrate */
      caps = gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/mpeg",
          "mpegversion", G_TYPE_INT, 1,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;

    case AV_CODEC_ID_MPEG2VIDEO:
      if (encode) {
        /* FIXME: bitrate */
        caps =
            gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/mpeg",
            "mpegversion", G_TYPE_INT, 2, "systemstream", G_TYPE_BOOLEAN, FALSE,
            NULL);
      } else {
        /* decode both MPEG-1 and MPEG-2; width/height/fps are all in
         * the MPEG video stream headers, so may be omitted from caps. */
        caps = gst_caps_new_simple ("video/mpeg",
            "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
      break;

    case AV_CODEC_ID_H263:
      if (encode) {
        caps =
            gst_ff_vid_caps_new (context, NULL, codec_id, encode,
            "video/x-h263", "variant", G_TYPE_STRING, "itu", "h263version",
            G_TYPE_STRING, "h263", NULL);
      } else {
        /* don't pass codec_id, we can decode other variants with the H263
         * decoder that don't have specific size requirements
         */
        caps =
            gst_ff_vid_caps_new (context, NULL, AV_CODEC_ID_NONE, encode,
            "video/x-h263", "variant", G_TYPE_STRING, "itu", NULL);
      }
      break;

    case AV_CODEC_ID_H263P:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-h263",
          "variant", G_TYPE_STRING, "itu", "h263version", G_TYPE_STRING,
          "h263p", NULL);
      if (encode && context) {

        gst_caps_set_simple (caps,
            "annex-f", G_TYPE_BOOLEAN, context->flags & AV_CODEC_FLAG_4MV,
            "annex-j", G_TYPE_BOOLEAN,
            context->flags & AV_CODEC_FLAG_LOOP_FILTER,
            "annex-i", G_TYPE_BOOLEAN, context->flags & AV_CODEC_FLAG_AC_PRED,
            "annex-t", G_TYPE_BOOLEAN, context->flags & AV_CODEC_FLAG_AC_PRED,
            NULL);
      }
      break;

    case AV_CODEC_ID_H263I:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-intel-h263", "variant", G_TYPE_STRING, "intel", NULL);
      break;

    case AV_CODEC_ID_H261:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-h261",
          NULL);
      break;

    case AV_CODEC_ID_RV10:
    case AV_CODEC_ID_RV20:
    case AV_CODEC_ID_RV30:
    case AV_CODEC_ID_RV40:
    {
      gint version;

      switch (codec_id) {
        case AV_CODEC_ID_RV40:
          version = 4;
          break;
        case AV_CODEC_ID_RV30:
          version = 3;
          break;
        case AV_CODEC_ID_RV20:
          version = 2;
          break;
        default:
          version = 1;
          break;
      }

      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-pn-realvideo", "rmversion", G_TYPE_INT, version, NULL);
      if (context) {
        if (context->extradata_size >= 8) {
          gst_caps_set_simple (caps,
              "subformat", G_TYPE_INT, GST_READ_UINT32_BE (context->extradata),
              NULL);
        }
      }
    }
      break;

    case AV_CODEC_ID_MP1:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 1, NULL);
      break;

    case AV_CODEC_ID_MP2:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 2, NULL);
      break;

    case AV_CODEC_ID_MP3:
      if (encode) {
        /* FIXME: bitrate */
        caps =
            gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/mpeg",
            "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
      } else {
        /* Decodes MPEG-1 layer 1/2/3. Samplerate, channels et al are
         * in the MPEG audio header, so may be omitted from caps. */
        caps = gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1,
            "layer", GST_TYPE_INT_RANGE, 1, 3, NULL);
      }
      break;

    case AV_CODEC_ID_MUSEPACK7:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-ffmpeg-parsed-musepack", "streamversion", G_TYPE_INT, 7,
          NULL);
      break;

    case AV_CODEC_ID_MUSEPACK8:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-ffmpeg-parsed-musepack", "streamversion", G_TYPE_INT, 8,
          NULL);
      break;

    case AV_CODEC_ID_AC3:
      /* FIXME: bitrate */
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-ac3",
          NULL);
      break;

    case AV_CODEC_ID_EAC3:
      /* FIXME: bitrate */
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-eac3",
          NULL);
      break;

    case AV_CODEC_ID_TRUEHD:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-true-hd", NULL);
      break;

    case AV_CODEC_ID_ATRAC1:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-vnd.sony.atrac1", NULL);
      break;

    case AV_CODEC_ID_ATRAC3:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-vnd.sony.atrac3", NULL);
      break;

    case AV_CODEC_ID_DTS:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-dts",
          NULL);
      break;

    case AV_CODEC_ID_APE:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-ffmpeg-parsed-ape", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;

    case AV_CODEC_ID_MLP:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-mlp",
          NULL);
      break;

    case AV_CODEC_ID_METASOUND:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-voxware", NULL);
      break;

    case AV_CODEC_ID_IMC:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-imc",
          NULL);
      break;

      /* MJPEG is normal JPEG, Motion-JPEG and Quicktime MJPEG-A. MJPEGB
       * is Quicktime's MJPEG-B. LJPEG is lossless JPEG. I don't know what
       * sp5x is, but it's apparently something JPEG... We don't separate
       * between those in GStreamer. Should we (at least between MJPEG,
       * MJPEG-B and sp5x decoding...)? */
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_LJPEG:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/jpeg",
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;

    case AV_CODEC_ID_JPEG2000:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/x-j2c",
          NULL);
      if (!encode) {
        gst_caps_append (caps, gst_ff_vid_caps_new (context, NULL, codec_id,
                encode, "image/x-jpc", NULL));
        gst_caps_append (caps, gst_ff_vid_caps_new (context, NULL, codec_id,
                encode, "image/jp2", NULL));
      }
      break;

    case AV_CODEC_ID_SP5X:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/sp5x",
          NULL);
      break;

    case AV_CODEC_ID_MJPEGB:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-mjpeg-b", NULL);
      break;

    case AV_CODEC_ID_MPEG4:
      if (encode && context != NULL) {
        /* I'm not exactly sure what ffmpeg outputs... ffmpeg itself uses
         * the AVI fourcc 'DIVX', but 'mp4v' for Quicktime... */
        switch (context->codec_tag) {
          case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
            caps =
                gst_ff_vid_caps_new (context, NULL, codec_id, encode,
                "video/x-divx", "divxversion", G_TYPE_INT, 5, NULL);
            break;
          case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
          default:
            /* FIXME: bitrate. libav doesn't expose the used profile and level */
            caps =
                gst_ff_vid_caps_new (context, NULL, codec_id, encode,
                "video/mpeg", "systemstream", G_TYPE_BOOLEAN, FALSE,
                "mpegversion", G_TYPE_INT, 4, NULL);
            break;
        }
      } else {
        /* The trick here is to separate xvid, divx, mpeg4, 3ivx et al */
        caps =
            gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/mpeg",
            "mpegversion", G_TYPE_INT, 4, "systemstream", G_TYPE_BOOLEAN, FALSE,
            NULL);

        if (encode) {
          GValue arr = { 0, };
          GValue item = { 0, };

          g_value_init (&arr, GST_TYPE_LIST);
          g_value_init (&item, G_TYPE_STRING);
          g_value_set_string (&item, "simple");
          gst_value_list_append_value (&arr, &item);
          g_value_set_string (&item, "advanced-simple");
          gst_value_list_append_value (&arr, &item);
          g_value_unset (&item);

          gst_caps_set_value (caps, "profile", &arr);
          g_value_unset (&arr);

          gst_caps_append (caps, gst_ff_vid_caps_new (context, NULL, codec_id,
                  encode, "video/x-divx", "divxversion", G_TYPE_INT, 5, NULL));
        } else {
          gst_caps_append (caps, gst_ff_vid_caps_new (context, NULL, codec_id,
                  encode, "video/x-divx", "divxversion", GST_TYPE_INT_RANGE, 4,
                  5, NULL));
        }
      }
      break;

    case AV_CODEC_ID_RAWVIDEO:
      caps =
          gst_ffmpeg_codectype_to_video_caps (context, codec_id, encode, NULL);
      break;

    case AV_CODEC_ID_MSMPEG4V1:
    case AV_CODEC_ID_MSMPEG4V2:
    case AV_CODEC_ID_MSMPEG4V3:
    {
      gint version = 41 + codec_id - AV_CODEC_ID_MSMPEG4V1;

      /* encode-FIXME: bitrate */
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-msmpeg", "msmpegversion", G_TYPE_INT, version, NULL);
      if (!encode && codec_id == AV_CODEC_ID_MSMPEG4V3) {
        gst_caps_append (caps, gst_ff_vid_caps_new (context, NULL, codec_id,
                encode, "video/x-divx", "divxversion", G_TYPE_INT, 3, NULL));
      }
    }
      break;

    case AV_CODEC_ID_WMV1:
    case AV_CODEC_ID_WMV2:
    {
      gint version = (codec_id == AV_CODEC_ID_WMV1) ? 1 : 2;

      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-wmv",
          "wmvversion", G_TYPE_INT, version, NULL);
    }
      break;

    case AV_CODEC_ID_FLV1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-flash-video", "flvversion", G_TYPE_INT, 1, NULL);
      break;

    case AV_CODEC_ID_SVQ1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-svq",
          "svqversion", G_TYPE_INT, 1, NULL);
      break;

    case AV_CODEC_ID_SVQ3:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-svq",
          "svqversion", G_TYPE_INT, 3, NULL);
      break;

    case AV_CODEC_ID_DVAUDIO:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-dv",
          NULL);
      break;

    case AV_CODEC_ID_DVVIDEO:
    {
      if (encode && context) {
        const gchar *format;

        switch (context->pix_fmt) {
          case AV_PIX_FMT_YUYV422:
            format = "YUY2";
            break;
          case AV_PIX_FMT_YUV420P:
            format = "I420";
            break;
          case AV_PIX_FMT_YUVA420P:
            format = "A420";
            break;
          case AV_PIX_FMT_YUV411P:
            format = "Y41B";
            break;
          case AV_PIX_FMT_YUV422P:
            format = "Y42B";
            break;
          case AV_PIX_FMT_YUV410P:
            format = "YUV9";
            break;
          default:
            GST_WARNING
                ("Couldnt' find format for pixfmt %d, defaulting to I420",
                context->pix_fmt);
            format = "I420";
            break;
        }
        caps =
            gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, FALSE, "format", G_TYPE_STRING,
            format, NULL);
      } else {
        caps =
            gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
    }
      break;

    case AV_CODEC_ID_WMAV1:
    case AV_CODEC_ID_WMAV2:
    {
      gint version = (codec_id == AV_CODEC_ID_WMAV1) ? 1 : 2;

      if (context) {
        caps =
            gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-wma",
            "wmaversion", G_TYPE_INT, version, "block_align", G_TYPE_INT,
            context->block_align, "bitrate", G_TYPE_INT,
            (guint) context->bit_rate, NULL);
      } else {
        caps =
            gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-wma",
            "wmaversion", G_TYPE_INT, version, "block_align",
            GST_TYPE_INT_RANGE, 0, G_MAXINT, "bitrate", GST_TYPE_INT_RANGE, 0,
            G_MAXINT, NULL);
      }
    }
      break;
    case AV_CODEC_ID_WMAPRO:
    {
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-wma",
          "wmaversion", G_TYPE_INT, 3, NULL);
      break;
    }
    case AV_CODEC_ID_WMALOSSLESS:
    {
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-wma",
          "wmaversion", G_TYPE_INT, 4, NULL);
      break;
    }
    case AV_CODEC_ID_WMAVOICE:
    {
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-wms",
          NULL);
      break;
    }

    case AV_CODEC_ID_XMA1:
    {
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-xma",
          "xmaversion", G_TYPE_INT, 1, NULL);
      break;
    }
    case AV_CODEC_ID_XMA2:
    {
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-xma",
          "xmaversion", G_TYPE_INT, 2, NULL);
      break;
    }

    case AV_CODEC_ID_MACE3:
    case AV_CODEC_ID_MACE6:
    {
      gint version = (codec_id == AV_CODEC_ID_MACE3) ? 3 : 6;

      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-mace",
          "maceversion", G_TYPE_INT, version, NULL);
    }
      break;

    case AV_CODEC_ID_HUFFYUV:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-huffyuv", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "bpp", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;

    case AV_CODEC_ID_FFVHUFF:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-ffvhuff", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "bpp", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;

    case AV_CODEC_ID_CYUV:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-compressed-yuv", NULL);
      break;

    case AV_CODEC_ID_H264:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-h264",
          "alignment", G_TYPE_STRING, "au", NULL);
      if (!encode) {
        GValue arr = { 0, };
        GValue item = { 0, };
        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_STRING);
        g_value_set_string (&item, "avc");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "byte-stream");
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);
        gst_caps_set_value (caps, "stream-format", &arr);
        g_value_unset (&arr);

        gst_caps_append (caps, gst_ff_vid_caps_new (context, NULL, codec_id,
                encode, "video/x-h264", "alignment", G_TYPE_STRING, "nal",
                "stream-format", G_TYPE_STRING, "byte-stream", NULL));

      } else if (context) {
        /* FIXME: ffmpeg currently assumes AVC if there is extradata and
         * byte-stream otherwise. See for example the MOV or MPEG-TS code.
         * ffmpeg does not distinguish the different types of AVC. */
        if (context->extradata_size > 0) {
          gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING, "avc",
              NULL);
        } else {
          gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING,
              "byte-stream", NULL);
        }
      }
      break;

    case AV_CODEC_ID_HEVC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-h265",
          "alignment", G_TYPE_STRING, "au", NULL);
      if (!encode) {
        GValue arr = { 0, };
        GValue item = { 0, };
        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_STRING);
        g_value_set_string (&item, "hvc1");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "hev1");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "byte-stream");
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);
        gst_caps_set_value (caps, "stream-format", &arr);
        g_value_unset (&arr);
      } else if (context) {
        /* FIXME: ffmpeg currently assumes HVC1 if there is extradata and
         * byte-stream otherwise. See for example the MOV or MPEG-TS code.
         * ffmpeg does not distinguish the different types: HVC1/HEV1/etc. */
        if (context->extradata_size > 0) {
          gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING, "hvc1",
              NULL);
        } else {
          gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING,
              "byte-stream", NULL);
        }
      }
      break;

    case AV_CODEC_ID_INDEO5:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 5, NULL);
      break;

    case AV_CODEC_ID_INDEO4:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 4, NULL);
      break;

    case AV_CODEC_ID_INDEO3:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 3, NULL);
      break;

    case AV_CODEC_ID_INDEO2:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 2, NULL);
      break;

    case AV_CODEC_ID_FLASHSV:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-flash-screen", NULL);
      break;

    case AV_CODEC_ID_FLASHSV2:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-flash-screen2", NULL);
      break;

    case AV_CODEC_ID_VP3:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-vp3",
          NULL);
      break;

    case AV_CODEC_ID_VP5:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-vp5",
          NULL);
      break;

    case AV_CODEC_ID_VP6:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-vp6",
          NULL);
      break;

    case AV_CODEC_ID_VP6F:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-vp6-flash", NULL);
      break;

    case AV_CODEC_ID_VP6A:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-vp6-alpha", NULL);
      break;

    case AV_CODEC_ID_VP8:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-vp8",
          NULL);
      break;

    case AV_CODEC_ID_VP9:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-vp9",
          NULL);
      break;

    case AV_CODEC_ID_THEORA:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-theora", NULL);
      break;

    case AV_CODEC_ID_CFHD:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-cineform", NULL);
      break;

    case AV_CODEC_ID_SPEEDHQ:
      if (context && context->codec_tag) {
        gchar *variant = g_strdup_printf ("%" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (context->codec_tag));
        caps =
            gst_ff_vid_caps_new (context, NULL, codec_id, encode,
            "video/x-speedhq", "variant", G_TYPE_STRING, variant, NULL);
        g_free (variant);
      } else {
        caps =
            gst_ff_vid_caps_new (context, NULL, codec_id, encode,
            "video/x-speedhq", NULL);
      }
      break;

    case AV_CODEC_ID_AAC:
    {
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/mpeg",
          NULL);

      if (!encode) {
        GValue arr = { 0, };
        GValue item = { 0, };

        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_INT);
        g_value_set_int (&item, 2);
        gst_value_list_append_value (&arr, &item);
        g_value_set_int (&item, 4);
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);

        gst_caps_set_value (caps, "mpegversion", &arr);
        g_value_unset (&arr);

        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_STRING);
        g_value_set_string (&item, "raw");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "adts");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "adif");
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);

        gst_caps_set_value (caps, "stream-format", &arr);
        g_value_unset (&arr);
      } else {
        gst_caps_set_simple (caps, "mpegversion", G_TYPE_INT, 4,
            "base-profile", G_TYPE_STRING, "lc", NULL);

        /* FIXME: ffmpeg currently assumes raw if there is extradata and
         * ADTS otherwise. See for example the FDK AAC encoder. */
        if (context && context->extradata_size > 0) {
          gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING, "raw",
              NULL);
          gst_codec_utils_aac_caps_set_level_and_profile (caps,
              context->extradata, context->extradata_size);
        } else if (context) {
          gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING, "adts",
              NULL);
        }
      }

      break;
    }
    case AV_CODEC_ID_AAC_LATM: /* LATM/LOAS AAC syntax */
      caps = gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, "stream-format", G_TYPE_STRING, "loas",
          NULL);
      break;

    case AV_CODEC_ID_ASV1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-asus",
          "asusversion", G_TYPE_INT, 1, NULL);
      break;
    case AV_CODEC_ID_ASV2:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-asus",
          "asusversion", G_TYPE_INT, 2, NULL);
      break;

    case AV_CODEC_ID_FFV1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-ffv",
          "ffvversion", G_TYPE_INT, 1, NULL);
      break;

    case AV_CODEC_ID_4XM:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-4xm",
          NULL);
      break;

    case AV_CODEC_ID_XAN_WC3:
    case AV_CODEC_ID_XAN_WC4:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-xan",
          "wcversion", G_TYPE_INT, 3 - AV_CODEC_ID_XAN_WC3 + codec_id, NULL);
      break;

    case AV_CODEC_ID_CLJR:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-cirrus-logic-accupak", NULL);
      break;

    case AV_CODEC_ID_FRAPS:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-fraps",
          NULL);
      break;

    case AV_CODEC_ID_MDEC:
    case AV_CODEC_ID_ROQ:
    case AV_CODEC_ID_INTERPLAY_VIDEO:
      buildcaps = TRUE;
      break;

    case AV_CODEC_ID_VCR1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-ati-vcr", "vcrversion", G_TYPE_INT, 1, NULL);
      break;

    case AV_CODEC_ID_RPZA:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-apple-video", NULL);
      break;

    case AV_CODEC_ID_CINEPAK:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-cinepak", NULL);
      break;

      /* WS_VQA belogns here (order) */

    case AV_CODEC_ID_MSRLE:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-rle",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      break;

    case AV_CODEC_ID_QTRLE:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-rle",
          "layout", G_TYPE_STRING, "quicktime", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      break;

    case AV_CODEC_ID_MSVIDEO1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-msvideocodec", "msvideoversion", G_TYPE_INT, 1, NULL);
      break;

    case AV_CODEC_ID_MSS1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 1, "format", G_TYPE_STRING, "MSS1", NULL);
      break;

    case AV_CODEC_ID_MSS2:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, "format", G_TYPE_STRING, "MSS2", NULL);
      break;

    case AV_CODEC_ID_WMV3:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, "format", G_TYPE_STRING, "WMV3", NULL);
      break;
    case AV_CODEC_ID_VC1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, NULL);
      if (!context && !encode) {
        GValue arr = { 0, };
        GValue item = { 0, };

        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_STRING);
        g_value_set_string (&item, "WVC1");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "WMVA");
        gst_value_list_append_and_take_value (&arr, &item);
        gst_caps_set_value (caps, "format", &arr);
        g_value_unset (&arr);
      } else {
        gst_caps_set_simple (caps, "format", G_TYPE_STRING, "WVC1", NULL);
      }
      break;
    case AV_CODEC_ID_QDM2:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-qdm2",
          NULL);
      break;

    case AV_CODEC_ID_MSZH:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-mszh",
          NULL);
      break;

    case AV_CODEC_ID_ZLIB:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-zlib",
          NULL);
      break;

    case AV_CODEC_ID_TRUEMOTION1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-truemotion", "trueversion", G_TYPE_INT, 1, NULL);
      break;
    case AV_CODEC_ID_TRUEMOTION2:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-truemotion", "trueversion", G_TYPE_INT, 2, NULL);
      break;

    case AV_CODEC_ID_ULTI:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-ultimotion", NULL);
      break;

    case AV_CODEC_ID_TSCC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-camtasia", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 8, 32, NULL);
      }
      break;

    case AV_CODEC_ID_TSCC2:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-tscc", "tsccversion", G_TYPE_INT, 2, NULL);
      break;

    case AV_CODEC_ID_KMVC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-kmvc",
          NULL);
      break;

    case AV_CODEC_ID_NUV:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-nuv",
          NULL);
      break;

    case AV_CODEC_ID_GIF:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "image/gst-libav-gif", "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      break;

    case AV_CODEC_ID_PNG:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/png",
          NULL);
      break;

    case AV_CODEC_ID_PPM:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/ppm",
          NULL);
      break;

    case AV_CODEC_ID_PBM:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/pbm",
          NULL);
      break;

    case AV_CODEC_ID_PAM:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "image/x-portable-anymap", NULL);
      break;

    case AV_CODEC_ID_PGM:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "image/x-portable-graymap", NULL);
      break;

    case AV_CODEC_ID_PCX:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/x-pcx",
          NULL);
      break;

    case AV_CODEC_ID_SGI:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/x-sgi",
          NULL);
      break;

    case AV_CODEC_ID_TARGA:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/x-tga",
          NULL);
      break;

    case AV_CODEC_ID_TIFF:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "image/tiff",
          NULL);
      break;

    case AV_CODEC_ID_SUNRAST:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "image/x-sun-raster", NULL);
      break;

    case AV_CODEC_ID_SMC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-smc",
          NULL);
      break;

    case AV_CODEC_ID_QDRAW:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-qdrw",
          NULL);
      break;

    case AV_CODEC_ID_DNXHD:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-dnxhd",
          NULL);
      break;

    case AV_CODEC_ID_PRORES:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-prores", NULL);
      if (context) {
        switch (context->codec_tag) {
          case GST_MAKE_FOURCC ('a', 'p', 'c', 'o'):
            gst_caps_set_simple (caps, "variant", G_TYPE_STRING, "proxy", NULL);
            break;
          case GST_MAKE_FOURCC ('a', 'p', 'c', 's'):
            gst_caps_set_simple (caps, "variant", G_TYPE_STRING, "lt", NULL);
            break;
          default:
          case GST_MAKE_FOURCC ('a', 'p', 'c', 'n'):
            gst_caps_set_simple (caps, "variant", G_TYPE_STRING, "standard",
                NULL);
            break;
          case GST_MAKE_FOURCC ('a', 'p', 'c', 'h'):
            gst_caps_set_simple (caps, "variant", G_TYPE_STRING, "hq", NULL);
            break;
          case GST_MAKE_FOURCC ('a', 'p', '4', 'h'):
            gst_caps_set_simple (caps, "variant", G_TYPE_STRING, "4444", NULL);
            break;
          case GST_MAKE_FOURCC ('a', 'p', '4', 'x'):
            gst_caps_set_simple (caps, "variant", G_TYPE_STRING, "4444xq",
                NULL);
            break;
        }
      }
      break;

    case AV_CODEC_ID_MIMIC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-mimic",
          NULL);
      break;

    case AV_CODEC_ID_VMNC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-vmnc",
          NULL);
      break;

    case AV_CODEC_ID_TRUESPEECH:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-truespeech", NULL);
      break;

    case AV_CODEC_ID_QCELP:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/qcelp",
          NULL);
      break;

    case AV_CODEC_ID_AMV:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-amv",
          NULL);
      break;

    case AV_CODEC_ID_AASC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-aasc",
          NULL);
      break;

    case AV_CODEC_ID_LOCO:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-loco",
          NULL);
      break;

    case AV_CODEC_ID_ZMBV:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-zmbv",
          NULL);
      break;

    case AV_CODEC_ID_LAGARITH:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-lagarith", NULL);
      break;

    case AV_CODEC_ID_CSCD:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-camstudio", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 8, 32, NULL);
      }
      break;

    case AV_CODEC_ID_AIC:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-apple-intermediate-codec", NULL);
      break;

    case AV_CODEC_ID_CAVS:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode,
          "video/x-cavs", NULL);
      break;

    case AV_CODEC_ID_WS_VQA:
    case AV_CODEC_ID_IDCIN:
    case AV_CODEC_ID_8BPS:
    case AV_CODEC_ID_FLIC:
    case AV_CODEC_ID_VMDVIDEO:
    case AV_CODEC_ID_VMDAUDIO:
    case AV_CODEC_ID_VIXL:
    case AV_CODEC_ID_QPEG:
    case AV_CODEC_ID_PGMYUV:
    case AV_CODEC_ID_WNV1:
    case AV_CODEC_ID_MP3ADU:
    case AV_CODEC_ID_MP3ON4:
    case AV_CODEC_ID_WESTWOOD_SND1:
    case AV_CODEC_ID_MMVIDEO:
    case AV_CODEC_ID_AVS:
      buildcaps = TRUE;
      break;

      /* weird quasi-codecs for the demuxers only */
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_U16LE:
    case AV_CODEC_ID_PCM_U16BE:
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
    {
      GstAudioFormat format;

      switch (codec_id) {
        case AV_CODEC_ID_PCM_S16LE:
          format = GST_AUDIO_FORMAT_S16LE;
          break;
        case AV_CODEC_ID_PCM_S16BE:
          format = GST_AUDIO_FORMAT_S16BE;
          break;
        case AV_CODEC_ID_PCM_U16LE:
          format = GST_AUDIO_FORMAT_U16LE;
          break;
        case AV_CODEC_ID_PCM_U16BE:
          format = GST_AUDIO_FORMAT_U16BE;
          break;
        case AV_CODEC_ID_PCM_S8:
          format = GST_AUDIO_FORMAT_S8;
          break;
        case AV_CODEC_ID_PCM_U8:
          format = GST_AUDIO_FORMAT_U8;
          break;
        default:
          format = 0;
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-raw",
          "format", G_TYPE_STRING, gst_audio_format_to_string (format),
          "layout", G_TYPE_STRING, "interleaved", NULL);
    }
      break;

    case AV_CODEC_ID_PCM_MULAW:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-mulaw",
          NULL);
      break;

    case AV_CODEC_ID_PCM_ALAW:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-alaw",
          NULL);
      break;

    case AV_CODEC_ID_ADPCM_G722:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/G722",
          NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, (guint) context->bit_rate, NULL);
      break;

    case AV_CODEC_ID_ADPCM_G726:
    {
      /* the G726 decoder can also handle G721 */
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-adpcm",
          "layout", G_TYPE_STRING, "g726", NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, (guint) context->bit_rate, NULL);

      if (!encode) {
        gst_caps_append (caps, gst_caps_new_simple ("audio/x-adpcm",
                "layout", G_TYPE_STRING, "g721",
                "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL));
      }
      break;
    }
    case AV_CODEC_ID_ADPCM_IMA_QT:
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    case AV_CODEC_ID_ADPCM_IMA_DK3:
    case AV_CODEC_ID_ADPCM_IMA_DK4:
    case AV_CODEC_ID_ADPCM_IMA_OKI:
    case AV_CODEC_ID_ADPCM_IMA_WS:
    case AV_CODEC_ID_ADPCM_IMA_SMJPEG:
    case AV_CODEC_ID_ADPCM_IMA_AMV:
    case AV_CODEC_ID_ADPCM_IMA_ISS:
    case AV_CODEC_ID_ADPCM_IMA_EA_EACS:
    case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
    case AV_CODEC_ID_ADPCM_MS:
    case AV_CODEC_ID_ADPCM_4XM:
    case AV_CODEC_ID_ADPCM_XA:
    case AV_CODEC_ID_ADPCM_ADX:
    case AV_CODEC_ID_ADPCM_EA:
    case AV_CODEC_ID_ADPCM_CT:
    case AV_CODEC_ID_ADPCM_SWF:
    case AV_CODEC_ID_ADPCM_YAMAHA:
    case AV_CODEC_ID_ADPCM_SBPRO_2:
    case AV_CODEC_ID_ADPCM_SBPRO_3:
    case AV_CODEC_ID_ADPCM_SBPRO_4:
    case AV_CODEC_ID_ADPCM_EA_R1:
    case AV_CODEC_ID_ADPCM_EA_R2:
    case AV_CODEC_ID_ADPCM_EA_R3:
    case AV_CODEC_ID_ADPCM_EA_MAXIS_XA:
    case AV_CODEC_ID_ADPCM_EA_XAS:
    case AV_CODEC_ID_ADPCM_THP:
    {
      const gchar *layout = NULL;

      switch (codec_id) {
        case AV_CODEC_ID_ADPCM_IMA_QT:
          layout = "quicktime";
          break;
        case AV_CODEC_ID_ADPCM_IMA_WAV:
          layout = "dvi";
          break;
        case AV_CODEC_ID_ADPCM_IMA_DK3:
          layout = "dk3";
          break;
        case AV_CODEC_ID_ADPCM_IMA_DK4:
          layout = "dk4";
          break;
        case AV_CODEC_ID_ADPCM_IMA_OKI:
          layout = "oki";
          break;
        case AV_CODEC_ID_ADPCM_IMA_WS:
          layout = "westwood";
          break;
        case AV_CODEC_ID_ADPCM_IMA_SMJPEG:
          layout = "smjpeg";
          break;
        case AV_CODEC_ID_ADPCM_IMA_AMV:
          layout = "amv";
          break;
        case AV_CODEC_ID_ADPCM_IMA_ISS:
          layout = "iss";
          break;
        case AV_CODEC_ID_ADPCM_IMA_EA_EACS:
          layout = "ea-eacs";
          break;
        case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
          layout = "ea-sead";
          break;
        case AV_CODEC_ID_ADPCM_MS:
          layout = "microsoft";
          break;
        case AV_CODEC_ID_ADPCM_4XM:
          layout = "4xm";
          break;
        case AV_CODEC_ID_ADPCM_XA:
          layout = "xa";
          break;
        case AV_CODEC_ID_ADPCM_ADX:
          layout = "adx";
          break;
        case AV_CODEC_ID_ADPCM_EA:
          layout = "ea";
          break;
        case AV_CODEC_ID_ADPCM_CT:
          layout = "ct";
          break;
        case AV_CODEC_ID_ADPCM_SWF:
          layout = "swf";
          break;
        case AV_CODEC_ID_ADPCM_YAMAHA:
          layout = "yamaha";
          break;
        case AV_CODEC_ID_ADPCM_SBPRO_2:
          layout = "sbpro2";
          break;
        case AV_CODEC_ID_ADPCM_SBPRO_3:
          layout = "sbpro3";
          break;
        case AV_CODEC_ID_ADPCM_SBPRO_4:
          layout = "sbpro4";
          break;
        case AV_CODEC_ID_ADPCM_EA_R1:
          layout = "ea-r1";
          break;
        case AV_CODEC_ID_ADPCM_EA_R2:
          layout = "ea-r3";
          break;
        case AV_CODEC_ID_ADPCM_EA_R3:
          layout = "ea-r3";
          break;
        case AV_CODEC_ID_ADPCM_EA_MAXIS_XA:
          layout = "ea-maxis-xa";
          break;
        case AV_CODEC_ID_ADPCM_EA_XAS:
          layout = "ea-xas";
          break;
        case AV_CODEC_ID_ADPCM_THP:
          layout = "thp";
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      /* FIXME: someone please check whether we need additional properties
       * in this caps definition. */
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-adpcm",
          "layout", G_TYPE_STRING, layout, NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, (guint) context->bit_rate, NULL);
    }
      break;

    case AV_CODEC_ID_AMR_NB:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/AMR",
          NULL);
      break;

    case AV_CODEC_ID_AMR_WB:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/AMR-WB",
          NULL);
      break;

    case AV_CODEC_ID_GSM:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-gsm",
          NULL);
      break;

    case AV_CODEC_ID_GSM_MS:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/ms-gsm",
          NULL);
      break;

    case AV_CODEC_ID_NELLYMOSER:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-nellymoser", NULL);
      break;

    case AV_CODEC_ID_SIPR:
    {
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-sipro",
          NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "leaf_size", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, (guint) context->bit_rate, NULL);
      }
    }
      break;

    case AV_CODEC_ID_RA_144:
    case AV_CODEC_ID_RA_288:
    case AV_CODEC_ID_COOK:
    {
      gint version = 0;

      switch (codec_id) {
        case AV_CODEC_ID_RA_144:
          version = 1;
          break;
        case AV_CODEC_ID_RA_288:
          version = 2;
          break;
        case AV_CODEC_ID_COOK:
          version = 8;
          break;
        default:
          break;
      }

      /* FIXME: properties? */
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-pn-realaudio", "raversion", G_TYPE_INT, version, NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "leaf_size", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, (guint) context->bit_rate, NULL);
      }
    }
      break;

    case AV_CODEC_ID_ROQ_DPCM:
    case AV_CODEC_ID_INTERPLAY_DPCM:
    case AV_CODEC_ID_XAN_DPCM:
    case AV_CODEC_ID_SOL_DPCM:
    {
      const gchar *layout = NULL;

      switch (codec_id) {
        case AV_CODEC_ID_ROQ_DPCM:
          layout = "roq";
          break;
        case AV_CODEC_ID_INTERPLAY_DPCM:
          layout = "interplay";
          break;
        case AV_CODEC_ID_XAN_DPCM:
          layout = "xan";
          break;
        case AV_CODEC_ID_SOL_DPCM:
          layout = "sol";
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      /* FIXME: someone please check whether we need additional properties
       * in this caps definition. */
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-dpcm",
          "layout", G_TYPE_STRING, layout, NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, (guint) context->bit_rate, NULL);
    }
      break;

    case AV_CODEC_ID_SHORTEN:
      caps = gst_caps_new_empty_simple ("audio/x-shorten");
      break;

    case AV_CODEC_ID_ALAC:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-alac",
          NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "samplesize", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;

    case AV_CODEC_ID_FLAC:
      /* Note that ffmpeg has no encoder yet, but just for safety. In the
       * encoder case, we want to add things like samplerate, channels... */
      if (!encode) {
        caps = gst_caps_new_empty_simple ("audio/x-flac");
      }
      break;

    case AV_CODEC_ID_OPUS:
      /* Note that ffmpeg has no encoder yet, but just for safety. In the
       * encoder case, we want to add things like samplerate, channels... */
      if (!encode) {
        /* FIXME: can ffmpeg handle multichannel Opus? */
        caps = gst_caps_new_simple ("audio/x-opus",
            "channel-mapping-family", G_TYPE_INT, 0, NULL);
      }
      break;

    case AV_CODEC_ID_S302M:
      caps = gst_caps_new_empty_simple ("audio/x-smpte-302m");
      break;

    case AV_CODEC_ID_DVD_SUBTITLE:
    case AV_CODEC_ID_DVB_SUBTITLE:
      caps = NULL;
      break;
    case AV_CODEC_ID_BMP:
      caps = gst_caps_new_empty_simple ("image/bmp");
      break;
    case AV_CODEC_ID_TTA:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-tta",
          NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "samplesize", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;
    case AV_CODEC_ID_TWINVQ:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode,
          "audio/x-twin-vq", NULL);
      break;
    case AV_CODEC_ID_G729:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/G729",
          NULL);
      break;
    case AV_CODEC_ID_DSD_LSBF:
    case AV_CODEC_ID_DSD_MSBF:
    case AV_CODEC_ID_DSD_LSBF_PLANAR:
    case AV_CODEC_ID_DSD_MSBF_PLANAR:
    {
      gboolean reversed_bytes;
      gboolean interleaved;

      switch (codec_id) {
        case AV_CODEC_ID_DSD_LSBF:
          reversed_bytes = TRUE;
          interleaved = TRUE;
          break;
        case AV_CODEC_ID_DSD_MSBF:
          reversed_bytes = FALSE;
          interleaved = TRUE;
          break;
        case AV_CODEC_ID_DSD_LSBF_PLANAR:
          reversed_bytes = TRUE;
          interleaved = FALSE;
          break;
        case AV_CODEC_ID_DSD_MSBF_PLANAR:
          reversed_bytes = FALSE;
          interleaved = FALSE;
          break;
        default:
          reversed_bytes = FALSE;
          interleaved = FALSE;
          break;
      }

      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/x-dsd",
          "format", G_TYPE_STRING, "DSDU8",
          "reversed-bytes", G_TYPE_BOOLEAN, reversed_bytes,
          "layout", G_TYPE_STRING,
          (interleaved) ? "interleaved" : "non-interleaved", NULL);

      break;
    }
    case AV_CODEC_ID_APTX:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/aptx",
          NULL);
      break;
    case AV_CODEC_ID_APTX_HD:
      caps =
          gst_ff_aud_caps_new (context, NULL, codec_id, encode, "audio/aptx-hd",
          NULL);
      break;
    case AV_CODEC_ID_AV1:
      caps =
          gst_ff_vid_caps_new (context, NULL, codec_id, encode, "video/x-av1",
          "stream-format", G_TYPE_STRING, "obu-stream", NULL);
      if (encode) {
        GValue arr = { 0, };
        GValue item = { 0, };
        g_value_init (&arr, GST_TYPE_LIST);
        g_value_init (&item, G_TYPE_STRING);
        g_value_set_string (&item, "tu");
        gst_value_list_append_value (&arr, &item);
        g_value_set_string (&item, "frame");
        gst_value_list_append_value (&arr, &item);
        g_value_unset (&item);

        gst_caps_set_value (caps, "alignment", &arr);
        g_value_unset (&arr);
      }
      break;
    default:
      GST_DEBUG ("Unknown codec ID %d, please add mapping here", codec_id);
      break;
  }

  if (buildcaps) {
    const AVCodec *codec;

    if ((codec = avcodec_find_decoder (codec_id)) ||
        (codec = avcodec_find_encoder (codec_id))) {
      gchar *mime = NULL;

      GST_LOG ("Could not create stream format caps for %s", codec->name);

      switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO:
          mime = g_strdup_printf ("video/x-gst-av-%s", codec->name);
          caps =
              gst_ff_vid_caps_new (context, NULL, codec_id, encode, mime, NULL);
          g_free (mime);
          break;
        case AVMEDIA_TYPE_AUDIO:
          mime = g_strdup_printf ("audio/x-gst-av-%s", codec->name);
          caps =
              gst_ff_aud_caps_new (context, NULL, codec_id, encode, mime, NULL);
          if (context)
            gst_caps_set_simple (caps,
                "block_align", G_TYPE_INT, context->block_align,
                "bitrate", G_TYPE_INT, (guint) context->bit_rate, NULL);
          g_free (mime);
          break;
        default:
          break;
      }
    }
  }

  if (caps != NULL) {

    /* set private data */
    if (context && context->extradata_size > 0) {
      GstBuffer *data = gst_buffer_new_and_alloc (context->extradata_size);

      gst_buffer_fill (data, 0, context->extradata, context->extradata_size);
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, data, NULL);
      gst_buffer_unref (data);
    }

    GST_LOG ("caps for codec_id=%d: %" GST_PTR_FORMAT, codec_id, caps);

  } else {
    GST_LOG ("No caps found for codec_id=%d", codec_id);
  }

  return caps;
}

/* Convert a FFMPEG Pixel Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_pixfmt_to_caps (enum AVPixelFormat pix_fmt, AVCodecContext * context,
    enum AVCodecID codec_id)
{
  GstCaps *caps = NULL;
  GstVideoFormat format;

  format = gst_ffmpeg_pixfmt_to_videoformat (pix_fmt);

  if (format != GST_VIDEO_FORMAT_UNKNOWN) {
    caps = gst_ff_vid_caps_new (context, NULL, codec_id, TRUE, "video/x-raw",
        "format", G_TYPE_STRING, gst_video_format_to_string (format), NULL);
  }

  if (caps != NULL) {
    GST_DEBUG ("caps for pix_fmt=%d: %" GST_PTR_FORMAT, pix_fmt, caps);
  } else {
    GST_LOG ("No caps found for pix_fmt=%d", pix_fmt);
  }

  return caps;
}

GstAudioFormat
gst_ffmpeg_smpfmt_to_audioformat (enum AVSampleFormat sample_fmt,
    GstAudioLayout * layout)
{
  if (layout)
    *layout = GST_AUDIO_LAYOUT_NON_INTERLEAVED;

  switch (sample_fmt) {
    case AV_SAMPLE_FMT_U8:
      if (layout)
        *layout = GST_AUDIO_LAYOUT_INTERLEAVED;
    case AV_SAMPLE_FMT_U8P:
      return GST_AUDIO_FORMAT_U8;
      break;

    case AV_SAMPLE_FMT_S16:
      if (layout)
        *layout = GST_AUDIO_LAYOUT_INTERLEAVED;
    case AV_SAMPLE_FMT_S16P:
      return GST_AUDIO_FORMAT_S16;
      break;

    case AV_SAMPLE_FMT_S32:
      if (layout)
        *layout = GST_AUDIO_LAYOUT_INTERLEAVED;
    case AV_SAMPLE_FMT_S32P:
      return GST_AUDIO_FORMAT_S32;
      break;
    case AV_SAMPLE_FMT_FLT:
      if (layout)
        *layout = GST_AUDIO_LAYOUT_INTERLEAVED;
    case AV_SAMPLE_FMT_FLTP:
      return GST_AUDIO_FORMAT_F32;
      break;

    case AV_SAMPLE_FMT_DBL:
      if (layout)
        *layout = GST_AUDIO_LAYOUT_INTERLEAVED;
    case AV_SAMPLE_FMT_DBLP:
      return GST_AUDIO_FORMAT_F64;
      break;

    default:
      /* .. */
      return GST_AUDIO_FORMAT_UNKNOWN;
      break;
  }
}

/* Convert a FFMPEG Sample Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_smpfmt_to_caps (enum AVSampleFormat sample_fmt,
    AVCodecContext * context, AVCodec * codec, enum AVCodecID codec_id)
{
  GstCaps *caps = NULL;
  GstAudioFormat format;
  GstAudioLayout layout;

  format = gst_ffmpeg_smpfmt_to_audioformat (sample_fmt, &layout);

  if (format != GST_AUDIO_FORMAT_UNKNOWN) {
    caps = gst_ff_aud_caps_new (context, codec, codec_id, TRUE, "audio/x-raw",
        "format", G_TYPE_STRING, gst_audio_format_to_string (format),
        "layout", G_TYPE_STRING,
        (layout == GST_AUDIO_LAYOUT_INTERLEAVED) ?
        "interleaved" : "non-interleaved", NULL);
    GST_LOG ("caps for sample_fmt=%d: %" GST_PTR_FORMAT, sample_fmt, caps);
  } else {
    GST_LOG ("No caps found for sample_fmt=%d", sample_fmt);
  }

  return caps;
}

static gboolean
caps_has_field (GstCaps * caps, const gchar * field)
{
  guint i, n;

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    if (gst_structure_has_field (s, field))
      return TRUE;
  }

  return FALSE;
}

GstCaps *
gst_ffmpeg_codectype_to_audio_caps (AVCodecContext * context,
    enum AVCodecID codec_id, gboolean encode, AVCodec * codec)
{
  GstCaps *caps = NULL;

  GST_DEBUG ("context:%p, codec_id:%d, encode:%d, codec:%p",
      context, codec_id, encode, codec);
  if (codec)
    GST_DEBUG ("sample_fmts:%p, samplerates:%p",
        codec->sample_fmts, codec->supported_samplerates);

  if (context) {
    /* Specific codec context */
    caps =
        gst_ffmpeg_smpfmt_to_caps (context->sample_fmt, context, codec,
        codec_id);
  } else {
    caps = gst_ff_aud_caps_new (context, codec, codec_id, encode, "audio/x-raw",
        NULL);
    if (!caps_has_field (caps, "format"))
      gst_ffmpeg_audio_set_sample_fmts (caps,
          codec ? codec->sample_fmts : NULL, encode);
  }

  return caps;
}

GstCaps *
gst_ffmpeg_codectype_to_video_caps (AVCodecContext * context,
    enum AVCodecID codec_id, gboolean encode, const AVCodec * codec)
{
  GstCaps *caps;

  GST_LOG ("context:%p, codec_id:%d, encode:%d, codec:%p",
      context, codec_id, encode, codec);

  if (context) {
    caps = gst_ffmpeg_pixfmt_to_caps (context->pix_fmt, context, codec_id);
  } else {
    caps =
        gst_ff_vid_caps_new (context, codec, codec_id, encode, "video/x-raw",
        NULL);
    if (!caps_has_field (caps, "format"))
      gst_ffmpeg_video_set_pix_fmts (caps, codec ? codec->pix_fmts : NULL);
  }
  return caps;
}

/* Convert a GstCaps (audio/raw) to a FFMPEG SampleFmt
 * and other audio properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_smpfmt (const GstCaps * caps,
    AVCodecContext * context, gboolean raw)
{
  GstStructure *structure;
  const gchar *fmt;
  GstAudioFormat format = GST_AUDIO_FORMAT_UNKNOWN;
  gint bitrate;
  const gchar *layout;
  gboolean interleaved;

  g_return_if_fail (gst_caps_get_size (caps) == 1);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &context->channels);
  gst_structure_get_int (structure, "rate", &context->sample_rate);
  gst_structure_get_int (structure, "block_align", &context->block_align);
  if (gst_structure_get_int (structure, "bitrate", &bitrate))
    context->bit_rate = bitrate;

  if (!raw)
    return;

  if (gst_structure_has_name (structure, "audio/x-raw")) {
    if ((fmt = gst_structure_get_string (structure, "format"))) {
      format = gst_audio_format_from_string (fmt);
    }
  }

  layout = gst_structure_get_string (structure, "layout");
  interleaved = !!g_strcmp0 (layout, "non-interleaved");

  switch (format) {
    case GST_AUDIO_FORMAT_F32:
      context->sample_fmt =
          interleaved ? AV_SAMPLE_FMT_FLT : AV_SAMPLE_FMT_FLTP;
      break;
    case GST_AUDIO_FORMAT_F64:
      context->sample_fmt =
          interleaved ? AV_SAMPLE_FMT_DBL : AV_SAMPLE_FMT_DBLP;
      break;
    case GST_AUDIO_FORMAT_S32:
      context->sample_fmt =
          interleaved ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S32P;
      break;
    case GST_AUDIO_FORMAT_S16:
      context->sample_fmt =
          interleaved ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_S16P;
      break;
    default:
      break;
  }
}

/* Convert a GstCaps (video/raw) to a FFMPEG PixFmt
 * and other video properties in a AVCodecContext.
 *
 * For usefullness, see below
 */

static void
gst_ffmpeg_caps_to_pixfmt (const GstCaps * caps,
    AVCodecContext * context, gboolean raw)
{
  GstStructure *structure;
  const GValue *fps;
  const GValue *par = NULL;
  const gchar *fmt;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  const gchar *s;

  GST_DEBUG ("converting caps %" GST_PTR_FORMAT, caps);
  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &context->width);
  gst_structure_get_int (structure, "height", &context->height);
  gst_structure_get_int (structure, "bpp", &context->bits_per_coded_sample);

  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {

    int num = gst_value_get_fraction_numerator (fps);
    int den = gst_value_get_fraction_denominator (fps);

    if (num > 0 && den > 0) {
      /* somehow these seem mixed up.. */
      /* they're fine, this is because it does period=1/frequency */
      context->time_base.den = gst_value_get_fraction_numerator (fps);
      context->time_base.num = gst_value_get_fraction_denominator (fps);
      context->ticks_per_frame = 1;

      GST_DEBUG ("setting framerate %d/%d = %lf",
          context->time_base.den, context->time_base.num,
          1. * context->time_base.den / context->time_base.num);
    } else {
      GST_INFO ("ignoring framerate %d/%d (probably variable framerate)",
          context->time_base.num, context->time_base.den);
    }
  }

  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par && GST_VALUE_HOLDS_FRACTION (par)) {

    int num = gst_value_get_fraction_numerator (par);
    int den = gst_value_get_fraction_denominator (par);

    if (num > 0 && den > 0) {
      context->sample_aspect_ratio.num = num;
      context->sample_aspect_ratio.den = den;

      GST_DEBUG ("setting pixel-aspect-ratio %d/%d = %lf",
          context->sample_aspect_ratio.num, context->sample_aspect_ratio.den,
          1. * context->sample_aspect_ratio.num /
          context->sample_aspect_ratio.den);
    } else {
      GST_WARNING ("ignoring insane pixel-aspect-ratio %d/%d",
          context->sample_aspect_ratio.num, context->sample_aspect_ratio.den);
    }
  }

  if (!raw)
    return;

  g_return_if_fail (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps));

  if (gst_structure_has_name (structure, "video/x-raw")) {
    if ((fmt = gst_structure_get_string (structure, "format"))) {
      format = gst_video_format_from_string (fmt);
    }
  }

  switch (format) {
    case GST_VIDEO_FORMAT_YUY2:
      context->pix_fmt = AV_PIX_FMT_YUYV422;
      break;
    case GST_VIDEO_FORMAT_I420:
      context->pix_fmt = AV_PIX_FMT_YUV420P;
      break;
    case GST_VIDEO_FORMAT_A420:
      context->pix_fmt = AV_PIX_FMT_YUVA420P;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      context->pix_fmt = AV_PIX_FMT_YUV411P;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      context->pix_fmt = AV_PIX_FMT_YUV422P;
      break;
    case GST_VIDEO_FORMAT_YUV9:
      context->pix_fmt = AV_PIX_FMT_YUV410P;
      break;
    case GST_VIDEO_FORMAT_Y444:
      context->pix_fmt = AV_PIX_FMT_YUV444P;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      context->pix_fmt = AV_PIX_FMT_GRAY8;
      break;
    case GST_VIDEO_FORMAT_xRGB:
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      context->pix_fmt = AV_PIX_FMT_RGB32;
#endif
      break;
    case GST_VIDEO_FORMAT_BGRx:
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
      context->pix_fmt = AV_PIX_FMT_RGB32;
#endif
      break;
    case GST_VIDEO_FORMAT_RGB:
      context->pix_fmt = AV_PIX_FMT_RGB24;
      break;
    case GST_VIDEO_FORMAT_BGR:
      context->pix_fmt = AV_PIX_FMT_BGR24;
      break;
    case GST_VIDEO_FORMAT_RGB16:
      context->pix_fmt = AV_PIX_FMT_RGB565;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      context->pix_fmt = AV_PIX_FMT_RGB555;
      break;
    case GST_VIDEO_FORMAT_RGB8P:
      context->pix_fmt = AV_PIX_FMT_PAL8;
      break;
    default:
      break;
  }

  s = gst_structure_get_string (structure, "interlace-mode");
  if (s) {
    if (strcmp (s, "progressive") == 0) {
      context->field_order = AV_FIELD_PROGRESSIVE;
    } else if (strcmp (s, "interleaved") == 0) {
      s = gst_structure_get_string (structure, "field-order");
      if (s) {
        if (strcmp (s, "top-field-first") == 0) {
          context->field_order = AV_FIELD_TT;
        } else if (strcmp (s, "bottom-field-first") == 0) {
          context->field_order = AV_FIELD_TB;
        }
      }
    }
  }
}

typedef struct
{
  GstVideoFormat format;
  enum AVPixelFormat pixfmt;
} PixToFmt;

/* FIXME : FILLME */
static const PixToFmt pixtofmttable[] = {
  /* GST_VIDEO_FORMAT_I420, */
  {GST_VIDEO_FORMAT_I420, AV_PIX_FMT_YUV420P},
  /* Note : this should use a different chroma placement */
  {GST_VIDEO_FORMAT_I420, AV_PIX_FMT_YUVJ420P},

  /* GST_VIDEO_FORMAT_YV12, */
  /* GST_VIDEO_FORMAT_YUY2, */
  {GST_VIDEO_FORMAT_YUY2, AV_PIX_FMT_YUYV422},
  /* GST_VIDEO_FORMAT_UYVY, */
  {GST_VIDEO_FORMAT_UYVY, AV_PIX_FMT_UYVY422},
  /* GST_VIDEO_FORMAT_AYUV, */
  /* GST_VIDEO_FORMAT_RGBx, */
  {GST_VIDEO_FORMAT_RGBx, AV_PIX_FMT_RGB0},
  /* GST_VIDEO_FORMAT_BGRx, */
  {GST_VIDEO_FORMAT_BGRx, AV_PIX_FMT_BGR0},
  /* GST_VIDEO_FORMAT_xRGB, */
  {GST_VIDEO_FORMAT_xRGB, AV_PIX_FMT_0RGB},
  /* GST_VIDEO_FORMAT_xBGR, */
  {GST_VIDEO_FORMAT_xBGR, AV_PIX_FMT_0BGR},
  /* GST_VIDEO_FORMAT_RGBA, */
  {GST_VIDEO_FORMAT_RGBA, AV_PIX_FMT_RGBA},
  /* GST_VIDEO_FORMAT_BGRA, */
  {GST_VIDEO_FORMAT_BGRA, AV_PIX_FMT_BGRA},
  /* GST_VIDEO_FORMAT_ARGB, */
  {GST_VIDEO_FORMAT_ARGB, AV_PIX_FMT_ARGB},
  /* GST_VIDEO_FORMAT_ABGR, */
  {GST_VIDEO_FORMAT_ABGR, AV_PIX_FMT_ABGR},
  /* GST_VIDEO_FORMAT_RGB, */
  {GST_VIDEO_FORMAT_RGB, AV_PIX_FMT_RGB24},
  /* GST_VIDEO_FORMAT_BGR, */
  {GST_VIDEO_FORMAT_BGR, AV_PIX_FMT_BGR24},
  /* GST_VIDEO_FORMAT_Y41B, */
  {GST_VIDEO_FORMAT_Y41B, AV_PIX_FMT_YUV411P},
  /* GST_VIDEO_FORMAT_Y42B, */
  {GST_VIDEO_FORMAT_Y42B, AV_PIX_FMT_YUV422P},
  {GST_VIDEO_FORMAT_Y42B, AV_PIX_FMT_YUVJ422P},
  /* GST_VIDEO_FORMAT_YVYU, */
  /* GST_VIDEO_FORMAT_Y444, */
  {GST_VIDEO_FORMAT_Y444, AV_PIX_FMT_YUV444P},
  {GST_VIDEO_FORMAT_Y444, AV_PIX_FMT_YUVJ444P},
  /* GST_VIDEO_FORMAT_v210, */
  /* GST_VIDEO_FORMAT_v216, */
  /* GST_VIDEO_FORMAT_NV12, */
  {GST_VIDEO_FORMAT_NV12, AV_PIX_FMT_NV12},
  /* GST_VIDEO_FORMAT_NV21, */
  {GST_VIDEO_FORMAT_NV21, AV_PIX_FMT_NV21},
  /* GST_VIDEO_FORMAT_GRAY8, */
  {GST_VIDEO_FORMAT_GRAY8, AV_PIX_FMT_GRAY8},
  /* GST_VIDEO_FORMAT_GRAY16_BE, */
  {GST_VIDEO_FORMAT_GRAY16_BE, AV_PIX_FMT_GRAY16BE},
  /* GST_VIDEO_FORMAT_GRAY16_LE, */
  {GST_VIDEO_FORMAT_GRAY16_LE, AV_PIX_FMT_GRAY16LE},
  /* GST_VIDEO_FORMAT_v308, */
  /* GST_VIDEO_FORMAT_Y800, */
  /* GST_VIDEO_FORMAT_Y16, */
  /* GST_VIDEO_FORMAT_RGB16, */
  {GST_VIDEO_FORMAT_RGB16, AV_PIX_FMT_RGB565},
  /* GST_VIDEO_FORMAT_BGR16, */
  /* GST_VIDEO_FORMAT_RGB15, */
  {GST_VIDEO_FORMAT_RGB15, AV_PIX_FMT_RGB555},
  /* GST_VIDEO_FORMAT_BGR15, */
  /* GST_VIDEO_FORMAT_UYVP, */
  /* GST_VIDEO_FORMAT_A420, */
  {GST_VIDEO_FORMAT_A420, AV_PIX_FMT_YUVA420P},
  /* GST_VIDEO_FORMAT_RGB8_PALETTED, */
  {GST_VIDEO_FORMAT_RGB8P, AV_PIX_FMT_PAL8},
  /* GST_VIDEO_FORMAT_YUV9, */
  {GST_VIDEO_FORMAT_YUV9, AV_PIX_FMT_YUV410P},
  /* GST_VIDEO_FORMAT_YVU9, */
  /* GST_VIDEO_FORMAT_IYU1, */
  /* GST_VIDEO_FORMAT_ARGB64, */
  /* GST_VIDEO_FORMAT_AYUV64, */
  /* GST_VIDEO_FORMAT_r210, */
  {GST_VIDEO_FORMAT_I420_10LE, AV_PIX_FMT_YUV420P10LE},
  {GST_VIDEO_FORMAT_I420_10BE, AV_PIX_FMT_YUV420P10BE},
  {GST_VIDEO_FORMAT_I422_10LE, AV_PIX_FMT_YUV422P10LE},
  {GST_VIDEO_FORMAT_I422_10BE, AV_PIX_FMT_YUV422P10BE},
  {GST_VIDEO_FORMAT_Y444_10LE, AV_PIX_FMT_YUV444P10LE},
  {GST_VIDEO_FORMAT_Y444_10BE, AV_PIX_FMT_YUV444P10BE},
  {GST_VIDEO_FORMAT_GBR, AV_PIX_FMT_GBRP},
  {GST_VIDEO_FORMAT_GBRA, AV_PIX_FMT_GBRAP},
  {GST_VIDEO_FORMAT_GBR_10LE, AV_PIX_FMT_GBRP10LE},
  {GST_VIDEO_FORMAT_GBR_10BE, AV_PIX_FMT_GBRP10BE},
  {GST_VIDEO_FORMAT_GBRA_10LE, AV_PIX_FMT_GBRAP10LE},
  {GST_VIDEO_FORMAT_GBRA_10BE, AV_PIX_FMT_GBRAP10BE},
  {GST_VIDEO_FORMAT_GBR_12LE, AV_PIX_FMT_GBRP12LE},
  {GST_VIDEO_FORMAT_GBR_12BE, AV_PIX_FMT_GBRP12BE},
  {GST_VIDEO_FORMAT_GBRA_12LE, AV_PIX_FMT_GBRAP12LE},
  {GST_VIDEO_FORMAT_GBRA_12BE, AV_PIX_FMT_GBRAP12BE},
  {GST_VIDEO_FORMAT_A420_10LE, AV_PIX_FMT_YUVA420P10LE},
  {GST_VIDEO_FORMAT_A420_10BE, AV_PIX_FMT_YUVA420P10BE},
  {GST_VIDEO_FORMAT_A422_10LE, AV_PIX_FMT_YUVA422P10LE},
  {GST_VIDEO_FORMAT_A422_10BE, AV_PIX_FMT_YUVA422P10BE},
  {GST_VIDEO_FORMAT_A444_10LE, AV_PIX_FMT_YUVA444P10LE},
  {GST_VIDEO_FORMAT_A444_10BE, AV_PIX_FMT_YUVA444P10BE},
  {GST_VIDEO_FORMAT_I420_12LE, AV_PIX_FMT_YUV420P12LE},
  {GST_VIDEO_FORMAT_I420_12BE, AV_PIX_FMT_YUV420P12BE},
  {GST_VIDEO_FORMAT_I422_12LE, AV_PIX_FMT_YUV422P12LE},
  {GST_VIDEO_FORMAT_I422_12BE, AV_PIX_FMT_YUV422P12BE},
  {GST_VIDEO_FORMAT_Y444_12LE, AV_PIX_FMT_YUV444P12LE},
  {GST_VIDEO_FORMAT_Y444_12BE, AV_PIX_FMT_YUV444P12BE},
  {GST_VIDEO_FORMAT_P010_10LE, AV_PIX_FMT_P010LE},
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57,34,100)
  {GST_VIDEO_FORMAT_VUYA, AV_PIX_FMT_VUYX},
#endif
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57,36,100)
  {GST_VIDEO_FORMAT_Y410, AV_PIX_FMT_XV30LE},
  {GST_VIDEO_FORMAT_P012_LE, AV_PIX_FMT_P012LE},
  {GST_VIDEO_FORMAT_Y212_LE, AV_PIX_FMT_Y212LE},
  {GST_VIDEO_FORMAT_Y412_LE, AV_PIX_FMT_XV36LE},
#endif
};

GstVideoFormat
gst_ffmpeg_pixfmt_to_videoformat (enum AVPixelFormat pixfmt)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (pixtofmttable); i++)
    if (pixtofmttable[i].pixfmt == pixfmt)
      return pixtofmttable[i].format;

  GST_DEBUG ("Unknown pixel format %d", pixfmt);
  return GST_VIDEO_FORMAT_UNKNOWN;
}

static enum AVPixelFormat
gst_ffmpeg_videoformat_to_pixfmt_for_codec (GstVideoFormat format,
    const AVCodec * codec)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (pixtofmttable); i++) {
    if (pixtofmttable[i].format == format) {
      gint j;

      if (codec && codec->pix_fmts) {
        for (j = 0; codec->pix_fmts[j] != -1; j++) {
          if (pixtofmttable[i].pixfmt == codec->pix_fmts[j])
            return pixtofmttable[i].pixfmt;
        }
      } else {
        return pixtofmttable[i].pixfmt;
      }
    }
  }

  return AV_PIX_FMT_NONE;
}

enum AVPixelFormat
gst_ffmpeg_videoformat_to_pixfmt (GstVideoFormat format)
{
  return gst_ffmpeg_videoformat_to_pixfmt_for_codec (format, NULL);
}

void
gst_ffmpeg_videoinfo_to_context (GstVideoInfo * info, AVCodecContext * context)
{
  gint i, bpp = 0;

  context->width = GST_VIDEO_INFO_WIDTH (info);
  context->height = GST_VIDEO_INFO_HEIGHT (info);
  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (info); i++)
    bpp += GST_VIDEO_INFO_COMP_DEPTH (info, i);
  context->bits_per_coded_sample = bpp;

  context->ticks_per_frame = 1;
  if (GST_VIDEO_INFO_FPS_N (info) == 0) {
    GST_DEBUG ("Using 25/1 framerate");
    context->time_base.den = 25;
    context->time_base.num = 1;
  } else {
    context->time_base.den = GST_VIDEO_INFO_FPS_N (info);
    context->time_base.num = GST_VIDEO_INFO_FPS_D (info);
  }

  context->sample_aspect_ratio.num = GST_VIDEO_INFO_PAR_N (info);
  context->sample_aspect_ratio.den = GST_VIDEO_INFO_PAR_D (info);

  context->pix_fmt =
      gst_ffmpeg_videoformat_to_pixfmt_for_codec (GST_VIDEO_INFO_FORMAT (info),
      context->codec);

  switch (info->chroma_site) {
    case GST_VIDEO_CHROMA_SITE_MPEG2:
      context->chroma_sample_location = AVCHROMA_LOC_LEFT;
      break;
    case GST_VIDEO_CHROMA_SITE_JPEG:
      context->chroma_sample_location = AVCHROMA_LOC_CENTER;
      break;
    case GST_VIDEO_CHROMA_SITE_DV:
      context->chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
      break;
    case GST_VIDEO_CHROMA_SITE_V_COSITED:
      context->chroma_sample_location = AVCHROMA_LOC_TOP;
      break;
    default:
      break;
  }

  context->color_primaries =
      gst_video_color_primaries_to_iso (info->colorimetry.primaries);
  context->color_trc =
      gst_video_transfer_function_to_iso (info->colorimetry.transfer);
  context->colorspace =
      gst_video_color_matrix_to_iso (info->colorimetry.matrix);

  if (info->colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255) {
    context->color_range = AVCOL_RANGE_JPEG;
  } else {
    context->color_range = AVCOL_RANGE_MPEG;
    context->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
  }
}

void
gst_ffmpeg_audioinfo_to_context (GstAudioInfo * info, AVCodecContext * context)
{
  const AVCodec *codec;
  const enum AVSampleFormat *smpl_fmts;
  enum AVSampleFormat smpl_fmt = -1;

  context->channels = info->channels;
  context->sample_rate = info->rate;
  context->channel_layout =
      gst_ffmpeg_channel_positions_to_layout (info->position, info->channels);

  codec = context->codec;

  smpl_fmts = codec->sample_fmts;

  switch (info->finfo->format) {
    case GST_AUDIO_FORMAT_F32:
      if (smpl_fmts) {
        while (*smpl_fmts != -1) {
          if (*smpl_fmts == AV_SAMPLE_FMT_FLT) {
            smpl_fmt = *smpl_fmts;
            break;
          } else if (*smpl_fmts == AV_SAMPLE_FMT_FLTP) {
            smpl_fmt = *smpl_fmts;
          }

          smpl_fmts++;
        }
      } else {
        smpl_fmt = AV_SAMPLE_FMT_FLT;
      }
      break;
    case GST_AUDIO_FORMAT_F64:
      if (smpl_fmts) {
        while (*smpl_fmts != -1) {
          if (*smpl_fmts == AV_SAMPLE_FMT_DBL) {
            smpl_fmt = *smpl_fmts;
            break;
          } else if (*smpl_fmts == AV_SAMPLE_FMT_DBLP) {
            smpl_fmt = *smpl_fmts;
          }

          smpl_fmts++;
        }
      } else {
        smpl_fmt = AV_SAMPLE_FMT_DBL;
      }
      break;
    case GST_AUDIO_FORMAT_S32:
      if (smpl_fmts) {
        while (*smpl_fmts != -1) {
          if (*smpl_fmts == AV_SAMPLE_FMT_S32) {
            smpl_fmt = *smpl_fmts;
            break;
          } else if (*smpl_fmts == AV_SAMPLE_FMT_S32P) {
            smpl_fmt = *smpl_fmts;
          }

          smpl_fmts++;
        }
      } else {
        smpl_fmt = AV_SAMPLE_FMT_S32;
      }
      break;
    case GST_AUDIO_FORMAT_S16:
      if (smpl_fmts) {
        while (*smpl_fmts != -1) {
          if (*smpl_fmts == AV_SAMPLE_FMT_S16) {
            smpl_fmt = *smpl_fmts;
            break;
          } else if (*smpl_fmts == AV_SAMPLE_FMT_S16P) {
            smpl_fmt = *smpl_fmts;
          }

          smpl_fmts++;
        }
      } else {
        smpl_fmt = AV_SAMPLE_FMT_S16;
      }
      break;
    case GST_AUDIO_FORMAT_U8:
      if (smpl_fmts) {
        while (*smpl_fmts != -1) {
          if (*smpl_fmts == AV_SAMPLE_FMT_U8) {
            smpl_fmt = *smpl_fmts;
            break;
          } else if (*smpl_fmts == AV_SAMPLE_FMT_U8P) {
            smpl_fmt = *smpl_fmts;
          }

          smpl_fmts++;
        }
      } else {
        smpl_fmt = AV_SAMPLE_FMT_U8;
      }
      break;
    default:
      break;
  }

  g_assert (smpl_fmt != -1);

  context->sample_fmt = smpl_fmt;
}

/* Convert a GstCaps and a FFMPEG codec Type to a
 * AVCodecContext. If the context is ommitted, no fixed values
 * for video/audio size will be included in the context
 *
 * AVMediaType is primarily meant for uncompressed data GstCaps!
 */

void
gst_ffmpeg_caps_with_codectype (enum AVMediaType type,
    const GstCaps * caps, AVCodecContext * context)
{
  if (context == NULL)
    return;

  switch (type) {
    case AVMEDIA_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context, TRUE);
      break;

    case AVMEDIA_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context, TRUE);
      break;

    default:
      /* unknown */
      break;
  }
}

#if 0
static void
nal_escape (guint8 * dst, guint8 * src, guint size, guint * destsize)
{
  guint8 *dstp = dst;
  guint8 *srcp = src;
  guint8 *end = src + size;
  gint count = 0;

  while (srcp < end) {
    if (count == 2 && *srcp <= 0x03) {
      GST_DEBUG ("added escape code");
      *dstp++ = 0x03;
      count = 0;
    }
    if (*srcp == 0)
      count++;
    else
      count = 0;

    GST_DEBUG ("copy %02x, count %d", *srcp, count);
    *dstp++ = *srcp++;
  }
  *destsize = dstp - dst;
}

/* copy the config, escaping NAL units as we iterate them, if something fails we
 * copy everything and hope for the best. */
static void
copy_config (guint8 * dst, guint8 * src, guint size, guint * destsize)
{
  guint8 *dstp = dst;
  guint8 *srcp = src;
  gint cnt, i;
  guint nalsize, esize;

  /* check size */
  if (size < 7)
    goto full_copy;

  /* check version */
  if (*srcp != 1)
    goto full_copy;

  cnt = *(srcp + 5) & 0x1f;     /* Number of sps */

  GST_DEBUG ("num SPS %d", cnt);

  memcpy (dstp, srcp, 6);
  srcp += 6;
  dstp += 6;

  for (i = 0; i < cnt; i++) {
    GST_DEBUG ("copy SPS %d", i);
    nalsize = (srcp[0] << 8) | srcp[1];
    nal_escape (dstp + 2, srcp + 2, nalsize, &esize);
    dstp[0] = esize >> 8;
    dstp[1] = esize & 0xff;
    dstp += esize + 2;
    srcp += nalsize + 2;
  }

  cnt = *(dstp++) = *(srcp++);  /* Number of pps */

  GST_DEBUG ("num PPS %d", cnt);

  for (i = 0; i < cnt; i++) {
    GST_DEBUG ("copy PPS %d", i);
    nalsize = (srcp[0] << 8) | srcp[1];
    nal_escape (dstp + 2, srcp + 2, nalsize, &esize);
    dstp[0] = esize >> 8;
    dstp[1] = esize & 0xff;
    dstp += esize + 2;
    srcp += nalsize + 2;
  }
  *destsize = dstp - dst;

  return;

full_copy:
  {
    GST_DEBUG ("something unexpected, doing full copy");
    memcpy (dst, src, size);
    *destsize = size;
    return;
  }
}
#endif

/*
 * caps_with_codecid () transforms a GstCaps for a known codec
 * ID into a filled-in context.
 * codec_data from caps will override possible extradata already in the context
 */

void
gst_ffmpeg_caps_with_codecid (enum AVCodecID codec_id,
    enum AVMediaType codec_type, const GstCaps * caps, AVCodecContext * context)
{
  GstStructure *str;
  const GValue *value;
  GstBuffer *buf;

  GST_LOG ("codec_id:%d, codec_type:%d, caps:%" GST_PTR_FORMAT " context:%p",
      codec_id, codec_type, caps, context);

  if (!context || !gst_caps_get_size (caps))
    return;

  str = gst_caps_get_structure (caps, 0);

  /* extradata parsing (esds [mpeg4], wma/wmv, msmpeg4v1/2/3, etc.) */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    GstMapInfo map;

    buf = gst_value_get_buffer (value);
    gst_buffer_map (buf, &map, GST_MAP_READ);

    /* free the old one if it is there */
    if (context->extradata)
      av_free (context->extradata);

#if 0
    if (codec_id == AV_CODEC_ID_H264) {
      guint extrasize;

      GST_DEBUG ("copy, escaping codec_data %d", size);
      /* ffmpeg h264 expects the codec_data to be escaped, there is no real
       * reason for this but let's just escape it for now. Start by allocating
       * enough space, x2 is more than enough.
       *
       * FIXME, we disabled escaping because some file already contain escaped
       * codec_data and then we escape twice and fail. It's better to leave it
       * as is, as that is what most players do. */
      context->extradata =
          av_mallocz (GST_ROUND_UP_16 (size * 2 +
              AV_INPUT_BUFFER_PADDING_SIZE));
      copy_config (context->extradata, data, size, &extrasize);
      GST_DEBUG ("escaped size: %d", extrasize);
      context->extradata_size = extrasize;
    } else
#endif
    {
      /* allocate with enough padding */
      GST_DEBUG ("copy codec_data");
      context->extradata =
          av_mallocz (GST_ROUND_UP_16 (map.size +
              AV_INPUT_BUFFER_PADDING_SIZE));
      memcpy (context->extradata, map.data, map.size);
      context->extradata_size = map.size;
    }

    /* Hack for VC1. Sometimes the first (length) byte is 0 for some files */
    if (codec_id == AV_CODEC_ID_VC1 && map.size > 0 && map.data[0] == 0) {
      context->extradata[0] = (guint8) map.size;
    }

    GST_DEBUG ("have codec data of size %" G_GSIZE_FORMAT, map.size);

    gst_buffer_unmap (buf, &map);
  } else {
    context->extradata = NULL;
    context->extradata_size = 0;
    GST_DEBUG ("no codec data");
  }

  switch (codec_id) {
    case AV_CODEC_ID_MPEG4:
    {
      const gchar *mime = gst_structure_get_name (str);

      context->flags |= AV_CODEC_FLAG_4MV;

      if (!strcmp (mime, "video/x-divx"))
        context->codec_tag = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
      else if (!strcmp (mime, "video/mpeg")) {
        const gchar *profile;

        context->codec_tag = GST_MAKE_FOURCC ('m', 'p', '4', 'v');

        profile = gst_structure_get_string (str, "profile");
        if (profile) {
          if (g_strcmp0 (profile, "advanced-simple") == 0)
            context->flags |= AV_CODEC_FLAG_QPEL;
        }
      }
      break;
    }

    case AV_CODEC_ID_SVQ3:
      /* FIXME: this is a workaround for older gst-plugins releases
       * (<= 0.8.9). This should be removed at some point, because
       * it causes wrong decoded frame order. */
      if (!context->extradata) {
        gint halfpel_flag, thirdpel_flag, low_delay, unknown_svq3_flag;
        guint16 flags;

        if (gst_structure_get_int (str, "halfpel_flag", &halfpel_flag) &&
            gst_structure_get_int (str, "thirdpel_flag", &thirdpel_flag) &&
            gst_structure_get_int (str, "low_delay", &low_delay) &&
            gst_structure_get_int (str, "unknown_svq3_flag",
                &unknown_svq3_flag)) {
          context->extradata = (guint8 *) av_mallocz (0x64);
          g_stpcpy ((gchar *) context->extradata, "SVQ3");
          flags = 1 << 3;
          flags |= low_delay;
          flags = flags << 2;
          flags |= unknown_svq3_flag;
          flags = flags << 6;
          flags |= halfpel_flag;
          flags = flags << 1;
          flags |= thirdpel_flag;
          flags = flags << 3;

          flags = GUINT16_FROM_LE (flags);

          memcpy ((gchar *) context->extradata + 0x62, &flags, 2);
          context->extradata_size = 0x64;
        }
      }
      break;

    case AV_CODEC_ID_MSRLE:
    case AV_CODEC_ID_QTRLE:
    case AV_CODEC_ID_TSCC:
    case AV_CODEC_ID_CSCD:
    case AV_CODEC_ID_APE:
    {
      gint depth;

      if (gst_structure_get_int (str, "depth", &depth)) {
        context->bits_per_coded_sample = depth;
      } else {
        GST_WARNING ("No depth field in caps %" GST_PTR_FORMAT, caps);
      }

    }
      break;

    case AV_CODEC_ID_COOK:
    case AV_CODEC_ID_RA_288:
    case AV_CODEC_ID_RA_144:
    case AV_CODEC_ID_SIPR:
    {
      gint leaf_size;
      gint bitrate;

      if (gst_structure_get_int (str, "leaf_size", &leaf_size))
        context->block_align = leaf_size;
      if (gst_structure_get_int (str, "bitrate", &bitrate))
        context->bit_rate = bitrate;
    }
      break;
    case AV_CODEC_ID_ALAC:
      gst_structure_get_int (str, "samplesize",
          &context->bits_per_coded_sample);
      break;

    case AV_CODEC_ID_DVVIDEO:
    {
      const gchar *format;

      if ((format = gst_structure_get_string (str, "format"))) {

        if (g_str_equal (format, "YUY2"))
          context->pix_fmt = AV_PIX_FMT_YUYV422;
        else if (g_str_equal (format, "I420"))
          context->pix_fmt = AV_PIX_FMT_YUV420P;
        else if (g_str_equal (format, "A420"))
          context->pix_fmt = AV_PIX_FMT_YUVA420P;
        else if (g_str_equal (format, "Y41B"))
          context->pix_fmt = AV_PIX_FMT_YUV411P;
        else if (g_str_equal (format, "Y42B"))
          context->pix_fmt = AV_PIX_FMT_YUV422P;
        else if (g_str_equal (format, "YUV9"))
          context->pix_fmt = AV_PIX_FMT_YUV410P;
        else {
          GST_WARNING ("couldn't convert format %s" " to a pixel format",
              format);
        }
      } else
        GST_WARNING ("No specified format");
      break;
    }
    case AV_CODEC_ID_H263P:
    {
      gboolean val;

      if (!gst_structure_get_boolean (str, "annex-f", &val) || val)
        context->flags |= AV_CODEC_FLAG_4MV;
      else
        context->flags &= ~AV_CODEC_FLAG_4MV;
      if ((!gst_structure_get_boolean (str, "annex-i", &val) || val) &&
          (!gst_structure_get_boolean (str, "annex-t", &val) || val))
        context->flags |= AV_CODEC_FLAG_AC_PRED;
      else
        context->flags &= ~AV_CODEC_FLAG_AC_PRED;
      if (!gst_structure_get_boolean (str, "annex-j", &val) || val)
        context->flags |= AV_CODEC_FLAG_LOOP_FILTER;
      else
        context->flags &= ~AV_CODEC_FLAG_LOOP_FILTER;
      break;
    }
    case AV_CODEC_ID_ADPCM_G726:
    {
      const gchar *layout;

      if ((layout = gst_structure_get_string (str, "layout"))) {
        if (!strcmp (layout, "g721")) {
          context->sample_rate = 8000;
          context->channels = 1;
          context->bit_rate = 32000;
        }
      }
      break;
    }
    case AV_CODEC_ID_SPEEDHQ:
    {
      const gchar *variant;

      if (context && (variant = gst_structure_get_string (str, "variant"))
          && strlen (variant) == 4) {

        context->codec_tag =
            GST_MAKE_FOURCC (variant[0], variant[1], variant[2], variant[3]);
      }
      break;
    }
    default:
      break;
  }

  if (!gst_caps_is_fixed (caps))
    return;

  /* common properties (width, height, fps) */
  switch (codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context,
          codec_id == AV_CODEC_ID_RAWVIDEO);
      break;
    case AVMEDIA_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context, FALSE);
      break;
    default:
      break;
  }

  /* fixup of default settings */
  switch (codec_id) {
    case AV_CODEC_ID_QCELP:
      /* QCELP is always mono, no matter what the caps say */
      context->channels = 1;
      break;
    case AV_CODEC_ID_ADPCM_G726:
      if (context->sample_rate && context->bit_rate)
        context->bits_per_coded_sample =
            context->bit_rate / context->sample_rate;
      break;
    default:
      break;
  }
}

/* _formatid_to_caps () is meant for muxers/demuxers, it
 * transforms a name (ffmpeg way of ID'ing these, why don't
 * they have unique numerical IDs?) to the corresponding
 * caps belonging to that mux-format
 *
 * Note: we don't need any additional info because the caps
 * isn't supposed to contain any useful info besides the
 * media type anyway
 */

GstCaps *
gst_ffmpeg_formatid_to_caps (const gchar * format_name)
{
  GstCaps *caps = NULL;

  if (!strcmp (format_name, "mpeg")) {
    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "mpegts")) {
    caps = gst_caps_new_simple ("video/mpegts",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "rm")) {
    caps = gst_caps_new_simple ("application/x-pn-realmedia",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "asf")) {
    caps = gst_caps_new_empty_simple ("video/x-ms-asf");
  } else if (!strcmp (format_name, "avi")) {
    caps = gst_caps_new_empty_simple ("video/x-msvideo");
  } else if (!strcmp (format_name, "wav")) {
    caps = gst_caps_new_empty_simple ("audio/x-wav");
  } else if (!strcmp (format_name, "ape")) {
    caps = gst_caps_new_empty_simple ("application/x-ape");
  } else if (!strcmp (format_name, "swf")) {
    caps = gst_caps_new_empty_simple ("application/x-shockwave-flash");
  } else if (!strcmp (format_name, "au")) {
    caps = gst_caps_new_empty_simple ("audio/x-au");
  } else if (!strcmp (format_name, "dv")) {
    caps = gst_caps_new_simple ("video/x-dv",
        "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  } else if (!strcmp (format_name, "4xm")) {
    caps = gst_caps_new_empty_simple ("video/x-4xm");
  } else if (!strcmp (format_name, "matroska")) {
    caps = gst_caps_new_empty_simple ("video/x-matroska");
  } else if (!strcmp (format_name, "ivf")) {
    caps = gst_caps_new_empty_simple ("video/x-ivf");
  } else if (!strcmp (format_name, "mp3")) {
    caps = gst_caps_new_empty_simple ("application/x-id3");
  } else if (!strcmp (format_name, "flic")) {
    caps = gst_caps_new_empty_simple ("video/x-fli");
  } else if (!strcmp (format_name, "flv")) {
    caps = gst_caps_new_empty_simple ("video/x-flv");
  } else if (!strcmp (format_name, "tta")) {
    caps = gst_caps_new_empty_simple ("audio/x-ttafile");
  } else if (!strcmp (format_name, "aiff")) {
    caps = gst_caps_new_empty_simple ("audio/x-aiff");
  } else if (!strcmp (format_name, "mov_mp4_m4a_3gp_3g2")) {
    caps =
        gst_caps_from_string
        ("application/x-3gp; video/quicktime; audio/x-m4a");
  } else if (!strcmp (format_name, "mov")) {
    caps = gst_caps_from_string ("video/quicktime,variant=(string)apple");
  } else if (!strcmp (format_name, "mp4")) {
    caps = gst_caps_from_string ("video/quicktime,variant=(string)iso");
  } else if (!strcmp (format_name, "3gp")) {
    caps = gst_caps_from_string ("video/quicktime,variant=(string)3gpp");
  } else if (!strcmp (format_name, "3g2")) {
    caps = gst_caps_from_string ("video/quicktime,variant=(string)3g2");
  } else if (!strcmp (format_name, "psp")) {
    caps = gst_caps_from_string ("video/quicktime,variant=(string)psp");
  } else if (!strcmp (format_name, "ipod")) {
    caps = gst_caps_from_string ("video/quicktime,variant=(string)ipod");
  } else if (!strcmp (format_name, "aac")) {
    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 4, NULL);
  } else if (!strcmp (format_name, "gif")) {
    caps = gst_caps_from_string ("image/gif");
  } else if (!strcmp (format_name, "ogg")) {
    caps = gst_caps_from_string ("application/ogg");
  } else if (!strcmp (format_name, "mxf") || !strcmp (format_name, "mxf_d10")) {
    caps = gst_caps_from_string ("application/mxf");
  } else if (!strcmp (format_name, "gxf")) {
    caps = gst_caps_from_string ("application/gxf");
  } else if (!strcmp (format_name, "yuv4mpegpipe")) {
    caps = gst_caps_new_simple ("application/x-yuv4mpeg",
        "y4mversion", G_TYPE_INT, 2, NULL);
  } else if (!strcmp (format_name, "mpc")) {
    caps = gst_caps_from_string ("audio/x-musepack, streamversion = (int) 7");
  } else if (!strcmp (format_name, "mpc8")) {
    caps = gst_caps_from_string ("audio/x-musepack, streamversion = (int) 8");
  } else if (!strcmp (format_name, "vqf")) {
    caps = gst_caps_from_string ("audio/x-vqf");
  } else if (!strcmp (format_name, "nsv")) {
    caps = gst_caps_from_string ("video/x-nsv");
  } else if (!strcmp (format_name, "amr")) {
    caps = gst_caps_from_string ("audio/x-amr-nb-sh");
  } else if (!strcmp (format_name, "webm")) {
    caps = gst_caps_from_string ("video/webm");
  } else if (!strcmp (format_name, "voc")) {
    caps = gst_caps_from_string ("audio/x-voc");
  } else if (!strcmp (format_name, "pva")) {
    caps = gst_caps_from_string ("video/x-pva");
  } else if (!strcmp (format_name, "brstm")) {
    caps = gst_caps_from_string ("audio/x-brstm");
  } else if (!strcmp (format_name, "bfstm")) {
    caps = gst_caps_from_string ("audio/x-bfstm");
  } else {
    gchar *name;

    GST_LOG ("Could not create stream format caps for %s", format_name);
    name = g_strdup_printf ("application/x-gst-av-%s", format_name);
    caps = gst_caps_new_empty_simple (name);
    g_free (name);
  }

  return caps;
}

gboolean
gst_ffmpeg_formatid_get_codecids (const gchar * format_name,
    enum AVCodecID **video_codec_list, enum AVCodecID **audio_codec_list,
    AVOutputFormat * plugin)
{
  static enum AVCodecID tmp_vlist[] = {
    AV_CODEC_ID_NONE,
    AV_CODEC_ID_NONE
  };
  static enum AVCodecID tmp_alist[] = {
    AV_CODEC_ID_NONE,
    AV_CODEC_ID_NONE
  };

  GST_LOG ("format_name : %s", format_name);

  if (!strcmp (format_name, "mp4")) {
    static enum AVCodecID mp4_video_list[] = {
      AV_CODEC_ID_MPEG4, AV_CODEC_ID_H264,
      AV_CODEC_ID_MJPEG,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID mp4_audio_list[] = {
      AV_CODEC_ID_AAC, AV_CODEC_ID_MP3,
      AV_CODEC_ID_NONE
    };

    *video_codec_list = mp4_video_list;
    *audio_codec_list = mp4_audio_list;
  } else if (!strcmp (format_name, "mpeg")) {
    static enum AVCodecID mpeg_video_list[] = { AV_CODEC_ID_MPEG1VIDEO,
      AV_CODEC_ID_MPEG2VIDEO,
      AV_CODEC_ID_H264,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID mpeg_audio_list[] = { AV_CODEC_ID_MP1,
      AV_CODEC_ID_MP2,
      AV_CODEC_ID_MP3,
      AV_CODEC_ID_NONE
    };

    *video_codec_list = mpeg_video_list;
    *audio_codec_list = mpeg_audio_list;
  } else if (!strcmp (format_name, "dvd")) {
    static enum AVCodecID mpeg_video_list[] = { AV_CODEC_ID_MPEG2VIDEO,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID mpeg_audio_list[] = { AV_CODEC_ID_MP2,
      AV_CODEC_ID_AC3,
      AV_CODEC_ID_DTS,
      AV_CODEC_ID_PCM_S16BE,
      AV_CODEC_ID_NONE
    };

    *video_codec_list = mpeg_video_list;
    *audio_codec_list = mpeg_audio_list;
  } else if (!strcmp (format_name, "mpegts")) {
    static enum AVCodecID mpegts_video_list[] = { AV_CODEC_ID_MPEG1VIDEO,
      AV_CODEC_ID_MPEG2VIDEO,
      AV_CODEC_ID_H264,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID mpegts_audio_list[] = { AV_CODEC_ID_MP2,
      AV_CODEC_ID_MP3,
      AV_CODEC_ID_AC3,
      AV_CODEC_ID_DTS,
      AV_CODEC_ID_AAC,
      AV_CODEC_ID_NONE
    };

    *video_codec_list = mpegts_video_list;
    *audio_codec_list = mpegts_audio_list;
  } else if (!strcmp (format_name, "vob")) {
    static enum AVCodecID vob_video_list[] =
        { AV_CODEC_ID_MPEG2VIDEO, AV_CODEC_ID_NONE };
    static enum AVCodecID vob_audio_list[] = { AV_CODEC_ID_MP2, AV_CODEC_ID_AC3,
      AV_CODEC_ID_DTS, AV_CODEC_ID_NONE
    };

    *video_codec_list = vob_video_list;
    *audio_codec_list = vob_audio_list;
  } else if (!strcmp (format_name, "flv")) {
    static enum AVCodecID flv_video_list[] =
        { AV_CODEC_ID_FLV1, AV_CODEC_ID_NONE };
    static enum AVCodecID flv_audio_list[] =
        { AV_CODEC_ID_MP3, AV_CODEC_ID_NONE };

    *video_codec_list = flv_video_list;
    *audio_codec_list = flv_audio_list;
  } else if (!strcmp (format_name, "asf")) {
    static enum AVCodecID asf_video_list[] =
        { AV_CODEC_ID_WMV1, AV_CODEC_ID_WMV2, AV_CODEC_ID_MSMPEG4V3,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID asf_audio_list[] =
        { AV_CODEC_ID_WMAV1, AV_CODEC_ID_WMAV2, AV_CODEC_ID_MP3,
      AV_CODEC_ID_NONE
    };

    *video_codec_list = asf_video_list;
    *audio_codec_list = asf_audio_list;
  } else if (!strcmp (format_name, "dv")) {
    static enum AVCodecID dv_video_list[] =
        { AV_CODEC_ID_DVVIDEO, AV_CODEC_ID_NONE };
    static enum AVCodecID dv_audio_list[] =
        { AV_CODEC_ID_PCM_S16LE, AV_CODEC_ID_NONE };

    *video_codec_list = dv_video_list;
    *audio_codec_list = dv_audio_list;
  } else if (!strcmp (format_name, "mov")) {
    static enum AVCodecID mov_video_list[] = {
      AV_CODEC_ID_SVQ1, AV_CODEC_ID_SVQ3, AV_CODEC_ID_MPEG4,
      AV_CODEC_ID_H263, AV_CODEC_ID_H263P,
      AV_CODEC_ID_H264, AV_CODEC_ID_DVVIDEO,
      AV_CODEC_ID_MJPEG,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID mov_audio_list[] = {
      AV_CODEC_ID_PCM_MULAW, AV_CODEC_ID_PCM_ALAW, AV_CODEC_ID_ADPCM_IMA_QT,
      AV_CODEC_ID_MACE3, AV_CODEC_ID_MACE6, AV_CODEC_ID_AAC,
      AV_CODEC_ID_AMR_NB, AV_CODEC_ID_AMR_WB,
      AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE,
      AV_CODEC_ID_MP3, AV_CODEC_ID_NONE
    };

    *video_codec_list = mov_video_list;
    *audio_codec_list = mov_audio_list;
  } else if ((!strcmp (format_name, "3gp") || !strcmp (format_name, "3g2"))) {
    static enum AVCodecID tgp_video_list[] = {
      AV_CODEC_ID_MPEG4, AV_CODEC_ID_H263, AV_CODEC_ID_H263P, AV_CODEC_ID_H264,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID tgp_audio_list[] = {
      AV_CODEC_ID_AMR_NB, AV_CODEC_ID_AMR_WB,
      AV_CODEC_ID_AAC,
      AV_CODEC_ID_NONE
    };

    *video_codec_list = tgp_video_list;
    *audio_codec_list = tgp_audio_list;
  } else if (!strcmp (format_name, "mmf")) {
    static enum AVCodecID mmf_audio_list[] = {
      AV_CODEC_ID_ADPCM_YAMAHA, AV_CODEC_ID_NONE
    };
    *video_codec_list = NULL;
    *audio_codec_list = mmf_audio_list;
  } else if (!strcmp (format_name, "amr")) {
    static enum AVCodecID amr_audio_list[] = {
      AV_CODEC_ID_AMR_NB, AV_CODEC_ID_AMR_WB,
      AV_CODEC_ID_NONE
    };
    *video_codec_list = NULL;
    *audio_codec_list = amr_audio_list;
  } else if (!strcmp (format_name, "gif")) {
    static enum AVCodecID gif_image_list[] = {
      AV_CODEC_ID_RAWVIDEO, AV_CODEC_ID_NONE
    };
    *video_codec_list = gif_image_list;
    *audio_codec_list = NULL;
  } else if ((!strcmp (format_name, "pva"))) {
    static enum AVCodecID pga_video_list[] = {
      AV_CODEC_ID_MPEG2VIDEO,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID pga_audio_list[] = {
      AV_CODEC_ID_MP2,
      AV_CODEC_ID_NONE
    };

    *video_codec_list = pga_video_list;
    *audio_codec_list = pga_audio_list;
  } else if ((!strcmp (format_name, "ivf"))) {
    static enum AVCodecID ivf_video_list[] = {
      AV_CODEC_ID_VP8,
      AV_CODEC_ID_VP9,
      AV_CODEC_ID_AV1,
      AV_CODEC_ID_NONE
    };
    static enum AVCodecID ivf_audio_list[] = {
      AV_CODEC_ID_NONE
    };

    *video_codec_list = ivf_video_list;
    *audio_codec_list = ivf_audio_list;
  } else if ((plugin->audio_codec != AV_CODEC_ID_NONE) ||
      (plugin->video_codec != AV_CODEC_ID_NONE)) {
    tmp_vlist[0] = plugin->video_codec;
    tmp_alist[0] = plugin->audio_codec;

    *video_codec_list = tmp_vlist;
    *audio_codec_list = tmp_alist;
  } else {
    GST_LOG ("Format %s not found", format_name);
    return FALSE;
  }

  return TRUE;
}

/* Convert a GstCaps to a FFMPEG codec ID. Size et all
 * are omitted, that can be queried by the user itself,
 * we're not eating the GstCaps or anything
 * A pointer to an allocated context is also needed for
 * optional extra info
 */

enum AVCodecID
gst_ffmpeg_caps_to_codecid (const GstCaps * caps, AVCodecContext * context)
{
  enum AVCodecID id = AV_CODEC_ID_NONE;
  const gchar *mimetype;
  const GstStructure *structure;
  gboolean video = FALSE, audio = FALSE;        /* we want to be sure! */

  g_return_val_if_fail (caps != NULL, AV_CODEC_ID_NONE);
  g_return_val_if_fail (gst_caps_get_size (caps) == 1, AV_CODEC_ID_NONE);
  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw")) {
    id = AV_CODEC_ID_RAWVIDEO;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-raw")) {
    GstAudioInfo info;

    if (gst_audio_info_from_caps (&info, caps)) {
      switch (GST_AUDIO_INFO_FORMAT (&info)) {
        case GST_AUDIO_FORMAT_S8:
          id = AV_CODEC_ID_PCM_S8;
          break;
        case GST_AUDIO_FORMAT_U8:
          id = AV_CODEC_ID_PCM_U8;
          break;
        case GST_AUDIO_FORMAT_S16LE:
          id = AV_CODEC_ID_PCM_S16LE;
          break;
        case GST_AUDIO_FORMAT_S16BE:
          id = AV_CODEC_ID_PCM_S16BE;
          break;
        case GST_AUDIO_FORMAT_U16LE:
          id = AV_CODEC_ID_PCM_U16LE;
          break;
        case GST_AUDIO_FORMAT_U16BE:
          id = AV_CODEC_ID_PCM_U16BE;
          break;
        default:
          break;
      }
      if (id != AV_CODEC_ID_NONE)
        audio = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-mulaw")) {
    id = AV_CODEC_ID_PCM_MULAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-alaw")) {
    id = AV_CODEC_ID_PCM_ALAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-dv")) {
    gboolean sys_strm;

    if (gst_structure_get_boolean (structure, "systemstream", &sys_strm) &&
        !sys_strm) {
      id = AV_CODEC_ID_DVVIDEO;
      video = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-dv")) {        /* ??? */
    id = AV_CODEC_ID_DVAUDIO;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-h263")) {
    const gchar *h263version =
        gst_structure_get_string (structure, "h263version");
    if (h263version && !strcmp (h263version, "h263p"))
      id = AV_CODEC_ID_H263P;
    else
      id = AV_CODEC_ID_H263;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-intel-h263")) {
    id = AV_CODEC_ID_H263I;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-h261")) {
    id = AV_CODEC_ID_H261;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/mpeg")) {
    gboolean sys_strm;
    gint mpegversion;

    if (gst_structure_get_boolean (structure, "systemstream", &sys_strm) &&
        gst_structure_get_int (structure, "mpegversion", &mpegversion) &&
        !sys_strm) {
      switch (mpegversion) {
        case 1:
          id = AV_CODEC_ID_MPEG1VIDEO;
          break;
        case 2:
          id = AV_CODEC_ID_MPEG2VIDEO;
          break;
        case 4:
          id = AV_CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "image/jpeg")) {
    id = AV_CODEC_ID_MJPEG;     /* A... B... */
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-jpeg-b")) {
    id = AV_CODEC_ID_MJPEGB;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-wmv")) {
    gint wmvversion = 0;

    if (gst_structure_get_int (structure, "wmvversion", &wmvversion)) {
      switch (wmvversion) {
        case 1:
          id = AV_CODEC_ID_WMV1;
          break;
        case 2:
          id = AV_CODEC_ID_WMV2;
          break;
        case 3:
        {
          const gchar *format;

          /* WMV3 unless the fourcc exists and says otherwise */
          id = AV_CODEC_ID_WMV3;

          if ((format = gst_structure_get_string (structure, "format")) &&
              (g_str_equal (format, "WVC1") || g_str_equal (format, "WMVA")))
            id = AV_CODEC_ID_VC1;

          break;
        }
      }
    }
    if (id != AV_CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-vorbis")) {
    id = AV_CODEC_ID_VORBIS;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-qdm2")) {
    id = AV_CODEC_ID_QDM2;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/mpeg")) {
    gint layer = 0;
    gint mpegversion = 0;

    if (gst_structure_get_int (structure, "mpegversion", &mpegversion)) {
      switch (mpegversion) {
        case 2:                /* ffmpeg uses faad for both... */
        case 4:
          id = AV_CODEC_ID_AAC;
          break;
        case 1:
          if (gst_structure_get_int (structure, "layer", &layer)) {
            switch (layer) {
              case 1:
                id = AV_CODEC_ID_MP1;
                break;
              case 2:
                id = AV_CODEC_ID_MP2;
                break;
              case 3:
                id = AV_CODEC_ID_MP3;
                break;
            }
          }
      }
    }
    if (id != AV_CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-musepack")) {
    gint streamversion = -1;

    if (gst_structure_get_int (structure, "streamversion", &streamversion)) {
      if (streamversion == 7)
        id = AV_CODEC_ID_MUSEPACK7;
    } else {
      id = AV_CODEC_ID_MUSEPACK7;
    }
  } else if (!strcmp (mimetype, "audio/x-wma")) {
    gint wmaversion = 0;

    if (gst_structure_get_int (structure, "wmaversion", &wmaversion)) {
      switch (wmaversion) {
        case 1:
          id = AV_CODEC_ID_WMAV1;
          break;
        case 2:
          id = AV_CODEC_ID_WMAV2;
          break;
        case 3:
          id = AV_CODEC_ID_WMAPRO;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-xma")) {
    gint xmaversion = 0;

    if (gst_structure_get_int (structure, "xmaversion", &xmaversion)) {
      switch (xmaversion) {
        case 1:
          id = AV_CODEC_ID_XMA1;
          break;
        case 2:
          id = AV_CODEC_ID_XMA2;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-wms")) {
    id = AV_CODEC_ID_WMAVOICE;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-ac3")) {
    id = AV_CODEC_ID_AC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-eac3")) {
    id = AV_CODEC_ID_EAC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-vnd.sony.atrac3") ||
      !strcmp (mimetype, "audio/atrac3")) {
    id = AV_CODEC_ID_ATRAC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dts")) {
    id = AV_CODEC_ID_DTS;
    audio = TRUE;
  } else if (!strcmp (mimetype, "application/x-ape")) {
    id = AV_CODEC_ID_APE;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-msmpeg")) {
    gint msmpegversion = 0;

    if (gst_structure_get_int (structure, "msmpegversion", &msmpegversion)) {
      switch (msmpegversion) {
        case 41:
          id = AV_CODEC_ID_MSMPEG4V1;
          break;
        case 42:
          id = AV_CODEC_ID_MSMPEG4V2;
          break;
        case 43:
          id = AV_CODEC_ID_MSMPEG4V3;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-svq")) {
    gint svqversion = 0;

    if (gst_structure_get_int (structure, "svqversion", &svqversion)) {
      switch (svqversion) {
        case 1:
          id = AV_CODEC_ID_SVQ1;
          break;
        case 3:
          id = AV_CODEC_ID_SVQ3;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-huffyuv")) {
    id = AV_CODEC_ID_HUFFYUV;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-ffvhuff")) {
    id = AV_CODEC_ID_FFVHUFF;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-mace")) {
    gint maceversion = 0;

    if (gst_structure_get_int (structure, "maceversion", &maceversion)) {
      switch (maceversion) {
        case 3:
          id = AV_CODEC_ID_MACE3;
          break;
        case 6:
          id = AV_CODEC_ID_MACE6;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-theora")) {
    id = AV_CODEC_ID_THEORA;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp3")) {
    id = AV_CODEC_ID_VP3;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp5")) {
    id = AV_CODEC_ID_VP5;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6")) {
    id = AV_CODEC_ID_VP6;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6-flash")) {
    id = AV_CODEC_ID_VP6F;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6-alpha")) {
    id = AV_CODEC_ID_VP6A;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp8")) {
    id = AV_CODEC_ID_VP8;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp9")) {
    id = AV_CODEC_ID_VP9;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-av1")) {
    id = AV_CODEC_ID_AV1;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-flash-screen")) {
    id = AV_CODEC_ID_FLASHSV;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-flash-screen2")) {
    id = AV_CODEC_ID_FLASHSV2;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-cineform")) {
    id = AV_CODEC_ID_CFHD;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-speedhq")) {
    id = AV_CODEC_ID_SPEEDHQ;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-indeo")) {
    gint indeoversion = 0;

    if (gst_structure_get_int (structure, "indeoversion", &indeoversion)) {
      switch (indeoversion) {
        case 5:
          id = AV_CODEC_ID_INDEO5;
          break;
        case 4:
          id = AV_CODEC_ID_INDEO4;
          break;
        case 3:
          id = AV_CODEC_ID_INDEO3;
          break;
        case 2:
          id = AV_CODEC_ID_INDEO2;
          break;
      }
      if (id != AV_CODEC_ID_NONE)
        video = TRUE;
    }
  } else if (!strcmp (mimetype, "video/x-divx")) {
    gint divxversion = 0;

    if (gst_structure_get_int (structure, "divxversion", &divxversion)) {
      switch (divxversion) {
        case 3:
          id = AV_CODEC_ID_MSMPEG4V3;
          break;
        case 4:
        case 5:
          id = AV_CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-ffv")) {
    gint ffvversion = 0;

    if (gst_structure_get_int (structure, "ffvversion", &ffvversion) &&
        ffvversion == 1) {
      id = AV_CODEC_ID_FFV1;
      video = TRUE;
    }
  } else if (!strcmp (mimetype, "video/x-apple-intermediate-codec")) {
    id = AV_CODEC_ID_AIC;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-adpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (layout == NULL) {
      /* break */
    } else if (!strcmp (layout, "quicktime")) {
      id = AV_CODEC_ID_ADPCM_IMA_QT;
    } else if (!strcmp (layout, "microsoft")) {
      id = AV_CODEC_ID_ADPCM_MS;
    } else if (!strcmp (layout, "dvi")) {
      id = AV_CODEC_ID_ADPCM_IMA_WAV;
    } else if (!strcmp (layout, "4xm")) {
      id = AV_CODEC_ID_ADPCM_4XM;
    } else if (!strcmp (layout, "smjpeg")) {
      id = AV_CODEC_ID_ADPCM_IMA_SMJPEG;
    } else if (!strcmp (layout, "dk3")) {
      id = AV_CODEC_ID_ADPCM_IMA_DK3;
    } else if (!strcmp (layout, "dk4")) {
      id = AV_CODEC_ID_ADPCM_IMA_DK4;
    } else if (!strcmp (layout, "oki")) {
      id = AV_CODEC_ID_ADPCM_IMA_OKI;
    } else if (!strcmp (layout, "westwood")) {
      id = AV_CODEC_ID_ADPCM_IMA_WS;
    } else if (!strcmp (layout, "iss")) {
      id = AV_CODEC_ID_ADPCM_IMA_ISS;
    } else if (!strcmp (layout, "xa")) {
      id = AV_CODEC_ID_ADPCM_XA;
    } else if (!strcmp (layout, "adx")) {
      id = AV_CODEC_ID_ADPCM_ADX;
    } else if (!strcmp (layout, "ea")) {
      id = AV_CODEC_ID_ADPCM_EA;
    } else if (!strcmp (layout, "g726")) {
      id = AV_CODEC_ID_ADPCM_G726;
    } else if (!strcmp (layout, "g721")) {
      id = AV_CODEC_ID_ADPCM_G726;
    } else if (!strcmp (layout, "ct")) {
      id = AV_CODEC_ID_ADPCM_CT;
    } else if (!strcmp (layout, "swf")) {
      id = AV_CODEC_ID_ADPCM_SWF;
    } else if (!strcmp (layout, "yamaha")) {
      id = AV_CODEC_ID_ADPCM_YAMAHA;
    } else if (!strcmp (layout, "sbpro2")) {
      id = AV_CODEC_ID_ADPCM_SBPRO_2;
    } else if (!strcmp (layout, "sbpro3")) {
      id = AV_CODEC_ID_ADPCM_SBPRO_3;
    } else if (!strcmp (layout, "sbpro4")) {
      id = AV_CODEC_ID_ADPCM_SBPRO_4;
    }
    if (id != AV_CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-4xm")) {
    id = AV_CODEC_ID_4XM;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (!layout) {
      /* .. */
    } else if (!strcmp (layout, "roq")) {
      id = AV_CODEC_ID_ROQ_DPCM;
    } else if (!strcmp (layout, "interplay")) {
      id = AV_CODEC_ID_INTERPLAY_DPCM;
    } else if (!strcmp (layout, "xan")) {
      id = AV_CODEC_ID_XAN_DPCM;
    } else if (!strcmp (layout, "sol")) {
      id = AV_CODEC_ID_SOL_DPCM;
    }
    if (id != AV_CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-flac")) {
    id = AV_CODEC_ID_FLAC;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-shorten")) {
    id = AV_CODEC_ID_SHORTEN;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-alac")) {
    id = AV_CODEC_ID_ALAC;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-cinepak")) {
    id = AV_CODEC_ID_CINEPAK;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-pn-realvideo")) {
    gint rmversion;

    if (gst_structure_get_int (structure, "rmversion", &rmversion)) {
      switch (rmversion) {
        case 1:
          id = AV_CODEC_ID_RV10;
          break;
        case 2:
          id = AV_CODEC_ID_RV20;
          break;
        case 3:
          id = AV_CODEC_ID_RV30;
          break;
        case 4:
          id = AV_CODEC_ID_RV40;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-sipro")) {
    id = AV_CODEC_ID_SIPR;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-pn-realaudio")) {
    gint raversion;

    if (gst_structure_get_int (structure, "raversion", &raversion)) {
      switch (raversion) {
        case 1:
          id = AV_CODEC_ID_RA_144;
          break;
        case 2:
          id = AV_CODEC_ID_RA_288;
          break;
        case 8:
          id = AV_CODEC_ID_COOK;
          break;
      }
    }
    if (id != AV_CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-rle")) {
    const gchar *layout;

    if ((layout = gst_structure_get_string (structure, "layout"))) {
      if (!strcmp (layout, "microsoft")) {
        id = AV_CODEC_ID_MSRLE;
        video = TRUE;
      }
    }
  } else if (!strcmp (mimetype, "video/x-xan")) {
    gint wcversion = 0;

    if ((gst_structure_get_int (structure, "wcversion", &wcversion))) {
      switch (wcversion) {
        case 3:
          id = AV_CODEC_ID_XAN_WC3;
          video = TRUE;
          break;
        case 4:
          id = AV_CODEC_ID_XAN_WC4;
          video = TRUE;
          break;
        default:
          break;
      }
    }
  } else if (!strcmp (mimetype, "audio/AMR")) {
    audio = TRUE;
    id = AV_CODEC_ID_AMR_NB;
  } else if (!strcmp (mimetype, "audio/AMR-WB")) {
    id = AV_CODEC_ID_AMR_WB;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/qcelp")) {
    id = AV_CODEC_ID_QCELP;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-h264")) {
    id = AV_CODEC_ID_H264;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-h265")) {
    id = AV_CODEC_ID_HEVC;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-flash-video")) {
    gint flvversion = 0;

    if ((gst_structure_get_int (structure, "flvversion", &flvversion))) {
      switch (flvversion) {
        case 1:
          id = AV_CODEC_ID_FLV1;
          video = TRUE;
          break;
        default:
          break;
      }
    }

  } else if (!strcmp (mimetype, "audio/x-nellymoser")) {
    id = AV_CODEC_ID_NELLYMOSER;
    audio = TRUE;
  } else if (!strncmp (mimetype, "audio/x-gst-av-", 15)) {
    gchar ext[16];
    const AVCodec *codec;

    if (strlen (mimetype) <= 30 &&
        sscanf (mimetype, "audio/x-gst-av-%s", ext) == 1) {
      if ((codec = avcodec_find_decoder_by_name (ext)) ||
          (codec = avcodec_find_encoder_by_name (ext))) {
        id = codec->id;
        audio = TRUE;
      }
    }
  } else if (!strncmp (mimetype, "video/x-gst-av-", 15)) {
    gchar ext[16];
    const AVCodec *codec;

    if (strlen (mimetype) <= 30 &&
        sscanf (mimetype, "video/x-gst-av-%s", ext) == 1) {
      if ((codec = avcodec_find_decoder_by_name (ext)) ||
          (codec = avcodec_find_encoder_by_name (ext))) {
        id = codec->id;
        video = TRUE;
      }
    }
  }

  if (context != NULL) {
    if (video == TRUE) {
      context->codec_type = AVMEDIA_TYPE_VIDEO;
    } else if (audio == TRUE) {
      context->codec_type = AVMEDIA_TYPE_AUDIO;
    } else {
      context->codec_type = AVMEDIA_TYPE_UNKNOWN;
    }
    context->codec_id = id;
    gst_ffmpeg_caps_with_codecid (id, context->codec_type, caps, context);
  }

  if (id != AV_CODEC_ID_NONE) {
    GST_DEBUG ("The id=%d belongs to the caps %" GST_PTR_FORMAT, id, caps);
  } else {
    GST_WARNING ("Couldn't figure out the id for caps %" GST_PTR_FORMAT, caps);
  }

  return id;
}
