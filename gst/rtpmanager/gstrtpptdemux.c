/* 
 * RTP Demux element
 *
 * Copyright (C) 2005 Nokia Corporation.
 * @author Kai Vehmanen <kai.vehmanen@nokia.com>
 *
 * Loosely based on GStreamer gstdecodebin
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
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
 * SECTION:element-rtpptdemux
 * @short_description: separate RTP payloads based on the payload type
 *
 * <refsect2>
 * <para>
 * rtpptdemux acts as a demuxer for RTP packets based on the payload type of the
 * packets. Its main purpose is to allow an application to easily receive and
 * decode an RTP stream with multiple payload types.
 * </para>
 * <para>
 * For each payload type that is detected, a new pad will be created and the
 * ::new-payload-type signal will be emitted. When the payload for the RTP
 * stream changes, the ::payload-type-change signal will be emitted.
 * </para>
 * <para>
 * The element will try to set complete and unique application/x-rtp caps on the
 * outgoing buffers and pads based on the result of the ::request-pt-map signal.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch udpsrc caps="application/x-rtp" ! rtpptdemux ! fakesink
 * </programlisting>
 * Takes an RTP stream and send the RTP packets with the first detected payload
 * type to fakesink, discarding the other payload types.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-05-22 (0.10.6)
 */

/*
 * Contributors:
 * Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 */
/*
 * Status:
 *  - works with the test_rtpdemux.c tool
 *
 * Check:
 *  - is emitting a signal enough, or should we
 *    use GstEvent to notify downstream elements
 *    of the new packet... no?
 *
 * Notes:
 *  - emits event both for new PTs, and whenever
 *    a PT is changed
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpbin-marshal.h"
#include "gstrtpptdemux.h"

/* generic templates */
static GstStaticPadTemplate rtp_pt_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_pt_demux_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp, " "payload = (int) [ 0, 255 ]")
    );

GST_DEBUG_CATEGORY_STATIC (gst_rtp_pt_demux_debug);
#define GST_CAT_DEFAULT gst_rtp_pt_demux_debug

/**
 * Item for storing GstPad<->pt pairs.
 */
struct _GstRTPPtDemuxPad
{
  GstPad *pad;        /**< pointer to the actual pad */
  gint pt;             /**< RTP payload-type attached to pad */
};

/* signals */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  SIGNAL_NEW_PAYLOAD_TYPE,
  SIGNAL_PAYLOAD_TYPE_CHANGE,
  SIGNAL_CLEAR_PT_MAP,
  LAST_SIGNAL
};

GST_BOILERPLATE (GstRTPPtDemux, gst_rtp_pt_demux, GstElement, GST_TYPE_ELEMENT);

static void gst_rtp_pt_demux_finalize (GObject * object);

static void gst_rtp_pt_demux_release (GstElement * element);
static gboolean gst_rtp_pt_demux_setup (GstElement * element);

static GstFlowReturn gst_rtp_pt_demux_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_rtp_pt_demux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_rtp_pt_demux_clear_pt_map (GstRTPPtDemux * rtpdemux);

static GstPad *find_pad_for_pt (GstRTPPtDemux * rtpdemux, guint8 pt);

static guint gst_rtp_pt_demux_signals[LAST_SIGNAL] = { 0 };

static GstElementDetails gst_rtp_pt_demux_details = {
  "RTP Demux",
  "Demux/Network/RTP",
  "Parses codec streams transmitted in the same RTP session",
  "Kai Vehmanen <kai.vehmanen@nokia.com>"
};

static void
gst_rtp_pt_demux_base_init (gpointer g_class)
{
  GstElementClass *gstelement_klass = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_pt_demux_sink_template));
  gst_element_class_add_pad_template (gstelement_klass,
      gst_static_pad_template_get (&rtp_pt_demux_src_template));

  gst_element_class_set_details (gstelement_klass, &gst_rtp_pt_demux_details);
}

static void
gst_rtp_pt_demux_class_init (GstRTPPtDemuxClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;

  /**
   * GstRTPPtDemux::request-pt-map:
   * @demux: the object which received the signal
   * @pt: the payload type
   *
   * Request the payload type as #GstCaps for @pt.
   */
  gst_rtp_pt_demux_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPPtDemuxClass, request_pt_map),
      NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT, GST_TYPE_CAPS, 1,
      G_TYPE_UINT);

  /**
   * GstRTPPtDemux::new-payload-type:
   * @demux: the object which received the signal
   * @pt: the payload type
   * @pad: the pad with the new payload
   *
   * Emited when a new payload type pad has been created in @demux.
   */
  gst_rtp_pt_demux_signals[SIGNAL_NEW_PAYLOAD_TYPE] =
      g_signal_new ("new-payload-type", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPPtDemuxClass, new_payload_type),
      NULL, NULL, gst_rtp_bin_marshal_VOID__UINT_OBJECT, G_TYPE_NONE, 2,
      G_TYPE_UINT, GST_TYPE_PAD);

  /**
   * GstRTPPtDemux::payload-type-change:
   * @demux: the object which received the signal
   * @pt: the new payload type
   *
   * Emited when the payload type changed.
   */
  gst_rtp_pt_demux_signals[SIGNAL_PAYLOAD_TYPE_CHANGE] =
      g_signal_new ("payload-type-change", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPPtDemuxClass,
          payload_type_change), NULL, NULL, g_cclosure_marshal_VOID__UINT,
      G_TYPE_NONE, 1, G_TYPE_UINT);

  /**
   * GstRTPPtDemux::clear-pt-map:
   * @demux: the object which received the signal
   *
   * The application can call this signal to instruct the element to discard the
   * currently cached payload type map.
   */
  gst_rtp_pt_demux_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_ACTION | G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPPtDemuxClass,
          clear_pt_map), NULL, NULL, g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0, G_TYPE_NONE);

  gobject_klass->finalize = GST_DEBUG_FUNCPTR (gst_rtp_pt_demux_finalize);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_pt_demux_change_state);

  klass->clear_pt_map = GST_DEBUG_FUNCPTR (gst_rtp_pt_demux_clear_pt_map);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_pt_demux_debug,
      "rtpptdemux", 0, "RTP codec demuxer");
}

static void
gst_rtp_pt_demux_init (GstRTPPtDemux * ptdemux, GstRTPPtDemuxClass * g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (ptdemux);

  ptdemux->sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  g_assert (ptdemux->sink != NULL);

  gst_pad_set_chain_function (ptdemux->sink, gst_rtp_pt_demux_chain);

  gst_element_add_pad (GST_ELEMENT (ptdemux), ptdemux->sink);
}

static void
gst_rtp_pt_demux_finalize (GObject * object)
{
  gst_rtp_pt_demux_release (GST_ELEMENT (object));

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_pt_demux_clear_pt_map (GstRTPPtDemux * rtpdemux)
{
  /* FIXME, do something */
}

static GstFlowReturn
gst_rtp_pt_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstRTPPtDemux *rtpdemux;
  GstElement *element = GST_ELEMENT (GST_OBJECT_PARENT (pad));
  guint8 pt;
  GstPad *srcpad;

  rtpdemux = GST_RTP_PT_DEMUX (GST_OBJECT_PARENT (pad));

  if (!gst_rtp_buffer_validate (buf))
    goto invalid_buffer;

  pt = gst_rtp_buffer_get_payload_type (buf);

  GST_DEBUG_OBJECT (rtpdemux, "received buffer for pt %d", pt);

  srcpad = find_pad_for_pt (rtpdemux, pt);
  if (srcpad == NULL) {
    /* new PT, create a src pad */
    GstElementClass *klass;
    GstPadTemplate *templ;
    gchar *padname;
    GstCaps *caps;
    GstRTPPtDemuxPad *rtpdemuxpad;
    GValue ret = { 0 };
    GValue args[2] = { {0}
    , {0}
    };


    klass = GST_ELEMENT_GET_CLASS (rtpdemux);
    templ = gst_element_class_get_pad_template (klass, "src_%d");
    padname = g_strdup_printf ("src_%d", pt);
    srcpad = gst_pad_new_from_template (templ, padname);
    gst_pad_use_fixed_caps (srcpad);
    g_free (padname);

    /* figure out the caps */
    g_value_init (&args[0], GST_TYPE_ELEMENT);
    g_value_set_object (&args[0], rtpdemux);
    g_value_init (&args[1], G_TYPE_UINT);
    g_value_set_uint (&args[1], pt);

    g_value_init (&ret, GST_TYPE_CAPS);
    g_value_set_boxed (&ret, NULL);

    g_signal_emitv (args, gst_rtp_pt_demux_signals[SIGNAL_REQUEST_PT_MAP], 0,
        &ret);

    caps = g_value_get_boxed (&ret);
    if (caps == NULL)
      caps = GST_PAD_CAPS (rtpdemux->sink);
    if (!caps)
      goto no_caps;

    caps = gst_caps_make_writable (caps);
    gst_caps_set_simple (caps, "payload", G_TYPE_INT, pt, NULL);
    gst_pad_set_caps (srcpad, caps);

    GST_DEBUG ("Adding pt=%d to the list.", pt);
    rtpdemuxpad = g_new0 (GstRTPPtDemuxPad, 1);
    rtpdemuxpad->pt = pt;
    rtpdemuxpad->pad = srcpad;
    rtpdemux->srcpads = g_slist_append (rtpdemux->srcpads, rtpdemuxpad);

    gst_pad_set_active (srcpad, TRUE);
    gst_element_add_pad (element, srcpad);

    GST_DEBUG ("emitting new-payload_type for pt %d", pt);
    g_signal_emit (G_OBJECT (rtpdemux),
        gst_rtp_pt_demux_signals[SIGNAL_NEW_PAYLOAD_TYPE], 0, pt, srcpad);
  }

  if (pt != rtpdemux->last_pt) {
    gint emit_pt = pt;

    /* our own signal with an extra flag that this is the only pad */
    rtpdemux->last_pt = pt;
    GST_DEBUG ("emitting payload-type-changed for pt %d", emit_pt);
    g_signal_emit (G_OBJECT (rtpdemux),
        gst_rtp_pt_demux_signals[SIGNAL_PAYLOAD_TYPE_CHANGE], 0, emit_pt);
  }

  gst_buffer_set_caps (buf, GST_PAD_CAPS (srcpad));

  /* push to srcpad */
  if (srcpad)
    ret = gst_pad_push (srcpad, GST_BUFFER (buf));

  return ret;

  /* ERRORS */
invalid_buffer:
  {
    /* this is fatal and should be filtered earlier */
    GST_ELEMENT_ERROR (rtpdemux, STREAM, DECODE, (NULL),
        ("Dropping invalid RTP payload"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
no_caps:
  {
    GST_ELEMENT_ERROR (rtpdemux, STREAM, DECODE, (NULL),
        ("Could not get caps for payload"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static GstPad *
find_pad_for_pt (GstRTPPtDemux * rtpdemux, guint8 pt)
{
  GstPad *respad = NULL;
  GSList *item = rtpdemux->srcpads;

  for (; item; item = g_slist_next (item)) {
    GstRTPPtDemuxPad *pad = item->data;

    if (pad->pt == pt) {
      respad = pad->pad;
      break;
    }
  }

  return respad;
}

/**
 * Reserves resources for the object.
 */
static gboolean
gst_rtp_pt_demux_setup (GstElement * element)
{
  GstRTPPtDemux *ptdemux = GST_RTP_PT_DEMUX (element);
  gboolean res = TRUE;

  if (ptdemux) {
    ptdemux->srcpads = NULL;
    ptdemux->last_pt = 0xFFFF;
  }

  return res;
}

/**
 * Free resources for the object.
 */
static void
gst_rtp_pt_demux_release (GstElement * element)
{
  GstRTPPtDemux *ptdemux = GST_RTP_PT_DEMUX (element);

  if (ptdemux) {
    /* note: GstElement's dispose() will handle the pads */
    g_slist_free (ptdemux->srcpads);
    ptdemux->srcpads = NULL;
  }
}

static GstStateChangeReturn
gst_rtp_pt_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPPtDemux *ptdemux;

  ptdemux = GST_RTP_PT_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (gst_rtp_pt_demux_setup (element) != TRUE)
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_rtp_pt_demux_release (element);
      break;
    default:
      break;
  }

  return ret;
}
