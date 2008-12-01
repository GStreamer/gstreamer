/* RTP muxer element for GStreamer
 *
 * gstrtpmux.c:
 *
 * Copyright (C) <2007> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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
 * SECTION:element-rtpmux
 * @short_description: Muxer that takes one or several RTP streams
 * and muxes them to a single rtp stream.
 *
 * <refsect2>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gstrtpmux.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_rtp_mux_debug);
#define GST_CAT_DEFAULT gst_rtp_mux_debug

/* elementfactory information */
static const GstElementDetails gst_rtp_mux_details =
GST_ELEMENT_DETAILS ("RTP muxer",
    "Codec/Muxer",
    "multiplex N rtp streams into one",
    "Zeeshan Ali <first.last@nokia.com>");

enum
{
  ARG_0,
  PROP_CLOCK_RATE,
  PROP_TIMESTAMP_OFFSET,
  PROP_SEQNUM_OFFSET,
  PROP_SEQNUM,
  PROP_SSRC
};

#define DEFAULT_TIMESTAMP_OFFSET -1
#define DEFAULT_SEQNUM_OFFSET    -1
#define DEFAULT_SSRC             -1
#define DEFAULT_CLOCK_RATE        0

typedef struct {
  gboolean have_base;
  guint clock_base;
} GstRTPMuxPadPrivate;

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static void gst_rtp_mux_base_init (gpointer g_class);
static void gst_rtp_mux_class_init (GstRTPMuxClass * klass);
static void gst_rtp_mux_init (GstRTPMux * rtp_mux);

static void gst_rtp_mux_finalize (GObject * object);

static GstPad *gst_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_mux_release_pad (GstElement * element, GstPad *pad);
static GstFlowReturn gst_rtp_mux_chain (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_rtp_mux_setcaps (GstPad *pad, GstCaps *caps);

static GstStateChangeReturn gst_rtp_mux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_rtp_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtp_mux_src_event (GstPad * pad, GstEvent * event);

static GstElementClass *parent_class = NULL;

GType
gst_rtp_mux_get_type (void)
{
  static GType rtp_mux_type = 0;

  if (!rtp_mux_type) {
    static const GTypeInfo rtp_mux_info = {
      sizeof (GstRTPMuxClass),
      gst_rtp_mux_base_init,
      NULL,
      (GClassInitFunc) gst_rtp_mux_class_init,
      NULL,
      NULL,
      sizeof (GstRTPMux),
      0,
      (GInstanceInitFunc) gst_rtp_mux_init,
    };

    rtp_mux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTPMux",
        &rtp_mux_info, 0);
  }
  return rtp_mux_type;
}

static void
gst_rtp_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &gst_rtp_mux_details);
}

static void
gst_rtp_mux_class_init (GstRTPMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_rtp_mux_finalize;
  gobject_class->get_property = gst_rtp_mux_get_property;
  gobject_class->set_property = gst_rtp_mux_set_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLOCK_RATE,
      g_param_spec_uint ("clock-rate", "clockrate",
          "The clock-rate of the RTP streams",
          0, G_MAXUINT, DEFAULT_CLOCK_RATE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (-1 = random)", -1,
          G_MAXINT, DEFAULT_TIMESTAMP_OFFSET, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM_OFFSET,
      g_param_spec_int ("seqnum-offset", "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXINT,
          DEFAULT_SEQNUM_OFFSET, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet",
          0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (-1 == random)",
          0, G_MAXUINT, DEFAULT_SSRC, G_PARAM_READWRITE));

  gstelement_class->request_new_pad = gst_rtp_mux_request_new_pad;
  gstelement_class->release_pad = gst_rtp_mux_release_pad;
  gstelement_class->change_state = gst_rtp_mux_change_state;

  klass->chain_func = gst_rtp_mux_chain;
}

static gboolean gst_rtp_mux_src_event (GstPad * pad,
    GstEvent * event)
{
  GstElement *rtp_mux;
  GstIterator *iter;
  GstPad *sinkpad;
  gboolean result = FALSE;
  gboolean done = FALSE;

  rtp_mux = gst_pad_get_parent_element (pad);
  g_return_val_if_fail (rtp_mux != NULL, FALSE);

  iter = gst_element_iterate_sink_pads (rtp_mux);

  while (!done) {
    switch (gst_iterator_next (iter, (gpointer) &sinkpad)) {
      case GST_ITERATOR_OK:
        gst_event_ref (event);
        result |= gst_pad_push_event (sinkpad, event);
        gst_object_unref (sinkpad);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        result = FALSE;
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (rtp_mux, "Error iterating sinkpads");
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  gst_object_unref (rtp_mux);
  gst_event_unref (event);

  return result;
}

static void
gst_rtp_mux_init (GstRTPMux * rtp_mux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (rtp_mux);

  rtp_mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (rtp_mux->srcpad, gst_rtp_mux_src_event);
  gst_element_add_pad (GST_ELEMENT (rtp_mux), rtp_mux->srcpad);

  rtp_mux->ssrc = DEFAULT_SSRC;
  rtp_mux->ts_offset = DEFAULT_TIMESTAMP_OFFSET;
  rtp_mux->seqnum_offset = DEFAULT_SEQNUM_OFFSET;
  rtp_mux->clock_rate = DEFAULT_CLOCK_RATE;
}

static void
gst_rtp_mux_finalize (GObject * object)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstPad *
gst_rtp_mux_create_sinkpad (GstRTPMux * rtp_mux, GstPadTemplate * templ)
{
  GstPad *newpad = NULL;
  GstPadTemplate * class_templ;

  class_templ = gst_element_class_get_pad_template (
          GST_ELEMENT_GET_CLASS (rtp_mux), "sink_%d");

  if (templ == class_templ) {
    gchar *name;

    /* create new pad with the name */
    name = g_strdup_printf ("sink_%02d", rtp_mux->numpads);
    newpad = gst_pad_new_from_template (templ, name);
    g_free (name);

    rtp_mux->numpads++;
  } else {
    GST_WARNING_OBJECT (rtp_mux, "this is not our template!\n");
  }

  return newpad;
}

static void
gst_rtp_mux_setup_sinkpad (GstRTPMux * rtp_mux, GstPad * sinkpad)
{
  GstRTPMuxClass *klass;
  GstRTPMuxPadPrivate *padpriv = g_slice_new0 (GstRTPMuxPadPrivate);

  klass = GST_RTP_MUX_GET_CLASS (rtp_mux);

  /* setup some pad functions */
  gst_pad_set_setcaps_function (sinkpad, gst_rtp_mux_setcaps);
  if (klass->chain_func)
    gst_pad_set_chain_function (sinkpad, klass->chain_func);
  if (klass->sink_event_func)
    gst_pad_set_event_function (sinkpad, klass->sink_event_func);

  /* This could break with gstreamer 0.10.9 */
  gst_pad_set_active (sinkpad, TRUE);

  gst_pad_set_element_private (sinkpad, padpriv);

  /* dd the pad to the element */
  gst_element_add_pad (GST_ELEMENT (rtp_mux), sinkpad);
}

static GstPad *
gst_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstRTPMux *rtp_mux;
  GstPad *newpad;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_MUX (element), NULL);

  rtp_mux = GST_RTP_MUX (element);

  if (templ->direction != GST_PAD_SINK) {
    GST_WARNING_OBJECT (rtp_mux, "request pad that is not a SINK pad");
    return NULL;
  }

  newpad = gst_rtp_mux_create_sinkpad (rtp_mux, templ);
  if (newpad)
    gst_rtp_mux_setup_sinkpad (rtp_mux, newpad);
  else
    GST_WARNING_OBJECT (rtp_mux, "failed to create request pad");

  return newpad;
}

static void
gst_rtp_mux_release_pad (GstElement * element, GstPad *pad)
{
  GstRTPMuxPadPrivate *padpriv = gst_pad_get_element_private (pad);

  if (padpriv)
    g_slice_free (GstRTPMuxPadPrivate, padpriv);
  gst_pad_set_element_private (pad, NULL);

  gst_element_remove_pad (element, pad);
}

/* Put our own clock-base on the buffer */
static void
gst_rtp_mux_readjust_rtp_timestamp (GstRTPMux * rtp_mux, GstPad * pad,
    GstBuffer * buffer)
{
  guint32 ts;
  guint32 sink_ts_base = 0;
  GstRTPMuxPadPrivate *padpriv = gst_pad_get_element_private (pad);

  if (padpriv->have_base)
    sink_ts_base = padpriv->clock_base;

  ts = gst_rtp_buffer_get_timestamp (buffer) - sink_ts_base + rtp_mux->ts_base;
  GST_LOG_OBJECT (rtp_mux, "Re-adjusting RTP ts %u to %u",
      gst_rtp_buffer_get_timestamp (buffer), ts);
  gst_rtp_buffer_set_timestamp (buffer, ts);
}

static GstFlowReturn
gst_rtp_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTPMux *rtp_mux;
  GstStructure * structure;
  GstFlowReturn ret;

  rtp_mux = GST_RTP_MUX (gst_pad_get_parent (pad));

  buffer = gst_buffer_make_writable(buffer);

  rtp_mux->seqnum++;
  gst_rtp_buffer_set_seq (buffer, rtp_mux->seqnum);
  GST_BUFFER_CAPS (buffer) = gst_caps_make_writable(GST_BUFFER_CAPS (buffer));
  structure = gst_caps_get_structure (GST_BUFFER_CAPS (buffer), 0U);
  gst_structure_set (structure, "seqnum-base", G_TYPE_UINT, rtp_mux->seqnum_base, NULL);
  gst_rtp_buffer_set_ssrc (buffer, rtp_mux->current_ssrc);
  gst_rtp_mux_readjust_rtp_timestamp (rtp_mux, pad, buffer);
  GST_LOG_OBJECT (rtp_mux, "Pushing packet size %d, seq=%d, ts=%u",
      GST_BUFFER_SIZE (buffer), rtp_mux->seqnum);

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (rtp_mux->srcpad));

  ret = gst_pad_push (rtp_mux->srcpad, buffer);

  gst_object_unref (rtp_mux);
  return ret;
}

static gboolean
gst_rtp_mux_set_clock_rate (GstRTPMux *rtp_mux, gint clock_rate)
{
  gint ret = TRUE;

  if (rtp_mux->clock_rate == 0) {
    rtp_mux->clock_rate = clock_rate;
    ret = TRUE;
  }

  else if (rtp_mux->clock_rate != clock_rate) {
    GST_WARNING_OBJECT (rtp_mux, "Clock-rate already set to: %u",
            rtp_mux->clock_rate);
    ret = FALSE;
  }

  return ret;
}

static gboolean
gst_rtp_mux_setcaps (GstPad *pad, GstCaps *caps)
{
  GstRTPMux *rtp_mux;
  GstStructure *structure;
  gboolean ret = TRUE;
  gint clock_rate;
  GstRTPMuxPadPrivate *padpriv = gst_pad_get_element_private (pad);

  rtp_mux = GST_RTP_MUX (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_int (structure, "clock-rate", &clock_rate)) {
    ret = gst_rtp_mux_set_clock_rate (rtp_mux, clock_rate);
  }

  if (!ret)
    goto out;

  if (gst_structure_get_uint (structure, "clock-base", &padpriv->clock_base)) {
    padpriv->have_base = TRUE;
  }

  caps = gst_caps_copy (caps);

  gst_caps_set_simple (caps,
      "clock-base", G_TYPE_UINT, rtp_mux->ts_base,
      "seqnum-base", G_TYPE_UINT, rtp_mux->seqnum_base,
      NULL);

  GST_DEBUG_OBJECT (rtp_mux,
      "setting caps %" GST_PTR_FORMAT " on src pad..", caps);
  ret = gst_pad_set_caps (rtp_mux->srcpad, caps);
  gst_caps_unref (caps);

 out:
  gst_object_unref (rtp_mux);

  return ret;
}

static void
gst_rtp_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  switch (prop_id) {
    case PROP_CLOCK_RATE:
      g_value_set_uint (value, rtp_mux->clock_rate);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int (value, rtp_mux->ts_offset);
      break;
    case PROP_SEQNUM_OFFSET:
      g_value_set_int (value, rtp_mux->seqnum_offset);
      break;
    case PROP_SEQNUM:
      g_value_set_uint (value, rtp_mux->seqnum);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, rtp_mux->ssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  switch (prop_id) {
    case PROP_CLOCK_RATE:
      rtp_mux->clock_rate = g_value_get_uint (value);
      break;
    case PROP_TIMESTAMP_OFFSET:
      rtp_mux->ts_offset = g_value_get_int (value);
      break;
    case PROP_SEQNUM_OFFSET:
      rtp_mux->seqnum_offset = g_value_get_int (value);
      break;
    case PROP_SSRC:
      rtp_mux->ssrc = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mux_ready_to_paused (GstRTPMux * rtp_mux)
{
  if (rtp_mux->ssrc == -1)
    rtp_mux->current_ssrc = g_random_int ();
  else
    rtp_mux->current_ssrc = rtp_mux->ssrc;

  if (rtp_mux->seqnum_offset == -1)
    rtp_mux->seqnum_base = g_random_int_range (0, G_MAXUINT16);
  else
    rtp_mux->seqnum_base = rtp_mux->seqnum_offset;
  rtp_mux->seqnum = rtp_mux->seqnum_base;

  if (rtp_mux->ts_offset == -1)
    rtp_mux->ts_base = g_random_int ();
  else
    rtp_mux->ts_base = rtp_mux->ts_offset;
    GST_DEBUG_OBJECT (rtp_mux, "set clock-base to %u", rtp_mux->ts_base);
}

static GstStateChangeReturn
gst_rtp_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_mux_ready_to_paused (rtp_mux);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

gboolean
gst_rtp_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_mux_debug, "rtpmux", 0,
      "rtp muxer");

  return gst_element_register (plugin, "rtpmux", GST_RANK_NONE,
      GST_TYPE_RTP_MUX);
}

