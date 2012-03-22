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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gst/gst.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <libavcodec/avcodec.h>
#endif
#include <string.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/codec-utils.h>

/*
 * Read a palette from a caps.
 */

static void
gst_ffmpeg_get_palette (const GstCaps * caps, AVCodecContext * context)
{
  GstStructure *str = gst_caps_get_structure (caps, 0);
  const GValue *palette_v;
  GstBuffer *palette;

  /* do we have a palette? */
  if ((palette_v = gst_structure_get_value (str, "palette_data")) && context) {
    palette = gst_value_get_buffer (palette_v);
    if (gst_buffer_get_size (palette) >= AVPALETTE_SIZE) {
      if (context->palctrl)
        av_free (context->palctrl);
      context->palctrl = av_malloc (sizeof (AVPaletteControl));
      context->palctrl->palette_changed = 1;
      gst_buffer_extract (palette, 0, context->palctrl->palette,
          AVPALETTE_SIZE);
    }
  }
}

static void
gst_ffmpeg_set_palette (GstCaps * caps, AVCodecContext * context)
{
  if (context->palctrl) {
    GstBuffer *palette = gst_buffer_new_and_alloc (AVPALETTE_SIZE);

    gst_buffer_fill (palette, 0, context->palctrl->palette, AVPALETTE_SIZE);
    gst_caps_set_simple (caps, "palette_data", GST_TYPE_BUFFER, palette, NULL);
  }
}

/* IMPORTANT: Keep this sorted by the ffmpeg channel masks */
static const struct
{
  guint64 ff;
  GstAudioChannelPosition gst;
} _ff_to_gst_layout[] = {
  {
  CH_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  CH_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
  CH_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
  CH_LOW_FREQUENCY, GST_AUDIO_CHANNEL_POSITION_LFE1}, {
  CH_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_REAR_LEFT}, {
  CH_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
  CH_FRONT_LEFT_OF_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER}, {
  CH_FRONT_RIGHT_OF_CENTER, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
  CH_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
  CH_SIDE_LEFT, GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT}, {
  CH_SIDE_RIGHT, GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}, {
  CH_TOP_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_CENTER}, {
  CH_TOP_FRONT_LEFT, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT}, {
  CH_TOP_FRONT_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER}, {
  CH_TOP_FRONT_RIGHT, GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT}, {
  CH_TOP_BACK_LEFT, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT}, {
  CH_TOP_BACK_CENTER, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER}, {
  CH_TOP_BACK_RIGHT, GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT}, {
  CH_STEREO_LEFT, GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT}, {
  CH_STEREO_RIGHT, GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}
};

gboolean
gst_ffmpeg_channel_layout_to_gst (AVCodecContext * context,
    GstAudioChannelPosition * pos)
{
  guint nchannels = 0, channels = context->channels;
  guint64 channel_layout = context->channel_layout;
  gboolean none_layout = FALSE;

  if (channel_layout == 0) {
    nchannels = channels;
    none_layout = TRUE;
  } else {
    guint i, j;

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

      for (i = 0; i < nchannels; i++)
        pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    }
  }

  return TRUE;
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
gst_ff_vid_caps_new (AVCodecContext * context, enum CodecID codec_id,
    const char *mimetype, const char *fieldname, ...)
{
  GstStructure *structure = NULL;
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

    num = context->time_base.den / context->ticks_per_frame;
    denom = context->time_base.num;

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
  } else {
    /* so we are after restricted caps in this case */
    switch (codec_id) {
      case CODEC_ID_H261:
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
      case CODEC_ID_H263:
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
      case CODEC_ID_DVVIDEO:
      {
        static struct
        {
          guint32 csp;
          gint width, height;
          gint par_n, par_d;
          gint framerate_n, framerate_d;
        } profiles[] = {
          {
          GST_MAKE_FOURCC ('Y', '4', '1', 'B'), 720, 480, 10, 11, 30000, 1001}, {
          GST_MAKE_FOURCC ('Y', '4', '1', 'B'), 720, 480, 40, 33, 30000, 1001}, {
          GST_MAKE_FOURCC ('I', '4', '2', '0'), 720, 576, 59, 54, 25, 1}, {
          GST_MAKE_FOURCC ('I', '4', '2', '0'), 720, 576, 118, 81, 25, 1}, {
          GST_MAKE_FOURCC ('Y', '4', '1', 'B'), 720, 576, 59, 54, 25, 1}, {
          GST_MAKE_FOURCC ('Y', '4', '1', 'B'), 720, 576, 118, 81, 25, 1}
        };
        GstCaps *temp;
        gint n_sizes = G_N_ELEMENTS (profiles);

        caps = gst_caps_new_empty ();
        for (i = 0; i < n_sizes; i++) {
          temp = gst_caps_new_simple (mimetype,
              "width", G_TYPE_INT, profiles[i].width,
              "height", G_TYPE_INT, profiles[i].height,
              "framerate", GST_TYPE_FRACTION, profiles[i].framerate_n,
              profiles[i].framerate_d, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              profiles[i].par_n, profiles[i].par_d, NULL);

          gst_caps_append (caps, temp);
        }
        break;
      }
      case CODEC_ID_DNXHD:
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
        break;
    }
  }

  /* no fixed caps or special restrictions applied;
   * default unfixed setting */
  if (!caps) {
    GST_DEBUG ("Creating default caps");
    caps = gst_caps_new_simple (mimetype, NULL, NULL, NULL);
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    va_start (var_args, fieldname);
    structure = gst_caps_get_structure (caps, i);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
}

/* same for audio - now with channels/sample rate
 */
static GstCaps *
gst_ff_aud_caps_new (AVCodecContext * context, enum CodecID codec_id,
    const char *mimetype, const char *fieldname, ...)
{
  GstCaps *caps = NULL;
  GstStructure *structure = NULL;
  gint i;
  va_list var_args;

  /* fixed, non-probing context */
  if (context != NULL && context->channels != -1) {
    GstAudioChannelPosition pos[64];

    caps = gst_caps_new_simple (mimetype,
        "rate", G_TYPE_INT, context->sample_rate,
        "channels", G_TYPE_INT, context->channels, NULL);

    if (gst_ffmpeg_channel_layout_to_gst (context, pos)) {
      guint64 mask;

      if (gst_audio_channel_positions_to_mask (pos, context->channels, &mask)) {
        gst_caps_set_simple (caps, "channel-mask", GST_TYPE_BITMASK, mask,
            NULL);
      }
    }
  } else {
    gint maxchannels = 2;
    const gint *rates = NULL;
    gint n_rates = 0;

    /* so we must be after restricted caps in this case */
    switch (codec_id) {
      case CODEC_ID_AAC:
      case CODEC_ID_AAC_LATM:
      case CODEC_ID_DTS:
        maxchannels = 6;
        break;
      case CODEC_ID_MP2:
      {
        const static gint l_rates[] =
            { 48000, 44100, 32000, 24000, 22050, 16000 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case CODEC_ID_EAC3:
      case CODEC_ID_AC3:
      {
        const static gint l_rates[] = { 48000, 44100, 32000 };
        maxchannels = 6;
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case CODEC_ID_ADPCM_G722:
      {
        const static gint l_rates[] = { 16000 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        maxchannels = 1;
        break;
      }
      case CODEC_ID_ADPCM_G726:
      {
        const static gint l_rates[] = { 8000 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        maxchannels = 1;
        break;
      }
      case CODEC_ID_ADPCM_SWF:
      {
        const static gint l_rates[] = { 11025, 22050, 44100 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case CODEC_ID_ROQ_DPCM:
      {
        const static gint l_rates[] = { 22050 };
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case CODEC_ID_AMR_NB:
      {
        const static gint l_rates[] = { 8000 };
        maxchannels = 1;
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      case CODEC_ID_AMR_WB:
      {
        const static gint l_rates[] = { 16000 };
        maxchannels = 1;
        n_rates = G_N_ELEMENTS (l_rates);
        rates = l_rates;
        break;
      }
      default:
        break;
    }

    /* TODO: handle context->channel_layouts here to set
     * the list of channel layouts supported by the encoder.
     * Unfortunately no encoder uses this yet....
     */
    /* regardless of encode/decode, open up channels if applicable */
    /* Until decoders/encoders expose the maximum number of channels
     * they support, we whitelist them here. */
    switch (codec_id) {
      case CODEC_ID_WMAPRO:
      case CODEC_ID_TRUEHD:
        maxchannels = 8;
        break;
      default:
        break;
    }

    if (maxchannels == 1)
      caps = gst_caps_new_simple (mimetype,
          "channels", G_TYPE_INT, maxchannels, NULL);
    else
      caps = gst_caps_new_simple (mimetype,
          "channels", GST_TYPE_INT_RANGE, 1, maxchannels, NULL);
    if (n_rates) {
      GValue list = { 0, };
      GstStructure *structure;

      g_value_init (&list, GST_TYPE_LIST);
      for (i = 0; i < n_rates; i++) {
        GValue v = { 0, };

        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, rates[i]);
        gst_value_list_append_value (&list, &v);
        g_value_unset (&v);
      }
      structure = gst_caps_get_structure (caps, 0);
      gst_structure_set_value (structure, "rate", &list);
      g_value_unset (&list);
    } else
      gst_caps_set_simple (caps, "rate", GST_TYPE_INT_RANGE, 4000, 96000, NULL);
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    va_start (var_args, fieldname);
    structure = gst_caps_get_structure (caps, i);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }

  return caps;
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
gst_ffmpeg_codecid_to_caps (enum CodecID codec_id,
    AVCodecContext * context, gboolean encode)
{
  GstCaps *caps = NULL;
  gboolean buildcaps = FALSE;

  GST_LOG ("codec_id:%d, context:%p, encode:%d", codec_id, context, encode);

  switch (codec_id) {
    case CODEC_ID_MPEG1VIDEO:
      /* FIXME: bitrate */
      caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
          "mpegversion", G_TYPE_INT, 1,
          "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      break;

    case CODEC_ID_MPEG2VIDEO:
      if (encode) {
        /* FIXME: bitrate */
        caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
            "mpegversion", G_TYPE_INT, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      } else {
        /* decode both MPEG-1 and MPEG-2; width/height/fps are all in
         * the MPEG video stream headers, so may be omitted from caps. */
        caps = gst_caps_new_simple ("video/mpeg",
            "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
      break;

    case CODEC_ID_MPEG2VIDEO_XVMC:
      /* this is a special ID - don't need it in GStreamer, I think */
      break;

    case CODEC_ID_H263:
      if (encode) {
        caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h263",
            "variant", G_TYPE_STRING, "itu",
            "h263version", G_TYPE_STRING, "h263", NULL);
      } else {
        /* don't pass codec_id, we can decode other variants with the H263
         * decoder that don't have specific size requirements
         */
        caps = gst_ff_vid_caps_new (context, CODEC_ID_NONE, "video/x-h263",
            "variant", G_TYPE_STRING, "itu", NULL);
      }
      break;

    case CODEC_ID_H263P:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h263",
          "variant", G_TYPE_STRING, "itu",
          "h263version", G_TYPE_STRING, "h263p", NULL);
      if (encode && context) {

        gst_caps_set_simple (caps,
            "annex-f", G_TYPE_BOOLEAN, context->flags & CODEC_FLAG_4MV,
            "annex-j", G_TYPE_BOOLEAN, context->flags & CODEC_FLAG_LOOP_FILTER,
            "annex-i", G_TYPE_BOOLEAN, context->flags & CODEC_FLAG_AC_PRED,
            "annex-t", G_TYPE_BOOLEAN, context->flags & CODEC_FLAG_AC_PRED,
            NULL);
      }
      break;

    case CODEC_ID_H263I:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-intel-h263",
          "variant", G_TYPE_STRING, "intel", NULL);
      break;

    case CODEC_ID_H261:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h261", NULL);
      break;

    case CODEC_ID_RV10:
    case CODEC_ID_RV20:
    case CODEC_ID_RV30:
    case CODEC_ID_RV40:
    {
      gint version;

      switch (codec_id) {
        case CODEC_ID_RV40:
          version = 4;
          break;
        case CODEC_ID_RV30:
          version = 3;
          break;
        case CODEC_ID_RV20:
          version = 2;
          break;
        default:
          version = 1;
          break;
      }

      /* FIXME: context->sub_id must be filled in during decoding */
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-pn-realvideo",
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "rmversion", G_TYPE_INT, version, NULL);
      if (context) {
        gst_caps_set_simple (caps, "format", G_TYPE_INT, context->sub_id, NULL);
        if (context->extradata_size >= 8) {
          gst_caps_set_simple (caps,
              "subformat", G_TYPE_INT, GST_READ_UINT32_BE (context->extradata),
              NULL);
        }
      }
    }
      break;

    case CODEC_ID_MP1:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_MP2:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg",
          "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_MP3:
      if (encode) {
        /* FIXME: bitrate */
        caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg",
            "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, 3, NULL);
      } else {
        /* Decodes MPEG-1 layer 1/2/3. Samplerate, channels et al are
         * in the MPEG audio header, so may be omitted from caps. */
        caps = gst_caps_new_simple ("audio/mpeg",
            "mpegversion", G_TYPE_INT, 1,
            "layer", GST_TYPE_INT_RANGE, 1, 3, NULL);
      }
      break;

    case CODEC_ID_MUSEPACK7:
      caps =
          gst_ff_aud_caps_new (context, codec_id,
          "audio/x-ffmpeg-parsed-musepack", "streamversion", G_TYPE_INT, 7,
          NULL);
      break;

    case CODEC_ID_MUSEPACK8:
      caps =
          gst_ff_aud_caps_new (context, codec_id,
          "audio/x-ffmpeg-parsed-musepack", "streamversion", G_TYPE_INT, 8,
          NULL);
      break;

    case CODEC_ID_AC3:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-ac3", NULL);
      break;

    case CODEC_ID_EAC3:
      /* FIXME: bitrate */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-eac3", NULL);
      break;

    case CODEC_ID_TRUEHD:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-true-hd", NULL);
      break;

    case CODEC_ID_ATRAC1:
      caps =
          gst_ff_aud_caps_new (context, codec_id, "audio/x-vnd.sony.atrac1",
          NULL);
      break;

    case CODEC_ID_ATRAC3:
      caps =
          gst_ff_aud_caps_new (context, codec_id, "audio/x-vnd.sony.atrac3",
          NULL);
      break;

    case CODEC_ID_DTS:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-dts", NULL);
      break;

    case CODEC_ID_APE:
      caps =
          gst_ff_aud_caps_new (context, codec_id, "audio/x-ffmpeg-parsed-ape",
          NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;

    case CODEC_ID_MLP:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-mlp", NULL);
      break;

    case CODEC_ID_IMC:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-imc", NULL);
      break;

      /* MJPEG is normal JPEG, Motion-JPEG and Quicktime MJPEG-A. MJPEGB
       * is Quicktime's MJPEG-B. LJPEG is lossless JPEG. I don't know what
       * sp5x is, but it's apparently something JPEG... We don't separate
       * between those in GStreamer. Should we (at least between MJPEG,
       * MJPEG-B and sp5x decoding...)? */
    case CODEC_ID_MJPEG:
    case CODEC_ID_LJPEG:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/jpeg", NULL);
      break;

    case CODEC_ID_SP5X:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/sp5x", NULL);
      break;

    case CODEC_ID_MJPEGB:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-mjpeg-b", NULL);
      break;

    case CODEC_ID_MPEG4:
      if (encode && context != NULL) {
        /* I'm not exactly sure what ffmpeg outputs... ffmpeg itself uses
         * the AVI fourcc 'DIVX', but 'mp4v' for Quicktime... */
        switch (context->codec_tag) {
          case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
            caps = gst_ff_vid_caps_new (context, codec_id, "video/x-divx",
                "divxversion", G_TYPE_INT, 5, NULL);
            break;
          case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
          default:
            /* FIXME: bitrate */
            caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
                "systemstream", G_TYPE_BOOLEAN, FALSE,
                "mpegversion", G_TYPE_INT, 4, NULL);
            break;
        }
      } else {
        /* The trick here is to separate xvid, divx, mpeg4, 3ivx et al */
        caps = gst_ff_vid_caps_new (context, codec_id, "video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
        if (encode) {
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-divx", "divxversion", G_TYPE_INT, 5, NULL));
        } else {
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-divx", "divxversion", GST_TYPE_INT_RANGE, 4, 5,
                  NULL));
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-xvid", NULL));
          gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                  "video/x-3ivx", NULL));
        }
      }
      break;

    case CODEC_ID_RAWVIDEO:
      caps =
          gst_ffmpeg_codectype_to_caps (AVMEDIA_TYPE_VIDEO, context, codec_id,
          encode);
      break;

    case CODEC_ID_MSMPEG4V1:
    case CODEC_ID_MSMPEG4V2:
    case CODEC_ID_MSMPEG4V3:
    {
      gint version = 41 + codec_id - CODEC_ID_MSMPEG4V1;

      /* encode-FIXME: bitrate */
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-msmpeg",
          "msmpegversion", G_TYPE_INT, version, NULL);
      if (!encode && codec_id == CODEC_ID_MSMPEG4V3) {
        gst_caps_append (caps, gst_ff_vid_caps_new (context, codec_id,
                "video/x-divx", "divxversion", G_TYPE_INT, 3, NULL));
      }
    }
      break;

    case CODEC_ID_WMV1:
    case CODEC_ID_WMV2:
    {
      gint version = (codec_id == CODEC_ID_WMV1) ? 1 : 2;

      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-wmv",
          "wmvversion", G_TYPE_INT, version, NULL);
    }
      break;

    case CODEC_ID_FLV1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-flash-video",
          "flvversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_SVQ1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-svq",
          "svqversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_SVQ3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-svq",
          "svqversion", G_TYPE_INT, 3, NULL);
      break;

    case CODEC_ID_DVAUDIO:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-dv", NULL);
      break;

    case CODEC_ID_DVVIDEO:
    {
      if (encode && context) {
        const gchar *format;

        switch (context->pix_fmt) {
          case PIX_FMT_YUYV422:
            format = "YUY2";
            break;
          case PIX_FMT_YUV420P:
            format = "I420";
            break;
          case PIX_FMT_YUVA420P:
            format = "A420";
            break;
          case PIX_FMT_YUV411P:
            format = "Y41B";
            break;
          case PIX_FMT_YUV422P:
            format = "Y42B";
            break;
          case PIX_FMT_YUV410P:
            format = "YUV9";
            break;
          default:
            GST_WARNING
                ("Couldnt' find format for pixfmt %d, defaulting to I420",
                context->pix_fmt);
            format = "I420";
            break;
        }
        caps = gst_ff_vid_caps_new (context, codec_id, "video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, FALSE,
            "format", G_TYPE_STRING, format, NULL);
      } else {
        caps = gst_ff_vid_caps_new (context, codec_id, "video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
      }
    }
      break;

    case CODEC_ID_WMAV1:
    case CODEC_ID_WMAV2:
    {
      gint version = (codec_id == CODEC_ID_WMAV1) ? 1 : 2;

      if (context) {
        caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-wma",
            "wmaversion", G_TYPE_INT, version,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      } else {
        caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-wma",
            "wmaversion", G_TYPE_INT, version,
            "block_align", GST_TYPE_INT_RANGE, 0, G_MAXINT,
            "bitrate", GST_TYPE_INT_RANGE, 0, G_MAXINT, NULL);
      }
    }
      break;
    case CODEC_ID_WMAPRO:
    {
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-wma",
          "wmaversion", G_TYPE_INT, 3, NULL);
      break;
    }

    case CODEC_ID_WMAVOICE:
    {
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-wms", NULL);
      break;
    }

    case CODEC_ID_MACE3:
    case CODEC_ID_MACE6:
    {
      gint version = (codec_id == CODEC_ID_MACE3) ? 3 : 6;

      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-mace",
          "maceversion", G_TYPE_INT, version, NULL);
    }
      break;

    case CODEC_ID_HUFFYUV:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-huffyuv", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "bpp", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;

    case CODEC_ID_CYUV:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "video/x-compressed-yuv",
          NULL);
      break;

    case CODEC_ID_H264:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-h264", NULL);
      break;

    case CODEC_ID_INDEO5:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 5, NULL);
      break;

    case CODEC_ID_INDEO4:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 4, NULL);
      break;

    case CODEC_ID_INDEO3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 3, NULL);
      break;

    case CODEC_ID_INDEO2:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-indeo",
          "indeoversion", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_FLASHSV:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "video/x-flash-screen", NULL);
      break;

    case CODEC_ID_VP3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp3", NULL);
      break;

    case CODEC_ID_VP5:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp5", NULL);
      break;

    case CODEC_ID_VP6:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp6", NULL);
      break;

    case CODEC_ID_VP6F:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp6-flash", NULL);
      break;

    case CODEC_ID_VP6A:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp6-alpha", NULL);
      break;

    case CODEC_ID_VP8:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vp8", NULL);
      break;

    case CODEC_ID_THEORA:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-theora", NULL);
      break;

    case CODEC_ID_AAC:
    {
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg", NULL);

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
            "stream-format", G_TYPE_STRING, "raw",
            "base-profile", G_TYPE_STRING, "lc", NULL);

        if (context && context->extradata_size > 0)
          gst_codec_utils_aac_caps_set_level_and_profile (caps,
              context->extradata, context->extradata_size);
      }

      break;
    }
    case CODEC_ID_AAC_LATM:    /* LATM/LOAS AAC syntax */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, "stream-format", G_TYPE_STRING, "loas",
          NULL);
      break;

    case CODEC_ID_ASV1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-asus",
          "asusversion", G_TYPE_INT, 1, NULL);
      break;
    case CODEC_ID_ASV2:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-asus",
          "asusversion", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_FFV1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-ffv",
          "ffvversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_4XM:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-4xm", NULL);
      break;

    case CODEC_ID_XAN_WC3:
    case CODEC_ID_XAN_WC4:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-xan",
          "wcversion", G_TYPE_INT, 3 - CODEC_ID_XAN_WC3 + codec_id, NULL);
      break;

    case CODEC_ID_CLJR:
      caps =
          gst_ff_vid_caps_new (context, codec_id,
          "video/x-cirrus-logic-accupak", NULL);
      break;

    case CODEC_ID_FRAPS:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-fraps", NULL);
      break;

    case CODEC_ID_MDEC:
    case CODEC_ID_ROQ:
    case CODEC_ID_INTERPLAY_VIDEO:
      buildcaps = TRUE;
      break;

    case CODEC_ID_VCR1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-ati-vcr",
          "vcrversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_RPZA:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "video/x-apple-video", NULL);
      break;

    case CODEC_ID_CINEPAK:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-cinepak", NULL);
      break;

      /* WS_VQA belogns here (order) */

    case CODEC_ID_MSRLE:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-rle",
          "layout", G_TYPE_STRING, "microsoft", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      break;

    case CODEC_ID_QTRLE:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-rle",
          "layout", G_TYPE_STRING, "quicktime", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 1, 64, NULL);
      }
      break;

    case CODEC_ID_MSVIDEO1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-msvideocodec",
          "msvideoversion", G_TYPE_INT, 1, NULL);
      break;

    case CODEC_ID_WMV3:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, NULL);
      break;
    case CODEC_ID_VC1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-wmv",
          "wmvversion", G_TYPE_INT, 3, "format", G_TYPE_STRING, "WVC1", NULL);
      break;
    case CODEC_ID_QDM2:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-qdm2", NULL);
      break;

    case CODEC_ID_MSZH:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-mszh", NULL);
      break;

    case CODEC_ID_ZLIB:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-zlib", NULL);
      break;

    case CODEC_ID_TRUEMOTION1:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-truemotion",
          "trueversion", G_TYPE_INT, 1, NULL);
      break;
    case CODEC_ID_TRUEMOTION2:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-truemotion",
          "trueversion", G_TYPE_INT, 2, NULL);
      break;

    case CODEC_ID_ULTI:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-ultimotion",
          NULL);
      break;

    case CODEC_ID_TSCC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-camtasia", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 8, 32, NULL);
      }
      break;

    case CODEC_ID_KMVC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-kmvc", NULL);
      break;

    case CODEC_ID_NUV:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-nuv", NULL);
      break;

    case CODEC_ID_GIF:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/gif", NULL);
      break;

    case CODEC_ID_PNG:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/png", NULL);
      break;

    case CODEC_ID_PPM:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/ppm", NULL);
      break;

    case CODEC_ID_PBM:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/pbm", NULL);
      break;

    case CODEC_ID_PAM:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "image/x-portable-anymap",
          NULL);
      break;

    case CODEC_ID_PGM:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "image/x-portable-graymap",
          NULL);
      break;

    case CODEC_ID_PCX:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/x-pcx", NULL);
      break;

    case CODEC_ID_SGI:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/x-sgi", NULL);
      break;

    case CODEC_ID_TARGA:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/x-tga", NULL);
      break;

    case CODEC_ID_TIFF:
      caps = gst_ff_vid_caps_new (context, codec_id, "image/tiff", NULL);
      break;

    case CODEC_ID_SUNRAST:
      caps =
          gst_ff_vid_caps_new (context, codec_id, "image/x-sun-raster", NULL);
      break;

    case CODEC_ID_SMC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-smc", NULL);
      break;

    case CODEC_ID_QDRAW:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-qdrw", NULL);
      break;

    case CODEC_ID_DNXHD:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-dnxhd", NULL);
      break;

    case CODEC_ID_MIMIC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-mimic", NULL);
      break;

    case CODEC_ID_VMNC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-vmnc", NULL);
      break;

    case CODEC_ID_TRUESPEECH:
      caps =
          gst_ff_aud_caps_new (context, codec_id, "audio/x-truespeech", NULL);
      break;

    case CODEC_ID_QCELP:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/qcelp", NULL);
      break;

    case CODEC_ID_AMV:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-amv", NULL);
      break;

    case CODEC_ID_AASC:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-aasc", NULL);
      break;

    case CODEC_ID_LOCO:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-loco", NULL);
      break;

    case CODEC_ID_ZMBV:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-zmbv", NULL);
      break;

    case CODEC_ID_LAGARITH:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-lagarith", NULL);
      break;

    case CODEC_ID_CSCD:
      caps = gst_ff_vid_caps_new (context, codec_id, "video/x-camstudio", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "depth", G_TYPE_INT, (gint) context->bits_per_coded_sample, NULL);
      } else {
        gst_caps_set_simple (caps, "depth", GST_TYPE_INT_RANGE, 8, 32, NULL);
      }
      break;

    case CODEC_ID_WS_VQA:
    case CODEC_ID_IDCIN:
    case CODEC_ID_8BPS:
    case CODEC_ID_FLIC:
    case CODEC_ID_VMDVIDEO:
    case CODEC_ID_VMDAUDIO:
    case CODEC_ID_SONIC:
    case CODEC_ID_SONIC_LS:
    case CODEC_ID_SNOW:
    case CODEC_ID_VIXL:
    case CODEC_ID_QPEG:
    case CODEC_ID_PGMYUV:
    case CODEC_ID_FFVHUFF:
    case CODEC_ID_WNV1:
    case CODEC_ID_MP3ADU:
    case CODEC_ID_MP3ON4:
    case CODEC_ID_WESTWOOD_SND1:
    case CODEC_ID_MMVIDEO:
    case CODEC_ID_AVS:
    case CODEC_ID_CAVS:
      buildcaps = TRUE;
      break;

      /* weird quasi-codecs for the demuxers only */
    case CODEC_ID_PCM_S16LE:
    case CODEC_ID_PCM_S16BE:
    case CODEC_ID_PCM_U16LE:
    case CODEC_ID_PCM_U16BE:
    case CODEC_ID_PCM_S8:
    case CODEC_ID_PCM_U8:
    {
      GstAudioFormat format;

      switch (codec_id) {
        case CODEC_ID_PCM_S16LE:
          format = GST_AUDIO_FORMAT_S16LE;
          break;
        case CODEC_ID_PCM_S16BE:
          format = GST_AUDIO_FORMAT_S16BE;
          break;
        case CODEC_ID_PCM_U16LE:
          format = GST_AUDIO_FORMAT_U16LE;
          break;
        case CODEC_ID_PCM_U16BE:
          format = GST_AUDIO_FORMAT_U16BE;
          break;
        case CODEC_ID_PCM_S8:
          format = GST_AUDIO_FORMAT_S8;
          break;
        case CODEC_ID_PCM_U8:
          format = GST_AUDIO_FORMAT_U8;
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-raw",
          "format", G_TYPE_STRING, gst_audio_format_to_string (format),
          "layout", G_TYPE_STRING, "interleaved", NULL);
    }
      break;

    case CODEC_ID_PCM_MULAW:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-mulaw", NULL);
      break;

    case CODEC_ID_PCM_ALAW:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-alaw", NULL);
      break;

    case CODEC_ID_ADPCM_G722:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/G722", NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      break;

    case CODEC_ID_ADPCM_G726:
    {
      /* the G726 decoder can also handle G721 */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-adpcm",
          "layout", G_TYPE_STRING, "g726", NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);

      if (!encode) {
        gst_caps_append (caps, gst_caps_new_simple ("audio/x-adpcm",
                "layout", G_TYPE_STRING, "g721",
                "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, 8000, NULL));
      }
      break;
    }
    case CODEC_ID_ADPCM_IMA_QT:
    case CODEC_ID_ADPCM_IMA_WAV:
    case CODEC_ID_ADPCM_IMA_DK3:
    case CODEC_ID_ADPCM_IMA_DK4:
    case CODEC_ID_ADPCM_IMA_WS:
    case CODEC_ID_ADPCM_IMA_SMJPEG:
    case CODEC_ID_ADPCM_IMA_AMV:
    case CODEC_ID_ADPCM_IMA_ISS:
    case CODEC_ID_ADPCM_IMA_EA_EACS:
    case CODEC_ID_ADPCM_IMA_EA_SEAD:
    case CODEC_ID_ADPCM_MS:
    case CODEC_ID_ADPCM_4XM:
    case CODEC_ID_ADPCM_XA:
    case CODEC_ID_ADPCM_ADX:
    case CODEC_ID_ADPCM_EA:
    case CODEC_ID_ADPCM_CT:
    case CODEC_ID_ADPCM_SWF:
    case CODEC_ID_ADPCM_YAMAHA:
    case CODEC_ID_ADPCM_SBPRO_2:
    case CODEC_ID_ADPCM_SBPRO_3:
    case CODEC_ID_ADPCM_SBPRO_4:
    case CODEC_ID_ADPCM_EA_R1:
    case CODEC_ID_ADPCM_EA_R2:
    case CODEC_ID_ADPCM_EA_R3:
    case CODEC_ID_ADPCM_EA_MAXIS_XA:
    case CODEC_ID_ADPCM_EA_XAS:
    case CODEC_ID_ADPCM_THP:
    {
      const gchar *layout = NULL;

      switch (codec_id) {
        case CODEC_ID_ADPCM_IMA_QT:
          layout = "quicktime";
          break;
        case CODEC_ID_ADPCM_IMA_WAV:
          layout = "dvi";
          break;
        case CODEC_ID_ADPCM_IMA_DK3:
          layout = "dk3";
          break;
        case CODEC_ID_ADPCM_IMA_DK4:
          layout = "dk4";
          break;
        case CODEC_ID_ADPCM_IMA_WS:
          layout = "westwood";
          break;
        case CODEC_ID_ADPCM_IMA_SMJPEG:
          layout = "smjpeg";
          break;
        case CODEC_ID_ADPCM_IMA_AMV:
          layout = "amv";
          break;
        case CODEC_ID_ADPCM_IMA_ISS:
          layout = "iss";
          break;
        case CODEC_ID_ADPCM_IMA_EA_EACS:
          layout = "ea-eacs";
          break;
        case CODEC_ID_ADPCM_IMA_EA_SEAD:
          layout = "ea-sead";
          break;
        case CODEC_ID_ADPCM_MS:
          layout = "microsoft";
          break;
        case CODEC_ID_ADPCM_4XM:
          layout = "4xm";
          break;
        case CODEC_ID_ADPCM_XA:
          layout = "xa";
          break;
        case CODEC_ID_ADPCM_ADX:
          layout = "adx";
          break;
        case CODEC_ID_ADPCM_EA:
          layout = "ea";
          break;
        case CODEC_ID_ADPCM_CT:
          layout = "ct";
          break;
        case CODEC_ID_ADPCM_SWF:
          layout = "swf";
          break;
        case CODEC_ID_ADPCM_YAMAHA:
          layout = "yamaha";
          break;
        case CODEC_ID_ADPCM_SBPRO_2:
          layout = "sbpro2";
          break;
        case CODEC_ID_ADPCM_SBPRO_3:
          layout = "sbpro3";
          break;
        case CODEC_ID_ADPCM_SBPRO_4:
          layout = "sbpro4";
          break;
        case CODEC_ID_ADPCM_EA_R1:
          layout = "ea-r1";
          break;
        case CODEC_ID_ADPCM_EA_R2:
          layout = "ea-r3";
          break;
        case CODEC_ID_ADPCM_EA_R3:
          layout = "ea-r3";
          break;
        case CODEC_ID_ADPCM_EA_MAXIS_XA:
          layout = "ea-maxis-xa";
          break;
        case CODEC_ID_ADPCM_EA_XAS:
          layout = "ea-xas";
          break;
        case CODEC_ID_ADPCM_THP:
          layout = "thp";
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      /* FIXME: someone please check whether we need additional properties
       * in this caps definition. */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-adpcm",
          "layout", G_TYPE_STRING, layout, NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
    }
      break;

    case CODEC_ID_AMR_NB:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/AMR", NULL);
      break;

    case CODEC_ID_AMR_WB:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/AMR-WB", NULL);
      break;

    case CODEC_ID_GSM:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-gsm", NULL);
      break;

    case CODEC_ID_GSM_MS:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/ms-gsm", NULL);
      break;

    case CODEC_ID_NELLYMOSER:
      caps =
          gst_ff_aud_caps_new (context, codec_id, "audio/x-nellymoser", NULL);
      break;

    case CODEC_ID_SIPR:
    {
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-sipro", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "leaf_size", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      }
    }
      break;

    case CODEC_ID_RA_144:
    case CODEC_ID_RA_288:
    case CODEC_ID_COOK:
    {
      gint version = 0;

      switch (codec_id) {
        case CODEC_ID_RA_144:
          version = 1;
          break;
        case CODEC_ID_RA_288:
          version = 2;
          break;
        case CODEC_ID_COOK:
          version = 8;
          break;
        default:
          break;
      }

      /* FIXME: properties? */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-pn-realaudio",
          "raversion", G_TYPE_INT, version, NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "leaf_size", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
      }
    }
      break;

    case CODEC_ID_ROQ_DPCM:
    case CODEC_ID_INTERPLAY_DPCM:
    case CODEC_ID_XAN_DPCM:
    case CODEC_ID_SOL_DPCM:
    {
      const gchar *layout = NULL;

      switch (codec_id) {
        case CODEC_ID_ROQ_DPCM:
          layout = "roq";
          break;
        case CODEC_ID_INTERPLAY_DPCM:
          layout = "interplay";
          break;
        case CODEC_ID_XAN_DPCM:
          layout = "xan";
          break;
        case CODEC_ID_SOL_DPCM:
          layout = "sol";
          break;
        default:
          g_assert (0);         /* don't worry, we never get here */
          break;
      }

      /* FIXME: someone please check whether we need additional properties
       * in this caps definition. */
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-dpcm",
          "layout", G_TYPE_STRING, layout, NULL);
      if (context)
        gst_caps_set_simple (caps,
            "block_align", G_TYPE_INT, context->block_align,
            "bitrate", G_TYPE_INT, context->bit_rate, NULL);
    }
      break;

    case CODEC_ID_SHORTEN:
      caps = gst_caps_new_empty_simple ("audio/x-shorten");
      break;

    case CODEC_ID_ALAC:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-alac", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "samplesize", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;

    case CODEC_ID_FLAC:
      /* Note that ffmpeg has no encoder yet, but just for safety. In the
       * encoder case, we want to add things like samplerate, channels... */
      if (!encode) {
        caps = gst_caps_new_empty_simple ("audio/x-flac");
      }
      break;

    case CODEC_ID_DVD_SUBTITLE:
    case CODEC_ID_DVB_SUBTITLE:
      caps = NULL;
      break;
    case CODEC_ID_BMP:
      caps = gst_caps_new_empty_simple ("image/bmp");
      break;
    case CODEC_ID_TTA:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-tta", NULL);
      if (context) {
        gst_caps_set_simple (caps,
            "samplesize", G_TYPE_INT, context->bits_per_coded_sample, NULL);
      }
      break;
    case CODEC_ID_TWINVQ:
      caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-twin-vq", NULL);
      break;
    default:
      GST_DEBUG ("Unknown codec ID %d, please add mapping here", codec_id);
      break;
  }

  if (buildcaps) {
    AVCodec *codec;

    if ((codec = avcodec_find_decoder (codec_id)) ||
        (codec = avcodec_find_encoder (codec_id))) {
      gchar *mime = NULL;

      GST_LOG ("Could not create stream format caps for %s", codec->name);

      switch (codec->type) {
        case AVMEDIA_TYPE_VIDEO:
          mime = g_strdup_printf ("video/x-gst_ff-%s", codec->name);
          caps = gst_ff_vid_caps_new (context, codec_id, mime, NULL);
          g_free (mime);
          break;
        case AVMEDIA_TYPE_AUDIO:
          mime = g_strdup_printf ("audio/x-gst_ff-%s", codec->name);
          caps = gst_ff_aud_caps_new (context, codec_id, mime, NULL);
          if (context)
            gst_caps_set_simple (caps,
                "block_align", G_TYPE_INT, context->block_align,
                "bitrate", G_TYPE_INT, context->bit_rate, NULL);
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

    /* palette */
    if (context) {
      gst_ffmpeg_set_palette (caps, context);
    }

    GST_LOG ("caps for codec_id=%d: %" GST_PTR_FORMAT, codec_id, caps);

  } else {
    GST_LOG ("No caps found for codec_id=%d", codec_id);
  }

  return caps;
}

/* Convert a FFMPEG Pixel Format to a GStreamer VideoFormat */
GstVideoFormat
gst_ffmpeg_pixfmt_to_video_format (enum PixelFormat pix_fmt)
{
  GstVideoFormat fmt;

  switch (pix_fmt) {
    case PIX_FMT_YUVJ420P:
    case PIX_FMT_YUV420P:
      fmt = GST_VIDEO_FORMAT_I420;
      break;
    case PIX_FMT_YUVA420P:
      fmt = GST_VIDEO_FORMAT_A420;
      break;
    case PIX_FMT_YUYV422:
      fmt = GST_VIDEO_FORMAT_YUY2;
      break;
    case PIX_FMT_RGB24:
      fmt = GST_VIDEO_FORMAT_RGB;
      break;
    case PIX_FMT_BGR24:
      fmt = GST_VIDEO_FORMAT_BGR;
      break;
    case PIX_FMT_YUVJ422P:
    case PIX_FMT_YUV422P:
      fmt = GST_VIDEO_FORMAT_Y42B;
      break;
    case PIX_FMT_YUVJ444P:
    case PIX_FMT_YUV444P:
      fmt = GST_VIDEO_FORMAT_Y444;
      break;
    case PIX_FMT_RGB32:
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      fmt = GST_VIDEO_FORMAT_xRGB;
#else
      fmt = GST_VIDEO_FORMAT_BGRx;
#endif
      break;
    case PIX_FMT_YUV410P:
      fmt = GST_VIDEO_FORMAT_YUV9;
      break;
    case PIX_FMT_YUV411P:
      fmt = GST_VIDEO_FORMAT_Y41B;
      break;
    case PIX_FMT_RGB565:
      fmt = GST_VIDEO_FORMAT_RGB16;
      break;
    case PIX_FMT_RGB555:
      fmt = GST_VIDEO_FORMAT_RGB15;
      break;
    case PIX_FMT_PAL8:
      fmt = GST_VIDEO_FORMAT_RGB8_PALETTED;
      break;
    case PIX_FMT_GRAY8:
      fmt = GST_VIDEO_FORMAT_GRAY8;
      break;
    default:
      /* give up ... */
      fmt = GST_VIDEO_FORMAT_UNKNOWN;
      break;
  }
  return fmt;
}

/* Convert a FFMPEG Pixel Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

GstCaps *
gst_ffmpeg_pixfmt_to_caps (enum PixelFormat pix_fmt, AVCodecContext * context,
    enum CodecID codec_id)
{
  GstCaps *caps = NULL;
  GstVideoFormat format;

  format = gst_ffmpeg_pixfmt_to_video_format (pix_fmt);

  if (format != GST_VIDEO_FORMAT_UNKNOWN) {
    caps = gst_ff_vid_caps_new (context, codec_id, "video/x-raw",
        "format", G_TYPE_STRING, gst_video_format_to_string (format), NULL);
  }

  if (caps != NULL) {
    GST_DEBUG ("caps for pix_fmt=%d: %" GST_PTR_FORMAT, pix_fmt, caps);
  } else {
    GST_LOG ("No caps found for pix_fmt=%d", pix_fmt);
  }

  return caps;
}

/* Convert a FFMPEG Sample Format and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * See below for usefullness
 */

static GstCaps *
gst_ffmpeg_smpfmt_to_caps (enum SampleFormat sample_fmt,
    AVCodecContext * context, enum CodecID codec_id)
{
  GstCaps *caps = NULL;
  GstAudioFormat format;

  switch (sample_fmt) {
    case SAMPLE_FMT_S16:
      format = GST_AUDIO_FORMAT_S16;
      break;
    case SAMPLE_FMT_S32:
      format = GST_AUDIO_FORMAT_S32;
      break;
    case SAMPLE_FMT_FLT:
      format = GST_AUDIO_FORMAT_F32;
      break;
    case SAMPLE_FMT_DBL:
      format = GST_AUDIO_FORMAT_F64;
      break;
    default:
      /* .. */
      format = GST_AUDIO_FORMAT_UNKNOWN;
      break;
  }

  if (format != GST_AUDIO_FORMAT_UNKNOWN) {
    caps = gst_ff_aud_caps_new (context, codec_id, "audio/x-raw",
        "format", G_TYPE_STRING, gst_audio_format_to_string (format),
        "layout", G_TYPE_STRING, "interleaved", NULL);
    GST_LOG ("caps for sample_fmt=%d: %" GST_PTR_FORMAT, sample_fmt, caps);
  } else {
    GST_LOG ("No caps found for sample_fmt=%d", sample_fmt);
  }

  return caps;
}

GstCaps *
gst_ffmpeg_codectype_to_audio_caps (AVCodecContext * context,
    enum CodecID codec_id, gboolean encode, AVCodec * codec)
{
  GstCaps *caps = NULL;

  GST_DEBUG ("context:%p, codec_id:%d, encode:%d, codec:%p",
      context, codec_id, encode, codec);
  if (codec)
    GST_DEBUG ("sample_fmts:%p, samplerates:%p",
        codec->sample_fmts, codec->supported_samplerates);

  if (context) {
    /* Specific codec context */
    caps = gst_ffmpeg_smpfmt_to_caps (context->sample_fmt, context, codec_id);
  } else if (codec && codec->sample_fmts) {
    GstCaps *temp;
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; codec->sample_fmts[i] != -1; i++) {
      temp =
          gst_ffmpeg_smpfmt_to_caps (codec->sample_fmts[i], context, codec_id);
      if (temp != NULL)
        gst_caps_append (caps, temp);
    }
  } else {
    GstCaps *temp;
    enum SampleFormat i;
    AVCodecContext ctx = { 0, };

    ctx.channels = -1;
    caps = gst_caps_new_empty ();
    for (i = 0; i <= SAMPLE_FMT_DBL; i++) {
      temp = gst_ffmpeg_smpfmt_to_caps (i, encode ? &ctx : NULL, codec_id);
      if (temp != NULL) {
        gst_caps_append (caps, temp);
      }
    }
  }
  return caps;
}

GstCaps *
gst_ffmpeg_codectype_to_video_caps (AVCodecContext * context,
    enum CodecID codec_id, gboolean encode, AVCodec * codec)
{
  GstCaps *caps;

  GST_LOG ("context:%p, codec_id:%d, encode:%d, codec:%p",
      context, codec_id, encode, codec);

  if (context) {
    caps = gst_ffmpeg_pixfmt_to_caps (context->pix_fmt, context, codec_id);
  } else {
    GstCaps *temp;
    enum PixelFormat i;
    AVCodecContext ctx = { 0, };

    caps = gst_caps_new_empty ();
    for (i = 0; i < PIX_FMT_NB; i++) {
      ctx.width = -1;
      ctx.pix_fmt = i;
      temp = gst_ffmpeg_pixfmt_to_caps (i, encode ? &ctx : NULL, codec_id);
      if (temp != NULL) {
        gst_caps_append (caps, temp);
      }
    }
  }
  return caps;
}

/* Convert a FFMPEG codec Type and optional AVCodecContext
 * to a GstCaps. If the context is ommitted, no fixed values
 * for video/audio size will be included in the GstCaps
 *
 * AVMediaType is primarily meant for uncompressed data GstCaps!
 */

GstCaps *
gst_ffmpeg_codectype_to_caps (enum AVMediaType codec_type,
    AVCodecContext * context, enum CodecID codec_id, gboolean encode)
{
  GstCaps *caps;

  switch (codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      caps =
          gst_ffmpeg_codectype_to_video_caps (context, codec_id, encode, NULL);
      break;
    case AVMEDIA_TYPE_AUDIO:
      caps =
          gst_ffmpeg_codectype_to_audio_caps (context, codec_id, encode, NULL);
      break;
    default:
      caps = NULL;
      break;
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

  g_return_if_fail (gst_caps_get_size (caps) == 1);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &context->channels);
  gst_structure_get_int (structure, "rate", &context->sample_rate);
  gst_structure_get_int (structure, "block_align", &context->block_align);
  gst_structure_get_int (structure, "bitrate", &context->bit_rate);

  if (!raw)
    return;

  if (gst_structure_has_name (structure, "audio/x-raw")) {
    if ((fmt = gst_structure_get_string (structure, "format"))) {
      format = gst_audio_format_from_string (fmt);
    }
  }

  switch (format) {
    case GST_AUDIO_FORMAT_F32:
      context->sample_fmt = SAMPLE_FMT_FLT;
      break;
    case GST_AUDIO_FORMAT_F64:
      context->sample_fmt = SAMPLE_FMT_DBL;
      break;
    case GST_AUDIO_FORMAT_S32:
      context->sample_fmt = SAMPLE_FMT_S32;
      break;
    case GST_AUDIO_FORMAT_S16:
      context->sample_fmt = SAMPLE_FMT_S16;
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

  GST_DEBUG ("converting caps %" GST_PTR_FORMAT, caps);
  g_return_if_fail (gst_caps_get_size (caps) == 1);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &context->width);
  gst_structure_get_int (structure, "height", &context->height);
  gst_structure_get_int (structure, "bpp", &context->bits_per_coded_sample);

  fps = gst_structure_get_value (structure, "framerate");
  if (fps != NULL && GST_VALUE_HOLDS_FRACTION (fps)) {

    /* somehow these seem mixed up.. */
    context->time_base.den = gst_value_get_fraction_numerator (fps);
    context->time_base.num = gst_value_get_fraction_denominator (fps);
    context->ticks_per_frame = 1;

    GST_DEBUG ("setting framerate %d/%d = %lf",
        context->time_base.den, context->time_base.num,
        1. * context->time_base.den / context->time_base.num);
  }

  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par && GST_VALUE_HOLDS_FRACTION (par)) {

    context->sample_aspect_ratio.num = gst_value_get_fraction_numerator (par);
    context->sample_aspect_ratio.den = gst_value_get_fraction_denominator (par);

    GST_DEBUG ("setting pixel-aspect-ratio %d/%d = %lf",
        context->sample_aspect_ratio.den, context->sample_aspect_ratio.num,
        1. * context->sample_aspect_ratio.den /
        context->sample_aspect_ratio.num);
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
      context->pix_fmt = PIX_FMT_YUYV422;
      break;
    case GST_VIDEO_FORMAT_I420:
      context->pix_fmt = PIX_FMT_YUV420P;
      break;
    case GST_VIDEO_FORMAT_A420:
      context->pix_fmt = PIX_FMT_YUVA420P;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      context->pix_fmt = PIX_FMT_YUV411P;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      context->pix_fmt = PIX_FMT_YUV422P;
      break;
    case GST_VIDEO_FORMAT_YUV9:
      context->pix_fmt = PIX_FMT_YUV410P;
      break;
    case GST_VIDEO_FORMAT_Y444:
      context->pix_fmt = PIX_FMT_YUV444P;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      context->pix_fmt = PIX_FMT_GRAY8;
      break;
    case GST_VIDEO_FORMAT_xRGB:
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      context->pix_fmt = PIX_FMT_RGB32;
#endif
      break;
    case GST_VIDEO_FORMAT_BGRx:
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
      context->pix_fmt = PIX_FMT_RGB32;
#endif
      break;
    case GST_VIDEO_FORMAT_RGB:
      context->pix_fmt = PIX_FMT_RGB24;
      break;
    case GST_VIDEO_FORMAT_BGR:
      context->pix_fmt = PIX_FMT_BGR24;
      break;
    case GST_VIDEO_FORMAT_RGB16:
      context->pix_fmt = PIX_FMT_RGB565;
      break;
    case GST_VIDEO_FORMAT_RGB15:
      context->pix_fmt = PIX_FMT_RGB555;
      break;
    case GST_VIDEO_FORMAT_RGB8_PALETTED:
      context->pix_fmt = PIX_FMT_PAL8;
      gst_ffmpeg_get_palette (caps, context);
      break;
    default:
      break;
  }
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
gst_ffmpeg_caps_with_codecid (enum CodecID codec_id,
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
    if (codec_id == CODEC_ID_H264) {
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
              FF_INPUT_BUFFER_PADDING_SIZE));
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
              FF_INPUT_BUFFER_PADDING_SIZE));
      memcpy (context->extradata, map.data, map.size);
      context->extradata_size = map.size;
    }

    /* Hack for VC1. Sometimes the first (length) byte is 0 for some files */
    if (codec_id == CODEC_ID_VC1 && map.size > 0 && map.data[0] == 0) {
      context->extradata[0] = (guint8) map.size;
    }

    GST_DEBUG ("have codec data of size %" G_GSIZE_FORMAT, map.size);

    gst_buffer_unmap (buf, &map);
  } else if (context->extradata == NULL && codec_id != CODEC_ID_AAC_LATM &&
      codec_id != CODEC_ID_FLAC) {
    /* no extradata, alloc dummy with 0 sized, some codecs insist on reading
     * extradata anyway which makes then segfault. */
    context->extradata =
        av_mallocz (GST_ROUND_UP_16 (FF_INPUT_BUFFER_PADDING_SIZE));
    context->extradata_size = 0;
    GST_DEBUG ("no codec data");
  }

  switch (codec_id) {
    case CODEC_ID_MPEG4:
    {
      const gchar *mime = gst_structure_get_name (str);

      if (!strcmp (mime, "video/x-divx"))
        context->codec_tag = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
      else if (!strcmp (mime, "video/x-xvid"))
        context->codec_tag = GST_MAKE_FOURCC ('X', 'V', 'I', 'D');
      else if (!strcmp (mime, "video/x-3ivx"))
        context->codec_tag = GST_MAKE_FOURCC ('3', 'I', 'V', '1');
      else if (!strcmp (mime, "video/mpeg"))
        context->codec_tag = GST_MAKE_FOURCC ('m', 'p', '4', 'v');
    }
      break;

    case CODEC_ID_SVQ3:
      /* FIXME: this is a workaround for older gst-plugins releases
       * (<= 0.8.9). This should be removed at some point, because
       * it causes wrong decoded frame order. */
      if (!context->extradata) {
        gint halfpel_flag, thirdpel_flag, low_delay, unknown_svq3_flag;
        guint16 flags;

        if (gst_structure_get_int (str, "halfpel_flag", &halfpel_flag) ||
            gst_structure_get_int (str, "thirdpel_flag", &thirdpel_flag) ||
            gst_structure_get_int (str, "low_delay", &low_delay) ||
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

    case CODEC_ID_MSRLE:
    case CODEC_ID_QTRLE:
    case CODEC_ID_TSCC:
    case CODEC_ID_CSCD:
    case CODEC_ID_APE:
    {
      gint depth;

      if (gst_structure_get_int (str, "depth", &depth)) {
        context->bits_per_coded_sample = depth;
      } else {
        GST_WARNING ("No depth field in caps %" GST_PTR_FORMAT, caps);
      }

    }
      break;

    case CODEC_ID_RV10:
    case CODEC_ID_RV20:
    case CODEC_ID_RV30:
    case CODEC_ID_RV40:
    {
      gint format;

      if (gst_structure_get_int (str, "format", &format))
        context->sub_id = format;

      break;
    }
    case CODEC_ID_COOK:
    case CODEC_ID_RA_288:
    case CODEC_ID_RA_144:
    case CODEC_ID_SIPR:
    {
      gint leaf_size;
      gint bitrate;

      if (gst_structure_get_int (str, "leaf_size", &leaf_size))
        context->block_align = leaf_size;
      if (gst_structure_get_int (str, "bitrate", &bitrate))
        context->bit_rate = bitrate;
    }
    case CODEC_ID_ALAC:
      gst_structure_get_int (str, "samplesize",
          &context->bits_per_coded_sample);
      break;

    case CODEC_ID_DVVIDEO:
    {
      const gchar *format;

      if ((format = gst_structure_get_string (str, "format"))) {

        if (g_str_equal (format, "YUY2"))
          context->pix_fmt = PIX_FMT_YUYV422;
        else if (g_str_equal (format, "I420"))
          context->pix_fmt = PIX_FMT_YUV420P;
        else if (g_str_equal (format, "A420"))
          context->pix_fmt = PIX_FMT_YUVA420P;
        else if (g_str_equal (format, "Y41B"))
          context->pix_fmt = PIX_FMT_YUV411P;
        else if (g_str_equal (format, "Y42B"))
          context->pix_fmt = PIX_FMT_YUV422P;
        else if (g_str_equal (format, "YUV9"))
          context->pix_fmt = PIX_FMT_YUV410P;
        else {
          GST_WARNING ("couldn't convert format %s" " to a pixel format",
              format);
        }
      } else
        GST_WARNING ("No specified format");
      break;
    }
    case CODEC_ID_H263P:
    {
      gboolean val;

      if (!gst_structure_get_boolean (str, "annex-f", &val) || val)
        context->flags |= CODEC_FLAG_4MV;
      else
        context->flags &= ~CODEC_FLAG_4MV;
      if ((!gst_structure_get_boolean (str, "annex-i", &val) || val) &&
          (!gst_structure_get_boolean (str, "annex-t", &val) || val))
        context->flags |= CODEC_FLAG_AC_PRED;
      else
        context->flags &= ~CODEC_FLAG_AC_PRED;
      if (!gst_structure_get_boolean (str, "annex-j", &val) || val)
        context->flags |= CODEC_FLAG_LOOP_FILTER;
      else
        context->flags &= ~CODEC_FLAG_LOOP_FILTER;
      break;
    }
    case CODEC_ID_ADPCM_G726:
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
    default:
      break;
  }

  if (!gst_caps_is_fixed (caps))
    return;

  /* common properties (width, height, fps) */
  switch (codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      gst_ffmpeg_caps_to_pixfmt (caps, context, codec_id == CODEC_ID_RAWVIDEO);
      gst_ffmpeg_get_palette (caps, context);
      break;
    case AVMEDIA_TYPE_AUDIO:
      gst_ffmpeg_caps_to_smpfmt (caps, context, FALSE);
      break;
    default:
      break;
  }

  /* fixup of default settings */
  switch (codec_id) {
    case CODEC_ID_QCELP:
      /* QCELP is always mono, no matter what the caps say */
      context->channels = 1;
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
  } else {
    gchar *name;

    GST_LOG ("Could not create stream format caps for %s", format_name);
    name = g_strdup_printf ("application/x-gst_ff-%s", format_name);
    caps = gst_caps_new_empty_simple (name);
    g_free (name);
  }

  return caps;
}

gboolean
gst_ffmpeg_formatid_get_codecids (const gchar * format_name,
    enum CodecID ** video_codec_list, enum CodecID ** audio_codec_list,
    AVOutputFormat * plugin)
{
  static enum CodecID tmp_vlist[] = {
    CODEC_ID_NONE,
    CODEC_ID_NONE
  };
  static enum CodecID tmp_alist[] = {
    CODEC_ID_NONE,
    CODEC_ID_NONE
  };

  GST_LOG ("format_name : %s", format_name);

  if (!strcmp (format_name, "mp4")) {
    static enum CodecID mp4_video_list[] = {
      CODEC_ID_MPEG4, CODEC_ID_H264,
      CODEC_ID_MJPEG,
      CODEC_ID_NONE
    };
    static enum CodecID mp4_audio_list[] = {
      CODEC_ID_AAC, CODEC_ID_MP3,
      CODEC_ID_NONE
    };

    *video_codec_list = mp4_video_list;
    *audio_codec_list = mp4_audio_list;
  } else if (!strcmp (format_name, "mpeg")) {
    static enum CodecID mpeg_video_list[] = { CODEC_ID_MPEG1VIDEO,
      CODEC_ID_MPEG2VIDEO,
      CODEC_ID_H264,
      CODEC_ID_NONE
    };
    static enum CodecID mpeg_audio_list[] = { CODEC_ID_MP1,
      CODEC_ID_MP2,
      CODEC_ID_MP3,
      CODEC_ID_NONE
    };

    *video_codec_list = mpeg_video_list;
    *audio_codec_list = mpeg_audio_list;
  } else if (!strcmp (format_name, "dvd")) {
    static enum CodecID mpeg_video_list[] = { CODEC_ID_MPEG2VIDEO,
      CODEC_ID_NONE
    };
    static enum CodecID mpeg_audio_list[] = { CODEC_ID_MP2,
      CODEC_ID_AC3,
      CODEC_ID_DTS,
      CODEC_ID_PCM_S16BE,
      CODEC_ID_NONE
    };

    *video_codec_list = mpeg_video_list;
    *audio_codec_list = mpeg_audio_list;
  } else if (!strcmp (format_name, "mpegts")) {
    static enum CodecID mpegts_video_list[] = { CODEC_ID_MPEG1VIDEO,
      CODEC_ID_MPEG2VIDEO,
      CODEC_ID_H264,
      CODEC_ID_NONE
    };
    static enum CodecID mpegts_audio_list[] = { CODEC_ID_MP2,
      CODEC_ID_MP3,
      CODEC_ID_AC3,
      CODEC_ID_DTS,
      CODEC_ID_AAC,
      CODEC_ID_NONE
    };

    *video_codec_list = mpegts_video_list;
    *audio_codec_list = mpegts_audio_list;
  } else if (!strcmp (format_name, "vob")) {
    static enum CodecID vob_video_list[] =
        { CODEC_ID_MPEG2VIDEO, CODEC_ID_NONE };
    static enum CodecID vob_audio_list[] = { CODEC_ID_MP2, CODEC_ID_AC3,
      CODEC_ID_DTS, CODEC_ID_NONE
    };

    *video_codec_list = vob_video_list;
    *audio_codec_list = vob_audio_list;
  } else if (!strcmp (format_name, "flv")) {
    static enum CodecID flv_video_list[] = { CODEC_ID_FLV1, CODEC_ID_NONE };
    static enum CodecID flv_audio_list[] = { CODEC_ID_MP3, CODEC_ID_NONE };

    *video_codec_list = flv_video_list;
    *audio_codec_list = flv_audio_list;
  } else if (!strcmp (format_name, "asf")) {
    static enum CodecID asf_video_list[] =
        { CODEC_ID_WMV1, CODEC_ID_WMV2, CODEC_ID_MSMPEG4V3, CODEC_ID_NONE };
    static enum CodecID asf_audio_list[] =
        { CODEC_ID_WMAV1, CODEC_ID_WMAV2, CODEC_ID_MP3, CODEC_ID_NONE };

    *video_codec_list = asf_video_list;
    *audio_codec_list = asf_audio_list;
  } else if (!strcmp (format_name, "dv")) {
    static enum CodecID dv_video_list[] = { CODEC_ID_DVVIDEO, CODEC_ID_NONE };
    static enum CodecID dv_audio_list[] = { CODEC_ID_PCM_S16LE, CODEC_ID_NONE };

    *video_codec_list = dv_video_list;
    *audio_codec_list = dv_audio_list;
  } else if (!strcmp (format_name, "mov")) {
    static enum CodecID mov_video_list[] = {
      CODEC_ID_SVQ1, CODEC_ID_SVQ3, CODEC_ID_MPEG4,
      CODEC_ID_H263, CODEC_ID_H263P,
      CODEC_ID_H264, CODEC_ID_DVVIDEO,
      CODEC_ID_MJPEG,
      CODEC_ID_NONE
    };
    static enum CodecID mov_audio_list[] = {
      CODEC_ID_PCM_MULAW, CODEC_ID_PCM_ALAW, CODEC_ID_ADPCM_IMA_QT,
      CODEC_ID_MACE3, CODEC_ID_MACE6, CODEC_ID_AAC,
      CODEC_ID_AMR_NB, CODEC_ID_AMR_WB,
      CODEC_ID_PCM_S16BE, CODEC_ID_PCM_S16LE,
      CODEC_ID_MP3, CODEC_ID_NONE
    };

    *video_codec_list = mov_video_list;
    *audio_codec_list = mov_audio_list;
  } else if ((!strcmp (format_name, "3gp") || !strcmp (format_name, "3g2"))) {
    static enum CodecID tgp_video_list[] = {
      CODEC_ID_MPEG4, CODEC_ID_H263, CODEC_ID_H263P, CODEC_ID_H264,
      CODEC_ID_NONE
    };
    static enum CodecID tgp_audio_list[] = {
      CODEC_ID_AMR_NB, CODEC_ID_AMR_WB,
      CODEC_ID_AAC,
      CODEC_ID_NONE
    };

    *video_codec_list = tgp_video_list;
    *audio_codec_list = tgp_audio_list;
  } else if (!strcmp (format_name, "mmf")) {
    static enum CodecID mmf_audio_list[] = {
      CODEC_ID_ADPCM_YAMAHA, CODEC_ID_NONE
    };
    *video_codec_list = NULL;
    *audio_codec_list = mmf_audio_list;
  } else if (!strcmp (format_name, "amr")) {
    static enum CodecID amr_audio_list[] = {
      CODEC_ID_AMR_NB, CODEC_ID_AMR_WB,
      CODEC_ID_NONE
    };
    *video_codec_list = NULL;
    *audio_codec_list = amr_audio_list;
  } else if (!strcmp (format_name, "gif")) {
    static enum CodecID gif_image_list[] = {
      CODEC_ID_RAWVIDEO, CODEC_ID_NONE
    };
    *video_codec_list = gif_image_list;
    *audio_codec_list = NULL;
  } else if ((plugin->audio_codec != CODEC_ID_NONE) ||
      (plugin->video_codec != CODEC_ID_NONE)) {
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

enum CodecID
gst_ffmpeg_caps_to_codecid (const GstCaps * caps, AVCodecContext * context)
{
  enum CodecID id = CODEC_ID_NONE;
  const gchar *mimetype;
  const GstStructure *structure;
  gboolean video = FALSE, audio = FALSE;        /* we want to be sure! */

  g_return_val_if_fail (caps != NULL, CODEC_ID_NONE);
  g_return_val_if_fail (gst_caps_get_size (caps) == 1, CODEC_ID_NONE);
  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);

  if (!strcmp (mimetype, "video/x-raw")) {
    id = CODEC_ID_RAWVIDEO;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-raw")) {
    GstAudioInfo info;

    if (gst_audio_info_from_caps (&info, caps)) {
      switch (GST_AUDIO_INFO_FORMAT (&info)) {
        case GST_AUDIO_FORMAT_S8:
          id = CODEC_ID_PCM_S8;
          break;
        case GST_AUDIO_FORMAT_U8:
          id = CODEC_ID_PCM_U8;
          break;
        case GST_AUDIO_FORMAT_S16LE:
          id = CODEC_ID_PCM_S16LE;
          break;
        case GST_AUDIO_FORMAT_S16BE:
          id = CODEC_ID_PCM_S16BE;
          break;
        case GST_AUDIO_FORMAT_U16LE:
          id = CODEC_ID_PCM_U16LE;
          break;
        case GST_AUDIO_FORMAT_U16BE:
          id = CODEC_ID_PCM_U16BE;
          break;
        default:
          break;
      }
      if (id != CODEC_ID_NONE)
        audio = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-mulaw")) {
    id = CODEC_ID_PCM_MULAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-alaw")) {
    id = CODEC_ID_PCM_ALAW;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-dv")) {
    gboolean sys_strm;

    if (gst_structure_get_boolean (structure, "systemstream", &sys_strm) &&
        !sys_strm) {
      id = CODEC_ID_DVVIDEO;
      video = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-dv")) {        /* ??? */
    id = CODEC_ID_DVAUDIO;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-h263")) {
    const gchar *h263version =
        gst_structure_get_string (structure, "h263version");
    if (h263version && !strcmp (h263version, "h263p"))
      id = CODEC_ID_H263P;
    else
      id = CODEC_ID_H263;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-intel-h263")) {
    id = CODEC_ID_H263I;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-h261")) {
    id = CODEC_ID_H261;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/mpeg")) {
    gboolean sys_strm;
    gint mpegversion;

    if (gst_structure_get_boolean (structure, "systemstream", &sys_strm) &&
        gst_structure_get_int (structure, "mpegversion", &mpegversion) &&
        !sys_strm) {
      switch (mpegversion) {
        case 1:
          id = CODEC_ID_MPEG1VIDEO;
          break;
        case 2:
          id = CODEC_ID_MPEG2VIDEO;
          break;
        case 4:
          id = CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "image/jpeg")) {
    id = CODEC_ID_MJPEG;        /* A... B... */
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-jpeg-b")) {
    id = CODEC_ID_MJPEGB;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-wmv")) {
    gint wmvversion = 0;

    if (gst_structure_get_int (structure, "wmvversion", &wmvversion)) {
      switch (wmvversion) {
        case 1:
          id = CODEC_ID_WMV1;
          break;
        case 2:
          id = CODEC_ID_WMV2;
          break;
        case 3:
        {
          const gchar *format;

          /* WMV3 unless the fourcc exists and says otherwise */
          id = CODEC_ID_WMV3;

          if ((format = gst_structure_get_string (structure, "format")) &&
              (g_str_equal (format, "WVC1") || g_str_equal (format, "WMVA")))
            id = CODEC_ID_VC1;

          break;
        }
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-vorbis")) {
    id = CODEC_ID_VORBIS;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-qdm2")) {
    id = CODEC_ID_QDM2;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/mpeg")) {
    gint layer = 0;
    gint mpegversion = 0;

    if (gst_structure_get_int (structure, "mpegversion", &mpegversion)) {
      switch (mpegversion) {
        case 2:                /* ffmpeg uses faad for both... */
        case 4:
          id = CODEC_ID_AAC;
          break;
        case 1:
          if (gst_structure_get_int (structure, "layer", &layer)) {
            switch (layer) {
              case 1:
                id = CODEC_ID_MP1;
                break;
              case 2:
                id = CODEC_ID_MP2;
                break;
              case 3:
                id = CODEC_ID_MP3;
                break;
            }
          }
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-musepack")) {
    gint streamversion = -1;

    if (gst_structure_get_int (structure, "streamversion", &streamversion)) {
      if (streamversion == 7)
        id = CODEC_ID_MUSEPACK7;
    } else {
      id = CODEC_ID_MUSEPACK7;
    }
  } else if (!strcmp (mimetype, "audio/x-wma")) {
    gint wmaversion = 0;

    if (gst_structure_get_int (structure, "wmaversion", &wmaversion)) {
      switch (wmaversion) {
        case 1:
          id = CODEC_ID_WMAV1;
          break;
        case 2:
          id = CODEC_ID_WMAV2;
          break;
        case 3:
          id = CODEC_ID_WMAPRO;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-wms")) {
    id = CODEC_ID_WMAVOICE;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-ac3")) {
    id = CODEC_ID_AC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-eac3")) {
    id = CODEC_ID_EAC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-vnd.sony.atrac3") ||
      !strcmp (mimetype, "audio/atrac3")) {
    id = CODEC_ID_ATRAC3;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dts")) {
    id = CODEC_ID_DTS;
    audio = TRUE;
  } else if (!strcmp (mimetype, "application/x-ape")) {
    id = CODEC_ID_APE;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-msmpeg")) {
    gint msmpegversion = 0;

    if (gst_structure_get_int (structure, "msmpegversion", &msmpegversion)) {
      switch (msmpegversion) {
        case 41:
          id = CODEC_ID_MSMPEG4V1;
          break;
        case 42:
          id = CODEC_ID_MSMPEG4V2;
          break;
        case 43:
          id = CODEC_ID_MSMPEG4V3;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-svq")) {
    gint svqversion = 0;

    if (gst_structure_get_int (structure, "svqversion", &svqversion)) {
      switch (svqversion) {
        case 1:
          id = CODEC_ID_SVQ1;
          break;
        case 3:
          id = CODEC_ID_SVQ3;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-huffyuv")) {
    id = CODEC_ID_HUFFYUV;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-mace")) {
    gint maceversion = 0;

    if (gst_structure_get_int (structure, "maceversion", &maceversion)) {
      switch (maceversion) {
        case 3:
          id = CODEC_ID_MACE3;
          break;
        case 6:
          id = CODEC_ID_MACE6;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-theora")) {
    id = CODEC_ID_THEORA;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp3")) {
    id = CODEC_ID_VP3;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp5")) {
    id = CODEC_ID_VP5;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6")) {
    id = CODEC_ID_VP6;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6-flash")) {
    id = CODEC_ID_VP6F;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp6-alpha")) {
    id = CODEC_ID_VP6A;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-vp8")) {
    id = CODEC_ID_VP8;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-flash-screen")) {
    id = CODEC_ID_FLASHSV;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-indeo")) {
    gint indeoversion = 0;

    if (gst_structure_get_int (structure, "indeoversion", &indeoversion)) {
      switch (indeoversion) {
        case 5:
          id = CODEC_ID_INDEO5;
          break;
        case 4:
          id = CODEC_ID_INDEO4;
          break;
        case 3:
          id = CODEC_ID_INDEO3;
          break;
        case 2:
          id = CODEC_ID_INDEO2;
          break;
      }
      if (id != CODEC_ID_NONE)
        video = TRUE;
    }
  } else if (!strcmp (mimetype, "video/x-divx")) {
    gint divxversion = 0;

    if (gst_structure_get_int (structure, "divxversion", &divxversion)) {
      switch (divxversion) {
        case 3:
          id = CODEC_ID_MSMPEG4V3;
          break;
        case 4:
        case 5:
          id = CODEC_ID_MPEG4;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "video/x-3ivx")) {
    id = CODEC_ID_MPEG4;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-xvid")) {
    id = CODEC_ID_MPEG4;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-ffv")) {
    gint ffvversion = 0;

    if (gst_structure_get_int (structure, "ffvversion", &ffvversion) &&
        ffvversion == 1) {
      id = CODEC_ID_FFV1;
      video = TRUE;
    }
  } else if (!strcmp (mimetype, "audio/x-adpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (layout == NULL) {
      /* break */
    } else if (!strcmp (layout, "quicktime")) {
      id = CODEC_ID_ADPCM_IMA_QT;
    } else if (!strcmp (layout, "microsoft")) {
      id = CODEC_ID_ADPCM_MS;
    } else if (!strcmp (layout, "dvi")) {
      id = CODEC_ID_ADPCM_IMA_WAV;
    } else if (!strcmp (layout, "4xm")) {
      id = CODEC_ID_ADPCM_4XM;
    } else if (!strcmp (layout, "smjpeg")) {
      id = CODEC_ID_ADPCM_IMA_SMJPEG;
    } else if (!strcmp (layout, "dk3")) {
      id = CODEC_ID_ADPCM_IMA_DK3;
    } else if (!strcmp (layout, "dk4")) {
      id = CODEC_ID_ADPCM_IMA_DK4;
    } else if (!strcmp (layout, "westwood")) {
      id = CODEC_ID_ADPCM_IMA_WS;
    } else if (!strcmp (layout, "iss")) {
      id = CODEC_ID_ADPCM_IMA_ISS;
    } else if (!strcmp (layout, "xa")) {
      id = CODEC_ID_ADPCM_XA;
    } else if (!strcmp (layout, "adx")) {
      id = CODEC_ID_ADPCM_ADX;
    } else if (!strcmp (layout, "ea")) {
      id = CODEC_ID_ADPCM_EA;
    } else if (!strcmp (layout, "g726")) {
      id = CODEC_ID_ADPCM_G726;
    } else if (!strcmp (layout, "g721")) {
      id = CODEC_ID_ADPCM_G726;
    } else if (!strcmp (layout, "ct")) {
      id = CODEC_ID_ADPCM_CT;
    } else if (!strcmp (layout, "swf")) {
      id = CODEC_ID_ADPCM_SWF;
    } else if (!strcmp (layout, "yamaha")) {
      id = CODEC_ID_ADPCM_YAMAHA;
    } else if (!strcmp (layout, "sbpro2")) {
      id = CODEC_ID_ADPCM_SBPRO_2;
    } else if (!strcmp (layout, "sbpro3")) {
      id = CODEC_ID_ADPCM_SBPRO_3;
    } else if (!strcmp (layout, "sbpro4")) {
      id = CODEC_ID_ADPCM_SBPRO_4;
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-4xm")) {
    id = CODEC_ID_4XM;
    video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-dpcm")) {
    const gchar *layout;

    layout = gst_structure_get_string (structure, "layout");
    if (!layout) {
      /* .. */
    } else if (!strcmp (layout, "roq")) {
      id = CODEC_ID_ROQ_DPCM;
    } else if (!strcmp (layout, "interplay")) {
      id = CODEC_ID_INTERPLAY_DPCM;
    } else if (!strcmp (layout, "xan")) {
      id = CODEC_ID_XAN_DPCM;
    } else if (!strcmp (layout, "sol")) {
      id = CODEC_ID_SOL_DPCM;
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-flac")) {
    id = CODEC_ID_FLAC;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-shorten")) {
    id = CODEC_ID_SHORTEN;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-alac")) {
    id = CODEC_ID_ALAC;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-cinepak")) {
    id = CODEC_ID_CINEPAK;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-pn-realvideo")) {
    gint rmversion;

    if (gst_structure_get_int (structure, "rmversion", &rmversion)) {
      switch (rmversion) {
        case 1:
          id = CODEC_ID_RV10;
          break;
        case 2:
          id = CODEC_ID_RV20;
          break;
        case 3:
          id = CODEC_ID_RV30;
          break;
        case 4:
          id = CODEC_ID_RV40;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      video = TRUE;
  } else if (!strcmp (mimetype, "audio/x-sipro")) {
    id = CODEC_ID_SIPR;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/x-pn-realaudio")) {
    gint raversion;

    if (gst_structure_get_int (structure, "raversion", &raversion)) {
      switch (raversion) {
        case 1:
          id = CODEC_ID_RA_144;
          break;
        case 2:
          id = CODEC_ID_RA_288;
          break;
        case 8:
          id = CODEC_ID_COOK;
          break;
      }
    }
    if (id != CODEC_ID_NONE)
      audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-rle")) {
    const gchar *layout;

    if ((layout = gst_structure_get_string (structure, "layout"))) {
      if (!strcmp (layout, "microsoft")) {
        id = CODEC_ID_MSRLE;
        video = TRUE;
      }
    }
  } else if (!strcmp (mimetype, "video/x-xan")) {
    gint wcversion = 0;

    if ((gst_structure_get_int (structure, "wcversion", &wcversion))) {
      switch (wcversion) {
        case 3:
          id = CODEC_ID_XAN_WC3;
          video = TRUE;
          break;
        case 4:
          id = CODEC_ID_XAN_WC4;
          video = TRUE;
          break;
        default:
          break;
      }
    }
  } else if (!strcmp (mimetype, "audio/AMR")) {
    audio = TRUE;
    id = CODEC_ID_AMR_NB;
  } else if (!strcmp (mimetype, "audio/AMR-WB")) {
    id = CODEC_ID_AMR_WB;
    audio = TRUE;
  } else if (!strcmp (mimetype, "audio/qcelp")) {
    id = CODEC_ID_QCELP;
    audio = TRUE;
  } else if (!strcmp (mimetype, "video/x-h264")) {
    id = CODEC_ID_H264;
    video = TRUE;
  } else if (!strcmp (mimetype, "video/x-flash-video")) {
    gint flvversion = 0;

    if ((gst_structure_get_int (structure, "flvversion", &flvversion))) {
      switch (flvversion) {
        case 1:
          id = CODEC_ID_FLV1;
          video = TRUE;
          break;
        default:
          break;
      }
    }

  } else if (!strcmp (mimetype, "audio/x-nellymoser")) {
    id = CODEC_ID_NELLYMOSER;
    audio = TRUE;
  } else if (!strncmp (mimetype, "audio/x-gst_ff-", 15)) {
    gchar ext[16];
    AVCodec *codec;

    if (strlen (mimetype) <= 30 &&
        sscanf (mimetype, "audio/x-gst_ff-%s", ext) == 1) {
      if ((codec = avcodec_find_decoder_by_name (ext)) ||
          (codec = avcodec_find_encoder_by_name (ext))) {
        id = codec->id;
        audio = TRUE;
      }
    }
  } else if (!strncmp (mimetype, "video/x-gst_ff-", 15)) {
    gchar ext[16];
    AVCodec *codec;

    if (strlen (mimetype) <= 30 &&
        sscanf (mimetype, "video/x-gst_ff-%s", ext) == 1) {
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

  if (id != CODEC_ID_NONE) {
    GST_DEBUG ("The id=%d belongs to the caps %" GST_PTR_FORMAT, id, caps);
  } else {
    GST_WARNING ("Couldn't figure out the id for caps %" GST_PTR_FORMAT, caps);
  }

  return id;
}
