/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim dot taymans at gmail dot com>
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
 * SECTION:element-sdpdemux
 * @title: sdpdemux
 *
 * sdpdemux currently understands SDP as the input format of the session description.
 * For each stream listed in the SDP a new stream_\%u pad will be created
 * with caps derived from the SDP media description. This is a caps of mime type
 * "application/x-rtp" that can be connected to any available RTP depayloader
 * element.
 *
 * sdpdemux will internally instantiate an RTP session manager element
 * that will handle the RTCP messages to and from the server, jitter removal,
 * packet reordering along with providing a clock for the pipeline.
 *
 * sdpdemux acts like a live element and will therefore only generate data in the
 * PLAYING state.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 souphttpsrc location=http://some.server/session.sdp ! sdpdemux ! fakesink
 * ]| Establish a connection to an HTTP server that contains an SDP session description
 * that gets parsed by sdpdemux and send the raw RTP packets to a fakesink.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsdpdemux.h"

#include <gst/rtp/gstrtppayloads.h>
#include <gst/sdp/gstsdpmessage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (sdpdemux_debug);
#define GST_CAT_DEFAULT (sdpdemux_debug)

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/sdp"));

static GstStaticPadTemplate rtptemplate = GST_STATIC_PAD_TEMPLATE ("stream_%u",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp"));

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_DEBUG            FALSE
#define DEFAULT_TIMEOUT          10000000
#define DEFAULT_LATENCY_MS       200
#define DEFAULT_REDIRECT         TRUE

enum
{
  PROP_0,
  PROP_DEBUG,
  PROP_TIMEOUT,
  PROP_LATENCY,
  PROP_REDIRECT
};

static void gst_sdp_demux_finalize (GObject * object);

static void gst_sdp_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_sdp_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_sdp_demux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_sdp_demux_handle_message (GstBin * bin, GstMessage * message);

static void gst_sdp_demux_stream_push_event (GstSDPDemux * demux,
    GstSDPStream * stream, GstEvent * event);

static gboolean gst_sdp_demux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_sdp_demux_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);

/*static guint gst_sdp_demux_signals[LAST_SIGNAL] = { 0 }; */

#define gst_sdp_demux_parent_class parent_class
G_DEFINE_TYPE (GstSDPDemux, gst_sdp_demux, GST_TYPE_BIN);

static void
gst_sdp_demux_class_init (GstSDPDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;

  gobject_class->set_property = gst_sdp_demux_set_property;
  gobject_class->get_property = gst_sdp_demux_get_property;

  gobject_class->finalize = gst_sdp_demux_finalize;

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug",
          "Dump request and response messages to stdout",
          DEFAULT_DEBUG,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Fail transport after UDP timeout microseconds (0 = disabled)",
          0, G_MAXUINT64, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REDIRECT,
      g_param_spec_boolean ("redirect", "Redirect",
          "Sends a redirection message instead of using a custom session element",
          DEFAULT_REDIRECT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &rtptemplate);

  gst_element_class_set_static_metadata (gstelement_class, "SDP session setup",
      "Codec/Demuxer/Network/RTP",
      "Receive data over the network via SDP",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstelement_class->change_state = gst_sdp_demux_change_state;

  gstbin_class->handle_message = gst_sdp_demux_handle_message;

  GST_DEBUG_CATEGORY_INIT (sdpdemux_debug, "sdpdemux", 0, "SDP demux");
}

static void
gst_sdp_demux_init (GstSDPDemux * demux)
{
  demux->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sdp_demux_sink_event));
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_sdp_demux_sink_chain));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* protects the streaming thread in interleaved mode or the polling
   * thread in UDP mode. */
  g_rec_mutex_init (&demux->stream_rec_lock);

  demux->adapter = gst_adapter_new ();
}

static void
gst_sdp_demux_finalize (GObject * object)
{
  GstSDPDemux *demux;

  demux = GST_SDP_DEMUX (object);

  /* free locks */
  g_rec_mutex_clear (&demux->stream_rec_lock);

  g_object_unref (demux->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_sdp_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSDPDemux *demux;

  demux = GST_SDP_DEMUX (object);

  switch (prop_id) {
    case PROP_DEBUG:
      demux->debug = g_value_get_boolean (value);
      break;
    case PROP_TIMEOUT:
      demux->udp_timeout = g_value_get_uint64 (value);
      break;
    case PROP_LATENCY:
      demux->latency = g_value_get_uint (value);
      break;
    case PROP_REDIRECT:
      demux->redirect = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sdp_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSDPDemux *demux;

  demux = GST_SDP_DEMUX (object);

  switch (prop_id) {
    case PROP_DEBUG:
      g_value_set_boolean (value, demux->debug);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, demux->udp_timeout);
      break;
    case PROP_LATENCY:
      g_value_set_uint (value, demux->latency);
      break;
    case PROP_REDIRECT:
      g_value_set_boolean (value, demux->redirect);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
find_stream_by_id (GstSDPStream * stream, gconstpointer a)
{
  gint id = GPOINTER_TO_INT (a);

  if (stream->id == id)
    return 0;

  return -1;
}

static gint
find_stream_by_pt (GstSDPStream * stream, gconstpointer a)
{
  gint pt = GPOINTER_TO_INT (a);

  if (stream->pt == pt)
    return 0;

  return -1;
}

static gint
find_stream_by_udpsrc (GstSDPStream * stream, gconstpointer a)
{
  GstElement *src = (GstElement *) a;

  if (stream->udpsrc[0] == src)
    return 0;
  if (stream->udpsrc[1] == src)
    return 0;

  return -1;
}

static GstSDPStream *
find_stream (GstSDPDemux * demux, gconstpointer data, gconstpointer func)
{
  GList *lstream;

  /* find and get stream */
  if ((lstream =
          g_list_find_custom (demux->streams, data, (GCompareFunc) func)))
    return (GstSDPStream *) lstream->data;

  return NULL;
}

static void
gst_sdp_demux_stream_free (GstSDPDemux * demux, GstSDPStream * stream)
{
  gint i;

  GST_DEBUG_OBJECT (demux, "free stream %p", stream);

  if (stream->caps)
    gst_caps_unref (stream->caps);

  for (i = 0; i < 2; i++) {
    GstElement *udpsrc = stream->udpsrc[i];

    if (udpsrc) {
      gst_element_set_state (udpsrc, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (demux), udpsrc);
      stream->udpsrc[i] = NULL;
    }
  }
  if (stream->udpsink) {
    gst_element_set_state (stream->udpsink, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (demux), stream->udpsink);
    stream->udpsink = NULL;
  }
  if (stream->srcpad) {
    gst_pad_set_active (stream->srcpad, FALSE);
    if (stream->added) {
      gst_element_remove_pad (GST_ELEMENT_CAST (demux), stream->srcpad);
      stream->added = FALSE;
    }
    stream->srcpad = NULL;
  }
  g_free (stream);
}

static gboolean
is_multicast_address (const gchar * host_name)
{
  GInetAddress *addr;
  GResolver *resolver = NULL;
  gboolean ret = FALSE;

  addr = g_inet_address_new_from_string (host_name);
  if (!addr) {
    GList *results;

    resolver = g_resolver_get_default ();
    results = g_resolver_lookup_by_name (resolver, host_name, NULL, NULL);
    if (!results)
      goto out;
    addr = G_INET_ADDRESS (g_object_ref (results->data));

    g_resolver_free_addresses (results);
  }
  g_assert (addr != NULL);

  ret = g_inet_address_get_is_multicast (addr);

out:
  if (resolver)
    g_object_unref (resolver);
  if (addr)
    g_object_unref (addr);
  return ret;
}

static GstSDPStream *
gst_sdp_demux_create_stream (GstSDPDemux * demux, GstSDPMessage * sdp, gint idx)
{
  GstSDPStream *stream;
  const gchar *payload;
  const GstSDPMedia *media;
  const GstSDPConnection *conn;

  /* get media, should not return NULL */
  media = gst_sdp_message_get_media (sdp, idx);
  if (media == NULL)
    return NULL;

  stream = g_new0 (GstSDPStream, 1);
  stream->parent = demux;
  /* we mark the pad as not linked, we will mark it as OK when we add the pad to
   * the element. */
  stream->last_ret = GST_FLOW_OK;
  stream->added = FALSE;
  stream->disabled = FALSE;
  stream->id = demux->numstreams++;
  stream->eos = FALSE;

  /* we must have a payload. No payload means we cannot create caps */
  /* FIXME, handle multiple formats. */
  if ((payload = gst_sdp_media_get_format (media, 0))) {
    GstStructure *s;

    stream->pt = atoi (payload);
    /* convert caps */
    stream->caps = gst_sdp_media_get_caps_from_media (media, stream->pt);

    s = gst_caps_get_structure (stream->caps, 0);
    gst_structure_set_name (s, "application/x-rtp");

    if (stream->pt >= 96) {
      /* If we have a dynamic payload type, see if we have a stream with the
       * same payload number. If there is one, they are part of the same
       * container and we only need to add one pad. */
      if (find_stream (demux, GINT_TO_POINTER (stream->pt),
              (gpointer) find_stream_by_pt)) {
        stream->container = TRUE;
      }
    }
  }
  if (!(conn = gst_sdp_media_get_connection (media, 0))) {
    if (!(conn = gst_sdp_message_get_connection (sdp)))
      goto no_connection;
  }

  if (!conn->address)
    goto no_connection;

  stream->destination = conn->address;
  stream->ttl = conn->ttl;
  stream->multicast = is_multicast_address (stream->destination);

  stream->rtp_port = gst_sdp_media_get_port (media);
  if (gst_sdp_media_get_attribute_val (media, "rtcp")) {
    /* FIXME, RFC 3605 */
    stream->rtcp_port = stream->rtp_port + 1;
  } else {
    stream->rtcp_port = stream->rtp_port + 1;
  }

  GST_DEBUG_OBJECT (demux, "stream %d, (%p)", stream->id, stream);
  GST_DEBUG_OBJECT (demux, " pt: %d", stream->pt);
  GST_DEBUG_OBJECT (demux, " container: %d", stream->container);
  GST_DEBUG_OBJECT (demux, " caps: %" GST_PTR_FORMAT, stream->caps);

  /* we keep track of all streams */
  demux->streams = g_list_append (demux->streams, stream);

  return stream;

  /* ERRORS */
no_connection:
  {
    gst_sdp_demux_stream_free (demux, stream);
    return NULL;
  }
}

static void
gst_sdp_demux_cleanup (GstSDPDemux * demux)
{
  GList *walk;

  GST_DEBUG_OBJECT (demux, "cleanup");

  for (walk = demux->streams; walk; walk = g_list_next (walk)) {
    GstSDPStream *stream = (GstSDPStream *) walk->data;

    gst_sdp_demux_stream_free (demux, stream);
  }
  g_list_free (demux->streams);
  demux->streams = NULL;
  if (demux->session) {
    if (demux->session_sig_id) {
      g_signal_handler_disconnect (demux->session, demux->session_sig_id);
      demux->session_sig_id = 0;
    }
    if (demux->session_nmp_id) {
      g_signal_handler_disconnect (demux->session, demux->session_nmp_id);
      demux->session_nmp_id = 0;
    }
    if (demux->session_ptmap_id) {
      g_signal_handler_disconnect (demux->session, demux->session_ptmap_id);
      demux->session_ptmap_id = 0;
    }
    gst_element_set_state (demux->session, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (demux), demux->session);
    demux->session = NULL;
  }
  demux->numstreams = 0;
}

/* this callback is called when the session manager generated a new src pad with
 * payloaded RTP packets. We simply ghost the pad here. */
static void
new_session_pad (GstElement * session, GstPad * pad, GstSDPDemux * demux)
{
  gchar *name, *pad_name;
  GstPadTemplate *template;
  gint id, ssrc, pt;
  GList *lstream;
  GstSDPStream *stream;
  gboolean all_added;

  GST_DEBUG_OBJECT (demux, "got new session pad %" GST_PTR_FORMAT, pad);

  GST_SDP_STREAM_LOCK (demux);
  /* find stream */
  name = gst_object_get_name (GST_OBJECT_CAST (pad));
  if (sscanf (name, "recv_rtp_src_%u_%u_%u", &id, &ssrc, &pt) != 3)
    goto unknown_stream;

  GST_DEBUG_OBJECT (demux, "stream: %u, SSRC %d, PT %d", id, ssrc, pt);

  stream =
      find_stream (demux, GINT_TO_POINTER (id), (gpointer) find_stream_by_id);
  if (stream == NULL)
    goto unknown_stream;

  /* no need for a timeout anymore now */
  g_object_set (G_OBJECT (stream->udpsrc[0]), "timeout", (guint64) 0, NULL);

  pad_name = g_strdup_printf ("stream_%u", stream->id);
  /* create a new pad we will use to stream to */
  template = gst_static_pad_template_get (&rtptemplate);
  stream->srcpad = gst_ghost_pad_new_from_template (pad_name, pad, template);
  gst_object_unref (template);
  g_free (name);
  g_free (pad_name);

  stream->added = TRUE;
  gst_pad_set_active (stream->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), stream->srcpad);

  /* check if we added all streams */
  all_added = TRUE;
  for (lstream = demux->streams; lstream; lstream = g_list_next (lstream)) {
    stream = (GstSDPStream *) lstream->data;
    /* a container stream only needs one pad added. Also disabled streams don't
     * count */
    if (!stream->container && !stream->disabled && !stream->added) {
      all_added = FALSE;
      break;
    }
  }
  GST_SDP_STREAM_UNLOCK (demux);

  if (all_added) {
    GST_DEBUG_OBJECT (demux, "We added all streams");
    /* when we get here, all stream are added and we can fire the no-more-pads
     * signal. */
    gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
  }

  return;

  /* ERRORS */
unknown_stream:
  {
    GST_DEBUG_OBJECT (demux, "ignoring unknown stream");
    GST_SDP_STREAM_UNLOCK (demux);
    g_free (name);
    return;
  }
}

static void
rtsp_session_pad_added (GstElement * session, GstPad * pad, GstSDPDemux * demux)
{
  GstPad *srcpad = NULL;
  gchar *name;

  GST_DEBUG_OBJECT (demux, "got new session pad %" GST_PTR_FORMAT, pad);

  name = gst_pad_get_name (pad);
  srcpad = gst_ghost_pad_new (name, pad);
  g_free (name);

  gst_pad_set_active (srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (demux), srcpad);
}

static void
rtsp_session_no_more_pads (GstElement * session, GstSDPDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "got no-more-pads");
  gst_element_no_more_pads (GST_ELEMENT_CAST (demux));
}

static GstCaps *
request_pt_map (GstElement * sess, guint session, guint pt, GstSDPDemux * demux)
{
  GstSDPStream *stream;
  GstCaps *caps;

  GST_DEBUG_OBJECT (demux, "getting pt map for pt %d in session %d", pt,
      session);

  GST_SDP_STREAM_LOCK (demux);
  stream =
      find_stream (demux, GINT_TO_POINTER (session),
      (gpointer) find_stream_by_id);
  if (!stream)
    goto unknown_stream;

  caps = stream->caps;
  if (caps)
    gst_caps_ref (caps);
  GST_SDP_STREAM_UNLOCK (demux);

  return caps;

unknown_stream:
  {
    GST_DEBUG_OBJECT (demux, "unknown stream %d", session);
    GST_SDP_STREAM_UNLOCK (demux);
    return NULL;
  }
}

static void
gst_sdp_demux_do_stream_eos (GstSDPDemux * demux, guint session)
{
  GstSDPStream *stream;

  GST_DEBUG_OBJECT (demux, "setting stream for session %u to EOS", session);

  /* get stream for session */
  stream =
      find_stream (demux, GINT_TO_POINTER (session),
      (gpointer) find_stream_by_id);
  if (!stream)
    goto unknown_stream;

  if (stream->eos)
    goto was_eos;

  stream->eos = TRUE;
  gst_sdp_demux_stream_push_event (demux, stream, gst_event_new_eos ());
  return;

  /* ERRORS */
unknown_stream:
  {
    GST_DEBUG_OBJECT (demux, "unknown stream for session %u", session);
    return;
  }
was_eos:
  {
    GST_DEBUG_OBJECT (demux, "stream for session %u was already EOS", session);
    return;
  }
}

static void
on_bye_ssrc (GstElement * manager, guint session, guint32 ssrc,
    GstSDPDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "SSRC %08x in session %u received BYE", ssrc,
      session);

  gst_sdp_demux_do_stream_eos (demux, session);
}

static void
on_timeout (GstElement * manager, guint session, guint32 ssrc,
    GstSDPDemux * demux)
{
  GST_DEBUG_OBJECT (demux, "SSRC %08x in session %u timed out", ssrc, session);

  gst_sdp_demux_do_stream_eos (demux, session);
}

/* try to get and configure a manager */
static gboolean
gst_sdp_demux_configure_manager (GstSDPDemux * demux, char *rtsp_sdp)
{
  /* configure the session manager */
  if (rtsp_sdp != NULL) {
    if (!(demux->session = gst_element_factory_make ("rtspsrc", NULL)))
      goto rtspsrc_failed;

    g_object_set (demux->session, "location", rtsp_sdp, NULL);

    GST_DEBUG_OBJECT (demux, "connect to signals on rtspsrc");
    demux->session_sig_id =
        g_signal_connect (demux->session, "pad-added",
        (GCallback) rtsp_session_pad_added, demux);
    demux->session_nmp_id =
        g_signal_connect (demux->session, "no-more-pads",
        (GCallback) rtsp_session_no_more_pads, demux);
  } else {
    if (!(demux->session = gst_element_factory_make ("rtpbin", NULL)))
      goto manager_failed;

    /* connect to signals if we did not already do so */
    GST_DEBUG_OBJECT (demux, "connect to signals on session manager");
    demux->session_sig_id =
        g_signal_connect (demux->session, "pad-added",
        (GCallback) new_session_pad, demux);
    demux->session_ptmap_id =
        g_signal_connect (demux->session, "request-pt-map",
        (GCallback) request_pt_map, demux);
    g_signal_connect (demux->session, "on-bye-ssrc", (GCallback) on_bye_ssrc,
        demux);
    g_signal_connect (demux->session, "on-bye-timeout", (GCallback) on_timeout,
        demux);
    g_signal_connect (demux->session, "on-timeout", (GCallback) on_timeout,
        demux);
  }

  g_object_set (demux->session, "latency", demux->latency, NULL);

  /* we manage this element */
  gst_bin_add (GST_BIN_CAST (demux), demux->session);

  return TRUE;

  /* ERRORS */
manager_failed:
  {
    GST_DEBUG_OBJECT (demux, "no session manager element gstrtpbin found");
    return FALSE;
  }
rtspsrc_failed:
  {
    GST_DEBUG_OBJECT (demux, "no manager element rtspsrc found");
    return FALSE;
  }
}

static gboolean
gst_sdp_demux_stream_configure_udp (GstSDPDemux * demux, GstSDPStream * stream)
{
  gchar *uri, *name;
  const gchar *destination;
  GstPad *pad;

  GST_DEBUG_OBJECT (demux, "creating UDP sources for multicast");

  /* if the destination is not a multicast address, we just want to listen on
   * our local ports */
  if (!stream->multicast)
    destination = "0.0.0.0";
  else
    destination = stream->destination;

  /* creating UDP source */
  if (stream->rtp_port != -1) {
    GST_DEBUG_OBJECT (demux, "receiving RTP from %s:%d", destination,
        stream->rtp_port);

    uri = g_strdup_printf ("udp://%s:%d", destination, stream->rtp_port);
    stream->udpsrc[0] =
        gst_element_make_from_uri (GST_URI_SRC, uri, NULL, NULL);
    g_free (uri);
    if (stream->udpsrc[0] == NULL)
      goto no_element;

    /* take ownership */
    gst_bin_add (GST_BIN_CAST (demux), stream->udpsrc[0]);

    GST_DEBUG_OBJECT (demux,
        "setting up UDP source with timeout %" G_GINT64_FORMAT,
        demux->udp_timeout);

    /* configure a timeout on the UDP port. When the timeout message is
     * posted, we assume UDP transport is not possible. */
    g_object_set (G_OBJECT (stream->udpsrc[0]), "timeout",
        demux->udp_timeout * 1000, NULL);

    /* get output pad of the UDP source. */
    pad = gst_element_get_static_pad (stream->udpsrc[0], "src");

    name = g_strdup_printf ("recv_rtp_sink_%u", stream->id);
    stream->channelpad[0] = gst_element_get_request_pad (demux->session, name);
    g_free (name);

    GST_DEBUG_OBJECT (demux, "connecting RTP source 0 to manager");
    /* configure for UDP delivery, we need to connect the UDP pads to
     * the session plugin. */
    gst_pad_link (pad, stream->channelpad[0]);
    gst_object_unref (pad);

    /* change state */
    gst_element_set_state (stream->udpsrc[0], GST_STATE_PAUSED);
  }

  /* creating another UDP source */
  if (stream->rtcp_port != -1) {
    GST_DEBUG_OBJECT (demux, "receiving RTCP from %s:%d", destination,
        stream->rtcp_port);
    uri = g_strdup_printf ("udp://%s:%d", destination, stream->rtcp_port);
    stream->udpsrc[1] =
        gst_element_make_from_uri (GST_URI_SRC, uri, NULL, NULL);
    g_free (uri);
    if (stream->udpsrc[1] == NULL)
      goto no_element;

    /* take ownership */
    gst_bin_add (GST_BIN_CAST (demux), stream->udpsrc[1]);

    GST_DEBUG_OBJECT (demux, "connecting RTCP source to manager");

    name = g_strdup_printf ("recv_rtcp_sink_%u", stream->id);
    stream->channelpad[1] = gst_element_get_request_pad (demux->session, name);
    g_free (name);

    pad = gst_element_get_static_pad (stream->udpsrc[1], "src");
    gst_pad_link (pad, stream->channelpad[1]);
    gst_object_unref (pad);

    gst_element_set_state (stream->udpsrc[1], GST_STATE_PAUSED);
  }
  return TRUE;

  /* ERRORS */
no_element:
  {
    GST_DEBUG_OBJECT (demux, "no UDP source element found");
    return FALSE;
  }
}

/* configure the UDP sink back to the server for status reports */
static gboolean
gst_sdp_demux_stream_configure_udp_sink (GstSDPDemux * demux,
    GstSDPStream * stream)
{
  GstPad *pad, *sinkpad;
  gint port;
  GSocket *socket;
  gchar *destination, *uri, *name;

  /* get destination and port */
  port = stream->rtcp_port;
  destination = stream->destination;

  GST_DEBUG_OBJECT (demux, "configure UDP sink for %s:%d", destination, port);

  uri = g_strdup_printf ("udp://%s:%d", destination, port);
  stream->udpsink = gst_element_make_from_uri (GST_URI_SINK, uri, NULL, NULL);
  g_free (uri);
  if (stream->udpsink == NULL)
    goto no_sink_element;

  /* we clear all destinations because we don't really know where to send the
   * RTCP to and we want to avoid sending it to our own ports.
   * FIXME when we get an RTCP packet from the sender, we could look at its
   * source port and address and try to send RTCP there. */
  if (!stream->multicast)
    g_signal_emit_by_name (stream->udpsink, "clear");

  g_object_set (G_OBJECT (stream->udpsink), "auto-multicast", FALSE, NULL);
  g_object_set (G_OBJECT (stream->udpsink), "loop", FALSE, NULL);
  /* no sync needed */
  g_object_set (G_OBJECT (stream->udpsink), "sync", FALSE, NULL);
  /* no async state changes needed */
  g_object_set (G_OBJECT (stream->udpsink), "async", FALSE, NULL);

  if (stream->udpsrc[1]) {
    /* configure socket, we give it the same UDP socket as the udpsrc for RTCP
     * because some servers check the port number of where it sends RTCP to identify
     * the RTCP packets it receives */
    g_object_get (G_OBJECT (stream->udpsrc[1]), "used_socket", &socket, NULL);
    GST_DEBUG_OBJECT (demux, "UDP src has socket %p", socket);
    /* configure socket and make sure udpsink does not close it when shutting
     * down, it belongs to udpsrc after all. */
    g_object_set (G_OBJECT (stream->udpsink), "socket", socket, NULL);
    g_object_set (G_OBJECT (stream->udpsink), "close-socket", FALSE, NULL);
    g_object_unref (socket);
  }

  /* we keep this playing always */
  gst_element_set_locked_state (stream->udpsink, TRUE);
  gst_element_set_state (stream->udpsink, GST_STATE_PLAYING);

  gst_bin_add (GST_BIN_CAST (demux), stream->udpsink);

  /* get session RTCP pad */
  name = g_strdup_printf ("send_rtcp_src_%u", stream->id);
  pad = gst_element_get_request_pad (demux->session, name);
  g_free (name);

  /* and link */
  if (pad) {
    sinkpad = gst_element_get_static_pad (stream->udpsink, "sink");
    gst_pad_link (pad, sinkpad);
    gst_object_unref (pad);
    gst_object_unref (sinkpad);
  } else {
    /* not very fatal, we just won't be able to send RTCP */
    GST_WARNING_OBJECT (demux, "could not get session RTCP pad");
  }


  return TRUE;

  /* ERRORS */
no_sink_element:
  {
    GST_DEBUG_OBJECT (demux, "no UDP sink element found");
    return FALSE;
  }
}

static GstFlowReturn
gst_sdp_demux_combine_flows (GstSDPDemux * demux, GstSDPStream * stream,
    GstFlowReturn ret)
{
  GList *streams;

  /* store the value */
  stream->last_ret = ret;

  /* if it's success we can return the value right away */
  if (ret == GST_FLOW_OK)
    goto done;

  /* any other error that is not-linked can be returned right
   * away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (streams = demux->streams; streams; streams = g_list_next (streams)) {
    GstSDPStream *ostream = (GstSDPStream *) streams->data;

    ret = ostream->last_ret;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  return ret;
}

static void
gst_sdp_demux_stream_push_event (GstSDPDemux * demux, GstSDPStream * stream,
    GstEvent * event)
{
  /* only streams that have a connection to the outside world */
  if (stream->srcpad == NULL)
    goto done;

  if (stream->channelpad[0]) {
    gst_event_ref (event);
    gst_pad_send_event (stream->channelpad[0], event);
  }

  if (stream->channelpad[1]) {
    gst_event_ref (event);
    gst_pad_send_event (stream->channelpad[1], event);
  }

done:
  gst_event_unref (event);
}

static void
gst_sdp_demux_handle_message (GstBin * bin, GstMessage * message)
{
  GstSDPDemux *demux;

  demux = GST_SDP_DEMUX (bin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s = gst_message_get_structure (message);

      if (gst_structure_has_name (s, "GstUDPSrcTimeout")) {
        gboolean ignore_timeout;

        GST_DEBUG_OBJECT (bin, "timeout on UDP port");

        GST_OBJECT_LOCK (demux);
        ignore_timeout = demux->ignore_timeout;
        demux->ignore_timeout = TRUE;
        GST_OBJECT_UNLOCK (demux);

        /* we only act on the first udp timeout message, others are irrelevant
         * and can be ignored. */
        if (ignore_timeout)
          gst_message_unref (message);
        else {
          GST_ELEMENT_ERROR (demux, RESOURCE, READ, (NULL),
              ("Could not receive any UDP packets for %.4f seconds, maybe your "
                  "firewall is blocking it.",
                  gst_guint64_to_gdouble (demux->udp_timeout / 1000000.0)));
        }
        return;
      }
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GstObject *udpsrc;
      GstSDPStream *stream;
      GstFlowReturn ret;

      udpsrc = GST_MESSAGE_SRC (message);

      GST_DEBUG_OBJECT (demux, "got error from %s", GST_ELEMENT_NAME (udpsrc));

      stream = find_stream (demux, udpsrc, (gpointer) find_stream_by_udpsrc);
      /* fatal but not our message, forward */
      if (!stream)
        goto forward;

      /* we ignore the RTCP udpsrc */
      if (stream->udpsrc[1] == GST_ELEMENT_CAST (udpsrc))
        goto done;

      /* if we get error messages from the udp sources, that's not a problem as
       * long as not all of them error out. We also don't really know what the
       * problem is, the message does not give enough detail... */
      ret = gst_sdp_demux_combine_flows (demux, stream, GST_FLOW_NOT_LINKED);
      GST_DEBUG_OBJECT (demux, "combined flows: %s", gst_flow_get_name (ret));
      if (ret != GST_FLOW_OK)
        goto forward;

    done:
      gst_message_unref (message);
      break;

    forward:
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
    default:
    {
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
  }
}

static gboolean
gst_sdp_demux_start (GstSDPDemux * demux)
{
  guint8 *data = NULL;
  guint size;
  gint i, n_streams;
  GstSDPMessage sdp = { 0 };
  GstSDPStream *stream = NULL;
  GList *walk;
  gchar *uri = NULL;
  GstStateChangeReturn ret;

  /* grab the lock so that no state change can interfere */
  GST_SDP_STREAM_LOCK (demux);

  GST_DEBUG_OBJECT (demux, "parse SDP...");

  size = gst_adapter_available (demux->adapter);
  if (size == 0)
    goto no_data;

  data = gst_adapter_take (demux->adapter, size);

  gst_sdp_message_init (&sdp);
  if (gst_sdp_message_parse_buffer (data, size, &sdp) != GST_SDP_OK)
    goto could_not_parse;

  if (demux->debug)
    gst_sdp_message_dump (&sdp);

  /* maybe this is plain RTSP DESCRIBE rtsp and we should redirect */
  /* look for rtsp control url */
  {
    const gchar *control;

    for (i = 0;; i++) {
      control = gst_sdp_message_get_attribute_val_n (&sdp, "control", i);
      if (control == NULL)
        break;

      /* only take fully qualified urls */
      if (g_str_has_prefix (control, "rtsp://"))
        break;
    }
    if (!control) {
      gint idx;

      /* try to find non-aggragate control */
      n_streams = gst_sdp_message_medias_len (&sdp);

      for (idx = 0; idx < n_streams; idx++) {
        const GstSDPMedia *media;

        /* get media, should not return NULL */
        media = gst_sdp_message_get_media (&sdp, idx);
        if (media == NULL)
          break;

        for (i = 0;; i++) {
          control = gst_sdp_media_get_attribute_val_n (media, "control", i);
          if (control == NULL)
            break;

          /* only take fully qualified urls */
          if (g_str_has_prefix (control, "rtsp://"))
            break;
        }
        /* this media has no control, exit */
        if (!control)
          break;
      }
    }

    if (control) {
      /* we have RTSP now */
      uri = gst_sdp_message_as_uri ("rtsp-sdp", &sdp);

      if (demux->redirect) {
        GST_INFO_OBJECT (demux, "redirect to %s", uri);

        gst_element_post_message (GST_ELEMENT_CAST (demux),
            gst_message_new_element (GST_OBJECT_CAST (demux),
                gst_structure_new ("redirect",
                    "new-location", G_TYPE_STRING, uri, NULL)));
        goto sent_redirect;
      }
    }
  }

  /* we get here when we didn't do a redirect */

  /* try to get and configure a manager */
  if (!gst_sdp_demux_configure_manager (demux, uri))
    goto no_manager;
  if (!uri) {
    /* create streams with UDP sources and sinks */
    n_streams = gst_sdp_message_medias_len (&sdp);
    for (i = 0; i < n_streams; i++) {
      stream = gst_sdp_demux_create_stream (demux, &sdp, i);

      if (!stream)
        continue;

      GST_DEBUG_OBJECT (demux, "configuring transport for stream %p", stream);

      if (!gst_sdp_demux_stream_configure_udp (demux, stream))
        goto transport_failed;
      if (!gst_sdp_demux_stream_configure_udp_sink (demux, stream))
        goto transport_failed;
    }

    if (!demux->streams)
      goto no_streams;
  }

  /* set target state on session manager */
  /* setting rtspsrc to PLAYING may cause it to loose it that target state
   * along the way due to no-preroll udpsrc elements, so ...
   * do it in two stages here (similar to other elements) */
  if (demux->target > GST_STATE_PAUSED) {
    ret = gst_element_set_state (demux->session, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE)
      goto start_session_failure;
  }
  ret = gst_element_set_state (demux->session, demux->target);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_session_failure;

  if (!uri) {
    /* activate all streams */
    for (walk = demux->streams; walk; walk = g_list_next (walk)) {
      stream = (GstSDPStream *) walk->data;

      /* configure target state on udp sources */
      gst_element_set_state (stream->udpsrc[0], demux->target);
      gst_element_set_state (stream->udpsrc[1], demux->target);
    }
  }
  GST_SDP_STREAM_UNLOCK (demux);
  gst_sdp_message_uninit (&sdp);
  g_free (data);

  return TRUE;

  /* ERRORS */
done:
  {
    GST_SDP_STREAM_UNLOCK (demux);
    gst_sdp_message_uninit (&sdp);
    g_free (data);
    return FALSE;
  }
transport_failed:
  {
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Could not create RTP stream transport."));
    goto done;
  }
no_manager:
  {
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Could not create RTP session manager."));
    goto done;
  }
no_data:
  {
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Empty SDP message."));
    goto done;
  }
could_not_parse:
  {
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Could not parse SDP message."));
    goto done;
  }
no_streams:
  {
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND, (NULL),
        ("No streams in SDP message."));
    goto done;
  }
sent_redirect:
  {
    /* avoid hanging if redirect not handled */
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Sent RTSP redirect."));
    goto done;
  }
start_session_failure:
  {
    GST_ELEMENT_ERROR (demux, STREAM, TYPE_NOT_FOUND, (NULL),
        ("Could not start RTP session manager."));
    gst_element_set_state (demux->session, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (demux), demux->session);
    demux->session = NULL;
    goto done;
  }
}

static gboolean
gst_sdp_demux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSDPDemux *demux;
  gboolean res = TRUE;

  demux = GST_SDP_DEMUX (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* when we get EOS, start parsing the SDP */
      res = gst_sdp_demux_start (demux);
      gst_event_unref (event);
      break;
    default:
      gst_event_unref (event);
      break;
  }

  return res;
}

static GstFlowReturn
gst_sdp_demux_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSDPDemux *demux;

  demux = GST_SDP_DEMUX (parent);

  /* push the SDP message in an adapter, we start doing something with it when
   * we receive EOS */
  gst_adapter_push (demux->adapter, buffer);

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_sdp_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstSDPDemux *demux;
  GstStateChangeReturn ret;

  demux = GST_SDP_DEMUX (element);

  GST_SDP_STREAM_LOCK (demux);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* first attempt, don't ignore timeouts */
      gst_adapter_clear (demux->adapter);
      demux->ignore_timeout = FALSE;
      demux->target = GST_STATE_PAUSED;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      demux->target = GST_STATE_PLAYING;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      ret = GST_STATE_CHANGE_NO_PREROLL;
      demux->target = GST_STATE_PAUSED;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_sdp_demux_cleanup (demux);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

done:
  GST_SDP_STREAM_UNLOCK (demux);

  return ret;
}
