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
 * SECTION:element-rtpbin
 * @short_description: handle media from one RTP bin
 * @see_also: rtpjitterbuffer, rtpclient, rtpsession
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
#include <string.h>

#include "gstrtpbin-marshal.h"
#include "gstrtpbin.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_bin_debug);
#define GST_CAT_DEFAULT gst_rtp_bin_debug


/* elementfactory information */
static const GstElementDetails rtpbin_details = GST_ELEMENT_DETAILS ("RTP Bin",
    "Filter/Editor/Video",
    "Implement an RTP bin",
    "Wim Taymans <wim@fluendo.com>");

/* sink pads */
static GstStaticPadTemplate rtpbin_recv_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpbin_recv_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtcp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtpbin_send_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("send_rtp_sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

/* src pads */
static GstStaticPadTemplate rtpbin_recv_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("recv_rtp_src_%d_%d_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate rtpbin_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src_%d",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate rtpbin_send_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("send_rtp_src_%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp")
    );

#define GST_RTP_BIN_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTP_BIN, GstRTPBinPrivate))

#define GST_RTP_BIN_LOCK(bin)   g_mutex_lock ((bin)->priv->bin_lock)
#define GST_RTP_BIN_UNLOCK(bin) g_mutex_unlock ((bin)->priv->bin_lock)

struct _GstRTPBinPrivate
{
  GMutex *bin_lock;
};

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

/* helper objects */
typedef struct _GstRTPBinSession GstRTPBinSession;
typedef struct _GstRTPBinStream GstRTPBinStream;
typedef struct _GstRTPBinClient GstRTPBinClient;

static guint gst_rtp_bin_signals[LAST_SIGNAL] = { 0 };

static GstCaps *pt_map_requested (GstElement * element, guint pt,
    GstRTPBinSession * session);

/* Manages the RTP stream for one SSRC.
 *
 * We pipe the stream (comming from the SSRC demuxer) into a jitterbuffer.
 * If we see an SDES RTCP packet that links multiple SSRCs together based on a
 * common CNAME, we create a GstRTPBinClient structure to group the SSRCs
 * together (see below).
 */
struct _GstRTPBinStream
{
  /* the SSRC of this stream */
  guint32 ssrc;
  /* parent bin */
  GstRTPBin *bin;
  /* the session this SSRC belongs to */
  GstRTPBinSession *session;
  /* the jitterbuffer of the SSRC */
  GstElement *buffer;
  /* the PT demuxer of the SSRC */
  GstElement *demux;
  gulong demux_newpad_sig;
  gulong demux_ptreq_sig;
};

#define GST_RTP_SESSION_LOCK(sess)   g_mutex_lock ((sess)->lock)
#define GST_RTP_SESSION_UNLOCK(sess) g_mutex_unlock ((sess)->lock)

/* Manages the receiving end of the packets.
 *
 * There is one such structure for each RTP session (audio/video/...).
 * We get the RTP/RTCP packets and stuff them into the session manager. From
 * there they are pushed into an SSRC demuxer that splits the stream based on
 * SSRC. Each of the SSRC streams go into their own jitterbuffer (managed with
 * the GstRTPBinStream above).
 */
struct _GstRTPBinSession
{
  /* session id */
  gint id;
  /* the parent bin */
  GstRTPBin *bin;
  /* the session element */
  GstElement *session;
  /* the SSRC demuxer */
  GstElement *demux;
  gulong demux_newpad_sig;

  GMutex *lock;

  /* list of GstRTPBinStream */
  GSList *streams;

  /* mapping of payload type to caps */
  GHashTable *ptmap;

  /* the pads of the session */
  GstPad *recv_rtp_sink;
  GstPad *recv_rtp_src;
  GstPad *recv_rtcp_sink;
  GstPad *recv_rtcp_src;
  GstPad *send_rtp_sink;
  GstPad *send_rtp_src;
  GstPad *rtcp_src;
};

/* find a session with the given id. Must be called with RTP_BIN_LOCK */
static GstRTPBinSession *
find_session_by_id (GstRTPBin * rtpbin, gint id)
{
  GSList *walk;

  for (walk = rtpbin->sessions; walk; walk = g_slist_next (walk)) {
    GstRTPBinSession *sess = (GstRTPBinSession *) walk->data;

    if (sess->id == id)
      return sess;
  }
  return NULL;
}

/* create a session with the given id.  Must be called with RTP_BIN_LOCK */
static GstRTPBinSession *
create_session (GstRTPBin * rtpbin, gint id)
{
  GstRTPBinSession *sess;
  GstElement *session, *demux;

  if (!(session = gst_element_factory_make ("rtpsession", NULL)))
    goto no_session;

  if (!(demux = gst_element_factory_make ("rtpssrcdemux", NULL)))
    goto no_demux;

  sess = g_new0 (GstRTPBinSession, 1);
  sess->lock = g_mutex_new ();
  sess->id = id;
  sess->bin = rtpbin;
  sess->session = session;
  sess->demux = demux;
  sess->ptmap = g_hash_table_new (NULL, NULL);
  rtpbin->sessions = g_slist_prepend (rtpbin->sessions, sess);

  /* provide clock_rate to the session manager when needed */
  g_signal_connect (session, "request-pt-map",
      (GCallback) pt_map_requested, sess);

  gst_bin_add (GST_BIN_CAST (rtpbin), session);
  gst_element_set_state (session, GST_STATE_PLAYING);
  gst_bin_add (GST_BIN_CAST (rtpbin), demux);
  gst_element_set_state (demux, GST_STATE_PLAYING);

  return sess;

  /* ERRORS */
no_session:
  {
    g_warning ("rtpbin: could not create rtpsession element");
    return NULL;
  }
no_demux:
  {
    gst_object_unref (session);
    g_warning ("rtpbin: could not create rtpssrcdemux element");
    return NULL;
  }
}

#if 0
static GstRTPBinStream *
find_stream_by_ssrc (GstRTPBinSession * session, guint32 ssrc)
{
  GSList *walk;

  for (walk = session->streams; walk; walk = g_slist_next (walk)) {
    GstRTPBinStream *stream = (GstRTPBinStream *) walk->data;

    if (stream->ssrc == ssrc)
      return stream;
  }
  return NULL;
}
#endif

/* get the payload type caps for the specific payload @pt in @session */
static GstCaps *
get_pt_map (GstRTPBinSession * session, guint pt)
{
  GstCaps *caps = NULL;
  GstRTPBin *bin;
  GValue ret = { 0 };
  GValue args[3] = { {0}, {0}, {0} };

  GST_DEBUG ("searching pt %d in cache", pt);

  GST_RTP_SESSION_LOCK (session);

  /* first look in the cache */
  caps = g_hash_table_lookup (session->ptmap, GINT_TO_POINTER (pt));
  if (caps)
    goto done;

  bin = session->bin;

  GST_DEBUG ("emiting signal for pt %d in session %d", pt, session->id);

  /* not in cache, send signal to request caps */
  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], bin);
  g_value_init (&args[1], G_TYPE_UINT);
  g_value_set_uint (&args[1], session->id);
  g_value_init (&args[2], G_TYPE_UINT);
  g_value_set_uint (&args[2], pt);

  g_value_init (&ret, GST_TYPE_CAPS);
  g_value_set_boxed (&ret, NULL);

  g_signal_emitv (args, gst_rtp_bin_signals[SIGNAL_REQUEST_PT_MAP], 0, &ret);

  caps = (GstCaps *) g_value_get_boxed (&ret);
  if (!caps)
    goto no_caps;

  GST_DEBUG ("caching pt %d as %" GST_PTR_FORMAT, pt, caps);

  /* store in cache */
  g_hash_table_insert (session->ptmap, GINT_TO_POINTER (pt), caps);

done:
  GST_RTP_SESSION_UNLOCK (session);

  return caps;

  /* ERRORS */
no_caps:
  {
    GST_RTP_SESSION_UNLOCK (session);
    GST_DEBUG ("no pt map could be obtained");
    return NULL;
  }
}

/* create a new stream with @ssrc in @session. Must be called with
 * RTP_SESSION_LOCK. */
static GstRTPBinStream *
create_stream (GstRTPBinSession * session, guint32 ssrc)
{
  GstElement *buffer, *demux;
  GstRTPBinStream *stream;

  if (!(buffer = gst_element_factory_make ("rtpjitterbuffer", NULL)))
    goto no_jitterbuffer;

  if (!(demux = gst_element_factory_make ("rtpptdemux", NULL)))
    goto no_demux;

  stream = g_new0 (GstRTPBinStream, 1);
  stream->ssrc = ssrc;
  stream->bin = session->bin;
  stream->session = session;
  stream->buffer = buffer;
  stream->demux = demux;
  session->streams = g_slist_prepend (session->streams, stream);

  /* provide clock_rate to the jitterbuffer when needed */
  g_signal_connect (buffer, "request-pt-map",
      (GCallback) pt_map_requested, session);

  gst_bin_add (GST_BIN_CAST (session->bin), buffer);
  gst_element_set_state (buffer, GST_STATE_PLAYING);
  gst_bin_add (GST_BIN_CAST (session->bin), demux);
  gst_element_set_state (demux, GST_STATE_PLAYING);

  /* link stuff */
  gst_element_link (buffer, demux);

  return stream;

  /* ERRORS */
no_jitterbuffer:
  {
    g_warning ("rtpbin: could not create rtpjitterbuffer element");
    return NULL;
  }
no_demux:
  {
    gst_object_unref (buffer);
    g_warning ("rtpbin: could not create rtpptdemux element");
    return NULL;
  }
}

/* Manages the RTP streams that come from one client and should therefore be
 * synchronized.
 */
struct _GstRTPBinClient
{
  /* the common CNAME for the streams */
  gchar *cname;
  /* the streams */
  GSList *streams;
};

/* GObject vmethods */
static void gst_rtp_bin_finalize (GObject * object);
static void gst_rtp_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* GstElement vmethods */
static GstClock *gst_rtp_bin_provide_clock (GstElement * element);
static GstStateChangeReturn gst_rtp_bin_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_rtp_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_bin_release_pad (GstElement * element, GstPad * pad);

GST_BOILERPLATE (GstRTPBin, gst_rtp_bin, GstBin, GST_TYPE_BIN);

static void
gst_rtp_bin_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* sink pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_recv_rtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_recv_rtcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_send_rtp_sink_template));

  /* src pads */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_recv_rtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_rtcp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtpbin_send_rtp_src_template));

  gst_element_class_set_details (element_class, &rtpbin_details);
}

static void
gst_rtp_bin_class_init (GstRTPBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstRTPBinPrivate));

  gobject_class->finalize = gst_rtp_bin_finalize;
  gobject_class->set_property = gst_rtp_bin_set_property;
  gobject_class->get_property = gst_rtp_bin_get_property;

  /**
   * GstRTPBin::request-pt-map:
   * @rtpbin: the object which received the signal
   * @session: the session
   * @pt: the pt
   *
   * Request the payload type as #GstCaps for @pt in @session.
   */
  gst_rtp_bin_signals[SIGNAL_REQUEST_PT_MAP] =
      g_signal_new ("request-pt-map", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstRTPBinClass, request_pt_map),
      NULL, NULL, gst_rtp_bin_marshal_BOXED__UINT_UINT, GST_TYPE_CAPS, 2,
      G_TYPE_UINT, G_TYPE_UINT);

  gstelement_class->provide_clock =
      GST_DEBUG_FUNCPTR (gst_rtp_bin_provide_clock);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rtp_bin_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_bin_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_rtp_bin_release_pad);

  GST_DEBUG_CATEGORY_INIT (gst_rtp_bin_debug, "rtpbin", 0, "RTP bin");
}

static void
gst_rtp_bin_init (GstRTPBin * rtpbin, GstRTPBinClass * klass)
{
  rtpbin->priv = GST_RTP_BIN_GET_PRIVATE (rtpbin);
  rtpbin->priv->bin_lock = g_mutex_new ();
  rtpbin->provided_clock = gst_system_clock_obtain ();
}

static void
gst_rtp_bin_finalize (GObject * object)
{
  GstRTPBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

  g_mutex_free (rtpbin->priv->bin_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtp_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTPBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstClock *
gst_rtp_bin_provide_clock (GstElement * element)
{
  GstRTPBin *rtpbin;

  rtpbin = GST_RTP_BIN (element);

  return GST_CLOCK_CAST (gst_object_ref (rtpbin->provided_clock));
}

static GstStateChangeReturn
gst_rtp_bin_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn res;
  GstRTPBin *rtpbin;

  rtpbin = GST_RTP_BIN (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
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
}

/* a new pad (SSRC) was created in @session */
static void
new_payload_found (GstElement * element, guint pt, GstPad * pad,
    GstRTPBinStream * stream)
{
  GstRTPBin *rtpbin;
  GstElementClass *klass;
  GstPadTemplate *templ;
  gchar *padname;
  GstPad *gpad;

  rtpbin = stream->bin;

  GST_DEBUG ("new payload pad %d", pt);

  /* ghost the pad to the parent */
  klass = GST_ELEMENT_GET_CLASS (rtpbin);
  templ = gst_element_class_get_pad_template (klass, "recv_rtp_src_%d_%d_%d");
  padname = g_strdup_printf ("recv_rtp_src_%d_%u_%d",
      stream->session->id, stream->ssrc, pt);
  gpad = gst_ghost_pad_new_from_template (padname, pad, templ);
  g_free (padname);

  gst_pad_set_active (gpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), gpad);
}

static GstCaps *
pt_map_requested (GstElement * element, guint pt, GstRTPBinSession * session)
{
  GstRTPBin *rtpbin;
  GstCaps *caps;

  rtpbin = session->bin;

  GST_DEBUG_OBJECT (rtpbin, "payload map requested for pt %d in session %d", pt,
      session->id);

  caps = get_pt_map (session, pt);
  if (!caps)
    goto no_caps;

  return caps;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (rtpbin, "could not get caps");
    return NULL;
  }
}

/* a new pad (SSRC) was created in @session */
static void
new_ssrc_pad_found (GstElement * element, guint ssrc, GstPad * pad,
    GstRTPBinSession * session)
{
  GstRTPBinStream *stream;
  GstPad *sinkpad;

  GST_DEBUG_OBJECT (session->bin, "new SSRC pad %08x", ssrc);

  GST_RTP_SESSION_LOCK (session);

  /* create new stream */
  stream = create_stream (session, ssrc);
  if (!stream)
    goto no_stream;

  /* get pad and link */
  GST_DEBUG_OBJECT (session->bin, "linking jitterbuffer");
  sinkpad = gst_element_get_static_pad (stream->buffer, "sink");
  gst_pad_link (pad, sinkpad);
  gst_object_unref (sinkpad);

  /* connect to the new-pad signal of the payload demuxer, this will expose the
   * new pad by ghosting it. */
  stream->demux_newpad_sig = g_signal_connect (stream->demux,
      "new-payload-type", (GCallback) new_payload_found, stream);
  /* connect to the request-pt-map signal. This signal will be emited by the
   * demuxer so that it can apply a proper caps on the buffers for the
   * depayloaders. */
  stream->demux_ptreq_sig = g_signal_connect (stream->demux,
      "request-pt-map", (GCallback) pt_map_requested, session);

  GST_RTP_SESSION_UNLOCK (session);

  return;

  /* ERRORS */
no_stream:
  {
    GST_RTP_SESSION_UNLOCK (session);
    GST_DEBUG ("could not create stream");
    return;
  }
}

/* Create a pad for receiving RTP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_recv_rtp (GstRTPBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result, *sinkdpad;
  guint sessid;
  GstRTPBinSession *session;
  GstPadLinkReturn lres;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtp_sink_%d", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rtpbin, "finding session %d", sessid);

  /* get or create session */
  session = find_session_by_id (rtpbin, sessid);
  if (!session) {
    GST_DEBUG_OBJECT (rtpbin, "creating session %d", sessid);
    /* create session now */
    session = create_session (rtpbin, sessid);
    if (session == NULL)
      goto create_error;
  }

  /* check if pad was requested */
  if (session->recv_rtp_sink != NULL)
    goto existed;

  GST_DEBUG_OBJECT (rtpbin, "getting RTP sink pad");
  /* get recv_rtp pad and store */
  session->recv_rtp_sink =
      gst_element_get_request_pad (session->session, "recv_rtp_sink");
  if (session->recv_rtp_sink == NULL)
    goto pad_failed;

  GST_DEBUG_OBJECT (rtpbin, "getting RTP src pad");
  /* get srcpad, link to SSRCDemux */
  session->recv_rtp_src =
      gst_element_get_static_pad (session->session, "recv_rtp_src");
  if (session->recv_rtp_src == NULL)
    goto pad_failed;

  GST_DEBUG_OBJECT (rtpbin, "getting demuxer sink pad");
  sinkdpad = gst_element_get_static_pad (session->demux, "sink");
  lres = gst_pad_link (session->recv_rtp_src, sinkdpad);
  gst_object_unref (sinkdpad);
  if (lres != GST_PAD_LINK_OK)
    goto link_failed;

  /* connect to the new-ssrc-pad signal of the SSRC demuxer */
  session->demux_newpad_sig = g_signal_connect (session->demux,
      "new-ssrc-pad", (GCallback) new_ssrc_pad_found, session);

  GST_DEBUG_OBJECT (rtpbin, "ghosting session sink pad");
  result =
      gst_ghost_pad_new_from_template (name, session->recv_rtp_sink, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("rtpbin: invalid name given");
    return NULL;
  }
create_error:
  {
    /* create_session already warned */
    return NULL;
  }
existed:
  {
    g_warning ("rtpbin: recv_rtp pad already requested for session %d", sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("rtpbin: failed to get session pad");
    return NULL;
  }
link_failed:
  {
    g_warning ("rtpbin: failed to link pads");
    return NULL;
  }
}

/* Create a pad for receiving RTCP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_recv_rtcp (GstRTPBin * rtpbin, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *result;
  guint sessid;
  GstRTPBinSession *session;

#if 0
  GstPad *sinkdpad;
  GstPadLinkReturn lres;
#endif

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtcp_sink_%d", &sessid) != 1)
    goto no_name;

  GST_DEBUG_OBJECT (rtpbin, "finding session %d", sessid);

  /* get the session, it must exist or we error */
  session = find_session_by_id (rtpbin, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->recv_rtcp_sink != NULL)
    goto existed;

  GST_DEBUG_OBJECT (rtpbin, "getting RTCP sink pad");

  /* get recv_rtp pad and store */
  session->recv_rtcp_sink =
      gst_element_get_request_pad (session->session, "recv_rtcp_sink");
  if (session->recv_rtcp_sink == NULL)
    goto pad_failed;

#if 0
  /* get srcpad, link to SSRCDemux */
  GST_DEBUG_OBJECT (rtpbin, "getting sync src pad");
  session->recv_rtcp_src =
      gst_element_get_static_pad (session->session, "sync_src");
  if (session->recv_rtcp_src == NULL)
    goto pad_failed;

  GST_DEBUG_OBJECT (rtpbin, "linking sync to demux");
  sinkdpad = gst_element_get_static_pad (session->demux, "sink");
  lres = gst_pad_link (session->recv_rtcp_src, sinkdpad);
  gst_object_unref (sinkdpad);
  if (lres != GST_PAD_LINK_OK)
    goto link_failed;
#endif

  result =
      gst_ghost_pad_new_from_template (name, session->recv_rtcp_sink, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("rtpbin: invalid name given");
    return NULL;
  }
no_session:
  {
    g_warning ("rtpbin: no session with id %d", sessid);
    return NULL;
  }
existed:
  {
    g_warning ("rtpbin: recv_rtcp pad already requested for session %d",
        sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("rtpbin: failed to get session pad");
    return NULL;
  }
#if 0
link_failed:
  {
    g_warning ("rtpbin: failed to link pads");
    return NULL;
  }
#endif
}

/* Create a pad for sending RTP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_send_rtp (GstRTPBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result, *srcpad, *srcghost;
  gchar *gname;
  guint sessid;
  GstRTPBinSession *session;
  GstElementClass *klass;

  /* first get the session number */
  if (name == NULL || sscanf (name, "send_rtp_sink_%d", &sessid) != 1)
    goto no_name;

  /* get or create session */
  session = find_session_by_id (rtpbin, sessid);
  if (!session) {
    /* create session now */
    session = create_session (rtpbin, sessid);
    if (session == NULL)
      goto create_error;
  }

  /* check if pad was requested */
  if (session->send_rtp_sink != NULL)
    goto existed;

  /* get recv_rtp pad and store */
  session->send_rtp_sink =
      gst_element_get_request_pad (session->session, "send_rtp_sink");
  if (session->send_rtp_sink == NULL)
    goto pad_failed;

  result =
      gst_ghost_pad_new_from_template (name, session->send_rtp_sink, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  /* get srcpad */
  srcpad = gst_element_get_pad (session->session, "send_rtp_src");
  if (srcpad == NULL)
    goto no_srcpad;

  /* ghost the new source pad */
  klass = GST_ELEMENT_GET_CLASS (rtpbin);
  gname = g_strdup_printf ("send_rtp_src_%d", sessid);
  templ = gst_element_class_get_pad_template (klass, "send_rtp_src_%d");
  srcghost =
      gst_ghost_pad_new_from_template (gname, session->send_rtp_sink, templ);
  gst_pad_set_active (srcghost, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), srcghost);
  g_free (gname);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("rtpbin: invalid name given");
    return NULL;
  }
create_error:
  {
    /* create_session already warned */
    return NULL;
  }
existed:
  {
    g_warning ("rtpbin: send_rtp pad already requested for session %d", sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("rtpbin: failed to get session pad for session %d", sessid);
    return NULL;
  }
no_srcpad:
  {
    g_warning ("rtpbin: failed to get rtp source pad for session %d", sessid);
    return NULL;
  }
}

/* Create a pad for sending RTCP for the session in @name. Must be called with
 * RTP_BIN_LOCK.
 */
static GstPad *
create_rtcp (GstRTPBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result;
  guint sessid;
  GstRTPBinSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "rtcp_src_%d", &sessid) != 1)
    goto no_name;

  /* get or create session */
  session = find_session_by_id (rtpbin, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->rtcp_src != NULL)
    goto existed;

  /* get rtcp_src pad and store */
  session->rtcp_src =
      gst_element_get_request_pad (session->session, "send_rtcp_src");
  if (session->rtcp_src == NULL)
    goto pad_failed;

  result = gst_ghost_pad_new_from_template (name, session->rtcp_src, templ);
  gst_pad_set_active (result, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  return result;

  /* ERRORS */
no_name:
  {
    g_warning ("rtpbin: invalid name given");
    return NULL;
  }
no_session:
  {
    g_warning ("rtpbin: session with id %d does not exist", sessid);
    return NULL;
  }
existed:
  {
    g_warning ("rtpbin: rtcp_src pad already requested for session %d", sessid);
    return NULL;
  }
pad_failed:
  {
    g_warning ("rtpbin: failed to get rtcp pad for session %d", sessid);
    return NULL;
  }
}

/* 
 */
static GstPad *
gst_rtp_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstRTPBin *rtpbin;
  GstElementClass *klass;
  GstPad *result;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_BIN (element), NULL);

  rtpbin = GST_RTP_BIN (element);
  klass = GST_ELEMENT_GET_CLASS (element);

  GST_RTP_BIN_LOCK (rtpbin);

  /* figure out the template */
  if (templ == gst_element_class_get_pad_template (klass, "recv_rtp_sink_%d")) {
    result = create_recv_rtp (rtpbin, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "recv_rtcp_sink_%d")) {
    result = create_recv_rtcp (rtpbin, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass,
          "send_rtp_sink_%d")) {
    result = create_send_rtp (rtpbin, templ, name);
  } else if (templ == gst_element_class_get_pad_template (klass, "rtcp_src_%d")) {
    result = create_rtcp (rtpbin, templ, name);
  } else
    goto wrong_template;

  GST_RTP_BIN_UNLOCK (rtpbin);

  return result;

  /* ERRORS */
wrong_template:
  {
    GST_RTP_BIN_UNLOCK (rtpbin);
    g_warning ("rtpbin: this is not our template");
    return NULL;
  }
}

static void
gst_rtp_bin_release_pad (GstElement * element, GstPad * pad)
{
}
