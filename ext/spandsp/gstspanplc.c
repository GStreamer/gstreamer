/*
 * (C) 2011 Collabora Ltd.
 *  Contact: Youness Alaoui <youness.alaoui@collabora.co.uk>
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

/**
 * SECTION:element-spanplc
 *
 * The spanplc (Packet Loss Concealment) element provides a synthetic
 * fill-in signal, to minimise the audible effect of lost packets in
 * VoIP applications
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstspanplc.h"

GST_BOILERPLATE (GstSpanPlc, gst_span_plc, GstElement, GST_TYPE_ELEMENT);

GST_DEBUG_CATEGORY_STATIC (gst_span_plc_debug);
#define GST_CAT_DEFAULT gst_span_plc_debug


static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, signed = (bool) TRUE, "
        "width = (int) 16, depth = (int) 16, "
        "rate = (int) [ 1, MAX ], channels = (int) 1")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, signed = (bool) TRUE, "
        "width = (int) 16, depth = (int) 16, "
        "rate = (int) [ 1, MAX ], channels = (int) 1")
    );

static void gst_span_plc_dispose (GObject * object);

static GstStateChangeReturn gst_span_plc_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_span_plc_setcaps_sink (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_span_plc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_span_plc_event_sink (GstPad * pad, GstEvent * event);

static void
gst_span_plc_base_init (gpointer gclass)
{
  GstElementClass *element_class = (GstElementClass *) gclass;

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "SpanDSP PLC",
      "Filter/Effect/Audio",
      "Adds packet loss concealment to audio",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");
}

/* initialize the plugin's class */
static void
gst_span_plc_class_init (GstSpanPlcClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_span_plc_dispose;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_span_plc_change_state);

  GST_DEBUG_CATEGORY_INIT (gst_span_plc_debug, "spanplc",
      0, "spanDSP's packet loss concealment");
}

static void
gst_span_plc_init (GstSpanPlc * plc, GstSpanPlcClass * gclass)
{
  GST_DEBUG_OBJECT (plc, "init");

  plc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  plc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");

  gst_pad_set_setcaps_function (plc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_span_plc_setcaps_sink));

  gst_pad_set_getcaps_function (plc->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_getcaps_function (plc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  gst_pad_set_chain_function (plc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_span_plc_chain));

  gst_pad_set_event_function (plc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_span_plc_event_sink));

  gst_element_add_pad (GST_ELEMENT (plc), plc->srcpad);
  gst_element_add_pad (GST_ELEMENT (plc), plc->sinkpad);

  plc->plc_state = NULL;
  plc->last_stop = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (plc, "init complete");
}

static void
gst_span_plc_dispose (GObject * object)
{
  GstSpanPlc *plc = GST_SPAN_PLC (object);

  if (plc->plc_state)
    plc_free (plc->plc_state);
  plc->plc_state = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_span_plc_flush (GstSpanPlc * plc, gboolean renew)
{
  if (plc->plc_state)
    plc_free (plc->plc_state);
  if (renew)
    plc->plc_state = plc_init (NULL);
  else
    plc->plc_state = NULL;
  plc->last_stop = GST_CLOCK_TIME_NONE;
}

static GstStateChangeReturn
gst_span_plc_change_state (GstElement * element, GstStateChange transition)
{
  GstSpanPlc *plc = GST_SPAN_PLC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_span_plc_flush (plc, TRUE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_span_plc_flush (plc, FALSE);
    default:
      break;
  }

  return ret;
}

static gboolean
gst_span_plc_setcaps_sink (GstPad * pad, GstCaps * caps)
{
  GstSpanPlc *plc = GST_SPAN_PLC (gst_pad_get_parent (pad));
  GstStructure *s = NULL;
  gboolean ret = FALSE;

  ret = gst_pad_set_caps (plc->srcpad, caps);
  s = gst_caps_get_structure (caps, 0);
  if (s) {
    gst_structure_get_int (s, "rate", &plc->sample_rate);
    GST_DEBUG_OBJECT (plc, "setcaps: got sample rate : %d", plc->sample_rate);
  }

  gst_span_plc_flush (plc, TRUE);
  gst_object_unref (plc);

  return ret;
}

static GstFlowReturn
gst_span_plc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSpanPlc *plc = GST_SPAN_PLC (GST_PAD_PARENT (pad));
  GstClockTime buffer_duration;

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
    plc->last_stop = GST_BUFFER_TIMESTAMP (buffer);
  else
    GST_WARNING_OBJECT (plc, "Buffer has no timestamp!");

  if (GST_BUFFER_DURATION_IS_VALID (buffer)) {
    buffer_duration = GST_BUFFER_DURATION (buffer);
  } else {
    GST_WARNING_OBJECT (plc, "Buffer has no duration!");
    buffer_duration = (GST_BUFFER_SIZE (buffer) /
        (plc->sample_rate * sizeof (guint16))) * GST_SECOND;
    GST_DEBUG_OBJECT (plc, "Buffer duration : %" GST_TIME_FORMAT,
        GST_TIME_ARGS (buffer_duration));
  }

  plc->last_stop += buffer_duration;

  if (plc->plc_state->missing_samples != 0)
    buffer = gst_buffer_make_writable (buffer);
  plc_rx (plc->plc_state, (int16_t *) GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer) / 2);

  return gst_pad_push (plc->srcpad, buffer);
}

static void
gst_span_plc_send_fillin (GstSpanPlc * plc, GstClockTime duration)
{
  guint buf_size;
  GstBuffer *buffer = NULL;

  buf_size = ((float) duration / GST_SECOND) * plc->sample_rate;
  buf_size *= sizeof (guint16);
  buffer = gst_buffer_new_and_alloc (buf_size);
  GST_DEBUG_OBJECT (plc, "Missing packet of %" GST_TIME_FORMAT
      " == %d bytes", GST_TIME_ARGS (duration), buf_size);
  plc_fillin (plc->plc_state, (int16_t *) GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer) / 2);
  GST_BUFFER_TIMESTAMP (buffer) = plc->last_stop;
  GST_BUFFER_DURATION (buffer) = duration;
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (plc->srcpad));
  gst_pad_push (plc->srcpad, buffer);
}

static gboolean
gst_span_plc_event_sink (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstSpanPlc *plc = GST_SPAN_PLC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (plc, "received event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment (event, &update, &rate, &format, &start,
          &stop, &time);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (update) {
        /* time progressed without data, see if we can fill the gap with
         * some concealment data */
        if (plc->last_stop < start)
          gst_span_plc_send_fillin (plc, start - plc->last_stop);
      }
      plc->last_stop = start;
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_span_plc_flush (plc, TRUE);
      break;
    default:
      break;
  }
  ret = gst_pad_push_event (plc->srcpad, event);

  gst_object_unref (plc);

  return ret;
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (plc, "received non TIME newsegment");
    gst_object_unref (plc);
    return FALSE;
  }
}
