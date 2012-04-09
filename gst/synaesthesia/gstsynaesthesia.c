/* GStreamer
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
 *
 * gstsynaesthesia.c: implementation of synaesthesia drawing element
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-synaesthesia
 * @see_also: goom
 *
 * Synaesthesia is an audio visualisation element. It creates glitter and
 * pulsating fog based on the incomming audio signal.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v audiotestsrc ! audioconvert ! synaesthesia ! ximagesink
 * gst-launch -v audiotestsrc ! audioconvert ! synaesthesia ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsynaesthesia.h"

static GstStaticPadTemplate gst_synaesthesia_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
#if G_BYTE_ORDER == G_BIG_ENDIAN
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("xRGB"))
#else
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("BGRx"))
#endif
    );

static GstStaticPadTemplate gst_synaesthesia_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "rate = (int) [ 8000, 96000 ], "
        "channels = (int) 2, "
        "channel-mask = (bitmask) 0x3, " "layout = (string) interleaved")
    );

static void gst_synaesthesia_finalize (GObject * object);
static void gst_synaesthesia_dispose (GObject * object);

static GstFlowReturn gst_synaesthesia_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_synaesthesia_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstStateChangeReturn
gst_synaesthesia_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_synaesthesia_src_negotiate (GstSynaesthesia * synaesthesia);
static gboolean gst_synaesthesia_src_setcaps (GstSynaesthesia * synaesthesia,
    GstCaps * caps);
static gboolean gst_synaesthesia_sink_setcaps (GstSynaesthesia * synaesthesia,
    GstCaps * caps);

#define gst_synaesthesia_parent_class parent_class
G_DEFINE_TYPE (GstSynaesthesia, gst_synaesthesia, GST_TYPE_ELEMENT);

static void
gst_synaesthesia_class_init (GstSynaesthesiaClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_synaesthesia_dispose;
  gobject_class->finalize = gst_synaesthesia_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_synaesthesia_change_state);

  gst_element_class_set_static_metadata (gstelement_class, "Synaesthesia",
      "Visualization",
      "Creates video visualizations of audio input, using stereo and pitch information",
      "Richard Boulton <richard@tartarus.org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_synaesthesia_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_synaesthesia_sink_template));

  synaesthesia_init ();
}

static void
gst_synaesthesia_init (GstSynaesthesia * synaesthesia)
{
  /* create the sink and src pads */
  synaesthesia->sinkpad =
      gst_pad_new_from_static_template (&gst_synaesthesia_sink_template,
      "sink");
  gst_pad_set_chain_function (synaesthesia->sinkpad,
      GST_DEBUG_FUNCPTR (gst_synaesthesia_chain));
  gst_pad_set_event_function (synaesthesia->sinkpad,
      GST_DEBUG_FUNCPTR (gst_synaesthesia_sink_event));
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->sinkpad);

  synaesthesia->srcpad =
      gst_pad_new_from_static_template (&gst_synaesthesia_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->srcpad);

  synaesthesia->adapter = gst_adapter_new ();

  /* reset the initial video state */
  synaesthesia->width = 320;
  synaesthesia->height = 200;
  synaesthesia->fps_n = 25;     /* desired frame rate */
  synaesthesia->fps_d = 1;
  synaesthesia->frame_duration = -1;

  /* reset the initial audio state */
  synaesthesia->rate = GST_AUDIO_DEF_RATE;
  synaesthesia->channels = 2;

  synaesthesia->next_ts = GST_CLOCK_TIME_NONE;

  synaesthesia->si =
      synaesthesia_new (synaesthesia->width, synaesthesia->height);
}

static void
gst_synaesthesia_dispose (GObject * object)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (object);

  if (synaesthesia->adapter) {
    g_object_unref (synaesthesia->adapter);
    synaesthesia->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_synaesthesia_finalize (GObject * object)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (object);

  synaesthesia_close (synaesthesia->si);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_synaesthesia_sink_setcaps (GstSynaesthesia * synaesthesia, GstCaps * caps)
{
  GstStructure *structure;
  gint channels;
  gint rate;
  gboolean res = TRUE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate))
    goto missing_caps_details;

  if (channels != 2)
    goto wrong_channels;

  if (rate <= 0)
    goto wrong_rate;

  synaesthesia->channels = channels;
  synaesthesia->rate = rate;

done:
  return res;

  /* Errors */
missing_caps_details:
  {
    GST_WARNING_OBJECT (synaesthesia, "missing channels or rate in the caps");
    res = FALSE;
    goto done;
  }
wrong_channels:
  {
    GST_WARNING_OBJECT (synaesthesia, "number of channels must be 2, but is %d",
        channels);
    res = FALSE;
    goto done;
  }
wrong_rate:
  {
    GST_WARNING_OBJECT (synaesthesia, "sample rate must be >0, but is %d",
        rate);
    res = FALSE;
    goto done;
  }
}

static gboolean
gst_synaesthesia_src_negotiate (GstSynaesthesia * synaesthesia)
{
  GstCaps *othercaps, *target, *intersect;
  GstStructure *structure;
  GstCaps *templ;
  GstQuery *query;
  GstBufferPool *pool = NULL;
  guint size, min, max;

  templ = gst_pad_get_pad_template_caps (synaesthesia->srcpad);

  GST_DEBUG_OBJECT (synaesthesia, "performing negotiation");

  /* see what the peer can do */
  othercaps = gst_pad_peer_query_caps (synaesthesia->srcpad, NULL);
  if (othercaps) {
    intersect = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);
    gst_caps_unref (templ);

    if (gst_caps_is_empty (intersect))
      goto no_format;

    target = gst_caps_copy_nth (intersect, 0);
    gst_caps_unref (intersect);
  } else {
    target = templ;
  }

  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width",
      synaesthesia->width);
  gst_structure_fixate_field_nearest_int (structure, "height",
      synaesthesia->height);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate",
      synaesthesia->fps_n, synaesthesia->fps_d);

  GST_DEBUG_OBJECT (synaesthesia, "final caps are %" GST_PTR_FORMAT, target);

  gst_synaesthesia_src_setcaps (synaesthesia, target);

  /* try to get a bufferpool now */
  /* find a pool for the negotiated caps now */
  query = gst_query_new_allocation (target, TRUE);

  if (gst_pad_peer_query (synaesthesia->srcpad, query) &&
      gst_query_get_n_allocation_pools (query) > 0) {
    /* we got configuration from our peer, parse them */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
  } else {
    size = synaesthesia->outsize;
    min = max = 0;
  }

  if (pool == NULL) {
    GstStructure *config;

    /* we did not get a pool, make one ourselves then */
    pool = gst_buffer_pool_new ();

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, target, size, min, max);
    gst_buffer_pool_set_config (pool, config);
  }

  if (synaesthesia->pool)
    gst_object_unref (synaesthesia->pool);
  synaesthesia->pool = pool;

  /* and activate */
  gst_buffer_pool_set_active (pool, TRUE);

  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    gst_caps_unref (intersect);
    return FALSE;
  }
}

static gboolean
gst_synaesthesia_src_setcaps (GstSynaesthesia * synaesthesia, GstCaps * caps)
{
  GstStructure *structure;
  gint w, h;
  gint num, denom;
  gboolean res = TRUE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &w) ||
      !gst_structure_get_int (structure, "height", &h) ||
      !gst_structure_get_fraction (structure, "framerate", &num, &denom)) {
    goto missing_caps_details;
  }

  synaesthesia->width = w;
  synaesthesia->height = h;
  synaesthesia->fps_n = num;
  synaesthesia->fps_d = denom;

  synaesthesia_resize (synaesthesia->si, synaesthesia->width,
      synaesthesia->height);

  /* size of the output buffer in bytes, depth is always 4 bytes */
  synaesthesia->outsize = synaesthesia->width * synaesthesia->height * 4;
  synaesthesia->frame_duration = gst_util_uint64_scale_int (GST_SECOND,
      synaesthesia->fps_d, synaesthesia->fps_n);
  synaesthesia->spf = gst_util_uint64_scale_int (synaesthesia->rate,
      synaesthesia->fps_d, synaesthesia->fps_n);

  GST_DEBUG_OBJECT (synaesthesia, "dimension %dx%d, framerate %d/%d, spf %d",
      synaesthesia->width, synaesthesia->height,
      synaesthesia->fps_n, synaesthesia->fps_d, synaesthesia->spf);

  res = gst_pad_push_event (synaesthesia->srcpad, gst_event_new_caps (caps));

done:
  return res;

  /* Errors */
missing_caps_details:
  {
    GST_WARNING_OBJECT (synaesthesia,
        "missing width, height or framerate in the caps");
    res = FALSE;
    goto done;
  }
}

/* make sure we are negotiated */
static GstFlowReturn
ensure_negotiated (GstSynaesthesia * synaesthesia)
{
  gboolean reconfigure;

  reconfigure = gst_pad_check_reconfigure (synaesthesia->srcpad);

  /* we don't know an output format yet, pick one */
  if (reconfigure || !gst_pad_has_current_caps (synaesthesia->srcpad)) {
    if (!gst_synaesthesia_src_negotiate (synaesthesia))
      return GST_FLOW_NOT_NEGOTIATED;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_synaesthesia_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstSynaesthesia *synaesthesia;
  guint32 avail, bytesperread;

  synaesthesia = GST_SYNAESTHESIA (parent);

  GST_LOG_OBJECT (synaesthesia, "chainfunc called");

  if (synaesthesia->rate == 0) {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto exit;
  }

  /* Make sure have an output format */
  ret = ensure_negotiated (synaesthesia);
  if (ret != GST_FLOW_OK) {
    gst_buffer_unref (buffer);
    goto exit;
  }

  /* resync on DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    synaesthesia->next_ts = GST_CLOCK_TIME_NONE;
    gst_adapter_clear (synaesthesia->adapter);
  }

  /* Match timestamps from the incoming audio */
  if (GST_BUFFER_TIMESTAMP (buffer) != GST_CLOCK_TIME_NONE)
    synaesthesia->next_ts = GST_BUFFER_TIMESTAMP (buffer);

  gst_adapter_push (synaesthesia->adapter, buffer);

  /* this is what we want */
  bytesperread =
      MAX (FFT_BUFFER_SIZE,
      synaesthesia->spf) * synaesthesia->channels * sizeof (gint16);

  /* this is what we have */
  avail = gst_adapter_available (synaesthesia->adapter);
  while (avail > bytesperread) {
    const guint16 *data =
        (const guint16 *) gst_adapter_map (synaesthesia->adapter,
        bytesperread);
    GstBuffer *outbuf = NULL;
    guchar *out_frame;
    guint i;

    /* deinterleave */
    for (i = 0; i < FFT_BUFFER_SIZE; i++) {
      synaesthesia->datain[0][i] = *data++;
      synaesthesia->datain[1][i] = *data++;
    }

    /* alloc a buffer */
    GST_DEBUG_OBJECT (synaesthesia, "allocating output buffer");
    ret = gst_buffer_pool_acquire_buffer (synaesthesia->pool, &outbuf, NULL);
    if (ret != GST_FLOW_OK) {
      gst_adapter_unmap (synaesthesia->adapter);
      goto exit;
    }

    GST_BUFFER_TIMESTAMP (outbuf) = synaesthesia->next_ts;
    GST_BUFFER_DURATION (outbuf) = synaesthesia->frame_duration;

    out_frame = (guchar *)
        synaesthesia_update (synaesthesia->si, synaesthesia->datain);
    gst_buffer_fill (outbuf, 0, out_frame, synaesthesia->outsize);

    gst_adapter_unmap (synaesthesia->adapter);

    ret = gst_pad_push (synaesthesia->srcpad, outbuf);
    outbuf = NULL;

    if (ret != GST_FLOW_OK)
      break;

    if (synaesthesia->next_ts != GST_CLOCK_TIME_NONE)
      synaesthesia->next_ts += synaesthesia->frame_duration;

    /* flush sampled for one frame */
    gst_adapter_flush (synaesthesia->adapter, synaesthesia->spf *
        synaesthesia->channels * sizeof (gint16));

    avail = gst_adapter_available (synaesthesia->adapter);
  }

exit:
  return ret;
}

static gboolean
gst_synaesthesia_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_synaesthesia_sink_setcaps (synaesthesia, caps);
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_push_event (synaesthesia->srcpad, event);
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_synaesthesia_change_state (GstElement * element, GstStateChange transition)
{
  GstSynaesthesia *synaesthesia;
  GstStateChangeReturn ret;

  synaesthesia = GST_SYNAESTHESIA (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      synaesthesia->next_ts = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (synaesthesia->adapter);
      synaesthesia->channels = synaesthesia->rate = 0;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (synaesthesia->pool) {
        gst_buffer_pool_set_active (synaesthesia->pool, FALSE);
        gst_object_replace ((GstObject **) & synaesthesia->pool, NULL);
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
  GST_DEBUG_CATEGORY_INIT (synaesthesia_debug, "synaesthesia", 0,
      "synaesthesia audio visualisations");

  return gst_element_register (plugin, "synaesthesia", GST_RANK_NONE,
      GST_TYPE_SYNAESTHESIA);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    synaesthesia,
    "Creates video visualizations of audio input, using stereo and pitch information",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
