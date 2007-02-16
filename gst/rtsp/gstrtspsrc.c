/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim at fluendo dot com>
 *               <2006> Lutz Mueller <lutz at topfrose dot de>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/**
 * SECTION:element-rtspsrc
 *
 * <refsect2>
 * <para>
 * Makes a connection to an RTSP server and read the data.
 * rtspsrc strictly follows RFC 2326 and therefore does not (yet) support
 * RealMedia/Quicktime/Microsoft extensions.
 * </para>
 * <para>
 * RTSP supports transport over TCP or UDP in unicast or multicast mode. By
 * default rtspsrc will negotiate a connection in the following order:
 * UDP unicast/UDP multicast/TCP. The order cannot be changed but the allowed
 * protocols can be controlled with the "protocols" property.
 * </para>
 * <para>
 * rtspsrc currently understands SDP as the format of the session description.
 * For each stream listed in the SDP a new rtp_stream%d pad will be created
 * with caps derived from the SDP media description. This is a caps of mime type
 * "application/x-rtp" that can be connected to any available RTP depayloader
 * element. 
 * </para>
 * <para>
 * rtspsrc will internally instantiate an RTP session manager element
 * that will handle the RTCP messages to and from the server, jitter removal,
 * packet reordering along with providing a clock for the pipeline. 
 * This feature is however currently not yet implemented.
 * </para>
 * <para>
 * rtspsrc acts like a live source and will therefore only generate data in the 
 * PLAYING state.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch rtspsrc location=rtsp://some.server/url ! fakesink
 * </programlisting>
 * Establish a connection to an RTSP server and send the raw RTP packets to a fakesink.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-08-18 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "gstrtspsrc.h"
#include "sdp.h"

/* define for experimental real support */
#undef WITH_EXT_REAL

#include "rtspextwms.h"
#ifdef WITH_EXT_REAL
#include "rtspextreal.h"
#endif

GST_DEBUG_CATEGORY_STATIC (rtspsrc_debug);
#define GST_CAT_DEFAULT (rtspsrc_debug)

/* elementfactory information */
static const GstElementDetails gst_rtspsrc_details =
GST_ELEMENT_DETAILS ("RTSP packet receiver",
    "Source/Network",
    "Receive data over the network via RTSP (RFC 2326)",
    "Wim Taymans <wim@fluendo.com>\n"
    "Thijs Vermeir <thijs.vermeir@barco.com>\n"
    "Lutz Mueller <lutz@topfrose.de>");

static GstStaticPadTemplate rtptemplate = GST_STATIC_PAD_TEMPLATE ("stream%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("application/x-rtp; application/x-rdt"));

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_LOCATION        NULL
#define DEFAULT_PROTOCOLS       RTSP_LOWER_TRANS_UDP | RTSP_LOWER_TRANS_UDP_MCAST | RTSP_LOWER_TRANS_TCP
#define DEFAULT_DEBUG           FALSE
#define DEFAULT_RETRY           20
#define DEFAULT_TIMEOUT         5000000

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PROTOCOLS,
  PROP_DEBUG,
  PROP_RETRY,
  PROP_TIMEOUT,
};

#define GST_TYPE_RTSP_LOWER_TRANS (gst_rtsp_lower_trans_get_type())
static GType
gst_rtsp_lower_trans_get_type (void)
{
  static GType rtsp_lower_trans_type = 0;
  static const GFlagsValue rtsp_lower_trans[] = {
    {RTSP_LOWER_TRANS_UDP, "UDP Unicast Mode", "udp-unicast"},
    {RTSP_LOWER_TRANS_UDP_MCAST, "UDP Multicast Mode", "udp-multicast"},
    {RTSP_LOWER_TRANS_TCP, "TCP interleaved mode", "tcp"},
    {0, NULL, NULL},
  };

  if (!rtsp_lower_trans_type) {
    rtsp_lower_trans_type =
        g_flags_register_static ("GstRTSPLowerTrans", rtsp_lower_trans);
  }
  return rtsp_lower_trans_type;
}

static void gst_rtspsrc_base_init (gpointer g_class);
static void gst_rtspsrc_finalize (GObject * object);

static void gst_rtspsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtspsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_rtspsrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);
static GstCaps *gst_rtspsrc_media_to_caps (gint pt, SDPMedia * media);

static GstStateChangeReturn gst_rtspsrc_change_state (GstElement * element,
    GstStateChange transition);
static void gst_rtspsrc_handle_message (GstBin * bin, GstMessage * message);

static gboolean gst_rtspsrc_open (GstRTSPSrc * src);
static gboolean gst_rtspsrc_play (GstRTSPSrc * src);
static gboolean gst_rtspsrc_pause (GstRTSPSrc * src);
static gboolean gst_rtspsrc_close (GstRTSPSrc * src);

static gboolean gst_rtspsrc_uri_set_uri (GstURIHandler * handler,
    const gchar * uri);

static gboolean gst_rtspsrc_activate_streams (GstRTSPSrc * src);
static void gst_rtspsrc_loop (GstRTSPSrc * src);

/* commands we send to out loop to notify it of events */
#define CMD_WAIT	0
#define CMD_RECONNECT	1
#define CMD_STOP	2

/*static guint gst_rtspsrc_signals[LAST_SIGNAL] = { 0 }; */

static void
_do_init (GType rtspsrc_type)
{
  static const GInterfaceInfo urihandler_info = {
    gst_rtspsrc_uri_handler_init,
    NULL,
    NULL
  };

  GST_DEBUG_CATEGORY_INIT (rtspsrc_debug, "rtspsrc", 0, "RTSP src");

  g_type_add_interface_static (rtspsrc_type, GST_TYPE_URI_HANDLER,
      &urihandler_info);
}

GST_BOILERPLATE_FULL (GstRTSPSrc, gst_rtspsrc, GstBin, GST_TYPE_BIN, _do_init);

static void
gst_rtspsrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&rtptemplate));

  gst_element_class_set_details (element_class, &gst_rtspsrc_details);
}

static void
gst_rtspsrc_class_init (GstRTSPSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;

  gobject_class->set_property = gst_rtspsrc_set_property;
  gobject_class->get_property = gst_rtspsrc_get_property;

  gobject_class->finalize = gst_rtspsrc_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "RTSP Location",
          "Location of the RTSP url to read",
          DEFAULT_LOCATION, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols",
          "Allowed lower transport protocols", GST_TYPE_RTSP_LOWER_TRANS,
          DEFAULT_PROTOCOLS, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug",
          "Dump request and response messages to stdout",
          DEFAULT_DEBUG, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_RETRY,
      g_param_spec_uint ("retry", "Retry",
          "Max number of retries when allocating RTP ports.",
          0, G_MAXUINT16, DEFAULT_RETRY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Retry TCP transport after timeout microseconds (0 = disabled)",
          0, G_MAXUINT64, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  gstelement_class->change_state = gst_rtspsrc_change_state;

  gstbin_class->handle_message = gst_rtspsrc_handle_message;
}

static void
gst_rtspsrc_init (GstRTSPSrc * src, GstRTSPSrcClass * g_class)
{
  src->stream_rec_lock = g_new (GStaticRecMutex, 1);
  g_static_rec_mutex_init (src->stream_rec_lock);

  src->loop_cond = g_cond_new ();

  src->location = g_strdup (DEFAULT_LOCATION);
  src->url = NULL;

  /* install WMS extension by default */
  src->extension = rtsp_ext_wms_get_context ();
#ifdef WITH_EXT_REAL
  src->extension = rtsp_ext_real_get_context ();
#endif
  src->extension->src = (gpointer) src;
}

static void
gst_rtspsrc_finalize (GObject * object)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  g_static_rec_mutex_free (rtspsrc->stream_rec_lock);
  g_free (rtspsrc->stream_rec_lock);
  g_cond_free (rtspsrc->loop_cond);
  g_free (rtspsrc->location);
  g_free (rtspsrc->content_base);
  rtsp_url_free (rtspsrc->url);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtspsrc_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_rtspsrc_uri_set_uri (GST_URI_HANDLER (rtspsrc),
          g_value_get_string (value));
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
    case PROP_TIMEOUT:
      rtspsrc->timeout = g_value_get_uint64 (value);
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
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, rtspsrc->timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
find_stream_by_pt (GstRTSPStream * stream, gconstpointer a)
{
  gint pt = GPOINTER_TO_INT (a);

  if (stream->pt == pt)
    return 0;

  return -1;
}

static GstRTSPStream *
gst_rtspsrc_create_stream (GstRTSPSrc * src, SDPMessage * sdp, gint idx)
{
  GstRTSPStream *stream;
  gchar *control_url;
  gchar *payload;
  SDPMedia *media;

  /* get media, should not return NULL */
  media = sdp_message_get_media (sdp, idx);
  if (media == NULL)
    return NULL;

  stream = g_new0 (GstRTSPStream, 1);
  stream->parent = src;
  /* we mark the pad as not linked, we will mark it as OK when we add the pad to
   * the element. */
  stream->last_ret = GST_FLOW_NOT_LINKED;
  stream->added = FALSE;
  stream->id = src->numstreams++;

  /* we must have a payload. No payload means we cannot create caps */
  /* FIXME, handle multiple formats. */
  if ((payload = sdp_media_get_format (media, 0))) {
    stream->pt = atoi (payload);
    /* convert caps */
    stream->caps = gst_rtspsrc_media_to_caps (stream->pt, media);

    if (stream->pt >= 96) {
      /* If we have a dynamic payload type, see if we have a stream with the
       * same payload number. If there is one, they are part of the same
       * container and we only need to add one pad. */
      if (g_list_find_custom (src->streams, GINT_TO_POINTER (stream->pt),
              (GCompareFunc) find_stream_by_pt)) {
        stream->container = TRUE;
      }
    }
  }

  /* get control url to construct the setup url. The setup url is used to
   * configure the transport of the stream and is used to identity the stream in
   * the RTP-Info header field returned from PLAY. */
  control_url = sdp_media_get_attribute_val (media, "control");

  GST_DEBUG_OBJECT (src, "stream %d", stream->id);
  GST_DEBUG_OBJECT (src, " pt: %d", stream->pt);
  GST_DEBUG_OBJECT (src, " container: %d", stream->container);
  GST_DEBUG_OBJECT (src, " caps: %" GST_PTR_FORMAT, stream->caps);
  GST_DEBUG_OBJECT (src, " control: %s", GST_STR_NULL (control_url));

  if (control_url != NULL) {
    /* If the control_url starts with a '/' or a non rtsp: protocol we will most
     * likely build a URL that the server will fail to understand, this is ok,
     * we will fail then. */
    if (g_str_has_prefix (control_url, "rtsp://"))
      stream->setup_url = g_strdup (control_url);
    else if (src->content_base)
      stream->setup_url =
          g_strdup_printf ("%s%s", src->content_base, control_url);
    else
      stream->setup_url = g_strdup_printf ("%s/%s", src->location, control_url);
  }
  GST_DEBUG_OBJECT (src, " setup: %s", GST_STR_NULL (stream->setup_url));

  /* we keep track of all streams */
  src->streams = g_list_append (src->streams, stream);

  return stream;
}

static void
gst_rtspsrc_stream_free (GstRTSPSrc * src, GstRTSPStream * stream)
{
  gint i;

  GST_DEBUG_OBJECT (src, "free stream %p", stream);

  if (stream->caps)
    gst_caps_unref (stream->caps);

  g_free (stream->setup_url);

  for (i = 0; i < 2; i++) {
    GstElement *udpsrc = stream->udpsrc[i];

    if (udpsrc) {
      GstPad *pad;

      /* unlink the pad */
      pad = gst_element_get_pad (udpsrc, "src");
      if (stream->channelpad[i]) {
        gst_pad_unlink (pad, stream->channelpad[i]);
        gst_object_unref (stream->channelpad[i]);
        stream->channelpad[i] = NULL;
      }

      gst_element_set_state (udpsrc, GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (src), udpsrc);
      gst_object_unref (stream->udpsrc[i]);
      stream->udpsrc[i] = NULL;
    }
  }
  if (stream->sess) {
    gst_element_set_state (stream->sess, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (src), stream->sess);
    stream->sess = NULL;
  }
  if (stream->srcpad) {
    gst_pad_set_active (stream->srcpad, FALSE);
    if (stream->added) {
      gst_element_remove_pad (GST_ELEMENT_CAST (src), stream->srcpad);
      stream->added = FALSE;
    }
    stream->srcpad = NULL;
  }
  g_free (stream);
}

static void
gst_rtspsrc_cleanup (GstRTSPSrc * src)
{
  GList *walk;

  GST_DEBUG_OBJECT (src, "cleanup");

  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;

    gst_rtspsrc_stream_free (src, stream);
  }
  g_list_free (src->streams);
  src->streams = NULL;
  src->numstreams = 0;
  if (src->props)
    gst_structure_free (src->props);
  src->props = NULL;
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

/* rtpmap contains:
 *
 *  <payload> <encoding_name>/<clock_rate>[/<encoding_params>]
 */
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
  if (*name == NULL) {
    /* no rate, assume 0 then */
    *name = p;
    *rate = -1;
    return TRUE;
  }

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
 *   m=<media> <UDP port> RTP/AVP <payload> 
 *   a=rtpmap:<payload> <encoding_name>/<clock_rate>[/<encoding_params>]
 *   a=fmtp:<payload> <param>[=<value>];...
 */
static GstCaps *
gst_rtspsrc_media_to_caps (gint pt, SDPMedia * media)
{
  GstCaps *caps;
  gchar *rtpmap;
  gchar *fmtp;
  gchar *name = NULL;
  gint rate = -1;
  gchar *params = NULL;
  gchar *tmp;
  GstStructure *s;

  /* dynamic payloads need rtpmap */
  if (pt >= 96) {
    gint payload = 0;
    gboolean ret;

    if ((rtpmap = sdp_media_get_attribute_val (media, "rtpmap"))) {
      ret = gst_rtspsrc_parse_rtpmap (rtpmap, &payload, &name, &rate, &params);
      if (ret) {
        if (payload != pt) {
          /* FIXME, not fatal? */
          g_warning ("rtpmap of wrong payload type");
          name = NULL;
          rate = -1;
          params = NULL;
        }
      } else {
        /* FIXME, not fatal? */
        g_warning ("error parsing rtpmap");
      }
    } else
      goto no_rtpmap;
  }

  tmp = g_ascii_strdown (media->media, -1);
  caps = gst_caps_new_simple ("application/x-unknown",
      "media", G_TYPE_STRING, tmp, "payload", G_TYPE_INT, pt, NULL);
  g_free (tmp);
  s = gst_caps_get_structure (caps, 0);

  if (rate != -1)
    gst_structure_set (s, "clock-rate", G_TYPE_INT, rate, NULL);

  /* encoding name must be upper case */
  if (name != NULL) {
    tmp = g_ascii_strup (name, -1);
    gst_structure_set (s, "encoding-name", G_TYPE_STRING, tmp, NULL);
    g_free (tmp);
  }

  /* params must be lower case */
  if (params != NULL) {
    tmp = g_ascii_strdown (params, -1);
    gst_structure_set (s, "encoding-params", G_TYPE_STRING, tmp, NULL);
    g_free (tmp);
  }

  /* parse optional fmtp: field */
  if ((fmtp = sdp_media_get_attribute_val (media, "fmtp"))) {
    gchar *p;
    gint payload = 0;

    p = fmtp;

    /* p is now of the format <payload> <param>[=<value>];... */
    PARSE_INT (p, " ", payload);
    if (payload != -1 && payload == pt) {
      gchar **pairs;
      gint i;

      /* <param>[=<value>] are separated with ';' */
      pairs = g_strsplit (p, ";", 0);
      for (i = 0; pairs[i]; i++) {
        gchar *valpos;
        gchar *val, *key;

        /* the key may not have a '=', the value can have other '='s */
        valpos = strstr (pairs[i], "=");
        if (valpos) {
          /* we have a '=' and thus a value, remove the '=' with \0 */
          *valpos = '\0';
          /* value is everything between '=' and ';'. FIXME, strip? */
          val = g_strstrip (valpos + 1);
        } else {
          /* simple <param>;.. is translated into <param>=1;... */
          val = "1";
        }
        /* strip the key of spaces, convert key to lowercase but not the value. */
        key = g_strstrip (pairs[i]);
        tmp = g_ascii_strdown (key, -1);
        gst_structure_set (s, tmp, G_TYPE_STRING, val, NULL);
        g_free (tmp);
      }
      g_strfreev (pairs);
    }
  }
  return caps;

  /* ERRORS */
no_rtpmap:
  {
    g_warning ("rtpmap type not given for dynamic payload %d", pt);
    return NULL;
  }
}

static gboolean
gst_rtspsrc_alloc_udp_ports (GstRTSPStream * stream,
    gint * rtpport, gint * rtcpport)
{
  GstRTSPSrc *src;
  GstStateChangeReturn ret;
  GstElement *tmp, *udpsrc0, *udpsrc1;
  gint tmp_rtp, tmp_rtcp;
  guint count;

  src = stream->parent;

  tmp = NULL;
  udpsrc0 = NULL;
  udpsrc1 = NULL;
  count = 0;

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  udpsrc0 = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0:0", NULL);
  if (udpsrc0 == NULL)
    goto no_udp_protocol;

  ret = gst_element_set_state (udpsrc0, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_udp_failure;

  g_object_get (G_OBJECT (udpsrc0), "port", &tmp_rtp, NULL);
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
    tmp = udpsrc0;
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
  udpsrc1 = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
  if (udpsrc1 == NULL)
    goto no_udp_rtcp_protocol;

  /* set port */
  tmp_rtcp = tmp_rtp + 1;
  g_object_set (G_OBJECT (udpsrc1), "port", tmp_rtcp, NULL);

  GST_DEBUG_OBJECT (src, "starting RTCP on port %d", tmp_rtcp);
  ret = gst_element_set_state (udpsrc1, GST_STATE_PAUSED);
  /* FIXME, this could fail if the next port is not free, we
   * should retry with another port then */
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_rtcp_failure;

  /* all fine, do port check */
  g_object_get (G_OBJECT (udpsrc0), "port", rtpport, NULL);
  g_object_get (G_OBJECT (udpsrc1), "port", rtcpport, NULL);

  /* this should not happen */
  if (*rtpport != tmp_rtp || *rtcpport != tmp_rtcp)
    goto port_error;

  /* we keep these elements, we configure all in configure_transport when the
   * server told us to really use the UDP ports. */
  stream->udpsrc[0] = gst_object_ref (udpsrc0);
  stream->udpsrc[1] = gst_object_ref (udpsrc1);

  /* they are ours now */
  gst_object_sink (udpsrc0);
  gst_object_sink (udpsrc1);

  return TRUE;

  /* ERRORS */
no_udp_protocol:
  {
    GST_DEBUG_OBJECT (src, "could not get UDP source");
    goto cleanup;
  }
start_udp_failure:
  {
    GST_DEBUG_OBJECT (src, "could not start UDP source");
    goto cleanup;
  }
no_ports:
  {
    GST_DEBUG_OBJECT (src, "could not allocate UDP port pair after %d retries",
        count);
    goto cleanup;
  }
no_udp_rtcp_protocol:
  {
    GST_DEBUG_OBJECT (src, "could not get UDP source for RTCP");
    goto cleanup;
  }
start_rtcp_failure:
  {
    GST_DEBUG_OBJECT (src, "could not start UDP source for RTCP");
    goto cleanup;
  }
port_error:
  {
    GST_DEBUG_OBJECT (src, "ports don't match rtp: %d<->%d, rtcp: %d<->%d",
        tmp_rtp, *rtpport, tmp_rtcp, *rtcpport);
    goto cleanup;
  }
cleanup:
  {
    if (tmp) {
      gst_element_set_state (tmp, GST_STATE_NULL);
      gst_object_unref (tmp);
    }
    if (udpsrc0) {
      gst_element_set_state (udpsrc0, GST_STATE_NULL);
      gst_object_unref (udpsrc0);
    }
    if (udpsrc1) {
      gst_element_set_state (udpsrc1, GST_STATE_NULL);
      gst_object_unref (udpsrc1);
    }
    return FALSE;
  }
}

static void
pad_unblocked (GstPad * pad, gboolean blocked, GstRTSPSrc * src)
{
  GST_DEBUG_OBJECT (src, "pad %s:%s unblocked", GST_DEBUG_PAD_NAME (pad));
}

static void
pad_blocked (GstPad * pad, gboolean blocked, GstRTSPSrc * src)
{
  GST_DEBUG_OBJECT (src, "pad %s:%s blocked, activating streams",
      GST_DEBUG_PAD_NAME (pad));

  /* activate the streams */
  GST_OBJECT_LOCK (src);
  if (!src->need_activate)
    goto was_ok;

  src->need_activate = FALSE;
  GST_OBJECT_UNLOCK (src);

  gst_rtspsrc_activate_streams (src);

  return;

was_ok:
  {
    GST_OBJECT_UNLOCK (src);
    return;
  }
}

/* sets up all elements needed for streaming over the specified transport.
 * Does not yet expose the element pads, this will be done when there is actuall
 * dataflow detected, which might never happen when UDP is blocked in a
 * firewall, for example.
 */
static gboolean
gst_rtspsrc_stream_configure_transport (GstRTSPStream * stream,
    RTSPTransport * transport)
{
  GstRTSPSrc *src;
  GstPad *outpad = NULL;
  GstPadTemplate *template;
  GstStateChangeReturn ret;
  gchar *name;
  GstStructure *s;
  const gchar *mime, *manager;
  RTSPResult res;

  src = stream->parent;

  GST_DEBUG_OBJECT (src, "configuring transport for stream %p", stream);

  s = gst_caps_get_structure (stream->caps, 0);

  /* get the proper mime type for this stream now */
  if ((res = rtsp_transport_get_mime (transport->trans, &mime)) < 0)
    goto no_mime;
  if (!mime)
    goto no_mime;

  /* configure the final mime type */
  GST_DEBUG_OBJECT (src, "setting mime to %s", mime);
  gst_structure_set_name (s, mime);

  /* find a manager */
  if ((res = rtsp_transport_get_manager (transport->trans, &manager)) < 0)
    goto no_manager;

  if (manager) {
    GST_DEBUG_OBJECT (src, "using manager %s", manager);
    /* FIXME, the session manager needs to be shared with all the streams */
    if (!(stream->sess = gst_element_factory_make (manager, NULL)))
      goto no_element;

    /* we manage this element */
    gst_bin_add (GST_BIN_CAST (src), stream->sess);

    ret = gst_element_set_state (stream->sess, GST_STATE_PAUSED);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      goto start_session_failure;

    /* we stream directly to the manager, FIXME, pad names should not be
     * hardcoded. */
    stream->channelpad[0] = gst_element_get_pad (stream->sess, "sinkrtp");
    stream->channelpad[1] = gst_element_get_pad (stream->sess, "sinkrtcp");
  }

  if (transport->lower_transport == RTSP_LOWER_TRANS_TCP) {
    gint i;

    /* configure for interleaved delivery, nothing needs to be done
     * here, the loop function will call the chain functions of the
     * session manager. */
    stream->channel[0] = transport->interleaved.min;
    stream->channel[1] = transport->interleaved.max;
    GST_DEBUG_OBJECT (src, "stream %p on channels %d-%d", stream,
        stream->channel[0], stream->channel[1]);

    /* we can remove the allocated UDP ports now */
    for (i = 0; i < 2; i++) {
      if (stream->udpsrc[i]) {
        gst_element_set_state (stream->udpsrc[i], GST_STATE_NULL);
        gst_object_unref (stream->udpsrc[i]);
        stream->udpsrc[i] = NULL;
      }
    }

    /* no session manager, send data to srcpad directly */
    if (!stream->channelpad[0]) {
      GST_DEBUG_OBJECT (src, "no manager, creating pad");

      /* create a new pad we will use to stream to */
      name = g_strdup_printf ("stream%d", stream->id);
      template = gst_static_pad_template_get (&rtptemplate);
      stream->channelpad[0] = gst_pad_new_from_template (template, name);
      gst_object_unref (template);
      g_free (name);

      /* set caps and activate */
      gst_pad_use_fixed_caps (stream->channelpad[0]);
      gst_pad_set_caps (stream->channelpad[0], stream->caps);
      gst_pad_set_active (stream->channelpad[0], TRUE);

      outpad = gst_object_ref (stream->channelpad[0]);
    } else {
      GST_DEBUG_OBJECT (src, "using manager source pad");
      outpad = gst_element_get_pad (stream->sess, "srcrtp");
    }
  } else {
    /* multicast was selected, create UDP sources and join the multicast
     * group. */
    if (transport->lower_transport == RTSP_LOWER_TRANS_UDP_MCAST) {
      gchar *uri;

      GST_DEBUG_OBJECT (src, "creating UDP sources for multicast");

      /* creating UDP source */
      if (transport->port.min != -1) {
        uri = g_strdup_printf ("udp://%s:%d", transport->destination,
            transport->port.min);
        stream->udpsrc[0] = gst_element_make_from_uri (GST_URI_SRC, uri, NULL);
        g_free (uri);
        if (stream->udpsrc[0] == NULL)
          goto no_element;

        /* take ownership */
        gst_object_ref (stream->udpsrc[0]);
        gst_object_sink (stream->udpsrc[0]);

        /* change state */
        gst_element_set_state (stream->udpsrc[0], GST_STATE_READY);
      }

      /* creating another UDP source */
      if (transport->port.max != -1) {
        uri = g_strdup_printf ("udp://%s:%d", transport->destination,
            transport->port.max);
        stream->udpsrc[1] = gst_element_make_from_uri (GST_URI_SRC, uri, NULL);
        g_free (uri);
        if (stream->udpsrc[1] == NULL)
          goto no_element;

        /* take ownership */
        gst_object_ref (stream->udpsrc[0]);
        gst_object_sink (stream->udpsrc[0]);

        gst_element_set_state (stream->udpsrc[1], GST_STATE_READY);
      }
    }

    /* we manage the UDP elements now. For unicast, the UDP sources where
     * allocated in the stream when we suggested a transport. */
    if (stream->udpsrc[0]) {
      gst_bin_add (GST_BIN_CAST (src), stream->udpsrc[0]);

      GST_DEBUG_OBJECT (src, "setting up UDP source");

      /* set caps */
      g_object_set (G_OBJECT (stream->udpsrc[0]), "caps", stream->caps, NULL);

      /* configure a timeout on the UDP port. When the timeout message is
       * posted, we assume UDP transport is not possible. We reconnect using TCP
       * if we can. */
      g_object_set (G_OBJECT (stream->udpsrc[0]), "timeout", src->timeout,
          NULL);

      /* get output pad of the UDP source. */
      outpad = gst_element_get_pad (stream->udpsrc[0], "src");

      /* save it so we can unblock */
      stream->blockedpad = outpad;

      /* configure pad block on the pad. As soon as there is dataflow on the
       * UDP source, we know that UDP is not blocked by a firewall and we can
       * configure all the streams to let the application autoplug decoders. */
      gst_pad_set_blocked_async (outpad, TRUE,
          (GstPadBlockCallback) pad_blocked, src);

      if (stream->channelpad[0]) {
        GST_DEBUG_OBJECT (src, "connecting UDP source 0 to manager");
        /* configure for UDP delivery, we need to connect the UDP pads to
         * the session plugin. */
        gst_pad_link (outpad, stream->channelpad[0]);
        gst_object_unref (outpad);
        /* the real output pad is that of the session manager */
        outpad = gst_element_get_pad (stream->sess, "srcrtp");
      } else {
        GST_DEBUG_OBJECT (src, "using UDP src pad as output");
      }
    }

    if (stream->udpsrc[1]) {
      gst_bin_add (GST_BIN_CAST (src), stream->udpsrc[1]);

      if (stream->channelpad[1]) {
        GstPad *pad;

        GST_DEBUG_OBJECT (src, "connecting UDP source 1 to manager");

        pad = gst_element_get_pad (stream->udpsrc[1], "src");
        gst_pad_link (pad, stream->channelpad[1]);
        gst_object_unref (pad);
      }
    }
  }

  if (outpad) {
    GST_DEBUG_OBJECT (src, "creating ghostpad");

    gst_pad_use_fixed_caps (outpad);
    gst_pad_set_caps (outpad, stream->caps);

    /* create ghostpad, don't add just yet, this will be done when we activate
     * the stream. */
    name = g_strdup_printf ("stream%d", stream->id);
    template = gst_static_pad_template_get (&rtptemplate);
    stream->srcpad = gst_ghost_pad_new_from_template (name, outpad, template);
    gst_object_unref (template);
    g_free (name);

    gst_object_unref (outpad);
  }
  /* mark pad as ok */
  stream->last_ret = GST_FLOW_OK;

  return TRUE;

  /* ERRORS */
no_mime:
  {
    GST_DEBUG_OBJECT (src, "unknown transport");
    return FALSE;
  }
no_manager:
  {
    GST_DEBUG_OBJECT (src, "cannot get a session manager");
    return FALSE;
  }
no_element:
  {
    GST_DEBUG_OBJECT (src, "no rtpdec element found");
    return FALSE;
  }
start_session_failure:
  {
    GST_DEBUG_OBJECT (src, "could not start session");
    return FALSE;
  }
}

/* Adds the source pads of all configured streams to the element.
 * This code is performed when we detected dataflow.
 *
 * We detect dataflow from either the _loop function or with pad probes on the
 * udp sources.
 */
static gboolean
gst_rtspsrc_activate_streams (GstRTSPSrc * src)
{
  GList *walk;

  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;

    if (stream->srcpad) {
      gst_pad_set_active (stream->srcpad, TRUE);
      /* add the pad */
      if (!stream->added) {
        gst_element_add_pad (GST_ELEMENT_CAST (src), stream->srcpad);
        stream->added = TRUE;
      }
    }
  }

  /* if we got here all was configured. We have dynamic pads so we notify that
   * we are done */
  gst_element_no_more_pads (GST_ELEMENT_CAST (src));

  /* unblock all pads */
  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;

    if (stream->blockedpad) {
      gst_pad_set_blocked_async (stream->blockedpad, FALSE,
          (GstPadBlockCallback) pad_unblocked, src);
      stream->blockedpad = NULL;
    }
  }

  return TRUE;
}

static gint
find_stream_by_channel (GstRTSPStream * stream, gconstpointer a)
{
  gint channel = GPOINTER_TO_INT (a);

  if (stream->channel[0] == channel || stream->channel[1] == channel)
    return 0;

  return -1;
}

static GstFlowReturn
gst_rtspsrc_combine_flows (GstRTSPSrc * src, GstRTSPStream * stream,
    GstFlowReturn ret)
{
  GList *streams;

  /* store the value */
  stream->last_ret = ret;

  /* if it's success we can return the value right away */
  if (GST_FLOW_IS_SUCCESS (ret))
    goto done;

  /* any other error that is not-linked can be returned right
   * away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (streams = src->streams; streams; streams = g_list_next (streams)) {
    GstRTSPStream *ostream = (GstRTSPStream *) streams->data;

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
gst_rtspsrc_push_event (GstRTSPSrc * src, GstEvent * event)
{
  GList *streams;

  for (streams = src->streams; streams; streams = g_list_next (streams)) {
    GstRTSPStream *ostream = (GstRTSPStream *) streams->data;

    /* only pads that have a connection to the outside world */
    if (ostream->srcpad == NULL)
      continue;

    if (ostream->channelpad[0]) {
      gst_event_ref (event);
      if (GST_PAD_IS_SRC (ostream->channelpad[0]))
        gst_pad_push_event (ostream->channelpad[0], event);
      else
        gst_pad_send_event (ostream->channelpad[0], event);
    }

    if (ostream->channelpad[1]) {
      gst_event_ref (event);
      if (GST_PAD_IS_SRC (ostream->channelpad[1]))
        gst_pad_push_event (ostream->channelpad[1], event);
      else
        gst_pad_send_event (ostream->channelpad[1], event);
    }
  }
  gst_event_unref (event);
}

static void
gst_rtspsrc_loop_interleaved (GstRTSPSrc * src)
{
  RTSPMessage response = { 0 };
  RTSPResult res;
  gint channel;
  GList *lstream;
  GstRTSPStream *stream;
  GstPad *outpad = NULL;
  guint8 *data;
  guint size;
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps = NULL;
  GstBuffer *buf;

  do {
    GST_DEBUG_OBJECT (src, "doing receive");
    if ((res = rtsp_connection_receive (src->connection, &response)) < 0)
      goto receive_error;

    GST_DEBUG_OBJECT (src, "got packet type %d", response.type);
  }
  while (response.type != RTSP_MESSAGE_DATA);

  channel = response.type_data.data.channel;

  lstream = g_list_find_custom (src->streams, GINT_TO_POINTER (channel),
      (GCompareFunc) find_stream_by_channel);
  if (!lstream)
    goto unknown_stream;

  stream = (GstRTSPStream *) lstream->data;
  if (channel == stream->channel[0]) {
    outpad = stream->channelpad[0];
    caps = stream->caps;
  } else if (channel == stream->channel[1]) {
    outpad = stream->channelpad[1];
  }

  /* take a look at the body to figure out what we have */
  rtsp_message_get_body (&response, &data, &size);
  if (size < 2)
    goto invalid_length;

  /* channels are not correct on some servers, do extra check */
  if (data[1] >= 200 && data[1] <= 204) {
    /* hmm RTCP message switch to the RTCP pad of the same stream. */
    outpad = stream->channelpad[1];
  }

  /* we have no clue what this is, just ignore then. */
  if (outpad == NULL)
    goto unknown_stream;

  /* and chain buffer to internal element */
  rtsp_message_steal_body (&response, &data, &size);

  /* strip the trailing \0 */
  size -= 1;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = data;
  GST_BUFFER_MALLOCDATA (buf) = data;
  GST_BUFFER_SIZE (buf) = size;

  /* don't need message anymore */
  rtsp_message_unset (&response);

  if (caps)
    gst_buffer_set_caps (buf, caps);

  GST_DEBUG_OBJECT (src, "pushing data of size %d on channel %d", size,
      channel);

  if (src->need_activate) {
    gst_rtspsrc_activate_streams (src);
    src->need_activate = FALSE;
  }

  /* chain to the peer pad */
  if (GST_PAD_IS_SINK (outpad))
    ret = gst_pad_chain (outpad, buf);
  else
    ret = gst_pad_push (outpad, buf);

  /* combine all stream flows */
  ret = gst_rtspsrc_combine_flows (src, stream, ret);
  if (ret != GST_FLOW_OK)
    goto need_pause;

  return;

  /* ERRORS */
unknown_stream:
  {
    GST_DEBUG_OBJECT (src, "unknown stream on channel %d, ignored", channel);
    rtsp_message_unset (&response);
    return;
  }
receive_error:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not receive message. (%s)", str));
    g_free (str);

    if (src->debug)
      rtsp_message_dump (&response);

    rtsp_message_unset (&response);
    ret = GST_FLOW_UNEXPECTED;
    goto need_pause;
  }
invalid_length:
  {
    GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
        ("Short message received."));
    rtsp_message_unset (&response);
    return;
  }
need_pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (src, "pausing task, reason %s", reason);
    src->running = FALSE;
    gst_task_pause (src->task);
    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */
        if (src->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gst_element_post_message (GST_ELEMENT_CAST (src),
              gst_message_new_segment_done (GST_OBJECT_CAST (src),
                  src->segment.format, src->segment.last_stop));
        } else {
          gst_rtspsrc_push_event (src, gst_event_new_eos ());
        }
      } else {
        /* for fatal errors we post an error message, post the error
         * first so the app knows about the error first. */
        GST_ELEMENT_ERROR (src, STREAM, FAILED,
            ("Internal data flow error."),
            ("streaming task paused, reason %s (%d)", reason, ret));
        gst_rtspsrc_push_event (src, gst_event_new_eos ());
      }
    }
    return;
  }
}

static void
gst_rtspsrc_loop_udp (GstRTSPSrc * src)
{
  gboolean restart = FALSE;

  GST_OBJECT_LOCK (src);
  if (src->loop_cmd == CMD_STOP)
    goto stopping;

  while (src->loop_cmd == CMD_WAIT) {
    GST_DEBUG_OBJECT (src, "waiting");
    GST_RTSP_LOOP_WAIT (src);
    GST_DEBUG_OBJECT (src, "waiting done");
    if (src->loop_cmd == CMD_STOP)
      goto stopping;
  }
  if (src->loop_cmd == CMD_RECONNECT) {
    /* FIXME, when we get here we have to reconnect using tcp */
    src->loop_cmd = CMD_WAIT;

    /* only restart when the pads were not yet activated, else we were
     * streaming over UDP */
    restart = src->need_activate;
  }
  GST_OBJECT_UNLOCK (src);

  /* no need to restart, we're done */
  if (!restart)
    goto done;

  /* We post a warning message now to inform the user
   * that nothing happened. It's most likely a firewall thing. */
  GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
      ("Could not receive any UDP packets for %.4f seconds, maybe your "
          "firewall is blocking it. Retrying using a TCP connection.",
          gst_guint64_to_gdouble (src->timeout / 1000000)));
  /* we can try only TCP now */
  src->cur_protocols = RTSP_LOWER_TRANS_TCP;

  /* pause to prepare for a restart */
  gst_rtspsrc_pause (src);

  if (src->task) {
    /* stop task, we cannot join as this would deadlock */
    gst_task_stop (src->task);
    /* and free the task so that _close will not stop/join it again. */
    gst_object_unref (GST_OBJECT (src->task));
    src->task = NULL;
  }
  /* close and cleanup our state */
  gst_rtspsrc_close (src);

  /* see if we have TCP left to try */
  if (!(src->cur_protocols & RTSP_LOWER_TRANS_TCP))
    goto no_protocols;

  /* open new connection using tcp */
  if (!gst_rtspsrc_open (src))
    goto open_failed;

  /* start playback */
  if (!gst_rtspsrc_play (src))
    goto play_failed;

done:
  return;

  /* ERRORS */
stopping:
  {
    GST_OBJECT_UNLOCK (src);
    src->running = FALSE;
    gst_task_pause (src->task);
    return;
  }
no_protocols:
  {
    src->cur_protocols = 0;
    /* no transport possible, post an error and stop */
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not connect to server, no protocols left"));
    return;
  }
open_failed:
  {
    GST_DEBUG_OBJECT (src, "open failed");
    return;
  }
play_failed:
  {
    GST_DEBUG_OBJECT (src, "play failed");
    return;
  }
}

static void
gst_rtspsrc_loop_send_cmd (GstRTSPSrc * src, gint cmd)
{
  GST_OBJECT_LOCK (src);
  src->loop_cmd = cmd;
  GST_RTSP_LOOP_SIGNAL (src);
  GST_OBJECT_UNLOCK (src);
}

static void
gst_rtspsrc_loop (GstRTSPSrc * src)
{
  if (src->interleaved)
    gst_rtspsrc_loop_interleaved (src);
  else
    gst_rtspsrc_loop_udp (src);
}

static RTSPResult
gst_rtspsrc_handle_request (GstRTSPSrc * src, RTSPMessage * request)
{
  RTSPMessage response = { 0 };
  RTSPResult res;

  res = rtsp_message_init_response (&response, RTSP_STS_OK, "OK", request);
  if (res < 0)
    goto send_error;

  if (src->debug)
    rtsp_message_dump (&response);

  if ((res = rtsp_connection_send (src->connection, &response)) < 0)
    goto send_error;

  return RTSP_OK;

  /* ERRORS */
send_error:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message. (%s)", str));
    g_free (str);
    return res;
  }
}

/**
 * gst_rtspsrc_send:
 * @src: the rtsp source
 * @request: must point to a valid request
 * @response: must point to an empty #RTSPMessage
 *
 * send @request and retrieve the response in @response. optionally @code can be
 * non-NULL in which case it will contain the status code of the response.
 *
 * If This function returns TRUE, @response will contain a valid response
 * message that should be cleaned with rtsp_message_unset() after usage. 
 *
 * If @code is NULL, this function will return FALSE (with an invalid @response
 * message) if the response code was not 200 (OK).
 *
 * Returns: TRUE if the processing was successful.
 */
gboolean
gst_rtspsrc_send (GstRTSPSrc * src, RTSPMessage * request,
    RTSPMessage * response, RTSPStatusCode * code)
{
  RTSPResult res;
  RTSPStatusCode thecode;
  gchar *content_base = NULL;

  if (src->extension && src->extension->before_send)
    src->extension->before_send (src->extension, request);

  if (src->debug)
    rtsp_message_dump (request);

  if ((res = rtsp_connection_send (src->connection, request)) < 0)
    goto send_error;

next:
  if ((res = rtsp_connection_receive (src->connection, response)) < 0)
    goto receive_error;

  if (src->debug)
    rtsp_message_dump (response);

  switch (response->type) {
    case RTSP_MESSAGE_REQUEST:
      /* FIXME, handle server request, reply with OK, for now */
      if ((res = gst_rtspsrc_handle_request (src, response)) < 0)
        goto handle_request_failed;
      goto next;
    case RTSP_MESSAGE_RESPONSE:
      /* ok, a response is good */
      break;
    default:
    case RTSP_MESSAGE_DATA:
      /* get next response */
      goto next;
  }

  thecode = response->type_data.response.code;
  /* if the caller wanted the result code, we store it. Else we check if it's
   * OK. */
  if (code)
    *code = thecode;
  else if (thecode != RTSP_STS_OK)
    goto error_response;

  /* store new content base if any */
  rtsp_message_get_header (response, RTSP_HDR_CONTENT_BASE, &content_base);
  g_free (src->content_base);
  src->content_base = g_strdup (content_base);

  if (src->extension && src->extension->after_send)
    src->extension->after_send (src->extension, request, response);

  return TRUE;

  /* ERRORS */
send_error:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message. (%s)", str));
    g_free (str);
    return FALSE;
  }
receive_error:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not receive message. (%s)", str));
    g_free (str);
    return FALSE;
  }
handle_request_failed:
  {
    /* ERROR was posted */
    return FALSE;
  }
error_response:
  {
    switch (response->type_data.response.code) {
      case RTSP_STS_NOT_FOUND:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL), ("%s",
                response->type_data.response.reason));
        break;
      default:
        GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
            ("Got error response: %d (%s).", response->type_data.response.code,
                response->type_data.response.reason));
        break;
    }
    /* we return FALSE so we should unset the response ourselves */
    rtsp_message_unset (response);
    return FALSE;
  }
}

/* parse the response and collect all the supported methods. We need this
 * information so that we don't try to send an unsupported request to the
 * server.
 */
static gboolean
gst_rtspsrc_parse_methods (GstRTSPSrc * src, RTSPMessage * response)
{
  gchar *respoptions = NULL;
  gchar **options;
  gint i;

  /* clear supported methods */
  src->methods = 0;

  /* Try Allow Header first */
  rtsp_message_get_header (response, RTSP_HDR_ALLOW, &respoptions);
  if (!respoptions)
    /* Then maybe Public Header... */
    rtsp_message_get_header (response, RTSP_HDR_PUBLIC, &respoptions);
  if (!respoptions) {
    /* this field is not required, assume the server supports
     * DESCRIBE, SETUP and PLAY */
    GST_DEBUG_OBJECT (src, "could not get OPTIONS");
    src->methods = RTSP_DESCRIBE | RTSP_SETUP | RTSP_PLAY | RTSP_PAUSE;
    goto done;
  }

  /* If we get here, the server gave a list of supported methods, parse
   * them here. The string is like: 
   *
   * OPTIONS, DESCRIBE, ANNOUNCE, PLAY, SETUP, ...
   */
  options = g_strsplit (respoptions, ",", 0);

  for (i = 0; options[i]; i++) {
    gchar *stripped;
    gint method;

    stripped = g_strstrip (options[i]);
    method = rtsp_find_method (stripped);

    /* keep bitfield of supported methods */
    if (method != RTSP_INVALID)
      src->methods |= method;
  }
  g_strfreev (options);

  /* we need describe and setup */
  if (!(src->methods & RTSP_DESCRIBE))
    goto no_describe;
  if (!(src->methods & RTSP_SETUP))
    goto no_setup;

done:
  return TRUE;

  /* ERRORS */
no_describe:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Server does not support DESCRIBE."));
    return FALSE;
  }
no_setup:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("Server does not support SETUP."));
    return FALSE;
  }
}

static RTSPResult
gst_rtspsrc_create_transports_string (GstRTSPSrc * src,
    RTSPLowerTrans protocols, gchar ** transports)
{
  gchar *result;
  RTSPResult res;

  *transports = NULL;
  if (src->extension && src->extension->get_transports)
    if ((res =
            src->extension->get_transports (src->extension, protocols,
                transports)) < 0)
      goto failed;

  /* extension listed transports, use those */
  if (*transports != NULL)
    return RTSP_OK;

  /* the default RTSP transports */
  result = g_strdup ("");
  if (protocols & RTSP_LOWER_TRANS_UDP) {
    gchar *new;

    GST_DEBUG_OBJECT (src, "adding UDP unicast");

    new =
        g_strconcat (result, "RTP/AVP/UDP;unicast;client_port=%%u1-%%u2", NULL);
    g_free (result);
    result = new;
  }
  if (protocols & RTSP_LOWER_TRANS_UDP_MCAST) {
    gchar *new;

    GST_DEBUG_OBJECT (src, "adding UDP multicast");

    /* we don't have to allocate any UDP ports yet, if the selected transport
     * turns out to be multicast we can create them and join the multicast
     * group indicated in the transport reply */
    new = g_strconcat (result, result[0] ? "," : "",
        "RTP/AVP/UDP;multicast", NULL);
    g_free (result);
    result = new;
  }
  if (protocols & RTSP_LOWER_TRANS_TCP) {
    gchar *new;

    GST_DEBUG_OBJECT (src, "adding TCP");

    new = g_strconcat (result, result[0] ? "," : "",
        "RTP/AVP/TCP;unicast;interleaved=%%i1-%%i2", NULL);
    g_free (result);
    result = new;
  }
  *transports = result;

  return RTSP_OK;

  /* ERRORS */
failed:
  {
    return res;
  }
}

static RTSPResult
gst_rtspsrc_prepare_transports (GstRTSPStream * stream, gchar ** transports)
{
  GstRTSPSrc *src;
  gint nr_udp, nr_int;
  gchar *next, *p;
  gint rtpport = 0, rtcpport = 0;
  GString *str;

  src = stream->parent;

  /* find number of placeholders first */
  if (strstr (*transports, "%%i2"))
    nr_int = 2;
  else if (strstr (*transports, "%%i1"))
    nr_int = 1;
  else
    nr_int = 0;

  if (strstr (*transports, "%%u2"))
    nr_udp = 2;
  else if (strstr (*transports, "%%u1"))
    nr_udp = 1;
  else
    nr_udp = 0;

  if (nr_udp == 0 && nr_int == 0)
    goto done;

  if (nr_udp > 0) {
    if (!gst_rtspsrc_alloc_udp_ports (stream, &rtpport, &rtcpport))
      goto failed;
  }

  str = g_string_new ("");
  p = *transports;
  while ((next = strstr (p, "%%"))) {
    g_string_append_len (str, p, next - p);
    if (next[2] == 'u') {
      if (next[3] == '1')
        g_string_append_printf (str, "%d", rtpport);
      else if (next[3] == '2')
        g_string_append_printf (str, "%d", rtcpport);
    }
    if (next[2] == 'i') {
      if (next[3] == '1')
        g_string_append_printf (str, "%d", src->free_channel);
      else if (next[3] == '2')
        g_string_append_printf (str, "%d", src->free_channel + 1);
    }

    p = next + 4;
  }

  g_free (*transports);
  *transports = g_string_free (str, FALSE);

done:
  return RTSP_OK;

  /* ERRORS */
failed:
  {
    return RTSP_ERROR;
  }
}

static gboolean
gst_rtspsrc_setup_streams (GstRTSPSrc * src)
{
  GList *walk;
  RTSPResult res;
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  GstRTSPStream *stream = NULL;
  RTSPLowerTrans protocols;

  /* we initially allow all configured lower transports. based on the URL
   * transports and the replies from the server we narrow them down. */
  protocols = src->url->transports & src->cur_protocols;

  /* reset some state */
  src->free_channel = 0;
  src->interleaved = FALSE;

  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    gchar *transports;

    stream = (GstRTSPStream *) walk->data;

    /* see if we need to configure this stream */
    if (src->extension && src->extension->configure_stream) {
      if (!src->extension->configure_stream (src->extension, stream)) {
        GST_DEBUG_OBJECT (src, "skipping stream %p, disabled by extension",
            stream);
        continue;
      }
    }

    /* merge/overwrite global caps */
    if (stream->caps) {
      guint j, num;
      GstStructure *s;

      s = gst_caps_get_structure (stream->caps, 0);

      num = gst_structure_n_fields (src->props);
      for (j = 0; j < num; j++) {
        const gchar *name;
        const GValue *val;

        name = gst_structure_nth_field_name (src->props, j);
        val = gst_structure_get_value (src->props, name);
        gst_structure_set_value (s, name, val);

        GST_DEBUG_OBJECT (src, "copied %s", name);
      }
    }

    /* skip setup if we have no URL for it */
    if (stream->setup_url == NULL) {
      GST_DEBUG_OBJECT (src, "skipping stream %p, no setup", stream);
      continue;
    }

    GST_DEBUG_OBJECT (src, "doing setup of stream %p with %s", stream,
        stream->setup_url);

    /* create a string with all the transports */
    res = gst_rtspsrc_create_transports_string (src, protocols, &transports);
    if (res < 0)
      goto setup_transport_failed;

    /* replace placeholders with real values, this function will optionally
     * allocate UDP ports and other info needed to execute the setup request */
    res = gst_rtspsrc_prepare_transports (stream, &transports);
    if (res < 0)
      goto setup_transport_failed;

    /* create SETUP request */
    res = rtsp_message_init_request (&request, RTSP_SETUP, stream->setup_url);
    if (res < 0)
      goto create_request_failed;

    /* select transport, copy is made when adding to header so we can free it. */
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
      if (rtsp_transport_parse (resptrans, &transport) != RTSP_OK)
        continue;

      /* update allowed transports for other streams. once the transport of
       * one stream has been determined, we make sure that all other streams
       * are configured in the same way */
      switch (transport.lower_transport) {
        case RTSP_LOWER_TRANS_TCP:
          GST_DEBUG_OBJECT (src, "stream %p as TCP interleaved", stream);
          protocols = RTSP_LOWER_TRANS_TCP;
          src->interleaved = TRUE;
          /* update free channels */
          src->free_channel =
              MAX (transport.interleaved.min, src->free_channel);
          src->free_channel =
              MAX (transport.interleaved.max, src->free_channel);
          src->free_channel++;
          break;
        case RTSP_LOWER_TRANS_UDP_MCAST:
          /* only allow multicast for other streams */
          GST_DEBUG_OBJECT (src, "stream %p as UDP multicast", stream);
          protocols = RTSP_LOWER_TRANS_UDP_MCAST;
          break;
        case RTSP_LOWER_TRANS_UDP:
          /* only allow unicast for other streams */
          GST_DEBUG_OBJECT (src, "stream %p as UDP unicast", stream);
          protocols = RTSP_LOWER_TRANS_UDP;
          break;
        default:
          GST_DEBUG_OBJECT (src, "stream %p unknown transport %d", stream,
              transport.lower_transport);
          break;
      }

      if (!stream->container || !src->interleaved) {
        /* now configure the stream with the selected transport */
        if (!gst_rtspsrc_stream_configure_transport (stream, &transport)) {
          GST_DEBUG_OBJECT (src,
              "could not configure stream %p transport, skipping stream",
              stream);
        }
      }
      /* clean up our transport struct */
      rtsp_transport_init (&transport);
    }
  }
  if (src->extension && src->extension->stream_select)
    src->extension->stream_select (src->extension);

  /* we need to activate the streams when we detect activity */
  src->need_activate = TRUE;

  return TRUE;

  /* ERRORS */
create_request_failed:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
setup_transport_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Could not setup transport."));
    goto cleanup_error;
  }
send_error:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
no_transport:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Server did not select transport."));
    goto cleanup_error;
  }
cleanup_error:
  {
    rtsp_message_unset (&request);
    rtsp_message_unset (&response);
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_open (GstRTSPSrc * src)
{
  RTSPResult res;
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  guint8 *data;
  guint size;
  gint i, n_streams;
  SDPMessage sdp = { 0 };
  GstRTSPStream *stream = NULL;
  gchar *respcont = NULL;

  /* reset our state */
  gst_segment_init (&src->segment, GST_FORMAT_TIME);

  /* can't continue without a valid url */
  if (G_UNLIKELY (src->url == NULL))
    goto no_url;

  /* create connection */
  GST_DEBUG_OBJECT (src, "creating connection (%s)...", src->location);
  if ((res = rtsp_connection_create (src->url, &src->connection)) < 0)
    goto could_not_create;

  /* connect */
  GST_DEBUG_OBJECT (src, "connecting (%s)...", src->location);
  if ((res = rtsp_connection_connect (src->connection)) < 0)
    goto could_not_connect;

  /* create OPTIONS */
  GST_DEBUG_OBJECT (src, "create options...");
  res = rtsp_message_init_request (&request, RTSP_OPTIONS, src->location);
  if (res < 0)
    goto create_request_failed;

  /* send OPTIONS */
  GST_DEBUG_OBJECT (src, "send options...");
  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  /* parse OPTIONS */
  if (!gst_rtspsrc_parse_methods (src, &response))
    goto methods_error;

  /* create DESCRIBE */
  GST_DEBUG_OBJECT (src, "create describe...");
  res = rtsp_message_init_request (&request, RTSP_DESCRIBE, src->location);
  if (res < 0)
    goto create_request_failed;

  /* we only accept SDP for now */
  rtsp_message_add_header (&request, RTSP_HDR_ACCEPT, "application/sdp");

  /* prepare global stream caps properties */
  if (src->props)
    gst_structure_remove_all_fields (src->props);
  else
    src->props = gst_structure_empty_new ("RTSP Properties");

  /* send DESCRIBE */
  GST_DEBUG_OBJECT (src, "send describe...");
  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  /* check if reply is SDP */
  rtsp_message_get_header (&response, RTSP_HDR_CONTENT_TYPE, &respcont);
  /* could not be set but since the request returned OK, we assume it
   * was SDP, else check it. */
  if (respcont) {
    if (!g_ascii_strcasecmp (respcont, "application/sdp") == 0)
      goto wrong_content_type;
  }

  /* get message body and parse as SDP */
  rtsp_message_get_body (&response, &data, &size);

  GST_DEBUG_OBJECT (src, "parse SDP...");
  sdp_message_init (&sdp);
  sdp_message_parse_buffer (data, size, &sdp);

  if (src->debug)
    sdp_message_dump (&sdp);

  if (src->extension && src->extension->parse_sdp)
    src->extension->parse_sdp (src->extension, &sdp);

  /* create streams */
  n_streams = sdp_message_medias_len (&sdp);
  for (i = 0; i < n_streams; i++) {
    stream = gst_rtspsrc_create_stream (src, &sdp, i);
  }

  /* setup streams */
  gst_rtspsrc_setup_streams (src);

  /* clean up any messages */
  rtsp_message_unset (&request);
  rtsp_message_unset (&response);

  return TRUE;

  /* ERRORS */
no_url:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
        ("No valid RTSP URL was provided"));
    goto cleanup_error;
  }
could_not_create:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Could not create connection. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
could_not_connect:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Could not connect to server. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
create_request_failed:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
send_error:
  {
    gchar *str = rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
methods_error:
  {
    /* error was posted */
    goto cleanup_error;
  }
wrong_content_type:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Server does not support SDP, got %s.", respcont));
    goto cleanup_error;
  }
cleanup_error:
  {
    rtsp_message_unset (&request);
    rtsp_message_unset (&response);
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_close (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  GST_DEBUG_OBJECT (src, "TEARDOWN...");

  gst_rtspsrc_loop_send_cmd (src, CMD_STOP);

  /* stop task if any */
  if (src->task) {
    gst_task_stop (src->task);

    /* make sure it is not running */
    g_static_rec_mutex_lock (src->stream_rec_lock);
    g_static_rec_mutex_unlock (src->stream_rec_lock);

    /* no wait for the task to finish */
    gst_task_join (src->task);

    /* and free the task */
    gst_object_unref (GST_OBJECT (src->task));
    src->task = NULL;
  }

  if (src->methods & RTSP_PLAY) {
    /* do TEARDOWN */
    res = rtsp_message_init_request (&request, RTSP_TEARDOWN, src->location);
    if (res < 0)
      goto create_request_failed;

    if (!gst_rtspsrc_send (src, &request, &response, NULL))
      goto send_error;

    /* FIXME, parse result? */
    rtsp_message_unset (&request);
    rtsp_message_unset (&response);
  }

  /* close connection */
  GST_DEBUG_OBJECT (src, "closing connection...");
  if ((res = rtsp_connection_close (src->connection)) < 0)
    goto close_failed;

  /* free connection */
  rtsp_connection_free (src->connection);
  src->connection = NULL;

  /* cleanup */
  gst_rtspsrc_cleanup (src);

  return TRUE;

  /* ERRORS */
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request."));
    return FALSE;
  }
send_error:
  {
    rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message."));
    return FALSE;
  }
close_failed:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, CLOSE, (NULL), ("Close failed."));
    return FALSE;
  }
}

/* RTP-Info is of the format:
 *
 * url=<URL>;[seq=<seqbase>;rtptime=<timebase>] [, url=...]
 */
static gboolean
gst_rtspsrc_parse_rtpinfo (GstRTSPSrc * src, gchar * rtpinfo)
{
  gchar **infos;
  gint i;

  infos = g_strsplit (rtpinfo, ",", 0);
  for (i = 0; infos[i]; i++) {
    /* FIXME, do something here:
     * parse url, find stream for url.
     * parse seq and rtptime. The seq number should be configured in the rtp
     * depayloader or session manager to detect gaps. Same for the rtptime, it
     * should be used to create an initial time newsegment.
     */
  }
  g_strfreev (infos);

  return TRUE;
}

static gboolean
gst_rtspsrc_play (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;
  gchar *rtpinfo;

  if (!(src->methods & RTSP_PLAY))
    return TRUE;

  GST_DEBUG_OBJECT (src, "PLAY...");

  /* do play */
  res = rtsp_message_init_request (&request, RTSP_PLAY, src->location);
  if (res < 0)
    goto create_request_failed;

  rtsp_message_add_header (&request, RTSP_HDR_RANGE, "npt=0-");

  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  rtsp_message_unset (&request);

  /* parse the RTP-Info header field (if ANY) to get the base seqnum and timestamp
   * for the RTP packets. If this is not present, we assume all starts from 0... 
   * FIXME, this is info for the RTP session manager ideally. */
  rtsp_message_get_header (&response, RTSP_HDR_RTP_INFO, &rtpinfo);
  if (rtpinfo)
    gst_rtspsrc_parse_rtpinfo (src, rtpinfo);

  rtsp_message_unset (&response);

  /* for interleaved transport, we receive the data on the RTSP connection
   * instead of UDP. We start a task to select and read from that connection.
   * For UDP we start the task as well to look for server info and UDP timeouts. */
  if (src->task == NULL) {
    src->task = gst_task_create ((GstTaskFunction) gst_rtspsrc_loop, src);
    gst_task_set_lock (src->task, src->stream_rec_lock);
  }
  src->running = TRUE;
  gst_rtspsrc_loop_send_cmd (src, CMD_WAIT);
  gst_task_start (src->task);

  return TRUE;

  /* ERRORS */
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request."));
    return FALSE;
  }
send_error:
  {
    rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message."));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_pause (GstRTSPSrc * src)
{
  RTSPMessage request = { 0 };
  RTSPMessage response = { 0 };
  RTSPResult res;

  if (!(src->methods & RTSP_PAUSE))
    return TRUE;

  GST_DEBUG_OBJECT (src, "PAUSE...");
  /* do pause */
  res = rtsp_message_init_request (&request, RTSP_PAUSE, src->location);
  if (res < 0)
    goto create_request_failed;

  if (!gst_rtspsrc_send (src, &request, &response, NULL))
    goto send_error;

  rtsp_message_unset (&request);
  rtsp_message_unset (&response);

  return TRUE;

  /* ERRORS */
create_request_failed:
  {
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request."));
    return FALSE;
  }
send_error:
  {
    rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message."));
    return FALSE;
  }
}

static void
gst_rtspsrc_handle_message (GstBin * bin, GstMessage * message)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT:
    {
      GstRTSPSrc *rtspsrc;
      const GstStructure *s = gst_message_get_structure (message);

      rtspsrc = GST_RTSPSRC (bin);

      if (gst_structure_has_name (s, "GstUDPSrcTimeout")) {
        GST_DEBUG_OBJECT (bin, "timeout on UDP port");
        gst_rtspsrc_loop_send_cmd (rtspsrc, CMD_RECONNECT);
        return;
      }
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
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
      rtspsrc->cur_protocols = rtspsrc->protocols;
      if (!gst_rtspsrc_open (rtspsrc))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      rtsp_connection_flush (rtspsrc->connection, FALSE);
      gst_rtspsrc_play (rtspsrc);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      rtsp_connection_flush (rtspsrc->connection, TRUE);
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

static GstURIType
gst_rtspsrc_uri_get_type (void)
{
  return GST_URI_SRC;
}
static gchar **
gst_rtspsrc_uri_get_protocols (void)
{
  static gchar *protocols[] = { "rtsp", "rtspu", "rtspt", NULL };

  return protocols;
}

static const gchar *
gst_rtspsrc_uri_get_uri (GstURIHandler * handler)
{
  GstRTSPSrc *src = GST_RTSPSRC (handler);

  /* should not dup */
  return src->location;
}

static gboolean
gst_rtspsrc_uri_set_uri (GstURIHandler * handler, const gchar * uri)
{
  GstRTSPSrc *src;
  RTSPResult res;
  RTSPUrl *newurl;

  src = GST_RTSPSRC (handler);

  /* same URI, we're fine */
  if (src->location && uri && !strcmp (uri, src->location))
    goto was_ok;

  /* try to parse */
  if ((res = rtsp_url_parse (uri, &newurl)) < 0)
    goto parse_error;

  /* if worked, free previous and store new url object along with the original
   * location. */
  rtsp_url_free (src->url);
  src->url = newurl;
  g_free (src->location);
  src->location = g_strdup (uri);
  if (!g_str_has_prefix (src->location, "rtsp://"))
    memmove (src->location + 4, src->location + 5, strlen (src->location) - 4);

  GST_DEBUG_OBJECT (src, "set uri: %s", GST_STR_NULL (uri));

  return TRUE;

  /* Special cases */
was_ok:
  {
    GST_DEBUG_OBJECT (src, "URI was ok: '%s'", GST_STR_NULL (uri));
    return TRUE;
  }
parse_error:
  {
    GST_ERROR_OBJECT (src, "Not a valid RTSP url '%s' (%d)",
        GST_STR_NULL (uri), res);
    return FALSE;
  }
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
