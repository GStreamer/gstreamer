/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <string.h>

#include "gstrtspsrc.h"
#include "sdp.h"

GST_DEBUG_CATEGORY (rtspsrc_debug);
#define GST_CAT_DEFAULT (rtspsrc_debug)

/* elementfactory information */
static GstElementDetails gst_rtspsrc_details =
GST_ELEMENT_DETAILS ("RTSP packet receiver",
    "Source/Network",
    "Receive data over the network via RTSP (RFC 2326)",
    "Wim Taymans <wim@fluendo.com>");

static GstStaticPadTemplate rtptemplate =
GST_STATIC_PAD_TEMPLATE ("rtp_stream%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate rtcptemplate =
GST_STATIC_PAD_TEMPLATE ("rtcp_stream%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_LOCATION        NULL
#define DEFAULT_PROTOCOLS       GST_RTSP_PROTO_UDP_UNICAST | GST_RTSP_PROTO_UDP_MULTICAST | GST_RTSP_PROTO_TCP
#define DEFAULT_DEBUG           FALSE
#define DEFAULT_RETRY           20

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PROTOCOLS,
  PROP_DEBUG,
  PROP_RETRY,
  /* FILL ME */
};

#define GST_TYPE_RTSP_PROTO (gst_rtsp_proto_get_type())
static GType
gst_rtsp_proto_get_type (void)
{
  static GType rtsp_proto_type = 0;
  static GFlagsValue rtsp_proto[] = {
    {GST_RTSP_PROTO_UDP_UNICAST, "UDP Unicast", "UDP unicast mode"},
    {GST_RTSP_PROTO_UDP_MULTICAST, "UDP Multicast", "UDP Multicast mode"},
    {GST_RTSP_PROTO_TCP, "TCP", "TCP interleaved mode"},
    {0, NULL, NULL},
  };

  if (!rtsp_proto_type) {
    rtsp_proto_type = g_flags_register_static ("GstRTSPProto", rtsp_proto);
  }
  return rtsp_proto_type;
}


static void gst_rtspsrc_base_init (gpointer g_class);
static void gst_rtspsrc_class_init (GstRTSPSrc * klass);
static void gst_rtspsrc_init (GstRTSPSrc * rtspsrc);

static void gst_rtspsrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static GstStateChangeReturn gst_rtspsrc_change_state (GstElement * element,
    GstStateChange transition);

static void gst_rtspsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtspsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_rtspsrc_loop (GstRTSPSrc * src);

static GstElementClass *parent_class = NULL;

/*static guint gst_rtspsrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_rtspsrc_get_type (void)
{
  static GType rtspsrc_type = 0;

  if (!rtspsrc_type) {
    static const GTypeInfo rtspsrc_info = {
      sizeof (GstRTSPSrcClass),
      gst_rtspsrc_base_init,
      NULL,
      (GClassInitFunc) gst_rtspsrc_class_init,
      NULL,
      NULL,
      sizeof (GstRTSPSrc),
      0,
      (GInstanceInitFunc) gst_rtspsrc_init,
      NULL
    };
    static const GInterfaceInfo urihandler_info = {
      gst_rtspsrc_uri_handler_init,
      NULL,
      NULL
    };

    GST_DEBUG_CATEGORY_INIT (rtspsrc_debug, "rtspsrc", 0, "RTSP src");

    rtspsrc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTSPSrc", &rtspsrc_info,
        0);

    g_type_add_interface_static (rtspsrc_type, GST_TYPE_URI_HANDLER,
        &urihandler_info);
  }
  return rtspsrc_type;
}

static void
gst_rtspsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtptemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtcptemplate));

  gst_element_class_set_details (element_class, &gst_rtspsrc_details);
}

static void
gst_rtspsrc_class_init (GstRTSPSrc * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_rtspsrc_set_property;
  gobject_class->get_property = gst_rtspsrc_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "RTSP Location",
          "Location of the RTSP url to read",
          DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols", "Allowed protocols",
          GST_TYPE_RTSP_PROTO, DEFAULT_PROTOCOLS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug",
          "Dump request and response messages to stdout",
          DEFAULT_DEBUG, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_RETRY,
      g_param_spec_uint ("retry", "Retry",
          "Max number of retries when allocating RTP ports.",
          0, G_MAXUINT16, DEFAULT_RETRY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->change_state = gst_rtspsrc_change_state;
}

static void
gst_rtspsrc_init (GstRTSPSrc * src)
{
}

static void
gst_rtspsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_free (rtspsrc->location);
      rtspsrc->location = g_value_dup_string (value);
      break;
    case PROP_PROTOCOLS:
      rtspsrc->protocols = g_value_get_flags (value);
      break;
    case PROP_DEBUG:
      rtspsrc->debug = g_value_get_boolean (value);
      break;
    case PROP_RETRY:
      rtspsrc->retry = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtspsrc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, rtspsrc->location);
      break;
    case PROP_PROTOCOLS:
      g_value_set_flags (value, rtspsrc->protocols);
      break;
    case PROP_DEBUG:
      g_value_set_boolean (value, rtspsrc->debug);
      break;
    case PROP_RETRY:
      g_value_set_uint (value, rtspsrc->retry);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstRTSPStream *
gst_rtspsrc_create_stream (GstRTSPSrc * src)
{
  GstRTSPStream *s;

  s = g_new0 (GstRTSPStream, 1);
  s->parent = src;
  s->id = src->numstreams++;

  src->streams = g_list_append (src->streams, s);

  return s;
}

static gboolean
gst_rtspsrc_add_element (GstRTSPSrc * src, GstElement * element)
{
  gst_object_set_parent (GST_OBJECT (element), GST_OBJECT (src));

  return TRUE;
}

static GstStateChangeReturn
gst_rtspsrc_set_state (GstRTSPSrc * src, GstState state)
{
  GstStateChangeReturn ret;
  GList *streams;

  ret = GST_STATE_CHANGE_SUCCESS;

  /* for all streams */
  for (streams = src->streams; streams; streams = g_list_next (streams)) {
    GstRTSPStream *stream;

    stream = (GstRTSPStream *) streams->data;

    /* first our rtp session manager */
    if ((ret =
            gst_element_set_state (stream->rtpdec,
                state)) == GST_STATE_CHANGE_FAILURE)
      goto done;

    /* then our sources */
    if (stream->rtpsrc) {
      if ((ret =
              gst_element_set_state (stream->rtpsrc,
                  state)) == GST_STATE_CHANGE_FAILURE)
        goto done;
    }

    if (stream->rtcpsrc) {
      if ((ret =
              gst_element_set_state (stream->rtcpsrc,
                  state)) == GST_STATE_CHANGE_FAILURE)
        goto done;
    }
  }

done:
  return ret;
}

#define PARSE_INT(p, del, res)          \
G_STMT_START {                          \
  gchar *t = p;                         \
  p = strstr (p, del);                  \
  if (p == NULL)                        \
    res = -1;                           \
  else {                                \
    *p = '\0';                          \
    p++;                                \
    res = atoi (t);                     \
  }                                     \
} G_STMT_END

#define PARSE_STRING(p, del, res)       \
G_STMT_START {                          \
  gchar *t = p;                         \
  p = strstr (p, del);                  \
  if (p == NULL)                        \
    res = NULL;                         \
  else {                                \
    *p = '\0';                          \
    p++;                                \
    res = t;                            \
  }                                     \
} G_STMT_END

#define SKIP_SPACES(p)                  \
  while (*p && g_ascii_isspace (*p))    \
    p++;

static gboolean
gst_rtspsrc_parse_rtpmap (gchar * rtpmap, gint * payload, gchar ** name,
    gint * rate, gchar ** params)
{
  gchar *p, *t;

  t = p = rtpmap;

  PARSE_INT (p, " ", *payload);
  if (*payload == -1)
    return FALSE;

  SKIP_SPACES (p);
  if (*p == '\0')
    return FALSE;

  PARSE_STRING (p, "/", *name);
  if (*name == NULL)
    return FALSE;

  t = p;
  p = strstr (p, "/");
  if (p == NULL) {
    *rate = atoi (t);
    return TRUE;
  }
  *p = '\0';
  p++;
  *rate = atoi (t);

  t = p;
  if (*p == '\0')
    return TRUE;
  *params = t;

  return TRUE;
}

/*
 *  Mapping of caps to and from SDP fields:
 *
 *   m=<media> <udp port> RTP/AVP <payload> 
 *   a=rtpmap:<payload> <encoding_name>/<clock_rate>[/<encoding_params>]
 *   a=fmtp:<payload> <param>=<value>;...
 */
static GstCaps *
gst_rtspsrc_media_to_caps (SDPMedia * media)
{
  GstCaps *caps;
  gchar *payload;
  gchar *rtpmap;
  gchar *fmtp;
  gint pt;
  gchar *name = NULL;
  gint rate = -1;
  gchar *params = NULL;
  GstStructure *s;

  payload = sdp_media_get_format (media, 0);
  if (payload == NULL) {
    g_warning ("payload type not given");
    return NULL;
  }
  pt = atoi (payload);

  if (pt >= 96) {
    gint payload = 0;
    gboolean ret;

    if ((rtpmap = sdp_media_get_attribute_val (media, "rtpmap"))) {
      if ((ret =
              gst_rtspsrc_parse_rtpmap (rtpmap, &payload, &name, &rate,
                  &params))) {
        if (payload != pt) {
          g_warning ("rtpmap of wrong payload type");
          name = NULL;
          rate = -1;
          params = NULL;
        }
      } else {
        g_warning ("error parsing rtpmap");
      }
    } else {
      g_warning ("rtpmap type not given fot dynamic payload %d", pt);
      return NULL;
    }
  }

  caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, media->media, "payload", G_TYPE_INT, pt, NULL);
  s = gst_caps_get_structure (caps, 0);

  if (rate != -1)
    gst_structure_set (s, "clock-rate", G_TYPE_INT, rate, NULL);

  if (name != NULL)
    gst_structure_set (s, "encoding-name", G_TYPE_STRING, name, NULL);

  if (params != NULL)
    gst_structure_set (s, "encoding-params", G_TYPE_STRING, params, NULL);

  /* parse optional fmtp: field */
  if ((fmtp = sdp_media_get_attribute_val (media, "fmtp"))) {
    gchar *p;
    gint payload = 0;

    p = fmtp;

    PARSE_INT (p, " ", payload);
    if (payload != -1 && payload == pt) {
      gchar **pairs;
      gint i;

      pairs = g_strsplit (p, ";", 0);
      for (i = 0; pairs[i]; i++) {
        gchar **keyval;

        keyval = g_strsplit (pairs[i], "=", 0);
        if (keyval[0]) {
          gchar *val, *key;

          if (keyval[1])
            val = g_strstrip (keyval[1]);
          else
            val = "1";

          key = g_strstrip (keyval[0]);

          gst_structure_set (s, key, G_TYPE_STRING, val, NULL);
        }
        g_strfreev (keyval);
      }
      g_strfreev (pairs);
    }
  }

  return caps;
}

static gboolean
gst_rtspsrc_stream_setup_rtp (GstRTSPStream * stream, SDPMedia * media,
    gint * rtpport, gint * rtcpport)
{
  GstStateChangeReturn ret;
  GstRTSPSrc *src;
  GstCaps *caps;
  GstElement *tmp, *rtp, *rtcp;
  gint tmp_rtp, tmp_rtcp;
  guint count;

  src = stream->parent;

  tmp = NULL;
  rtp = NULL;
  rtcp = NULL;
  count = 0;

  /* try to allocate 2 udp ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  rtp = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0:0", NULL);
  if (rtp == NULL)
    goto no_udp_rtp_protocol;

  ret = gst_element_set_state (rtp, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_rtp_failure;

  g_object_get (G_OBJECT (rtp), "port", &tmp_rtp, NULL);
  GST_DEBUG_OBJECT (src, "got RTP port %d", tmp_rtp);

  /* check if port is even */
  if ((tmp_rtp & 0x01) != 0) {
    /* port not even, close and allocate another */
    count++;
    if (count > src->retry)
      goto no_ports;

    GST_DEBUG_OBJECT (src, "RTP port not even, retry %d", count);
    /* have to keep port allocated so we can get a new one */
    if (tmp != NULL) {
      GST_DEBUG_OBJECT (src, "free temp");
      gst_element_set_state (tmp, GST_STATE_NULL);
      gst_object_unref (tmp);
    }
    tmp = rtp;
    GST_DEBUG_OBJECT (src, "retry %d", count);
    goto again;
  }
  /* free leftover temp element/port */
  if (tmp) {
    gst_element_set_state (tmp, GST_STATE_NULL);
    gst_object_unref (tmp);
    tmp = NULL;
  }

  /* allocate port+1 for RTCP now */
  rtcp = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
  if (rtcp == NULL)
    goto no_udp_rtcp_protocol;

  /* set port */
  tmp_rtcp = tmp_rtp + 1;
  g_object_set (G_OBJECT (rtcp), "port", tmp_rtcp, NULL);

  GST_DEBUG_OBJECT (src, "starting RTCP on port %d", tmp_rtcp);
  ret = gst_element_set_state (rtcp, GST_STATE_PAUSED);
  /* FIXME, this could fail if the next port is not free, we
   * should retry with another port then */
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_rtcp_failure;

  /* all fine, do port check */
  g_object_get (G_OBJECT (rtp), "port", rtpport, NULL);
  g_object_get (G_OBJECT (rtcp), "port", rtcpport, NULL);

  /* this should not happen */
  if (*rtpport != tmp_rtp || *rtcpport != tmp_rtcp)
    goto port_error;

  /* we manage these elements */
  stream->rtpsrc = rtp;
  gst_rtspsrc_add_element (src, stream->rtpsrc);
  stream->rtcpsrc = rtcp;
  gst_rtspsrc_add_element (src, stream->rtcpsrc);

  caps = gst_rtspsrc_media_to_caps (media);

  /* set caps */
  g_object_set (G_OBJECT (stream->rtpsrc), "caps", caps, NULL);

  return TRUE;

  /* ERRORS */
no_udp_rtp_protocol:
  {
    GST_DEBUG ("could not get UDP source for RTP");
    goto cleanup;
  }
start_rtp_failure:
  {
    GST_DEBUG ("could not start UDP source for RTP");
    goto cleanup;
  }
no_ports:
  {
    GST_DEBUG ("could not allocate UDP port pair after %d retries", count);
    goto cleanup;
  }
no_udp_rtcp_protocol:
  {
    GST_DEBUG ("could not get UDP source for RTCP");
    goto cleanup;
  }
start_rtcp_failure:
  {
    GST_DEBUG ("could not start UDP source for RTCP");
    goto cleanup;
  }
port_error:
  {
    GST_DEBUG ("ports don't match rtp: %d<->%d, rtcp: %d<->%d",
        tmp_rtp, *rtpport, tmp_rtcp, *rtcpport);
    goto cleanup;
  }
cleanup:
  {
    if (tmp) {
      gst_element_set_state (tmp, GST_STATE_NULL);
      gst_object_unref (tmp);
    }
    if (rtp) {
      gst_element_set_state (rtp, GST_STATE_NULL);
      gst_object_unref (rtp);
    }
    if (rtcp) {
      gst_element_set_state (rtcp, GST_STATE_NULL);
      gst_object_unref (rtcp);
    }
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_stream_configure_transport (GstRTSPStream * stream,
    RTSPTransport * transport)
{
  GstRTSPSrc *src;
  GstPad *pad;
  GstStateChangeReturn ret;
  gchar *name;

  src = stream->parent;

  if (!(stream->rtpdec = gst_element_factory_make ("rtpdec", NULL)))
    goto no_element;

  /* we manage this element */
  gst_rtspsrc_add_element (src, stream->rtpdec);

  if ((ret =
          gst_element_set_state (stream->rtpdec,
              GST_STATE_PAUSED)) != GST_STATE_CHANGE_SUCCESS)
    goto start_rtpdec_failure;

  stream->rtpdecrtp = gst_element_get_pad (stream->rtpdec, "sinkrtp");
  stream->rtpdecrtcp = gst_element_get_pad (stream->rtpdec, "sinkrtcp");

  if (transport->lower_transport == RTSP_LOWER_TRANS_TCP) {
    /* configure for interleaved delivery, nothing needs to be done
     * here, the loop function will call the chain functions of the
     * rtp session manager. */
  } else {
    /* configure for UDP delivery, we need to connect the udp pads to
     * the rtp session plugin. */
    pad = gst_element_get_pad (stream->rtpsrc, "src");
    gst_pad_link (pad, stream->rtpdecrtp);
    gst_object_unref (pad);

    pad = gst_element_get_pad (stream->rtcpsrc, "src");
    gst_pad_link (pad, stream->rtpdecrtcp);
    gst_object_unref (pad);
  }

  pad = gst_element_get_pad (stream->rtpdec, "srcrtp");
  name = g_strdup_printf ("rtp_stream%d", stream->id);
  gst_element_add_pad (GST_ELEMENT_CAST (src), gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  return TRUE;

no_element:
  {
    GST_DEBUG ("no rtpdec element found");
    return FALSE;
  }
start_rtpdec_failure:
  {
    GST_DEBUG ("could not start RTP session");
    return FALSE;
  }
}

static gint
find_stream (GstRTSPStream * stream, gconstpointer a)
{
  gint channel = GPOINTER_TO_INT (a);

  if (stream->rtpchannel == channel || stream->rtcpchannel == channel)
    return 0;

  return -1;
}

static void
gst_rtspsrc_loop (GstRTSPSrc * src)
{
  RTSPMessage response = { 0 };
  RTSPResult res;
  gint channel;
  GList *lstream;
  GstRTSPStream *stream;
  GstPad *outpad = NULL;
  guint8 *data;
  guint size;

  do {
    GST_DEBUG ("doing reveive");
    if ((res = rtsp_connection_receive (src->connection, &response)) < 0)
      goto receive_error;
    GST_DEBUG ("got packet");
  }
  while (response.type != RTSP_MESSAGE_DATA);

  channel = response.type_data.data.channel;

  lstream = g_list_find_custom (src->streams, GINT_TO_POINTER (channel),
      (GCompareFunc) find_stream);
  if (!lstream)
    goto unknown_stream;

  stream = (GstRTSPStream *) lstream->data;
  if (channel == stream->rtpchannel)
    outpad = stream->rtpdecrtp;
  else if (channel == stream->rtcpchannel)
    outpad = stream->rtpdecrtcp;

  rtsp_message_get_body (&response, &data, &size);

  /* channels are not correct on some servers, do extra check */
  if (data[1] >= 200 && data[1] <= 204) {
    /* hmm RTCP message */
    outpad = stream->rtpdecrtcp;
  }

  /* we have no clue what this is, just ignore then. */
  if (outpad == NULL)
    goto unknown_stream;

  /* and chain buffer to internal element */
  {
    GstBuffer *buf;

    buf = gst_buffer_new_and_alloc (size);
    memcpy (GST_BUFFER_DATA (buf), data, size);

    if (gst_pad_chain (outpad, buf) != GST_FLOW_OK)
      goto need_pause;
  }

unknown_stream:

  return;

receive_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not receive message."), (NULL));
    /*
       gst_pad_push_event (src->srcpad, gst_event_new (GST_EVENT_EOS));
     */
    goto need_pause;
  }
need_pause:
  {
    gst_task_pause (src->task);
    return;
  }
}

static gboolean
gst_rtspsrc_send (GstRTSPSrc * src, RTSPMessage * request,
    RTSPMessage * response, RTSPStatusCode * code)
{
  RTSPResult res;

  if (src->debug) {
    rtsp_message_dump (request);
  }
  if ((res = rtsp_connection_send (src->connection, request)) < 0)
    goto send_error;

  if ((res = rtsp_connection_receive (src->connection, response)) < 0)
    goto receive_error;

  if (code) {
    *code = response->type_data.response.code;
  }

  if (src->debug) {
    rtsp_message_dump (response);
  }
  if (response->type_data.response.code != RTSP_STS_OK)
    goto error_response;

  return TRUE;

send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
receive_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ,
        ("Could not receive message."), (NULL));
    return FALSE;
  }
error_response:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, READ, ("Got error response: %d (%s).",
            response->type_data.response.code,
            response->type_data.response.reason), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_open (GstRTSPSrc * src)
{
  RTSPUrl *url;
  RTSPResult res;
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  guint8 *data;
  guint size;
  SDPMessage sdp = { 0 };
  GstRTSPProto protocols;

  /* parse url */
  GST_DEBUG ("parsing url...");
  if ((res = rtsp_url_parse (src->location, &url)) < 0)
    goto invalid_url;

  /* open connection */
  GST_DEBUG ("opening connection...");
  if ((res = rtsp_connection_open (url, &src->connection)) < 0)
    goto could_not_open;

  /* create OPTIONS */
  GST_DEBUG ("create options...");
  if ((res =
          rtsp_message_init_request (RTSP_OPTIONS, src->location,
              &request)) < 0)
    goto create_request_failed;

  /* send OPTIONS */
  GST_DEBUG ("send options...");
  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  {
    gchar *respoptions = NULL;
    gchar **options;
    gint i;

    /* Try Allow Header first */
    rtsp_message_get_header (&response, RTSP_HDR_ALLOW, &respoptions);
    if (!respoptions) {
      /* Then maybe Public Header... */
      rtsp_message_get_header (&response, RTSP_HDR_PUBLIC, &respoptions);
      if (!respoptions) {
        /* this field is not required, assume the server supports
         * DESCRIBE and SETUP*/
        GST_DEBUG_OBJECT (src, "could not get OPTIONS");
        src->options = RTSP_DESCRIBE | RTSP_SETUP;
        goto no_options;
      }
    }

    /* parse options */
    options = g_strsplit (respoptions, ",", 0);

    i = 0;
    while (options[i]) {
      gchar *stripped;
      gint method;

      stripped = g_strdup (options[i]);
      stripped = g_strstrip (stripped);
      method = rtsp_find_method (stripped);
      g_free (stripped);

      /* keep bitfield of supported methods */
      if (method != -1)
        src->options |= method;
      i++;
    }
    g_strfreev (options);

  no_options:
    /* we need describe and setup */
    if (!(src->options & RTSP_DESCRIBE))
      goto no_describe;
    if (!(src->options & RTSP_SETUP))
      goto no_setup;
  }

  /* create DESCRIBE */
  GST_DEBUG ("create describe...");
  if ((res =
          rtsp_message_init_request (RTSP_DESCRIBE, src->location,
              &request)) < 0)
    goto create_request_failed;
  /* we accept SDP for now */
  rtsp_message_add_header (&request, RTSP_HDR_ACCEPT, "application/sdp");

  /* send DESCRIBE */
  GST_DEBUG ("send describe...");
  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  /* check if reply is SDP */
  {
    gchar *respcont = NULL;

    rtsp_message_get_header (&response, RTSP_HDR_CONTENT_TYPE, &respcont);
    /* could not be set but since the request returned OK, we assume it
     * was SDP, else check it. */
    if (respcont) {
      if (!g_ascii_strcasecmp (respcont, "application/sdp") == 0)
        goto wrong_content_type;
    }
  }

  /* parse SDP */
  rtsp_message_get_body (&response, &data, &size);

  GST_DEBUG ("parse sdp...");
  sdp_message_init (&sdp);
  sdp_message_parse_buffer (data, size, &sdp);

  if (src->debug)
    sdp_message_dump (&sdp);

  /* we allow all configured protocols */
  protocols = src->protocols;
  /* setup streams */
  {
    gint i;

    for (i = 0; i < sdp_message_medias_len (&sdp); i++) {
      SDPMedia *media;
      gchar *setup_url;
      gchar *control_url;
      gchar *transports;
      GstRTSPStream *stream;

      media = sdp_message_get_media (&sdp, i);

      stream = gst_rtspsrc_create_stream (src);

      GST_DEBUG ("setup media %d", i);
      control_url = sdp_media_get_attribute_val (media, "control");
      if (control_url == NULL) {
        GST_DEBUG ("no control url found, skipping stream");
        continue;
      }

      /* check absolute/relative URL */
      /* FIXME, what if the control_url starts with a '/' or a non rtsp: protocol? */
      if (g_str_has_prefix (control_url, "rtsp://")) {
        setup_url = g_strdup (control_url);
      } else {
        setup_url = g_strdup_printf ("%s/%s", src->location, control_url);
      }

      GST_DEBUG ("setup %s", setup_url);
      /* create SETUP request */
      if ((res =
              rtsp_message_init_request (RTSP_SETUP, setup_url,
                  &request)) < 0) {
        g_free (setup_url);
        goto create_request_failed;
      }
      g_free (setup_url);

      transports = g_strdup ("");
      if (protocols & GST_RTSP_PROTO_UDP_UNICAST) {
        gchar *new;
        gint rtpport, rtcpport;
        gchar *trxparams;

        /* allocate two udp ports */
        if (!gst_rtspsrc_stream_setup_rtp (stream, media, &rtpport, &rtcpport))
          goto setup_rtp_failed;

        trxparams = g_strdup_printf ("client_port=%d-%d", rtpport, rtcpport);
        new = g_strconcat (transports, "RTP/AVP/UDP;unicast;", trxparams, NULL);
        g_free (trxparams);
        g_free (transports);
        transports = new;
      }
      if (protocols & GST_RTSP_PROTO_UDP_MULTICAST) {
        gchar *new;

        new =
            g_strconcat (transports, transports[0] ? "," : "",
            "RTP/AVP/UDP;multicast", NULL);
        g_free (transports);
        transports = new;
      }
      if (protocols & GST_RTSP_PROTO_TCP) {
        gchar *new;

        new =
            g_strconcat (transports, transports[0] ? "," : "", "RTP/AVP/TCP",
            NULL);
        g_free (transports);
        transports = new;
      }

      /* select transport, copy is made when adding to header */
      rtsp_message_add_header (&request, RTSP_HDR_TRANSPORT, transports);
      g_free (transports);

      if (!gst_rtspsrc_send (src, &request, &response, NULL))
        goto send_error;

      /* parse response transport */
      {
        gchar *resptrans = NULL;
        RTSPTransport transport = { 0 };

        rtsp_message_get_header (&response, RTSP_HDR_TRANSPORT, &resptrans);
        if (!resptrans)
          goto no_transport;

        /* parse transport */
        rtsp_transport_parse (resptrans, &transport);
        /* update allowed transports for other streams */
        if (transport.lower_transport == RTSP_LOWER_TRANS_TCP) {
          protocols = GST_RTSP_PROTO_TCP;
          src->interleaved = TRUE;
        } else {
          if (transport.multicast) {
            /* disable unicast */
            protocols = GST_RTSP_PROTO_UDP_MULTICAST;
          } else {
            /* disable multicast */
            protocols = GST_RTSP_PROTO_UDP_UNICAST;
          }
        }
        /* now configure the stream with the transport */
        if (!gst_rtspsrc_stream_configure_transport (stream, &transport)) {
          GST_DEBUG ("could not configure stream transport, skipping stream");
        }
        /* clean up our transport struct */
        rtsp_transport_init (&transport);
      }
    }
  }
  return TRUE;

  /* ERRORS */
invalid_url:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND,
        ("Not a valid RTSP url."), (NULL));
    return FALSE;
  }
could_not_open:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE,
        ("Could not open connection."), (NULL));
    return FALSE;
  }
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
no_describe:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Server does not support DESCRIBE."), (NULL));
    return FALSE;
  }
no_setup:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Server does not support SETUP."), (NULL));
    return FALSE;
  }
wrong_content_type:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Server does not support SDP."), (NULL));
    return FALSE;
  }
setup_rtp_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, ("Could not setup rtp."), (NULL));
    return FALSE;
  }
no_transport:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Server did not select transport."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_close (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  GST_DEBUG ("TEARDOWN...");

  /* stop task if any */
  if (src->task) {
    gst_task_stop (src->task);
    gst_object_unref (GST_OBJECT (src->task));
    src->task = NULL;
  }

  if (src->options & RTSP_PLAY) {
    /* do TEARDOWN */
    if ((res =
            rtsp_message_init_request (RTSP_TEARDOWN, src->location,
                &request)) < 0)
      goto create_request_failed;

    if (!gst_rtspsrc_send (src, &request, &response, NULL))
      goto send_error;
  }

  /* close connection */
  GST_DEBUG ("closing connection...");
  if ((res = rtsp_connection_close (src->connection)) < 0)
    goto close_failed;

  return TRUE;

create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
close_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, CLOSE, ("Close failed."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_play (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  if (!(src->options & RTSP_PLAY))
    return TRUE;

  GST_DEBUG ("PLAY...");

  /* do play */
  if ((res =
          rtsp_message_init_request (RTSP_PLAY, src->location, &request)) < 0)
    goto create_request_failed;

  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  if (src->interleaved) {
    src->task = gst_task_create ((GstTaskFunction) gst_rtspsrc_loop, src);

    gst_task_start (src->task);
  }

  return TRUE;

create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_pause (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  if (!(src->options & RTSP_PAUSE))
    return TRUE;

  GST_DEBUG ("PAUSE...");
  /* do pause */
  if ((res =
          rtsp_message_init_request (RTSP_PAUSE, src->location, &request)) < 0)
    goto create_request_failed;

  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  return TRUE;

create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    return FALSE;
  }
send_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    return FALSE;
  }
}

static GstStateChangeReturn
gst_rtspsrc_change_state (GstElement * element, GstStateChange transition)
{
  GstRTSPSrc *rtspsrc;
  GstStateChangeReturn ret;

  rtspsrc = GST_RTSPSRC (element);


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rtspsrc->interleaved = FALSE;
      rtspsrc->options = 0;
      if (!gst_rtspsrc_open (rtspsrc))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_rtspsrc_play (rtspsrc);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  ret = gst_rtspsrc_set_state (rtspsrc, GST_STATE_PENDING (rtspsrc));
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_rtspsrc_pause (rtspsrc);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtspsrc_close (rtspsrc);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

done:
  return ret;

open_failed:
  {
    return GST_STATE_CHANGE_FAILURE;
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static guint
gst_rtspsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_rtspsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "rtsp", NULL };

  return protocols;
}

static const gchar *
gst_rtspsrc_uri_get_uri (GstURIHandler * handler)
{
  GstRTSPSrc *src = GST_RTSPSRC (handler);

  return g_strdup (src->location);
}

static gboolean
gst_rtspsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstRTSPSrc *src = GST_RTSPSRC (handler);

  g_free (src->location);
  src->location = g_strdup (uri);

  return TRUE;
}

static void
gst_rtspsrc_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rtspsrc_uri_get_type;
  iface->get_protocols = gst_rtspsrc_uri_get_protocols;
  iface->get_uri = gst_rtspsrc_uri_get_uri;
  iface->set_uri = gst_rtspsrc_uri_set_uri;
}
