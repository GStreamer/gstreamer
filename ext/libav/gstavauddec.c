/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2012> Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <assert.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>

#include <gst/gst.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavauddec.h"

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegauddec_base_init (GstFFMpegAudDecClass * klass);
static void gst_ffmpegauddec_class_init (GstFFMpegAudDecClass * klass);
static void gst_ffmpegauddec_init (GstFFMpegAudDec * ffmpegdec);
static void gst_ffmpegauddec_finalize (GObject * object);
static gboolean gst_ffmpegauddec_propose_allocation (GstAudioDecoder * decoder,
    GstQuery * query);

static gboolean gst_ffmpegauddec_start (GstAudioDecoder * decoder);
static gboolean gst_ffmpegauddec_stop (GstAudioDecoder * decoder);
static void gst_ffmpegauddec_flush (GstAudioDecoder * decoder, gboolean hard);
static gboolean gst_ffmpegauddec_set_format (GstAudioDecoder * decoder,
    GstCaps * caps);
static GstFlowReturn gst_ffmpegauddec_handle_frame (GstAudioDecoder * decoder,
    GstBuffer * inbuf);

static gboolean gst_ffmpegauddec_negotiate (GstFFMpegAudDec * ffmpegdec,
    AVCodecContext * context, AVFrame * frame, gboolean force);

static void gst_ffmpegauddec_drain (GstFFMpegAudDec * ffmpegdec);

#define GST_FFDEC_PARAMS_QDATA g_quark_from_static_string("avdec-params")

static GstElementClass *parent_class = NULL;

static void
gst_ffmpegauddec_base_init (GstFFMpegAudDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps, *srccaps;
  AVCodec *in_plugin;
  gchar *longname, *description;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFDEC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("libav %s decoder", in_plugin->long_name);
  description = g_strdup_printf ("libav %s decoder", in_plugin->name);
  gst_element_class_set_metadata (element_class, longname,
      "Codec/Decoder/Audio", description,
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
      "Edward Hervey <bilboed@bilboed.com>");
  g_free (longname);
  g_free (description);

  /* get the caps */
  sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, FALSE);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for decoder '%s'", in_plugin->name);
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }
  srccaps = gst_ffmpeg_codectype_to_audio_caps (NULL,
      in_plugin->id, FALSE, in_plugin);
  if (!srccaps) {
    GST_DEBUG ("Couldn't get source caps for decoder '%s'", in_plugin->name);
    srccaps = gst_caps_from_string ("audio/x-raw");
  }

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  gst_caps_unref (sinkcaps);
  gst_caps_unref (srccaps);

  klass->in_plugin = in_plugin;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;
}

static void
gst_ffmpegauddec_class_init (GstFFMpegAudDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioDecoderClass *gstaudiodecoder_class = GST_AUDIO_DECODER_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_ffmpegauddec_finalize;

  gstaudiodecoder_class->start = GST_DEBUG_FUNCPTR (gst_ffmpegauddec_start);
  gstaudiodecoder_class->stop = GST_DEBUG_FUNCPTR (gst_ffmpegauddec_stop);
  gstaudiodecoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_ffmpegauddec_set_format);
  gstaudiodecoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_ffmpegauddec_handle_frame);
  gstaudiodecoder_class->flush = GST_DEBUG_FUNCPTR (gst_ffmpegauddec_flush);
  gstaudiodecoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_ffmpegauddec_propose_allocation);
}

static void
gst_ffmpegauddec_init (GstFFMpegAudDec * ffmpegdec)
{
  GstFFMpegAudDecClass *klass =
      (GstFFMpegAudDecClass *) G_OBJECT_GET_CLASS (ffmpegdec);

  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context3 (klass->in_plugin);
  ffmpegdec->context->opaque = ffmpegdec;
  ffmpegdec->opened = FALSE;

  ffmpegdec->frame = av_frame_alloc ();

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (ffmpegdec));
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST
      (ffmpegdec), TRUE);

  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (ffmpegdec), TRUE);
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (ffmpegdec), TRUE);
}

static void
gst_ffmpegauddec_finalize (GObject * object)
{
  GstFFMpegAudDec *ffmpegdec = (GstFFMpegAudDec *) object;

  av_frame_free (&ffmpegdec->frame);

  if (ffmpegdec->context != NULL) {
    gst_ffmpeg_avcodec_close (ffmpegdec->context);
    av_free (ffmpegdec->context);
    ffmpegdec->context = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* With LOCK */
static gboolean
gst_ffmpegauddec_close (GstFFMpegAudDec * ffmpegdec, gboolean reset)
{
  GstFFMpegAudDecClass *oclass;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_LOG_OBJECT (ffmpegdec, "closing libav codec");

  gst_caps_replace (&ffmpegdec->last_caps, NULL);
  gst_buffer_replace (&ffmpegdec->outbuf, NULL);

  gst_ffmpeg_avcodec_close (ffmpegdec->context);
  ffmpegdec->opened = FALSE;

  if (ffmpegdec->context->extradata) {
    av_free (ffmpegdec->context->extradata);
    ffmpegdec->context->extradata = NULL;
  }

  if (reset) {
    if (avcodec_get_context_defaults3 (ffmpegdec->context,
            oclass->in_plugin) < 0) {
      GST_DEBUG_OBJECT (ffmpegdec, "Failed to set context defaults");
      return FALSE;
    }
    ffmpegdec->context->opaque = ffmpegdec;
  }

  return TRUE;
}

static gboolean
gst_ffmpegauddec_start (GstAudioDecoder * decoder)
{
  GstFFMpegAudDec *ffmpegdec = (GstFFMpegAudDec *) decoder;
  GstFFMpegAudDecClass *oclass;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_OBJECT_LOCK (ffmpegdec);
  gst_ffmpeg_avcodec_close (ffmpegdec->context);
  if (avcodec_get_context_defaults3 (ffmpegdec->context, oclass->in_plugin) < 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to set context defaults");
    GST_OBJECT_UNLOCK (ffmpegdec);
    return FALSE;
  }
  ffmpegdec->context->opaque = ffmpegdec;
  GST_OBJECT_UNLOCK (ffmpegdec);

  return TRUE;
}

static gboolean
gst_ffmpegauddec_stop (GstAudioDecoder * decoder)
{
  GstFFMpegAudDec *ffmpegdec = (GstFFMpegAudDec *) decoder;

  GST_OBJECT_LOCK (ffmpegdec);
  gst_ffmpegauddec_close (ffmpegdec, FALSE);
  g_free (ffmpegdec->padded);
  ffmpegdec->padded = NULL;
  ffmpegdec->padded_size = 0;
  GST_OBJECT_UNLOCK (ffmpegdec);
  gst_audio_info_init (&ffmpegdec->info);
  gst_caps_replace (&ffmpegdec->last_caps, NULL);

  return TRUE;
}

/* with LOCK */
static gboolean
gst_ffmpegauddec_open (GstFFMpegAudDec * ffmpegdec)
{
  GstFFMpegAudDecClass *oclass;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (gst_ffmpeg_avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0)
    goto could_not_open;

  ffmpegdec->opened = TRUE;

  GST_LOG_OBJECT (ffmpegdec, "Opened libav codec %s, id %d",
      oclass->in_plugin->name, oclass->in_plugin->id);

  gst_audio_info_init (&ffmpegdec->info);

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    gst_ffmpegauddec_close (ffmpegdec, TRUE);
    GST_DEBUG_OBJECT (ffmpegdec, "avdec_%s: Failed to open libav codec",
        oclass->in_plugin->name);
    return FALSE;
  }
}

static gboolean
gst_ffmpegauddec_propose_allocation (GstAudioDecoder * decoder,
    GstQuery * query)
{
  GstAllocationParams params;

  gst_allocation_params_init (&params);
  params.flags = GST_MEMORY_FLAG_ZERO_PADDED;
  params.align = 15;
  params.padding = FF_INPUT_BUFFER_PADDING_SIZE;
  /* we would like to have some padding so that we don't have to
   * memcpy. We don't suggest an allocator. */
  gst_query_add_allocation_param (query, NULL, &params);

  return GST_AUDIO_DECODER_CLASS (parent_class)->propose_allocation (decoder,
      query);
}

static gboolean
gst_ffmpegauddec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  GstFFMpegAudDec *ffmpegdec = (GstFFMpegAudDec *) decoder;
  GstFFMpegAudDecClass *oclass;
  gboolean ret = TRUE;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_DEBUG_OBJECT (ffmpegdec, "setcaps called");

  GST_OBJECT_LOCK (ffmpegdec);

  if (ffmpegdec->last_caps && gst_caps_is_equal (ffmpegdec->last_caps, caps)) {
    GST_DEBUG_OBJECT (ffmpegdec, "same caps");
    GST_OBJECT_UNLOCK (ffmpegdec);
    return TRUE;
  }

  gst_caps_replace (&ffmpegdec->last_caps, caps);

  /* close old session */
  if (ffmpegdec->opened) {
    GST_OBJECT_UNLOCK (ffmpegdec);
    gst_ffmpegauddec_drain (ffmpegdec);
    GST_OBJECT_LOCK (ffmpegdec);
    if (!gst_ffmpegauddec_close (ffmpegdec, TRUE)) {
      GST_OBJECT_UNLOCK (ffmpegdec);
      return FALSE;
    }
  }

  /* get size and so */
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, caps, ffmpegdec->context);

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;
  ffmpegdec->context->err_recognition = 1;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegauddec_open (ffmpegdec))
    goto open_failed;

done:
  GST_OBJECT_UNLOCK (ffmpegdec);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to open");
    ret = FALSE;
    goto done;
  }
}

static gboolean
settings_changed (GstFFMpegAudDec * ffmpegdec, AVFrame * frame)
{
  GstAudioFormat format;
  gint channels =
      av_get_channel_layout_nb_channels (av_frame_get_channel_layout (frame));

  format = gst_ffmpeg_smpfmt_to_audioformat (frame->format);
  if (format == GST_AUDIO_FORMAT_UNKNOWN)
    return TRUE;

  return !(ffmpegdec->info.rate ==
      av_frame_get_sample_rate (frame) &&
      ffmpegdec->info.channels == channels &&
      ffmpegdec->info.finfo->format == format);
}

static gboolean
gst_ffmpegauddec_negotiate (GstFFMpegAudDec * ffmpegdec,
    AVCodecContext * context, AVFrame * frame, gboolean force)
{
  GstFFMpegAudDecClass *oclass;
  GstAudioFormat format;
  gint channels;
  GstAudioChannelPosition pos[64] = { 0, };

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  format = gst_ffmpeg_smpfmt_to_audioformat (frame->format);
  if (format == GST_AUDIO_FORMAT_UNKNOWN)
    goto no_caps;
  channels =
      av_get_channel_layout_nb_channels (av_frame_get_channel_layout (frame));
  if (channels == 0)
    channels = av_frame_get_channels (frame);
  if (channels == 0)
    goto no_caps;

  if (!force && !settings_changed (ffmpegdec, frame))
    return TRUE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "Renegotiating audio from %dHz@%dchannels (%d) to %dHz@%dchannels (%d)",
      ffmpegdec->info.rate, ffmpegdec->info.channels,
      ffmpegdec->info.finfo->format, av_frame_get_sample_rate (frame), channels,
      format);

  gst_ffmpeg_channel_layout_to_gst (av_frame_get_channel_layout (frame),
      channels, pos);
  memcpy (ffmpegdec->ffmpeg_layout, pos,
      sizeof (GstAudioChannelPosition) * channels);

  /* Get GStreamer channel layout */
  gst_audio_channel_positions_to_valid_order (pos, channels);
  ffmpegdec->needs_reorder =
      memcmp (pos, ffmpegdec->ffmpeg_layout, sizeof (pos[0]) * channels) != 0;
  gst_audio_info_set_format (&ffmpegdec->info, format,
      av_frame_get_sample_rate (frame), channels, pos);

  if (!gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (ffmpegdec),
          &ffmpegdec->info))
    goto caps_failed;

  return TRUE;

  /* ERRORS */
no_caps:
  {
#ifdef HAVE_LIBAV_UNINSTALLED
    /* using internal ffmpeg snapshot */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for libav codec '%s'.",
            oclass->in_plugin->name), (NULL));
#else
    /* using external ffmpeg */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for libav codec '%s', and "
            "you are using an external libavcodec. This is most likely due to "
            "a packaging problem and/or libavcodec having been upgraded to a "
            "version that is not compatible with this version of "
            "gstreamer-libav. Make sure your gstreamer-libav and libavcodec "
            "packages come from the same source/repository.",
            oclass->in_plugin->name), (NULL));
#endif
    return FALSE;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("Could not set caps for libav decoder (%s), not fixed?",
            oclass->in_plugin->name));
    memset (&ffmpegdec->info, 0, sizeof (ffmpegdec->info));

    return FALSE;
  }
}

static void
gst_avpacket_init (AVPacket * packet, guint8 * data, guint size)
{
  memset (packet, 0, sizeof (AVPacket));
  packet->data = data;
  packet->size = size;
}

static gint
gst_ffmpegauddec_audio_frame (GstFFMpegAudDec * ffmpegdec,
    AVCodec * in_plugin, guint8 * data, guint size, gint * have_data,
    GstBuffer ** outbuf, GstFlowReturn * ret)
{
  gint len = -1;
  AVPacket packet;

  GST_DEBUG_OBJECT (ffmpegdec, "size: %d", size);

  gst_avpacket_init (&packet, data, size);
  len =
      avcodec_decode_audio4 (ffmpegdec->context, ffmpegdec->frame, have_data,
      &packet);

  GST_DEBUG_OBJECT (ffmpegdec,
      "Decode audio: len=%d, have_data=%d", len, *have_data);

  if (len >= 0 && *have_data) {
    gint nsamples, channels, byte_per_sample;
    gsize output_size;

    if (!gst_ffmpegauddec_negotiate (ffmpegdec, ffmpegdec->context,
            ffmpegdec->frame, FALSE)) {
      *outbuf = NULL;
      *ret = GST_FLOW_NOT_NEGOTIATED;
      len = -1;
      goto beach;
    }

    channels = ffmpegdec->info.channels;
    nsamples = ffmpegdec->frame->nb_samples;
    byte_per_sample = ffmpegdec->info.finfo->width / 8;

    /* ffmpegdec->frame->linesize[0] might contain padding, allocate only what's needed */
    output_size = nsamples * byte_per_sample * channels;

    GST_DEBUG_OBJECT (ffmpegdec, "Creating output buffer");
    if (av_sample_fmt_is_planar (ffmpegdec->context->sample_fmt)
        && channels > 1) {
      gint i, j;
      GstMapInfo minfo;

      /* note: linesize[0] might contain padding, allocate only what's needed */
      *outbuf =
          gst_audio_decoder_allocate_output_buffer (GST_AUDIO_DECODER
          (ffmpegdec), output_size);

      gst_buffer_map (*outbuf, &minfo, GST_MAP_WRITE);

      switch (ffmpegdec->info.finfo->width) {
        case 8:{
          guint8 *odata = minfo.data;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              odata[j] =
                  ((const guint8 *) ffmpegdec->frame->extended_data[j])[i];
            }
            odata += channels;
          }
          break;
        }
        case 16:{
          guint16 *odata = (guint16 *) minfo.data;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              odata[j] =
                  ((const guint16 *) ffmpegdec->frame->extended_data[j])[i];
            }
            odata += channels;
          }
          break;
        }
        case 32:{
          guint32 *odata = (guint32 *) minfo.data;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              odata[j] =
                  ((const guint32 *) ffmpegdec->frame->extended_data[j])[i];
            }
            odata += channels;
          }
          break;
        }
        case 64:{
          guint64 *odata = (guint64 *) minfo.data;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              odata[j] =
                  ((const guint64 *) ffmpegdec->frame->extended_data[j])[i];
            }
            odata += channels;
          }
          break;
        }
        default:
          g_assert_not_reached ();
          break;
      }
      gst_buffer_unmap (*outbuf, &minfo);
    } else {
      *outbuf =
          gst_audio_decoder_allocate_output_buffer (GST_AUDIO_DECODER
          (ffmpegdec), output_size);
      gst_buffer_fill (*outbuf, 0, ffmpegdec->frame->data[0], output_size);
    }

    GST_DEBUG_OBJECT (ffmpegdec, "Buffer created. Size: %" G_GSIZE_FORMAT,
        output_size);

    /* Reorder channels to the GStreamer channel order */
    if (ffmpegdec->needs_reorder) {
      *outbuf = gst_buffer_make_writable (*outbuf);
      gst_audio_buffer_reorder_channels (*outbuf, ffmpegdec->info.finfo->format,
          ffmpegdec->info.channels, ffmpegdec->ffmpeg_layout,
          ffmpegdec->info.position);
    }

    /* Mark corrupted frames as corrupted */
    if (ffmpegdec->frame->flags & AV_FRAME_FLAG_CORRUPT)
      GST_BUFFER_FLAG_SET (*outbuf, GST_BUFFER_FLAG_CORRUPTED);
  } else {
    *outbuf = NULL;
  }

beach:
  av_frame_unref (ffmpegdec->frame);
  GST_DEBUG_OBJECT (ffmpegdec, "return flow %d, out %p, len %d",
      *ret, *outbuf, len);
  return len;
}

/* gst_ffmpegauddec_frame:
 * ffmpegdec:
 * data: pointer to the data to decode
 * size: size of data in bytes
 * got_data: 0 if no data was decoded, != 0 otherwise.
 * in_time: timestamp of data
 * in_duration: duration of data
 * ret: GstFlowReturn to return in the chain function
 *
 * Decode the given frame and pushes it downstream.
 *
 * Returns: Number of bytes used in decoding, -1 on error/failure.
 */

static gint
gst_ffmpegauddec_frame (GstFFMpegAudDec * ffmpegdec,
    guint8 * data, guint size, gint * have_data, GstFlowReturn * ret)
{
  GstFFMpegAudDecClass *oclass;
  GstBuffer *outbuf = NULL;
  gint len = 0;

  if (G_UNLIKELY (ffmpegdec->context->codec == NULL))
    goto no_codec;

  GST_LOG_OBJECT (ffmpegdec, "data:%p, size:%d", data, size);

  *ret = GST_FLOW_OK;
  ffmpegdec->context->frame_number++;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  len =
      gst_ffmpegauddec_audio_frame (ffmpegdec, oclass->in_plugin, data, size,
      have_data, &outbuf, ret);

  if (len < 0) {
    GST_WARNING_OBJECT (ffmpegdec,
        "avdec_%s: decoding error (len: %d, have_data: %d)",
        oclass->in_plugin->name, len, *have_data);
    goto beach;
  }

  if (outbuf) {
    GST_LOG_OBJECT (ffmpegdec, "Decoded data, now storing buffer %p", outbuf);

    if (ffmpegdec->outbuf)
      ffmpegdec->outbuf = gst_buffer_append (ffmpegdec->outbuf, outbuf);
    else
      ffmpegdec->outbuf = outbuf;
  } else {
    GST_DEBUG_OBJECT (ffmpegdec, "We didn't get a decoded buffer");
  }

beach:
  return len;

  /* ERRORS */
no_codec:
  {
    GST_ERROR_OBJECT (ffmpegdec, "no codec context");
    return -1;
  }
}

static void
gst_ffmpegauddec_drain (GstFFMpegAudDec * ffmpegdec)
{
  GstFFMpegAudDecClass *oclass;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
    gint have_data, len;

    GST_LOG_OBJECT (ffmpegdec,
        "codec has delay capabilities, calling until libav has drained everything");

    do {
      GstFlowReturn ret;

      len = gst_ffmpegauddec_frame (ffmpegdec, NULL, 0, &have_data, &ret);

    } while (len >= 0 && have_data == 1);
    avcodec_flush_buffers (ffmpegdec->context);
  }

  if (ffmpegdec->outbuf)
    gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (ffmpegdec),
        ffmpegdec->outbuf, 1);
  ffmpegdec->outbuf = NULL;
}

static void
gst_ffmpegauddec_flush (GstAudioDecoder * decoder, gboolean hard)
{
  GstFFMpegAudDec *ffmpegdec = (GstFFMpegAudDec *) decoder;

  if (ffmpegdec->opened) {
    avcodec_flush_buffers (ffmpegdec->context);
  }
}

static GstFlowReturn
gst_ffmpegauddec_handle_frame (GstAudioDecoder * decoder, GstBuffer * inbuf)
{
  GstFFMpegAudDec *ffmpegdec;
  GstFFMpegAudDecClass *oclass;
  guint8 *data, *bdata;
  GstMapInfo map;
  gint size, bsize, len, have_data;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean do_padding, is_header;

  ffmpegdec = (GstFFMpegAudDec *) decoder;

  if (G_UNLIKELY (!ffmpegdec->opened))
    goto not_negotiated;

  if (inbuf == NULL) {
    gst_ffmpegauddec_drain (ffmpegdec);
    return GST_FLOW_OK;
  }

  inbuf = gst_buffer_ref (inbuf);
  is_header = GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_HEADER);

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %" G_GSIZE_FORMAT ", offset:%" G_GUINT64_FORMAT
      ", ts:%" GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT,
      gst_buffer_get_size (inbuf), GST_BUFFER_OFFSET (inbuf),
      GST_TIME_ARGS (GST_BUFFER_PTS (inbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)));

  /* workarounds, functions write to buffers:
   *  libavcodec/svq1.c:svq1_decode_frame writes to the given buffer.
   *  libavcodec/svq3.c:svq3_decode_slice_header too.
   * ffmpeg devs know about it and will fix it (they said). */
  if (oclass->in_plugin->id == AV_CODEC_ID_SVQ1 ||
      oclass->in_plugin->id == AV_CODEC_ID_SVQ3) {
    inbuf = gst_buffer_make_writable (inbuf);
  }

  gst_buffer_map (inbuf, &map, GST_MAP_READ);

  bdata = map.data;
  bsize = map.size;

  if (bsize > 0 && (!GST_MEMORY_IS_ZERO_PADDED (map.memory)
          || (map.maxsize - map.size) < FF_INPUT_BUFFER_PADDING_SIZE)) {
    /* add padding */
    if (ffmpegdec->padded_size < bsize + FF_INPUT_BUFFER_PADDING_SIZE) {
      ffmpegdec->padded_size = bsize + FF_INPUT_BUFFER_PADDING_SIZE;
      ffmpegdec->padded = g_realloc (ffmpegdec->padded, ffmpegdec->padded_size);
      GST_LOG_OBJECT (ffmpegdec, "resized padding buffer to %d",
          ffmpegdec->padded_size);
    }
    GST_CAT_TRACE_OBJECT (CAT_PERFORMANCE, ffmpegdec,
        "Copy input to add padding");
    memcpy (ffmpegdec->padded, bdata, bsize);
    memset (ffmpegdec->padded + bsize, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    bdata = ffmpegdec->padded;
    do_padding = TRUE;
  } else {
    do_padding = FALSE;
  }

  do {
    guint8 tmp_padding[FF_INPUT_BUFFER_PADDING_SIZE];

    data = bdata;
    size = bsize;

    if (do_padding) {
      /* add temporary padding */
      GST_CAT_TRACE_OBJECT (CAT_PERFORMANCE, ffmpegdec,
          "Add temporary input padding");
      memcpy (tmp_padding, data + size, FF_INPUT_BUFFER_PADDING_SIZE);
      memset (data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    /* decode a frame of audio now */
    len = gst_ffmpegauddec_frame (ffmpegdec, data, size, &have_data, &ret);

    if (do_padding) {
      memcpy (data + size, tmp_padding, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (ffmpegdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      /* bad flow return, make sure we discard all data and exit */
      bsize = 0;
      break;
    }

    if (len == 0 && have_data == 0) {
      /* nothing was decoded, this could be because no data was available or
       * because we were skipping frames.
       * If we have no context we must exit and wait for more data, we keep the
       * data we tried. */
      GST_LOG_OBJECT (ffmpegdec, "Decoding didn't return any data, breaking");
      break;
    } else if (len < 0) {
      /* a decoding error happened, we must break and try again with next data. */
      GST_LOG_OBJECT (ffmpegdec, "Decoding error, breaking");
      bsize = 0;
      break;
    }
    /* prepare for the next round, for codecs with a context we did this
     * already when using the parser. */
    bsize -= len;
    bdata += len;

    do_padding = TRUE;

    GST_LOG_OBJECT (ffmpegdec, "Before (while bsize>0).  bsize:%d , bdata:%p",
        bsize, bdata);
  } while (bsize > 0);

  gst_buffer_unmap (inbuf, &map);
  gst_buffer_unref (inbuf);

  if (ffmpegdec->outbuf)
    ret =
        gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (ffmpegdec),
        ffmpegdec->outbuf, 1);
  else if (len < 0 || is_header)
    ret =
        gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (ffmpegdec), NULL, 1);
  ffmpegdec->outbuf = NULL;

  if (bsize > 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Dropping %d bytes of data", bsize);
  }

  return ret;

  /* ERRORS */
not_negotiated:
  {
    oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("avdec_%s: input format was not set before data start",
            oclass->in_plugin->name));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

gboolean
gst_ffmpegauddec_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegAudDecClass),
    (GBaseInitFunc) gst_ffmpegauddec_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegauddec_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegAudDec),
    0,
    (GInstanceInitFunc) gst_ffmpegauddec_init,
  };
  GType type;
  AVCodec *in_plugin;
  gint rank;

  in_plugin = av_codec_next (NULL);

  GST_LOG ("Registering decoders");

  while (in_plugin) {
    gchar *type_name;

    /* only decoders */
    if (!av_codec_is_decoder (in_plugin)
        || in_plugin->type != AVMEDIA_TYPE_AUDIO) {
      goto next;
    }

    /* no quasi codecs, please */
    if (in_plugin->id == AV_CODEC_ID_PCM_S16LE_PLANAR ||
        (in_plugin->id >= AV_CODEC_ID_PCM_S16LE &&
            in_plugin->id <= AV_CODEC_ID_PCM_BLURAY) ||
        (in_plugin->id >= AV_CODEC_ID_PCM_S8_PLANAR &&
#if AV_VERSION_INT (LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) >= AV_VERSION_INT (57,69,0)
            in_plugin->id <= AV_CODEC_ID_PCM_F24LE)) {
#elif AV_VERSION_INT (LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) >= AV_VERSION_INT (57,54,0)
            in_plugin->id <= AV_CODEC_ID_PCM_S64BE)) {
#else
            in_plugin->id <= AV_CODEC_ID_PCM_S16BE_PLANAR)) {
#endif
      goto next;
    }

    /* No decoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library decoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      goto next;
    }

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    /* MP1 : Use MP3 for decoding */
    /* MP2 : Use MP3 for decoding */
    /* Theora: Use libtheora based theoradec */
    if (!strcmp (in_plugin->name, "vorbis") ||
        !strcmp (in_plugin->name, "wavpack") ||
        !strcmp (in_plugin->name, "mp1") ||
        !strcmp (in_plugin->name, "mp2") ||
        !strcmp (in_plugin->name, "libfaad") ||
        !strcmp (in_plugin->name, "mpeg4aac") ||
        !strcmp (in_plugin->name, "ass") ||
        !strcmp (in_plugin->name, "srt") ||
        !strcmp (in_plugin->name, "pgssub") ||
        !strcmp (in_plugin->name, "dvdsub") ||
        !strcmp (in_plugin->name, "dvbsub")) {
      GST_LOG ("Ignoring decoder %s", in_plugin->name);
      goto next;
    }

    /* construct the type */
    type_name = g_strdup_printf ("avdec_%s", in_plugin->name);
    g_strdelimit (type_name, ".,|-<> ", '_');

    type = g_type_from_name (type_name);

    if (!type) {
      /* create the gtype now */
      type =
          g_type_register_static (GST_TYPE_AUDIO_DECODER, type_name, &typeinfo,
          0);
      g_type_set_qdata (type, GST_FFDEC_PARAMS_QDATA, (gpointer) in_plugin);
    }

    /* (Ronald) MPEG-4 gets a higher priority because it has been well-
     * tested and by far outperforms divxdec/xviddec - so we prefer it.
     * msmpeg4v3 same, as it outperforms divxdec for divx3 playback.
     * VC1/WMV3 are not working and thus unpreferred for now. */
    switch (in_plugin->id) {
      case AV_CODEC_ID_RA_144:
      case AV_CODEC_ID_RA_288:
      case AV_CODEC_ID_COOK:
      case AV_CODEC_ID_AAC:
        rank = GST_RANK_PRIMARY;
        break;
        /* SIPR: decoder should have a higher rank than realaudiodec.
         */
      case AV_CODEC_ID_SIPR:
        rank = GST_RANK_SECONDARY;
        break;
      default:
        rank = GST_RANK_MARGINAL;
        break;
    }
    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_warning ("Failed to register %s", type_name);
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = av_codec_next (in_plugin);
  }

  GST_LOG ("Finished Registering decoders");

  return TRUE;
}
