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
#include <libavutil/opt.h>

#include <gst/gst.h>
#include <gst/base/base.h>

#include "gstav.h"
#include "gstavcfg.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavaudenc.h"

enum
{
  PROP_0,
  PROP_CFG_BASE,
};

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

  gst_ffmpeg_cfg_install_properties (gobject_class, klass->in_plugin,
      PROP_CFG_BASE, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_AUDIO_PARAM);

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
  ffmpegaudenc->refcontext = avcodec_alloc_context3 (klass->in_plugin);
  ffmpegaudenc->opened = FALSE;
  ffmpegaudenc->frame = av_frame_alloc ();

  gst_audio_encoder_set_drainable (GST_AUDIO_ENCODER (ffmpegaudenc), TRUE);
}

static void
gst_ffmpegaudenc_finalize (GObject * object)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) object;

  /* clean up remaining allocated data */
  av_frame_free (&ffmpegaudenc->frame);
  avcodec_free_context (&ffmpegaudenc->context);
  avcodec_free_context (&ffmpegaudenc->refcontext);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ffmpegaudenc_start (GstAudioEncoder * encoder)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) encoder;
  GstFFMpegAudEncClass *oclass =
      (GstFFMpegAudEncClass *) G_OBJECT_GET_CLASS (ffmpegaudenc);

  ffmpegaudenc->opened = FALSE;
  ffmpegaudenc->need_reopen = FALSE;

  avcodec_free_context (&ffmpegaudenc->context);
  ffmpegaudenc->context = avcodec_alloc_context3 (oclass->in_plugin);
  if (ffmpegaudenc->context == NULL) {
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
  ffmpegaudenc->need_reopen = FALSE;

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

  ffmpegaudenc->need_reopen = FALSE;

  /* close old session */
  if (ffmpegaudenc->opened) {
    avcodec_free_context (&ffmpegaudenc->context);
    ffmpegaudenc->opened = FALSE;
    ffmpegaudenc->context = avcodec_alloc_context3 (oclass->in_plugin);
    if (ffmpegaudenc->context == NULL) {
      GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to set context defaults");
      return FALSE;
    }
  }

  gst_ffmpeg_cfg_fill_context (G_OBJECT (ffmpegaudenc), ffmpegaudenc->context);

  /* fetch pix_fmt and so on */
  gst_ffmpeg_audioinfo_to_context (info, ffmpegaudenc->context);
  if (!ffmpegaudenc->context->time_base.den) {
    ffmpegaudenc->context->time_base.den = GST_AUDIO_INFO_RATE (info);
    ffmpegaudenc->context->time_base.num = 1;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(60, 31, 100)
    ffmpegaudenc->context->ticks_per_frame = 1;
#endif
  }
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
  if (ffmpegaudenc->context->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC) {
    gst_ffmpeg_channel_layout_to_gst (&ffmpegaudenc->context->ch_layout,
        ffmpegaudenc->context->ch_layout.nb_channels,
        ffmpegaudenc->ffmpeg_layout);
    ffmpegaudenc->needs_reorder =
        (memcmp (ffmpegaudenc->ffmpeg_layout, info->position,
            sizeof (GstAudioChannelPosition) *
            ffmpegaudenc->context->ch_layout.nb_channels) != 0);
  }
#else
  if (ffmpegaudenc->context->channel_layout) {
    gst_ffmpeg_channel_layout_to_gst (ffmpegaudenc->context->channel_layout,
        ffmpegaudenc->context->channels, ffmpegaudenc->ffmpeg_layout);
    ffmpegaudenc->needs_reorder =
        (memcmp (ffmpegaudenc->ffmpeg_layout, info->position,
            sizeof (GstAudioChannelPosition) *
            ffmpegaudenc->context->channels) != 0);
  }
#endif

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
    avcodec_free_context (&ffmpegaudenc->context);
    GST_DEBUG_OBJECT (ffmpegaudenc, "avenc_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    ffmpegaudenc->context = avcodec_alloc_context3 (oclass->in_plugin);
    if (ffmpegaudenc->context == NULL)
      GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to set context defaults");

    if ((oclass->in_plugin->capabilities & AV_CODEC_CAP_EXPERIMENTAL) &&
        ffmpegaudenc->context->strict_std_compliance !=
        FF_COMPLIANCE_EXPERIMENTAL) {
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
    avcodec_free_context (&ffmpegaudenc->context);
    GST_DEBUG ("Unsupported codec - no caps found");
    ffmpegaudenc->context = avcodec_alloc_context3 (oclass->in_plugin);
    if (ffmpegaudenc->context == NULL)
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
    avcodec_free_context (&ffmpegaudenc->context);
    gst_caps_unref (icaps);
    ffmpegaudenc->context = avcodec_alloc_context3 (oclass->in_plugin);
    if (ffmpegaudenc->context == NULL)
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
  ffmpegaudenc->need_reopen = FALSE;

  return TRUE;
}

static void
gst_ffmpegaudenc_free_avpacket (gpointer pkt)
{
  av_packet_unref ((AVPacket *) pkt);
  g_free (pkt);
}

typedef struct
{
  GstBuffer *buffer;
  GstMapInfo map;

  guint8 *ext_data;
} BufferInfo;

static void
buffer_info_free (void *opaque, guint8 * data)
{
  BufferInfo *info = opaque;

  if (info->buffer) {
    gst_buffer_unmap (info->buffer, &info->map);
    gst_buffer_unref (info->buffer);
  } else {
    av_freep (&info->ext_data);
  }
  g_free (info);
}

static GstFlowReturn
gst_ffmpegaudenc_send_frame (GstFFMpegAudEnc * ffmpegaudenc, GstBuffer * buffer)
{
  GstAudioEncoder *enc;
  AVCodecContext *ctx;
  GstFlowReturn ret;
  gint res;
  GstAudioInfo *info;
  AVFrame *frame = ffmpegaudenc->frame;
  gboolean planar;
  gint nsamples = -1;

  enc = GST_AUDIO_ENCODER (ffmpegaudenc);

  ctx = ffmpegaudenc->context;

  if (buffer != NULL) {
    BufferInfo *buffer_info = g_new0 (BufferInfo, 1);
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
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 28, 100)
    av_channel_layout_copy (&frame->ch_layout,
        &ffmpegaudenc->context->ch_layout);
#else
    frame->channels = ffmpegaudenc->context->channels;
    frame->channel_layout = ffmpegaudenc->context->channel_layout;
#endif

    if (planar && info->channels > 1) {
      gint channels;
      gint i, j;

      nsamples = frame->nb_samples = in_size / info->bpf;
      channels = info->channels;

      frame->buf[0] =
          av_buffer_create (NULL, 0, buffer_info_free, buffer_info, 0);

      if (info->channels > AV_NUM_DATA_POINTERS) {
        frame->extended_data =
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
    res = avcodec_send_frame (ctx, frame);

    av_frame_unref (frame);
  } else {
    GstFFMpegAudEncClass *oclass =
        (GstFFMpegAudEncClass *) G_OBJECT_GET_CLASS (ffmpegaudenc);

    GST_LOG_OBJECT (ffmpegaudenc, "draining");
    /* flushing the encoder */
    res = avcodec_send_frame (ctx, NULL);

    /* If AV_CODEC_CAP_ENCODER_FLUSH wasn't set, we need to re-open
     * encoder */
    if (!(oclass->in_plugin->capabilities & AV_CODEC_CAP_ENCODER_FLUSH)) {
      GST_DEBUG_OBJECT (ffmpegaudenc, "Encoder needs reopen later");

      /* we will reopen later handle_frame() */
      ffmpegaudenc->need_reopen = TRUE;
    }
  }

  if (res == 0) {
    ret = GST_FLOW_OK;
  } else if (res == AVERROR_EOF) {
    ret = GST_FLOW_EOS;
  } else {                      /* Any other return value is an error in our context */
    ret = GST_FLOW_OK;
    GST_WARNING_OBJECT (ffmpegaudenc, "Failed to encode buffer");
  }

  return ret;
}

static GstFlowReturn
gst_ffmpegaudenc_receive_packet (GstFFMpegAudEnc * ffmpegaudenc,
    gboolean * got_packet)
{
  GstAudioEncoder *enc;
  AVCodecContext *ctx;
  gint res;
  GstFlowReturn ret;
  AVPacket *pkt;

  enc = GST_AUDIO_ENCODER (ffmpegaudenc);

  ctx = ffmpegaudenc->context;

  pkt = g_new0 (AVPacket, 1);

  res = avcodec_receive_packet (ctx, pkt);

  if (res == 0) {
    GstBuffer *outbuf;
    const uint8_t *side_data;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(58,130,0)
    size_t side_data_length = 0;
#else
    int side_data_length = 0;
#endif

    GST_LOG_OBJECT (ffmpegaudenc, "pushing size %d", pkt->size);

    outbuf =
        gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, pkt->data,
        pkt->size, 0, pkt->size, pkt, gst_ffmpegaudenc_free_avpacket);

    if ((side_data =
            av_packet_get_side_data (pkt, AV_PKT_DATA_SKIP_SAMPLES,
                &side_data_length)) && side_data_length == 10) {
      GstByteReader reader = GST_BYTE_READER_INIT (pkt->data, pkt->size);
      guint32 start, end;

      start = gst_byte_reader_get_uint32_le_unchecked (&reader);
      end = gst_byte_reader_get_uint32_le_unchecked (&reader);

      GST_LOG_OBJECT (ffmpegaudenc,
          "got skip samples side data with start %u and end %u", start, end);
      gst_buffer_add_audio_clipping_meta (outbuf, GST_FORMAT_DEFAULT, start,
          end);
    }

    ret =
        gst_audio_encoder_finish_frame (enc, outbuf,
        pkt->duration > 0 ? pkt->duration : -1);
    *got_packet = TRUE;
  } else {
    GST_LOG_OBJECT (ffmpegaudenc, "no output produced");
    g_free (pkt);
    ret = GST_FLOW_OK;
    *got_packet = FALSE;
  }

  return ret;
}

static GstFlowReturn
gst_ffmpegaudenc_drain (GstFFMpegAudEnc * ffmpegaudenc)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean got_packet;

  ret = gst_ffmpegaudenc_send_frame (ffmpegaudenc, NULL);

  if (ret == GST_FLOW_OK) {
    do {
      ret = gst_ffmpegaudenc_receive_packet (ffmpegaudenc, &got_packet);
      if (ret != GST_FLOW_OK)
        break;
    } while (got_packet);
  }

  /* NOTE: this may or may not work depending on capability */
  avcodec_flush_buffers (ffmpegaudenc->context);

  /* FFMpeg will return AVERROR_EOF if it's internal was fully drained
   * then we are translating it to GST_FLOW_EOS. However, because this behavior
   * is fully internal stuff of this implementation and gstaudioencoder
   * baseclass doesn't convert this GST_FLOW_EOS to GST_FLOW_OK,
   * convert this flow returned here */
  if (ret == GST_FLOW_EOS)
    ret = GST_FLOW_OK;

  return ret;
}

static GstFlowReturn
gst_ffmpegaudenc_handle_frame (GstAudioEncoder * encoder, GstBuffer * inbuf)
{
  GstFFMpegAudEnc *ffmpegaudenc;
  GstFlowReturn ret;
  gboolean got_packet;

  ffmpegaudenc = (GstFFMpegAudEnc *) encoder;

  if (G_UNLIKELY (!ffmpegaudenc->opened))
    goto not_negotiated;

  if (!inbuf)
    return gst_ffmpegaudenc_drain (ffmpegaudenc);

  /* endoder was drained or flushed, and ffmpeg encoder doesn't support
   * flushing. We need to re-open encoder then */
  if (ffmpegaudenc->need_reopen) {
    GST_DEBUG_OBJECT (ffmpegaudenc, "Open encoder again");

    if (!gst_ffmpegaudenc_set_format (encoder,
            gst_audio_encoder_get_audio_info (encoder))) {
      GST_ERROR_OBJECT (ffmpegaudenc, "Couldn't re-open encoder");
      return GST_FLOW_NOT_NEGOTIATED;
    }
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

  ret = gst_ffmpegaudenc_send_frame (ffmpegaudenc, inbuf);

  if (ret != GST_FLOW_OK)
    goto send_frame_failed;

  do {
    ret = gst_ffmpegaudenc_receive_packet (ffmpegaudenc, &got_packet);
  } while (got_packet);

  return GST_FLOW_OK;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (ffmpegaudenc, CORE, NEGOTIATION, (NULL),
        ("not configured to input format before data start"));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
send_frame_failed:
  {
    GST_DEBUG_OBJECT (ffmpegaudenc, "Failed to send frame %d (%s)", ret,
        gst_flow_get_name (ret));
    return ret;
  }
}

static void
gst_ffmpegaudenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegAudEnc *ffmpegaudenc;

  ffmpegaudenc = (GstFFMpegAudEnc *) (object);

  if (ffmpegaudenc->opened) {
    GST_WARNING_OBJECT (ffmpegaudenc,
        "Can't change properties once encoder is setup !");
    return;
  }

  switch (prop_id) {
    default:
      if (!gst_ffmpeg_cfg_set_property (ffmpegaudenc->refcontext, value, pspec))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ffmpegaudenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegAudEnc *ffmpegaudenc;

  ffmpegaudenc = (GstFFMpegAudEnc *) (object);

  switch (prop_id) {
    default:
      if (!gst_ffmpeg_cfg_get_property (ffmpegaudenc->refcontext, value, pspec))
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
  void *i = 0;


  GST_LOG ("Registering encoders");

  while ((in_plugin = (AVCodec *) av_codec_iterate (&i))) {
    gchar *type_name;
    guint rank;

    /* Skip non-AV codecs */
    if (in_plugin->type != AVMEDIA_TYPE_AUDIO)
      continue;

    /* no quasi codecs, please */
    if (in_plugin->id == AV_CODEC_ID_PCM_S16LE_PLANAR ||
        (in_plugin->id >= AV_CODEC_ID_PCM_S16LE &&
            in_plugin->id <= AV_CODEC_ID_PCM_BLURAY) ||
        (in_plugin->id >= AV_CODEC_ID_PCM_S8_PLANAR &&
            in_plugin->id <= AV_CODEC_ID_PCM_F24LE)) {
      continue;
    }

    /* No encoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library encoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      continue;
    }

    /* only encoders */
    if (!av_codec_is_encoder (in_plugin)) {
      continue;
    }

    /* FIXME : We should have a method to know cheaply whether we have a mapping
     * for the given plugin or not */

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    if (!strcmp (in_plugin->name, "vorbis")
        || !strcmp (in_plugin->name, "flac")) {
      GST_LOG ("Ignoring encoder %s", in_plugin->name);
      continue;
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
  }

  GST_LOG ("Finished registering encoders");

  return TRUE;
}
