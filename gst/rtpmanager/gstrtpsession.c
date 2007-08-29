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
 * SECTION:element-gstrtpsession
 * @short_description: an RTP session manager
 * @see_also: gstrtpjitterbuffer, gstrtpbin, gstrtpptdemux, gstrtpssrcdemux
 *
 * <refsect2>
 * <para>
 * The RTP session manager models one participant with a unique SSRC in an RTP
 * session. This session can be used to send and receive RTP and RTCP packets.
 * Based on what REQUEST pads are requested from the session manager, specific
 * functionality can be activated.
 * </para>
 * <para>
 * The session manager currently implements RFC 3550 including:
 * <itemizedlist>
 *   <listitem>
 *     <para>RTP packet validation based on consecutive sequence numbers.</para>
 *   </listitem>
 *   <listitem>
 *     <para>Maintainance of the SSRC participant database.</para>
 *   </listitem>
 *   <listitem>
 *     <para>Keeping per participant statistics based on received RTCP packets.</para>
 *   </listitem>
 *   <listitem>
 *     <para>Scheduling of RR/SR RTCP packets.</para>
 *   </listitem>
 * </itemizedlist>
 * </para>
 * <para>
 * The gstrtpsession will not demux packets based on SSRC or payload type, nor will
 * it correct for packet reordering and jitter. Use gstrtpssrcdemux, gstrtpptdemux and
 * gstrtpjitterbuffer in addition to gstrtpsession to perform these tasks. It is
 * usually a good idea to use gstrtpbin, which combines all these features in one
 * element.
 * </para>
 * <para>
 * To use gstrtpsession as an RTP receiver, request a recv_rtp_sink pad, which will
 * automatically create recv_rtp_src pad. Data received on the recv_rtp_sink pad
 * will be processed in the session and after being validated forwarded on the
 * recv_rtp_src pad.
 * </para>
 * <para>
 * To also use gstrtpsession as an RTCP receiver, request a recv_rtcp_sink pad,
 * which will automatically create a sync_src pad. Packets received on the RTCP
 * pad will be used by the session manager to update the stats and database of
 * the other participants. SR packets will be forwarded on the sync_src pad
 * so that they can be used to perform inter-stream synchronisation when needed.
 * </para>
 * <para>
 * If you want the session manager to generate and send RTCP packets, request
 * the send_rtcp_src pad. Packet pushed on this pad contain SR/RR RTCP reports
 * that should be sent to all participants in the session.
 * </para>
 * <para>
 * To use gstrtpsession as a sender, request a send_rtp_sink pad, which will
 * automatically create a send_rtp_src pad. The session manager will modify the
 * SSRC in the RTP packets to its own SSRC and wil forward the packets on the
 * send_rtp_src pad after updating its internal state.
 * </para>
 * <para>
 * The session manager needs the clock-rate of the payload types it is handling
 * and will signal the GstRtpSession::request-pt-map signal when it needs such a
 * mapping. One can clear the cached values with the GstRtpSession::clear-pt-map
 * signal.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch udpsrc port=5000 caps="application/x-rtp, ..." ! .recv_rtp_sink gstrtpsession .recv_rtp_src ! rtptheoradepay ! theoradec ! xvimagesink
 * </programlisting>
 * Receive theora RTP packets from port 5000 and send them to the depayloader,
 * decoder and display. Note that the application/x-rtp caps on udpsrc should be
 * configured based on some negotiation process such as RTSP for this pipeline
 * to work correctly.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch udpsrc port=5000 caps="application/x-rtp, ..." ! .recv_rtp_sink gstrtpsession name=session \
 *        .recv_rtp_src ! rtptheoradepay ! theoradec ! xvimagesink \
 *     udpsrc port=5001 caps="application/x-rtcp" ! session.recv_rtcp_sink
 * </programlisting>
 * Receive theora RTP packets from port 5000 and send them to the depayloader,
 * decoder and display. Receive RTCP packets from port 5001 and process them in
 * the session manager.
 * Note that the application/x-rtp caps on udpsrc should be
 * configured based on some negotiation process such as RTSP for this pipeline
 * to work correctly.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch videotestsrc ! theoraenc ! rtptheorapay ! .send_rtp_sink gstrtpsession .send_rtp_src ! udpsink port=5000
 * </programlisting>
 * Send theora RTP packets through the session manager and out on UDP port 5000.
 * </para>
 * <para>
 * <programlisting>
 * gst-launch videotestsrc ! theoraenc ! rtptheorapay ! .send_rtp_sink gstrtpsession name=session .send_rtp_src \
 *     ! udpsink port=5000  session.send_rtcp_src ! udpsink port=5001
 * </programlisting>
 * Send theora RTP packets through the session manager and out on UDP port 5000.
 * Send RTCP packets on port 5001. Note that this pipeline will not preroll
 * correctly because the second udpsink will not preroll correctly (no RTCP
 * packets are sent in the PAUSED state). Applications should manually set and
 * keep (see #gst_element_set_locked_state()) the RTCP udpsink to the PLAYING state.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-05-28 (0.10.5)
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
    "Filter/Network/RTP",
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
  SIGNAL_CLEAR_PT_MAP,

  SIGNAL_ON_NEW_SSRC,
  SIGNAL_ON_SSRC_COLLISION,
  SIGNAL_ON_SSRC_VALIDATED,
  SIGNAL_ON_BYE_SSRC,
  SIGNAL_ON_BYE_TIMEOUT,
  SIGNAL_ON_TIMEOUT,
  LAST_SIGNAL
};

enum
{
  PROP_0
};

#define GST_RTP_SESSION_GET_PRIVATE(obj)  \
	   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTP_SESSION, GstRtpSessionPrivate))

#define GST_RTP_SESSION_LOCK(sess)   g_mutex_lock ((sess)->priv->lock)
#define GST_RTP_SESSION_UNLOCK(sess) g_mutex_unlock ((sess)->priv->lock)

struct _GstRtpSessionPrivate
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

static void gst_rtp_session_clear_pt_map (GstRtpSession * rtpsession);

static guint gst_rtp_session_signals[LAST_SIGNAL] = { 0 };

static void
on_new_ssrc (RTPSession * session, RTPSource * src, GstRtpSession * sess)
{
  g_signal_emit (sess, gst_rtp_session_signals[SIGNAL_ON_NEW_SSRC], 0,
      src->ssrc);
}

static void
on_ssrc_collision (RTPSession * session, RTPSource * src, GstRtpSession * sess)
{
  g_signal_emit (sess, gst_rtp_session_signals[SIGNAL_ON_SSRC_COLLISION], 0,
      src->ssrc);
}

static void
on_ssrc_validated (RTPSession * session, RTPSource * src, GstRtpSession * sess)
{
  g_signal_emit (sess, gst_rtp_session_signals[SIGNAL_ON_SSRC_VALIDATED], 0,
      src->ssrc);
}

static void
on_bye_ssrc (RTPSession * session, RTPSource * src, GstRtpSession * sess)
{
  g_signal_emit (sess, gst_rtp_session_signals[SIGNAL_ON_BYE_SSRC], 0,
      src->ssrc);
}

static void
on_bye_timeout (RTPSession * session, RTPSource * src, GstRtpSession * sess)
{
  g_signal_emit (sess, gst_rtp_session_signals[SIGNAL_ON_BYE_TIMEOUT], 0,
      src->ssrc);
}

static void
on_timeout (RTPSession * session, RTPSource * src, GstRtpSession * sess)
{
  g_signal_emit (sess, gst_rtp_session_signals[SIGNAL_ON_TIMEOUT], 0,
      src->ssrc);
}

GST_BOILERPLATE (GstRtpSession, gst_rtp_session, GstElement, GST_TYPE_ELEMENT);

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
gst_rtp_session_class_init (GstRtpSessionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRtpSessionPrivate));

  gobject_class->finalize = gst_rtp_session_finalize;
  gobject_class->set_property = gst_rtp_session_set_property;
  gobject_class->get_property = gst_rtp_session_get_property;

  /**
   * GstRtpSession::request-pt-map:
   * @sess: the object which received the signal
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt.
   */
  gst_rtp_session_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpSessionClass, request_pt_map),
      NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT, GST_TYPE_CAPS, 1,
      G_TYPE_UINT);
  /**
   * GstRtpSession::clear-pt-map:
   * @sess: the object which received the signal
   *
   * Clear the cached pt-maps requested with GstRtpSession::request-pt-map.
   */
  gst_rtp_session_signals[SIGNAL_CLEAR_PT_MAP] =
      g_signal_new ("clear-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_ACTION, G_STRUCT_OFFSET (GstRtpSessionClass, clear_pt_map),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0, G_TYPE_NONE);

  /**
   * GstRtpSession::on-new-ssrc:
   * @sess: the object which received the signal
   * @ssrc: the SSRC 
   *
   * Notify of a new SSRC that entered @session.
   */
  gst_rtp_session_signals[SIGNAL_ON_NEW_SSRC] =
      g_signal_new ("on-new-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpSessionClass, on_new_ssrc),
      NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
  /**
   * GstRtpSession::on-ssrc_collision:
   * @sess: the object which received the signal
   * @ssrc: the SSRC 
   *
   * Notify when we have an SSRC collision
   */
  gst_rtp_session_signals[SIGNAL_ON_SSRC_COLLISION] =
      g_signal_new ("on-ssrc-collision", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpSessionClass,
          on_ssrc_collision), NULL, NULL, g_cclosure_marshal_VOID__UINT,
      G_TYPE_NONE, 1, G_TYPE_UINT);
  /**
   * GstRtpSession::on-ssrc_validated:
   * @sess: the object which received the signal
   * @ssrc: the SSRC 
   *
   * Notify of a new SSRC that became validated.
   */
  gst_rtp_session_signals[SIGNAL_ON_SSRC_VALIDATED] =
      g_signal_new ("on-ssrc-validated", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpSessionClass,
          on_ssrc_validated), NULL, NULL, g_cclosure_marshal_VOID__UINT,
      G_TYPE_NONE, 1, G_TYPE_UINT);

  /**
   * GstRtpSession::on-bye-ssrc:
   * @sess: the object which received the signal
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that became inactive because of a BYE packet.
   */
  gst_rtp_session_signals[SIGNAL_ON_BYE_SSRC] =
      g_signal_new ("on-bye-ssrc", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpSessionClass, on_bye_ssrc),
      NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
  /**
   * GstRtpSession::on-bye-timeout:
   * @sess: the object which received the signal
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that has timed out because of BYE
   */
  gst_rtp_session_signals[SIGNAL_ON_BYE_TIMEOUT] =
      g_signal_new ("on-bye-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpSessionClass, on_bye_timeout),
      NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);
  /**
   * GstRtpSession::on-timeout:
   * @sess: the object which received the signal
   * @ssrc: the SSRC 
   *
   * Notify of an SSRC that has timed out
   */
  gst_rtp_session_signals[SIGNAL_ON_TIMEOUT] =
      g_signal_new ("on-timeout", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRtpSessionClass, on_timeout),
      NULL, NULL, g_cclosure_marshal_VOID__UINT, G_TYPE_NONE, 1, G_TYPE_UINT);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_session_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_session_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_session_release_pad);

  klass->clear_pt_map = GST_DEBUG_FUNCPTR (gst_rtp_session_clear_pt_map);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_session_debug,
      "rtpsession", 0, "RTP Session");
}

static void
gst_rtp_session_init (GstRtpSession * rtpsession, GstRtpSessionClass * klass)
{
  rtpsession->priv = GST_RTP_SESSION_GET_PRIVATE (rtpsession);
  rtpsession->priv->lock = g_mutex_new ();
  rtpsession->priv->session = rtp_session_new ();
  /* configure callbacks */
  rtp_session_set_callbacks (rtpsession->priv->session, &callbacks, rtpsession);
  /* configure signals */
  g_signal_connect (rtpsession->priv->session, "on-new-ssrc",
      (GCallback) on_new_ssrc, rtpsession);
  g_signal_connect (rtpsession->priv->session, "on-ssrc-collision",
      (GCallback) on_ssrc_collision, rtpsession);
  g_signal_connect (rtpsession->priv->session, "on-ssrc-validated",
      (GCallback) on_ssrc_validated, rtpsession);
  g_signal_connect (rtpsession->priv->session, "on-bye-ssrc",
      (GCallback) on_bye_ssrc, rtpsession);
  g_signal_connect (rtpsession->priv->session, "on-bye-timeout",
      (GCallback) on_bye_timeout, rtpsession);
  g_signal_connect (rtpsession->priv->session, "on-timeout",
      (GCallback) on_timeout, rtpsession);
}

static void
gst_rtp_session_finalize (GObject * object)
{
  GstRtpSession *rtpsession;

  rtpsession = GST_RTP_SESSION (object);
  g_mutex_free (rtpsession->priv->lock);
  g_object_unref (rtpsession->priv->session);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_session_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpSession *rtpsession;

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
  GstRtpSession *rtpsession;

  rtpsession = GST_RTP_SESSION (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
rtcp_thread (GstRtpSession * rtpsession)
{
  GstClock *clock;
  GstClockID id;
  GstClockTime current_time;
  GstClockTime next_timeout;

  /* RTCP timeouts we use the system clock */
  clock = gst_system_clock_obtain ();
  if (clock == NULL)
    goto no_clock;

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

    /* perform actions, we ignore result. Release lock because it might push. */
    GST_RTP_SESSION_UNLOCK (rtpsession);
    rtp_session_on_timeout (rtpsession->priv->session, current_time);
    GST_RTP_SESSION_LOCK (rtpsession);
  }
  GST_RTP_SESSION_UNLOCK (rtpsession);

  gst_object_unref (clock);

  GST_DEBUG_OBJECT (rtpsession, "leaving RTCP thread");
  return;

  /* ERRORS */
no_clock:
  {
    GST_ELEMENT_ERROR (rtpsession, CORE, CLOCK, (NULL),
        ("Could not get system clock"));
    return;
  }
}

static gboolean
start_rtcp_thread (GstRtpSession * rtpsession)
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
stop_rtcp_thread (GstRtpSession * rtpsession)
{
  GST_DEBUG_OBJECT (rtpsession, "stopping RTCP thread");

  GST_RTP_SESSION_LOCK (rtpsession);
  rtpsession->priv->stop_thread = TRUE;
  if (rtpsession->priv->id)
    gst_clock_id_unschedule (rtpsession->priv->id);
  GST_RTP_SESSION_UNLOCK (rtpsession);

  /* FIXME, can deadlock because the thread might be blocked in a push */
  g_thread_join (rtpsession->priv->thread);
}

static GstStateChangeReturn
gst_rtp_session_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;

  rtpsession = GST_RTP_SESSION (element);
  priv = rtpsession->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      stop_rtcp_thread (rtpsession);
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GstClockTime base_time;

      base_time = GST_ELEMENT_CAST (rtpsession)->base_time;

      rtp_session_set_base_time (priv->session, base_time);

      if (!start_rtcp_thread (rtpsession))
        goto failed_thread;
      break;
    }
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

static void
gst_rtp_session_clear_pt_map (GstRtpSession * rtpsession)
{
  /* FIXME, do something */
}

/* called when the session manager has an RTP packet ready for further
 * processing */
static GstFlowReturn
gst_rtp_session_process_rtp (RTPSession * sess, RTPSource * src,
    GstBuffer * buffer, gpointer user_data)
{
  GstFlowReturn result;
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;

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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;

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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;

  rtpsession = GST_RTP_SESSION (user_data);
  priv = rtpsession->priv;

  if (rtpsession->send_rtcp_src) {
    GST_DEBUG_OBJECT (rtpsession, "sending RTCP");
    result = gst_pad_push (rtpsession->send_rtcp_src, buffer);
  } else {
    GST_DEBUG_OBJECT (rtpsession, "not sending RTCP, no output pad");
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
  GstRtpSession *rtpsession;
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
  GstRtpSession *rtpsession;
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
  GstRtpSession *rtpsession;

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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;
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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;
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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received event %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    default:
      if (rtpsession->send_rtcp_src) {
        gst_event_ref (event);
        ret = gst_pad_push_event (rtpsession->send_rtcp_src, event);
      }
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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;
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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));
  priv = rtpsession->priv;

  GST_DEBUG_OBJECT (rtpsession, "received event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, arate;
      GstFormat format;
      gint64 start, stop, time;
      GstSegment *segment;

      segment = &rtpsession->send_rtp_seg;

      /* the newsegment event is needed to convert the RTP timestamp to
       * running_time, which is needed to generate a mapping from RTP to NTP
       * timestamps in SR reports */
      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      GST_DEBUG_OBJECT (rtpsession,
          "configured NEWSEGMENT update %d, rate %lf, applied rate %lf, "
          "format GST_FORMAT_TIME, "
          "%" GST_TIME_FORMAT " -- %" GST_TIME_FORMAT
          ", time %" GST_TIME_FORMAT ", accum %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (segment->start),
          GST_TIME_ARGS (segment->stop), GST_TIME_ARGS (segment->time),
          GST_TIME_ARGS (segment->accum));

      gst_segment_set_newsegment_full (segment, update, rate,
          arate, format, start, stop, time);

      rtp_session_set_timestamp_sync (priv->session, start);

      /* push event forward */
      ret = gst_pad_push_event (rtpsession->send_rtp_src, event);
      break;
    }
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
  GstRtpSession *rtpsession;
  GstRtpSessionPrivate *priv;
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
create_recv_rtp_sink (GstRtpSession * rtpsession)
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
create_recv_rtcp_sink (GstRtpSession * rtpsession)
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
create_send_rtp_sink (GstRtpSession * rtpsession)
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
create_send_rtcp_src (GstRtpSession * rtpsession)
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
  GstRtpSession *rtpsession;
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
    g_warning ("gstrtpsession: this is not our template");
    return NULL;
  }
exists:
  {
    GST_RTP_SESSION_UNLOCK (rtpsession);
    g_warning ("gstrtpsession: pad already requested");
    return NULL;
  }
}

static void
gst_rtp_session_release_pad (GstElement * element, GstPad * pad)
{
}
