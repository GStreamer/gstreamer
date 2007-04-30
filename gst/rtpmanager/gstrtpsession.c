/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
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
 * SECTION:element-rtpsession
 * @short_description: an RTP session manager
 * @see_also: rtpjitterbuffer, rtpbin
 *
 * <refsect2>
 * <para>
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! alsasink
 * </programlisting>
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-04-02 (0.10.6)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstrtpbin-marshal.h"
#include "gstrtpsession.h"
#include "rtpsession.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_session_debug);
#define GST_CAT_DEFAULT gst_rtp_session_debug

/* elementfactory information */
static const GstElementDetails rtpsession_details =
GST_ELEMENT_DETAILS ("RTP Session",
    "Filter/Editor/Video",
    "Implement an RTP session",
    "Wim Taymans <wim@fluendo.com>");

/* sink pads */
static GstStaticPadTemplate rtpsession_recv_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpsession_recv_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtcp_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtpsession_send_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("send_rtp_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

/* src pads */
static GstStaticPadTemplate rtpsession_recv_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpsession_sync_src_template =
GST_STATIC_PAD_TEMPLATE ("sync_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtpsession_send_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("send_rtp_src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpsession_send_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("send_rtcp_src",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

/* signals and args */
enum
{
  SIGNAL_REQUEST_PT_MAP,
  LAST_SIGNAL
};

enum
{
  PROP_0
};

#define GST_RTP_SESSION_GET_PRIVATE(obj)  \
	   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTP_SESSION, GstRTPSessionPrivate))

#define GST_RTP_SESSION_LOCK(sess)   g_mutex_lock ((sess)->priv->lock)
#define GST_RTP_SESSION_UNLOCK(sess) g_mutex_unlock ((sess)->priv->lock)

struct _GstRTPSessionPrivate
{
  GMutex *lock;
  RTPSession *session;
  /* thread for sending out RTCP */
  GstClockID id;
  gboolean stop_thread;
  GThread *thread;
};

/* callbacks to handle actions from the session manager */
static GstFlowReturn gst_rtp_session_process_rtp (RTPSession * sess,
    RTPSource * src, GstBuffer * buffer, gpointer user_data);
static GstFlowReturn gst_rtp_session_send_rtp (RTPSession * sess,
    RTPSource * src, GstBuffer * buffer, gpointer user_data);
static GstFlowReturn gst_rtp_session_send_rtcp (RTPSession * sess,
    RTPSource * src, GstBuffer * buffer, gpointer user_data);
static gint gst_rtp_session_clock_rate (RTPSession * sess, guint8 payload,
    gpointer user_data);
static GstClockTime gst_rtp_session_get_time (RTPSession * sess,
    gpointer user_data);
static void gst_rtp_session_reconsider (RTPSession * sess, gpointer user_data);

static RTPSessionCallbacks callbacks = {
  gst_rtp_session_process_rtp,
  gst_rtp_session_send_rtp,
  gst_rtp_session_send_rtcp,
  gst_rtp_session_clock_rate,
  gst_rtp_session_get_time,
  gst_rtp_session_reconsider
};

/* GObject vmethods */
static void gst_rtp_session_finalize (GObject * object);
static void gst_rtp_session_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_session_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* GstElement vmethods */
static GstStateChangeReturn gst_rtp_session_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_rtp_session_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_session_release_pad (GstElement * element, GstPad * pad);

static guint gst_rtp_session_signals[LAST_SIGNAL] = { 0 };

GST_BOILERPLATE (GstRTPSession, gst_rtp_session, GstElement, GST_TYPE_ELEMENT);

static void
gst_rtp_session_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* sink pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpsession_recv_rtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpsession_recv_rtcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpsession_send_rtp_sink_template));

  /* src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpsession_recv_rtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpsession_sync_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpsession_send_rtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpsession_send_rtcp_src_template));

  gst_element_class_set_details (element_class, &rtpsession_details);
}

static void
gst_rtp_session_class_init (GstRTPSessionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRTPSessionPrivate));

  gobject_class->finalize = gst_rtp_session_finalize;
  gobject_class->set_property = gst_rtp_session_set_property;
  gobject_class->get_property = gst_rtp_session_get_property;

  /**
   * GstRTPSession::request-pt-map:
   * @sess: the object which received the signal
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt.
   */
  gst_rtp_session_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPSessionClass, request_pt_map),
      NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT, GST_TYPE_CAPS, 1,
      G_TYPE_UINT);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_session_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_session_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_session_release_pad);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_session_debug,
      "rtpsession", 0, "RTP Session");
}

static void
gst_rtp_session_init (GstRTPSession * rtpsession, GstRTPSessionClass * klass)
{
  rtpsession->priv = GST_RTP_SESSION_GET_PRIVATE (rtpsession);
  rtpsession->priv->lock = g_mutex_new ();
  rtpsession->priv->session = rtp_session_new ();
  /* configure callbacks */
  rtp_session_set_callbacks (rtpsession->priv->session, &callbacks, rtpsession);
}

static void
gst_rtp_session_finalize (GObject * object)
{
  GstRTPSession *rtpsession;

  rtpsession = GST_RTP_SESSION (object);
  g_mutex_free (rtpsession->priv->lock);
  g_object_unref (rtpsession->priv->session);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_session_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPSession *rtpsession;

  rtpsession = GST_RTP_SESSION (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_session_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTPSession *rtpsession;

  rtpsession = GST_RTP_SESSION (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rtcp_thread (GstRTPSession * rtpsession)
{
  GstClock *clock;
  GstClockID id;
  GstClockTime current_time;
  GstClockTime next_timeout;

  clock = gst_element_get_clock (GST_ELEMENT_CAST (rtpsession));
  if (clock == NULL)
    return;

  current_time = gst_clock_get_time (clock);

  GST_DEBUG_OBJECT (rtpsession, "entering RTCP thread");

  GST_RTP_SESSION_LOCK (rtpsession);

  while (!rtpsession->priv->stop_thread) {
    GstClockReturn res;

    /* get initial estimate */
    next_timeout =
        rtp_session_next_timeout (rtpsession->priv->session, current_time);


    GST_DEBUG_OBJECT (rtpsession, "next check time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (next_timeout));

    /* leave if no more timeouts, the session ended */
    if (next_timeout == GST_CLOCK_TIME_NONE)
      break;

    id = rtpsession->priv->id =
        gst_clock_new_single_shot_id (clock, next_timeout);
    GST_RTP_SESSION_UNLOCK (rtpsession);

    res = gst_clock_id_wait (id, NULL);

    GST_RTP_SESSION_LOCK (rtpsession);
    gst_clock_id_unref (id);
    rtpsession->priv->id = NULL;

    if (rtpsession->priv->stop_thread)
      break;

    /* update current time */
    current_time = gst_clock_get_time (clock);

    /* we get unlocked because we need to perform reconsideration, don't perform
     * the timeout but get a new reporting estimate. */
    GST_DEBUG_OBJECT (rtpsession, "unlocked %d, current %" GST_TIME_FORMAT,
        res, GST_TIME_ARGS (current_time));

    /* perform actions, we ignore result. */
    rtp_session_on_timeout (rtpsession->priv->session, current_time);
  }
  GST_RTP_SESSION_UNLOCK (rtpsession);

  gst_object_unref (clock);

  GST_DEBUG_OBJECT (rtpsession, "leaving RTCP thread");
}

static gboolean
start_rtcp_thread (GstRTPSession * rtpsession)
{
  GError *error = NULL;
  gboolean res;

  GST_DEBUG_OBJECT (rtpsession, "starting RTCP thread");

  GST_RTP_SESSION_LOCK (rtpsession);
  rtpsession->priv->stop_thread = FALSE;
  rtpsession->priv->thread =
      g_thread_create ((GThreadFunc) rtcp_thread, rtpsession, TRUE, &error);
  GST_RTP_SESSION_UNLOCK (rtpsession);

  if (error != NULL) {
    res = FALSE;
    GST_DEBUG_OBJECT (rtpsession, "failed to start thread, %s", error->message);
    g_error_free (error);
  } else {
    res = TRUE;
  }
  return res;
}

static void
stop_rtcp_thread (GstRTPSession * rtpsession)
{
  GST_DEBUG_OBJECT (rtpsession, "stopping RTCP thread");

  GST_RTP_SESSION_LOCK (rtpsession);
  rtpsession->priv->stop_thread = TRUE;
  if (rtpsession->priv->id)
    gst_clock_id_unschedule (rtpsession->priv->id);
  GST_RTP_SESSION_UNLOCK (rtpsession);

  g_thread_join (rtpsession->priv->thread);
}

static GstStateChangeReturn
gst_rtp_session_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstRTPSession *rtpsession;

  rtpsession = GST_RTP_SESSION (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      stop_rtcp_thread (rtpsession);
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (!start_rtcp_thread (rtpsession))
        goto failed_thread;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return res;

  /* ERRORS */
failed_thread:
  {
    return GST_STATE_CHANGE_FAILURE;
  }
}

/* called when the session manager has an RTP packet ready for further
 * processing */
static GstFlowReturn
gst_rtp_session_process_rtp (RTPSession * sess, RTPSource * src,
    GstBuffer * buffer, gpointer user_data)
{
  GstFlowReturn result;
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;

  rtpsession = GST_RTP_SESSION (user_data);
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "reading receiving RTP packet");

  if (rtpsession->recv_rtp_src) {
    result = gst_pad_push (rtpsession->recv_rtp_src, buffer);
  } else {
    gst_buffer_unref (buffer);
    result = GST_FLOW_OK;
  }
  return result;
}

/* called when the session manager has an RTP packet ready for further
 * sending */
static GstFlowReturn
gst_rtp_session_send_rtp (RTPSession * sess, RTPSource * src,
    GstBuffer * buffer, gpointer user_data)
{
  GstFlowReturn result;
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;

  rtpsession = GST_RTP_SESSION (user_data);
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "sending RTP packet");

  if (rtpsession->send_rtp_src) {
    result = gst_pad_push (rtpsession->send_rtp_src, buffer);
  } else {
    gst_buffer_unref (buffer);
    result = GST_FLOW_OK;
  }
  return result;
}

/* called when the session manager has an RTCP packet ready for further
 * sending */
static GstFlowReturn
gst_rtp_session_send_rtcp (RTPSession * sess, RTPSource * src,
    GstBuffer * buffer, gpointer user_data)
{
  GstFlowReturn result;
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;

  rtpsession = GST_RTP_SESSION (user_data);
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "sending RTCP");

  if (rtpsession->send_rtcp_src) {
    result = gst_pad_push (rtpsession->send_rtcp_src, buffer);
  } else {
    gst_buffer_unref (buffer);
    result = GST_FLOW_OK;
  }
  return result;
}


/* called when the session manager needs the clock rate */
static gint
gst_rtp_session_clock_rate (RTPSession * sess, guint8 payload,
    gpointer user_data)
{
  gint result = -1;
  GstRTPSession *rtpsession;
  GValue ret = { 0 };
  GValue args[2] = { {0}, {0} };
  GstCaps *caps;
  const GstStructure *caps_struct;

  rtpsession = GST_RTP_SESSION_CAST (user_data);

  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], rtpsession);
  g_value_init (&args[1], G_TYPE_UINT);
  g_value_set_uint (&args[1], payload);

  g_value_init (&ret, GST_TYPE_CAPS);
  g_value_set_boxed (&ret, NULL);

  g_signal_emitv (args, gst_rtp_session_signals[SIGNAL_REQUEST_PT_MAP], 0,
      &ret);

  caps = (GstCaps *) g_value_get_boxed (&ret);
  if (!caps)
    goto no_caps;

  caps_struct = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (caps_struct, "clock-rate", &result))
    goto no_clock_rate;

  GST_DEBUG_OBJECT (rtpsession, "parsed clock-rate %d", result);

  return result;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (rtpsession, "could not get caps");
    return -1;
  }
no_clock_rate:
  {
    GST_DEBUG_OBJECT (rtpsession, "could not clock-rate from caps");
    return -1;
  }
}

/* called when the session manager needs the time of clock */
static GstClockTime
gst_rtp_session_get_time (RTPSession * sess, gpointer user_data)
{
  GstClockTime result;
  GstRTPSession *rtpsession;
  GstClock *clock;

  rtpsession = GST_RTP_SESSION_CAST (user_data);

  clock = gst_element_get_clock (GST_ELEMENT_CAST (rtpsession));
  if (clock) {
    result = gst_clock_get_time (clock);
    gst_object_unref (clock);
  } else
    result = GST_CLOCK_TIME_NONE;

  return result;
}

/* called when the session manager asks us to reconsider the timeout */
static void
gst_rtp_session_reconsider (RTPSession * sess, gpointer user_data)
{
  GstRTPSession *rtpsession;

  rtpsession = GST_RTP_SESSION_CAST (user_data);

  GST_RTP_SESSION_LOCK (rtpsession);
  GST_DEBUG_OBJECT (rtpsession, "unlock timer for reconsideration");
  if (rtpsession->priv->id)
    gst_clock_id_unschedule (rtpsession->priv->id);
  GST_RTP_SESSION_UNLOCK (rtpsession);
}

static GstFlowReturn
gst_rtp_session_event_recv_rtp_sink (GstPad * pad, GstEvent * event)
{
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received event %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    default:
      ret = gst_pad_push_event (rtpsession->recv_rtp_src, event);
      break;
  }
  gst_object_unref (rtpsession);

  return ret;
}

/* receive a packet from a sender, send it to the RTP session manager and
 * forward the packet on the rtp_src pad
 */
static GstFlowReturn
gst_rtp_session_chain_recv_rtp (GstPad * pad, GstBuffer * buffer)
{
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;
  GstFlowReturn ret;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received RTP packet");

  ret = rtp_session_process_rtp (priv->session, buffer);

  gst_object_unref (rtpsession);

  return ret;
}

static GstFlowReturn
gst_rtp_session_event_recv_rtcp_sink (GstPad * pad, GstEvent * event)
{
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received event %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    default:
      ret = gst_pad_push_event (rtpsession->sync_src, event);
      break;
  }
  gst_object_unref (rtpsession);

  return ret;
}

/* Receive an RTCP packet from a sender, send it to the RTP session manager and
 * forward the SR packets to the sync_src pad.
 */
static GstFlowReturn
gst_rtp_session_chain_recv_rtcp (GstPad * pad, GstBuffer * buffer)
{
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;
  GstFlowReturn ret;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received RTCP packet");

  ret = rtp_session_process_rtcp (priv->session, buffer);

  gst_object_unref (rtpsession);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_rtp_session_event_send_rtp_sink (GstPad * pad, GstEvent * event)
{
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received event");

  switch (GST_EVENT_TYPE (event)) {
    default:
      ret = gst_pad_push_event (rtpsession->send_rtp_src, event);
      break;
  }
  gst_object_unref (rtpsession);

  return ret;
}

/* Recieve an RTP packet to be send to the receivers, send to RTP session
 * manager and forward to send_rtp_src.
 */
static GstFlowReturn
gst_rtp_session_chain_send_rtp (GstPad * pad, GstBuffer * buffer)
{
  GstRTPSession *rtpsession;
  GstRTPSessionPrivate *priv;
  GstFlowReturn ret;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received RTP packet");

  ret = rtp_session_send_rtp (priv->session, buffer);

  gst_object_unref (rtpsession);

  return ret;
}


/* Create sinkpad to receive RTP packets from senders. This will also create a
 * srcpad for the RTP packets.
 */
static GstPad *
create_recv_rtp_sink (GstRTPSession * rtpsession)
{
  GST_DEBUG_OBJECT (rtpsession, "creating RTP sink pad");

  rtpsession->recv_rtp_sink =
      gst_pad_new_from_static_template (&rtpsession_recv_rtp_sink_template,
      "recv_rtp_sink");
  gst_pad_set_chain_function (rtpsession->recv_rtp_sink,
      gst_rtp_session_chain_recv_rtp);
  gst_pad_set_event_function (rtpsession->recv_rtp_sink,
      gst_rtp_session_event_recv_rtp_sink);
  gst_pad_set_active (rtpsession->recv_rtp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession),
      rtpsession->recv_rtp_sink);

  GST_DEBUG_OBJECT (rtpsession, "creating RTP src pad");
  rtpsession->recv_rtp_src =
      gst_pad_new_from_static_template (&rtpsession_recv_rtp_src_template,
      "recv_rtp_src");
  gst_pad_set_active (rtpsession->recv_rtp_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession), rtpsession->recv_rtp_src);

  return rtpsession->recv_rtp_sink;
}

/* Create a sinkpad to receive RTCP messages from senders, this will also create a
 * sync_src pad for the SR packets.
 */
static GstPad *
create_recv_rtcp_sink (GstRTPSession * rtpsession)
{
  GST_DEBUG_OBJECT (rtpsession, "creating RTCP sink pad");

  rtpsession->recv_rtcp_sink =
      gst_pad_new_from_static_template (&rtpsession_recv_rtcp_sink_template,
      "recv_rtcp_sink");
  gst_pad_set_chain_function (rtpsession->recv_rtcp_sink,
      gst_rtp_session_chain_recv_rtcp);
  gst_pad_set_event_function (rtpsession->recv_rtcp_sink,
      gst_rtp_session_event_recv_rtcp_sink);
  gst_pad_set_active (rtpsession->recv_rtcp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession),
      rtpsession->recv_rtcp_sink);

  GST_DEBUG_OBJECT (rtpsession, "creating sync src pad");
  rtpsession->sync_src =
      gst_pad_new_from_static_template (&rtpsession_sync_src_template,
      "sync_src");
  gst_pad_set_active (rtpsession->sync_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession), rtpsession->sync_src);

  return rtpsession->recv_rtcp_sink;
}

/* Create a sinkpad to receive RTP packets for receivers. This will also create a
 * send_rtp_src pad.
 */
static GstPad *
create_send_rtp_sink (GstRTPSession * rtpsession)
{
  GST_DEBUG_OBJECT (rtpsession, "creating pad");

  rtpsession->send_rtp_sink =
      gst_pad_new_from_static_template (&rtpsession_send_rtp_sink_template,
      "send_rtp_sink");
  gst_pad_set_chain_function (rtpsession->send_rtp_sink,
      gst_rtp_session_chain_send_rtp);
  gst_pad_set_event_function (rtpsession->send_rtp_sink,
      gst_rtp_session_event_send_rtp_sink);
  gst_pad_set_active (rtpsession->send_rtp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession),
      rtpsession->send_rtp_sink);

  rtpsession->send_rtp_src =
      gst_pad_new_from_static_template (&rtpsession_send_rtp_src_template,
      "send_rtp_src");
  gst_pad_set_active (rtpsession->send_rtp_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession), rtpsession->send_rtp_src);

  return rtpsession->send_rtp_sink;
}

/* Create a srcpad with the RTCP packets to send out.
 * This pad will be driven by the RTP session manager when it wants to send out
 * RTCP packets.
 */
static GstPad *
create_send_rtcp_src (GstRTPSession * rtpsession)
{
  GST_DEBUG_OBJECT (rtpsession, "creating pad");

  rtpsession->send_rtcp_src =
      gst_pad_new_from_static_template (&rtpsession_send_rtcp_src_template,
      "send_rtcp_src");
  gst_pad_set_active (rtpsession->send_rtcp_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession),
      rtpsession->send_rtcp_src);

  return rtpsession->send_rtcp_src;
}

static GstPad *
gst_rtp_session_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstRTPSession *rtpsession;
  GstElementClass *klass;
  GstPad *result;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_SESSION (element), NULL);

  rtpsession = GST_RTP_SESSION (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  GST_DEBUG_OBJECT (element, "requesting pad %s", GST_STR_NULL (name));

  GST_RTP_SESSION_LOCK (rtpsession);

  /* figure out the template */
  if (templ == gst_element_class_get_pad_template (klass, "recv_rtp_sink")) {
    if (rtpsession->recv_rtp_sink != NULL)
      goto exists;

    result = create_recv_rtp_sink (rtpsession);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "recv_rtcp_sink")) {
    if (rtpsession->recv_rtcp_sink != NULL)
      goto exists;

    result = create_recv_rtcp_sink (rtpsession);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "send_rtp_sink")) {
    if (rtpsession->send_rtp_sink != NULL)
      goto exists;

    result = create_send_rtp_sink (rtpsession);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "send_rtcp_src")) {
    if (rtpsession->send_rtcp_src != NULL)
      goto exists;

    result = create_send_rtcp_src (rtpsession);
  } else
    goto wrong_template;

  GST_RTP_SESSION_UNLOCK (rtpsession);

  return result;

  /* ERRORS */
wrong_template:
  {
    GST_RTP_SESSION_UNLOCK (rtpsession);
    g_warning ("rtpsession: this is not our template");
    return NULL;
  }
exists:
  {
    GST_RTP_SESSION_UNLOCK (rtpsession);
    g_warning ("rtpsession: pad already requested");
    return NULL;
  }
}

static void
gst_rtp_session_release_pad (GstElement * element, GstPad * pad)
{
}
