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

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"
#include "gstffmpegenc.h"

#define DEFAULT_AUDIO_BITRATE 128000

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BIT_RATE,
  ARG_BUFSIZE,
  ARG_RTP_PAYLOAD_SIZE,
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegaudenc_class_init (GstFFMpegAudEncClass * klass);
static void gst_ffmpegaudenc_base_init (GstFFMpegAudEncClass * klass);
static void gst_ffmpegaudenc_init (GstFFMpegAudEnc * ffmpegaudenc);
static void gst_ffmpegaudenc_finalize (GObject * object);

static gboolean gst_ffmpegaudenc_setcaps (GstFFMpegAudEnc * ffmpegenc,
    GstCaps * caps);
static GstCaps *gst_ffmpegaudenc_getcaps (GstFFMpegAudEnc * ffmpegenc,
    GstCaps * filter);
static GstFlowReturn gst_ffmpegaudenc_chain_audio (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_ffmpegaudenc_query_sink (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_ffmpegaudenc_event_sink (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void gst_ffmpegaudenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegaudenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_ffmpegaudenc_change_state (GstElement * element,
    GstStateChange transition);

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

  klass->in_plugin = in_plugin;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;
  klass->sinkcaps = NULL;

  return;
}

static void
gst_ffmpegaudenc_class_init (GstFFMpegAudEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ffmpegaudenc_set_property;
  gobject_class->get_property = gst_ffmpegaudenc_get_property;

  /* FIXME: could use -1 for a sensible per-codec defaults */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_int ("bitrate", "Bit Rate",
          "Target Audio Bitrate", 0, G_MAXINT, DEFAULT_AUDIO_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_ffmpegaudenc_change_state;

  gobject_class->finalize = gst_ffmpegaudenc_finalize;
}

static void
gst_ffmpegaudenc_init (GstFFMpegAudEnc * ffmpegaudenc)
{
  GstFFMpegAudEncClass *oclass =
      (GstFFMpegAudEncClass *) (G_OBJECT_GET_CLASS (ffmpegaudenc));

  /* setup pads */
  ffmpegaudenc->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_event_function (ffmpegaudenc->sinkpad,
      gst_ffmpegaudenc_event_sink);
  gst_pad_set_query_function (ffmpegaudenc->sinkpad,
      gst_ffmpegaudenc_query_sink);
  gst_pad_set_chain_function (ffmpegaudenc->sinkpad,
      gst_ffmpegaudenc_chain_audio);

  ffmpegaudenc->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (ffmpegaudenc->srcpad);

  /* ffmpeg objects */
  ffmpegaudenc->context = avcodec_alloc_context ();
  ffmpegaudenc->opened = FALSE;

  gst_element_add_pad (GST_ELEMENT (ffmpegaudenc), ffmpegaudenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegaudenc), ffmpegaudenc->srcpad);

  ffmpegaudenc->adapter = gst_adapter_new ();
}

static void
gst_ffmpegaudenc_finalize (GObject * object)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) object;


  /* close old session */
  if (ffmpegaudenc->opened) {
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    ffmpegaudenc->opened = FALSE;
  }

  /* clean up remaining allocated data */
  av_free (ffmpegaudenc->context);

  g_object_unref (ffmpegaudenc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_ffmpegaudenc_getcaps (GstFFMpegAudEnc * ffmpegaudenc, GstCaps * filter)
{
  GstCaps *caps = NULL;

  GST_DEBUG_OBJECT (ffmpegaudenc, "getting caps");

  /* audio needs no special care */
  caps = gst_pad_get_pad_template_caps (ffmpegaudenc->sinkpad);

  if (filter) {
    GstCaps *tmp;
    tmp = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
    caps = tmp;
  }

  GST_DEBUG_OBJECT (ffmpegaudenc,
      "audio caps, return template %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_ffmpegaudenc_setcaps (GstFFMpegAudEnc * ffmpegaudenc, GstCaps * caps)
{
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  GstFFMpegAudEncClass *oclass =
      (GstFFMpegAudEncClass *) G_OBJECT_GET_CLASS (ffmpegaudenc);

  /* close old session */
  if (ffmpegaudenc->opened) {
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    ffmpegaudenc->opened = FALSE;
  }

  /* set defaults */
  avcodec_get_context_defaults (ffmpegaudenc->context);

  /* if we set it in _getcaps we should set it also in _link */
  ffmpegaudenc->context->strict_std_compliance = -1;

  /* user defined properties */
  if (ffmpegaudenc->bitrate > 0) {
    GST_INFO_OBJECT (ffmpegaudenc, "Setting avcontext to bitrate %d",
        ffmpegaudenc->bitrate);
    ffmpegaudenc->context->bit_rate = ffmpegaudenc->bitrate;
    ffmpegaudenc->context->bit_rate_tolerance = ffmpegaudenc->bitrate;
  } else {
    GST_INFO_OBJECT (ffmpegaudenc, "Using avcontext default bitrate %d",
        ffmpegaudenc->context->bit_rate);
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
  ffmpegaudenc->context->inter_threshold = 0;


  /* fetch pix_fmt and so on */
  gst_ffmpeg_caps_with_codectype (oclass->in_plugin->type,
      caps, ffmpegaudenc->context);
  if (!ffmpegaudenc->context->time_base.den) {
    ffmpegaudenc->context->time_base.den = 25;
    ffmpegaudenc->context->time_base.num = 1;
    ffmpegaudenc->context->ticks_per_frame = 1;
  }

  /* open codec */
  if (gst_ffmpeg_avcodec_open (ffmpegaudenc->context, oclass->in_plugin) < 0) {
    if (ffmpegaudenc->context->priv_data)
      gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    if (ffmpegaudenc->context->stats_in)
      g_free (ffmpegaudenc->context->stats_in);
    GST_DEBUG_OBJECT (ffmpegaudenc, "avenc_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    return FALSE;
  }

  /* second pass stats buffer no longer needed */
  if (ffmpegaudenc->context->stats_in)
    g_free (ffmpegaudenc->context->stats_in);

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (ffmpegaudenc, "picking an output format ...");
  allowed_caps = gst_pad_get_allowed_caps (ffmpegaudenc->srcpad);
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ffmpegaudenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps = gst_pad_get_pad_template_caps (ffmpegaudenc->srcpad);
  }
  GST_DEBUG_OBJECT (ffmpegaudenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, allowed_caps, ffmpegaudenc->context);

  /* try to set this caps on the other side */
  other_caps = gst_ffmpeg_codecid_to_caps (oclass->in_plugin->id,
      ffmpegaudenc->context, TRUE);

  if (!other_caps) {
    gst_caps_unref (allowed_caps);
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    GST_DEBUG ("Unsupported codec - no caps found");
    return FALSE;
  }

  icaps = gst_caps_intersect (allowed_caps, other_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (other_caps);
  if (gst_caps_is_empty (icaps)) {
    gst_caps_unref (icaps);
    return FALSE;
  }

  if (gst_caps_get_size (icaps) > 1) {
    GstCaps *newcaps;

    newcaps =
        gst_caps_new_full (gst_structure_copy (gst_caps_get_structure (icaps,
                0)), NULL);
    gst_caps_unref (icaps);
    icaps = newcaps;
  }

  if (!gst_pad_set_caps (ffmpegaudenc->srcpad, icaps)) {
    gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
    gst_caps_unref (icaps);
    return FALSE;
  }
  gst_caps_unref (icaps);

  /* success! */
  ffmpegaudenc->opened = TRUE;

  return TRUE;
}


static GstFlowReturn
gst_ffmpegaudenc_encode_audio (GstFFMpegAudEnc * ffmpegaudenc,
    guint8 * audio_in, guint in_size, guint max_size, GstClockTime timestamp,
    GstClockTime duration, gboolean discont)
{
  GstBuffer *outbuf;
  AVCodecContext *ctx;
  GstMapInfo map;
  gint res;
  GstFlowReturn ret;

  ctx = ffmpegaudenc->context;

  /* We need to provide at least ffmpegs minimal buffer size */
  outbuf = gst_buffer_new_and_alloc (max_size + FF_MIN_BUFFER_SIZE);
  gst_buffer_map (outbuf, &map, GST_MAP_WRITE);

  GST_LOG_OBJECT (ffmpegaudenc, "encoding buffer of max size %d", max_size);
  if (ffmpegaudenc->buffer_size != max_size)
    ffmpegaudenc->buffer_size = max_size;

  res = avcodec_encode_audio (ctx, map.data, max_size, (short *) audio_in);

  if (res < 0) {
    gst_buffer_unmap (outbuf, &map);
    GST_ERROR_OBJECT (ffmpegaudenc, "Failed to encode buffer: %d", res);
    gst_buffer_unref (outbuf);
    return GST_FLOW_OK;
  }
  GST_LOG_OBJECT (ffmpegaudenc, "got output size %d", res);
  gst_buffer_unmap (outbuf, &map);
  gst_buffer_resize (outbuf, 0, res);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;
  if (discont)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  GST_LOG_OBJECT (ffmpegaudenc, "pushing size %d, timestamp %" GST_TIME_FORMAT,
      res, GST_TIME_ARGS (timestamp));

  ret = gst_pad_push (ffmpegaudenc->srcpad, outbuf);

  return ret;
}

static GstFlowReturn
gst_ffmpegaudenc_chain_audio (GstPad * pad, GstObject * parent,
    GstBuffer * inbuf)
{
  GstFFMpegAudEnc *ffmpegaudenc;
  GstFFMpegAudEncClass *oclass;
  AVCodecContext *ctx;
  GstClockTime timestamp, duration;
  gsize size, frame_size;
  gint osize;
  GstFlowReturn ret;
  gint out_size;
  gboolean discont;
  guint8 *in_data;

  ffmpegaudenc = (GstFFMpegAudEnc *) parent;
  oclass = (GstFFMpegAudEncClass *) G_OBJECT_GET_CLASS (ffmpegaudenc);

  if (G_UNLIKELY (!ffmpegaudenc->opened))
    goto not_negotiated;

  ctx = ffmpegaudenc->context;

  size = gst_buffer_get_size (inbuf);
  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);
  discont = GST_BUFFER_IS_DISCONT (inbuf);

  GST_DEBUG_OBJECT (ffmpegaudenc,
      "Received time %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT, GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (duration), size);

  frame_size = ctx->frame_size;
  osize = av_get_bits_per_sample_format (ctx->sample_fmt) / 8;

  if (frame_size > 1) {
    /* we have a frame_size, feed the encoder multiples of this frame size */
    guint avail, frame_bytes;

    if (discont) {
      GST_LOG_OBJECT (ffmpegaudenc, "DISCONT, clear adapter");
      gst_adapter_clear (ffmpegaudenc->adapter);
      ffmpegaudenc->discont = TRUE;
    }

    if (gst_adapter_available (ffmpegaudenc->adapter) == 0) {
      /* lock on to new timestamp */
      GST_LOG_OBJECT (ffmpegaudenc, "taking buffer timestamp %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));
      ffmpegaudenc->adapter_ts = timestamp;
      ffmpegaudenc->adapter_consumed = 0;
    } else {
      GstClockTime upstream_time;
      GstClockTime consumed_time;
      guint64 bytes;

      /* use timestamp at head of the adapter */
      consumed_time =
          gst_util_uint64_scale (ffmpegaudenc->adapter_consumed, GST_SECOND,
          ctx->sample_rate);
      timestamp = ffmpegaudenc->adapter_ts + consumed_time;
      GST_LOG_OBJECT (ffmpegaudenc, "taking adapter timestamp %" GST_TIME_FORMAT
          " and adding consumed time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (ffmpegaudenc->adapter_ts),
          GST_TIME_ARGS (consumed_time));

      /* check with upstream timestamps, if too much deviation,
       * forego some timestamp perfection in favour of upstream syncing
       * (particularly in case these do not happen to come in multiple
       * of frame size) */
      upstream_time =
          gst_adapter_prev_timestamp (ffmpegaudenc->adapter, &bytes);
      if (GST_CLOCK_TIME_IS_VALID (upstream_time)) {
        GstClockTimeDiff diff;

        upstream_time +=
            gst_util_uint64_scale (bytes, GST_SECOND,
            ctx->sample_rate * osize * ctx->channels);
        diff = upstream_time - timestamp;
        /* relaxed difference, rather than half a sample or so ... */
        if (diff > GST_SECOND / 10 || diff < -GST_SECOND / 10) {
          GST_DEBUG_OBJECT (ffmpegaudenc, "adapter timestamp drifting, "
              "taking upstream timestamp %" GST_TIME_FORMAT,
              GST_TIME_ARGS (upstream_time));
          timestamp = upstream_time;
          /* samples corresponding to bytes */
          ffmpegaudenc->adapter_consumed = bytes / (osize * ctx->channels);
          ffmpegaudenc->adapter_ts = upstream_time -
              gst_util_uint64_scale (ffmpegaudenc->adapter_consumed, GST_SECOND,
              ctx->sample_rate);
          ffmpegaudenc->discont = TRUE;
        }
      }
    }

    GST_LOG_OBJECT (ffmpegaudenc, "pushing buffer in adapter");
    gst_adapter_push (ffmpegaudenc->adapter, inbuf);

    /* first see how many bytes we need to feed to the decoder. */
    frame_bytes = frame_size * osize * ctx->channels;
    avail = gst_adapter_available (ffmpegaudenc->adapter);

    GST_LOG_OBJECT (ffmpegaudenc, "frame_bytes %u, avail %u", frame_bytes,
        avail);

    /* while there is more than a frame size in the adapter, consume it */
    while (avail >= frame_bytes) {
      GST_LOG_OBJECT (ffmpegaudenc, "taking %u bytes from the adapter",
          frame_bytes);

      /* Note that we take frame_bytes and add frame_size.
       * Makes sense when resyncing because you don't have to count channels
       * or samplesize to divide by the samplerate */

      /* take an audio buffer out of the adapter */
      in_data = (guint8 *) gst_adapter_map (ffmpegaudenc->adapter, frame_bytes);
      ffmpegaudenc->adapter_consumed += frame_size;

      /* calculate timestamp and duration relative to start of adapter and to
       * the amount of samples we consumed */
      duration =
          gst_util_uint64_scale (ffmpegaudenc->adapter_consumed, GST_SECOND,
          ctx->sample_rate);
      duration -= (timestamp - ffmpegaudenc->adapter_ts);

      /* 4 times the input size should be big enough... */
      out_size = frame_bytes * 4;

      ret =
          gst_ffmpegaudenc_encode_audio (ffmpegaudenc, in_data, frame_bytes,
          out_size, timestamp, duration, ffmpegaudenc->discont);

      gst_adapter_unmap (ffmpegaudenc->adapter);
      gst_adapter_flush (ffmpegaudenc->adapter, frame_bytes);

      if (ret != GST_FLOW_OK)
        goto push_failed;

      /* advance the adapter timestamp with the duration */
      timestamp += duration;

      ffmpegaudenc->discont = FALSE;
      avail = gst_adapter_available (ffmpegaudenc->adapter);
    }
    GST_LOG_OBJECT (ffmpegaudenc, "%u bytes left in the adapter", avail);
  } else {
    GstMapInfo map;
    /* we have no frame_size, feed the encoder all the data and expect a fixed
     * output size */
    int coded_bps = av_get_bits_per_sample (oclass->in_plugin->id);

    GST_LOG_OBJECT (ffmpegaudenc, "coded bps %d, osize %d", coded_bps, osize);

    out_size = size / osize;
    if (coded_bps)
      out_size = (out_size * coded_bps) / 8;

    gst_buffer_map (inbuf, &map, GST_MAP_READ);
    in_data = map.data;
    size = map.size;
    ret = gst_ffmpegaudenc_encode_audio (ffmpegaudenc, in_data, size, out_size,
        timestamp, duration, discont);
    gst_buffer_unmap (inbuf, &map);
    gst_buffer_unref (inbuf);

    if (ret != GST_FLOW_OK)
      goto push_failed;
  }

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

static gboolean
gst_ffmpegaudenc_event_sink (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) parent;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_event_parse_caps (event, &caps);
      ret = gst_ffmpegaudenc_setcaps (ffmpegaudenc, caps);
      gst_event_unref (event);
      return ret;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_ffmpegaudenc_query_sink (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) parent;
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_ffmpegaudenc_getcaps (ffmpegaudenc, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
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
    case ARG_BIT_RATE:
      ffmpegaudenc->bitrate = g_value_get_int (value);
      break;
    case ARG_BUFSIZE:
      break;
    case ARG_RTP_PAYLOAD_SIZE:
      ffmpegaudenc->rtp_payload_size = g_value_get_int (value);
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
    case ARG_BIT_RATE:
      g_value_set_int (value, ffmpegaudenc->bitrate);
      break;
      break;
    case ARG_BUFSIZE:
      g_value_set_int (value, ffmpegaudenc->buffer_size);
      break;
    case ARG_RTP_PAYLOAD_SIZE:
      g_value_set_int (value, ffmpegaudenc->rtp_payload_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_ffmpegaudenc_change_state (GstElement * element, GstStateChange transition)
{
  GstFFMpegAudEnc *ffmpegaudenc = (GstFFMpegAudEnc *) element;
  GstStateChangeReturn result;

  switch (transition) {
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (ffmpegaudenc->opened) {
        gst_ffmpeg_avcodec_close (ffmpegaudenc->context);
        ffmpegaudenc->opened = FALSE;
      }
      gst_adapter_clear (ffmpegaudenc->adapter);
      break;
    default:
      break;
  }
  return result;
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

    /* Skip non-AV codecs */
    if (in_plugin->type != AVMEDIA_TYPE_AUDIO)
      goto next;

    /* no quasi codecs, please */
    if ((in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_BLURAY)) {
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
    if (!in_plugin->encode) {
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
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
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

    if (!gst_element_register (plugin, type_name, GST_RANK_SECONDARY, type)) {
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
