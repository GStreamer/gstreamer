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

#include "gstrtpbin.h"

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

struct _GstRTPBinPrivate
{
};

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

/* helper objects */
typedef struct
{
  /* session id */
  gint id;
  /* the session element */
  GstElement *session;
  /* the SSRC demuxer */
  GstElement *ssrcdemux;

  /* the pads of the session */
  GstPad *recv_rtp_sink;
  GstPad *recv_rtcp_sink;
  GstPad *send_rtp_sink;
  GstPad *rtcp_src;

} GstRTPBinSession;

/* find a session with the given id */
static GstRTPBinSession *
find_session_by_id (GstRTPBin * rtpbin, gint id)
{
  GList *walk;

  for (walk = rtpbin->sessions; walk; walk = g_list_next (walk)) {
    GstRTPBinSession *sess = (GstRTPBinSession *) walk->data;

    if (sess->id == id)
      return sess;
  }
  return NULL;
}

/* create a session with the given id */
static GstRTPBinSession *
create_session (GstRTPBin * rtpbin, gint id)
{
  GstRTPBinSession *sess;
  GstElement *elem;

  if (!(elem = gst_element_factory_make ("rtpsession", NULL)))
    goto no_session;

  sess = g_new0 (GstRTPBinSession, 1);
  sess->id = id;
  sess->session = elem;

  return sess;

  /* ERRORS */
no_session:
  {
    g_warning ("rtpbin: could not create rtpsession element");
    return NULL;
  }
}

/* GObject vmethods */
static void gst_rtp_bin_finalize (GObject * object);
static void gst_rtp_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* GstElement vmethods */
static GstStateChangeReturn gst_rtp_bin_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_rtp_bin_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_rtp_bin_release_pad (GstElement * element, GstPad * pad);

/*static guint gst_rtp_bin_signals[LAST_SIGNAL] = { 0 }; */

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

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rtp_bin_change_state);
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_bin_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_rtp_bin_release_pad);
}

static void
gst_rtp_bin_init (GstRTPBin * rtpbin, GstRTPBinClass * klass)
{
  rtpbin->priv = GST_RTP_BIN_GET_PRIVATE (rtpbin);
}

static void
gst_rtp_bin_finalize (GObject * object)
{
  GstRTPBin *rtpbin;

  rtpbin = GST_RTP_BIN (object);

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

/* Create a pad for receiving RTP for the session in @name
 */
static GstPad *
create_recv_rtp (GstRTPBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result;
  guint sessid;
  GstRTPBinSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtp_sink_%d", &sessid) != 1)
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
  if (session->recv_rtp_sink != NULL)
    goto existed;

  /* get recv_rtp pad and store */
  session->recv_rtp_sink =
      gst_element_get_request_pad (session->session, "recv_rtp_sink");
  if (session->recv_rtp_sink == NULL)
    goto pad_failed;

  result =
      gst_ghost_pad_new_from_template (name, session->recv_rtp_sink, templ);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  /* FIXME, get srcpad, link to SSRCDemux */

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
}

/* Create a pad for receiving RTCP for the session in @name
 */
static GstPad *
create_recv_rtcp (GstRTPBin * rtpbin, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *result;
  guint sessid;
  GstRTPBinSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "recv_rtcp_sink_%d", &sessid) != 1)
    goto no_name;

  /* get the session, it must exist or we error */
  session = find_session_by_id (rtpbin, sessid);
  if (!session)
    goto no_session;

  /* check if pad was requested */
  if (session->recv_rtcp_sink != NULL)
    goto existed;

  /* get recv_rtp pad and store */
  session->recv_rtcp_sink =
      gst_element_get_request_pad (session->session, "recv_rtcp_sink");
  if (session->recv_rtcp_sink == NULL)
    goto pad_failed;

  result =
      gst_ghost_pad_new_from_template (name, session->recv_rtcp_sink, templ);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpbin), result);

  /* FIXME, get srcpad, link to SSRCDemux */

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
}

/* Create a pad for sending RTP for the session in @name
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

/* Create a pad for sending RTCP for the session in @name
 */
static GstPad *
create_rtcp (GstRTPBin * rtpbin, GstPadTemplate * templ, const gchar * name)
{
  GstPad *result;
  guint sessid;
  GstRTPBinSession *session;

  /* first get the session number */
  if (name == NULL || sscanf (name, "send_rtp_sink_%d", &sessid) != 1)
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
      gst_element_get_request_pad (session->session, "rtcp_src");
  if (session->rtcp_src == NULL)
    goto pad_failed;

  result = gst_ghost_pad_new_from_template (name, session->rtcp_src, templ);
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

  return result;

  /* ERRORS */
wrong_template:
  {
    g_warning ("rtpbin: this is not our template");
    return NULL;
  }
}

static void
gst_rtp_bin_release_pad (GstElement * element, GstPad * pad)
{
}
