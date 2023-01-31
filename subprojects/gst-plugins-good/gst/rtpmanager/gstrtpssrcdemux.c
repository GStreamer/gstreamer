/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
 *
 * RTP SSRC demuxer
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
 * SECTION:element-rtpssrcdemux
 * @title: rtpssrcdemux
 *
 * rtpssrcdemux acts as a demuxer for RTP packets based on the SSRC of the
 * packets. Its main purpose is to allow an application to easily receive and
 * decode an RTP stream with multiple SSRCs.
 *
 * For each SSRC that is detected, a new pad will be created and the
 * #GstRtpSsrcDemux::new-ssrc-pad signal will be emitted.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 udpsrc caps="application/x-rtp" ! rtpssrcdemux ! fakesink
 * ]| Takes an RTP stream and send the RTP packets with the first detected SSRC
 * to fakesink, discarding the other SSRCs.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>

#include "gstrtpssrcdemux.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_ssrc_demux_debug);
#define GST_CAT_DEFAULT gst_rtp_ssrc_demux_debug

/* generic templates */
static GstStaticPadTemplate rtp_ssrc_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_ssrc_demux_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtp_ssrc_demux_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtp_ssrc_demux_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

#define INTERNAL_STREAM_LOCK(obj)   (g_rec_mutex_lock (&(obj)->padlock))
#define INTERNAL_STREAM_UNLOCK(obj) (g_rec_mutex_unlock (&(obj)->padlock))

#define GST_PAD_FLAG_STICKIES_SENT (GST_PAD_FLAG_LAST << 0)
#define GST_PAD_STICKIES_SENT(pad)  (GST_OBJECT_FLAG_IS_SET (pad, GST_PAD_FLAG_STICKIES_SENT))
#define GST_PAD_SET_STICKIES_SENT(pad) (GST_OBJECT_FLAG_SET (pad, GST_PAD_FLAG_STICKIES_SENT))

typedef enum
{
  RTP_PAD,
  RTCP_PAD
} PadType;

#define DEFAULT_MAX_STREAMS G_MAXUINT
enum
{
  PROP_0,
  PROP_MAX_STREAMS
};

/* signals */
enum
{
  SIGNAL_NEW_SSRC_PAD,
  SIGNAL_REMOVED_SSRC_PAD,
  SIGNAL_CLEAR_SSRC,
  LAST_SIGNAL
};

#define gst_rtp_ssrc_demux_parent_class parent_class
G_DEFINE_TYPE (GstRtpSsrcDemux, gst_rtp_ssrc_demux, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE (rtpssrcdemux, "rtpssrcdemux", GST_RANK_NONE,
    GST_TYPE_RTP_SSRC_DEMUX);

/* GObject vmethods */
static void gst_rtp_ssrc_demux_dispose (GObject * object);
static void gst_rtp_ssrc_demux_finalize (GObject * object);

/* GstElement vmethods */
static GstStateChangeReturn gst_rtp_ssrc_demux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_rtp_ssrc_demux_clear_ssrc (GstRtpSsrcDemux * demux,
    guint32 ssrc);

/* sinkpad stuff */
static GstFlowReturn gst_rtp_ssrc_demux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_rtp_ssrc_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstFlowReturn gst_rtp_ssrc_demux_rtcp_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstIterator *gst_rtp_ssrc_demux_iterate_internal_links_sink (GstPad *
    pad, GstObject * parent);

/* srcpad stuff */
static gboolean gst_rtp_ssrc_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstIterator *gst_rtp_ssrc_demux_iterate_internal_links_src (GstPad * pad,
    GstObject * parent);
static gboolean gst_rtp_ssrc_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static guint gst_rtp_ssrc_demux_signals[LAST_SIGNAL] = { 0 };

/*
 * Item for storing GstPad <-> SSRC pairs.
 */
typedef struct
{
  guint32 ssrc;
  GstPad *rtp_pad;
  GstCaps *caps;
  GstPad *rtcp_pad;
} GstRtpSsrcDemuxPads;

/* find a src pad for a given SSRC, returns NULL if the SSRC was not found
 * MUST be called with object lock
 */
static GstRtpSsrcDemuxPads *
find_demux_pads_for_ssrc (GstRtpSsrcDemux * demux, guint32 ssrc)
{
  GSList *walk;

  for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
    GstRtpSsrcDemuxPads *pad = (GstRtpSsrcDemuxPads *) walk->data;

    if (pad->ssrc == ssrc)
      return pad;
  }
  return NULL;
}

/* returns a reference to the pad if found, %NULL otherwise */
static GstPad *
get_demux_pad_for_ssrc (GstRtpSsrcDemux * demux, guint32 ssrc, PadType padtype)
{
  GstRtpSsrcDemuxPads *dpads;
  GstPad *retpad;

  GST_OBJECT_LOCK (demux);

  dpads = find_demux_pads_for_ssrc (demux, ssrc);
  if (!dpads) {
    GST_OBJECT_UNLOCK (demux);
    return NULL;
  }

  switch (padtype) {
    case RTP_PAD:
      retpad = gst_object_ref (dpads->rtp_pad);
      break;
    case RTCP_PAD:
      retpad = gst_object_ref (dpads->rtcp_pad);
      break;
    default:
      retpad = NULL;
      g_assert_not_reached ();
  }

  GST_OBJECT_UNLOCK (demux);

  return retpad;
}

static GstEvent *
add_ssrc_and_ref (GstEvent * event, guint32 ssrc)
{
  /* Set the ssrc on the output caps */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstCaps *newcaps;
      GstStructure *s;

      gst_event_parse_caps (event, &caps);
      newcaps = gst_caps_copy (caps);

      s = gst_caps_get_structure (newcaps, 0);
      gst_structure_set (s, "ssrc", G_TYPE_UINT, ssrc, NULL);
      event = gst_event_new_caps (newcaps);
      gst_caps_unref (newcaps);
      break;
    }
    case GST_EVENT_STREAM_START:
    {
      const gchar *stream_id;
      gchar *new_stream_id;
      guint group_id;
      GstStreamFlags flags;
      GstEvent *new_event;

      gst_event_parse_stream_start (event, &stream_id);

      new_stream_id =
          g_strdup_printf ("%s/%u", stream_id ? stream_id : "", ssrc);
      new_event = gst_event_new_stream_start (new_stream_id);
      g_free (new_stream_id);

      if (gst_event_parse_group_id (event, &group_id))
        gst_event_set_group_id (new_event, group_id);
      gst_event_parse_stream_flags (event, &flags);
      gst_event_set_stream_flags (new_event, flags);

      event = new_event;

      break;
    }
    default:
      gst_event_ref (event);
      break;
  }

  return event;
}

struct ForwardStickyEventData
{
  GstPad *pad;
  guint32 ssrc;
};

/* With internal stream lock held */
static gboolean
forward_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  struct ForwardStickyEventData *data = user_data;
  GstEvent *newevent;

  newevent = add_ssrc_and_ref (*event, data->ssrc);
  gst_pad_push_event (data->pad, newevent);

  return TRUE;
}

static void
forward_initial_events (GstRtpSsrcDemux * demux, guint32 ssrc, GstPad * pad,
    PadType padtype)
{
  struct ForwardStickyEventData fdata;
  GstPad *sinkpad = NULL;

  if (padtype == RTP_PAD)
    sinkpad = demux->rtp_sink;
  else if (padtype == RTCP_PAD)
    sinkpad = demux->rtcp_sink;
  else
    g_assert_not_reached ();

  fdata.ssrc = ssrc;
  fdata.pad = pad;

  gst_pad_sticky_events_foreach (sinkpad, forward_sticky_events, &fdata);
}

/* MUST only be called from streaming thread */
static GstPad *
find_or_create_demux_pad_for_ssrc (GstRtpSsrcDemux * demux, guint32 ssrc,
    PadType padtype)
{
  GstPad *rtp_pad, *rtcp_pad;
  GstElementClass *klass;
  GstPadTemplate *templ;
  gchar *padname;
  GstRtpSsrcDemuxPads *dpads;
  GstPad *retpad;
  guint num_streams;

  INTERNAL_STREAM_LOCK (demux);

  retpad = get_demux_pad_for_ssrc (demux, ssrc, padtype);
  if (retpad != NULL) {
    INTERNAL_STREAM_UNLOCK (demux);
    return retpad;
  }
  /* We create 2 src pads per ssrc (RTP & RTCP). Checking if we are allowed
     to create 2 more pads */
  num_streams = (GST_ELEMENT_CAST (demux)->numsrcpads) >> 1;
  if (num_streams >= demux->max_streams) {
    INTERNAL_STREAM_UNLOCK (demux);
    return NULL;
  }

  GST_DEBUG_OBJECT (demux, "creating new pad for SSRC %08x", ssrc);

  klass = GST_ELEMENT_GET_CLASS (demux);
  templ = gst_element_class_get_pad_template (klass, "src_%u");
  padname = g_strdup_printf ("src_%u", ssrc);
  rtp_pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  templ = gst_element_class_get_pad_template (klass, "rtcp_src_%u");
  padname = g_strdup_printf ("rtcp_src_%u", ssrc);
  rtcp_pad = gst_pad_new_from_template (templ, padname);
  g_free (padname);

  /* wrap in structure and add to list */
  dpads = g_new0 (GstRtpSsrcDemuxPads, 1);
  dpads->ssrc = ssrc;
  dpads->rtp_pad = rtp_pad;
  dpads->rtcp_pad = rtcp_pad;

  GST_OBJECT_LOCK (demux);
  demux->srcpads = g_slist_prepend (demux->srcpads, dpads);
  GST_OBJECT_UNLOCK (demux);

  gst_pad_set_query_function (rtp_pad, gst_rtp_ssrc_demux_src_query);
  gst_pad_set_iterate_internal_links_function (rtp_pad,
      gst_rtp_ssrc_demux_iterate_internal_links_src);
  gst_pad_set_event_function (rtp_pad, gst_rtp_ssrc_demux_src_event);
  gst_pad_use_fixed_caps (rtp_pad);
  gst_pad_set_active (rtp_pad, TRUE);

  gst_pad_set_event_function (rtcp_pad, gst_rtp_ssrc_demux_src_event);
  gst_pad_set_iterate_internal_links_function (rtcp_pad,
      gst_rtp_ssrc_demux_iterate_internal_links_src);
  gst_pad_use_fixed_caps (rtcp_pad);
  gst_pad_set_active (rtcp_pad, TRUE);

  gst_element_add_pad (GST_ELEMENT_CAST (demux), rtp_pad);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), rtcp_pad);

  switch (padtype) {
    case RTP_PAD:
      retpad = gst_object_ref (dpads->rtp_pad);
      break;
    case RTCP_PAD:
      retpad = gst_object_ref (dpads->rtcp_pad);
      break;
    default:
      retpad = NULL;
      g_assert_not_reached ();
  }

  g_signal_emit (G_OBJECT (demux),
      gst_rtp_ssrc_demux_signals[SIGNAL_NEW_SSRC_PAD], 0, ssrc, rtp_pad);

  INTERNAL_STREAM_UNLOCK (demux);

  return retpad;
}

static void
gst_rtp_ssrc_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (object);
  switch (prop_id) {
    case PROP_MAX_STREAMS:
      demux->max_streams = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_ssrc_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (object);
  switch (prop_id) {
    case PROP_MAX_STREAMS:
      g_value_set_uint (value, demux->max_streams);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_ssrc_demux_class_init (GstRtpSsrcDemuxClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *gstelement_klass;
  GstRtpSsrcDemuxClass *gstrtpssrcdemux_klass;

  gobject_klass = (GObjectClass *) klass;
  gstelement_klass = (GstElementClass *) klass;
  gstrtpssrcdemux_klass = (GstRtpSsrcDemuxClass *) klass;

  gobject_klass->dispose = gst_rtp_ssrc_demux_dispose;
  gobject_klass->finalize = gst_rtp_ssrc_demux_finalize;
  gobject_klass->set_property = gst_rtp_ssrc_demux_set_property;
  gobject_klass->get_property = gst_rtp_ssrc_demux_get_property;

  g_object_class_install_property (gobject_klass, PROP_MAX_STREAMS,
      g_param_spec_uint ("max-streams", "Max Streams",
          "The maximum number of streams allowed",
          0, G_MAXUINT, DEFAULT_MAX_STREAMS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRtpSsrcDemux::new-ssrc-pad:
   * @demux: the object which received the signal
   * @ssrc: the SSRC of the pad
   * @pad: the new pad.
   *
   * Emitted when a new SSRC pad has been created.
   */
  gst_rtp_ssrc_demux_signals[SIGNAL_NEW_SSRC_PAD] =
      g_signal_new ("new-ssrc-pad",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRtpSsrcDemuxClass, new_ssrc_pad),
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, GST_TYPE_PAD);

  /**
   * GstRtpSsrcDemux::removed-ssrc-pad:
   * @demux: the object which received the signal
   * @ssrc: the SSRC of the pad
   * @pad: the removed pad.
   *
   * Emitted when a SSRC pad has been removed.
   */
  gst_rtp_ssrc_demux_signals[SIGNAL_REMOVED_SSRC_PAD] =
      g_signal_new ("removed-ssrc-pad",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstRtpSsrcDemuxClass, removed_ssrc_pad),
      NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_UINT, GST_TYPE_PAD);

  /**
   * GstRtpSsrcDemux::clear-ssrc:
   * @demux: the object which received the signal
   * @ssrc: the SSRC of the pad
   *
   * Action signal to remove the pad for SSRC.
   */
  gst_rtp_ssrc_demux_signals[SIGNAL_CLEAR_SSRC] =
      g_signal_new ("clear-ssrc",
      G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstRtpSsrcDemuxClass, clear_ssrc),
      NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_UINT);

  gstelement_klass->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_ssrc_demux_change_state);
  gstrtpssrcdemux_klass->clear_ssrc =
      GST_DEBUG_FUNCPTR (gst_rtp_ssrc_demux_clear_ssrc);

  gst_element_class_add_static_pad_template (gstelement_klass,
      &rtp_ssrc_demux_sink_template);
  gst_element_class_add_static_pad_template (gstelement_klass,
      &rtp_ssrc_demux_rtcp_sink_template);
  gst_element_class_add_static_pad_template (gstelement_klass,
      &rtp_ssrc_demux_src_template);
  gst_element_class_add_static_pad_template (gstelement_klass,
      &rtp_ssrc_demux_rtcp_src_template);

  gst_element_class_set_static_metadata (gstelement_klass, "RTP SSRC Demux",
      "Demux/Network/RTP",
      "Splits RTP streams based on the SSRC",
      "Wim Taymans <wim.taymans@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (gst_rtp_ssrc_demux_debug,
      "rtpssrcdemux", 0, "RTP SSRC demuxer");

  GST_DEBUG_REGISTER_FUNCPTR (gst_rtp_ssrc_demux_chain);
  GST_DEBUG_REGISTER_FUNCPTR (gst_rtp_ssrc_demux_rtcp_chain);
}

static void
gst_rtp_ssrc_demux_init (GstRtpSsrcDemux * demux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);

  demux->rtp_sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_chain_function (demux->rtp_sink, gst_rtp_ssrc_demux_chain);
  gst_pad_set_event_function (demux->rtp_sink, gst_rtp_ssrc_demux_sink_event);
  gst_pad_set_iterate_internal_links_function (demux->rtp_sink,
      gst_rtp_ssrc_demux_iterate_internal_links_sink);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), demux->rtp_sink);

  demux->rtcp_sink =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "rtcp_sink"), "rtcp_sink");
  gst_pad_set_chain_function (demux->rtcp_sink, gst_rtp_ssrc_demux_rtcp_chain);
  gst_pad_set_event_function (demux->rtcp_sink, gst_rtp_ssrc_demux_sink_event);
  gst_pad_set_iterate_internal_links_function (demux->rtcp_sink,
      gst_rtp_ssrc_demux_iterate_internal_links_sink);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), demux->rtcp_sink);

  demux->max_streams = DEFAULT_MAX_STREAMS;

  g_rec_mutex_init (&demux->padlock);
}

static void
gst_rtp_ssrc_demux_pads_free (GstRtpSsrcDemuxPads * dpads)
{
  gst_pad_set_active (dpads->rtp_pad, FALSE);
  gst_pad_set_active (dpads->rtcp_pad, FALSE);

  gst_element_remove_pad (GST_PAD_PARENT (dpads->rtp_pad), dpads->rtp_pad);
  gst_element_remove_pad (GST_PAD_PARENT (dpads->rtcp_pad), dpads->rtcp_pad);

  g_free (dpads);
}

static void
gst_rtp_ssrc_demux_reset (GstRtpSsrcDemux * demux)
{
  g_slist_free_full (demux->srcpads,
      (GDestroyNotify) gst_rtp_ssrc_demux_pads_free);
  demux->srcpads = NULL;
}

static void
gst_rtp_ssrc_demux_dispose (GObject * object)
{
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (object);

  gst_rtp_ssrc_demux_reset (demux);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_rtp_ssrc_demux_finalize (GObject * object)
{
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (object);
  g_rec_mutex_clear (&demux->padlock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_ssrc_demux_clear_ssrc (GstRtpSsrcDemux * demux, guint32 ssrc)
{
  GstRtpSsrcDemuxPads *dpads;

  GST_OBJECT_LOCK (demux);
  dpads = find_demux_pads_for_ssrc (demux, ssrc);
  if (dpads == NULL) {
    GST_OBJECT_UNLOCK (demux);
    goto unknown_pad;
  }

  GST_DEBUG_OBJECT (demux, "clearing pad for SSRC %08x", ssrc);

  demux->srcpads = g_slist_remove (demux->srcpads, dpads);
  GST_OBJECT_UNLOCK (demux);

  g_signal_emit (G_OBJECT (demux),
      gst_rtp_ssrc_demux_signals[SIGNAL_REMOVED_SSRC_PAD], 0, ssrc,
      dpads->rtp_pad);

  gst_rtp_ssrc_demux_pads_free (dpads);

  return;

  /* ERRORS */
unknown_pad:
  {
    GST_WARNING_OBJECT (demux, "unknown SSRC %08x", ssrc);
    return;
  }
}

struct ForwardEventData
{
  GstRtpSsrcDemux *demux;
  GstEvent *event;
  gboolean res;
  GstPad *pad;
};

static gboolean
forward_event (GstPad * pad, gpointer user_data)
{
  struct ForwardEventData *fdata = user_data;
  GSList *walk = NULL;
  GstEvent *newevent = NULL;

  /* special case for EOS */
  if (GST_EVENT_TYPE (fdata->event) == GST_EVENT_EOS)
    GST_PAD_SET_STICKIES_SENT (pad);

  if (GST_EVENT_IS_STICKY (fdata->event) && !GST_PAD_STICKIES_SENT (pad))
    return FALSE;

  GST_OBJECT_LOCK (fdata->demux);
  for (walk = fdata->demux->srcpads; walk; walk = walk->next) {
    GstRtpSsrcDemuxPads *dpads = (GstRtpSsrcDemuxPads *) walk->data;

    if (pad == dpads->rtp_pad || pad == dpads->rtcp_pad) {
      newevent = add_ssrc_and_ref (fdata->event, dpads->ssrc);
      break;
    }
  }
  GST_OBJECT_UNLOCK (fdata->demux);

  if (newevent)
    fdata->res &= gst_pad_push_event (pad, newevent);

  return FALSE;
}


static gboolean
gst_rtp_ssrc_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRtpSsrcDemux *demux;
  struct ForwardEventData fdata;

  demux = GST_RTP_SSRC_DEMUX (parent);

  fdata.demux = demux;
  fdata.pad = pad;
  fdata.event = event;
  fdata.res = TRUE;

  gst_pad_forward (pad, forward_event, &fdata);

  gst_event_unref (event);

  return fdata.res;
}

static GstFlowReturn
gst_rtp_ssrc_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstRtpSsrcDemux *demux;
  guint32 ssrc;
  GstRTPBuffer rtp = { NULL };
  GstPad *srcpad;

  demux = GST_RTP_SSRC_DEMUX (parent);

  if (!gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp))
    goto invalid_payload;

  ssrc = gst_rtp_buffer_get_ssrc (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  GST_DEBUG_OBJECT (demux, "received buffer of SSRC %08x", ssrc);

  srcpad = find_or_create_demux_pad_for_ssrc (demux, ssrc, RTP_PAD);
  if (srcpad == NULL)
    goto create_failed;

  if (!GST_PAD_STICKIES_SENT (srcpad)) {
    forward_initial_events (demux, ssrc, srcpad, RTP_PAD);
    GST_PAD_SET_STICKIES_SENT (srcpad);
  }

  /* push to srcpad */
  ret = gst_pad_push (srcpad, buf);

  if (ret != GST_FLOW_OK) {
    GstPad *active_pad;

    /* check if the ssrc still there, may have been removed */
    active_pad = get_demux_pad_for_ssrc (demux, ssrc, RTP_PAD);

    if (active_pad == NULL || active_pad != srcpad) {
      /* SSRC was removed during the push ... ignore the error */
      ret = GST_FLOW_OK;
    }

    g_clear_object (&active_pad);
  }

  gst_object_unref (srcpad);

  return ret;

  /* ERRORS */
invalid_payload:
  {
    GST_DEBUG_OBJECT (demux, "Dropping invalid RTP packet");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }
create_failed:
  {
    gst_buffer_unref (buf);
    GST_WARNING_OBJECT (demux,
        "Dropping buffer SSRC %08x. "
        "Max streams number reached (%u)", ssrc, demux->max_streams);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_rtp_ssrc_demux_rtcp_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstFlowReturn ret;
  GstRtpSsrcDemux *demux;
  guint32 ssrc;
  GstRTCPPacket packet;
  GstRTCPBuffer rtcp = { NULL, };
  GstPad *srcpad;

  demux = GST_RTP_SSRC_DEMUX (parent);

  if (!gst_rtcp_buffer_validate_reduced (buf))
    goto invalid_rtcp;

  gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp);
  if (!gst_rtcp_buffer_get_first_packet (&rtcp, &packet)) {
    gst_rtcp_buffer_unmap (&rtcp);
    goto invalid_rtcp;
  }

  /* first packet must be SR or RR, or in case of a reduced size RTCP packet
   * it must be APP, RTPFB or PSFB feeadback, or else the validate would
   * have failed */
  switch (gst_rtcp_packet_get_type (&packet)) {
    case GST_RTCP_TYPE_SR:
      /* get the ssrc so that we can route it to the right source pad */
      gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, NULL, NULL, NULL,
          NULL);
      break;
    case GST_RTCP_TYPE_RR:
      ssrc = gst_rtcp_packet_rr_get_ssrc (&packet);
      break;
    case GST_RTCP_TYPE_APP:
      ssrc = gst_rtcp_packet_app_get_ssrc (&packet);
      break;
    case GST_RTCP_TYPE_RTPFB:
    case GST_RTCP_TYPE_PSFB:
      ssrc = gst_rtcp_packet_fb_get_sender_ssrc (&packet);
      break;
    default:
      goto unexpected_rtcp;
  }
  gst_rtcp_buffer_unmap (&rtcp);

  GST_DEBUG_OBJECT (demux, "received RTCP of SSRC %08x", ssrc);

  srcpad = find_or_create_demux_pad_for_ssrc (demux, ssrc, RTCP_PAD);
  if (srcpad == NULL)
    goto create_failed;

  if (!GST_PAD_STICKIES_SENT (srcpad)) {
    forward_initial_events (demux, ssrc, srcpad, RTCP_PAD);
    GST_PAD_SET_STICKIES_SENT (srcpad);
  }

  /* push to srcpad */
  ret = gst_pad_push (srcpad, buf);

  if (ret != GST_FLOW_OK) {
    GstPad *active_pad;

    /* check if the ssrc still there, may have been removed */
    active_pad = get_demux_pad_for_ssrc (demux, ssrc, RTCP_PAD);
    if (active_pad == NULL || active_pad != srcpad) {
      /* SSRC was removed during the push ... ignore the error */
      ret = GST_FLOW_OK;
    }

    g_clear_object (&active_pad);
  }

  gst_object_unref (srcpad);

  return ret;

  /* ERRORS */
invalid_rtcp:
  {
    GST_DEBUG_OBJECT (demux, "Dropping invalid RTCP packet");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }
unexpected_rtcp:
  {
    GST_DEBUG_OBJECT (demux, "dropping unexpected RTCP packet");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }
create_failed:
  {
    gst_buffer_unref (buf);
    GST_WARNING_OBJECT (demux,
        "Dropping buffer SSRC %08x. "
        "Max streams number reached (%u)", ssrc, demux->max_streams);
    return GST_FLOW_OK;
  }
}

static GstRtpSsrcDemuxPads *
find_demux_pad_for_pad (GstRtpSsrcDemux * demux, GstPad * pad)
{
  GSList *walk;

  for (walk = demux->srcpads; walk; walk = g_slist_next (walk)) {
    GstRtpSsrcDemuxPads *dpads = (GstRtpSsrcDemuxPads *) walk->data;
    if (dpads->rtp_pad == pad || dpads->rtcp_pad == pad) {
      return dpads;
    }
  }

  return NULL;
}


static gboolean
gst_rtp_ssrc_demux_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstRtpSsrcDemux *demux;
  const GstStructure *s;

  demux = GST_RTP_SSRC_DEMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:
    case GST_EVENT_CUSTOM_BOTH:
    case GST_EVENT_CUSTOM_BOTH_OOB:
      s = gst_event_get_structure (event);
      if (s && !gst_structure_has_field (s, "ssrc")) {
        GstRtpSsrcDemuxPads *dpads = find_demux_pad_for_pad (demux, pad);

        if (dpads) {
          GstStructure *ws;

          event = gst_event_make_writable (event);
          ws = gst_event_writable_structure (event);
          gst_structure_set (ws, "ssrc", G_TYPE_UINT, dpads->ssrc, NULL);
        }
      }
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static GstIterator *
gst_rtp_ssrc_demux_iterate_internal_links_src (GstPad * pad, GstObject * parent)
{
  GstRtpSsrcDemux *demux;
  GstPad *otherpad = NULL;
  GstIterator *it = NULL;
  GSList *current;

  demux = GST_RTP_SSRC_DEMUX (parent);

  GST_OBJECT_LOCK (demux);
  for (current = demux->srcpads; current; current = g_slist_next (current)) {
    GstRtpSsrcDemuxPads *dpads = (GstRtpSsrcDemuxPads *) current->data;

    if (pad == dpads->rtp_pad) {
      otherpad = demux->rtp_sink;
      break;
    } else if (pad == dpads->rtcp_pad) {
      otherpad = demux->rtcp_sink;
      break;
    }
  }
  if (otherpad) {
    GValue val = { 0, };

    g_value_init (&val, GST_TYPE_PAD);
    g_value_set_object (&val, otherpad);
    it = gst_iterator_new_single (GST_TYPE_PAD, &val);
    g_value_unset (&val);

  }
  GST_OBJECT_UNLOCK (demux);

  return it;
}

/* Should return 0 for elements to be included */
static gint
src_pad_compare_func (gconstpointer a, gconstpointer b)
{
  GstPad *pad = GST_PAD (g_value_get_object (a));
  const gchar *prefix = g_value_get_string (b);
  gint res;

  /* 0 means equal means we accept the pad, accepted if there is a name
   * and it starts with the prefix */
  GST_OBJECT_LOCK (pad);
  res = !GST_PAD_NAME (pad) || !g_str_has_prefix (GST_PAD_NAME (pad), prefix);
  GST_OBJECT_UNLOCK (pad);

  return res;
}

static GstIterator *
gst_rtp_ssrc_demux_iterate_internal_links_sink (GstPad * pad,
    GstObject * parent)
{
  GstRtpSsrcDemux *demux;
  GstIterator *it = NULL;
  GValue gval = { 0, };

  demux = GST_RTP_SSRC_DEMUX (parent);

  g_value_init (&gval, G_TYPE_STRING);
  if (pad == demux->rtp_sink)
    g_value_set_static_string (&gval, "src_");
  else if (pad == demux->rtcp_sink)
    g_value_set_static_string (&gval, "rtcp_src_");
  else
    g_assert_not_reached ();

  it = gst_element_iterate_src_pads (GST_ELEMENT_CAST (demux));
  it = gst_iterator_filter (it, src_pad_compare_func, &gval);

  return it;
}


static gboolean
gst_rtp_ssrc_demux_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstRtpSsrcDemux *demux;
  gboolean res = FALSE;

  demux = GST_RTP_SSRC_DEMUX (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {

      if ((res = gst_pad_peer_query (demux->rtp_sink, query))) {
        gboolean live;
        GstClockTime min_latency, max_latency;

        gst_query_parse_latency (query, &live, &min_latency, &max_latency);

        GST_DEBUG_OBJECT (pad, "peer min latency %" GST_TIME_FORMAT,
            GST_TIME_ARGS (min_latency));

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static GstStateChangeReturn
gst_rtp_ssrc_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRtpSsrcDemux *demux;

  demux = GST_RTP_SSRC_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_rtp_ssrc_demux_reset (demux);
      break;
    default:
      break;
  }
  return ret;
}
