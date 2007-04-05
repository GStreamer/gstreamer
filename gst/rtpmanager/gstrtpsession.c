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
#include "gstrtpsession.h"

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

static GstStaticPadTemplate rtpsession_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

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

/*static guint gst_rtp_session_signals[LAST_SIGNAL] = { 0 }; */

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
      gst_static_pad_template_get (&rtpsession_rtcp_src_template));

  gst_element_class_set_details (element_class, &rtpsession_details);
}

static void
gst_rtp_session_class_init (GstRTPSessionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_rtp_session_finalize;
  gobject_class->set_property = gst_rtp_session_set_property;
  gobject_class->get_property = gst_rtp_session_get_property;

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
}

static void
gst_rtp_session_finalize (GObject * object)
{
  GstRTPSession *rtpsession;

  rtpsession = GST_RTP_SESSION (object);

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
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

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

static GstFlowReturn
gst_rtp_session_event_recv_rtp_sink (GstPad * pad, GstEvent * event)
{
  GstRTPSession *rtpsession;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (rtpsession, "received event");

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
  GstFlowReturn ret;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (rtpsession, "received RTP packet");

  /* FIXME, do something */
  ret = gst_pad_push (rtpsession->recv_rtp_src, buffer);

  gst_object_unref (rtpsession);

  return ret;
}

static GstFlowReturn
gst_rtp_session_event_recv_rtcp_sink (GstPad * pad, GstEvent * event)
{
  GstRTPSession *rtpsession;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (rtpsession, "received event");

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
  GstFlowReturn ret;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));

  /* FIXME, do something */
  GST_DEBUG_OBJECT (rtpsession, "received RTCP packet");

  ret = gst_pad_push (rtpsession->sync_src, buffer);

  gst_object_unref (rtpsession);

  return ret;
}

static GstFlowReturn
gst_rtp_session_event_send_rtp_sink (GstPad * pad, GstEvent * event)
{
  GstRTPSession *rtpsession;
  gboolean ret = FALSE;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));

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
  GstFlowReturn ret;

  rtpsession = GST_RTP_SESSION (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (rtpsession, "received RTP packet");

  /* FIXME, do something */
  ret = gst_pad_push (rtpsession->send_rtp_src, buffer);

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
      NULL);
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
      NULL);
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
      NULL);
  gst_pad_set_chain_function (rtpsession->send_rtp_sink,
      gst_rtp_session_chain_send_rtp);
  gst_pad_set_event_function (rtpsession->send_rtp_sink,
      gst_rtp_session_event_send_rtp_sink);
  gst_pad_set_active (rtpsession->send_rtp_sink, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession),
      rtpsession->recv_rtcp_sink);

  rtpsession->send_rtp_src =
      gst_pad_new_from_static_template (&rtpsession_send_rtp_src_template,
      NULL);
  gst_pad_set_active (rtpsession->send_rtp_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession), rtpsession->send_rtp_src);

  return rtpsession->send_rtp_sink;
}

/* Create a srcpad with the RTCP packets to send out.
 * This pad will be driven by the RTP session manager when it wants to send out
 * RTCP packets.
 */
static GstPad *
create_rtcp_src (GstRTPSession * rtpsession)
{
  GST_DEBUG_OBJECT (rtpsession, "creating pad");

  rtpsession->rtcp_src =
      gst_pad_new_from_static_template (&rtpsession_rtcp_src_template, NULL);
  gst_pad_set_active (rtpsession->rtcp_src, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (rtpsession), rtpsession->rtcp_src);

  return rtpsession->rtcp_src;
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
  } else if (templ == gst_element_class_get_pad_template (klass, "rtcp_src")) {
    if (rtpsession->rtcp_src != NULL)
      goto exists;

    result = create_rtcp_src (rtpsession);
  } else
    goto wrong_template;

  return result;

  /* ERRORS */
wrong_template:
  {
    g_warning ("rtpsession: this is not our template");
    return NULL;
  }
exists:
  {
    g_warning ("rtpsession: pad already requested");
    return NULL;
  }
}

static void
gst_rtp_session_release_pad (GstElement * element, GstPad * pad)
{
}
