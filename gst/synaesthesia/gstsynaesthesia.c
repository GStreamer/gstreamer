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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB_HOST_ENDIAN)
    );

static GstStaticPadTemplate gst_synaesthesia_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AUDIO_INT_STANDARD_PAD_TEMPLATE_CAPS)
    );

static void gst_synaesthesia_finalize (GObject * object);
static void gst_synaesthesia_dispose (GObject * object);

static GstFlowReturn gst_synaesthesia_chain (GstPad * pad, GstBuffer * buffer);

static GstStateChangeReturn
gst_synaesthesia_change_state (GstElement * element, GstStateChange transition);

static gboolean gst_synaesthesia_src_negotiate (GstSynaesthesia * synaesthesia);
static gboolean gst_synaesthesia_src_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_synaesthesia_sink_setcaps (GstPad * pad, GstCaps * caps);

GST_BOILERPLATE (GstSynaesthesia, gst_synaesthesia, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_synaesthesia_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Synaesthesia",
      "Visualization",
      "Creates video visualizations of audio input, using stereo and pitch information",
      "Richard Boulton <richard@tartarus.org>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_synaesthesia_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_synaesthesia_sink_template));
}

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

  synaesthesia_init ();
}

static void
gst_synaesthesia_init (GstSynaesthesia * synaesthesia,
    GstSynaesthesiaClass * g_class)
{
  /* create the sink and src pads */
  synaesthesia->sinkpad =
      gst_pad_new_from_static_template (&gst_synaesthesia_sink_template,
      "sink");
  gst_pad_set_chain_function (synaesthesia->sinkpad,
      GST_DEBUG_FUNCPTR (gst_synaesthesia_chain));
  gst_pad_set_setcaps_function (synaesthesia->sinkpad,
      GST_DEBUG_FUNCPTR (gst_synaesthesia_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (synaesthesia), synaesthesia->sinkpad);

  synaesthesia->srcpad =
      gst_pad_new_from_static_template (&gst_synaesthesia_src_template, "src");
  gst_pad_set_setcaps_function (synaesthesia->srcpad,
      GST_DEBUG_FUNCPTR (gst_synaesthesia_src_setcaps));
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
gst_synaesthesia_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSynaesthesia *synaesthesia;
  GstStructure *structure;
  gint channels;
  gint rate;
  gboolean res = TRUE;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));
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
  gst_object_unref (synaesthesia);
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
  const GstCaps *templ;

  templ = gst_pad_get_pad_template_caps (synaesthesia->srcpad);

  GST_DEBUG_OBJECT (synaesthesia, "performing negotiation");

  /* see what the peer can do */
  othercaps = gst_pad_peer_get_caps (synaesthesia->srcpad);
  if (othercaps) {
    intersect = gst_caps_intersect (othercaps, templ);
    gst_caps_unref (othercaps);

    if (gst_caps_is_empty (intersect))
      goto no_format;

    target = gst_caps_copy_nth (intersect, 0);
    gst_caps_unref (intersect);
  } else {
    target = gst_caps_ref ((GstCaps *) templ);
  }

  structure = gst_caps_get_structure (target, 0);
  gst_structure_fixate_field_nearest_int (structure, "width",
      synaesthesia->width);
  gst_structure_fixate_field_nearest_int (structure, "height",
      synaesthesia->height);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate",
      synaesthesia->fps_n, synaesthesia->fps_d);

  GST_DEBUG_OBJECT (synaesthesia, "final caps are %" GST_PTR_FORMAT, target);

  gst_pad_set_caps (synaesthesia->srcpad, target);
  gst_caps_unref (target);

  return TRUE;

no_format:
  {
    gst_caps_unref (intersect);
    return FALSE;
  }
}

static gboolean
gst_synaesthesia_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSynaesthesia *synaesthesia;
  GstStructure *structure;
  gint w, h;
  gint num, denom;
  gboolean res = TRUE;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));
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

  synaesthesia->frame_duration = gst_util_uint64_scale_int (GST_SECOND,
      synaesthesia->fps_d, synaesthesia->fps_n);
  synaesthesia->spf = gst_util_uint64_scale_int (synaesthesia->rate,
      synaesthesia->fps_d, synaesthesia->fps_n);

  GST_DEBUG_OBJECT (synaesthesia, "dimension %dx%d, framerate %d/%d, spf %d",
      synaesthesia->width, synaesthesia->height,
      synaesthesia->fps_n, synaesthesia->fps_d, synaesthesia->spf);

done:
  gst_object_unref (synaesthesia);
  return res;

  /* Errors */
missing_caps_details:
  {
    GST_WARNING_OBJECT (synaesthesia, "missing channels or rate in the caps");
    res = FALSE;
    goto done;
  }
}

static GstFlowReturn
gst_synaesthesia_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstSynaesthesia *synaesthesia;
  guint32 avail, bytesperread;

  synaesthesia = GST_SYNAESTHESIA (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (synaesthesia, "chainfunc called");

  /* resync on DISCONT */
  if (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT)) {
    synaesthesia->next_ts = GST_CLOCK_TIME_NONE;
    gst_adapter_clear (synaesthesia->adapter);
  }

  if (GST_PAD_CAPS (synaesthesia->srcpad) == NULL) {
    if (!gst_synaesthesia_src_negotiate (synaesthesia))
      return GST_FLOW_NOT_NEGOTIATED;
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
        (const guint16 *) gst_adapter_peek (synaesthesia->adapter,
        bytesperread);
    GstBuffer *outbuf;
    guchar *out_frame;
    guint i;

    /* deinterleave */
    for (i = 0; i < FFT_BUFFER_SIZE; i++) {
      synaesthesia->datain[0][i] = *data++;
      synaesthesia->datain[1][i] = *data++;
    }

    ret =
        gst_pad_alloc_buffer_and_set_caps (synaesthesia->srcpad,
        GST_BUFFER_OFFSET_NONE,
        synaesthesia->width * synaesthesia->height * 4,
        GST_PAD_CAPS (synaesthesia->srcpad), &outbuf);

    /* no buffer allocated, we don't care why. */
    if (ret != GST_FLOW_OK)
      break;

    GST_BUFFER_TIMESTAMP (outbuf) = synaesthesia->next_ts;
    GST_BUFFER_DURATION (outbuf) = synaesthesia->frame_duration;

    out_frame = (guchar *)
        synaesthesia_update (synaesthesia->si, synaesthesia->datain);
    memcpy (GST_BUFFER_DATA (outbuf), out_frame, GST_BUFFER_SIZE (outbuf));

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

  gst_object_unref (synaesthesia);

  return ret;
}

static GstStateChangeReturn
gst_synaesthesia_change_state (GstElement * element, GstStateChange transition)
{
  GstSynaesthesia *synaesthesia;

  synaesthesia = GST_SYNAESTHESIA (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      synaesthesia->next_ts = GST_CLOCK_TIME_NONE;
      gst_adapter_clear (synaesthesia->adapter);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
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
    "synaesthesia",
    "Creates video visualizations of audio input, using stereo and pitch information",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
