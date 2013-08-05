/* gstgoom.c: implementation of goom drawing element
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
 *           (C) <2006> Wim Taymans <wim at fluendo dot com>
 *           (C) <2011> Wim Taymans <wim.taymans at gmail dot com>
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

/**
 * SECTION:element-goom
 * @see_also: synaesthesia
 *
 * Goom is an audio visualisation element. It creates warping structures
 * based on the incoming audio signal.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v audiotestsrc ! goom ! videoconvert ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include "gstgoom.h"
#include <gst/video/video.h>
#include <gst/audio/audio.h>
#include "goom.h"

#if HAVE_ORC
#include <orc/orc.h>
#endif

GST_DEBUG_CATEGORY (goom_debug);
#define GST_CAT_DEFAULT goom_debug

#define DEFAULT_WIDTH  320
#define DEFAULT_HEIGHT 240
#define DEFAULT_FPS_N  25
#define DEFAULT_FPS_D  1

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
#if G_BYTE_ORDER == G_BIG_ENDIAN
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("xRGB"))
#else
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("BGRx"))
#endif
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",    /* the name of the pads */
    GST_PAD_SINK,               /* type of the pad */
    GST_PAD_ALWAYS,             /* ALWAYS/SOMETIMES */
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "rate = (int) [ 8000, 96000 ], "
        "channels = (int) 1, "
        "layout = (string) interleaved; "
        "audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "rate = (int) [ 8000, 96000 ], "
        "channels = (int) 2, "
        "channel-mask = (bitmask) 0x3, " "layout = (string) interleaved")
    );


static void gst_goom_finalize (GObject * object);

static GstStateChangeReturn gst_goom_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_goom_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_goom_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_goom_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_goom_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static gboolean gst_goom_src_negotiate (GstGoom * goom);

#define gst_goom_parent_class parent_class
G_DEFINE_TYPE (GstGoom, gst_goom, GST_TYPE_ELEMENT);

static void
gst_goom_class_init (GstGoomClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_goom_finalize;

  gst_element_class_set_static_metadata (gstelement_class, "GOOM: what a GOOM!",
      "Visualization",
      "Takes frames of data and outputs video frames using the GOOM filter",
      "Wim Taymans <wim@fluendo.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_goom_change_state);
}

static void
gst_goom_init (GstGoom * goom)
{
  /* create the sink and src pads */
  goom->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_chain_function (goom->sinkpad,
      GST_DEBUG_FUNCPTR (gst_goom_chain));
  gst_pad_set_event_function (goom->sinkpad,
      GST_DEBUG_FUNCPTR (gst_goom_sink_event));
  gst_element_add_pad (GST_ELEMENT (goom), goom->sinkpad);

  goom->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_event_function (goom->srcpad,
      GST_DEBUG_FUNCPTR (gst_goom_src_event));
  gst_pad_set_query_function (goom->srcpad,
      GST_DEBUG_FUNCPTR (gst_goom_src_query));
  gst_element_add_pad (GST_ELEMENT (goom), goom->srcpad);

  goom->adapter = gst_adapter_new ();

  goom->width = DEFAULT_WIDTH;
  goom->height = DEFAULT_HEIGHT;
  goom->fps_n = DEFAULT_FPS_N;  /* desired frame rate */
  goom->fps_d = DEFAULT_FPS_D;  /* desired frame rate */
  goom->channels = 0;
  goom->rate = 0;
  goom->duration = 0;

  goom->plugin = goom_init (goom->width, goom->height);
}

static void
gst_goom_finalize (GObject * object)
{
  GstGoom *goom = GST_GOOM (object);

  goom_close (goom->plugin);
  goom->plugin = NULL;

  g_object_unref (goom->adapter);
  if (goom->pool)
    gst_object_unref (goom->pool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_goom_reset (GstGoom * goom)
{
  gst_adapter_clear (goom->adapter);
  gst_segment_init (&goom->segment, GST_FORMAT_UNDEFINED);

  GST_OBJECT_LOCK (goom);
  goom->proportion = 1.0;
  goom->earliest_time = -1;
  GST_OBJECT_UNLOCK (goom);
}

static gboolean
gst_goom_sink_setcaps (GstGoom * goom, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "channels", &goom->channels);
  gst_structure_get_int (structure, "rate", &goom->rate);

  goom->bps = goom->channels * sizeof (gint16);

  return gst_goom_src_negotiate (goom);
}

static gboolean
gst_goom_src_setcaps (GstGoom * goom, GstCaps * caps)
{
  GstStructure *structure;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "width", &goom->width) ||
      !gst_structure_get_int (structure, "height", &goom->height) ||
      !gst_structure_get_fraction (structure, "framerate", &goom->fps_n,
          &goom->fps_d))
    goto error;

  goom_set_resolution (goom->plugin, goom->width, goom->height);

  /* size of the output buffer in bytes, depth is always 4 bytes */
  goom->outsize = goom->width * goom->height * 4;
  goom->duration =
      gst_util_uint64_scale_int (GST_SECOND, goom->fps_d, goom->fps_n);
  goom->spf = gst_util_uint64_scale_int (goom->rate, goom->fps_d, goom->fps_n);
  goom->bpf = goom->spf * goom->bps;

  GST_DEBUG_OBJECT (goom, "dimension %dx%d, framerate %d/%d, spf %d",
      goom->width, goom->height, goom->fps_n, goom->fps_d, goom->spf);

  res = gst_pad_set_caps (goom->srcpad, caps);

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (goom, "error parsing caps");
    return FALSE;
  }
}

static gboolean
gst_goom_src_negotiate (GstGoom * goom)
{
  GstCaps *othercaps, *target;
  GstStructure *structure;
  GstCaps *templ;
  GstQuery *query;
  GstBufferPool *pool;
  GstStructure *config;
  guint size, min, max;

  templ = gst_pad_get_pad_template_caps (goom->srcpad);

  GST_DEBUG_OBJECT (goom, "performing negotiation");

  /* see what the peer can do */
  othercaps = gst_pad_peer_query_caps (goom->srcpad, NULL);
  if (othercaps) {
    target = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);
    gst_caps_unref (templ);

    if (gst_caps_is_empty (target))
      goto no_format;

    target = gst_caps_truncate (target);
  } else {
    target = templ;
  }

  target = gst_caps_make_writable (target);
  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_WIDTH);
  gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_HEIGHT);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate",
      DEFAULT_FPS_N, DEFAULT_FPS_D);

  gst_goom_src_setcaps (goom, target);

  /* try to get a bufferpool now */
  /* find a pool for the negotiated caps now */
  query = gst_query_new_allocation (target, TRUE);

  if (!gst_pad_peer_query (goom->srcpad, query)) {
    /* no problem, we use the query defaults */
    GST_DEBUG_OBJECT (goom, "ALLOCATION query failed");
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* we got configuration from our peer, parse them */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
  } else {
    pool = NULL;
    size = goom->outsize;
    min = max = 0;
  }

  if (pool == NULL) {
    /* we did not get a pool, make one ourselves then */
    pool = gst_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, target, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  if (goom->pool) {
    gst_buffer_pool_set_active (goom->pool, FALSE);
    gst_object_unref (goom->pool);
  }
  goom->pool = pool;

  /* and activate */
  gst_buffer_pool_set_active (pool, TRUE);

  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    gst_caps_unref (target);
    return FALSE;
  }
}

static gboolean
gst_goom_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstGoom *goom;

  goom = GST_GOOM (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, NULL, &proportion, &diff, &timestamp);

      /* save stuff for the _chain() function */
      GST_OBJECT_LOCK (goom);
      goom->proportion = proportion;
      if (diff >= 0)
        /* we're late, this is a good estimate for next displayable
         * frame (see part-qos.txt) */
        goom->earliest_time = timestamp + 2 * diff + goom->duration;
      else
        goom->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (goom);

      res = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_goom_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstGoom *goom;

  goom = GST_GOOM (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_goom_sink_setcaps (goom, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_goom_reset (goom);
      res = gst_pad_event_default (pad, parent, event);
      break;
    case GST_EVENT_SEGMENT:
    {
      /* the newsegment values are used to clip the input samples
       * and to convert the incomming timestamps to running time so
       * we can do QoS */
      gst_event_copy_segment (event, &goom->segment);

      res = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_goom_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;
  GstGoom *goom;

  goom = GST_GOOM (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      /* We need to send the query upstream and add the returned latency to our
       * own */
      GstClockTime min_latency, max_latency;
      gboolean us_live;
      GstClockTime our_latency;
      guint max_samples;

      if (goom->rate == 0)
        break;

      if ((res = gst_pad_peer_query (goom->sinkpad, query))) {
        gst_query_parse_latency (query, &us_live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (goom, "Peer latency: min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        /* the max samples we must buffer buffer */
        max_samples = MAX (GOOM_SAMPLES, goom->spf);
        our_latency =
            gst_util_uint64_scale_int (max_samples, GST_SECOND, goom->rate);

        GST_DEBUG_OBJECT (goom, "Our latency: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (our_latency));

        /* we add some latency but only if we need to buffer more than what
         * upstream gives us */
        min_latency += our_latency;
        if (max_latency != -1)
          max_latency += our_latency;

        GST_DEBUG_OBJECT (goom, "Calculated total latency : min %"
            GST_TIME_FORMAT " max %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

        gst_query_set_latency (query, TRUE, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

/* make sure we are negotiated */
static GstFlowReturn
ensure_negotiated (GstGoom * goom)
{
  if (gst_pad_check_reconfigure (goom->srcpad)) {
    if (!gst_goom_src_negotiate (goom))
      return GST_FLOW_NOT_NEGOTIATED;
  }
  return GST_FLOW_OK;
}


static GstFlowReturn
gst_goom_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstGoom *goom;
  GstFlowReturn ret;
  GstBuffer *outbuf = NULL;

  goom = GST_GOOM (parent);
  if (goom->bps == 0) {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }

  /* Make sure have an output format */
  ret = ensure_negotiated (goom);
  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (buffer);
    goto beach;
  }

  /* don't try to combine samples from discont buffer */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    gst_adapter_clear (goom->adapter);
  }

  GST_DEBUG_OBJECT (goom,
      "Input buffer has %" G_GSIZE_FORMAT " samples, time=%" G_GUINT64_FORMAT,
      gst_buffer_get_size (buffer) / goom->bps, GST_BUFFER_TIMESTAMP (buffer));

  /* Collect samples until we have enough for an output frame */
  gst_adapter_push (goom->adapter, buffer);

  ret = GST_FLOW_OK;

  while (TRUE) {
    const guint16 *data;
    guchar *out_frame;
    gint i;
    guint avail, to_flush;
    guint64 dist, timestamp;

    avail = gst_adapter_available (goom->adapter);
    GST_DEBUG_OBJECT (goom, "avail now %u", avail);

    /* we need GOOM_SAMPLES to get a meaningful result from goom. */
    if (avail < (GOOM_SAMPLES * goom->bps))
      break;

    /* we also need enough samples to produce one frame at least */
    if (avail < goom->bpf)
      break;

    GST_DEBUG_OBJECT (goom, "processing buffer");

    /* get timestamp of the current adapter byte */
    timestamp = gst_adapter_prev_pts (goom->adapter, &dist);
    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* convert bytes to time */
      dist /= goom->bps;
      timestamp += gst_util_uint64_scale_int (dist, GST_SECOND, goom->rate);
    }

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      gint64 qostime;
      gboolean need_skip;

      qostime = gst_segment_to_running_time (&goom->segment, GST_FORMAT_TIME,
          timestamp) + goom->duration;

      GST_OBJECT_LOCK (goom);
      /* check for QoS, don't compute buffers that are known to be late */
      need_skip = goom->earliest_time != -1 && qostime <= goom->earliest_time;
      GST_OBJECT_UNLOCK (goom);

      if (need_skip) {
        GST_WARNING_OBJECT (goom,
            "QoS: skip ts: %" GST_TIME_FORMAT ", earliest: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (qostime), GST_TIME_ARGS (goom->earliest_time));
        goto skip;
      }
    }

    /* get next GOOM_SAMPLES, we have at least this amount of samples */
    data =
        (const guint16 *) gst_adapter_map (goom->adapter,
        GOOM_SAMPLES * goom->bps);

    if (goom->channels == 2) {
      for (i = 0; i < GOOM_SAMPLES; i++) {
        goom->datain[0][i] = *data++;
        goom->datain[1][i] = *data++;
      }
    } else {
      for (i = 0; i < GOOM_SAMPLES; i++) {
        goom->datain[0][i] = *data;
        goom->datain[1][i] = *data++;
      }
    }

    /* alloc a buffer if we don't have one yet, this happens
     * when we pushed a buffer in this while loop before */
    if (outbuf == NULL) {
      GST_DEBUG_OBJECT (goom, "allocating output buffer");
      ret = gst_buffer_pool_acquire_buffer (goom->pool, &outbuf, NULL);
      if (ret != GST_FLOW_OK) {
        gst_adapter_unmap (goom->adapter);
        goto beach;
      }
    }

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    GST_BUFFER_DURATION (outbuf) = goom->duration;

    out_frame = (guchar *) goom_update (goom->plugin, goom->datain, 0, 0);
    gst_buffer_fill (outbuf, 0, out_frame, goom->outsize);

    gst_adapter_unmap (goom->adapter);

    GST_DEBUG ("Pushing frame with time=%" GST_TIME_FORMAT ", duration=%"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (goom->duration));

    ret = gst_pad_push (goom->srcpad, outbuf);
    outbuf = NULL;

  skip:
    /* Now flush the samples we needed for this frame, which might be more than
     * the samples we used (GOOM_SAMPLES). */
    to_flush = goom->bpf;

    GST_DEBUG_OBJECT (goom, "finished frame, flushing %u bytes from input",
        to_flush);
    gst_adapter_flush (goom->adapter, to_flush);

    if (ret != GST_FLOW_OK)
      break;
  }

  if (outbuf != NULL)
    gst_buffer_unref (outbuf);

beach:

  return ret;
}

static GstStateChangeReturn
gst_goom_change_state (GstElement * element, GstStateChange transition)
{
  GstGoom *goom = GST_GOOM (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_goom_reset (goom);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (goom->pool) {
        gst_buffer_pool_set_active (goom->pool, FALSE);
        gst_object_replace ((GstObject **) & goom->pool, NULL);
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (goom_debug, "goom", 0, "goom visualisation element");

#if HAVE_ORC
  orc_init ();
#endif

  return gst_element_register (plugin, "goom", GST_RANK_NONE, GST_TYPE_GOOM);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    goom,
    "GOOM visualization filter",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
