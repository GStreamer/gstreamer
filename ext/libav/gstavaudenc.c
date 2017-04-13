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
/* for stats file handling */
#include <stdio.h>
#include <glib/gstdio.h>
#include <errno.h>

#include <libavcodec/avcodec.h>

#include <gst/gst.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavaudenc.h"

#define DEFAULT_AUDIO_BITRATE 128000

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_BIT_RATE,
  PROP_RTP_PAYLOAD_SIZE,
  PROP_COMPLIANCE,
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegaudenc_class_init (GstFFMpegAudEncClass * klass);
static void gst_ffmpegaudenc_base_init (GstFFMpegAudEncClass * klass);
static void gst_ffmpegaudenc_init (GstFFMpegAudEnc * ffmpegaudenc);
static void gst_ffmpegaudenc_finalize (GObject * object);

static gboolean gst_ffmpegaudenc_set_format (GstAudioEncoder * encoder,
    GstAudioInfo * info);
static GstFlowReturn gst_ffmpegaudenc_handle_frame (GstAudioEncoder * encoder,
    GstBuffer * inbuf);
static gboolean gst_ffmpegaudenc_start (GstAudioEncoder * encoder);
static gboolean gst_ffmpegaudenc_stop (GstAudioEncoder * encoder);
static void gst_ffmpegaudenc_flush (GstAudioEncoder * encoder);

static void gst_ffmpegaudenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegaudenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

#define GST_FFENC_PARAMS_QDATA g_quark_from_static_string("avenc-params")

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegaudenc_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegaudenc_base_init (GstFFMpegAudEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  AVCodec *in_plugin;
  GstPadTemplate *srctempl = NULL, *sinktempl = NULL;
  GstCaps *srccaps = NULL, *sinkcaps = NULL;
  gchar *longname, *description;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFENC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("libav %s encoder", in_plugin->long_name);
  description = g_strdup_printf ("libav %s encoder", in_plugin->name);
  gst_element_class_set_metadata (element_class, longname,
      "Codec/Encoder/Audio", description,
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  g_free (longname);
  g_free (description);

  if (!(srccaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", in_plugin->name);
    srccaps = gst_caps_new_empty_simple ("unknown/unknown");
  }

  sinkcaps = gst_ffmpeg_codectype_to_audio_caps (NULL,
      in_plugin->id, TRUE, in_plugin);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for encoder '%s'", in_plugin->name);
    sinkcaps = gst_caps_new_empty_simple ("unknown/unknown");
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

  return;
}

static void
gst_ffmpegaudenc_class_init (GstFFMpegAudEncClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioEncoderClass *gstaudioencoder_class;

  gobject_class = (GObjectClass *) klass;
  gstaudioencoder_class = (GstAudioEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ffmpegaudenc_set_property;
  gobject_class->get_property = gst_ffmpegaudenc_get_property;

  /* FIXME: could use -1 for a sensible per-codec defaults */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BIT_RATE,
      g_param_spec_int ("bitrate", "Bit Rate",
          "Target Audio Bitrate", 0, G_MAXINT, DEFAULT_AUDIO_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COMPLIANCE,
      g_param_spec_enum ("compliance", "Compliance",
          "Adherence of the encoder to the specifications",
          GST_TYPE_FFMPEG_COMPLIANCE, FFMPEG_DEFAULT_COMPLIANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_ffmpegaudenc_finalize;

  gstaudioencoder_class->start = GST_DEBUG_FUNCPTR (gst_ffmpegaudenc_start);
  gstaudioencoder_class->stop = GST_DEBUG_FUNCPTR (gst_ffmpegaudenc_stop);
  gstaudioencoder_class->flush = GST_DEBUG_FUNCPTR (gst_ffmpegaudenc_flush);
  gstaudioencoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_ffmpegaudenc_set_format);
  gstaudioencoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_ffmpegaudenc_handle_frame);
}

static void
gst_ffmpegaudenc_init (GstFFMpegAudEnc * ffmpegaudenc)
{
  GstFFMpegAudEncClass *klass =
      (GstFFMpegAudEncClass *) G_OBJECT_GET_CLASS (ffmpegaudenc);

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_ENCODER_SINK_PAD (ffmpegaudenc));

  /* ffmpeg objects */
  ffmpegaudenc->context = avcodec_alloc_context3 (klass->in_plugin);
  ffmpegaudenc->opened = FALSE;
  ffmpegaudenc->frame = av_frame_alloc ();

  ffmpegaudenc->compliance = FFMPEG_DEFAULT_COMPLIANCE;

  gst_audio_encoder_set_drainable (GST_AUDIO_ENCODER (ffmpegaudenc), TRUE);
}

static void
gst_ffmpegaudenc_finalize (GObject * object)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) object;

  /* clean up remaining allocated data */
  av_frame_free (&ffmpegaudenc->frame);
  gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
  av_free (ffmpegaudenc->context);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ffmpegaudenc_start (GstAudioEncoder * encoder)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) encoder;
  GstFFMpegAudEncClass *oclass =
      (GstFFMpegAudEncClass *) G_OBJECT_GET_CLASS (ffmpegaudenc);

  gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
  if (avcodec_get_context_defaults3 (ffmpegaudenc->context,
          oclass->in_plugin) < 0) {
    GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to set context defaults");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ffmpegaudenc_stop (GstAudioEncoder * encoder)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) encoder;

  /* close old session */
  gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
  ffmpegaudenc->opened = FALSE;

  return TRUE;
}

static void
gst_ffmpegaudenc_flush (GstAudioEncoder * encoder)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) encoder;

  if (ffmpegaudenc->opened) {
    avcodec_flush_buffers (ffmpegaudenc->context);
  }
}

static gboolean
gst_ffmpegaudenc_set_format (GstAudioEncoder * encoder, GstAudioInfo * info)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) encoder;
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  gsize frame_size;
  GstFFMpegAudEncClass *oclass =
      (GstFFMpegAudEncClass *) G_OBJECT_GET_CLASS (ffmpegaudenc);

  /* close old session */
  if (ffmpegaudenc->opened) {
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    ffmpegaudenc->opened = FALSE;
    if (avcodec_get_context_defaults3 (ffmpegaudenc->context,
            oclass->in_plugin) < 0) {
      GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to set context defaults");
      return FALSE;
    }
  }

  /* if we set it in _getcaps we should set it also in _link */
  ffmpegaudenc->context->strict_std_compliance = ffmpegaudenc->compliance;

  /* user defined properties */
  if (ffmpegaudenc->bitrate > 0) {
    GST_INFO_OBJECT (ffmpegaudenc, "Setting avcontext to bitrate %d",
        ffmpegaudenc->bitrate);
    ffmpegaudenc->context->bit_rate = ffmpegaudenc->bitrate;
    ffmpegaudenc->context->bit_rate_tolerance = ffmpegaudenc->bitrate;
  } else {
    GST_INFO_OBJECT (ffmpegaudenc,
        "Using avcontext default bitrate %" G_GINT64_FORMAT,
        (gint64) ffmpegaudenc->context->bit_rate);
  }

  /* RTP payload used for GOB production (for Asterisk) */
  if (ffmpegaudenc->rtp_payload_size) {
    ffmpegaudenc->context->rtp_payload_size = ffmpegaudenc->rtp_payload_size;
  }

  /* some other defaults */
  ffmpegaudenc->context->rc_strategy = 2;
  ffmpegaudenc->context->b_frame_strategy = 0;
  ffmpegaudenc->context->coder_type = 0;
  ffmpegaudenc->context->context_model = 0;
  ffmpegaudenc->context->scenechange_threshold = 0;

  /* fetch pix_fmt and so on */
  gst_ffmpeg_audioinfo_to_context (info, ffmpegaudenc->context);
  if (!ffmpegaudenc->context->time_base.den) {
    ffmpegaudenc->context->time_base.den = GST_AUDIO_INFO_RATE (info);
    ffmpegaudenc->context->time_base.num = 1;
    ffmpegaudenc->context->ticks_per_frame = 1;
  }

  if (ffmpegaudenc->context->channel_layout) {
    gst_ffmpeg_channel_layout_to_gst (ffmpegaudenc->context->channel_layout,
        ffmpegaudenc->context->channels, ffmpegaudenc->ffmpeg_layout);
    ffmpegaudenc->needs_reorder =
        (memcmp (ffmpegaudenc->ffmpeg_layout, info->position,
            sizeof (GstAudioChannelPosition) *
            ffmpegaudenc->context->channels) != 0);
  }

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (ffmpegaudenc, "picking an output format ...");
  allowed_caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ffmpegaudenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_AUDIO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (ffmpegaudenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, allowed_caps, ffmpegaudenc->context);

  /* open codec */
  if (gst_ffmpeg_avcodec_open (ffmpegaudenc->context, oclass->in_plugin) < 0) {
    gst_caps_unref (allowed_caps);
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    GST_DEBUG_OBJECT (ffmpegaudenc, "avenc_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    if (avcodec_get_context_defaults3 (ffmpegaudenc->context,
            oclass->in_plugin) < 0)
      GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to set context defaults");

    if ((oclass->in_plugin->capabilities & CODEC_CAP_EXPERIMENTAL) &&
        ffmpegaudenc->compliance != GST_FFMPEG_EXPERIMENTAL) {
      GST_ELEMENT_ERROR (ffmpegaudenc, LIBRARY, SETTINGS,
          ("Codec is experimental, but settings don't allow encoders to "
              "produce output of experimental quality"),
          ("This codec may not create output that is conformant to the specs "
              "or of good quality. If you must use it anyway, set the "
              "compliance property to experimental"));
    }
    return FALSE;
  }

  /* try to set this caps on the other side */
  other_caps = gst_ffmpeg_codecid_to_caps (oclass->in_plugin->id,
      ffmpegaudenc->context, TRUE);

  if (!other_caps) {
    gst_caps_unref (allowed_caps);
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    GST_DEBUG ("Unsupported codec - no caps found");
    if (avcodec_get_context_defaults3 (ffmpegaudenc->context,
            oclass->in_plugin) < 0)
      GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to set context defaults");
    return FALSE;
  }

  icaps = gst_caps_intersect (allowed_caps, other_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (other_caps);
  if (gst_caps_is_empty (icaps)) {
    gst_caps_unref (icaps);
    return FALSE;
  }
  icaps = gst_caps_fixate (icaps);

  if (!gst_audio_encoder_set_output_format (GST_AUDIO_ENCODER (ffmpegaudenc),
          icaps)) {
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    gst_caps_unref (icaps);
    if (avcodec_get_context_defaults3 (ffmpegaudenc->context,
            oclass->in_plugin) < 0)
      GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to set context defaults");
    return FALSE;
  }
  gst_caps_unref (icaps);

  frame_size = ffmpegaudenc->context->frame_size;
  if (frame_size > 1) {
    gst_audio_encoder_set_frame_samples_min (GST_AUDIO_ENCODER (ffmpegaudenc),
        frame_size);
    gst_audio_encoder_set_frame_samples_max (GST_AUDIO_ENCODER (ffmpegaudenc),
        frame_size);
    gst_audio_encoder_set_frame_max (GST_AUDIO_ENCODER (ffmpegaudenc), 1);
  } else {
    gst_audio_encoder_set_frame_samples_min (GST_AUDIO_ENCODER (ffmpegaudenc),
        0);
    gst_audio_encoder_set_frame_samples_max (GST_AUDIO_ENCODER (ffmpegaudenc),
        0);
    gst_audio_encoder_set_frame_max (GST_AUDIO_ENCODER (ffmpegaudenc), 0);
  }

  /* Store some tags */
  {
    GstTagList *tags = gst_tag_list_new_empty ();
    const gchar *codec;

    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_NOMINAL_BITRATE,
        (guint) ffmpegaudenc->context->bit_rate, NULL);

    if ((codec =
            gst_ffmpeg_get_codecid_longname (ffmpegaudenc->context->codec_id)))
      gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_AUDIO_CODEC, codec,
          NULL);

    gst_audio_encoder_merge_tags (encoder, tags, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (tags);
  }

  /* success! */
  ffmpegaudenc->opened = TRUE;

  return TRUE;
}

static void
gst_ffmpegaudenc_free_avpacket (gpointer pkt)
{
  av_packet_unref ((AVPacket *) pkt);
  g_slice_free (AVPacket, pkt);
}

typedef struct
{
  GstBuffer *buffer;
  GstMapInfo map;

  guint8 **ext_data_array, *ext_data;
} BufferInfo;

static void
buffer_info_free (void *opaque, guint8 * data)
{
  BufferInfo *info = opaque;

  if (info->buffer) {
    gst_buffer_unmap (info->buffer, &info->map);
    gst_buffer_unref (info->buffer);
  } else {
    av_free (info->ext_data);
    av_free (info->ext_data_array);
  }
  g_slice_free (BufferInfo, info);
}

static GstFlowReturn
gst_ffmpegaudenc_encode_audio (GstFFMpegAudEnc * ffmpegaudenc,
    GstBuffer * buffer, gint * have_data)
{
  GstAudioEncoder *enc;
  AVCodecContext *ctx;
  gint res;
  GstFlowReturn ret;
  GstAudioInfo *info;
  AVPacket *pkt;
  AVFrame *frame = ffmpegaudenc->frame;
  gboolean planar;
  gint nsamples = -1;

  enc = GST_AUDIO_ENCODER (ffmpegaudenc);

  ctx = ffmpegaudenc->context;

  pkt = g_slice_new0 (AVPacket);

  if (buffer != NULL) {
    BufferInfo *buffer_info = g_slice_new0 (BufferInfo);
    guint8 *audio_in;
    guint in_size;

    buffer_info->buffer = buffer;
    gst_buffer_map (buffer, &buffer_info->map, GST_MAP_READ);
    audio_in = buffer_info->map.data;
    in_size = buffer_info->map.size;

    GST_LOG_OBJECT (ffmpegaudenc, "encoding buffer %p size:%u", audio_in,
        in_size);

    info = gst_audio_encoder_get_audio_info (enc);
    planar = av_sample_fmt_is_planar (ffmpegaudenc->context->sample_fmt);
    frame->format = ffmpegaudenc->context->sample_fmt;
    frame->sample_rate = ffmpegaudenc->context->sample_rate;
    frame->channels = ffmpegaudenc->context->channels;
    frame->channel_layout = ffmpegaudenc->context->channel_layout;

    if (planar && info->channels > 1) {
      gint channels;
      gint i, j;

      nsamples = frame->nb_samples = in_size / info->bpf;
      channels = info->channels;

      frame->buf[0] =
          av_buffer_create (NULL, 0, buffer_info_free, buffer_info, 0);

      if (info->channels > AV_NUM_DATA_POINTERS) {
        buffer_info->ext_data_array = frame->extended_data =
            av_malloc_array (info->channels, sizeof (uint8_t *));
      } else {
        frame->extended_data = frame->data;
      }

      buffer_info->ext_data = frame->extended_data[0] = av_malloc (in_size);
      frame->linesize[0] = in_size / channels;
      for (i = 1; i < channels; i++)
        frame->extended_data[i] =
            frame->extended_data[i - 1] + frame->linesize[0];

      switch (info->finfo->width) {
        case 8:{
          const guint8 *idata = (const guint8 *) audio_in;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              ((guint8 *) frame->extended_data[j])[i] = idata[j];
            }
            idata += channels;
          }
          break;
        }
        case 16:{
          const guint16 *idata = (const guint16 *) audio_in;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              ((guint16 *) frame->extended_data[j])[i] = idata[j];
            }
            idata += channels;
          }
          break;
        }
        case 32:{
          const guint32 *idata = (const guint32 *) audio_in;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              ((guint32 *) frame->extended_data[j])[i] = idata[j];
            }
            idata += channels;
          }

          break;
        }
        case 64:{
          const guint64 *idata = (const guint64 *) audio_in;

          for (i = 0; i < nsamples; i++) {
            for (j = 0; j < channels; j++) {
              ((guint64 *) frame->extended_data[j])[i] = idata[j];
            }
            idata += channels;
          }

          break;
        }
        default:
          g_assert_not_reached ();
          break;
      }

      gst_buffer_unmap (buffer, &buffer_info->map);
      gst_buffer_unref (buffer);
      buffer_info->buffer = NULL;
    } else {
      frame->data[0] = audio_in;
      frame->extended_data = frame->data;
      frame->linesize[0] = in_size;
      frame->nb_samples = nsamples = in_size / info->bpf;
      frame->buf[0] =
          av_buffer_create (NULL, 0, buffer_info_free, buffer_info, 0);
    }

    /* we have a frame to feed the encoder */
    res = avcodec_encode_audio2 (ctx, pkt, frame, have_data);

    av_frame_unref (frame);
  } else {
    GST_LOG_OBJECT (ffmpegaudenc, "draining");
    /* flushing the encoder */
    res = avcodec_encode_audio2 (ctx, pkt, NULL, have_data);
  }

  if (res < 0) {
    char error_str[128] = { 0, };

    g_slice_free (AVPacket, pkt);
    av_strerror (res, error_str, sizeof (error_str));
    GST_ERROR_OBJECT (enc, "Failed to encode buffer: %d - %s", res, error_str);
    return GST_FLOW_OK;
  }
  GST_LOG_OBJECT (ffmpegaudenc, "got output size %d", res);

  if (*have_data) {
    GstBuffer *outbuf;
    const AVCodec *codec;

    GST_LOG_OBJECT (ffmpegaudenc, "pushing size %d", pkt->size);

    outbuf =
        gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, pkt->data,
        pkt->size, 0, pkt->size, pkt, gst_ffmpegaudenc_free_avpacket);

    codec = ffmpegaudenc->context->codec;
    if ((codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE) || !buffer) {
      /* FIXME: Not really correct, as -1 means "all the samples we got
         given so far", which may not be true depending on the codec,
         but we have no way to know AFAICT */
      ret = gst_audio_encoder_finish_frame (enc, outbuf, -1);
    } else {
      ret = gst_audio_encoder_finish_frame (enc, outbuf, nsamples);
    }
  } else {
    GST_LOG_OBJECT (ffmpegaudenc, "no output produced");
    g_slice_free (AVPacket, pkt);
    ret = GST_FLOW_OK;
  }

  return ret;
}

static void
gst_ffmpegaudenc_drain (GstFFMpegAudEnc * ffmpegaudenc)
{
  GstFFMpegAudEncClass *oclass;

  oclass = (GstFFMpegAudEncClass *) (G_OBJECT_GET_CLASS (ffmpegaudenc));

  if (oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
    gint have_data, try = 0;

    GST_LOG_OBJECT (ffmpegaudenc,
        "codec has delay capabilities, calling until libav has drained everything");

    do {
      GstFlowReturn ret;

      ret = gst_ffmpegaudenc_encode_audio (ffmpegaudenc, NULL, &have_data);
      if (ret != GST_FLOW_OK || have_data == 0)
        break;
    } while (try++ < 10);
  }
}

static GstFlowReturn
gst_ffmpegaudenc_handle_frame (GstAudioEncoder * encoder, GstBuffer * inbuf)
{
  GstFFMpegAudEnc *ffmpegaudenc;
  GstFlowReturn ret;
  gint have_data;

  ffmpegaudenc = (GstFFMpegAudEnc *) encoder;

  if (G_UNLIKELY (!ffmpegaudenc->opened))
    goto not_negotiated;

  if (!inbuf) {
    gst_ffmpegaudenc_drain (ffmpegaudenc);
    return GST_FLOW_OK;
  }

  inbuf = gst_buffer_ref (inbuf);

  GST_DEBUG_OBJECT (ffmpegaudenc,
      "Received time %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (inbuf)), gst_buffer_get_size (inbuf));

  /* Reorder channels to the GStreamer channel order */
  if (ffmpegaudenc->needs_reorder) {
    GstAudioInfo *info = gst_audio_encoder_get_audio_info (encoder);

    inbuf = gst_buffer_make_writable (inbuf);
    gst_audio_buffer_reorder_channels (inbuf, info->finfo->format,
        info->channels, info->position, ffmpegaudenc->ffmpeg_layout);
  }

  ret = gst_ffmpegaudenc_encode_audio (ffmpegaudenc, inbuf, &have_data);

  if (ret != GST_FLOW_OK)
    goto push_failed;

  return GST_FLOW_OK;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (ffmpegaudenc, CORE, NEGOTIATION, (NULL),
        ("not configured to input format before data start"));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
push_failed:
  {
    GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to push buffer %d (%s)", ret,
        gst_flow_get_name (ret));
    return ret;
  }
}

static void
gst_ffmpegaudenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegAudEnc *ffmpegaudenc;

  /* Get a pointer of the right type. */
  ffmpegaudenc = (GstFFMpegAudEnc *) (object);

  if (ffmpegaudenc->opened) {
    GST_WARNING_OBJECT (ffmpegaudenc,
        "Can't change properties once decoder is setup !");
    return;
  }

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case PROP_BIT_RATE:
      ffmpegaudenc->bitrate = g_value_get_int (value);
      break;
    case PROP_RTP_PAYLOAD_SIZE:
      ffmpegaudenc->rtp_payload_size = g_value_get_int (value);
      break;
    case PROP_COMPLIANCE:
      ffmpegaudenc->compliance = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ffmpegaudenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegAudEnc *ffmpegaudenc;

  /* It's not null if we got it, but it might not be ours */
  ffmpegaudenc = (GstFFMpegAudEnc *) (object);

  switch (prop_id) {
    case PROP_BIT_RATE:
      g_value_set_int (value, ffmpegaudenc->bitrate);
      break;
      break;
    case PROP_RTP_PAYLOAD_SIZE:
      g_value_set_int (value, ffmpegaudenc->rtp_payload_size);
      break;
    case PROP_COMPLIANCE:
      g_value_set_enum (value, ffmpegaudenc->compliance);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegaudenc_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegAudEncClass),
    (GBaseInitFunc) gst_ffmpegaudenc_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegaudenc_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegAudEnc),
    0,
    (GInstanceInitFunc) gst_ffmpegaudenc_init,
  };
  GType type;
  AVCodec *in_plugin;


  GST_LOG ("Registering encoders");

  in_plugin = av_codec_next (NULL);
  while (in_plugin) {
    gchar *type_name;
    guint rank;

    /* Skip non-AV codecs */
    if (in_plugin->type != AVMEDIA_TYPE_AUDIO)
      goto next;

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

    /* No encoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library encoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      goto next;
    }

    /* only encoders */
    if (!av_codec_is_encoder (in_plugin)) {
      goto next;
    }

    /* FIXME : We should have a method to know cheaply whether we have a mapping
     * for the given plugin or not */

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    if (!strcmp (in_plugin->name, "vorbis")
        || !strcmp (in_plugin->name, "flac")) {
      GST_LOG ("Ignoring encoder %s", in_plugin->name);
      goto next;
    }

    /* construct the type */
    type_name = g_strdup_printf ("avenc_%s", in_plugin->name);

    type = g_type_from_name (type_name);

    if (!type) {

      /* create the glib type now */
      type =
          g_type_register_static (GST_TYPE_AUDIO_ENCODER, type_name, &typeinfo,
          0);
      g_type_set_qdata (type, GST_FFENC_PARAMS_QDATA, (gpointer) in_plugin);

      {
        static const GInterfaceInfo preset_info = {
          NULL,
          NULL,
          NULL
        };
        g_type_add_interface_static (type, GST_TYPE_PRESET, &preset_info);
      }
    }

    switch (in_plugin->id) {
        /* avenc_aac: see https://bugzilla.gnome.org/show_bug.cgi?id=691617 */
      case AV_CODEC_ID_AAC:
        rank = GST_RANK_NONE;
        break;
      default:
        rank = GST_RANK_SECONDARY;
        break;
    }

    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = av_codec_next (in_plugin);
  }

  GST_LOG ("Finished registering encoders");

  return TRUE;
}
