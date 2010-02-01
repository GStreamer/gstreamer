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
 * Makes a connection to an RTSP server and read the data.
 * rtspsrc strictly follows RFC 2326 and therefore does not (yet) support
 * RealMedia/Quicktime/Microsoft extensions.
 *
 * RTSP supports transport over TCP or UDP in unicast or multicast mode. By
 * default rtspsrc will negotiate a connection in the following order:
 * UDP unicast/UDP multicast/TCP. The order cannot be changed but the allowed
 * protocols can be controlled with the #GstRTSPSrc:protocols property.
 *
 * rtspsrc currently understands SDP as the format of the session description.
 * For each stream listed in the SDP a new rtp_stream%d pad will be created
 * with caps derived from the SDP media description. This is a caps of mime type
 * "application/x-rtp" that can be connected to any available RTP depayloader
 * element. 
 *
 * rtspsrc will internally instantiate an RTP session manager element
 * that will handle the RTCP messages to and from the server, jitter removal,
 * packet reordering along with providing a clock for the pipeline. 
 * This feature is currently fully implemented with the gstrtpbin in the
 * gst-plugins-bad module.
 *
 * rtspsrc acts like a live source and will therefore only generate data in the 
 * PLAYING state.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch rtspsrc location=rtsp://some.server/url ! fakesink
 * ]| Establish a connection to an RTSP server and send the raw RTP packets to a
 * fakesink.
 * </refsect2>
 *
 * Last reviewed on 2006-08-18 (0.10.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <stdio.h>
#include <stdarg.h>

#include <gst/sdp/gstsdpmessage.h>
#include <gst/rtp/gstrtppayloads.h>

#include "gst/gst-i18n-plugin.h"

#include "gstrtspsrc.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
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

/* templates used internally */
static GstStaticPadTemplate anysrctemplate =
GST_STATIC_PAD_TEMPLATE ("internalsrc%d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate anysinktemplate =
GST_STATIC_PAD_TEMPLATE ("internalsink%d",
    GST_PAD_SINK,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_LOCATION         NULL
#define DEFAULT_PROTOCOLS        GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | GST_RTSP_LOWER_TRANS_TCP
#define DEFAULT_DEBUG            FALSE
#define DEFAULT_RETRY            20
#define DEFAULT_TIMEOUT          5000000
#define DEFAULT_TCP_TIMEOUT      20000000
#define DEFAULT_LATENCY_MS       3000
#define DEFAULT_CONNECTION_SPEED 0
#define DEFAULT_NAT_METHOD       GST_RTSP_NAT_DUMMY
#define DEFAULT_DO_RTCP          TRUE
#define DEFAULT_PROXY            NULL
#define DEFAULT_RTP_BLOCKSIZE    0

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PROTOCOLS,
  PROP_DEBUG,
  PROP_RETRY,
  PROP_TIMEOUT,
  PROP_TCP_TIMEOUT,
  PROP_LATENCY,
  PROP_CONNECTION_SPEED,
  PROP_NAT_METHOD,
  PROP_DO_RTCP,
  PROP_PROXY,
  PROP_RTP_BLOCKSIZE,
  PROP_USER_ID,
  PROP_USER_PW,
  PROP_LAST
};

#define GST_TYPE_RTSP_LOWER_TRANS (gst_rtsp_lower_trans_get_type())
static GType
gst_rtsp_lower_trans_get_type (void)
{
  static GType rtsp_lower_trans_type = 0;
  static const GFlagsValue rtsp_lower_trans[] = {
    {GST_RTSP_LOWER_TRANS_UDP, "UDP Unicast Mode", "udp-unicast"},
    {GST_RTSP_LOWER_TRANS_UDP_MCAST, "UDP Multicast Mode", "udp-multicast"},
    {GST_RTSP_LOWER_TRANS_TCP, "TCP interleaved mode", "tcp"},
    {GST_RTSP_LOWER_TRANS_HTTP, "HTTP tunneled mode", "http"},
    {0, NULL, NULL},
  };

  if (!rtsp_lower_trans_type) {
    rtsp_lower_trans_type =
        g_flags_register_static ("GstRTSPLowerTrans", rtsp_lower_trans);
  }
  return rtsp_lower_trans_type;
}

#define GST_TYPE_RTSP_NAT_METHOD (gst_rtsp_nat_method_get_type())
static GType
gst_rtsp_nat_method_get_type (void)
{
  static GType rtsp_nat_method_type = 0;
  static const GEnumValue rtsp_nat_method[] = {
    {GST_RTSP_NAT_NONE, "None", "none"},
    {GST_RTSP_NAT_DUMMY, "Send Dummy packets", "dummy"},
    {0, NULL, NULL},
  };

  if (!rtsp_nat_method_type) {
    rtsp_nat_method_type =
        g_enum_register_static ("GstRTSPNatMethod", rtsp_nat_method);
  }
  return rtsp_nat_method_type;
}

static void gst_rtspsrc_base_init (gpointer g_class);
static void gst_rtspsrc_finalize (GObject * object);

static void gst_rtspsrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtspsrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_rtspsrc_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static void gst_rtspsrc_sdp_attributes_to_caps (GArray * attributes,
    GstCaps * caps);

static GstCaps *gst_rtspsrc_media_to_caps (gint pt, const GstSDPMedia * media);

static GstStateChangeReturn gst_rtspsrc_change_state (GstElement * element,
    GstStateChange transition);
static void gst_rtspsrc_handle_message (GstBin * bin, GstMessage * message);

static void gst_rtspsrc_loop_send_cmd (GstRTSPSrc * src, gint cmd,
    gboolean flush);
static GstRTSPResult gst_rtspsrc_send_cb (GstRTSPExtension * ext,
    GstRTSPMessage * request, GstRTSPMessage * response, GstRTSPSrc * src);

static gboolean gst_rtspsrc_open (GstRTSPSrc * src);
static gboolean gst_rtspsrc_play (GstRTSPSrc * src, GstSegment * segment);
static gboolean gst_rtspsrc_pause (GstRTSPSrc * src);
static gboolean gst_rtspsrc_close (GstRTSPSrc * src);

static gboolean gst_rtspsrc_uri_set_uri (GstURIHandler * handler,
    const gchar * uri);

static gboolean gst_rtspsrc_activate_streams (GstRTSPSrc * src);
static void gst_rtspsrc_loop (GstRTSPSrc * src);
static void gst_rtspsrc_stream_push_event (GstRTSPSrc * src,
    GstRTSPStream * stream, GstEvent * event);
static void gst_rtspsrc_push_event (GstRTSPSrc * src, GstEvent * event);
static gchar *gst_rtspsrc_dup_printf (const gchar * format, ...);

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
          "Retry TCP transport after UDP timeout microseconds (0 = disabled)",
          0, G_MAXUINT64, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_TCP_TIMEOUT,
      g_param_spec_uint64 ("tcp-timeout", "TCP Timeout",
          "Fail after timeout microseconds on TCP connections (0 = disabled)",
          0, G_MAXUINT64, DEFAULT_TCP_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_CONNECTION_SPEED,
      g_param_spec_uint ("connection-speed", "Connection Speed",
          "Network connection speed in kbps (0 = unknown)",
          0, G_MAXINT / 1000, DEFAULT_CONNECTION_SPEED,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_NAT_METHOD,
      g_param_spec_enum ("nat-method", "NAT Method",
          "Method to use for traversing firewalls and NAT",
          GST_TYPE_RTSP_NAT_METHOD, DEFAULT_NAT_METHOD,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GstRTSPSrc::do-rtcp
   *
   * Enable RTCP support. Some old server don't like RTCP and then this property
   * needs to be set to FALSE.
   *
   * Since: 0.10.15
   */
  g_object_class_install_property (gobject_class, PROP_DO_RTCP,
      g_param_spec_boolean ("do-rtcp", "Do RTCP",
          "Send RTCP packets, disable for old incompatible server.",
          DEFAULT_DO_RTCP, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GstRTSPSrc::proxy
   *
   * Set the proxy parameters. This has to be a string of the format
   * [http://][user:passwd@]host[:port].
   *
   * Since: 0.10.15
   */
  g_object_class_install_property (gobject_class, PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy",
          "Proxy settings for HTTP tunneling. Format: [http://][user:passwd@]host[:port]",
          DEFAULT_PROXY, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GstRTSPSrc::rtp_blocksize
   *
   * RTP package size to suggest to server.
   *
   * Since: 0.10.16
   */
  g_object_class_install_property (gobject_class, PROP_RTP_BLOCKSIZE,
      g_param_spec_uint ("rtp-blocksize", "RTP Blocksize",
          "RTP package size to suggest to server (0 = disabled)",
          0, 65536, DEFAULT_RTP_BLOCKSIZE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class,
      PROP_USER_ID,
      g_param_spec_string ("user-id", "user-id",
          "RTSP location URI user id for authentication", NULL,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_USER_PW,
      g_param_spec_string ("user-pw", "user-pw",
          "RTSP location URI user password for authentication", NULL,
          G_PARAM_READWRITE));

  gstelement_class->change_state = gst_rtspsrc_change_state;

  gstbin_class->handle_message = gst_rtspsrc_handle_message;

  gst_rtsp_ext_list_init ();
}

static void
gst_rtspsrc_init (GstRTSPSrc * src, GstRTSPSrcClass * g_class)
{
#ifdef G_OS_WIN32
  WSADATA wsa_data;

  if (WSAStartup (MAKEWORD (2, 2), &wsa_data) != 0) {
    GST_ERROR_OBJECT (src, "WSAStartup failed: 0x%08x", WSAGetLastError ());
  }
#endif

  src->location = g_strdup (DEFAULT_LOCATION);
  src->url = NULL;

  /* get a list of all extensions */
  src->extensions = gst_rtsp_ext_list_get ();

  /* connect to send signal */
  gst_rtsp_ext_list_connect (src->extensions, "send",
      (GCallback) gst_rtspsrc_send_cb, src);

  /* protects the streaming thread in interleaved mode or the polling
   * thread in UDP mode. */
  src->stream_rec_lock = g_new (GStaticRecMutex, 1);
  g_static_rec_mutex_init (src->stream_rec_lock);

  /* protects our state changes from multiple invocations */
  src->state_rec_lock = g_new (GStaticRecMutex, 1);
  g_static_rec_mutex_init (src->state_rec_lock);

  /* protects access to the server connection */
  src->conn_rec_lock = g_new (GStaticRecMutex, 1);
  g_static_rec_mutex_init (src->conn_rec_lock);

  src->state = GST_RTSP_STATE_INVALID;
}

static void
gst_rtspsrc_finalize (GObject * object)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (object);

  gst_rtsp_ext_list_free (rtspsrc->extensions);
  g_free (rtspsrc->location);
  g_free (rtspsrc->req_location);
  gst_rtsp_url_free (rtspsrc->url);
  g_free (rtspsrc->user_id);
  g_free (rtspsrc->user_pw);

  /* free locks */
  g_static_rec_mutex_free (rtspsrc->stream_rec_lock);
  g_free (rtspsrc->stream_rec_lock);
  g_static_rec_mutex_free (rtspsrc->state_rec_lock);
  g_free (rtspsrc->state_rec_lock);
  g_static_rec_mutex_free (rtspsrc->conn_rec_lock);
  g_free (rtspsrc->conn_rec_lock);

#ifdef G_OS_WIN32
  WSACleanup ();
#endif

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* a proxy string of the format [user:passwd@]host[:port] */
static gboolean
gst_rtspsrc_set_proxy (GstRTSPSrc * rtsp, const gchar * proxy)
{
  gchar *p, *at, *col;

  g_free (rtsp->proxy_user);
  rtsp->proxy_user = NULL;
  g_free (rtsp->proxy_passwd);
  rtsp->proxy_passwd = NULL;
  g_free (rtsp->proxy_host);
  rtsp->proxy_host = NULL;
  rtsp->proxy_port = 0;

  p = (gchar *) proxy;

  if (p == NULL)
    return TRUE;

  /* we allow http:// in front but ignore it */
  if (g_str_has_prefix (p, "http://"))
    p += 7;

  at = strchr (p, '@');
  if (at) {
    /* look for user:passwd */
    col = strchr (proxy, ':');
    if (col == NULL || col > at)
      return FALSE;

    rtsp->proxy_user = g_strndup (p, col - p);
    col++;
    rtsp->proxy_passwd = g_strndup (col, at - col);

    /* move to host */
    p = at + 1;
  }
  col = strchr (p, ':');

  if (col) {
    /* everything before the colon is the hostname */
    rtsp->proxy_host = g_strndup (p, col - p);
    p = col + 1;
    rtsp->proxy_port = strtoul (p, (char **) &p, 10);
  } else {
    rtsp->proxy_host = g_strdup (p);
    rtsp->proxy_port = 8080;
  }
  return TRUE;
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
      rtspsrc->udp_timeout = g_value_get_uint64 (value);
      break;
    case PROP_TCP_TIMEOUT:
    {
      guint64 timeout = g_value_get_uint64 (value);

      rtspsrc->tcp_timeout.tv_sec = timeout / G_USEC_PER_SEC;
      rtspsrc->tcp_timeout.tv_usec = timeout % G_USEC_PER_SEC;

      if (timeout != 0)
        rtspsrc->ptcp_timeout = &rtspsrc->tcp_timeout;
      else
        rtspsrc->ptcp_timeout = NULL;
      break;
    }
    case PROP_LATENCY:
      rtspsrc->latency = g_value_get_uint (value);
      break;
    case PROP_CONNECTION_SPEED:
      rtspsrc->connection_speed = g_value_get_uint (value);
      break;
    case PROP_NAT_METHOD:
      rtspsrc->nat_method = g_value_get_enum (value);
      break;
    case PROP_DO_RTCP:
      rtspsrc->do_rtcp = g_value_get_boolean (value);
      break;
    case PROP_PROXY:
      gst_rtspsrc_set_proxy (rtspsrc, g_value_get_string (value));
      break;
    case PROP_RTP_BLOCKSIZE:
      rtspsrc->rtp_blocksize = g_value_get_uint (value);
      break;
    case PROP_USER_ID:
      if (rtspsrc->user_id)
        g_free (rtspsrc->user_id);
      rtspsrc->user_id = g_value_dup_string (value);
      break;
    case PROP_USER_PW:
      if (rtspsrc->user_pw)
        g_free (rtspsrc->user_pw);
      rtspsrc->user_pw = g_value_dup_string (value);
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
      g_value_set_uint64 (value, rtspsrc->udp_timeout);
      break;
    case PROP_TCP_TIMEOUT:
    {
      guint64 timeout;

      timeout = rtspsrc->tcp_timeout.tv_sec * G_USEC_PER_SEC +
          rtspsrc->tcp_timeout.tv_usec;
      g_value_set_uint64 (value, timeout);
      break;
    }
    case PROP_LATENCY:
      g_value_set_uint (value, rtspsrc->latency);
      break;
    case PROP_CONNECTION_SPEED:
      g_value_set_uint (value, rtspsrc->connection_speed);
      break;
    case PROP_NAT_METHOD:
      g_value_set_enum (value, rtspsrc->nat_method);
      break;
    case PROP_DO_RTCP:
      g_value_set_boolean (value, rtspsrc->do_rtcp);
      break;
    case PROP_PROXY:
    {
      gchar *str;

      if (rtspsrc->proxy_host) {
        str =
            g_strdup_printf ("%s:%d", rtspsrc->proxy_host, rtspsrc->proxy_port);
      } else {
        str = NULL;
      }
      g_value_take_string (value, str);
      break;
    }
    case PROP_RTP_BLOCKSIZE:
      g_value_set_uint (value, rtspsrc->rtp_blocksize);
      break;
    case PROP_USER_ID:
      g_value_set_string (value, rtspsrc->user_id);
      break;
    case PROP_USER_PW:
      g_value_set_string (value, rtspsrc->user_pw);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
find_stream_by_id (GstRTSPStream * stream, gint * id)
{
  if (stream->id == *id)
    return 0;

  return -1;
}

static gint
find_stream_by_channel (GstRTSPStream * stream, gint * channel)
{
  if (stream->channel[0] == *channel || stream->channel[1] == *channel)
    return 0;

  return -1;
}

static gint
find_stream_by_pt (GstRTSPStream * stream, gint * pt)
{
  if (stream->pt == *pt)
    return 0;

  return -1;
}

static gint
find_stream_by_udpsrc (GstRTSPStream * stream, gconstpointer a)
{
  GstElement *src = (GstElement *) a;

  if (stream->udpsrc[0] == src)
    return 0;
  if (stream->udpsrc[1] == src)
    return 0;

  return -1;
}

static gint
find_stream_by_setup (GstRTSPStream * stream, gconstpointer a)
{
  /* check qualified setup_url */
  if (!strcmp (stream->setup_url, (gchar *) a))
    return 0;
  /* check original control_url */
  if (!strcmp (stream->control_url, (gchar *) a))
    return 0;

  /* check if qualified setup_url ends with string */
  if (g_str_has_suffix (stream->control_url, (gchar *) a))
    return 0;

  return -1;
}

GstRTSPStream *
find_stream (GstRTSPSrc * src, gconstpointer data, gconstpointer func)
{
  GList *lstream;

  /* find and get stream */
  if ((lstream = g_list_find_custom (src->streams, data, (GCompareFunc) func)))
    return (GstRTSPStream *) lstream->data;

  return NULL;
}

static const GstSDPBandwidth *
gst_rtspsrc_get_bandwidth (GstRTSPSrc * src, const GstSDPMessage * sdp,
    const GstSDPMedia * media, const gchar * type)
{
  guint i, len;

  /* first look in the media specific section */
  len = gst_sdp_media_bandwidths_len (media);
  for (i = 0; i < len; i++) {
    const GstSDPBandwidth *bw = gst_sdp_media_get_bandwidth (media, i);

    if (strcmp (bw->bwtype, type) == 0)
      return bw;
  }
  /* then look in the message specific section */
  len = gst_sdp_message_bandwidths_len (sdp);
  for (i = 0; i < len; i++) {
    const GstSDPBandwidth *bw = gst_sdp_message_get_bandwidth (sdp, i);

    if (strcmp (bw->bwtype, type) == 0)
      return bw;
  }
  return NULL;
}

static void
gst_rtspsrc_collect_bandwidth (GstRTSPSrc * src, const GstSDPMessage * sdp,
    const GstSDPMedia * media, GstRTSPStream * stream)
{
  const GstSDPBandwidth *bw;

  if ((bw = gst_rtspsrc_get_bandwidth (src, sdp, media, GST_SDP_BWTYPE_AS)))
    stream->as_bandwidth = bw->bandwidth;
  else
    stream->as_bandwidth = -1;

  if ((bw = gst_rtspsrc_get_bandwidth (src, sdp, media, GST_SDP_BWTYPE_RR)))
    stream->rr_bandwidth = bw->bandwidth;
  else
    stream->rr_bandwidth = -1;

  if ((bw = gst_rtspsrc_get_bandwidth (src, sdp, media, GST_SDP_BWTYPE_RS)))
    stream->rs_bandwidth = bw->bandwidth;
  else
    stream->rs_bandwidth = -1;
}

static GstRTSPStream *
gst_rtspsrc_create_stream (GstRTSPSrc * src, GstSDPMessage * sdp, gint idx)
{
  GstRTSPStream *stream;
  const gchar *control_url;
  const gchar *payload;
  const GstSDPMedia *media;

  /* get media, should not return NULL */
  media = gst_sdp_message_get_media (sdp, idx);
  if (media == NULL)
    return NULL;

  stream = g_new0 (GstRTSPStream, 1);
  stream->parent = src;
  /* we mark the pad as not linked, we will mark it as OK when we add the pad to
   * the element. */
  stream->last_ret = GST_FLOW_NOT_LINKED;
  stream->added = FALSE;
  stream->disabled = FALSE;
  stream->id = src->numstreams++;
  stream->eos = FALSE;
  stream->discont = TRUE;
  stream->seqbase = -1;
  stream->timebase = -1;

  /* collect bandwidth information for this steam */
  gst_rtspsrc_collect_bandwidth (src, sdp, media, stream);

  /* we must have a payload. No payload means we cannot create caps */
  /* FIXME, handle multiple formats. The problem here is that we just want to
   * take the first available format that we can handle but in order to do that
   * we need to scan for depayloader plugins. Scanning for payloader plugins is
   * also suboptimal because the user maybe just wants to save the raw stream
   * and then we don't care. */
  if ((payload = gst_sdp_media_get_format (media, 0))) {
    stream->pt = atoi (payload);
    /* convert caps */
    stream->caps = gst_rtspsrc_media_to_caps (stream->pt, media);

    GST_DEBUG ("mapping sdp session level attributes to caps");
    gst_rtspsrc_sdp_attributes_to_caps (sdp->attributes, stream->caps);
    GST_DEBUG ("mapping sdp media level attributes to caps");
    gst_rtspsrc_sdp_attributes_to_caps (media->attributes, stream->caps);

    if (stream->pt >= 96) {
      /* If we have a dynamic payload type, see if we have a stream with the
       * same payload number. If there is one, they are part of the same
       * container and we only need to add one pad. */
      if (find_stream (src, &stream->pt, (gpointer) find_stream_by_pt)) {
        stream->container = TRUE;
      }
    }
  }

  /* get control url to construct the setup url. The setup url is used to
   * configure the transport of the stream and is used to identity the stream in
   * the RTP-Info header field returned from PLAY. */
  control_url = gst_sdp_media_get_attribute_val (media, "control");

  GST_DEBUG_OBJECT (src, "stream %d, (%p)", stream->id, stream);
  GST_DEBUG_OBJECT (src, " pt: %d", stream->pt);
  GST_DEBUG_OBJECT (src, " container: %d", stream->container);
  GST_DEBUG_OBJECT (src, " caps: %" GST_PTR_FORMAT, stream->caps);
  GST_DEBUG_OBJECT (src, " control: %s", GST_STR_NULL (control_url));

  if (control_url != NULL) {
    stream->control_url = g_strdup (control_url);
    /* Build a fully qualified url using the content_base if any or by prefixing
     * the original request.
     * If the control_url starts with a '/' or a non rtsp: protocol we will most
     * likely build a URL that the server will fail to understand, this is ok,
     * we will fail then. */
    if (g_str_has_prefix (control_url, "rtsp://"))
      stream->setup_url = g_strdup (control_url);
    else if (src->content_base)
      stream->setup_url =
          g_strdup_printf ("%s%s", src->content_base, control_url);
    else
      stream->setup_url =
          g_strdup_printf ("%s/%s", src->req_location, control_url);
  }
  GST_DEBUG_OBJECT (src, " setup: %s", GST_STR_NULL (stream->setup_url));

  /* we keep track of all streams */
  src->streams = g_list_append (src->streams, stream);

  return stream;

  /* ERRORS */
}

static void
gst_rtspsrc_stream_free (GstRTSPSrc * src, GstRTSPStream * stream)
{
  gint i;

  GST_DEBUG_OBJECT (src, "free stream %p", stream);

  if (stream->caps)
    gst_caps_unref (stream->caps);

  g_free (stream->control_url);
  g_free (stream->setup_url);

  for (i = 0; i < 2; i++) {
    if (stream->udpsrc[i]) {
      gst_element_set_state (stream->udpsrc[i], GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (src), stream->udpsrc[i]);
      gst_object_unref (stream->udpsrc[i]);
      stream->udpsrc[i] = NULL;
    }
    if (stream->channelpad[i]) {
      gst_object_unref (stream->channelpad[i]);
      stream->channelpad[i] = NULL;
    }
    if (stream->udpsink[i]) {
      gst_element_set_state (stream->udpsink[i], GST_STATE_NULL);
      gst_bin_remove (GST_BIN_CAST (src), stream->udpsink[i]);
      gst_object_unref (stream->udpsink[i]);
      stream->udpsink[i] = NULL;
    }
  }
  if (stream->fakesrc) {
    gst_element_set_state (stream->fakesrc, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (src), stream->fakesrc);
    gst_object_unref (stream->fakesrc);
    stream->fakesrc = NULL;
  }
  if (stream->srcpad) {
    gst_pad_set_active (stream->srcpad, FALSE);
    if (stream->added) {
      gst_element_remove_pad (GST_ELEMENT_CAST (src), stream->srcpad);
      stream->added = FALSE;
    }
    stream->srcpad = NULL;
  }
  if (stream->rtcppad) {
    gst_object_unref (stream->rtcppad);
    stream->rtcppad = NULL;
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
  if (src->session) {
    if (src->session_sig_id) {
      g_signal_handler_disconnect (src->session, src->session_sig_id);
      src->session_sig_id = 0;
    }
    gst_element_set_state (src->session, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (src), src->session);
    src->session = NULL;
  }
  src->numstreams = 0;
  if (src->props)
    gst_structure_free (src->props);
  src->props = NULL;

  g_free (src->content_base);
  src->content_base = NULL;

  if (src->range)
    gst_rtsp_range_free (src->range);
  src->range = NULL;
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
  if (p == NULL) {                      \
    res = NULL;                         \
    p = t;                              \
  }                                     \
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
gst_rtspsrc_parse_rtpmap (const gchar * rtpmap, gint * payload, gchar ** name,
    gint * rate, gchar ** params)
{
  gchar *p, *t;

  p = (gchar *) rtpmap;

  PARSE_INT (p, " ", *payload);
  if (*payload == -1)
    return FALSE;

  SKIP_SPACES (p);
  if (*p == '\0')
    return FALSE;

  PARSE_STRING (p, "/", *name);
  if (*name == NULL) {
    GST_DEBUG ("no rate, name %s", p);
    /* no rate, assume -1 then, this is not supposed to happen but RealMedia
     * streams seem to omit the rate. */
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
 * Mapping SDP attributes to caps
 *
 * prepend 'a-' to IANA registered sdp attributes names
 * (ie: not prefixed with 'x-') in order to avoid
 * collision with gstreamer standard caps properties names
 */
static void
gst_rtspsrc_sdp_attributes_to_caps (GArray * attributes, GstCaps * caps)
{
  if (attributes->len > 0) {
    GstStructure *s;
    guint i;

    s = gst_caps_get_structure (caps, 0);

    for (i = 0; i < attributes->len; i++) {
      GstSDPAttribute *attr = &g_array_index (attributes, GstSDPAttribute, i);
      gchar *tofree, *key;

      key = attr->key;

      /* skip some of the attribute we already handle */
      if (!strcmp (key, "fmtp"))
        continue;
      if (!strcmp (key, "rtpmap"))
        continue;
      if (!strcmp (key, "control"))
        continue;
      if (!strcmp (key, "range"))
        continue;

      /* string must be valid UTF8 */
      if (!g_utf8_validate (attr->value, -1, NULL))
        continue;

      if (!g_str_has_prefix (key, "x-"))
        tofree = key = g_strdup_printf ("a-%s", key);
      else
        tofree = NULL;

      GST_DEBUG ("adding caps: %s=%s", key, attr->value);
      gst_structure_set (s, key, G_TYPE_STRING, attr->value, NULL);
      g_free (tofree);
    }
  }
}

/*
 *  Mapping of caps to and from SDP fields:
 *
 *   m=<media> <UDP port> RTP/AVP <payload> 
 *   a=rtpmap:<payload> <encoding_name>/<clock_rate>[/<encoding_params>]
 *   a=fmtp:<payload> <param>[=<value>];...
 */
static GstCaps *
gst_rtspsrc_media_to_caps (gint pt, const GstSDPMedia * media)
{
  GstCaps *caps;
  const gchar *rtpmap;
  const gchar *fmtp;
  gchar *name = NULL;
  gint rate = -1;
  gchar *params = NULL;
  gchar *tmp;
  GstStructure *s;
  gint payload = 0;
  gboolean ret;

  /* get and parse rtpmap */
  if ((rtpmap = gst_sdp_media_get_attribute_val (media, "rtpmap"))) {
    ret = gst_rtspsrc_parse_rtpmap (rtpmap, &payload, &name, &rate, &params);
    if (ret) {
      if (payload != pt) {
        /* we ignore the rtpmap if the payload type is different. */
        g_warning ("rtpmap of wrong payload type, ignoring");
        name = NULL;
        rate = -1;
        params = NULL;
      }
    } else {
      /* if we failed to parse the rtpmap for a dynamic payload type, we have an
       * error */
      if (pt >= 96)
        goto no_rtpmap;
      /* else we can ignore */
      g_warning ("error parsing rtpmap, ignoring");
    }
  } else {
    /* dynamic payloads need rtpmap or we fail */
    if (pt >= 96)
      goto no_rtpmap;
  }
  /* check if we have a rate, if not, we need to look up the rate from the
   * default rates based on the payload types. */
  if (rate == -1) {
    const GstRTPPayloadInfo *info;

    if (GST_RTP_PAYLOAD_IS_DYNAMIC (pt)) {
      /* dynamic types, use media and encoding_name */
      tmp = g_ascii_strdown (media->media, -1);
      info = gst_rtp_payload_info_for_name (tmp, name);
      g_free (tmp);
    } else {
      /* static types, use payload type */
      info = gst_rtp_payload_info_for_pt (pt);
    }

    if (info) {
      if ((rate = info->clock_rate) == 0)
        rate = -1;
    }
    /* we fail if we cannot find one */
    if (rate == -1)
      goto no_rate;
  }

  tmp = g_ascii_strdown (media->media, -1);
  caps = gst_caps_new_simple ("application/x-unknown",
      "media", G_TYPE_STRING, tmp, "payload", G_TYPE_INT, pt, NULL);
  g_free (tmp);
  s = gst_caps_get_structure (caps, 0);

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
  if ((fmtp = gst_sdp_media_get_attribute_val (media, "fmtp"))) {
    gchar *p;
    gint payload = 0;

    p = (gchar *) fmtp;

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
          /* value is everything between '=' and ';'. We split the pairs at ;
           * boundaries so we can take the remainder of the value. Some servers
           * put spaces around the value which we strip off here. Alternatively
           * we could strip those spaces in the depayloaders should these spaces
           * actually carry any meaning in the future. */
          val = g_strstrip (valpos + 1);
        } else {
          /* simple <param>;.. is translated into <param>=1;... */
          val = "1";
        }
        /* strip the key of spaces, convert key to lowercase but not the value. */
        key = g_strstrip (pairs[i]);
        if (strlen (key) > 1) {
          tmp = g_ascii_strdown (key, -1);
          gst_structure_set (s, tmp, G_TYPE_STRING, val, NULL);
          g_free (tmp);
        }
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
no_rate:
  {
    g_warning ("rate unknown for payload type %d", pt);
    return NULL;
  }
}

static gboolean
gst_rtspsrc_alloc_udp_ports (GstRTSPStream * stream,
    gint * rtpport, gint * rtcpport)
{
  GstRTSPSrc *src;
  GstStateChangeReturn ret;
  GstElement *udpsrc0, *udpsrc1;
  gint tmp_rtp, tmp_rtcp;
  guint count;

  src = stream->parent;

  udpsrc0 = NULL;
  udpsrc1 = NULL;
  count = 0;

  /* Start with random port */
  tmp_rtp = 0;

  /* try to allocate 2 UDP ports, the RTP port should be an even
   * number and the RTCP port should be the next (uneven) port */
again:
  udpsrc0 = gst_element_make_from_uri (GST_URI_SRC, "udp://0.0.0.0", NULL);
  if (udpsrc0 == NULL)
    goto no_udp_protocol;
  g_object_set (G_OBJECT (udpsrc0), "port", tmp_rtp, NULL);

  ret = gst_element_set_state (udpsrc0, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    if (tmp_rtp != 0) {
      GST_DEBUG_OBJECT (src, "Unable to make udpsrc from RTP port %d", tmp_rtp);

      tmp_rtp += 2;
      if (++count > src->retry)
        goto no_ports;

      GST_DEBUG_OBJECT (src, "free RTP udpsrc");
      gst_element_set_state (udpsrc0, GST_STATE_NULL);
      gst_object_unref (udpsrc0);

      GST_DEBUG_OBJECT (src, "retry %d", count);
      goto again;
    }
    goto no_udp_protocol;
  }

  g_object_get (G_OBJECT (udpsrc0), "port", &tmp_rtp, NULL);
  GST_DEBUG_OBJECT (src, "got RTP port %d", tmp_rtp);

  /* check if port is even */
  if ((tmp_rtp & 0x01) != 0) {
    /* port not even, close and allocate another */
    if (++count > src->retry)
      goto no_ports;

    GST_DEBUG_OBJECT (src, "RTP port not even");

    GST_DEBUG_OBJECT (src, "free RTP udpsrc");
    gst_element_set_state (udpsrc0, GST_STATE_NULL);
    gst_object_unref (udpsrc0);

    GST_DEBUG_OBJECT (src, "retry %d", count);
    tmp_rtp++;
    goto again;
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
  /* tmp_rtcp port is busy already : retry to make rtp/rtcp pair */
  if (ret == GST_STATE_CHANGE_FAILURE) {

    GST_DEBUG_OBJECT (src, "Unable to make udpsrc from RTCP port %d", tmp_rtcp);

    if (++count > src->retry)
      goto no_ports;

    GST_DEBUG_OBJECT (src, "free RTP udpsrc");
    gst_element_set_state (udpsrc0, GST_STATE_NULL);
    gst_object_unref (udpsrc0);

    GST_DEBUG_OBJECT (src, "free RTCP udpsrc");
    gst_element_set_state (udpsrc1, GST_STATE_NULL);
    gst_object_unref (udpsrc1);
    udpsrc1 = NULL;

    tmp_rtp += 2;
    GST_DEBUG_OBJECT (src, "retry %d", count);
    goto again;
  }

  /* all fine, do port check */
  g_object_get (G_OBJECT (udpsrc0), "port", rtpport, NULL);
  g_object_get (G_OBJECT (udpsrc1), "port", rtcpport, NULL);

  /* this should not happen... */
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
port_error:
  {
    GST_DEBUG_OBJECT (src, "ports don't match rtp: %d<->%d, rtcp: %d<->%d",
        tmp_rtp, *rtpport, tmp_rtcp, *rtcpport);
    goto cleanup;
  }
cleanup:
  {
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
gst_rtspsrc_flush (GstRTSPSrc * src, gboolean flush)
{
  GstEvent *event;
  gint cmd, i;
  GstState state;
  GList *walk;
  GstClock *clock;
  GstClockTime base_time = GST_CLOCK_TIME_NONE;

  if (flush) {
    event = gst_event_new_flush_start ();
    GST_DEBUG_OBJECT (src, "start flush");
    cmd = CMD_STOP;
    state = GST_STATE_PAUSED;
  } else {
    event = gst_event_new_flush_stop ();
    GST_DEBUG_OBJECT (src, "stop flush");
    cmd = CMD_WAIT;
    state = GST_STATE_PLAYING;
    clock = gst_element_get_clock (GST_ELEMENT_CAST (src));
    if (clock) {
      base_time = gst_clock_get_time (clock);
      gst_object_unref (clock);
    }
  }
  gst_rtspsrc_push_event (src, event);
  gst_rtspsrc_loop_send_cmd (src, cmd, flush);

  /* make running time start start at 0 again */
  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;

    for (i = 0; i < 2; i++) {
      /* for udp case */
      if (stream->udpsrc[i]) {
        if (base_time != -1)
          gst_element_set_base_time (stream->udpsrc[i], base_time);
        gst_element_set_state (stream->udpsrc[i], state);
      }
    }
  }
  /* for tcp interleaved case */
  if (base_time != -1)
    gst_element_set_base_time (GST_ELEMENT_CAST (src), base_time);
}

static GstRTSPResult
gst_rtspsrc_connection_send (GstRTSPSrc * src, GstRTSPMessage * message,
    GTimeVal * timeout)
{
  GstRTSPResult ret;

  GST_RTSP_CONN_LOCK (src);
  if (src->connection)
    ret = gst_rtsp_connection_send (src->connection, message, timeout);
  else
    ret = GST_RTSP_ERROR;
  GST_RTSP_CONN_UNLOCK (src);

  return ret;
}

static GstRTSPResult
gst_rtspsrc_connection_receive (GstRTSPSrc * src, GstRTSPMessage * message,
    GTimeVal * timeout)
{
  GstRTSPResult ret;

  GST_RTSP_CONN_LOCK (src);
  if (src->connection)
    ret = gst_rtsp_connection_receive (src->connection, message, timeout);
  else
    ret = GST_RTSP_ERROR;
  GST_RTSP_CONN_UNLOCK (src);

  return ret;
}

static gboolean
gst_rtspsrc_do_seek (GstRTSPSrc * src, GstSegment * segment)
{
  src->state = GST_RTSP_STATE_SEEKING;
  /* PLAY will add the range header now. */
  src->need_range = TRUE;

  return TRUE;
}

static gboolean
gst_rtspsrc_perform_seek (GstRTSPSrc * src, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type = GST_SEEK_TYPE_NONE, stop_type;
  gint64 cur, stop;
  gboolean flush, skip;
  gboolean update;
  gboolean playing;
  GstSegment seeksegment = { 0, };
  GList *walk;

  if (event) {
    GST_DEBUG_OBJECT (src, "doing seek with event");

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* no negative rates yet */
    if (rate < 0.0)
      goto negative_rate;

    /* we need TIME format */
    if (format != src->segment.format)
      goto no_format;
  } else {
    GST_DEBUG_OBJECT (src, "doing seek without event");
    flags = 0;
    cur_type = GST_SEEK_TYPE_SET;
    stop_type = GST_SEEK_TYPE_SET;
  }

  /* get flush flag */
  flush = flags & GST_SEEK_FLAG_FLUSH;
  skip = flags & GST_SEEK_FLAG_SKIP;

  /* now we need to make sure the streaming thread is stopped. We do this by
   * either sending a FLUSH_START event downstream which will cause the
   * streaming thread to stop with a WRONG_STATE.
   * For a non-flushing seek we simply pause the task, which will happen as soon
   * as it completes one iteration (and thus might block when the sink is
   * blocking in preroll). */
  if (flush) {
    GST_DEBUG_OBJECT (src, "starting flush");
    gst_rtspsrc_flush (src, TRUE);
  } else {
    if (src->task) {
      gst_task_pause (src->task);
    }
  }

  /* we should now be able to grab the streaming thread because we stopped it
   * with the above flush/pause code */
  GST_RTSP_STREAM_LOCK (src);

  /* stop flushing state */
  gst_rtspsrc_loop_send_cmd (src, CMD_WAIT, FALSE);

  GST_DEBUG_OBJECT (src, "stopped streaming");

  /* copy segment, we need this because we still need the old
   * segment when we close the current segment. */
  memcpy (&seeksegment, &src->segment, sizeof (GstSegment));

  /* configure the seek parameters in the seeksegment. We will then have the
   * right values in the segment to perform the seek */
  if (event) {
    GST_DEBUG_OBJECT (src, "configuring seek");
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  /* figure out the last position we need to play. If it's configured (stop !=
   * -1), use that, else we play until the total duration of the file */
  if ((stop = seeksegment.stop) == -1)
    stop = seeksegment.duration;

  playing = (src->state == GST_RTSP_STATE_PLAYING);

  /* if we were playing, pause first */
  if (playing)
    gst_rtspsrc_pause (src);

  gst_rtspsrc_do_seek (src, &seeksegment);

  /* and continue playing */
  if (playing)
    gst_rtspsrc_play (src, &seeksegment);

  /* prepare for streaming again */
  if (flush) {
    /* if we started flush, we stop now */
    GST_DEBUG_OBJECT (src, "stopping flush");
    gst_rtspsrc_flush (src, FALSE);
  } else if (src->running) {
    /* we are running the current segment and doing a non-flushing seek,
     * close the segment first based on the previous last_stop. */
    GST_DEBUG_OBJECT (src, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, src->segment.accum, src->segment.last_stop);

    /* queue the segment for sending in the stream thread */
    if (src->close_segment)
      gst_event_unref (src->close_segment);
    src->close_segment = gst_event_new_new_segment (TRUE,
        src->segment.rate, src->segment.format,
        src->segment.accum, src->segment.last_stop, src->segment.accum);

    /* keep track of our last_stop */
    seeksegment.accum = src->segment.last_stop;
  }

  /* now we did the seek and can activate the new segment values */
  memcpy (&src->segment, &seeksegment, sizeof (GstSegment));

  /* if we're doing a segment seek, post a SEGMENT_START message */
  if (src->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT_CAST (src),
        gst_message_new_segment_start (GST_OBJECT_CAST (src),
            src->segment.format, src->segment.last_stop));
  }

  /* now create the newsegment */
  GST_DEBUG_OBJECT (src, "Creating newsegment from %" G_GINT64_FORMAT
      " to %" G_GINT64_FORMAT, src->segment.last_stop, stop);

  /* store the newsegment event so it can be sent from the streaming thread. */
  if (src->start_segment)
    gst_event_unref (src->start_segment);
  src->start_segment =
      gst_event_new_new_segment (FALSE, src->segment.rate,
      src->segment.format, src->segment.last_stop, stop,
      src->segment.last_stop);

  /* mark discont */
  GST_DEBUG_OBJECT (src, "mark DISCONT, we did a seek to another position");
  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;
    stream->discont = TRUE;
  }
  src->skip = skip;

  GST_RTSP_STREAM_UNLOCK (src);

  return TRUE;

  /* ERRORS */
negative_rate:
  {
    GST_DEBUG_OBJECT (src, "negative playback rates are not supported yet.");
    return FALSE;
  }
no_format:
  {
    GST_DEBUG_OBJECT (src, "unsupported format given, seek aborted.");
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstRTSPSrc *src;
  gboolean res = TRUE;
  gboolean forward;

  src = GST_RTSPSRC_CAST (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (src, "pad %s:%s received event %s",
      GST_DEBUG_PAD_NAME (pad), GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_rtspsrc_perform_seek (src, event);
      forward = FALSE;
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    default:
      forward = TRUE;
      break;
  }
  if (forward) {
    GstPad *target;

    if ((target = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (pad)))) {
      res = gst_pad_send_event (target, event);
      gst_object_unref (target);
    } else {
      gst_event_unref (event);
    }
  } else {
    gst_event_unref (event);
  }
  gst_object_unref (src);

  return res;
}

/* this is the final event function we receive on the internal source pad when
 * we deal with TCP connections */
static gboolean
gst_rtspsrc_handle_internal_src_event (GstPad * pad, GstEvent * event)
{
  GstRTSPSrc *src;
  gboolean res;

  src = GST_RTSPSRC_CAST (gst_pad_get_element_private (pad));

  GST_DEBUG_OBJECT (src, "pad %s:%s received event %s",
      GST_DEBUG_PAD_NAME (pad), GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    case GST_EVENT_QOS:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_LATENCY:
    default:
      gst_event_unref (event);
      res = TRUE;
      break;
  }
  return res;
}

/* this is the final query function we receive on the internal source pad when
 * we deal with TCP connections */
static gboolean
gst_rtspsrc_handle_internal_src_query (GstPad * pad, GstQuery * query)
{
  GstRTSPSrc *src;
  gboolean res = TRUE;

  src = GST_RTSPSRC_CAST (gst_pad_get_element_private (pad));

  GST_DEBUG_OBJECT (src, "pad %s:%s received query %s",
      GST_DEBUG_PAD_NAME (pad), GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      /* no idea */
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_duration (query, format, src->segment.duration);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_LATENCY:
    {
      /* we are live with a min latency of 0 and unlimited max latency, this
       * result will be updated by the session manager if there is any. */
      gst_query_set_latency (query, TRUE, 0, -1);
      break;
    }
    default:
      break;
  }

  return res;
}

/* this query is executed on the ghost source pad exposed on rtspsrc. */
static gboolean
gst_rtspsrc_handle_src_query (GstPad * pad, GstQuery * query)
{
  GstRTSPSrc *src;
  gboolean res = FALSE;

  src = GST_RTSPSRC_CAST (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (src, "pad %s:%s received query %s",
      GST_DEBUG_PAD_NAME (pad), GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          gst_query_set_duration (query, format, src->segment.duration);
          res = TRUE;
          break;
        default:
          break;
      }
      break;
    }
    default:
    {
      GstPad *target = gst_ghost_pad_get_target (GST_GHOST_PAD_CAST (pad));

      /* forward the query to the proxy target pad */
      if (target) {
        res = gst_pad_query (target, query);
        gst_object_unref (target);
      }
      break;
    }
  }
  gst_object_unref (src);

  return res;
}

/* callback for RTCP messages to be sent to the server when operating in TCP
 * mode. */
static GstFlowReturn
gst_rtspsrc_sink_chain (GstPad * pad, GstBuffer * buffer)
{
  GstRTSPSrc *src;
  GstRTSPStream *stream;
  GstFlowReturn res = GST_FLOW_OK;
  guint8 *data;
  guint size;
  GstRTSPResult ret;
  GstRTSPMessage message = { 0 };

  stream = (GstRTSPStream *) gst_pad_get_element_private (pad);
  src = stream->parent;

  data = GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer);

  gst_rtsp_message_init_data (&message, stream->channel[1]);

  /* lend the body data to the message */
  gst_rtsp_message_take_body (&message, data, size);

  GST_DEBUG_OBJECT (src, "sending %u bytes RTCP", size);
  ret = gst_rtspsrc_connection_send (src, &message, NULL);
  GST_DEBUG_OBJECT (src, "sent RTCP, %d", ret);

  /* and steal it away again because we will free it when unreffing the
   * buffer */
  gst_rtsp_message_steal_body (&message, &data, &size);
  gst_rtsp_message_unset (&message);

  gst_buffer_unref (buffer);

  return res;
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

/* this callback is called when the session manager generated a new src pad with
 * payloaded RTP packets. We simply ghost the pad here. */
static void
new_session_pad (GstElement * session, GstPad * pad, GstRTSPSrc * src)
{
  gchar *name;
  GstPadTemplate *template;
  gint id, ssrc, pt;
  GList *lstream;
  GstRTSPStream *stream;
  gboolean all_added;

  GST_DEBUG_OBJECT (src, "got new session pad %" GST_PTR_FORMAT, pad);

  GST_RTSP_STATE_LOCK (src);
  /* find stream */
  name = gst_object_get_name (GST_OBJECT_CAST (pad));
  if (sscanf (name, "recv_rtp_src_%d_%d_%d", &id, &ssrc, &pt) != 3)
    goto unknown_stream;

  GST_DEBUG_OBJECT (src, "stream: %u, SSRC %d, PT %d", id, ssrc, pt);

  stream = find_stream (src, &id, (gpointer) find_stream_by_id);
  if (stream == NULL)
    goto unknown_stream;

  /* create a new pad we will use to stream to */
  template = gst_static_pad_template_get (&rtptemplate);
  stream->srcpad = gst_ghost_pad_new_from_template (name, pad, template);
  gst_object_unref (template);
  g_free (name);

  stream->added = TRUE;
  gst_pad_set_event_function (stream->srcpad, gst_rtspsrc_handle_src_event);
  gst_pad_set_query_function (stream->srcpad, gst_rtspsrc_handle_src_query);
  gst_pad_set_active (stream->srcpad, TRUE);
  gst_element_add_pad (GST_ELEMENT_CAST (src), stream->srcpad);

  /* check if we added all streams */
  all_added = TRUE;
  for (lstream = src->streams; lstream; lstream = g_list_next (lstream)) {
    stream = (GstRTSPStream *) lstream->data;

    GST_DEBUG_OBJECT (src, "stream %p, container %d, disabled %d, added %d",
        stream, stream->container, stream->disabled, stream->added);

    /* a container stream only needs one pad added. Also disabled streams don't
     * count */
    if (!stream->container && !stream->disabled && !stream->added) {
      all_added = FALSE;
      break;
    }
  }
  GST_RTSP_STATE_UNLOCK (src);

  if (all_added) {
    GST_DEBUG_OBJECT (src, "We added all streams");
    /* when we get here, all stream are added and we can fire the no-more-pads
     * signal. */
    gst_element_no_more_pads (GST_ELEMENT_CAST (src));
  }

  return;

  /* ERRORS */
unknown_stream:
  {
    GST_DEBUG_OBJECT (src, "ignoring unknown stream");
    GST_RTSP_STATE_UNLOCK (src);
    g_free (name);
    return;
  }
}

static GstCaps *
request_pt_map (GstElement * sess, guint session, guint pt, GstRTSPSrc * src)
{
  GstRTSPStream *stream;
  GstCaps *caps;

  GST_DEBUG_OBJECT (src, "getting pt map for pt %d in session %d", pt, session);

  GST_RTSP_STATE_LOCK (src);
  stream = find_stream (src, &session, (gpointer) find_stream_by_id);
  if (!stream)
    goto unknown_stream;

  caps = stream->caps;
  if (caps)
    gst_caps_ref (caps);
  GST_RTSP_STATE_UNLOCK (src);

  return caps;

unknown_stream:
  {
    GST_DEBUG_OBJECT (src, "unknown stream %d", session);
    GST_RTSP_STATE_UNLOCK (src);
    return NULL;
  }
}

static void
gst_rtspsrc_do_stream_eos (GstRTSPSrc * src, guint session)
{
  GstRTSPStream *stream;

  GST_DEBUG_OBJECT (src, "setting stream for session %u to EOS", session);

  /* get stream for session */
  stream = find_stream (src, &session, (gpointer) find_stream_by_id);
  if (!stream)
    goto unknown_stream;

  if (stream->eos)
    goto was_eos;

  stream->eos = TRUE;
  gst_rtspsrc_stream_push_event (src, stream, gst_event_new_eos ());
  return;

  /* ERRORS */
unknown_stream:
  {
    GST_DEBUG_OBJECT (src, "unknown stream for session %u", session);
    return;
  }
was_eos:
  {
    GST_DEBUG_OBJECT (src, "stream for session %u was already EOS", session);
    return;
  }
}

static void
on_bye_ssrc (GstElement * manager, guint session, guint32 ssrc,
    GstRTSPSrc * src)
{
  GST_DEBUG_OBJECT (src, "SSRC %08x in session %u received BYE", ssrc, session);

  gst_rtspsrc_do_stream_eos (src, session);
}

static void
on_timeout (GstElement * manager, guint session, guint32 ssrc, GstRTSPSrc * src)
{
  GST_DEBUG_OBJECT (src, "SSRC %08x in session %u timed out", ssrc, session);

  gst_rtspsrc_do_stream_eos (src, session);
}

static void
on_npt_stop (GstElement * manager, guint session, guint32 ssrc,
    GstRTSPSrc * src)
{
  GST_DEBUG_OBJECT (src, "SSRC %08x in session %u reached the NPT stop", ssrc,
      session);

  gst_rtspsrc_do_stream_eos (src, session);
}

/* try to get and configure a manager */
static gboolean
gst_rtspsrc_stream_configure_manager (GstRTSPSrc * src, GstRTSPStream * stream,
    GstRTSPTransport * transport)
{
  const gchar *manager;
  gchar *name;
  GstStateChangeReturn ret;

  /* find a manager */
  if (gst_rtsp_transport_get_manager (transport->trans, &manager, 0) < 0)
    goto no_manager;

  if (manager) {
    GST_DEBUG_OBJECT (src, "using manager %s", manager);

    /* configure the manager */
    if (src->session == NULL) {
      GstState target;

      if (!(src->session = gst_element_factory_make (manager, NULL))) {
        /* fallback */
        if (gst_rtsp_transport_get_manager (transport->trans, &manager, 1) < 0)
          goto no_manager;

        if (!manager)
          goto use_no_manager;

        if (!(src->session = gst_element_factory_make (manager, NULL)))
          goto manager_failed;
      }

      /* we manage this element */
      gst_bin_add (GST_BIN_CAST (src), src->session);

      GST_OBJECT_LOCK (src);
      target = GST_STATE_TARGET (src);
      GST_OBJECT_UNLOCK (src);

      ret = gst_element_set_state (src->session, target);
      if (ret == GST_STATE_CHANGE_FAILURE)
        goto start_session_failure;

      g_object_set (src->session, "latency", src->latency, NULL);

      /* connect to signals if we did not already do so */
      GST_DEBUG_OBJECT (src, "connect to signals on session manager, stream %p",
          stream);
      src->session_sig_id =
          g_signal_connect (src->session, "pad-added",
          (GCallback) new_session_pad, src);
      src->session_ptmap_id =
          g_signal_connect (src->session, "request-pt-map",
          (GCallback) request_pt_map, src);
      g_signal_connect (src->session, "on-bye-ssrc", (GCallback) on_bye_ssrc,
          src);
      g_signal_connect (src->session, "on-bye-timeout", (GCallback) on_timeout,
          src);
      g_signal_connect (src->session, "on-timeout", (GCallback) on_timeout,
          src);
      /* FIXME: remove this once the rdtmanager is released */
      if (g_signal_lookup ("on-npt-stop", G_OBJECT_TYPE (src->session)) != 0) {
        g_signal_connect (src->session, "on-npt-stop", (GCallback) on_npt_stop,
            src);
      } else {
        GST_INFO_OBJECT (src, "skipping on-npt-stop handling, not implemented");
      }
    }

    /* we stream directly to the manager, get some pads. Each RTSP stream goes
     * into a separate RTP session. */
    name = g_strdup_printf ("recv_rtp_sink_%d", stream->id);
    stream->channelpad[0] = gst_element_get_request_pad (src->session, name);
    g_free (name);
    name = g_strdup_printf ("recv_rtcp_sink_%d", stream->id);
    stream->channelpad[1] = gst_element_get_request_pad (src->session, name);
    g_free (name);
  }

use_no_manager:
  return TRUE;

  /* ERRORS */
no_manager:
  {
    GST_DEBUG_OBJECT (src, "cannot get a session manager");
    return FALSE;
  }
manager_failed:
  {
    GST_DEBUG_OBJECT (src, "no session manager element %s found", manager);
    return FALSE;
  }
start_session_failure:
  {
    GST_DEBUG_OBJECT (src, "could not start session");
    return FALSE;
  }
}

/* free the UDP sources allocated when negotiating a transport.
 * This function is called when the server negotiated to a transport where the
 * UDP sources are not needed anymore, such as TCP or multicast. */
static void
gst_rtspsrc_stream_free_udp (GstRTSPStream * stream)
{
  gint i;

  for (i = 0; i < 2; i++) {
    if (stream->udpsrc[i]) {
      gst_element_set_state (stream->udpsrc[i], GST_STATE_NULL);
      gst_object_unref (stream->udpsrc[i]);
      stream->udpsrc[i] = NULL;
    }
  }
}

/* for TCP, create pads to send and receive data to and from the manager and to
 * intercept various events and queries
 */
static gboolean
gst_rtspsrc_stream_configure_tcp (GstRTSPSrc * src, GstRTSPStream * stream,
    GstRTSPTransport * transport, GstPad ** outpad)
{
  gchar *name;
  GstPadTemplate *template;
  GstPad *pad0, *pad1;

  /* configure for interleaved delivery, nothing needs to be done
   * here, the loop function will call the chain functions of the
   * session manager. */
  stream->channel[0] = transport->interleaved.min;
  stream->channel[1] = transport->interleaved.max;
  GST_DEBUG_OBJECT (src, "stream %p on channels %d-%d", stream,
      stream->channel[0], stream->channel[1]);

  /* we can remove the allocated UDP ports now */
  gst_rtspsrc_stream_free_udp (stream);

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
    gst_pad_set_active (stream->channelpad[0], TRUE);

    *outpad = gst_object_ref (stream->channelpad[0]);
  } else {
    GST_DEBUG_OBJECT (src, "using manager source pad");

    template = gst_static_pad_template_get (&anysrctemplate);

    /* allocate pads for sending the channel data into the manager */
    pad0 = gst_pad_new_from_template (template, "internalsrc0");
    gst_pad_link (pad0, stream->channelpad[0]);
    gst_object_unref (stream->channelpad[0]);
    stream->channelpad[0] = pad0;
    gst_pad_set_event_function (pad0, gst_rtspsrc_handle_internal_src_event);
    gst_pad_set_query_function (pad0, gst_rtspsrc_handle_internal_src_query);
    gst_pad_set_element_private (pad0, src);
    gst_pad_set_active (pad0, TRUE);

    if (stream->channelpad[1]) {
      /* if we have a sinkpad for the other channel, create a pad and link to the
       * manager. */
      pad1 = gst_pad_new_from_template (template, "internalsrc1");
      gst_pad_set_event_function (pad1, gst_rtspsrc_handle_internal_src_event);
      gst_pad_link (pad1, stream->channelpad[1]);
      gst_object_unref (stream->channelpad[1]);
      stream->channelpad[1] = pad1;
      gst_pad_set_active (pad1, TRUE);
    }
    gst_object_unref (template);
  }
  /* setup RTCP transport back to the server if we have to. */
  if (src->session && src->do_rtcp) {
    GstPad *pad;

    template = gst_static_pad_template_get (&anysinktemplate);

    stream->rtcppad = gst_pad_new_from_template (template, "internalsink0");
    gst_pad_set_chain_function (stream->rtcppad, gst_rtspsrc_sink_chain);
    gst_pad_set_element_private (stream->rtcppad, stream);
    gst_pad_set_active (stream->rtcppad, TRUE);

    /* get session RTCP pad */
    name = g_strdup_printf ("send_rtcp_src_%d", stream->id);
    pad = gst_element_get_request_pad (src->session, name);
    g_free (name);

    /* and link */
    if (pad) {
      gst_pad_link (pad, stream->rtcppad);
      gst_object_unref (pad);
    }

    gst_object_unref (template);
  }
  return TRUE;
}

/* For multicast create UDP sources and join the multicast group. */
static gboolean
gst_rtspsrc_stream_configure_mcast (GstRTSPSrc * src, GstRTSPStream * stream,
    GstRTSPTransport * transport, GstPad ** outpad)
{
  gchar *uri;

  GST_DEBUG_OBJECT (src, "creating UDP sources for multicast");

  /* we can remove the allocated UDP ports now */
  gst_rtspsrc_stream_free_udp (stream);

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
    gst_element_set_state (stream->udpsrc[0], GST_STATE_PAUSED);
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
    gst_object_ref (stream->udpsrc[1]);
    gst_object_sink (stream->udpsrc[1]);

    gst_element_set_state (stream->udpsrc[1], GST_STATE_PAUSED);
  }
  return TRUE;

  /* ERRORS */
no_element:
  {
    GST_DEBUG_OBJECT (src, "no UDP source element found");
    return FALSE;
  }
}

/* configure the remainder of the UDP ports */
static gboolean
gst_rtspsrc_stream_configure_udp (GstRTSPSrc * src, GstRTSPStream * stream,
    GstRTSPTransport * transport, GstPad ** outpad)
{
  /* we manage the UDP elements now. For unicast, the UDP sources where
   * allocated in the stream when we suggested a transport. */
  if (stream->udpsrc[0]) {
    gst_bin_add (GST_BIN_CAST (src), stream->udpsrc[0]);

    GST_DEBUG_OBJECT (src, "setting up UDP source");

    /* configure a timeout on the UDP port. When the timeout message is
     * posted, we assume UDP transport is not possible. We reconnect using TCP
     * if we can. */
    g_object_set (G_OBJECT (stream->udpsrc[0]), "timeout", src->udp_timeout,
        NULL);

    /* get output pad of the UDP source. */
    *outpad = gst_element_get_static_pad (stream->udpsrc[0], "src");

    /* save it so we can unblock */
    stream->blockedpad = *outpad;

    /* configure pad block on the pad. As soon as there is dataflow on the
     * UDP source, we know that UDP is not blocked by a firewall and we can
     * configure all the streams to let the application autoplug decoders. */
    gst_pad_set_blocked_async (stream->blockedpad, TRUE,
        (GstPadBlockCallback) pad_blocked, src);

    if (stream->channelpad[0]) {
      GST_DEBUG_OBJECT (src, "connecting UDP source 0 to manager");
      /* configure for UDP delivery, we need to connect the UDP pads to
       * the session plugin. */
      gst_pad_link (*outpad, stream->channelpad[0]);
      gst_object_unref (*outpad);
      *outpad = NULL;
      /* we connected to pad-added signal to get pads from the manager */
    } else {
      GST_DEBUG_OBJECT (src, "using UDP src pad as output");
    }
  }

  /* RTCP port */
  if (stream->udpsrc[1]) {
    gst_bin_add (GST_BIN_CAST (src), stream->udpsrc[1]);

    if (stream->channelpad[1]) {
      GstPad *pad;

      GST_DEBUG_OBJECT (src, "connecting UDP source 1 to manager");

      pad = gst_element_get_static_pad (stream->udpsrc[1], "src");
      gst_pad_link (pad, stream->channelpad[1]);
      gst_object_unref (pad);
    } else {
      /* leave unlinked */
    }
  }
  return TRUE;
}

/* configure the UDP sink back to the server for status reports */
static gboolean
gst_rtspsrc_stream_configure_udp_sinks (GstRTSPSrc * src,
    GstRTSPStream * stream, GstRTSPTransport * transport)
{
  GstPad *pad;
  gint rtp_port, rtcp_port, sockfd = -1;
  const gchar *destination;
  gchar *uri, *name;

  /* get host and port */
  if (transport->lower_transport == GST_RTSP_LOWER_TRANS_UDP_MCAST) {
    rtp_port = transport->port.min;
    rtcp_port = transport->port.max;
  } else {
    rtp_port = transport->server_port.min;
    rtcp_port = transport->server_port.max;
  }

  /* first take the source, then the endpoint to figure out where to send
   * the RTCP. */
  destination = transport->source;
  if (destination == NULL)
    destination = gst_rtsp_connection_get_ip (src->connection);

  /* try to construct the fakesrc to the RTP port of the server to open up any
   * NAT firewalls */
  if (rtp_port != -1) {
    GST_DEBUG_OBJECT (src, "configure RTP UDP sink for %s:%d", destination,
        rtp_port);

    uri = g_strdup_printf ("udp://%s:%d", destination, rtp_port);
    stream->udpsink[0] = gst_element_make_from_uri (GST_URI_SINK, uri, NULL);
    g_free (uri);
    if (stream->udpsink[0] == NULL)
      goto no_sink_element;

    /* no sync or async state changes needed */
    g_object_set (G_OBJECT (stream->udpsink[0]), "sync", FALSE, "async", FALSE,
        NULL);

    if (stream->udpsrc[0]) {
      /* configure socket, we give it the same UDP socket as the udpsrc for RTP
       * so that NAT firewalls will open a hole for us */
      g_object_get (G_OBJECT (stream->udpsrc[0]), "sock", &sockfd, NULL);
      GST_DEBUG_OBJECT (src, "RTP UDP src has sock %d", sockfd);
      /* configure socket and make sure udpsink does not close it when shutting
       * down, it belongs to udpsrc after all. */
      g_object_set (G_OBJECT (stream->udpsink[0]), "sockfd", sockfd, NULL);
      g_object_set (G_OBJECT (stream->udpsink[0]), "closefd", FALSE, NULL);
    }

    /* the source for the dummy packets to open up NAT */
    stream->fakesrc = gst_element_factory_make ("fakesrc", NULL);
    if (stream->fakesrc == NULL)
      goto no_fakesrc_element;

    /* random data in 5 buffers, a size of 200 bytes should be fine */
    g_object_set (G_OBJECT (stream->fakesrc), "filltype", 3, "num-buffers", 5,
        NULL);
    g_object_set (G_OBJECT (stream->fakesrc), "sizetype", 2, "sizemax", 200,
        "silent", TRUE, NULL);

    /* we don't want to consider this a sink */
    GST_OBJECT_FLAG_UNSET (stream->udpsink[0], GST_ELEMENT_IS_SINK);

    /* keep everything locked */
    gst_element_set_locked_state (stream->udpsink[0], TRUE);
    gst_element_set_locked_state (stream->fakesrc, TRUE);

    gst_object_ref (stream->udpsink[0]);
    gst_bin_add (GST_BIN_CAST (src), stream->udpsink[0]);
    gst_object_ref (stream->fakesrc);
    gst_bin_add (GST_BIN_CAST (src), stream->fakesrc);

    gst_element_link (stream->fakesrc, stream->udpsink[0]);
  }
  /* it's possible that the server does not want us to send RTCP in which case
   * the port is -1 */
  if (rtcp_port != -1 && src->session != NULL && src->do_rtcp) {
    GST_DEBUG_OBJECT (src, "configure RTCP UDP sink for %s:%d", destination,
        rtcp_port);

    uri = g_strdup_printf ("udp://%s:%d", destination, rtcp_port);
    stream->udpsink[1] = gst_element_make_from_uri (GST_URI_SINK, uri, NULL);
    g_free (uri);
    if (stream->udpsink[1] == NULL)
      goto no_sink_element;

    /* no sync needed */
    g_object_set (G_OBJECT (stream->udpsink[1]), "sync", FALSE, NULL);
    /* no async state changes needed */
    g_object_set (G_OBJECT (stream->udpsink[1]), "async", FALSE, NULL);

    if (stream->udpsrc[1]) {
      /* configure socket, we give it the same UDP socket as the udpsrc for RTCP
       * because some servers check the port number of where it sends RTCP to identify
       * the RTCP packets it receives */
      g_object_get (G_OBJECT (stream->udpsrc[1]), "sock", &sockfd, NULL);
      GST_DEBUG_OBJECT (src, "RTCP UDP src has sock %d", sockfd);
      /* configure socket and make sure udpsink does not close it when shutting
       * down, it belongs to udpsrc after all. */
      g_object_set (G_OBJECT (stream->udpsink[1]), "sockfd", sockfd, NULL);
      g_object_set (G_OBJECT (stream->udpsink[1]), "closefd", FALSE, NULL);
    }

    /* we don't want to consider this a sink */
    GST_OBJECT_FLAG_UNSET (stream->udpsink[1], GST_ELEMENT_IS_SINK);

    /* we keep this playing always */
    gst_element_set_locked_state (stream->udpsink[1], TRUE);
    gst_element_set_state (stream->udpsink[1], GST_STATE_PLAYING);

    gst_object_ref (stream->udpsink[1]);
    gst_bin_add (GST_BIN_CAST (src), stream->udpsink[1]);

    stream->rtcppad = gst_element_get_static_pad (stream->udpsink[1], "sink");

    /* get session RTCP pad */
    name = g_strdup_printf ("send_rtcp_src_%d", stream->id);
    pad = gst_element_get_request_pad (src->session, name);
    g_free (name);

    /* and link */
    if (pad) {
      gst_pad_link (pad, stream->rtcppad);
      gst_object_unref (pad);
    }
  }

  return TRUE;

  /* ERRORS */
no_sink_element:
  {
    GST_DEBUG_OBJECT (src, "no UDP sink element found");
    return FALSE;
  }
no_fakesrc_element:
  {
    GST_DEBUG_OBJECT (src, "no fakesrc element found");
    return FALSE;
  }
}

/* sets up all elements needed for streaming over the specified transport.
 * Does not yet expose the element pads, this will be done when there is actuall
 * dataflow detected, which might never happen when UDP is blocked in a
 * firewall, for example.
 */
static gboolean
gst_rtspsrc_stream_configure_transport (GstRTSPStream * stream,
    GstRTSPTransport * transport)
{
  GstRTSPSrc *src;
  GstPad *outpad = NULL;
  GstPadTemplate *template;
  gchar *name;
  GstStructure *s;
  const gchar *mime;

  src = stream->parent;

  GST_DEBUG_OBJECT (src, "configuring transport for stream %p", stream);

  s = gst_caps_get_structure (stream->caps, 0);

  /* get the proper mime type for this stream now */
  if (gst_rtsp_transport_get_mime (transport->trans, &mime) < 0)
    goto unknown_transport;
  if (!mime)
    goto unknown_transport;

  /* configure the final mime type */
  GST_DEBUG_OBJECT (src, "setting mime to %s", mime);
  gst_structure_set_name (s, mime);

  /* try to get and configure a manager, channelpad[0-1] will be configured with
   * the pads for the manager, or NULL when no manager is needed. */
  if (!gst_rtspsrc_stream_configure_manager (src, stream, transport))
    goto no_manager;

  switch (transport->lower_transport) {
    case GST_RTSP_LOWER_TRANS_TCP:
      if (!gst_rtspsrc_stream_configure_tcp (src, stream, transport, &outpad))
        goto transport_failed;
      break;
    case GST_RTSP_LOWER_TRANS_UDP_MCAST:
      if (!gst_rtspsrc_stream_configure_mcast (src, stream, transport, &outpad))
        goto transport_failed;
      /* fallthrough, the rest is the same for UDP and MCAST */
    case GST_RTSP_LOWER_TRANS_UDP:
      if (!gst_rtspsrc_stream_configure_udp (src, stream, transport, &outpad))
        goto transport_failed;
      /* configure udpsinks back to the server for RTCP messages and for the
       * dummy RTP messages to open NAT. */
      if (!gst_rtspsrc_stream_configure_udp_sinks (src, stream, transport))
        goto transport_failed;
      break;
    default:
      goto unknown_transport;
  }

  if (outpad) {
    GST_DEBUG_OBJECT (src, "creating ghostpad");

    gst_pad_use_fixed_caps (outpad);

    /* create ghostpad, don't add just yet, this will be done when we activate
     * the stream. */
    name = g_strdup_printf ("stream%d", stream->id);
    template = gst_static_pad_template_get (&rtptemplate);
    stream->srcpad = gst_ghost_pad_new_from_template (name, outpad, template);
    gst_pad_set_event_function (stream->srcpad, gst_rtspsrc_handle_src_event);
    gst_pad_set_query_function (stream->srcpad, gst_rtspsrc_handle_src_query);
    gst_object_unref (template);
    g_free (name);

    gst_object_unref (outpad);
  }
  /* mark pad as ok */
  stream->last_ret = GST_FLOW_OK;

  return TRUE;

  /* ERRORS */
transport_failed:
  {
    GST_DEBUG_OBJECT (src, "failed to configure transport");
    return FALSE;
  }
unknown_transport:
  {
    GST_DEBUG_OBJECT (src, "unknown transport");
    return FALSE;
  }
no_manager:
  {
    GST_DEBUG_OBJECT (src, "cannot get a session manager");
    return FALSE;
  }
}

/* send a couple of dummy random packets on the receiver RTP port to the server,
 * this should make a firewall think we initiated the data transfer and
 * hopefully allow packets to go from the sender port to our RTP receiver port */
static gboolean
gst_rtspsrc_send_dummy_packets (GstRTSPSrc * src)
{
  GList *walk;

  if (src->nat_method != GST_RTSP_NAT_DUMMY)
    return TRUE;

  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;

    if (stream->fakesrc && stream->udpsink[0]) {
      GST_DEBUG_OBJECT (src, "sending dummy packet to stream %p", stream);
      gst_element_set_state (stream->udpsink[0], GST_STATE_NULL);
      gst_element_set_state (stream->fakesrc, GST_STATE_NULL);
      gst_element_set_state (stream->udpsink[0], GST_STATE_PLAYING);
      gst_element_set_state (stream->fakesrc, GST_STATE_PLAYING);
    }
  }
  return TRUE;
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

  GST_DEBUG_OBJECT (src, "activating streams");

  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;

    if (stream->udpsrc[0]) {
      /* remove timeout, we are streaming now and timeouts will be handled by
       * the session manager and jitter buffer */
      g_object_set (G_OBJECT (stream->udpsrc[0]), "timeout", (guint64) 0, NULL);
    }
    if (stream->srcpad) {
      /* if we don't have a session manager, set the caps now. If we have a
       * session, we will get a notification of the pad and the caps. */
      if (!src->session) {
        GST_DEBUG_OBJECT (src, "setting pad caps for stream %p", stream);
        gst_pad_set_caps (stream->srcpad, stream->caps);
      }

      GST_DEBUG_OBJECT (src, "activating stream pad %p", stream);
      gst_pad_set_active (stream->srcpad, TRUE);
      /* add the pad */
      if (!stream->added) {
        GST_DEBUG_OBJECT (src, "adding stream pad %p", stream);
        gst_element_add_pad (GST_ELEMENT_CAST (src), stream->srcpad);
        stream->added = TRUE;
      }
    }
  }

  /* unblock all pads */
  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;

    if (stream->blockedpad) {
      GST_DEBUG_OBJECT (src, "unblocking stream pad %p", stream);
      gst_pad_set_blocked_async (stream->blockedpad, FALSE,
          (GstPadBlockCallback) pad_unblocked, src);
      stream->blockedpad = NULL;
    }
  }

  return TRUE;
}

static void
gst_rtspsrc_configure_caps (GstRTSPSrc * src, GstSegment * segment)
{
  GList *walk;
  guint64 start, stop;
  gdouble play_speed, play_scale;

  GST_DEBUG_OBJECT (src, "configuring stream caps");

  start = segment->last_stop;
  stop = segment->duration;
  play_speed = segment->rate;
  play_scale = segment->applied_rate;

  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    GstRTSPStream *stream = (GstRTSPStream *) walk->data;
    GstCaps *caps;

    if ((caps = stream->caps)) {
      caps = gst_caps_make_writable (caps);
      /* update caps */
      if (stream->timebase != -1)
        gst_caps_set_simple (caps, "clock-base", G_TYPE_UINT,
            (guint) stream->timebase, NULL);
      if (stream->seqbase != -1)
        gst_caps_set_simple (caps, "seqnum-base", G_TYPE_UINT,
            (guint) stream->seqbase, NULL);
      gst_caps_set_simple (caps, "npt-start", G_TYPE_UINT64, start, NULL);
      if (stop != -1)
        gst_caps_set_simple (caps, "npt-stop", G_TYPE_UINT64, stop, NULL);
      gst_caps_set_simple (caps, "play-speed", G_TYPE_DOUBLE, play_speed, NULL);
      gst_caps_set_simple (caps, "play-scale", G_TYPE_DOUBLE, play_scale, NULL);

      stream->caps = caps;
    }
    GST_DEBUG_OBJECT (src, "stream %p, caps %" GST_PTR_FORMAT, stream, caps);
  }
  if (src->session) {
    GST_DEBUG_OBJECT (src, "clear session");
    g_signal_emit_by_name (src->session, "clear-pt-map", NULL);
  }
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
gst_rtspsrc_stream_push_event (GstRTSPSrc * src, GstRTSPStream * stream,
    GstEvent * event)
{
  /* only streams that have a connection to the outside world */
  if (stream->srcpad == NULL)
    goto done;

  if (stream->channelpad[0]) {
    gst_event_ref (event);
    if (GST_PAD_IS_SRC (stream->channelpad[0]))
      gst_pad_push_event (stream->channelpad[0], event);
    else
      gst_pad_send_event (stream->channelpad[0], event);
  }

  if (stream->channelpad[1]) {
    gst_event_ref (event);
    if (GST_PAD_IS_SRC (stream->channelpad[1]))
      gst_pad_push_event (stream->channelpad[1], event);
    else
      gst_pad_send_event (stream->channelpad[1], event);
  }

done:
  gst_event_unref (event);
}

static void
gst_rtspsrc_push_event (GstRTSPSrc * src, GstEvent * event)
{
  GList *streams;

  for (streams = src->streams; streams; streams = g_list_next (streams)) {
    GstRTSPStream *ostream = (GstRTSPStream *) streams->data;

    gst_event_ref (event);
    gst_rtspsrc_stream_push_event (src, ostream, event);
  }
  gst_event_unref (event);
}

/* FIXME, handle server request, reply with OK, for now */
static GstRTSPResult
gst_rtspsrc_handle_request (GstRTSPSrc * src, GstRTSPMessage * request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPResult res;

  GST_DEBUG_OBJECT (src, "got server request message");

  if (src->debug)
    gst_rtsp_message_dump (request);

  res = gst_rtsp_ext_list_receive_request (src->extensions, request);

  if (res == GST_RTSP_ENOTIMPL) {
    /* default implementation, send OK */
    res =
        gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK, "OK",
        request);
    if (res < 0)
      goto send_error;

    GST_DEBUG_OBJECT (src, "replying with OK");

    if (src->debug)
      gst_rtsp_message_dump (&response);

    res = gst_rtspsrc_connection_send (src, &response, NULL);
    if (res < 0)
      goto send_error;
  } else if (res == GST_RTSP_EEOF)
    return res;

  return GST_RTSP_OK;

  /* ERRORS */
send_error:
  {
    return res;
  }
}

/* send server keep-alive */
static GstRTSPResult
gst_rtspsrc_send_keep_alive (GstRTSPSrc * src)
{
  GstRTSPMessage request = { 0 };
  GstRTSPResult res;
  GstRTSPMethod method;

  GST_DEBUG_OBJECT (src, "creating server keep-alive");

  /* find a method to use for keep-alive */
  if (src->methods & GST_RTSP_GET_PARAMETER)
    method = GST_RTSP_GET_PARAMETER;
  else
    method = GST_RTSP_OPTIONS;

  res = gst_rtsp_message_init_request (&request, method, src->req_location);
  if (res < 0)
    goto send_error;

  if (src->debug)
    gst_rtsp_message_dump (&request);

  res = gst_rtspsrc_connection_send (src, &request, NULL);
  if (res < 0)
    goto send_error;

  gst_rtsp_connection_reset_timeout (src->connection);
  gst_rtsp_message_unset (&request);

  return GST_RTSP_OK;

  /* ERRORS */
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    gst_rtsp_message_unset (&request);
    GST_ELEMENT_WARNING (src, RESOURCE, WRITE, (NULL),
        ("Could not send keep-alive. (%s)", str));
    g_free (str);
    return res;
  }
}

static GstFlowReturn
gst_rtspsrc_loop_interleaved (GstRTSPSrc * src)
{
  GstRTSPMessage message = { 0 };
  GstRTSPResult res;
  gint channel;
  GstRTSPStream *stream;
  GstPad *outpad = NULL;
  guint8 *data;
  guint size;
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf;
  gboolean is_rtcp, have_data;

  /* here we are only interested in data messages */
  have_data = FALSE;
  do {
    GTimeVal tv_timeout;

    /* get the next timeout interval */
    gst_rtsp_connection_next_timeout (src->connection, &tv_timeout);

    /* see if the timeout period expired */
    if ((tv_timeout.tv_sec | tv_timeout.tv_usec) == 0) {
      GST_DEBUG_OBJECT (src, "timout, sending keep-alive");
      /* send keep-alive, ignore the result, a warning will be posted. */
      gst_rtspsrc_send_keep_alive (src);
    }

    GST_DEBUG_OBJECT (src, "doing receive");

    /* We need to check if playback has been paused while we have been
     * doing something else in our own GstTask (e.g. pushing buffer). There
     * is a slight chance that we have just received data buffer when PAUSE
     * state change happens (in another thread). In this case we well be
     * totally ignorant of that unless we explicitly check it here. */
    GST_RTSP_STATE_LOCK (src);
    if (src->state == GST_RTSP_STATE_READY) {
      /* We are looping in a paused mode */
      GST_RTSP_STATE_UNLOCK (src);
      goto already_paused;
    }
    /* protect the connection with the connection lock so that we can see when
     * we are finished doing server communication */
    res = gst_rtspsrc_connection_receive (src, &message, src->ptcp_timeout);
    GST_RTSP_STATE_UNLOCK (src);

    switch (res) {
      case GST_RTSP_OK:
        GST_DEBUG_OBJECT (src, "we received a server message");
        break;
      case GST_RTSP_EINTR:
        /* we got interrupted this means we need to stop */
        goto interrupt;
      case GST_RTSP_ETIMEOUT:
        /* no reply, go EOS */
        goto timeout;
      case GST_RTSP_EEOF:
        /* go EOS when the server closed the connection */
        goto server_eof;
      default:
        goto receive_error;
    }

    switch (message.type) {
      case GST_RTSP_MESSAGE_REQUEST:
        /* server sends us a request message, handle it */
        res = gst_rtspsrc_handle_request (src, &message);
        if (res == GST_RTSP_EEOF)
          goto server_eof;
        else if (res < 0)
          goto handle_request_failed;
        break;
      case GST_RTSP_MESSAGE_RESPONSE:
        /* we ignore response messages */
        GST_DEBUG_OBJECT (src, "ignoring response message");
        if (src->debug)
          gst_rtsp_message_dump (&message);
        break;
      case GST_RTSP_MESSAGE_DATA:
        GST_DEBUG_OBJECT (src, "got data message");
        have_data = TRUE;
        break;
      default:
        GST_WARNING_OBJECT (src, "ignoring unknown message type %d",
            message.type);
        break;
    }
  }
  while (!have_data);

  channel = message.type_data.data.channel;

  stream = find_stream (src, &channel, (gpointer) find_stream_by_channel);
  if (!stream)
    goto unknown_stream;

  if (channel == stream->channel[0]) {
    outpad = stream->channelpad[0];
    is_rtcp = FALSE;
  } else if (channel == stream->channel[1]) {
    outpad = stream->channelpad[1];
    is_rtcp = TRUE;
  } else {
    is_rtcp = FALSE;
  }

  /* take a look at the body to figure out what we have */
  gst_rtsp_message_get_body (&message, &data, &size);
  if (size < 2)
    goto invalid_length;

  /* channels are not correct on some servers, do extra check */
  if (data[1] >= 200 && data[1] <= 204) {
    /* hmm RTCP message switch to the RTCP pad of the same stream. */
    outpad = stream->channelpad[1];
    is_rtcp = TRUE;
  }

  /* we have no clue what this is, just ignore then. */
  if (outpad == NULL)
    goto unknown_stream;

  /* take the message body for further processing */
  gst_rtsp_message_steal_body (&message, &data, &size);

  /* strip the trailing \0 */
  size -= 1;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = data;
  GST_BUFFER_MALLOCDATA (buf) = data;
  GST_BUFFER_SIZE (buf) = size;

  /* don't need message anymore */
  gst_rtsp_message_unset (&message);

  GST_DEBUG_OBJECT (src, "pushing data of size %d on channel %d", size,
      channel);

  if (src->need_activate) {
    gst_rtspsrc_activate_streams (src);
    src->need_activate = FALSE;
  }

  if (!src->session) {
    /* set stream caps on buffer when we don't have a session manager to do it
     * for us */
    gst_buffer_set_caps (buf, stream->caps);
  }

  if (src->base_time == -1) {
    /* Take current running_time. This timestamp will be put on
     * the first buffer of each stream because we are a live source and so we
     * timestamp with the running_time. When we are dealing with TCP, we also
     * only timestamp the first buffer (using the DISCONT flag) because a server
     * typically bursts data, for which we don't want to compensate by speeding
     * up the media. The other timestamps will be interpollated from this one
     * using the RTP timestamps. */
    GST_OBJECT_LOCK (src);
    if (GST_ELEMENT_CLOCK (src)) {
      GstClockTime now;
      GstClockTime base_time;

      now = gst_clock_get_time (GST_ELEMENT_CLOCK (src));
      base_time = GST_ELEMENT_CAST (src)->base_time;

      src->base_time = now - base_time;

      GST_DEBUG_OBJECT (src, "first buffer at time %" GST_TIME_FORMAT ", base %"
          GST_TIME_FORMAT, GST_TIME_ARGS (now), GST_TIME_ARGS (base_time));
    }
    GST_OBJECT_UNLOCK (src);
  }

  if (stream->discont && !is_rtcp) {
    /* mark first RTP buffer as discont */
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
    /* first buffer gets the timestamp, other buffers are not timestamped and
     * their presentation time will be interpollated from the rtp timestamps. */
    GST_DEBUG_OBJECT (src, "setting timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (src->base_time));

    GST_BUFFER_TIMESTAMP (buf) = src->base_time;
  }

  /* chain to the peer pad */
  if (GST_PAD_IS_SINK (outpad))
    ret = gst_pad_chain (outpad, buf);
  else
    ret = gst_pad_push (outpad, buf);

  if (!is_rtcp) {
    /* combine all stream flows for the data transport */
    ret = gst_rtspsrc_combine_flows (src, stream, ret);
  }
  return ret;

  /* ERRORS */
unknown_stream:
  {
    GST_DEBUG_OBJECT (src, "unknown stream on channel %d, ignored", channel);
    gst_rtsp_message_unset (&message);
    return GST_FLOW_OK;
  }
timeout:
  {
    GST_DEBUG_OBJECT (src, "we got a timeout");
    GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
        ("Timeout while waiting for server message."));
    gst_rtsp_message_unset (&message);
    return GST_FLOW_UNEXPECTED;
  }
server_eof:
  {
    GST_DEBUG_OBJECT (src, "we got an eof from the server");
    GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
        ("The server closed the connection."));
    src->connected = FALSE;
    gst_rtsp_message_unset (&message);
    return GST_FLOW_UNEXPECTED;
  }
interrupt:
  {
    gst_rtsp_message_unset (&message);
    GST_DEBUG_OBJECT (src, "got interrupted: stop connection flush");
    /* unset flushing so we can do something else */
    gst_rtsp_connection_flush (src->connection, FALSE);
    return GST_FLOW_WRONG_STATE;
  }
already_paused:
  {
    GST_DEBUG_OBJECT (src, "got interrupted: playback already paused");
    return GST_FLOW_WRONG_STATE;
  }
receive_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not receive message. (%s)", str));
    g_free (str);

    gst_rtsp_message_unset (&message);
    return GST_FLOW_ERROR;
  }
handle_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not handle server message. (%s)", str));
    g_free (str);
    gst_rtsp_message_unset (&message);
    return GST_FLOW_ERROR;
  }
invalid_length:
  {
    GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
        ("Short message received, ignoring."));
    gst_rtsp_message_unset (&message);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_rtspsrc_loop_udp (GstRTSPSrc * src)
{
  gboolean restart = FALSE;
  GstRTSPResult res;
  GstRTSPMessage message = { 0 };

  GST_OBJECT_LOCK (src);
  if (src->loop_cmd == CMD_STOP)
    goto stopping;

  while (src->loop_cmd == CMD_WAIT) {
    GST_OBJECT_UNLOCK (src);

    while (TRUE) {
      GTimeVal tv_timeout;

      /* get the next timeout interval */
      gst_rtsp_connection_next_timeout (src->connection, &tv_timeout);

      GST_DEBUG_OBJECT (src, "doing receive with timeout %d seconds",
          (gint) tv_timeout.tv_sec);

      /* we should continue reading the TCP socket because the server might
       * send us requests. When the session timeout expires, we need to send a
       * keep-alive request to keep the session open. */
      res = gst_rtspsrc_connection_receive (src, &message, &tv_timeout);

      switch (res) {
        case GST_RTSP_OK:
          GST_DEBUG_OBJECT (src, "we received a server message");
          break;
        case GST_RTSP_EINTR:
          /* we got interrupted, see what we have to do */
          GST_DEBUG_OBJECT (src, "got interrupted: stop connection flush");
          /* unset flushing so we can do something else */
          gst_rtsp_connection_flush (src->connection, FALSE);
          goto interrupt;
        case GST_RTSP_ETIMEOUT:
          /* send keep-alive, ignore the result, a warning will be posted. */
          GST_DEBUG_OBJECT (src, "timout, sending keep-alive");
          gst_rtspsrc_send_keep_alive (src);
          continue;
        case GST_RTSP_EEOF:
          /* server closed the connection. not very fatal for UDP, reconnect and
           * see what happens. */
          GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
              ("The server closed the connection."));
          gst_rtsp_connection_close (src->connection);
          res =
              gst_rtsp_connection_connect (src->connection, src->ptcp_timeout);
          if (res < 0)
            goto connect_error;
          src->connected = TRUE;
          continue;
        default:
          goto receive_error;
      }

      switch (message.type) {
        case GST_RTSP_MESSAGE_REQUEST:
          /* server sends us a request message, handle it */
          res = gst_rtspsrc_handle_request (src, &message);
          if (res == GST_RTSP_EEOF)
            goto server_eof;
          else if (res < 0)
            goto handle_request_failed;
          break;
        case GST_RTSP_MESSAGE_RESPONSE:
          /* we ignore response and data messages */
          GST_DEBUG_OBJECT (src, "ignoring response message");
          if (src->debug)
            gst_rtsp_message_dump (&message);
          break;
        case GST_RTSP_MESSAGE_DATA:
          /* we ignore response and data messages */
          GST_DEBUG_OBJECT (src, "ignoring data message");
          break;
        default:
          GST_WARNING_OBJECT (src, "ignoring unknown message type %d",
              message.type);
          break;
      }
    }
  interrupt:
    GST_OBJECT_LOCK (src);
    GST_DEBUG_OBJECT (src, "we have command %d", src->loop_cmd);
    if (src->loop_cmd == CMD_STOP)
      goto stopping;
  }
  if (src->loop_cmd == CMD_RECONNECT) {
    /* when we get here we have to reconnect using tcp */
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
          gst_guint64_to_gdouble (src->udp_timeout / 1000000.0)));
  /* we can try only TCP now */
  src->cur_protocols = GST_RTSP_LOWER_TRANS_TCP;

  /* pause to prepare for a restart */
  gst_rtspsrc_pause (src);

  if (src->task) {
    /* stop task, we cannot join as this would deadlock, the task will stop when
     * we exit this function below. */
    gst_task_stop (src->task);
    /* and free the task so that _close will not stop/join it again. */
    gst_object_unref (GST_OBJECT (src->task));
    src->task = NULL;
  }
  /* close and cleanup our state */
  gst_rtspsrc_close (src);

  /* see if we have TCP left to try */
  if (!(src->protocols & GST_RTSP_LOWER_TRANS_TCP))
    goto no_protocols;

  /* open new connection using tcp */
  if (!gst_rtspsrc_open (src))
    goto open_failed;

  /* start playback */
  if (!gst_rtspsrc_play (src, &src->segment))
    goto play_failed;

done:
  return GST_FLOW_OK;

  /* ERRORS */
stopping:
  {
    GST_DEBUG_OBJECT (src, "we are stopping");
    GST_OBJECT_UNLOCK (src);
    return GST_FLOW_WRONG_STATE;
  }
receive_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not receive message. (%s)", str));
    g_free (str);
    return GST_FLOW_ERROR;
  }
handle_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not handle server message. (%s)", str));
    g_free (str);
    gst_rtsp_message_unset (&message);
    return GST_FLOW_ERROR;
  }
connect_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    src->connected = FALSE;
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Could not connect to server. (%s)", str));
    g_free (str);
    return GST_FLOW_ERROR;
  }
no_protocols:
  {
    src->cur_protocols = 0;
    /* no transport possible, post an error and stop */
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Could not connect to server, no protocols left"));
    return GST_FLOW_ERROR;
  }
open_failed:
  {
    GST_DEBUG_OBJECT (src, "open failed");
    return GST_FLOW_OK;
  }
play_failed:
  {
    GST_DEBUG_OBJECT (src, "play failed");
    return GST_FLOW_OK;
  }
server_eof:
  {
    GST_DEBUG_OBJECT (src, "we got an eof from the server");
    GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
        ("The server closed the connection."));
    src->connected = FALSE;
    gst_rtsp_message_unset (&message);
    return GST_FLOW_UNEXPECTED;
  }
}

static void
gst_rtspsrc_loop_send_cmd (GstRTSPSrc * src, gint cmd, gboolean flush)
{
  GST_OBJECT_LOCK (src);
  src->loop_cmd = cmd;
  if (flush) {
    GST_DEBUG_OBJECT (src, "start connection flush");
    if (src->connection)
      gst_rtsp_connection_flush (src->connection, TRUE);
  } else {
    GST_DEBUG_OBJECT (src, "stop connection flush");
    if (src->connection)
      gst_rtsp_connection_flush (src->connection, FALSE);
  }
  GST_OBJECT_UNLOCK (src);
}

static void
gst_rtspsrc_loop (GstRTSPSrc * src)
{
  GstFlowReturn ret;

  if (src->interleaved)
    ret = gst_rtspsrc_loop_interleaved (src);
  else
    ret = gst_rtspsrc_loop_udp (src);

  if (ret != GST_FLOW_OK)
    goto pause;

  return;

  /* ERRORS */
pause:
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
        /* for fatal errors we post an error message, post the error before the
         * EOS so the app knows about the error first. */
        GST_ELEMENT_ERROR (src, STREAM, FAILED,
            ("Internal data flow error."),
            ("streaming task paused, reason %s (%d)", reason, ret));
        gst_rtspsrc_push_event (src, gst_event_new_eos ());
      }
    }
    return;
  }
}

#ifndef GST_DISABLE_GST_DEBUG
const gchar *
gst_rtsp_auth_method_to_string (GstRTSPAuthMethod method)
{
  gint index = 0;

  while (method != 0) {
    index++;
    method >>= 1;
  }
  switch (index) {
    case 0:
      return "None";
    case 1:
      return "Basic";
    case 2:
      return "Digest";
  }

  return "Unknown";
}
#endif

static const gchar *
gst_rtspsrc_skip_lws (const gchar * s)
{
  while (g_ascii_isspace (*s))
    s++;
  return s;
}

static const gchar *
gst_rtspsrc_unskip_lws (const gchar * s, const gchar * start)
{
  while (s > start && g_ascii_isspace (*(s - 1)))
    s--;
  return s;
}

static const gchar *
gst_rtspsrc_skip_commas (const gchar * s)
{
  /* The grammar allows for multiple commas */
  while (g_ascii_isspace (*s) || *s == ',')
    s++;
  return s;
}

static const gchar *
gst_rtspsrc_skip_item (const gchar * s)
{
  gboolean quoted = FALSE;
  const gchar *start = s;

  /* A list item ends at the last non-whitespace character
   * before a comma which is not inside a quoted-string. Or at
   * the end of the string.
   */
  while (*s) {
    if (*s == '"')
      quoted = !quoted;
    else if (quoted) {
      if (*s == '\\' && *(s + 1))
        s++;
    } else {
      if (*s == ',')
        break;
    }
    s++;
  }

  return gst_rtspsrc_unskip_lws (s, start);
}

static void
gst_rtsp_decode_quoted_string (gchar * quoted_string)
{
  gchar *src, *dst;

  src = quoted_string + 1;
  dst = quoted_string;
  while (*src && *src != '"') {
    if (*src == '\\' && *(src + 1))
      src++;
    *dst++ = *src++;
  }
  *dst = '\0';
}

/* Extract the authentication tokens that the server provided for each method
 * into an array of structures and give those to the connection object.
 */
static void
gst_rtspsrc_parse_digest_challenge (GstRTSPConnection * conn,
    const gchar * header)
{
  GSList *list = NULL, *iter;
  const gchar *end;
  gchar *item, *eq, *name_end, *value;

  gst_rtsp_connection_clear_auth_params (conn);

  /* Parse a header whose content is described by RFC2616 as
   * "#something", where "something" does not itself contain commas,
   * except as part of quoted-strings, into a list of allocated strings.
   */
  header = gst_rtspsrc_skip_commas (header);
  while (*header) {
    end = gst_rtspsrc_skip_item (header);
    list = g_slist_prepend (list, g_strndup (header, end - header));
    header = gst_rtspsrc_skip_commas (end);
  }
  if (!list)
    return;

  list = g_slist_reverse (list);
  for (iter = list; iter; iter = iter->next) {
    item = iter->data;

    eq = strchr (item, '=');
    if (eq) {
      name_end = (gchar *) gst_rtspsrc_unskip_lws (eq, item);
      if (name_end == item) {
        /* That's no good... */
        g_free (item);
        continue;
      }

      *name_end = '\0';

      value = (gchar *) gst_rtspsrc_skip_lws (eq + 1);
      if (*value == '"')
        gst_rtsp_decode_quoted_string (value);
    } else
      value = NULL;

    gst_rtsp_connection_set_auth_param (conn, item, value);
    g_free (item);
  }

  g_slist_free (list);
}

/* Parse a WWW-Authenticate Response header and determine the 
 * available authentication methods
 *
 * This code should also cope with the fact that each WWW-Authenticate
 * header can contain multiple challenge methods + tokens 
 *
 * At the moment, for Basic auth, we just do a minimal check and don't
 * even parse out the realm */
static void
gst_rtspsrc_parse_auth_hdr (gchar * hdr, GstRTSPAuthMethod * methods,
    GstRTSPConnection * conn)
{
  gchar *start;

  g_return_if_fail (hdr != NULL);
  g_return_if_fail (methods != NULL);

  /* Skip whitespace at the start of the string */
  for (start = hdr; start[0] != '\0' && g_ascii_isspace (start[0]); start++);

  if (g_ascii_strncasecmp (start, "basic", 5) == 0)
    *methods |= GST_RTSP_AUTH_BASIC;
  else if (g_ascii_strncasecmp (start, "digest ", 7) == 0) {
    *methods |= GST_RTSP_AUTH_DIGEST;
    gst_rtspsrc_parse_digest_challenge (conn, &start[7]);
  }
}

/**
 * gst_rtspsrc_setup_auth:
 * @src: the rtsp source
 *
 * Configure a username and password and auth method on the 
 * connection object based on a response we received from the 
 * peer.
 *
 * Currently, this requires that a username and password were supplied
 * in the uri. In the future, they may be requested on demand by sending
 * a message up the bus.
 *
 * Returns: TRUE if authentication information could be set up correctly.
 */
static gboolean
gst_rtspsrc_setup_auth (GstRTSPSrc * src, GstRTSPMessage * response)
{
  gchar *user = NULL;
  gchar *pass = NULL;
  GstRTSPAuthMethod avail_methods = GST_RTSP_AUTH_NONE;
  GstRTSPAuthMethod method;
  GstRTSPResult auth_result;
  GstRTSPUrl *url;
  gchar *hdr;

  /* Identify the available auth methods and see if any are supported */
  if (gst_rtsp_message_get_header (response, GST_RTSP_HDR_WWW_AUTHENTICATE,
          &hdr, 0) == GST_RTSP_OK) {
    gst_rtspsrc_parse_auth_hdr (hdr, &avail_methods, src->connection);
  }

  if (avail_methods == GST_RTSP_AUTH_NONE)
    goto no_auth_available;

  /* FIXME: For digest auth, if the response indicates that the session
   * data are stale, we just update them in the connection object and
   * return TRUE to retry the request */

  url = gst_rtsp_connection_get_url (src->connection);

  /* Do we have username and password available? */
  if (url != NULL && !src->tried_url_auth && url->user != NULL
      && url->passwd != NULL) {
    user = url->user;
    pass = url->passwd;
    src->tried_url_auth = TRUE;
    GST_DEBUG_OBJECT (src,
        "Attempting authentication using credentials from the URL");
  } else {
    user = src->user_id;
    pass = src->user_pw;
    GST_DEBUG_OBJECT (src,
        "Attempting authentication using credentials from the properties");
  }

  /* FIXME: If the url didn't contain username and password or we tried them
   * already, request a username and passwd from the application via some kind
   * of credentials request message */

  /* If we don't have a username and passwd at this point, bail out. */
  if (user == NULL || pass == NULL)
    goto no_user_pass;

  /* Try to configure for each available authentication method, strongest to
   * weakest */
  for (method = GST_RTSP_AUTH_MAX; method != GST_RTSP_AUTH_NONE; method >>= 1) {
    /* Check if this method is available on the server */
    if ((method & avail_methods) == 0)
      continue;

    /* Pass the credentials to the connection to try on the next request */
    auth_result =
        gst_rtsp_connection_set_auth (src->connection, method, user, pass);
    /* INVAL indicates an invalid username/passwd were supplied, so we'll just
     * ignore it and end up retrying later */
    if (auth_result == GST_RTSP_OK || auth_result == GST_RTSP_EINVAL) {
      GST_DEBUG_OBJECT (src, "Attempting %s authentication",
          gst_rtsp_auth_method_to_string (method));
      break;
    }
  }

  if (method == GST_RTSP_AUTH_NONE)
    goto no_auth_available;

  return TRUE;

no_auth_available:
  {
    /* Output an error indicating that we couldn't connect because there were
     * no supported authentication protocols */
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
        ("No supported authentication protocol was found"));
    return FALSE;
  }
no_user_pass:
  {
    /* We don't fire an error message, we just return FALSE and let the
     * normal NOT_AUTHORIZED error be propagated */
    return FALSE;
  }
}

static GstRTSPResult
gst_rtspsrc_try_send (GstRTSPSrc * src, GstRTSPMessage * request,
    GstRTSPMessage * response, GstRTSPStatusCode * code)
{
  GstRTSPResult res;
  GstRTSPStatusCode thecode;
  gchar *content_base = NULL;
  gint try = 0;

again:
  gst_rtsp_ext_list_before_send (src->extensions, request);

  GST_DEBUG_OBJECT (src, "sending message");

  if (src->debug)
    gst_rtsp_message_dump (request);

  res = gst_rtspsrc_connection_send (src, request, src->ptcp_timeout);
  if (res < 0)
    goto send_error;

  gst_rtsp_connection_reset_timeout (src->connection);

next:
  res = gst_rtspsrc_connection_receive (src, response, src->ptcp_timeout);
  if (res < 0)
    goto receive_error;

  if (src->debug)
    gst_rtsp_message_dump (response);

  switch (response->type) {
    case GST_RTSP_MESSAGE_REQUEST:
      res = gst_rtspsrc_handle_request (src, response);
      if (res == GST_RTSP_EEOF)
        goto server_eof;
      else if (res < 0)
        goto handle_request_failed;
      goto next;
    case GST_RTSP_MESSAGE_RESPONSE:
      /* ok, a response is good */
      GST_DEBUG_OBJECT (src, "received response message");
      break;
    default:
    case GST_RTSP_MESSAGE_DATA:
      /* get next response */
      GST_DEBUG_OBJECT (src, "ignoring data response message");
      goto next;
  }

  thecode = response->type_data.response.code;

  GST_DEBUG_OBJECT (src, "got response message %d", thecode);

  /* if the caller wanted the result code, we store it. */
  if (code)
    *code = thecode;

  /* If the request didn't succeed, bail out before doing any more */
  if (thecode != GST_RTSP_STS_OK)
    return GST_RTSP_OK;

  /* store new content base if any */
  gst_rtsp_message_get_header (response, GST_RTSP_HDR_CONTENT_BASE,
      &content_base, 0);
  if (content_base) {
    g_free (src->content_base);
    src->content_base = g_strdup (content_base);
  }
  gst_rtsp_ext_list_after_send (src->extensions, request, response);

  return GST_RTSP_OK;

  /* ERRORS */
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message. (%s)", str));
    g_free (str);
    return res;
  }
receive_error:
  {

    switch (res) {
      case GST_RTSP_EEOF:
        GST_WARNING_OBJECT (src, "server closed connection, doing reconnect");
        if (try == 0) {
          gst_rtsp_connection_close (src->connection);
          try++;
          /* if reconnect succeeds, try again */
          if ((res =
                  gst_rtsp_connection_connect (src->connection,
                      src->ptcp_timeout)) == 0)
            goto again;

          src->connected = FALSE;
        }
        /* only try once after reconnect, then fallthrough and error out */
      default:
      {
        gchar *str = gst_rtsp_strresult (res);

        GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
            ("Could not receive message. (%s)", str));
        g_free (str);
        break;
      }
    }
    return res;
  }
handle_request_failed:
  {
    /* ERROR was posted */
    gst_rtsp_message_unset (response);
    return res;
  }
server_eof:
  {
    GST_DEBUG_OBJECT (src, "we got an eof from the server");
    GST_ELEMENT_WARNING (src, RESOURCE, READ, (NULL),
        ("The server closed the connection."));
    gst_rtsp_message_unset (response);
    return GST_FLOW_UNEXPECTED;
  }
}

/**
 * gst_rtspsrc_send:
 * @src: the rtsp source
 * @request: must point to a valid request
 * @response: must point to an empty #GstRTSPMessage
 * @code: an optional code result
 *
 * send @request and retrieve the response in @response. optionally @code can be
 * non-NULL in which case it will contain the status code of the response.
 *
 * If This function returns #GST_RTSP_OK, @response will contain a valid response
 * message that should be cleaned with gst_rtsp_message_unset() after usage. 
 *
 * If @code is NULL, this function will return #GST_RTSP_ERROR (with an invalid
 * @response message) if the response code was not 200 (OK).
 *
 * If the attempt results in an authentication failure, then this will attempt
 * to retrieve authentication credentials via gst_rtspsrc_setup_auth and retry
 * the request.
 *
 * Returns: #GST_RTSP_OK if the processing was successful.
 */
static GstRTSPResult
gst_rtspsrc_send (GstRTSPSrc * src, GstRTSPMessage * request,
    GstRTSPMessage * response, GstRTSPStatusCode * code)
{
  GstRTSPStatusCode int_code = GST_RTSP_STS_OK;
  GstRTSPResult res = GST_RTSP_ERROR;
  gint count;
  gboolean retry;
  GstRTSPMethod method = GST_RTSP_INVALID;

  count = 0;
  do {
    retry = FALSE;

    /* make sure we don't loop forever */
    if (count++ > 8)
      break;

    /* save method so we can disable it when the server complains */
    method = request->type_data.request.method;

    if ((res = gst_rtspsrc_try_send (src, request, response, &int_code)) < 0)
      goto error;

    switch (int_code) {
      case GST_RTSP_STS_UNAUTHORIZED:
        if (gst_rtspsrc_setup_auth (src, response)) {
          /* Try the request/response again after configuring the auth info
           * and loop again */
          retry = TRUE;
        }
        break;
      default:
        break;
    }
  } while (retry == TRUE);

  /* If the user requested the code, let them handle errors, otherwise
   * post an error below */
  if (code != NULL)
    *code = int_code;
  else if (int_code != GST_RTSP_STS_OK)
    goto error_response;

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (src, "got error %d", res);
    return res;
  }
error_response:
  {
    res = GST_RTSP_ERROR;

    switch (response->type_data.response.code) {
      case GST_RTSP_STS_NOT_FOUND:
        GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL), ("%s",
                response->type_data.response.reason));
        break;
      case GST_RTSP_STS_MOVED_PERMANENTLY:
      case GST_RTSP_STS_MOVE_TEMPORARILY:
      {
        gchar *new_location;
        GstRTSPLowerTrans transports;

        GST_DEBUG_OBJECT (src, "got redirection");
        /* if we don't have a Location Header, we must error */
        if (gst_rtsp_message_get_header (response, GST_RTSP_HDR_LOCATION,
                &new_location, 0) < 0)
          break;

        /* When we receive a redirect result, we go back to the INIT state after
         * parsing the new URI. The caller should do the needed steps to issue
         * a new setup when it detects this state change. */
        GST_DEBUG_OBJECT (src, "redirection to %s", new_location);

        /* save current transports */
        if (src->url)
          transports = src->url->transports;
        else
          transports = GST_RTSP_LOWER_TRANS_UNKNOWN;

        gst_rtspsrc_uri_set_uri (GST_URI_HANDLER (src), new_location);

        /* set old transports */
        if (src->url && transports != GST_RTSP_LOWER_TRANS_UNKNOWN)
          src->url->transports = transports;

        src->need_redirect = TRUE;
        src->state = GST_RTSP_STATE_INIT;
        res = GST_RTSP_OK;
        break;
      }
      case GST_RTSP_STS_NOT_ACCEPTABLE:
      case GST_RTSP_STS_NOT_IMPLEMENTED:
      case GST_RTSP_STS_METHOD_NOT_ALLOWED:
        GST_WARNING_OBJECT (src, "got NOT IMPLEMENTED, disable method %s",
            gst_rtsp_method_as_text (method));
        src->methods &= ~method;
        res = GST_RTSP_OK;
        break;
      default:
        GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
            ("Got error response: %d (%s).", response->type_data.response.code,
                response->type_data.response.reason));
        break;
    }
    /* if we return ERROR we should unset the response ourselves */
    if (res == GST_RTSP_ERROR)
      gst_rtsp_message_unset (response);

    return res;
  }
}

static GstRTSPResult
gst_rtspsrc_send_cb (GstRTSPExtension * ext, GstRTSPMessage * request,
    GstRTSPMessage * response, GstRTSPSrc * src)
{
  return gst_rtspsrc_send (src, request, response, NULL);
}


/* parse the response and collect all the supported methods. We need this
 * information so that we don't try to send an unsupported request to the
 * server.
 */
static gboolean
gst_rtspsrc_parse_methods (GstRTSPSrc * src, GstRTSPMessage * response)
{
  GstRTSPHeaderField field;
  gchar *respoptions;
  gchar **options;
  gint indx = 0;
  gint i;

  /* reset supported methods */
  src->methods = 0;

  /* Try Allow Header first */
  field = GST_RTSP_HDR_ALLOW;
  while (TRUE) {
    respoptions = NULL;
    gst_rtsp_message_get_header (response, field, &respoptions, indx);
    if (indx == 0 && !respoptions) {
      /* if no Allow header was found then try the Public header... */
      field = GST_RTSP_HDR_PUBLIC;
      gst_rtsp_message_get_header (response, field, &respoptions, indx);
    }
    if (!respoptions)
      break;

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
      method = gst_rtsp_find_method (stripped);

      /* keep bitfield of supported methods */
      if (method != GST_RTSP_INVALID)
        src->methods |= method;
    }
    g_strfreev (options);

    indx++;
  }

  if (src->methods == 0) {
    /* neither Allow nor Public are required, assume the server supports
     * at least DESCRIBE, SETUP, we always assume it supports PLAY as
     * well. */
    GST_DEBUG_OBJECT (src, "could not get OPTIONS");
    src->methods = GST_RTSP_DESCRIBE | GST_RTSP_SETUP;
  }
  /* always assume PLAY, FIXME, extensions should be able to override
   * this */
  src->methods |= GST_RTSP_PLAY;

  /* we need describe and setup */
  if (!(src->methods & GST_RTSP_DESCRIBE))
    goto no_describe;
  if (!(src->methods & GST_RTSP_SETUP))
    goto no_setup;

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

/* masks to be kept in sync with the hardcoded protocol order of preference
 * in code below */
static guint protocol_masks[] = {
  GST_RTSP_LOWER_TRANS_UDP,
  GST_RTSP_LOWER_TRANS_UDP_MCAST,
  GST_RTSP_LOWER_TRANS_TCP,
  0
};

static GstRTSPResult
gst_rtspsrc_create_transports_string (GstRTSPSrc * src,
    GstRTSPLowerTrans protocols, gchar ** transports)
{
  GstRTSPResult res;
  GString *result;
  gboolean add_udp_str;

  *transports = NULL;

  res =
      gst_rtsp_ext_list_get_transports (src->extensions, protocols, transports);

  if (res < 0)
    goto failed;

  GST_DEBUG_OBJECT (src, "got transports %s", GST_STR_NULL (*transports));

  /* extension listed transports, use those */
  if (*transports != NULL)
    return GST_RTSP_OK;

  /* it's the default but some servers need it */
  add_udp_str = TRUE;

  /* the default RTSP transports */
  result = g_string_new ("");
  if (protocols & GST_RTSP_LOWER_TRANS_UDP) {
    GST_DEBUG_OBJECT (src, "adding UDP unicast");

    g_string_append (result, "RTP/AVP");
    if (add_udp_str)
      g_string_append (result, "/UDP");
    g_string_append (result, ";unicast;client_port=%%u1-%%u2");
  } else if (protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST) {
    GST_DEBUG_OBJECT (src, "adding UDP multicast");

    /* we don't have to allocate any UDP ports yet, if the selected transport
     * turns out to be multicast we can create them and join the multicast
     * group indicated in the transport reply */
    if (result->len > 0)
      g_string_append (result, ",");
    g_string_append (result, "RTP/AVP");
    if (add_udp_str)
      g_string_append (result, "/UDP");
    g_string_append (result, ";multicast");
  } else if (protocols & GST_RTSP_LOWER_TRANS_TCP) {
    GST_DEBUG_OBJECT (src, "adding TCP");

    if (result->len > 0)
      g_string_append (result, ",");
    g_string_append (result, "RTP/AVP/TCP;unicast;interleaved=%%i1-%%i2");
  }
  *transports = g_string_free (result, FALSE);

  GST_DEBUG_OBJECT (src, "prepared transports %s", GST_STR_NULL (*transports));

  return GST_RTSP_OK;

  /* ERRORS */
failed:
  {
    return res;
  }
}

static GstRTSPResult
gst_rtspsrc_prepare_transports (GstRTSPStream * stream, gchar ** transports,
    gint orig_rtpport, gint orig_rtcpport)
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
    if (!orig_rtpport || !orig_rtcpport) {
      if (!gst_rtspsrc_alloc_udp_ports (stream, &rtpport, &rtcpport))
        goto failed;
    } else {
      rtpport = orig_rtpport;
      rtcpport = orig_rtcpport;
    }
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
  /* append final part */
  g_string_append (str, p);

  g_free (*transports);
  *transports = g_string_free (str, FALSE);

done:
  return GST_RTSP_OK;

  /* ERRORS */
failed:
  {
    return GST_RTSP_ERROR;
  }
}

static gboolean
gst_rtspsrc_stream_is_real_media (GstRTSPStream * stream)
{
  gboolean res = FALSE;

  if (stream->caps) {
    GstStructure *s;
    const gchar *enc = NULL;

    s = gst_caps_get_structure (stream->caps, 0);
    if ((enc = gst_structure_get_string (s, "encoding-name"))) {
      res = (strstr (enc, "-REAL") != NULL);
    }
  }
  return res;
}

/* Perform the SETUP request for all the streams. 
 *
 * We ask the server for a specific transport, which initially includes all the
 * ones we can support (UDP/TCP/MULTICAST). For the UDP transport we allocate
 * two local UDP ports that we send to the server.
 *
 * Once the server replied with a transport, we configure the other streams
 * with the same transport.
 *
 * This function will also configure the stream for the selected transport,
 * which basically means creating the pipeline.
 */
static gboolean
gst_rtspsrc_setup_streams (GstRTSPSrc * src)
{
  GList *walk;
  GstRTSPResult res;
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GstRTSPStream *stream = NULL;
  GstRTSPLowerTrans protocols;
  GstRTSPStatusCode code;
  gboolean unsupported_real = FALSE;
  gint rtpport, rtcpport;
  GstRTSPUrl *url;
  gchar *hval;

  url = gst_rtsp_connection_get_url (src->connection);

  /* we initially allow all configured lower transports. based on the URL
   * transports and the replies from the server we narrow them down. */
  protocols = url->transports & src->cur_protocols;

  if (protocols == 0)
    goto no_protocols;

  /* reset some state */
  src->free_channel = 0;
  src->interleaved = FALSE;
  src->need_activate = FALSE;
  rtpport = rtcpport = 0;

  for (walk = src->streams; walk; walk = g_list_next (walk)) {
    gchar *transports;
    gint retry = 0;
    guint mask = 0;

    stream = (GstRTSPStream *) walk->data;

    /* see if we need to configure this stream */
    if (!gst_rtsp_ext_list_configure_stream (src->extensions, stream->caps)) {
      GST_DEBUG_OBJECT (src, "skipping stream %p, disabled by extension",
          stream);
      stream->disabled = TRUE;
      continue;
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

  next_protocol:
    /* first selectable protocol */
    while (protocol_masks[mask] && !(protocols & protocol_masks[mask]))
      mask++;
    if (!protocol_masks[mask])
      goto no_protocols;

  retry:
    GST_DEBUG_OBJECT (src, "protocols = 0x%x, protocol mask = 0x%x", protocols,
        protocol_masks[mask]);
    /* create a string with first transport in line */
    transports = NULL;
    res = gst_rtspsrc_create_transports_string (src,
        protocols & protocol_masks[mask], &transports);
    if (res < 0 || transports == NULL)
      goto setup_transport_failed;

    if (strlen (transports) == 0) {
      g_free (transports);
      GST_DEBUG_OBJECT (src, "no transports found");
      mask++;
      goto next_protocol;
    }

    GST_DEBUG_OBJECT (src, "replace ports in %s", GST_STR_NULL (transports));

    /* replace placeholders with real values, this function will optionally
     * allocate UDP ports and other info needed to execute the setup request */
    res = gst_rtspsrc_prepare_transports (stream, &transports,
        retry > 0 ? rtpport : 0, retry > 0 ? rtcpport : 0);
    if (res < 0) {
      g_free (transports);
      goto setup_transport_failed;
    }

    GST_DEBUG_OBJECT (src, "transport is now %s", GST_STR_NULL (transports));

    /* create SETUP request */
    res =
        gst_rtsp_message_init_request (&request, GST_RTSP_SETUP,
        stream->setup_url);
    if (res < 0) {
      g_free (transports);
      goto create_request_failed;
    }

    /* select transport, copy is made when adding to header so we can free it. */
    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_TRANSPORT, transports);
    g_free (transports);

    /* if the user wants a non default RTP packet size we add the blocksize
     * parameter */
    if (src->rtp_blocksize > 0) {
      hval = gst_rtspsrc_dup_printf ("%d", src->rtp_blocksize);
      gst_rtsp_message_add_header (&request, GST_RTSP_HDR_BLOCKSIZE, hval);
      g_free (hval);
    }

    /* handle the code ourselves */
    if ((res = gst_rtspsrc_send (src, &request, &response, &code) < 0))
      goto send_error;

    switch (code) {
      case GST_RTSP_STS_OK:
        break;
      case GST_RTSP_STS_UNSUPPORTED_TRANSPORT:
        gst_rtsp_message_unset (&request);
        gst_rtsp_message_unset (&response);
        /* cleanup of leftover transport */
        gst_rtspsrc_stream_free_udp (stream);
        /* MS WMServer RTSP MUST use same UDP pair in all SETUP requests;
         * we might be in this case */
        if (stream->container && rtpport && rtcpport && !retry) {
          GST_DEBUG_OBJECT (src, "retrying with original port pair %u-%u",
              rtpport, rtcpport);
          retry++;
          goto retry;
        }
        /* this transport did not go down well, but we may have others to try
         * that we did not send yet, try those and only give up then
         * but not without checking for lost cause/extension so we can
         * post a nicer/more useful error message later */
        if (!unsupported_real)
          unsupported_real = gst_rtspsrc_stream_is_real_media (stream);
        /* select next available protocol, give up on this stream if none */
        mask++;
        while (protocol_masks[mask] && !(protocols & protocol_masks[mask]))
          mask++;
        if (!protocol_masks[mask] || unsupported_real)
          continue;
        else
          goto retry;
      default:
        /* cleanup of leftover transport and move to the next stream */
        gst_rtspsrc_stream_free_udp (stream);
        goto response_error;
    }

    /* parse response transport */
    {
      gchar *resptrans = NULL;
      GstRTSPTransport transport = { 0 };

      gst_rtsp_message_get_header (&response, GST_RTSP_HDR_TRANSPORT,
          &resptrans, 0);
      if (!resptrans) {
        gst_rtspsrc_stream_free_udp (stream);
        goto no_transport;
      }

      /* parse transport, go to next stream on parse error */
      if (gst_rtsp_transport_parse (resptrans, &transport) != GST_RTSP_OK) {
        GST_WARNING_OBJECT (src, "failed to parse transport %s", resptrans);
        goto next;
      }

      /* update allowed transports for other streams. once the transport of
       * one stream has been determined, we make sure that all other streams
       * are configured in the same way */
      switch (transport.lower_transport) {
        case GST_RTSP_LOWER_TRANS_TCP:
          GST_DEBUG_OBJECT (src, "stream %p as TCP interleaved", stream);
          protocols = GST_RTSP_LOWER_TRANS_TCP;
          src->interleaved = TRUE;
          /* update free channels */
          src->free_channel =
              MAX (transport.interleaved.min, src->free_channel);
          src->free_channel =
              MAX (transport.interleaved.max, src->free_channel);
          src->free_channel++;
          break;
        case GST_RTSP_LOWER_TRANS_UDP_MCAST:
          /* only allow multicast for other streams */
          GST_DEBUG_OBJECT (src, "stream %p as UDP multicast", stream);
          protocols = GST_RTSP_LOWER_TRANS_UDP_MCAST;
          break;
        case GST_RTSP_LOWER_TRANS_UDP:
          /* only allow unicast for other streams */
          GST_DEBUG_OBJECT (src, "stream %p as UDP unicast", stream);
          protocols = GST_RTSP_LOWER_TRANS_UDP;
          break;
        default:
          GST_DEBUG_OBJECT (src, "stream %p unknown transport %d", stream,
              transport.lower_transport);
          break;
      }

      if (!stream->container || (!src->interleaved && !retry)) {
        /* now configure the stream with the selected transport */
        if (!gst_rtspsrc_stream_configure_transport (stream, &transport)) {
          GST_DEBUG_OBJECT (src,
              "could not configure stream %p transport, skipping stream",
              stream);
          goto next;
        } else if (stream->udpsrc[0] && stream->udpsrc[1]) {
          /* retain the first allocated UDP port pair */
          g_object_get (G_OBJECT (stream->udpsrc[0]), "port", &rtpport, NULL);
          g_object_get (G_OBJECT (stream->udpsrc[1]), "port", &rtcpport, NULL);
        }
      }
      /* we need to activate at least one streams when we detect activity */
      src->need_activate = TRUE;
    next:
      /* clean up our transport struct */
      gst_rtsp_transport_init (&transport);
      /* clean up used RTSP messages */
      gst_rtsp_message_unset (&request);
      gst_rtsp_message_unset (&response);
    }
  }

  gst_rtsp_ext_list_stream_select (src->extensions, url);

  /* if there is nothing to activate, error out */
  if (!src->need_activate)
    goto nothing_to_activate;

  return TRUE;

  /* ERRORS */
no_protocols:
  {
    /* no transport possible, post an error and stop */
    GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL),
        ("Could not connect to server, no protocols left"));
    return FALSE;
  }
create_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

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
response_error:
  {
    const gchar *str = gst_rtsp_status_as_text (code);

    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Error (%d): %s", code, GST_STR_NULL (str)));
    goto cleanup_error;
  }
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

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
nothing_to_activate:
  {
    /* none of the available error codes is really right .. */
    if (unsupported_real) {
      GST_ELEMENT_ERROR (src, STREAM, CODEC_NOT_FOUND,
          (_("No supported stream was found. You might need to install a "
                  "GStreamer RTSP extension plugin for Real media streams.")),
          (NULL));
    } else {
      GST_ELEMENT_ERROR (src, STREAM, CODEC_NOT_FOUND,
          (_("No supported stream was found. You might need to allow "
                  "more transport protocols or may otherwise be missing "
                  "the right GStreamer RTSP extension plugin.")), (NULL));
    }
    return FALSE;
  }
cleanup_error:
  {
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);
    return FALSE;
  }
}

static void
gst_rtspsrc_parse_range (GstRTSPSrc * src, const gchar * range,
    GstSegment * segment)
{
  gint64 seconds;
  GstRTSPTimeRange *therange;

  if (src->range)
    gst_rtsp_range_free (src->range);

  if (gst_rtsp_range_parse (range, &therange) == GST_RTSP_OK) {
    GST_DEBUG_OBJECT (src, "parsed range %s", range);
    src->range = therange;
  } else {
    GST_DEBUG_OBJECT (src, "failed to parse range %s", range);
    src->range = NULL;
    gst_segment_init (segment, GST_FORMAT_TIME);
    return;
  }

  GST_DEBUG_OBJECT (src, "range: type %d, min %f - type %d,  max %f ",
      therange->min.type, therange->min.seconds, therange->max.type,
      therange->max.seconds);

  if (therange->min.type == GST_RTSP_TIME_NOW)
    seconds = 0;
  else if (therange->min.type == GST_RTSP_TIME_END)
    seconds = 0;
  else
    seconds = therange->min.seconds * GST_SECOND;

  GST_DEBUG_OBJECT (src, "range: min %" GST_TIME_FORMAT,
      GST_TIME_ARGS (seconds));

  /* we need to start playback without clipping from the position reported by
   * the server */
  segment->start = seconds;
  segment->last_stop = seconds;

  if (therange->max.type == GST_RTSP_TIME_NOW)
    seconds = -1;
  else if (therange->max.type == GST_RTSP_TIME_END)
    seconds = -1;
  else
    seconds = therange->max.seconds * GST_SECOND;

  GST_DEBUG_OBJECT (src, "range: max %" GST_TIME_FORMAT,
      GST_TIME_ARGS (seconds));

  /* live (WMS) server might send overflowed large max as its idea of infinity,
   * compensate to prevent problems later on */
  if (seconds != -1 && seconds < 0) {
    seconds = -1;
    GST_DEBUG_OBJECT (src, "insane range, set to NONE");
  }

  /* live (WMS) might send min == max, which is not worth recording */
  if (segment->duration == -1 && seconds == segment->start)
    seconds = -1;

  /* don't change duration with unknown value, we might have a valid value
   * there that we want to keep. */
  if (seconds != -1)
    gst_segment_set_duration (segment, GST_FORMAT_TIME, seconds);
}

static gboolean
gst_rtspsrc_open (GstRTSPSrc * src)
{
  GstRTSPResult res;
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  guint8 *data;
  guint size;
  gint i, n_streams;
  GstSDPMessage sdp = { 0 };
  gchar *respcont = NULL;
  GstRTSPUrl *url;

  GST_RTSP_STATE_LOCK (src);

restart:
  /* reset our state */
  gst_segment_init (&src->segment, GST_FORMAT_TIME);
  src->need_range = TRUE;
  src->need_redirect = FALSE;
  src->skip = FALSE;

  /* can't continue without a valid url */
  if (G_UNLIKELY (src->url == NULL))
    goto no_url;
  src->tried_url_auth = FALSE;

  /* create connection */
  GST_DEBUG_OBJECT (src, "creating connection (%s)...", src->req_location);
  if ((res = gst_rtsp_connection_create (src->url, &src->connection)) < 0)
    goto could_not_create;

  url = gst_rtsp_connection_get_url (src->connection);

  if (url->transports & GST_RTSP_LOWER_TRANS_HTTP)
    gst_rtsp_connection_set_tunneled (src->connection, TRUE);

  if (src->proxy_host) {
    GST_DEBUG_OBJECT (src, "setting proxy %s:%d", src->proxy_host,
        src->proxy_port);
    gst_rtsp_connection_set_proxy (src->connection, src->proxy_host,
        src->proxy_port);
  }

  /* connect */
  GST_DEBUG_OBJECT (src, "connecting (%s)...", src->req_location);
  if ((res =
          gst_rtsp_connection_connect (src->connection, src->ptcp_timeout)) < 0)
    goto could_not_connect;

  src->connected = TRUE;

  /* create OPTIONS */
  GST_DEBUG_OBJECT (src, "create options...");
  res =
      gst_rtsp_message_init_request (&request, GST_RTSP_OPTIONS,
      src->req_location);
  if (res < 0)
    goto create_request_failed;

  /* send OPTIONS */
  GST_DEBUG_OBJECT (src, "send options...");
  if (gst_rtspsrc_send (src, &request, &response, NULL) < 0)
    goto send_error;

  /* parse OPTIONS */
  if (!gst_rtspsrc_parse_methods (src, &response))
    goto methods_error;

  /* create DESCRIBE */
  GST_DEBUG_OBJECT (src, "create describe...");
  res =
      gst_rtsp_message_init_request (&request, GST_RTSP_DESCRIBE,
      src->req_location);
  if (res < 0)
    goto create_request_failed;

  /* we only accept SDP for now */
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_ACCEPT,
      "application/sdp");

  /* prepare global stream caps properties */
  if (src->props)
    gst_structure_remove_all_fields (src->props);
  else
    src->props = gst_structure_empty_new ("RTSPProperties");

  /* send DESCRIBE */
  GST_DEBUG_OBJECT (src, "send describe...");
  if (gst_rtspsrc_send (src, &request, &response, NULL) < 0)
    goto send_error;

  /* we only perform redirect for the describe, currently */
  if (src->need_redirect) {
    /* close connection, we don't have to send a TEARDOWN yet, ignore the
     * result. */
    gst_rtsp_connection_close (src->connection);
    gst_rtsp_connection_free (src->connection);
    src->connection = NULL;
    src->connected = FALSE;

    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);

    /* and now retry */
    goto restart;
  }

  /* it could be that the DESCRIBE method was not implemented */
  if (!src->methods & GST_RTSP_DESCRIBE)
    goto no_describe;

  /* check if reply is SDP */
  gst_rtsp_message_get_header (&response, GST_RTSP_HDR_CONTENT_TYPE, &respcont,
      0);
  /* could not be set but since the request returned OK, we assume it
   * was SDP, else check it. */
  if (respcont) {
    if (!g_ascii_strcasecmp (respcont, "application/sdp") == 0)
      goto wrong_content_type;
  }

  /* get message body and parse as SDP */
  gst_rtsp_message_get_body (&response, &data, &size);

  if (data == NULL)
    goto no_describe;

  GST_DEBUG_OBJECT (src, "parse SDP...");
  gst_sdp_message_init (&sdp);
  gst_sdp_message_parse_buffer (data, size, &sdp);

  if (src->debug)
    gst_sdp_message_dump (&sdp);

  gst_rtsp_ext_list_parse_sdp (src->extensions, &sdp, src->props);

  /* parse range for duration reporting. */
  {
    const gchar *range;

    range = gst_sdp_message_get_attribute_val (&sdp, "range");
    if (range) {
      /* keep track of the range and configure it in the segment */
      gst_rtspsrc_parse_range (src, range, &src->segment);
    }
  }

  /* create streams */
  n_streams = gst_sdp_message_medias_len (&sdp);
  for (i = 0; i < n_streams; i++) {
    gst_rtspsrc_create_stream (src, &sdp, i);
  }

  src->state = GST_RTSP_STATE_INIT;

  /* setup streams */
  if (!gst_rtspsrc_setup_streams (src))
    goto setup_failed;

  src->state = GST_RTSP_STATE_READY;
  GST_RTSP_STATE_UNLOCK (src);

  /* clean up any messages */
  gst_rtsp_message_unset (&request);
  gst_rtsp_message_unset (&response);
  gst_sdp_message_uninit (&sdp);

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
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Could not create connection. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
could_not_connect:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Could not connect to server. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
create_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
send_error:
  {
    /* Don't post a message - the rtsp_send method will have
     * taken care of it because we passed NULL for the response code */
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
no_describe:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, SETTINGS, (NULL),
        ("Server can not provide an SDP."));
    goto cleanup_error;
  }
setup_failed:
  {
    gst_rtspsrc_close (src);
    /* error was posted */
    goto cleanup_error;
  }
cleanup_error:
  {
    if (src->connection) {
      GST_DEBUG_OBJECT (src, "free connection");
      gst_rtsp_connection_free (src->connection);
      src->connection = NULL;
      src->connected = FALSE;
    }
    GST_RTSP_STATE_UNLOCK (src);
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);
    gst_sdp_message_uninit (&sdp);
    return FALSE;
  }
}

#if 0
static gboolean
gst_rtspsrc_async_open (GstRTSPSrc * src)
{
  GError *error = NULL;
  gboolean res = TRUE;

  src->thread =
      g_thread_create ((GThreadFunc) gst_rtspsrc_open, src, TRUE, &error);
  if (error != NULL) {
    GST_ELEMENT_ERROR (src, RESOURCE, INIT, (NULL),
        ("Could not start async thread (%s).", error->message));
  }
  return res;
}
#endif

static gboolean
gst_rtspsrc_close (GstRTSPSrc * src)
{
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GstRTSPResult res;
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (src, "TEARDOWN...");

  GST_RTSP_STATE_LOCK (src);

  gst_rtspsrc_loop_send_cmd (src, CMD_STOP, TRUE);

  /* stop task if any */
  if (src->task) {
    /* release lock before trying to get the streamlock */
    GST_RTSP_STATE_UNLOCK (src);

    gst_task_stop (src->task);

    /* make sure it is not running */
    GST_RTSP_STREAM_LOCK (src);
    GST_RTSP_STREAM_UNLOCK (src);

    /* now wait for the task to finish */
    gst_task_join (src->task);

    /* and free the task */
    gst_object_unref (GST_OBJECT (src->task));
    src->task = NULL;

    GST_RTSP_STATE_LOCK (src);
  }

  if (!src->connection)
    goto done;

  GST_DEBUG_OBJECT (src, "stop connection flush");
  gst_rtsp_connection_flush (src->connection, FALSE);

  if (!src->connected) {
    GST_DEBUG_OBJECT (src, "not connected, doing cleanup");
    goto close;
  }
  if (src->state < GST_RTSP_STATE_READY) {
    GST_DEBUG_OBJECT (src, "not ready, doing cleanup");
    goto close;
  }

  if (src->methods & (GST_RTSP_PLAY | GST_RTSP_TEARDOWN)) {
    /* do TEARDOWN */
    res =
        gst_rtsp_message_init_request (&request, GST_RTSP_TEARDOWN,
        src->req_location);
    if (res < 0)
      goto create_request_failed;

    if (gst_rtspsrc_send (src, &request, &response, NULL) < 0)
      goto send_error;

    /* FIXME, parse result? */
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);
  } else {
    GST_DEBUG_OBJECT (src,
        "TEARDOWN and PLAY not supported, can't do TEARDOWN");
  }

close:
  /* close connection */
  GST_DEBUG_OBJECT (src, "closing connection...");
  gst_rtsp_connection_close (src->connection);
  /* free connection */
  gst_rtsp_connection_free (src->connection);
  src->connection = NULL;
  src->connected = FALSE;

done:
  /* cleanup */
  gst_rtspsrc_cleanup (src);

  src->state = GST_RTSP_STATE_INVALID;
  GST_RTSP_STATE_UNLOCK (src);

  return ret;

  /* ERRORS */
create_request_failed:
  {
    GST_RTSP_STATE_UNLOCK (src);
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request."));
    ret = FALSE;
    goto close;
  }
send_error:
  {
    GST_RTSP_STATE_UNLOCK (src);
    gst_rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message."));
    ret = FALSE;
    goto close;
  }
}

/* RTP-Info is of the format:
 *
 * url=<URL>;[seq=<seqbase>;rtptime=<timebase>] [, url=...]
 *
 * rtptime corresponds to the timestamp for the NPT time given in the header 
 * seqbase corresponds to the next sequence number we received. This number
 * indicates the first seqnum after the seek and should be used to discard
 * packets that are from before the seek.
 */
static gboolean
gst_rtspsrc_parse_rtpinfo (GstRTSPSrc * src, gchar * rtpinfo)
{
  gchar **infos;
  gint i, j;

  GST_DEBUG_OBJECT (src, "parsing RTP-Info %s", rtpinfo);

  infos = g_strsplit (rtpinfo, ",", 0);
  for (i = 0; infos[i]; i++) {
    gchar **fields;
    GstRTSPStream *stream;
    gint32 seqbase;
    gint64 timebase;

    GST_DEBUG_OBJECT (src, "parsing info %s", infos[i]);

    /* init values, types of seqbase and timebase are bigger than needed so we
     * can store -1 as uninitialized values */
    stream = NULL;
    seqbase = -1;
    timebase = -1;

    /* parse url, find stream for url.
     * parse seq and rtptime. The seq number should be configured in the rtp
     * depayloader or session manager to detect gaps. Same for the rtptime, it
     * should be used to create an initial time newsegment. */
    fields = g_strsplit (infos[i], ";", 0);
    for (j = 0; fields[j]; j++) {
      GST_DEBUG_OBJECT (src, "parsing field %s", fields[j]);
      /* remove leading whitespace */
      fields[j] = g_strchug (fields[j]);
      if (g_str_has_prefix (fields[j], "url=")) {
        /* get the url and the stream */
        stream =
            find_stream (src, (fields[j] + 4), (gpointer) find_stream_by_setup);
      } else if (g_str_has_prefix (fields[j], "seq=")) {
        seqbase = atoi (fields[j] + 4);
      } else if (g_str_has_prefix (fields[j], "rtptime=")) {
        timebase = g_ascii_strtoll (fields[j] + 8, NULL, 10);
      }
    }
    g_strfreev (fields);
    /* now we need to store the values for the caps of the stream */
    if (stream != NULL) {
      GST_DEBUG_OBJECT (src,
          "found stream %p, setting: seqbase %d, timebase %" G_GINT64_FORMAT,
          stream, seqbase, timebase);

      /* we have a stream, configure detected params */
      stream->seqbase = seqbase;
      stream->timebase = timebase;
    }
  }
  g_strfreev (infos);

  return TRUE;
}

#define USE_POSIX_LOCALE {                              \
  gchar *__old_locale = g_strdup (setlocale (LC_NUMERIC, NULL)); \
  setlocale (LC_NUMERIC, "POSIX");

#define RESTORE_LOCALE                                \
  setlocale (LC_NUMERIC, __old_locale);               \
  g_free (__old_locale);}

static gchar *
gst_rtspsrc_dup_printf (const gchar * format, ...)
{
  gchar *result;
  va_list varargs;

  USE_POSIX_LOCALE va_start (varargs, format);

  result = g_strdup_vprintf (format, varargs);
  va_end (varargs);
  RESTORE_LOCALE return result;
}

static gint
gst_rtspsrc_get_float (const char *str, gfloat * val)
{
  gint result;

  USE_POSIX_LOCALE result = sscanf (str, "%f", val);
  RESTORE_LOCALE return result;
}

static gchar *
gen_range_header (GstRTSPSrc * src, GstSegment * segment)
{
  gchar *res;

  if (src->range && src->range->min.type == GST_RTSP_TIME_NOW) {
    res = g_strdup_printf ("npt=now-");
  } else {
    if (segment->last_stop == 0)
      res = g_strdup_printf ("npt=0-");
    else
      res = gst_rtspsrc_dup_printf ("npt=%f-",
          ((gdouble) segment->last_stop) / GST_SECOND);
  }
  return res;
}

static gboolean
gst_rtspsrc_play (GstRTSPSrc * src, GstSegment * segment)
{
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GstRTSPResult res;
  gchar *hval;
  gfloat fval;
  gint hval_idx;

  GST_RTSP_STATE_LOCK (src);

  GST_DEBUG_OBJECT (src, "PLAY...");

  if (!(src->methods & GST_RTSP_PLAY))
    goto not_supported;

  if (src->state == GST_RTSP_STATE_PLAYING)
    goto was_playing;

  /* do play */
  res =
      gst_rtsp_message_init_request (&request, GST_RTSP_PLAY,
      src->req_location);
  if (res < 0)
    goto create_request_failed;

  if (src->need_range) {
    hval = gen_range_header (src, segment);

    gst_rtsp_message_add_header (&request, GST_RTSP_HDR_RANGE, hval);
    g_free (hval);
    src->need_range = FALSE;
  }

  if (segment->rate != 1.0) {
    hval = gst_rtspsrc_dup_printf ("%f", segment->rate);
    if (src->skip)
      gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SCALE, hval);
    else
      gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SPEED, hval);
    g_free (hval);
  }

  if (gst_rtspsrc_send (src, &request, &response, NULL) < 0)
    goto send_error;

  gst_rtsp_message_unset (&request);

  /* parse RTP npt field. This is the current position in the stream (Normal
   * Play Time) and should be put in the NEWSEGMENT position field. */
  if (gst_rtsp_message_get_header (&response, GST_RTSP_HDR_RANGE, &hval,
          0) == GST_RTSP_OK)
    gst_rtspsrc_parse_range (src, hval, segment);

  /* assume 1.0 rate now, overwrite when the SCALE or SPEED headers are present. */
  segment->rate = 1.0;

  /* parse Speed header. This is the intended playback rate of the stream
   * and should be put in the NEWSEGMENT rate field. */
  if (gst_rtsp_message_get_header (&response, GST_RTSP_HDR_SPEED, &hval,
          0) == GST_RTSP_OK) {
    if (gst_rtspsrc_get_float (hval, &fval) > 0)
      segment->rate = fval;
  } else if (gst_rtsp_message_get_header (&response, GST_RTSP_HDR_SCALE, &hval,
          0) == GST_RTSP_OK) {
    if (gst_rtspsrc_get_float (hval, &fval) > 0)
      segment->rate = fval;
  }

  /* parse the RTP-Info header field (if ANY) to get the base seqnum and timestamp
   * for the RTP packets. If this is not present, we assume all starts from 0... 
   * This is info for the RTP session manager that we pass to it in caps. */
  hval_idx = 0;
  while (gst_rtsp_message_get_header (&response, GST_RTSP_HDR_RTP_INFO,
          &hval, hval_idx++) == GST_RTSP_OK)
    gst_rtspsrc_parse_rtpinfo (src, hval);

  gst_rtsp_message_unset (&response);

  /* configure the caps of the streams after we parsed all headers. */
  gst_rtspsrc_configure_caps (src, segment);

  /* for interleaved transport, we receive the data on the RTSP connection
   * instead of UDP. We start a task to select and read from that connection.
   * For UDP we start the task as well to look for server info and UDP timeouts. */
  if (src->task == NULL) {
    src->task = gst_task_create ((GstTaskFunction) gst_rtspsrc_loop, src);
    gst_task_set_lock (src->task, GST_RTSP_STREAM_GET_LOCK (src));
  }
  src->running = TRUE;
  src->base_time = -1;
  src->state = GST_RTSP_STATE_PLAYING;
  gst_rtspsrc_loop_send_cmd (src, CMD_WAIT, FALSE);
  gst_task_start (src->task);

done:
  GST_RTSP_STATE_UNLOCK (src);

  return TRUE;

  /* ERRORS */
not_supported:
  {
    GST_DEBUG_OBJECT (src, "PLAY is not supported");
    goto done;
  }
was_playing:
  {
    GST_DEBUG_OBJECT (src, "we were already PLAYING");
    goto done;
  }
create_request_failed:
  {
    GST_RTSP_STATE_UNLOCK (src);
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request."));
    return FALSE;
  }
send_error:
  {
    GST_RTSP_STATE_UNLOCK (src);
    gst_rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message."));
    return FALSE;
  }
}

static gboolean
gst_rtspsrc_pause (GstRTSPSrc * src)
{
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };

  GST_RTSP_STATE_LOCK (src);

  GST_DEBUG_OBJECT (src, "PAUSE...");

  if (!(src->methods & GST_RTSP_PAUSE))
    goto not_supported;

  if (src->state == GST_RTSP_STATE_READY)
    goto was_paused;

  /* waiting for connection idle, we were flushing so any attempt at doing data
   * transfer will result in pausing the tasks. */
  GST_DEBUG_OBJECT (src, "wait for connection idle");
  GST_RTSP_CONN_LOCK (src);
  GST_DEBUG_OBJECT (src, "connection is idle now");
  GST_RTSP_CONN_UNLOCK (src);

  if (!src->connection || !src->connected)
    goto no_connection;

  GST_DEBUG_OBJECT (src, "stop connection flush");
  gst_rtsp_connection_flush (src->connection, FALSE);

  /* do pause */
  if (gst_rtsp_message_init_request (&request, GST_RTSP_PAUSE,
          src->req_location) < 0)
    goto create_request_failed;

  if (gst_rtspsrc_send (src, &request, &response, NULL) < 0)
    goto send_error;

  gst_rtsp_message_unset (&request);
  gst_rtsp_message_unset (&response);

no_connection:
  src->state = GST_RTSP_STATE_READY;

done:
  GST_RTSP_STATE_UNLOCK (src);

  return TRUE;

  /* ERRORS */
not_supported:
  {
    GST_DEBUG_OBJECT (src, "PAUSE is not supported");
    goto done;
  }
was_paused:
  {
    GST_DEBUG_OBJECT (src, "we were already PAUSED");
    goto done;
  }
create_request_failed:
  {
    GST_RTSP_STATE_UNLOCK (src);
    GST_ELEMENT_ERROR (src, LIBRARY, INIT, (NULL),
        ("Could not create request."));
    return FALSE;
  }
send_error:
  {
    GST_RTSP_STATE_UNLOCK (src);
    gst_rtsp_message_unset (&request);
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE, (NULL),
        ("Could not send message."));
    return FALSE;
  }
}

static void
gst_rtspsrc_handle_message (GstBin * bin, GstMessage * message)
{
  GstRTSPSrc *rtspsrc;

  rtspsrc = GST_RTSPSRC (bin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      gst_message_unref (message);
      break;
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s = gst_message_get_structure (message);

      if (gst_structure_has_name (s, "GstUDPSrcTimeout")) {
        gboolean ignore_timeout;

        GST_DEBUG_OBJECT (bin, "timeout on UDP port");

        GST_OBJECT_LOCK (rtspsrc);
        ignore_timeout = rtspsrc->ignore_timeout;
        rtspsrc->ignore_timeout = TRUE;
        GST_OBJECT_UNLOCK (rtspsrc);

        /* we only act on the first udp timeout message, others are irrelevant
         * and can be ignored. */
        if (!ignore_timeout)
          gst_rtspsrc_loop_send_cmd (rtspsrc, CMD_RECONNECT, TRUE);
        /* eat and free */
        gst_message_unref (message);
        return;
      }
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GstObject *udpsrc;
      GstRTSPStream *stream;
      GstFlowReturn ret;

      udpsrc = GST_MESSAGE_SRC (message);

      GST_DEBUG_OBJECT (rtspsrc, "got error from %s",
          GST_ELEMENT_NAME (udpsrc));

      stream = find_stream (rtspsrc, udpsrc, (gpointer) find_stream_by_udpsrc);
      if (!stream)
        goto forward;

      /* we ignore the RTCP udpsrc */
      if (stream->udpsrc[1] == GST_ELEMENT_CAST (udpsrc))
        goto done;

      /* if we get error messages from the udp sources, that's not a problem as
       * long as not all of them error out. We also don't really know what the
       * problem is, the message does not give enough detail... */
      ret = gst_rtspsrc_combine_flows (rtspsrc, stream, GST_FLOW_NOT_LINKED);
      GST_DEBUG_OBJECT (rtspsrc, "combined flows: %s", gst_flow_get_name (ret));
      if (ret != GST_FLOW_OK)
        goto forward;

    done:
      gst_message_unref (message);
      break;

    forward:
      /* fatal but not our message, forward */
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
      /* first attempt, don't ignore timeouts */
      rtspsrc->ignore_timeout = FALSE;
      if (!gst_rtspsrc_open (rtspsrc))
        goto open_failed;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_DEBUG_OBJECT (rtspsrc, "PAUSED->PLAYING: stop connection flush");
      gst_rtsp_connection_flush (rtspsrc->connection, FALSE);
      /* send some dummy packets before we chain up to the parent to activate
       * the receive in the udp sources */
      gst_rtspsrc_send_dummy_packets (rtspsrc);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (rtspsrc, "shutdown: sending stop command");
      gst_rtspsrc_loop_send_cmd (rtspsrc, CMD_STOP, TRUE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* chained up to parent so the udp sources are activated and receiving */
      gst_rtspsrc_play (rtspsrc, &rtspsrc->segment);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_rtspsrc_pause (rtspsrc);
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      ret = GST_STATE_CHANGE_NO_PREROLL;
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
    GST_DEBUG_OBJECT (rtspsrc, "open failed");
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
  static gchar *protocols[] = { "rtsp", "rtspu", "rtspt", "rtsph", NULL };

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
  GstRTSPResult res;
  GstRTSPUrl *newurl;

  src = GST_RTSPSRC (handler);

  /* same URI, we're fine */
  if (src->location && uri && !strcmp (uri, src->location))
    goto was_ok;

  /* try to parse */
  if ((res = gst_rtsp_url_parse (uri, &newurl)) < 0)
    goto parse_error;

  /* if worked, free previous and store new url object along with the original
   * location. */
  gst_rtsp_url_free (src->url);
  src->url = newurl;
  g_free (src->location);
  g_free (src->req_location);
  src->location = g_strdup (uri);
  src->req_location = gst_rtsp_url_get_request_uri (src->url);

  GST_DEBUG_OBJECT (src, "set uri: %s", GST_STR_NULL (uri));
  GST_DEBUG_OBJECT (src, "request uri is: %s",
      GST_STR_NULL (src->req_location));

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
