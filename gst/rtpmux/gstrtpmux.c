/* RTP muxer element for GStreamer
 *
 * gstrtpmux.c:
 *
 * Copyright (C) <2007-2010> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2007-2010> Collabora Ltd
 *   Contact: Olivier Crete <olivier.crete@collabora.co.uk>
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
 * @see_also: rtpdtmfmux
 *
 * The rtp muxer takes multiple RTP streams having the same clock-rate and
 * muxes into a single stream with a single SSRC.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch rtpmux name=mux ! udpsink host=127.0.0.1 port=8888        \
 *              alsasrc ! alawenc ! rtppcmapay !                        \
 *              application/x-rtp, payload=8, rate=8000 ! mux.sink_0    \
 *              audiotestsrc is-live=1 !                                \
 *              mulawenc ! rtppcmupay !                                 \
 *              application/x-rtp, payload=0, rate=8000 ! mux.sink_1
 * ]|
 * In this example, an audio stream is captured from ALSA and another is
 * generated, both are encoded into different payload types and muxed together
 * so they can be sent on the same port.
 * </refsect2>
 *
 * Last reviewed on 2010-09-30 (0.10.21)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <string.h>

#include "gstrtpmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_mux_debug);
#define GST_CAT_DEFAULT gst_rtp_mux_debug

enum
{
  ARG_0,
  PROP_TIMESTAMP_OFFSET,
  PROP_SEQNUM_OFFSET,
  PROP_SEQNUM,
  PROP_SSRC
};

#define DEFAULT_TIMESTAMP_OFFSET -1
#define DEFAULT_SEQNUM_OFFSET    -1
#define DEFAULT_SSRC             -1

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

static GstPad *gst_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_mux_release_pad (GstElement * element, GstPad * pad);
static GstFlowReturn gst_rtp_mux_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn gst_rtp_mux_chain_list (GstPad * pad,
    GstBufferList * bufferlist);
static gboolean gst_rtp_mux_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_rtp_mux_getcaps (GstPad * pad);
static gboolean gst_rtp_mux_sink_event (GstPad * pad, GstEvent * event);

static GstStateChangeReturn gst_rtp_mux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_rtp_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_dispose (GObject * object);

static gboolean gst_rtp_mux_src_event_real (GstRTPMux *rtp_mux,
    GstEvent * event);

GST_BOILERPLATE (GstRTPMux, gst_rtp_mux, GstElement, GST_TYPE_ELEMENT);

static void
gst_rtp_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "RTP muxer",
      "Codec/Muxer",
      "multiplex N rtp streams into one", "Zeeshan Ali <first.last@nokia.com>");
}

static void
gst_rtp_mux_class_init (GstRTPMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_rtp_mux_get_property;
  gobject_class->set_property = gst_rtp_mux_set_property;
  gobject_class->dispose = gst_rtp_mux_dispose;

  klass->src_event = gst_rtp_mux_src_event_real;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (-1 = random)", -1,
          G_MAXINT, DEFAULT_TIMESTAMP_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM_OFFSET,
      g_param_spec_int ("seqnum-offset", "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXINT,
          DEFAULT_SEQNUM_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (-1 == random)",
          0, G_MAXUINT, DEFAULT_SSRC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_mux_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_rtp_mux_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rtp_mux_change_state);
}

static void
gst_rtp_mux_dispose (GObject * object)
{
  GList *item;

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);
    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_rtp_mux_src_event (GstPad * pad, GstEvent * event)
{
  GstRTPMux *rtp_mux;
  GstRTPMuxClass *klass;
  gboolean ret = FALSE;

  rtp_mux = (GstRTPMux *) gst_pad_get_parent_element (pad);
  g_return_val_if_fail (rtp_mux != NULL, FALSE);
  klass = GST_RTP_MUX_GET_CLASS (rtp_mux);

  ret = klass->src_event (rtp_mux, event);

  gst_object_unref (rtp_mux);

  return ret;
}

static gboolean
gst_rtp_mux_src_event_real (GstRTPMux *rtp_mux, GstEvent * event)
{
  GstIterator *iter;
  GstPad *sinkpad;
  gboolean result = FALSE;
  gboolean done = FALSE;

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (rtp_mux));

  while (!done) {
    switch (gst_iterator_next (iter, (gpointer) & sinkpad)) {
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
  gst_event_unref (event);

  return result;
}

static void
gst_rtp_mux_init (GstRTPMux * object, GstRTPMuxClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (object);

  object->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (object->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_src_event));
  gst_element_add_pad (GST_ELEMENT (object), object->srcpad);

  object->ssrc = DEFAULT_SSRC;
  object->ts_offset = DEFAULT_TIMESTAMP_OFFSET;
  object->seqnum_offset = DEFAULT_SEQNUM_OFFSET;

  object->segment_pending = TRUE;
  object->last_stop = GST_CLOCK_TIME_NONE;
}

static void
gst_rtp_mux_setup_sinkpad (GstRTPMux * rtp_mux, GstPad * sinkpad)
{
  GstRTPMuxPadPrivate *padpriv = g_slice_new0 (GstRTPMuxPadPrivate);

  /* setup some pad functions */
  gst_pad_set_setcaps_function (sinkpad, gst_rtp_mux_setcaps);
  gst_pad_set_getcaps_function (sinkpad, gst_rtp_mux_getcaps);
  gst_pad_set_chain_function (sinkpad, GST_DEBUG_FUNCPTR (gst_rtp_mux_chain));
  gst_pad_set_chain_list_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_chain_list));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_sink_event));


  gst_segment_init (&padpriv->segment, GST_FORMAT_UNDEFINED);

  gst_pad_set_element_private (sinkpad, padpriv);

  gst_pad_set_active (sinkpad, TRUE);
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

  newpad = gst_pad_new_from_template (templ, req_name);
  if (newpad)
    gst_rtp_mux_setup_sinkpad (rtp_mux, newpad);
  else
    GST_WARNING_OBJECT (rtp_mux, "failed to create request pad");

  return newpad;
}

static void
gst_rtp_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstRTPMuxPadPrivate *padpriv;

  GST_OBJECT_LOCK (element);
  padpriv = gst_pad_get_element_private (pad);
  gst_pad_set_element_private (pad, NULL);
  GST_OBJECT_UNLOCK (element);

  gst_element_remove_pad (element, pad);

  if (padpriv) {
    gst_caps_replace (&padpriv->out_caps, NULL);
    g_slice_free (GstRTPMuxPadPrivate, padpriv);
  }
}

/* Put our own clock-base on the buffer */
static void
gst_rtp_mux_readjust_rtp_timestamp_locked (GstRTPMux * rtp_mux,
    GstRTPMuxPadPrivate * padpriv, GstBuffer * buffer)
{
  guint32 ts;
  guint32 sink_ts_base = 0;

  if (padpriv && padpriv->have_clock_base)
    sink_ts_base = padpriv->clock_base;

  ts = gst_rtp_buffer_get_timestamp (buffer) - sink_ts_base + rtp_mux->ts_base;
  GST_LOG_OBJECT (rtp_mux, "Re-adjusting RTP ts %u to %u",
      gst_rtp_buffer_get_timestamp (buffer), ts);
  gst_rtp_buffer_set_timestamp (buffer, ts);
}

static gboolean
process_buffer_locked (GstRTPMux * rtp_mux, GstRTPMuxPadPrivate * padpriv,
    GstBuffer * buffer)
{
  GstRTPMuxClass *klass = GST_RTP_MUX_GET_CLASS (rtp_mux);

  if (klass->accept_buffer_locked)
    if (!klass->accept_buffer_locked (rtp_mux, padpriv, buffer))
      return FALSE;

  rtp_mux->seqnum++;
  gst_rtp_buffer_set_seq (buffer, rtp_mux->seqnum);

  gst_rtp_buffer_set_ssrc (buffer, rtp_mux->current_ssrc);
  gst_rtp_mux_readjust_rtp_timestamp_locked (rtp_mux, padpriv, buffer);
  GST_LOG_OBJECT (rtp_mux, "Pushing packet size %d, seq=%d, ts=%u",
      GST_BUFFER_SIZE (buffer), rtp_mux->seqnum,
      gst_rtp_buffer_get_timestamp (buffer));

  if (padpriv) {
    gst_buffer_set_caps (buffer, padpriv->out_caps);
    if (padpriv->segment.format == GST_FORMAT_TIME)
      GST_BUFFER_TIMESTAMP (buffer) =
          gst_segment_to_running_time (&padpriv->segment, GST_FORMAT_TIME,
          GST_BUFFER_TIMESTAMP (buffer));
  }

  return TRUE;
}

static GstFlowReturn
gst_rtp_mux_chain_list (GstPad * pad, GstBufferList * bufferlist)
{
  GstRTPMux *rtp_mux;
  GstFlowReturn ret;
  GstBufferListIterator *it;
  GstRTPMuxPadPrivate *padpriv;
  GstEvent *newseg_event = NULL;
  gboolean drop = TRUE;

  rtp_mux = GST_RTP_MUX (gst_pad_get_parent (pad));

  if (!gst_rtp_buffer_list_validate (bufferlist)) {
    GST_ERROR_OBJECT (rtp_mux, "Invalid RTP buffer");
    gst_object_unref (rtp_mux);
    return GST_FLOW_ERROR;
  }

  GST_OBJECT_LOCK (rtp_mux);

  padpriv = gst_pad_get_element_private (pad);
  if (!padpriv) {
    GST_OBJECT_UNLOCK (rtp_mux);
    ret = GST_FLOW_NOT_LINKED;
    gst_buffer_list_unref (bufferlist);
    goto out;
  }

  bufferlist = gst_buffer_list_make_writable (bufferlist);
  it = gst_buffer_list_iterate (bufferlist);
  while (gst_buffer_list_iterator_next_group (it)) {
    GstBuffer *rtpbuf;

    rtpbuf = gst_buffer_list_iterator_next (it);
    rtpbuf = gst_buffer_make_writable (rtpbuf);

    drop = !process_buffer_locked (rtp_mux, padpriv, rtpbuf);

    if (drop)
      break;

    gst_buffer_list_iterator_take (it, rtpbuf);

    do {
      if (GST_BUFFER_DURATION_IS_VALID (rtpbuf) &&
          GST_BUFFER_TIMESTAMP_IS_VALID (rtpbuf))
        rtp_mux->last_stop = GST_BUFFER_TIMESTAMP (rtpbuf) +
            GST_BUFFER_DURATION (rtpbuf);
      else
        rtp_mux->last_stop = GST_CLOCK_TIME_NONE;

      gst_buffer_list_iterator_take (it, rtpbuf);

    } while ((rtpbuf = gst_buffer_list_iterator_next (it)) != NULL);


  }
  gst_buffer_list_iterator_free (it);

  if (!drop && rtp_mux->segment_pending) {
    /*
     * We set the start at 0, because we re-timestamps to the running time
     */
    newseg_event = gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
        GST_FORMAT_TIME, 0, -1, 0);

    rtp_mux->segment_pending = FALSE;
  }

  GST_OBJECT_UNLOCK (rtp_mux);

  if (newseg_event)
    gst_pad_push_event (rtp_mux->srcpad, newseg_event);

  if (drop) {
    gst_buffer_list_unref (bufferlist);
    ret = GST_FLOW_OK;
  } else {
    ret = gst_pad_push_list (rtp_mux->srcpad, bufferlist);
  }

out:

  gst_object_unref (rtp_mux);

  return ret;
}

static GstFlowReturn
gst_rtp_mux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTPMux *rtp_mux;
  GstFlowReturn ret;
  GstRTPMuxPadPrivate *padpriv;
  GstEvent *newseg_event = NULL;
  gboolean drop;

  rtp_mux = GST_RTP_MUX (GST_OBJECT_PARENT (pad));

  if (!gst_rtp_buffer_validate (buffer)) {
    gst_buffer_unref (buffer);
    GST_ERROR_OBJECT (rtp_mux, "Invalid RTP buffer");
    return GST_FLOW_ERROR;
  }

  GST_OBJECT_LOCK (rtp_mux);
  padpriv = gst_pad_get_element_private (pad);

  if (!padpriv) {
    GST_OBJECT_UNLOCK (rtp_mux);
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_LINKED;
  }

  buffer = gst_buffer_make_writable (buffer);

  drop = !process_buffer_locked (rtp_mux, padpriv, buffer);

  if (!drop) {
    if (rtp_mux->segment_pending) {
      /*
       * We set the start at 0, because we re-timestamps to the running time
       */
      newseg_event = gst_event_new_new_segment_full (FALSE, 1.0, 1.0,
          GST_FORMAT_TIME, 0, -1, 0);

      rtp_mux->segment_pending = FALSE;
    }

    if (GST_BUFFER_DURATION_IS_VALID (buffer) &&
        GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      rtp_mux->last_stop = GST_BUFFER_TIMESTAMP (buffer) +
          GST_BUFFER_DURATION (buffer);
    else
      rtp_mux->last_stop = GST_CLOCK_TIME_NONE;
  }

  GST_OBJECT_UNLOCK (rtp_mux);

  if (newseg_event)
    gst_pad_push_event (rtp_mux->srcpad, newseg_event);

  if (drop) {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
  } else {
    ret = gst_pad_push (rtp_mux->srcpad, buffer);
  }

  return ret;
}

static gboolean
gst_rtp_mux_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRTPMux *rtp_mux;
  GstStructure *structure;
  gboolean ret = FALSE;
  GstRTPMuxPadPrivate *padpriv;

  rtp_mux = GST_RTP_MUX (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  if (!structure)
    goto out;

  GST_OBJECT_LOCK (rtp_mux);
  padpriv = gst_pad_get_element_private (pad);
  if (padpriv &&
      gst_structure_get_uint (structure, "clock-base", &padpriv->clock_base)) {
    padpriv->have_clock_base = TRUE;
  }
  GST_OBJECT_UNLOCK (rtp_mux);

  caps = gst_caps_copy (caps);

  gst_caps_set_simple (caps,
      "clock-base", G_TYPE_UINT, rtp_mux->ts_base,
      "seqnum-base", G_TYPE_UINT, rtp_mux->seqnum_base, NULL);

  GST_DEBUG_OBJECT (rtp_mux,
      "setting caps %" GST_PTR_FORMAT " on src pad..", caps);
  ret = gst_pad_set_caps (rtp_mux->srcpad, caps);

  if (rtp_mux->ssrc == -1) {
    if (gst_structure_has_field_typed (structure, "ssrc", G_TYPE_UINT)) {
      rtp_mux->current_ssrc = g_value_get_uint
          (gst_structure_get_value (structure, "ssrc"));
    }
  }

  if (ret) {
    GST_OBJECT_LOCK (rtp_mux);
    padpriv = gst_pad_get_element_private (pad);
    if (padpriv)
      gst_caps_replace (&padpriv->out_caps, caps);
    GST_OBJECT_UNLOCK (rtp_mux);
  }
  gst_caps_unref (caps);

out:
  gst_object_unref (rtp_mux);

  return ret;
}

static void
clear_caps (GstCaps * caps, gboolean only_clock_rate)
{
  gint i, j;

  /* Lets only match on the clock-rate */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    for (j = 0; j < gst_structure_n_fields (s); j++) {
      const gchar *name = gst_structure_nth_field_name (s, j);

      if (strcmp (name, "clock-rate") && (only_clock_rate ||
              (strcmp (name, "ssrc")))) {
        gst_structure_remove_field (s, name);
        j--;
      }
    }
  }
}

static gboolean
same_clock_rate_fold (gpointer item, GValue * ret, gpointer user_data)
{
  GstPad *mypad = user_data;
  GstPad *pad = item;
  GstCaps *peercaps;
  GstCaps *othercaps;
  const GstCaps *accumcaps;
  GstCaps *intersect;

  if (pad == mypad) {
    gst_object_unref (pad);
    return TRUE;
  }

  peercaps = gst_pad_peer_get_caps (pad);
  if (!peercaps) {
    gst_object_unref (pad);
    return TRUE;
  }

  othercaps = gst_caps_intersect (peercaps,
      gst_pad_get_pad_template_caps (pad));
  gst_caps_unref (peercaps);

  accumcaps = gst_value_get_caps (ret);

  clear_caps (othercaps, TRUE);

  intersect = gst_caps_intersect (accumcaps, othercaps);

  g_value_take_boxed (ret, intersect);

  gst_caps_unref (othercaps);
  gst_object_unref (pad);

  return !gst_caps_is_empty (intersect);
}

static GstCaps *
gst_rtp_mux_getcaps (GstPad * pad)
{
  GstRTPMux *mux = GST_RTP_MUX (gst_pad_get_parent (pad));
  GstCaps *caps = NULL;
  GstIterator *iter = NULL;
  GValue v = { 0 };
  GstIteratorResult res;
  GstCaps *peercaps = gst_pad_peer_get_caps (mux->srcpad);
  GstCaps *othercaps = NULL;

  if (peercaps) {
    othercaps = gst_caps_intersect (peercaps,
        gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (peercaps);
  } else {
    othercaps = gst_caps_copy (gst_pad_get_pad_template_caps (mux->srcpad));
  }

  clear_caps (othercaps, FALSE);

  g_value_init (&v, GST_TYPE_CAPS);

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (mux));
  do {
    gst_value_set_caps (&v, othercaps);
    res = gst_iterator_fold (iter, same_clock_rate_fold, &v, pad);
  } while (res == GST_ITERATOR_RESYNC);
  gst_iterator_free (iter);

  caps = (GstCaps *) gst_value_get_caps (&v);

  if (res == GST_ITERATOR_ERROR) {
    gst_caps_unref (caps);
    caps = gst_caps_new_empty ();
  }

  if (othercaps)
    gst_caps_unref (othercaps);
  gst_object_unref (mux);

  return caps;
}


static void
gst_rtp_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int (value, rtp_mux->ts_offset);
      break;
    case PROP_SEQNUM_OFFSET:
      g_value_set_int (value, rtp_mux->seqnum_offset);
      break;
    case PROP_SEQNUM:
      GST_OBJECT_LOCK (rtp_mux);
      g_value_set_uint (value, rtp_mux->seqnum);
      GST_OBJECT_UNLOCK (rtp_mux);
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

static gboolean
gst_rtp_mux_sink_event (GstPad * pad, GstEvent * event)
{

  GstRTPMux *mux;
  gboolean ret = FALSE;
  gboolean forward = TRUE;

  mux = GST_RTP_MUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
    {
      GstRTPMuxPadPrivate *padpriv;

      GST_OBJECT_LOCK (mux);
      mux->last_stop = GST_CLOCK_TIME_NONE;
      mux->segment_pending = TRUE;
      padpriv = gst_pad_get_element_private (pad);
      if (padpriv)
        gst_segment_init (&padpriv->segment, GST_FORMAT_UNDEFINED);
      GST_OBJECT_UNLOCK (mux);
    }
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, position;
      GstRTPMuxPadPrivate *padpriv;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);

      GST_OBJECT_LOCK (mux);
      padpriv = gst_pad_get_element_private (pad);

      if (padpriv) {
        if (format == GST_FORMAT_TIME)
          gst_segment_set_newsegment_full (&padpriv->segment, update,
              rate, applied_rate, format, start, stop, position);
        else
          gst_segment_init (&padpriv->segment, GST_FORMAT_UNDEFINED);
      }
      GST_OBJECT_UNLOCK (mux);
      gst_event_unref (event);
      forward = FALSE;
      ret = TRUE;
      break;
    }
    default:
      break;
  }

  if (forward)
    ret = gst_pad_push_event (mux->srcpad, event);

  gst_object_unref (mux);
  return ret;
}


static void
clear_segment (gpointer data, gpointer user_data)
{
  GstPad *pad = data;
  GstRTPMux *mux = user_data;
  GstRTPMuxPadPrivate *padpriv;

  GST_OBJECT_LOCK (mux);
  padpriv = gst_pad_get_element_private (pad);
  if (padpriv)
    gst_segment_init (&padpriv->segment, GST_FORMAT_UNDEFINED);
  GST_OBJECT_UNLOCK (mux);

  gst_object_unref (pad);
}


static void
gst_rtp_mux_ready_to_paused (GstRTPMux * rtp_mux)
{
  GstIterator *iter;

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (rtp_mux));
  while (gst_iterator_foreach (iter, clear_segment, rtp_mux) ==
      GST_ITERATOR_RESYNC);
  gst_iterator_free (iter);

  GST_OBJECT_LOCK (rtp_mux);
  rtp_mux->segment_pending = TRUE;

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

  rtp_mux->last_stop = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (rtp_mux, "set clock-base to %u", rtp_mux->ts_base);

  GST_OBJECT_UNLOCK (rtp_mux);
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
  GST_DEBUG_CATEGORY_INIT (gst_rtp_mux_debug, "rtpmux", 0, "rtp muxer");

  return gst_element_register (plugin, "rtpmux", GST_RANK_NONE,
      GST_TYPE_RTP_MUX);
}
