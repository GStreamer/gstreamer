/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim at fluendo dot com>
 *               <2006> Lutz Mueller <lutz at topfrose dot de>
 *               <2015> Jan Schmidt <jan at centricular dot com>
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
 * SECTION:element-rtspclientsink
 *
 * Makes a connection to an RTSP server and send data via RTSP RECORD.
 * rtspclientsink strictly follows RFC 2326
 *
 * RTSP supports transport over TCP or UDP in unicast or multicast mode. By
 * default rtspclientsink will negotiate a connection in the following order:
 * UDP unicast/UDP multicast/TCP. The order cannot be changed but the allowed
 * protocols can be controlled with the #GstRTSPClientSink:protocols property.
 *
 * rtspclientsink will internally instantiate an RTP session manager element
 * that will handle the RTCP messages to and from the server, jitter removal,
 * and packet reordering.
 * This feature is implemented using the gstrtpbin element.
 *
 * rtspclientsink accepts any stream for which there is an installed payloader,
 * creates the payloader and manages payload-types, as well as RTX setup.
 * The new-payloader signal is fired when a payloader is created, in case
 * an app wants to do custom configuration (such as for MTU).
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 videotestsrc ! jpegenc ! rtspclientsink location=rtsp://some.server/url
 * ]| Establish a connection to an RTSP server and send JPEG encoded video packets
 */

/* FIXMEs
 * - Handle EOS properly and shutdown. The problem with EOS is we don't know
 *   when the server has received all data, so we don't know when to do teardown.
 *   At the moment, we forward EOS to the app as soon as we stop sending. Is there
 *   a way to know from the receiver that it's got all data? Some session timeout?
 * - Implement extension support for Real / WMS if they support RECORD?
 * - Add support for network clock synchronised streaming?
 * - Fix crypto key nego so SAVP/SAVPF profiles work.
 * - Test (&fix?) HTTP tunnel support
 * - Add an address pool object for GstRTSPStreams to use for multicast
 * - Test multicast UDP transport
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <gst/net/gstnet.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/sdp/gstmikey.h>
#include <gst/rtp/rtp.h>

#include "gstrtspclientsink.h"

typedef struct _GstRtspClientSinkPad GstRtspClientSinkPad;
typedef GstGhostPadClass GstRtspClientSinkPadClass;

struct _GstRtspClientSinkPad
{
  GstGhostPad parent;
  GstElement *custom_payloader;
  guint ulpfec_percentage;
};

enum
{
  PROP_PAD_0,
  PROP_PAD_PAYLOADER,
  PROP_PAD_ULPFEC_PERCENTAGE
};

#define DEFAULT_PAD_ULPFEC_PERCENTAGE 0

static GType gst_rtsp_client_sink_pad_get_type (void);
G_DEFINE_TYPE (GstRtspClientSinkPad, gst_rtsp_client_sink_pad,
    GST_TYPE_GHOST_PAD);
#define GST_TYPE_RTSP_CLIENT_SINK_PAD (gst_rtsp_client_sink_pad_get_type ())
#define GST_RTSP_CLIENT_SINK_PAD(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTSP_CLIENT_SINK_PAD,GstRtspClientSinkPad))

static void
gst_rtsp_client_sink_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtspClientSinkPad *pad;

  pad = GST_RTSP_CLIENT_SINK_PAD (object);

  switch (prop_id) {
    case PROP_PAD_PAYLOADER:
      GST_OBJECT_LOCK (pad);
      if (pad->custom_payloader)
        gst_object_unref (pad->custom_payloader);
      pad->custom_payloader = g_value_get_object (value);
      gst_object_ref_sink (pad->custom_payloader);
      GST_OBJECT_UNLOCK (pad);
      break;
    case PROP_PAD_ULPFEC_PERCENTAGE:
      GST_OBJECT_LOCK (pad);
      pad->ulpfec_percentage = g_value_get_uint (value);
      GST_OBJECT_UNLOCK (pad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtsp_client_sink_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtspClientSinkPad *pad;

  pad = GST_RTSP_CLIENT_SINK_PAD (object);

  switch (prop_id) {
    case PROP_PAD_PAYLOADER:
      GST_OBJECT_LOCK (pad);
      g_value_set_object (value, pad->custom_payloader);
      GST_OBJECT_UNLOCK (pad);
      break;
    case PROP_PAD_ULPFEC_PERCENTAGE:
      GST_OBJECT_LOCK (pad);
      g_value_set_uint (value, pad->ulpfec_percentage);
      GST_OBJECT_UNLOCK (pad);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtsp_client_sink_pad_dispose (GObject * object)
{
  GstRtspClientSinkPad *pad = GST_RTSP_CLIENT_SINK_PAD (object);

  if (pad->custom_payloader)
    gst_object_unref (pad->custom_payloader);

  G_OBJECT_CLASS (gst_rtsp_client_sink_pad_parent_class)->dispose (object);
}

static void
gst_rtsp_client_sink_pad_class_init (GstRtspClientSinkPadClass * klass)
{
  GObjectClass *gobject_klass;

  gobject_klass = (GObjectClass *) klass;

  gobject_klass->set_property = gst_rtsp_client_sink_pad_set_property;
  gobject_klass->get_property = gst_rtsp_client_sink_pad_get_property;
  gobject_klass->dispose = gst_rtsp_client_sink_pad_dispose;

  g_object_class_install_property (gobject_klass, PROP_PAD_PAYLOADER,
      g_param_spec_object ("payloader", "Payloader",
          "The payloader element to use (NULL = default automatically selected)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_klass, PROP_PAD_ULPFEC_PERCENTAGE,
      g_param_spec_uint ("ulpfec-percentage", "ULPFEC percentage",
          "The percentage of ULP redundancy to apply", 0, 100,
          DEFAULT_PAD_ULPFEC_PERCENTAGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_rtsp_client_sink_pad_init (GstRtspClientSinkPad * pad)
{
}

static GstPad *
gst_rtsp_client_sink_pad_new (const GstPadTemplate * pad_tmpl,
    const gchar * name)
{
  GstRtspClientSinkPad *ret;

  ret =
      g_object_new (GST_TYPE_RTSP_CLIENT_SINK_PAD, "direction", GST_PAD_SINK,
      "template", pad_tmpl, "name", name, NULL);

  return GST_PAD (ret);
}

GST_DEBUG_CATEGORY_STATIC (rtsp_client_sink_debug);
#define GST_CAT_DEFAULT (rtsp_client_sink_debug)

static GstStaticPadTemplate rtptemplate = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);       /* Actual caps come from available set of payloaders */

enum
{
  SIGNAL_HANDLE_REQUEST,
  SIGNAL_NEW_MANAGER,
  SIGNAL_NEW_PAYLOADER,
  SIGNAL_REQUEST_RTCP_KEY,
  SIGNAL_ACCEPT_CERTIFICATE,
  LAST_SIGNAL
};

enum _GstRTSPClientSinkNtpTimeSource
{
  NTP_TIME_SOURCE_NTP,
  NTP_TIME_SOURCE_UNIX,
  NTP_TIME_SOURCE_RUNNING_TIME,
  NTP_TIME_SOURCE_CLOCK_TIME
};

#define GST_TYPE_RTSP_CLIENT_SINK_NTP_TIME_SOURCE (gst_rtsp_client_sink_ntp_time_source_get_type())
static GType
gst_rtsp_client_sink_ntp_time_source_get_type (void)
{
  static GType ntp_time_source_type = 0;
  static const GEnumValue ntp_time_source_values[] = {
    {NTP_TIME_SOURCE_NTP, "NTP time based on realtime clock", "ntp"},
    {NTP_TIME_SOURCE_UNIX, "UNIX time based on realtime clock", "unix"},
    {NTP_TIME_SOURCE_RUNNING_TIME,
          "Running time based on pipeline clock",
        "running-time"},
    {NTP_TIME_SOURCE_CLOCK_TIME, "Pipeline clock time", "clock-time"},
    {0, NULL, NULL},
  };

  if (!ntp_time_source_type) {
    ntp_time_source_type =
        g_enum_register_static ("GstRTSPClientSinkNtpTimeSource",
        ntp_time_source_values);
  }
  return ntp_time_source_type;
}

#define DEFAULT_LOCATION         NULL
#define DEFAULT_PROTOCOLS        GST_RTSP_LOWER_TRANS_UDP | GST_RTSP_LOWER_TRANS_UDP_MCAST | GST_RTSP_LOWER_TRANS_TCP
#define DEFAULT_DEBUG            FALSE
#define DEFAULT_RETRY            20
#define DEFAULT_TIMEOUT          5000000
#define DEFAULT_UDP_BUFFER_SIZE  0x80000
#define DEFAULT_TCP_TIMEOUT      20000000
#define DEFAULT_LATENCY_MS       2000
#define DEFAULT_DO_RTSP_KEEP_ALIVE       TRUE
#define DEFAULT_PROXY            NULL
#define DEFAULT_RTP_BLOCKSIZE    0
#define DEFAULT_USER_ID          NULL
#define DEFAULT_USER_PW          NULL
#define DEFAULT_PORT_RANGE       NULL
#define DEFAULT_UDP_RECONNECT    TRUE
#define DEFAULT_MULTICAST_IFACE  NULL
#define DEFAULT_TLS_VALIDATION_FLAGS     G_TLS_CERTIFICATE_VALIDATE_ALL
#define DEFAULT_TLS_DATABASE     NULL
#define DEFAULT_TLS_INTERACTION     NULL
#define DEFAULT_NTP_TIME_SOURCE  NTP_TIME_SOURCE_NTP
#define DEFAULT_USER_AGENT       "GStreamer/" PACKAGE_VERSION
#define DEFAULT_PROFILES         GST_RTSP_PROFILE_AVP
#define DEFAULT_RTX_TIME_MS      500

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
  PROP_RTX_TIME,
  PROP_DO_RTSP_KEEP_ALIVE,
  PROP_PROXY,
  PROP_PROXY_ID,
  PROP_PROXY_PW,
  PROP_RTP_BLOCKSIZE,
  PROP_USER_ID,
  PROP_USER_PW,
  PROP_PORT_RANGE,
  PROP_UDP_BUFFER_SIZE,
  PROP_UDP_RECONNECT,
  PROP_MULTICAST_IFACE,
  PROP_SDES,
  PROP_TLS_VALIDATION_FLAGS,
  PROP_TLS_DATABASE,
  PROP_TLS_INTERACTION,
  PROP_NTP_TIME_SOURCE,
  PROP_USER_AGENT,
  PROP_PROFILES
};

static void gst_rtsp_client_sink_finalize (GObject * object);

static void gst_rtsp_client_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtsp_client_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstClock *gst_rtsp_client_sink_provide_clock (GstElement * element);

static void gst_rtsp_client_sink_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

static gboolean gst_rtsp_client_sink_set_proxy (GstRTSPClientSink * rtsp,
    const gchar * proxy);
static void gst_rtsp_client_sink_set_tcp_timeout (GstRTSPClientSink *
    rtsp_client_sink, guint64 timeout);

static GstStateChangeReturn gst_rtsp_client_sink_change_state (GstElement *
    element, GstStateChange transition);
static void gst_rtsp_client_sink_handle_message (GstBin * bin,
    GstMessage * message);

static gboolean gst_rtsp_client_sink_setup_auth (GstRTSPClientSink * sink,
    GstRTSPMessage * response);

static gboolean gst_rtsp_client_sink_loop_send_cmd (GstRTSPClientSink * sink,
    gint cmd, gint mask);

static GstRTSPResult gst_rtsp_client_sink_open (GstRTSPClientSink * sink,
    gboolean async);
static GstRTSPResult gst_rtsp_client_sink_record (GstRTSPClientSink * sink,
    gboolean async);
static GstRTSPResult gst_rtsp_client_sink_pause (GstRTSPClientSink * sink,
    gboolean async);
static GstRTSPResult gst_rtsp_client_sink_close (GstRTSPClientSink * sink,
    gboolean async, gboolean only_close);
static gboolean gst_rtsp_client_sink_collect_streams (GstRTSPClientSink * sink);

static gboolean gst_rtsp_client_sink_uri_set_uri (GstURIHandler * handler,
    const gchar * uri, GError ** error);
static gchar *gst_rtsp_client_sink_uri_get_uri (GstURIHandler * handler);

static gboolean gst_rtsp_client_sink_loop (GstRTSPClientSink * sink);
static void gst_rtsp_client_sink_connection_flush (GstRTSPClientSink * sink,
    gboolean flush);

static GstPad *gst_rtsp_client_sink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_rtsp_client_sink_release_pad (GstElement * element,
    GstPad * pad);

/* commands we send to out loop to notify it of events */
#define CMD_OPEN	(1 << 0)
#define CMD_RECORD	(1 << 1)
#define CMD_PAUSE	(1 << 2)
#define CMD_CLOSE	(1 << 3)
#define CMD_WAIT	(1 << 4)
#define CMD_RECONNECT	(1 << 5)
#define CMD_LOOP	(1 << 6)

/* mask for all commands */
#define CMD_ALL         ((CMD_LOOP << 1) - 1)

#define GST_ELEMENT_PROGRESS(el, type, code, text)      \
G_STMT_START {                                          \
  gchar *__txt = _gst_element_error_printf text;        \
  gst_element_post_message (GST_ELEMENT_CAST (el),      \
      gst_message_new_progress (GST_OBJECT_CAST (el),   \
          GST_PROGRESS_TYPE_ ##type, code, __txt));     \
  g_free (__txt);                                       \
} G_STMT_END

static guint gst_rtsp_client_sink_signals[LAST_SIGNAL] = { 0 };

/*********************************
 * GstChildProxy implementation  *
 *********************************/
static GObject *
gst_rtsp_client_sink_child_proxy_get_child_by_index (GstChildProxy *
    child_proxy, guint index)
{
  GObject *obj;
  GstRTSPClientSink *cs = GST_RTSP_CLIENT_SINK (child_proxy);

  GST_OBJECT_LOCK (cs);
  if ((obj = g_list_nth_data (GST_ELEMENT (cs)->sinkpads, index)))
    g_object_ref (obj);
  GST_OBJECT_UNLOCK (cs);

  return obj;
}

static guint
gst_rtsp_client_sink_child_proxy_get_children_count (GstChildProxy *
    child_proxy)
{
  guint count = 0;

  GST_OBJECT_LOCK (child_proxy);
  count = GST_ELEMENT (child_proxy)->numsinkpads;
  GST_OBJECT_UNLOCK (child_proxy);

  GST_INFO_OBJECT (child_proxy, "Children Count: %d", count);

  return count;
}

static void
gst_rtsp_client_sink_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = g_iface;

  GST_INFO ("intializing child proxy interface");
  iface->get_child_by_index =
      gst_rtsp_client_sink_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_rtsp_client_sink_child_proxy_get_children_count;
}

#define gst_rtsp_client_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRTSPClientSink, gst_rtsp_client_sink, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_rtsp_client_sink_uri_handler_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_rtsp_client_sink_child_proxy_init);
    );

#ifndef GST_DISABLE_GST_DEBUG
static inline const gchar *
cmd_to_string (guint cmd)
{
  switch (cmd) {
    case CMD_OPEN:
      return "OPEN";
    case CMD_RECORD:
      return "RECORD";
    case CMD_PAUSE:
      return "PAUSE";
    case CMD_CLOSE:
      return "CLOSE";
    case CMD_WAIT:
      return "WAIT";
    case CMD_RECONNECT:
      return "RECONNECT";
    case CMD_LOOP:
      return "LOOP";
  }

  return "unknown";
}
#endif

static void
gst_rtsp_client_sink_class_init (GstRTSPClientSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBinClass *gstbin_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbin_class = (GstBinClass *) klass;

  GST_DEBUG_CATEGORY_INIT (rtsp_client_sink_debug, "rtspclientsink", 0,
      "RTSP sink element");

  gobject_class->set_property = gst_rtsp_client_sink_set_property;
  gobject_class->get_property = gst_rtsp_client_sink_get_property;

  gobject_class->finalize = gst_rtsp_client_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "RTSP Location",
          "Location of the RTSP url to read",
          DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROTOCOLS,
      g_param_spec_flags ("protocols", "Protocols",
          "Allowed lower transport protocols", GST_TYPE_RTSP_LOWER_TRANS,
          DEFAULT_PROTOCOLS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PROFILES,
      g_param_spec_flags ("profiles", "Profiles",
          "Allowed RTSP profiles", GST_TYPE_RTSP_PROFILE,
          DEFAULT_PROFILES, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DEBUG,
      g_param_spec_boolean ("debug", "Debug",
          "Dump request and response messages to stdout",
          DEFAULT_DEBUG, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RETRY,
      g_param_spec_uint ("retry", "Retry",
          "Max number of retries when allocating RTP ports.",
          0, G_MAXUINT16, DEFAULT_RETRY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "Retry TCP transport after UDP timeout microseconds (0 = disabled)",
          0, G_MAXUINT64, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TCP_TIMEOUT,
      g_param_spec_uint64 ("tcp-timeout", "TCP Timeout",
          "Fail after timeout microseconds on TCP connections (0 = disabled)",
          0, G_MAXUINT64, DEFAULT_TCP_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LATENCY,
      g_param_spec_uint ("latency", "Buffer latency in ms",
          "Amount of ms to buffer", 0, G_MAXUINT, DEFAULT_LATENCY_MS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RTX_TIME,
      g_param_spec_uint ("rtx-time", "Retransmission buffer in ms",
          "Amount of ms to buffer for retransmission. 0 disables retransmission",
          0, G_MAXUINT, DEFAULT_RTX_TIME_MS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink:do-rtsp-keep-alive:
   *
   * Enable RTSP keep alive support. Some old server don't like RTSP
   * keep alive and then this property needs to be set to FALSE.
   */
  g_object_class_install_property (gobject_class, PROP_DO_RTSP_KEEP_ALIVE,
      g_param_spec_boolean ("do-rtsp-keep-alive", "Do RTSP Keep Alive",
          "Send RTSP keep alive packets, disable for old incompatible server.",
          DEFAULT_DO_RTSP_KEEP_ALIVE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink:proxy:
   *
   * Set the proxy parameters. This has to be a string of the format
   * [http://][user:passwd@]host[:port].
   */
  g_object_class_install_property (gobject_class, PROP_PROXY,
      g_param_spec_string ("proxy", "Proxy",
          "Proxy settings for HTTP tunneling. Format: [http://][user:passwd@]host[:port]",
          DEFAULT_PROXY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPClientSink:proxy-id:
   *
   * Sets the proxy URI user id for authentication. If the URI set via the
   * "proxy" property contains a user-id already, that will take precedence.
   *
   */
  g_object_class_install_property (gobject_class, PROP_PROXY_ID,
      g_param_spec_string ("proxy-id", "proxy-id",
          "HTTP proxy URI user id for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstRTSPClientSink:proxy-pw:
   *
   * Sets the proxy URI password for authentication. If the URI set via the
   * "proxy" property contains a password already, that will take precedence.
   *
   */
  g_object_class_install_property (gobject_class, PROP_PROXY_PW,
      g_param_spec_string ("proxy-pw", "proxy-pw",
          "HTTP proxy URI user password for authentication", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink:rtp-blocksize:
   *
   * RTP package size to suggest to server.
   */
  g_object_class_install_property (gobject_class, PROP_RTP_BLOCKSIZE,
      g_param_spec_uint ("rtp-blocksize", "RTP Blocksize",
          "RTP package size to suggest to server (0 = disabled)",
          0, 65536, DEFAULT_RTP_BLOCKSIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_USER_ID,
      g_param_spec_string ("user-id", "user-id",
          "RTSP location URI user id for authentication", DEFAULT_USER_ID,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_PW,
      g_param_spec_string ("user-pw", "user-pw",
          "RTSP location URI user password for authentication", DEFAULT_USER_PW,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink:port-range:
   *
   * Configure the client port numbers that can be used to receive
   * RTCP.
   */
  g_object_class_install_property (gobject_class, PROP_PORT_RANGE,
      g_param_spec_string ("port-range", "Port range",
          "Client port range that can be used to receive RTCP data, "
          "eg. 3000-3005 (NULL = no restrictions)", DEFAULT_PORT_RANGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink:udp-buffer-size:
   *
   * Size of the kernel UDP receive buffer in bytes.
   */
  g_object_class_install_property (gobject_class, PROP_UDP_BUFFER_SIZE,
      g_param_spec_int ("udp-buffer-size", "UDP Buffer Size",
          "Size of the kernel UDP receive buffer in bytes, 0=default",
          0, G_MAXINT, DEFAULT_UDP_BUFFER_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_UDP_RECONNECT,
      g_param_spec_boolean ("udp-reconnect", "Reconnect to the server",
          "Reconnect to the server if RTSP connection is closed when doing UDP",
          DEFAULT_UDP_RECONNECT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MULTICAST_IFACE,
      g_param_spec_string ("multicast-iface", "Multicast Interface",
          "The network interface on which to join the multicast group",
          DEFAULT_MULTICAST_IFACE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SDES,
      g_param_spec_boxed ("sdes", "SDES",
          "The SDES items of this session",
          GST_TYPE_STRUCTURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink::tls-validation-flags:
   *
   * TLS certificate validation flags used to validate server
   * certificate.
   *
   */
  g_object_class_install_property (gobject_class, PROP_TLS_VALIDATION_FLAGS,
      g_param_spec_flags ("tls-validation-flags", "TLS validation flags",
          "TLS certificate validation flags used to validate the server certificate",
          G_TYPE_TLS_CERTIFICATE_FLAGS, DEFAULT_TLS_VALIDATION_FLAGS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink::tls-database:
   *
   * TLS database with anchor certificate authorities used to validate
   * the server certificate.
   *
   */
  g_object_class_install_property (gobject_class, PROP_TLS_DATABASE,
      g_param_spec_object ("tls-database", "TLS database",
          "TLS database with anchor certificate authorities used to validate the server certificate",
          G_TYPE_TLS_DATABASE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink::tls-interaction:
   *
   * A #GTlsInteraction object to be used when the connection or certificate
   * database need to interact with the user. This will be used to prompt the
   * user for passwords where necessary.
   *
   */
  g_object_class_install_property (gobject_class, PROP_TLS_INTERACTION,
      g_param_spec_object ("tls-interaction", "TLS interaction",
          "A GTlsInteraction object to prompt the user for password or certificate",
          G_TYPE_TLS_INTERACTION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink::ntp-time-source:
   *
   * allows to select the time source that should be used
   * for the NTP time in outgoing packets
   *
   */
  g_object_class_install_property (gobject_class, PROP_NTP_TIME_SOURCE,
      g_param_spec_enum ("ntp-time-source", "NTP Time Source",
          "NTP time source for RTCP packets",
          GST_TYPE_RTSP_CLIENT_SINK_NTP_TIME_SOURCE, DEFAULT_NTP_TIME_SOURCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink::user-agent:
   *
   * The string to set in the User-Agent header.
   *
   */
  g_object_class_install_property (gobject_class, PROP_USER_AGENT,
      g_param_spec_string ("user-agent", "User Agent",
          "The User-Agent string to send to the server",
          DEFAULT_USER_AGENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstRTSPClientSink::handle-request:
   * @rtsp_client_sink: a #GstRTSPClientSink
   * @request: a #GstRTSPMessage
   * @response: a #GstRTSPMessage
   *
   * Handle a server request in @request and prepare @response.
   *
   * This signal is called from the streaming thread, you should therefore not
   * do any state changes on @rtsp_client_sink because this might deadlock. If you want
   * to modify the state as a result of this signal, post a
   * #GST_MESSAGE_REQUEST_STATE message on the bus or signal the main thread
   * in some other way.
   *
   */
  gst_rtsp_client_sink_signals[SIGNAL_HANDLE_REQUEST] =
      g_signal_new ("handle-request", G_TYPE_FROM_CLASS (klass), 0,
      0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);

  /**
   * GstRTSPClientSink::new-manager:
   * @rtsp_client_sink: a #GstRTSPClientSink
   * @manager: a #GstElement
   *
   * Emitted after a new manager (like rtpbin) was created and the default
   * properties were configured.
   *
   */
  gst_rtsp_client_sink_signals[SIGNAL_NEW_MANAGER] =
      g_signal_new_class_handler ("new-manager", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_CLEANUP, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  /**
   * GstRTSPClientSink::new-payloader:
   * @rtsp_client_sink: a #GstRTSPClientSink
   * @payloader: a #GstElement
   *
   * Emitted after a new RTP payloader was created and the default
   * properties were configured.
   *
   */
  gst_rtsp_client_sink_signals[SIGNAL_NEW_PAYLOADER] =
      g_signal_new_class_handler ("new-payloader", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_RUN_CLEANUP, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_ELEMENT);

  /**
   * GstRTSPClientSink::request-rtcp-key:
   * @rtsp_client_sink: a #GstRTSPClientSink
   * @num: the stream number
   *
   * Signal emitted to get the crypto parameters relevant to the RTCP
   * stream. User should provide the key and the RTCP encryption ciphers
   * and authentication, and return them wrapped in a GstCaps.
   *
   */
  gst_rtsp_client_sink_signals[SIGNAL_REQUEST_RTCP_KEY] =
      g_signal_new ("request-rtcp-key", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, GST_TYPE_CAPS, 1, G_TYPE_UINT);

  /**
   * GstRTSPClientSink::accept-certificate:
   * @rtsp_client_sink: a #GstRTSPClientSink
   * @peer_cert: the peer's #GTlsCertificate
   * @errors: the problems with @peer_cert
   * @user_data: user data set when the signal handler was connected.
   *
   * This will directly map to #GTlsConnection 's "accept-certificate"
   * signal and be performed after the default checks of #GstRTSPConnection
   * (checking against the #GTlsDatabase with the given #GTlsCertificateFlags)
   * have failed. If no #GTlsDatabase is set on this connection, only this
   * signal will be emitted.
   *
   * Since: 1.14
   */
  gst_rtsp_client_sink_signals[SIGNAL_ACCEPT_CERTIFICATE] =
      g_signal_new ("accept-certificate", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, g_signal_accumulator_true_handled, NULL, NULL,
      G_TYPE_BOOLEAN, 3, G_TYPE_TLS_CONNECTION, G_TYPE_TLS_CERTIFICATE,
      G_TYPE_TLS_CERTIFICATE_FLAGS);

  gstelement_class->provide_clock = gst_rtsp_client_sink_provide_clock;
  gstelement_class->change_state = gst_rtsp_client_sink_change_state;
  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtsp_client_sink_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_rtsp_client_sink_release_pad);

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &rtptemplate, GST_TYPE_RTSP_CLIENT_SINK_PAD);

  gst_element_class_set_static_metadata (gstelement_class,
      "RTSP RECORD client", "Sink/Network",
      "Send data over the network via RTSP RECORD(RFC 2326)",
      "Jan Schmidt <jan@centricular.com>");

  gstbin_class->handle_message = gst_rtsp_client_sink_handle_message;

  gst_type_mark_as_plugin_api (GST_TYPE_RTSP_CLIENT_SINK_PAD, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_RTSP_CLIENT_SINK_NTP_TIME_SOURCE, 0);
}

static void
gst_rtsp_client_sink_init (GstRTSPClientSink * sink)
{
  sink->conninfo.location = g_strdup (DEFAULT_LOCATION);
  sink->protocols = DEFAULT_PROTOCOLS;
  sink->debug = DEFAULT_DEBUG;
  sink->retry = DEFAULT_RETRY;
  sink->udp_timeout = DEFAULT_TIMEOUT;
  gst_rtsp_client_sink_set_tcp_timeout (sink, DEFAULT_TCP_TIMEOUT);
  sink->latency = DEFAULT_LATENCY_MS;
  sink->rtx_time = DEFAULT_RTX_TIME_MS;
  sink->do_rtsp_keep_alive = DEFAULT_DO_RTSP_KEEP_ALIVE;
  gst_rtsp_client_sink_set_proxy (sink, DEFAULT_PROXY);
  sink->rtp_blocksize = DEFAULT_RTP_BLOCKSIZE;
  sink->user_id = g_strdup (DEFAULT_USER_ID);
  sink->user_pw = g_strdup (DEFAULT_USER_PW);
  sink->client_port_range.min = 0;
  sink->client_port_range.max = 0;
  sink->udp_buffer_size = DEFAULT_UDP_BUFFER_SIZE;
  sink->udp_reconnect = DEFAULT_UDP_RECONNECT;
  sink->multi_iface = g_strdup (DEFAULT_MULTICAST_IFACE);
  sink->sdes = NULL;
  sink->tls_validation_flags = DEFAULT_TLS_VALIDATION_FLAGS;
  sink->tls_database = DEFAULT_TLS_DATABASE;
  sink->tls_interaction = DEFAULT_TLS_INTERACTION;
  sink->ntp_time_source = DEFAULT_NTP_TIME_SOURCE;
  sink->user_agent = g_strdup (DEFAULT_USER_AGENT);

  sink->profiles = DEFAULT_PROFILES;

  /* protects the streaming thread in interleaved mode or the polling
   * thread in UDP mode. */
  g_rec_mutex_init (&sink->stream_rec_lock);

  /* protects our state changes from multiple invocations */
  g_rec_mutex_init (&sink->state_rec_lock);

  g_mutex_init (&sink->send_lock);

  g_mutex_init (&sink->preroll_lock);
  g_cond_init (&sink->preroll_cond);

  sink->state = GST_RTSP_STATE_INVALID;

  g_mutex_init (&sink->conninfo.send_lock);
  g_mutex_init (&sink->conninfo.recv_lock);

  g_mutex_init (&sink->block_streams_lock);
  g_cond_init (&sink->block_streams_cond);

  g_mutex_init (&sink->open_conn_lock);
  g_cond_init (&sink->open_conn_cond);

  sink->internal_bin = (GstBin *) gst_bin_new ("rtspbin");
  g_object_set (sink->internal_bin, "async-handling", TRUE, NULL);
  gst_element_set_locked_state (GST_ELEMENT_CAST (sink->internal_bin), TRUE);
  gst_bin_add (GST_BIN (sink), GST_ELEMENT_CAST (sink->internal_bin));

  sink->next_dyn_pt = 96;

  gst_sdp_message_init (&sink->cursdp);

  GST_OBJECT_FLAG_SET (sink, GST_ELEMENT_FLAG_SINK);
}

static void
gst_rtsp_client_sink_finalize (GObject * object)
{
  GstRTSPClientSink *rtsp_client_sink;

  rtsp_client_sink = GST_RTSP_CLIENT_SINK (object);

  gst_sdp_message_uninit (&rtsp_client_sink->cursdp);

  g_free (rtsp_client_sink->conninfo.location);
  gst_rtsp_url_free (rtsp_client_sink->conninfo.url);
  g_free (rtsp_client_sink->conninfo.url_str);
  g_free (rtsp_client_sink->user_id);
  g_free (rtsp_client_sink->user_pw);
  g_free (rtsp_client_sink->multi_iface);
  g_free (rtsp_client_sink->user_agent);

  if (rtsp_client_sink->uri_sdp) {
    gst_sdp_message_free (rtsp_client_sink->uri_sdp);
    rtsp_client_sink->uri_sdp = NULL;
  }
  if (rtsp_client_sink->provided_clock)
    gst_object_unref (rtsp_client_sink->provided_clock);

  if (rtsp_client_sink->sdes)
    gst_structure_free (rtsp_client_sink->sdes);

  if (rtsp_client_sink->tls_database)
    g_object_unref (rtsp_client_sink->tls_database);

  if (rtsp_client_sink->tls_interaction)
    g_object_unref (rtsp_client_sink->tls_interaction);

  /* free locks */
  g_rec_mutex_clear (&rtsp_client_sink->stream_rec_lock);
  g_rec_mutex_clear (&rtsp_client_sink->state_rec_lock);

  g_mutex_clear (&rtsp_client_sink->conninfo.send_lock);
  g_mutex_clear (&rtsp_client_sink->conninfo.recv_lock);

  g_mutex_clear (&rtsp_client_sink->send_lock);

  g_mutex_clear (&rtsp_client_sink->preroll_lock);
  g_cond_clear (&rtsp_client_sink->preroll_cond);

  g_mutex_clear (&rtsp_client_sink->block_streams_lock);
  g_cond_clear (&rtsp_client_sink->block_streams_cond);

  g_mutex_clear (&rtsp_client_sink->open_conn_lock);
  g_cond_clear (&rtsp_client_sink->open_conn_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_rtp_payloader_filter_func (GstPluginFeature * feature, gpointer user_data)
{
  GstElementFactory *factory = NULL;
  const gchar *klass;

  if (!GST_IS_ELEMENT_FACTORY (feature))
    return FALSE;

  factory = GST_ELEMENT_FACTORY (feature);

  if (gst_plugin_feature_get_rank (feature) == GST_RANK_NONE)
    return FALSE;

  if (!gst_element_factory_list_is_type (factory,
          GST_ELEMENT_FACTORY_TYPE_PAYLOADER))
    return FALSE;

  klass =
      gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);
  if (strstr (klass, "Codec") == NULL)
    return FALSE;
  if (strstr (klass, "RTP") == NULL)
    return FALSE;

  return TRUE;
}

static gint
compare_ranks (GstPluginFeature * f1, GstPluginFeature * f2)
{
  gint diff;
  const gchar *rname1, *rname2;
  GstRank rank1, rank2;

  rname1 = gst_plugin_feature_get_name (f1);
  rname2 = gst_plugin_feature_get_name (f2);

  rank1 = gst_plugin_feature_get_rank (f1);
  rank2 = gst_plugin_feature_get_rank (f2);

  /* HACK: Prefer rtpmp4apay over rtpmp4gpay */
  if (g_str_equal (rname1, "rtpmp4apay"))
    rank1 = GST_RANK_SECONDARY + 1;
  if (g_str_equal (rname2, "rtpmp4apay"))
    rank2 = GST_RANK_SECONDARY + 1;

  diff = rank2 - rank1;
  if (diff != 0)
    return diff;

  diff = strcmp (rname2, rname1);

  return diff;
}

static GList *
gst_rtsp_client_sink_get_factories (void)
{
  static GList *payloader_factories = NULL;

  if (g_once_init_enter (&payloader_factories)) {
    GList *all_factories;

    all_factories =
        gst_registry_feature_filter (gst_registry_get (),
        gst_rtp_payloader_filter_func, FALSE, NULL);

    all_factories = g_list_sort (all_factories, (GCompareFunc) compare_ranks);

    g_once_init_leave (&payloader_factories, all_factories);
  }

  return payloader_factories;
}

static GstCaps *
gst_rtsp_client_sink_get_payloader_caps (GstElementFactory * factory)
{
  const GList *tmp;
  GstCaps *caps = gst_caps_new_empty ();

  for (tmp = gst_element_factory_get_static_pad_templates (factory);
      tmp; tmp = g_list_next (tmp)) {
    GstStaticPadTemplate *template = tmp->data;

    if (template->direction == GST_PAD_SINK) {
      GstCaps *static_caps = gst_static_pad_template_get_caps (template);

      GST_LOG ("Found pad template %s on factory %s",
          template->name_template, gst_plugin_feature_get_name (factory));

      if (static_caps)
        caps = gst_caps_merge (caps, static_caps);

      /* Early out, any is absorbing */
      if (gst_caps_is_any (caps))
        goto out;
    }
  }

out:
  return caps;
}

static GstCaps *
gst_rtsp_client_sink_get_all_payloaders_caps (void)
{
  /* Cached caps result */
  static GstCaps *ret;

  if (g_once_init_enter (&ret)) {
    GList *factories, *cur;
    GstCaps *caps = gst_caps_new_empty ();

    factories = gst_rtsp_client_sink_get_factories ();
    for (cur = factories; cur != NULL; cur = g_list_next (cur)) {
      GstElementFactory *factory = GST_ELEMENT_FACTORY (cur->data);
      GstCaps *payloader_caps =
          gst_rtsp_client_sink_get_payloader_caps (factory);

      caps = gst_caps_merge (caps, payloader_caps);

      /* Early out, any is absorbing */
      if (gst_caps_is_any (caps))
        goto out;
    }

    GST_MINI_OBJECT_FLAG_SET (caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  out:
    g_once_init_leave (&ret, caps);
  }

  /* Return cached result */
  return gst_caps_ref (ret);
}

static GstElement *
gst_rtsp_client_sink_make_payloader (GstCaps * caps)
{
  GList *factories, *cur;

  factories = gst_rtsp_client_sink_get_factories ();
  for (cur = factories; cur != NULL; cur = g_list_next (cur)) {
    GstElementFactory *factory = GST_ELEMENT_FACTORY (cur->data);
    const GList *tmp;

    for (tmp = gst_element_factory_get_static_pad_templates (factory);
        tmp; tmp = g_list_next (tmp)) {
      GstStaticPadTemplate *template = tmp->data;

      if (template->direction == GST_PAD_SINK) {
        GstCaps *static_caps = gst_static_pad_template_get_caps (template);
        GstElement *payloader = NULL;

        if (gst_caps_can_intersect (static_caps, caps)) {
          GST_DEBUG ("caps %" GST_PTR_FORMAT " intersects with template %"
              GST_PTR_FORMAT " for payloader %s", caps, static_caps,
              gst_plugin_feature_get_name (factory));
          payloader = gst_element_factory_create (factory, NULL);
        }

        gst_caps_unref (static_caps);

        if (payloader)
          return payloader;
      }
    }
  }

  return NULL;
}

static GstRTSPStream *
gst_rtsp_client_sink_create_stream (GstRTSPClientSink * sink,
    GstRTSPStreamContext * context, GstElement * payloader, GstPad * pad)
{
  GstRTSPStream *stream = NULL;
  guint pt, aux_pt, ulpfec_pt;

  GST_OBJECT_LOCK (sink);

  g_object_get (G_OBJECT (payloader), "pt", &pt, NULL);
  if (pt >= 96 && pt <= sink->next_dyn_pt) {
    /* Payloader has a dynamic PT, but one that's already used */
    /* FIXME: Create a caps->ptmap instead? */
    pt = sink->next_dyn_pt;

    if (pt > 127)
      goto no_free_pt;

    GST_DEBUG_OBJECT (sink, "Assigning pt %u to stream %d", pt, context->index);

    sink->next_dyn_pt++;
  } else {
    GST_DEBUG_OBJECT (sink, "Keeping existing pt %u for stream %d",
        pt, context->index);
  }

  aux_pt = sink->next_dyn_pt;
  if (aux_pt > 127)
    goto no_free_pt;
  sink->next_dyn_pt++;

  ulpfec_pt = sink->next_dyn_pt;
  if (ulpfec_pt > 127)
    goto no_free_pt;
  sink->next_dyn_pt++;

  GST_OBJECT_UNLOCK (sink);


  g_object_set (G_OBJECT (payloader), "pt", pt, NULL);

  stream = gst_rtsp_stream_new (context->index, payloader, pad);

  gst_rtsp_stream_set_client_side (stream, TRUE);
  gst_rtsp_stream_set_retransmission_time (stream,
      (GstClockTime) (sink->rtx_time) * GST_MSECOND);
  gst_rtsp_stream_set_protocols (stream, sink->protocols);
  gst_rtsp_stream_set_profiles (stream, sink->profiles);
  gst_rtsp_stream_set_retransmission_pt (stream, aux_pt);
  gst_rtsp_stream_set_buffer_size (stream, sink->udp_buffer_size);
  if (sink->rtp_blocksize > 0)
    gst_rtsp_stream_set_mtu (stream, sink->rtp_blocksize);
  gst_rtsp_stream_set_multicast_iface (stream, sink->multi_iface);

  gst_rtsp_stream_set_ulpfec_pt (stream, ulpfec_pt);
  gst_rtsp_stream_set_ulpfec_percentage (stream, context->ulpfec_percentage);

#if 0
  if (priv->pool)
    gst_rtsp_stream_set_address_pool (stream, priv->pool);
#endif

  return stream;
no_free_pt:
  GST_OBJECT_UNLOCK (sink);

  GST_ELEMENT_ERROR (sink, RESOURCE, NO_SPACE_LEFT, (NULL),
      ("Ran out of dynamic payload types."));

  return NULL;
}

static GstPadProbeReturn
handle_payloader_block (GstPad * pad, GstPadProbeInfo * info,
    GstRTSPStreamContext * context)
{
  GstRTSPClientSink *sink = context->parent;

  GST_INFO_OBJECT (sink, "Block on pad %" GST_PTR_FORMAT, pad);

  g_mutex_lock (&sink->preroll_lock);
  context->prerolled = TRUE;
  g_cond_broadcast (&sink->preroll_cond);
  g_mutex_unlock (&sink->preroll_lock);

  GST_INFO_OBJECT (sink, "Announced preroll on pad %" GST_PTR_FORMAT, pad);

  return GST_PAD_PROBE_OK;
}

static gboolean
gst_rtsp_client_sink_setup_payloader (GstRTSPClientSink * sink, GstPad * pad,
    GstCaps * caps)
{
  GstRTSPStreamContext *context;
  GstRtspClientSinkPad *cspad = GST_RTSP_CLIENT_SINK_PAD (pad);

  GstElement *payloader;
  GstPad *sinkpad, *srcpad, *ghostsink;

  context = gst_pad_get_element_private (pad);

  if (cspad->custom_payloader) {
    payloader = cspad->custom_payloader;
  } else {
    /* Find the payloader. */
    payloader = gst_rtsp_client_sink_make_payloader (caps);
  }

  if (payloader == NULL)
    return FALSE;

  GST_DEBUG_OBJECT (sink, "Configuring payloader %" GST_PTR_FORMAT
      " for pad %" GST_PTR_FORMAT, payloader, pad);

  sinkpad = gst_element_get_static_pad (payloader, "sink");
  if (sinkpad == NULL)
    goto no_sinkpad;

  srcpad = gst_element_get_static_pad (payloader, "src");
  if (srcpad == NULL)
    goto no_srcpad;

  gst_bin_add (GST_BIN (sink->internal_bin), payloader);
  ghostsink = gst_ghost_pad_new (NULL, sinkpad);
  gst_pad_set_active (ghostsink, TRUE);
  gst_element_add_pad (GST_ELEMENT (sink->internal_bin), ghostsink);

  g_signal_emit (sink, gst_rtsp_client_sink_signals[SIGNAL_NEW_PAYLOADER], 0,
      payloader);

  GST_RTSP_STATE_LOCK (sink);
  context->payloader_block_id =
      gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      (GstPadProbeCallback) handle_payloader_block, context, NULL);
  context->payloader = payloader;

  payloader = gst_object_ref (payloader);

  gst_ghost_pad_set_target (GST_GHOST_PAD (pad), ghostsink);
  gst_object_unref (GST_OBJECT (sinkpad));
  GST_RTSP_STATE_UNLOCK (sink);

  context->ulpfec_percentage = cspad->ulpfec_percentage;

  gst_element_sync_state_with_parent (payloader);

  gst_object_unref (payloader);
  gst_object_unref (GST_OBJECT (srcpad));

  return TRUE;

no_sinkpad:
  GST_ERROR_OBJECT (sink,
      "Could not find sink pad on payloader %" GST_PTR_FORMAT, payloader);
  if (!cspad->custom_payloader)
    gst_object_unref (payloader);
  return FALSE;

no_srcpad:
  GST_ERROR_OBJECT (sink,
      "Could not find src pad on payloader %" GST_PTR_FORMAT, payloader);
  gst_object_unref (GST_OBJECT (sinkpad));
  gst_object_unref (payloader);
  return TRUE;
}

static gboolean
gst_rtsp_client_sink_sinkpad_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_CAPS) {
    GstPad *target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
    if (target == NULL) {
      GstCaps *caps;

      /* No target yet - choose a payloader and configure it */
      gst_event_parse_caps (event, &caps);

      GST_DEBUG_OBJECT (parent,
          "Have set caps event on pad %" GST_PTR_FORMAT
          " caps %" GST_PTR_FORMAT, pad, caps);

      if (!gst_rtsp_client_sink_setup_payloader (GST_RTSP_CLIENT_SINK (parent),
              pad, caps)) {
        GstRtspClientSinkPad *cspad = GST_RTSP_CLIENT_SINK_PAD (pad);
        GST_ELEMENT_ERROR (parent, CORE, NEGOTIATION,
            ("Could not create payloader"),
            ("Custom payloader: %p, caps: %" GST_PTR_FORMAT,
                cspad->custom_payloader, caps));
        gst_event_unref (event);
        return FALSE;
      }
    } else {
      gst_object_unref (target);
    }
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_rtsp_client_sink_sinkpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  if (GST_QUERY_TYPE (query) == GST_QUERY_CAPS) {
    GstPad *target = gst_ghost_pad_get_target (GST_GHOST_PAD (pad));
    if (target == NULL) {
      GstRtspClientSinkPad *cspad = GST_RTSP_CLIENT_SINK_PAD (pad);
      GstCaps *caps;

      if (cspad->custom_payloader) {
        GstPad *sinkpad =
            gst_element_get_static_pad (cspad->custom_payloader, "sink");

        if (sinkpad) {
          caps = gst_pad_query_caps (sinkpad, NULL);
          gst_object_unref (sinkpad);
        } else {
          GST_ELEMENT_ERROR (parent, CORE, NEGOTIATION, (NULL),
              ("Custom payloaders are expected to expose a sink pad named 'sink'"));
          return FALSE;
        }
      } else {
        /* No target yet - return the union of all payloader caps */
        caps = gst_rtsp_client_sink_get_all_payloaders_caps ();
      }

      GST_TRACE_OBJECT (parent, "Returning payloader caps %" GST_PTR_FORMAT,
          caps);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);

      return TRUE;
    }
    gst_object_unref (target);
  }

  return gst_pad_query_default (pad, parent, query);
}

static GstPad *
gst_rtsp_client_sink_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstRTSPClientSink *sink = GST_RTSP_CLIENT_SINK (element);
  GstPad *pad;
  GstRTSPStreamContext *context;
  guint idx = (guint) - 1;
  gchar *tmpname;

  g_mutex_lock (&sink->preroll_lock);
  if (sink->streams_collected) {
    GST_WARNING_OBJECT (element, "Can't add streams to a running session");
    g_mutex_unlock (&sink->preroll_lock);
    return NULL;
  }
  g_mutex_unlock (&sink->preroll_lock);

  GST_OBJECT_LOCK (sink);
  if (name) {
    if (!sscanf (name, "sink_%u", &idx)) {
      GST_OBJECT_UNLOCK (sink);
      GST_ERROR_OBJECT (element, "Invalid sink pad name %s", name);
      return NULL;
    }

    if (idx >= sink->next_pad_id)
      sink->next_pad_id = idx + 1;
  }
  if (idx == (guint) - 1) {
    idx = sink->next_pad_id;
    sink->next_pad_id++;
  }
  GST_OBJECT_UNLOCK (sink);

  tmpname = g_strdup_printf ("sink_%u", idx);
  pad = gst_rtsp_client_sink_pad_new (templ, tmpname);
  g_free (tmpname);

  GST_DEBUG_OBJECT (element, "Creating request pad %" GST_PTR_FORMAT, pad);

  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR (gst_rtsp_client_sink_sinkpad_event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_rtsp_client_sink_sinkpad_query));

  context = g_new0 (GstRTSPStreamContext, 1);
  context->parent = sink;
  context->index = idx;

  gst_pad_set_element_private (pad, context);

  /* The rest of the context is configured on a caps set */
  gst_pad_set_active (pad, TRUE);
  gst_element_add_pad (element, pad);
  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_PAD_NAME (pad));

  (void) gst_rtsp_client_sink_get_factories ();

  g_mutex_init (&context->conninfo.send_lock);
  g_mutex_init (&context->conninfo.recv_lock);

  GST_RTSP_STATE_LOCK (sink);
  sink->contexts = g_list_prepend (sink->contexts, context);
  GST_RTSP_STATE_UNLOCK (sink);

  return pad;
}

static void
gst_rtsp_client_sink_release_pad (GstElement * element, GstPad * pad)
{
  GstRTSPClientSink *sink = GST_RTSP_CLIENT_SINK (element);
  GstRTSPStreamContext *context;

  context = gst_pad_get_element_private (pad);

  /* FIXME: we may need to change our blocking state waiting for
   * GstRTSPStreamBlocking messages */

  GST_RTSP_STATE_LOCK (sink);
  sink->contexts = g_list_remove (sink->contexts, context);
  GST_RTSP_STATE_UNLOCK (sink);

  /* FIXME: Shut down and clean up streaming on this pad,
   * do teardown if needed */
  GST_LOG_OBJECT (sink,
      "Cleaning up payloader and stream for released pad %" GST_PTR_FORMAT,
      pad);

  if (context->stream_transport) {
    gst_rtsp_stream_transport_set_active (context->stream_transport, FALSE);
    gst_object_unref (context->stream_transport);
    context->stream_transport = NULL;
  }
  if (context->stream) {
    if (context->joined) {
      gst_rtsp_stream_leave_bin (context->stream,
          GST_BIN (sink->internal_bin), sink->rtpbin);
      context->joined = FALSE;
    }
    gst_object_unref (context->stream);
    context->stream = NULL;
  }
  if (context->srtcpparams)
    gst_caps_unref (context->srtcpparams);

  g_free (context->conninfo.location);
  context->conninfo.location = NULL;

  g_mutex_clear (&context->conninfo.send_lock);
  g_mutex_clear (&context->conninfo.recv_lock);

  g_free (context);

  gst_element_remove_pad (element, pad);
}

static GstClock *
gst_rtsp_client_sink_provide_clock (GstElement * element)
{
  GstRTSPClientSink *sink = GST_RTSP_CLIENT_SINK (element);
  GstClock *clock;

  if ((clock = sink->provided_clock) != NULL)
    gst_object_ref (clock);

  return clock;
}

/* a proxy string of the format [user:passwd@]host[:port] */
static gboolean
gst_rtsp_client_sink_set_proxy (GstRTSPClientSink * rtsp, const gchar * proxy)
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
  } else {
    if (rtsp->prop_proxy_id != NULL && *rtsp->prop_proxy_id != '\0')
      rtsp->proxy_user = g_strdup (rtsp->prop_proxy_id);
    if (rtsp->prop_proxy_pw != NULL && *rtsp->prop_proxy_pw != '\0')
      rtsp->proxy_passwd = g_strdup (rtsp->prop_proxy_pw);
    if (rtsp->proxy_user != NULL || rtsp->proxy_passwd != NULL) {
      GST_LOG_OBJECT (rtsp, "set proxy user/pw from properties: %s:%s",
          GST_STR_NULL (rtsp->proxy_user), GST_STR_NULL (rtsp->proxy_passwd));
    }
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
gst_rtsp_client_sink_set_tcp_timeout (GstRTSPClientSink * rtsp_client_sink,
    guint64 timeout)
{
  rtsp_client_sink->tcp_timeout = timeout;
}

static void
gst_rtsp_client_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTSPClientSink *rtsp_client_sink;

  rtsp_client_sink = GST_RTSP_CLIENT_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      gst_rtsp_client_sink_uri_set_uri (GST_URI_HANDLER (rtsp_client_sink),
          g_value_get_string (value), NULL);
      break;
    case PROP_PROTOCOLS:
      rtsp_client_sink->protocols = g_value_get_flags (value);
      break;
    case PROP_PROFILES:
      rtsp_client_sink->profiles = g_value_get_flags (value);
      break;
    case PROP_DEBUG:
      rtsp_client_sink->debug = g_value_get_boolean (value);
      break;
    case PROP_RETRY:
      rtsp_client_sink->retry = g_value_get_uint (value);
      break;
    case PROP_TIMEOUT:
      rtsp_client_sink->udp_timeout = g_value_get_uint64 (value);
      break;
    case PROP_TCP_TIMEOUT:
      gst_rtsp_client_sink_set_tcp_timeout (rtsp_client_sink,
          g_value_get_uint64 (value));
      break;
    case PROP_LATENCY:
      rtsp_client_sink->latency = g_value_get_uint (value);
      break;
    case PROP_RTX_TIME:
      rtsp_client_sink->rtx_time = g_value_get_uint (value);
      break;
    case PROP_DO_RTSP_KEEP_ALIVE:
      rtsp_client_sink->do_rtsp_keep_alive = g_value_get_boolean (value);
      break;
    case PROP_PROXY:
      gst_rtsp_client_sink_set_proxy (rtsp_client_sink,
          g_value_get_string (value));
      break;
    case PROP_PROXY_ID:
      if (rtsp_client_sink->prop_proxy_id)
        g_free (rtsp_client_sink->prop_proxy_id);
      rtsp_client_sink->prop_proxy_id = g_value_dup_string (value);
      break;
    case PROP_PROXY_PW:
      if (rtsp_client_sink->prop_proxy_pw)
        g_free (rtsp_client_sink->prop_proxy_pw);
      rtsp_client_sink->prop_proxy_pw = g_value_dup_string (value);
      break;
    case PROP_RTP_BLOCKSIZE:
      rtsp_client_sink->rtp_blocksize = g_value_get_uint (value);
      break;
    case PROP_USER_ID:
      if (rtsp_client_sink->user_id)
        g_free (rtsp_client_sink->user_id);
      rtsp_client_sink->user_id = g_value_dup_string (value);
      break;
    case PROP_USER_PW:
      if (rtsp_client_sink->user_pw)
        g_free (rtsp_client_sink->user_pw);
      rtsp_client_sink->user_pw = g_value_dup_string (value);
      break;
    case PROP_PORT_RANGE:
    {
      const gchar *str;

      str = g_value_get_string (value);
      if (!str || !sscanf (str, "%u-%u",
              &rtsp_client_sink->client_port_range.min,
              &rtsp_client_sink->client_port_range.max)) {
        rtsp_client_sink->client_port_range.min = 0;
        rtsp_client_sink->client_port_range.max = 0;
      }
      break;
    }
    case PROP_UDP_BUFFER_SIZE:
      rtsp_client_sink->udp_buffer_size = g_value_get_int (value);
      break;
    case PROP_UDP_RECONNECT:
      rtsp_client_sink->udp_reconnect = g_value_get_boolean (value);
      break;
    case PROP_MULTICAST_IFACE:
      g_free (rtsp_client_sink->multi_iface);

      if (g_value_get_string (value) == NULL)
        rtsp_client_sink->multi_iface = g_strdup (DEFAULT_MULTICAST_IFACE);
      else
        rtsp_client_sink->multi_iface = g_value_dup_string (value);
      break;
    case PROP_SDES:
      rtsp_client_sink->sdes = g_value_dup_boxed (value);
      break;
    case PROP_TLS_VALIDATION_FLAGS:
      rtsp_client_sink->tls_validation_flags = g_value_get_flags (value);
      break;
    case PROP_TLS_DATABASE:
      g_clear_object (&rtsp_client_sink->tls_database);
      rtsp_client_sink->tls_database = g_value_dup_object (value);
      break;
    case PROP_TLS_INTERACTION:
      g_clear_object (&rtsp_client_sink->tls_interaction);
      rtsp_client_sink->tls_interaction = g_value_dup_object (value);
      break;
    case PROP_NTP_TIME_SOURCE:
      rtsp_client_sink->ntp_time_source = g_value_get_enum (value);
      break;
    case PROP_USER_AGENT:
      g_free (rtsp_client_sink->user_agent);
      rtsp_client_sink->user_agent = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtsp_client_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTSPClientSink *rtsp_client_sink;

  rtsp_client_sink = GST_RTSP_CLIENT_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, rtsp_client_sink->conninfo.location);
      break;
    case PROP_PROTOCOLS:
      g_value_set_flags (value, rtsp_client_sink->protocols);
      break;
    case PROP_PROFILES:
      g_value_set_flags (value, rtsp_client_sink->profiles);
      break;
    case PROP_DEBUG:
      g_value_set_boolean (value, rtsp_client_sink->debug);
      break;
    case PROP_RETRY:
      g_value_set_uint (value, rtsp_client_sink->retry);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, rtsp_client_sink->udp_timeout);
      break;
    case PROP_TCP_TIMEOUT:
      g_value_set_uint64 (value, rtsp_client_sink->tcp_timeout);
      break;
    case PROP_LATENCY:
      g_value_set_uint (value, rtsp_client_sink->latency);
      break;
    case PROP_RTX_TIME:
      g_value_set_uint (value, rtsp_client_sink->rtx_time);
      break;
    case PROP_DO_RTSP_KEEP_ALIVE:
      g_value_set_boolean (value, rtsp_client_sink->do_rtsp_keep_alive);
      break;
    case PROP_PROXY:
    {
      gchar *str;

      if (rtsp_client_sink->proxy_host) {
        str =
            g_strdup_printf ("%s:%d", rtsp_client_sink->proxy_host,
            rtsp_client_sink->proxy_port);
      } else {
        str = NULL;
      }
      g_value_take_string (value, str);
      break;
    }
    case PROP_PROXY_ID:
      g_value_set_string (value, rtsp_client_sink->prop_proxy_id);
      break;
    case PROP_PROXY_PW:
      g_value_set_string (value, rtsp_client_sink->prop_proxy_pw);
      break;
    case PROP_RTP_BLOCKSIZE:
      g_value_set_uint (value, rtsp_client_sink->rtp_blocksize);
      break;
    case PROP_USER_ID:
      g_value_set_string (value, rtsp_client_sink->user_id);
      break;
    case PROP_USER_PW:
      g_value_set_string (value, rtsp_client_sink->user_pw);
      break;
    case PROP_PORT_RANGE:
    {
      gchar *str;

      if (rtsp_client_sink->client_port_range.min != 0) {
        str = g_strdup_printf ("%u-%u", rtsp_client_sink->client_port_range.min,
            rtsp_client_sink->client_port_range.max);
      } else {
        str = NULL;
      }
      g_value_take_string (value, str);
      break;
    }
    case PROP_UDP_BUFFER_SIZE:
      g_value_set_int (value, rtsp_client_sink->udp_buffer_size);
      break;
    case PROP_UDP_RECONNECT:
      g_value_set_boolean (value, rtsp_client_sink->udp_reconnect);
      break;
    case PROP_MULTICAST_IFACE:
      g_value_set_string (value, rtsp_client_sink->multi_iface);
      break;
    case PROP_SDES:
      g_value_set_boxed (value, rtsp_client_sink->sdes);
      break;
    case PROP_TLS_VALIDATION_FLAGS:
      g_value_set_flags (value, rtsp_client_sink->tls_validation_flags);
      break;
    case PROP_TLS_DATABASE:
      g_value_set_object (value, rtsp_client_sink->tls_database);
      break;
    case PROP_TLS_INTERACTION:
      g_value_set_object (value, rtsp_client_sink->tls_interaction);
      break;
    case PROP_NTP_TIME_SOURCE:
      g_value_set_enum (value, rtsp_client_sink->ntp_time_source);
      break;
    case PROP_USER_AGENT:
      g_value_set_string (value, rtsp_client_sink->user_agent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static const gchar *
get_aggregate_control (GstRTSPClientSink * sink)
{
  const gchar *base;

  if (sink->control)
    base = sink->control;
  else if (sink->content_base)
    base = sink->content_base;
  else if (sink->conninfo.url_str)
    base = sink->conninfo.url_str;
  else
    base = "/";

  return base;
}

static void
gst_rtsp_client_sink_cleanup (GstRTSPClientSink * sink)
{
  GList *walk;

  GST_DEBUG_OBJECT (sink, "cleanup");

  gst_element_set_state (GST_ELEMENT (sink->internal_bin), GST_STATE_NULL);

  /* Clean up any left over stream objects */
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) (walk->data);
    if (context->stream_transport) {
      gst_rtsp_stream_transport_set_active (context->stream_transport, FALSE);
      gst_object_unref (context->stream_transport);
      context->stream_transport = NULL;
    }

    if (context->stream) {
      if (context->joined) {
        gst_rtsp_stream_leave_bin (context->stream,
            GST_BIN (sink->internal_bin), sink->rtpbin);
        context->joined = FALSE;
      }
      gst_object_unref (context->stream);
      context->stream = NULL;
    }

    if (context->srtcpparams) {
      gst_caps_unref (context->srtcpparams);
      context->srtcpparams = NULL;
    }
    g_free (context->conninfo.location);
    context->conninfo.location = NULL;
  }

  if (sink->rtpbin) {
    gst_element_set_state (sink->rtpbin, GST_STATE_NULL);
    gst_bin_remove (GST_BIN_CAST (sink->internal_bin), sink->rtpbin);
    sink->rtpbin = NULL;
  }

  g_free (sink->content_base);
  sink->content_base = NULL;

  g_free (sink->control);
  sink->control = NULL;

  if (sink->range)
    gst_rtsp_range_free (sink->range);
  sink->range = NULL;

  /* don't clear the SDP when it was used in the url */
  if (sink->uri_sdp && !sink->from_sdp) {
    gst_sdp_message_free (sink->uri_sdp);
    sink->uri_sdp = NULL;
  }

  if (sink->provided_clock) {
    gst_object_unref (sink->provided_clock);
    sink->provided_clock = NULL;
  }

  g_free (sink->server_ip);
  sink->server_ip = NULL;

  sink->next_pad_id = 0;
  sink->next_dyn_pt = 96;
}

static GstRTSPResult
gst_rtsp_client_sink_connection_send (GstRTSPClientSink * sink,
    GstRTSPConnInfo * conninfo, GstRTSPMessage * message, gint64 timeout)
{
  GstRTSPResult ret;

  if (conninfo->connection) {
    g_mutex_lock (&conninfo->send_lock);
    ret =
        gst_rtsp_connection_send_usec (conninfo->connection, message, timeout);
    g_mutex_unlock (&conninfo->send_lock);
  } else {
    ret = GST_RTSP_ERROR;
  }

  return ret;
}

static GstRTSPResult
gst_rtsp_client_sink_connection_send_messages (GstRTSPClientSink * sink,
    GstRTSPConnInfo * conninfo, GstRTSPMessage * messages, guint n_messages,
    gint64 timeout)
{
  GstRTSPResult ret;

  if (conninfo->connection) {
    g_mutex_lock (&conninfo->send_lock);
    ret =
        gst_rtsp_connection_send_messages_usec (conninfo->connection, messages,
        n_messages, timeout);
    g_mutex_unlock (&conninfo->send_lock);
  } else {
    ret = GST_RTSP_ERROR;
  }

  return ret;
}

static GstRTSPResult
gst_rtsp_client_sink_connection_receive (GstRTSPClientSink * sink,
    GstRTSPConnInfo * conninfo, GstRTSPMessage * message, gint64 timeout)
{
  GstRTSPResult ret;

  if (conninfo->connection) {
    g_mutex_lock (&conninfo->recv_lock);
    ret = gst_rtsp_connection_receive_usec (conninfo->connection, message,
        timeout);
    g_mutex_unlock (&conninfo->recv_lock);
  } else {
    ret = GST_RTSP_ERROR;
  }

  return ret;
}

static gboolean
accept_certificate_cb (GTlsConnection * conn, GTlsCertificate * peer_cert,
    GTlsCertificateFlags errors, gpointer user_data)
{
  GstRTSPClientSink *sink = user_data;
  gboolean accept = FALSE;

  g_signal_emit (sink, gst_rtsp_client_sink_signals[SIGNAL_ACCEPT_CERTIFICATE],
      0, conn, peer_cert, errors, &accept);

  return accept;
}

static GstRTSPResult
gst_rtsp_conninfo_connect (GstRTSPClientSink * sink, GstRTSPConnInfo * info,
    gboolean async)
{
  GstRTSPResult res;

  if (info->connection == NULL) {
    if (info->url == NULL) {
      GST_DEBUG_OBJECT (sink, "parsing uri (%s)...", info->location);
      if ((res = gst_rtsp_url_parse (info->location, &info->url)) < 0)
        goto parse_error;
    }

    /* create connection */
    GST_DEBUG_OBJECT (sink, "creating connection (%s)...", info->location);
    if ((res = gst_rtsp_connection_create (info->url, &info->connection)) < 0)
      goto could_not_create;

    if (info->url_str)
      g_free (info->url_str);
    info->url_str = gst_rtsp_url_get_request_uri (info->url);

    GST_DEBUG_OBJECT (sink, "sanitized uri %s", info->url_str);

    if (info->url->transports & GST_RTSP_LOWER_TRANS_TLS) {
      if (!gst_rtsp_connection_set_tls_validation_flags (info->connection,
              sink->tls_validation_flags))
        GST_WARNING_OBJECT (sink, "Unable to set TLS validation flags");

      if (sink->tls_database)
        gst_rtsp_connection_set_tls_database (info->connection,
            sink->tls_database);

      if (sink->tls_interaction)
        gst_rtsp_connection_set_tls_interaction (info->connection,
            sink->tls_interaction);

      gst_rtsp_connection_set_accept_certificate_func (info->connection,
          accept_certificate_cb, sink, NULL);
    }

    if (info->url->transports & GST_RTSP_LOWER_TRANS_HTTP)
      gst_rtsp_connection_set_tunneled (info->connection, TRUE);

    if (sink->proxy_host) {
      GST_DEBUG_OBJECT (sink, "setting proxy %s:%d", sink->proxy_host,
          sink->proxy_port);
      gst_rtsp_connection_set_proxy (info->connection, sink->proxy_host,
          sink->proxy_port);
    }
  }

  if (!info->connected) {
    /* connect */
    if (async)
      GST_ELEMENT_PROGRESS (sink, CONTINUE, "connect",
          ("Connecting to %s", info->location));
    GST_DEBUG_OBJECT (sink, "connecting (%s)...", info->location);
    if ((res =
            gst_rtsp_connection_connect_usec (info->connection,
                sink->tcp_timeout)) < 0)
      goto could_not_connect;

    info->connected = TRUE;
  }
  return GST_RTSP_OK;

  /* ERRORS */
parse_error:
  {
    GST_ERROR_OBJECT (sink, "No valid RTSP URL was provided");
    return res;
  }
could_not_create:
  {
    gchar *str = gst_rtsp_strresult (res);
    GST_ERROR_OBJECT (sink, "Could not create connection. (%s)", str);
    g_free (str);
    return res;
  }
could_not_connect:
  {
    gchar *str = gst_rtsp_strresult (res);
    GST_ERROR_OBJECT (sink, "Could not connect to server. (%s)", str);
    g_free (str);
    return res;
  }
}

static GstRTSPResult
gst_rtsp_conninfo_close (GstRTSPClientSink * sink, GstRTSPConnInfo * info,
    gboolean free)
{
  GST_RTSP_STATE_LOCK (sink);
  if (info->connected) {
    GST_DEBUG_OBJECT (sink, "closing connection...");
    gst_rtsp_connection_close (info->connection);
    info->connected = FALSE;
  }
  if (free && info->connection) {
    /* free connection */
    GST_DEBUG_OBJECT (sink, "freeing connection...");
    gst_rtsp_connection_free (info->connection);
    g_mutex_lock (&sink->preroll_lock);
    info->connection = NULL;
    g_cond_broadcast (&sink->preroll_cond);
    g_mutex_unlock (&sink->preroll_lock);
  }
  GST_RTSP_STATE_UNLOCK (sink);
  return GST_RTSP_OK;
}

static GstRTSPResult
gst_rtsp_conninfo_reconnect (GstRTSPClientSink * sink, GstRTSPConnInfo * info,
    gboolean async)
{
  GstRTSPResult res;

  GST_DEBUG_OBJECT (sink, "reconnecting connection...");
  gst_rtsp_conninfo_close (sink, info, FALSE);
  res = gst_rtsp_conninfo_connect (sink, info, async);

  return res;
}

static void
gst_rtsp_client_sink_connection_flush (GstRTSPClientSink * sink, gboolean flush)
{
  GList *walk;

  GST_DEBUG_OBJECT (sink, "set flushing %d", flush);
  g_mutex_lock (&sink->preroll_lock);
  if (sink->conninfo.connection && sink->conninfo.flushing != flush) {
    GST_DEBUG_OBJECT (sink, "connection flush");
    gst_rtsp_connection_flush (sink->conninfo.connection, flush);
    sink->conninfo.flushing = flush;
  }
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *stream = (GstRTSPStreamContext *) walk->data;
    if (stream->conninfo.connection && stream->conninfo.flushing != flush) {
      GST_DEBUG_OBJECT (sink, "stream %p flush", stream);
      gst_rtsp_connection_flush (stream->conninfo.connection, flush);
      stream->conninfo.flushing = flush;
    }
  }
  g_cond_broadcast (&sink->preroll_cond);
  g_mutex_unlock (&sink->preroll_lock);
}

static GstRTSPResult
gst_rtsp_client_sink_init_request (GstRTSPClientSink * sink,
    GstRTSPMessage * msg, GstRTSPMethod method, const gchar * uri)
{
  GstRTSPResult res;

  res = gst_rtsp_message_init_request (msg, method, uri);
  if (res < 0)
    return res;

  /* set user-agent */
  if (sink->user_agent)
    gst_rtsp_message_add_header (msg, GST_RTSP_HDR_USER_AGENT,
        sink->user_agent);

  return res;
}

/* FIXME, handle server request, reply with OK, for now */
static GstRTSPResult
gst_rtsp_client_sink_handle_request (GstRTSPClientSink * sink,
    GstRTSPConnInfo * conninfo, GstRTSPMessage * request)
{
  GstRTSPMessage response = { 0 };
  GstRTSPResult res;

  GST_DEBUG_OBJECT (sink, "got server request message");

  if (sink->debug)
    gst_rtsp_message_dump (request);

  /* default implementation, send OK */
  GST_DEBUG_OBJECT (sink, "prepare OK reply");
  res =
      gst_rtsp_message_init_response (&response, GST_RTSP_STS_OK, "OK",
      request);
  if (res < 0)
    goto send_error;

  /* let app parse and reply */
  g_signal_emit (sink, gst_rtsp_client_sink_signals[SIGNAL_HANDLE_REQUEST],
      0, request, &response);

  if (sink->debug)
    gst_rtsp_message_dump (&response);

  res = gst_rtsp_client_sink_connection_send (sink, conninfo, &response, 0);
  if (res < 0)
    goto send_error;

  gst_rtsp_message_unset (&response);

  return GST_RTSP_OK;

  /* ERRORS */
send_error:
  {
    gst_rtsp_message_unset (&response);
    return res;
  }
}

/* send server keep-alive */
static GstRTSPResult
gst_rtsp_client_sink_send_keep_alive (GstRTSPClientSink * sink)
{
  GstRTSPMessage request = { 0 };
  GstRTSPResult res;
  GstRTSPMethod method;
  const gchar *control;

  if (sink->do_rtsp_keep_alive == FALSE) {
    GST_DEBUG_OBJECT (sink, "do-rtsp-keep-alive is FALSE, not sending.");
    gst_rtsp_connection_reset_timeout (sink->conninfo.connection);
    return GST_RTSP_OK;
  }

  GST_DEBUG_OBJECT (sink, "creating server keep-alive");

  /* find a method to use for keep-alive */
  if (sink->methods & GST_RTSP_GET_PARAMETER)
    method = GST_RTSP_GET_PARAMETER;
  else
    method = GST_RTSP_OPTIONS;

  control = get_aggregate_control (sink);
  if (control == NULL)
    goto no_control;

  res = gst_rtsp_client_sink_init_request (sink, &request, method, control);
  if (res < 0)
    goto send_error;

  if (sink->debug)
    gst_rtsp_message_dump (&request);

  res =
      gst_rtsp_client_sink_connection_send (sink, &sink->conninfo, &request, 0);
  if (res < 0)
    goto send_error;

  gst_rtsp_connection_reset_timeout (sink->conninfo.connection);
  gst_rtsp_message_unset (&request);

  return GST_RTSP_OK;

  /* ERRORS */
no_control:
  {
    GST_WARNING_OBJECT (sink, "no control url to send keepalive");
    return GST_RTSP_OK;
  }
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    gst_rtsp_message_unset (&request);
    GST_ELEMENT_WARNING (sink, RESOURCE, WRITE, (NULL),
        ("Could not send keep-alive. (%s)", str));
    g_free (str);
    return res;
  }
}

static GstFlowReturn
gst_rtsp_client_sink_loop_rx (GstRTSPClientSink * sink)
{
  GstRTSPResult res;
  GstRTSPMessage message = { 0 };
  gint retry = 0;

  while (TRUE) {
    gint64 timeout;

    /* get the next timeout interval */
    timeout = gst_rtsp_connection_next_timeout_usec (sink->conninfo.connection);

    GST_DEBUG_OBJECT (sink, "doing receive with timeout %d seconds",
        (gint) timeout / G_USEC_PER_SEC);

    gst_rtsp_message_unset (&message);

    /* we should continue reading the TCP socket because the server might
     * send us requests. When the session timeout expires, we need to send a
     * keep-alive request to keep the session open. */
    res =
        gst_rtsp_client_sink_connection_receive (sink,
        &sink->conninfo, &message, timeout);

    switch (res) {
      case GST_RTSP_OK:
        GST_DEBUG_OBJECT (sink, "we received a server message");
        break;
      case GST_RTSP_EINTR:
        /* we got interrupted, see what we have to do */
        goto interrupt;
      case GST_RTSP_ETIMEOUT:
        /* send keep-alive, ignore the result, a warning will be posted. */
        GST_DEBUG_OBJECT (sink, "timeout, sending keep-alive");
        if ((res =
                gst_rtsp_client_sink_send_keep_alive (sink)) == GST_RTSP_EINTR)
          goto interrupt;
        continue;
      case GST_RTSP_EEOF:
        /* server closed the connection. not very fatal for UDP, reconnect and
         * see what happens. */
        GST_ELEMENT_WARNING (sink, RESOURCE, READ, (NULL),
            ("The server closed the connection."));
        if (sink->udp_reconnect) {
          if ((res =
                  gst_rtsp_conninfo_reconnect (sink, &sink->conninfo,
                      FALSE)) < 0)
            goto connect_error;
        } else {
          goto server_eof;
        }
        continue;
        break;
      case GST_RTSP_ENET:
        GST_DEBUG_OBJECT (sink, "An ethernet problem occured.");
      default:
        GST_ELEMENT_WARNING (sink, RESOURCE, READ, (NULL),
            ("Unhandled return value %d.", res));
        goto receive_error;
    }

    switch (message.type) {
      case GST_RTSP_MESSAGE_REQUEST:
        /* server sends us a request message, handle it */
        res =
            gst_rtsp_client_sink_handle_request (sink,
            &sink->conninfo, &message);
        if (res == GST_RTSP_EEOF)
          goto server_eof;
        else if (res < 0)
          goto handle_request_failed;
        break;
      case GST_RTSP_MESSAGE_RESPONSE:
        /* we ignore response and data messages */
        GST_DEBUG_OBJECT (sink, "ignoring response message");
        if (sink->debug)
          gst_rtsp_message_dump (&message);
        if (message.type_data.response.code == GST_RTSP_STS_UNAUTHORIZED) {
          GST_DEBUG_OBJECT (sink, "but is Unauthorized response ...");
          if (gst_rtsp_client_sink_setup_auth (sink, &message) && !(retry++)) {
            GST_DEBUG_OBJECT (sink, "so retrying keep-alive");
            if ((res =
                    gst_rtsp_client_sink_send_keep_alive (sink)) ==
                GST_RTSP_EINTR)
              goto interrupt;
          }
        } else {
          retry = 0;
        }
        break;
      case GST_RTSP_MESSAGE_DATA:
        /* we ignore response and data messages */
        GST_DEBUG_OBJECT (sink, "ignoring data message");
        break;
      default:
        GST_WARNING_OBJECT (sink, "ignoring unknown message type %d",
            message.type);
        break;
    }
  }
  g_assert_not_reached ();

  /* we get here when the connection got interrupted */
interrupt:
  {
    gst_rtsp_message_unset (&message);
    GST_DEBUG_OBJECT (sink, "got interrupted");
    return GST_FLOW_FLUSHING;
  }
connect_error:
  {
    gchar *str = gst_rtsp_strresult (res);
    GstFlowReturn ret;

    sink->conninfo.connected = FALSE;
    if (res != GST_RTSP_EINTR) {
      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ_WRITE, (NULL),
          ("Could not connect to server. (%s)", str));
      g_free (str);
      ret = GST_FLOW_ERROR;
    } else {
      ret = GST_FLOW_FLUSHING;
    }
    return ret;
  }
receive_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
        ("Could not receive message. (%s)", str));
    g_free (str);
    return GST_FLOW_ERROR;
  }
handle_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);
    GstFlowReturn ret;

    gst_rtsp_message_unset (&message);
    if (res != GST_RTSP_EINTR) {
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
          ("Could not handle server message. (%s)", str));
      g_free (str);
      ret = GST_FLOW_ERROR;
    } else {
      ret = GST_FLOW_FLUSHING;
    }
    return ret;
  }
server_eof:
  {
    GST_DEBUG_OBJECT (sink, "we got an eof from the server");
    GST_ELEMENT_WARNING (sink, RESOURCE, READ, (NULL),
        ("The server closed the connection."));
    sink->conninfo.connected = FALSE;
    gst_rtsp_message_unset (&message);
    return GST_FLOW_EOS;
  }
}

static GstRTSPResult
gst_rtsp_client_sink_reconnect (GstRTSPClientSink * sink, gboolean async)
{
  GstRTSPResult res = GST_RTSP_OK;
  gboolean restart = FALSE;

  GST_DEBUG_OBJECT (sink, "doing reconnect");

  GST_FIXME_OBJECT (sink, "Reconnection is not yet implemented");

  /* no need to restart, we're done */
  if (!restart)
    goto done;

  /* we can try only TCP now */
  sink->cur_protocols = GST_RTSP_LOWER_TRANS_TCP;

  /* close and cleanup our state */
  if ((res = gst_rtsp_client_sink_close (sink, async, FALSE)) < 0)
    goto done;

  /* see if we have TCP left to try. Also don't try TCP when we were configured
   * with an SDP. */
  if (!(sink->protocols & GST_RTSP_LOWER_TRANS_TCP) || sink->from_sdp)
    goto no_protocols;

  /* We post a warning message now to inform the user
   * that nothing happened. It's most likely a firewall thing. */
  GST_ELEMENT_WARNING (sink, RESOURCE, READ, (NULL),
      ("Could not receive any UDP packets for %.4f seconds, maybe your "
          "firewall is blocking it. Retrying using a TCP connection.",
          gst_guint64_to_gdouble (sink->udp_timeout / 1000000.0)));

  /* open new connection using tcp */
  if (gst_rtsp_client_sink_open (sink, async) < 0)
    goto open_failed;

  /* start recording */
  if (gst_rtsp_client_sink_record (sink, async) < 0)
    goto play_failed;

done:
  return res;

  /* ERRORS */
no_protocols:
  {
    sink->cur_protocols = 0;
    /* no transport possible, post an error and stop */
    GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
        ("Could not receive any UDP packets for %.4f seconds, maybe your "
            "firewall is blocking it. No other protocols to try.",
            gst_guint64_to_gdouble (sink->udp_timeout / 1000000.0)));
    return GST_RTSP_ERROR;
  }
open_failed:
  {
    GST_DEBUG_OBJECT (sink, "open failed");
    return GST_RTSP_OK;
  }
play_failed:
  {
    GST_DEBUG_OBJECT (sink, "play failed");
    return GST_RTSP_OK;
  }
}

static void
gst_rtsp_client_sink_loop_start_cmd (GstRTSPClientSink * sink, gint cmd)
{
  switch (cmd) {
    case CMD_OPEN:
      GST_ELEMENT_PROGRESS (sink, START, "open", ("Opening Stream"));
      break;
    case CMD_RECORD:
      GST_ELEMENT_PROGRESS (sink, START, "request", ("Sending RECORD request"));
      break;
    case CMD_PAUSE:
      GST_ELEMENT_PROGRESS (sink, START, "request", ("Sending PAUSE request"));
      break;
    case CMD_CLOSE:
      GST_ELEMENT_PROGRESS (sink, START, "close", ("Closing Stream"));
      break;
    default:
      break;
  }
}

static void
gst_rtsp_client_sink_loop_complete_cmd (GstRTSPClientSink * sink, gint cmd)
{
  switch (cmd) {
    case CMD_OPEN:
      GST_ELEMENT_PROGRESS (sink, COMPLETE, "open", ("Opened Stream"));
      break;
    case CMD_RECORD:
      GST_ELEMENT_PROGRESS (sink, COMPLETE, "request", ("Sent RECORD request"));
      break;
    case CMD_PAUSE:
      GST_ELEMENT_PROGRESS (sink, COMPLETE, "request", ("Sent PAUSE request"));
      break;
    case CMD_CLOSE:
      GST_ELEMENT_PROGRESS (sink, COMPLETE, "close", ("Closed Stream"));
      break;
    default:
      break;
  }
}

static void
gst_rtsp_client_sink_loop_cancel_cmd (GstRTSPClientSink * sink, gint cmd)
{
  switch (cmd) {
    case CMD_OPEN:
      GST_ELEMENT_PROGRESS (sink, CANCELED, "open", ("Open canceled"));
      break;
    case CMD_RECORD:
      GST_ELEMENT_PROGRESS (sink, CANCELED, "request", ("RECORD canceled"));
      break;
    case CMD_PAUSE:
      GST_ELEMENT_PROGRESS (sink, CANCELED, "request", ("PAUSE canceled"));
      break;
    case CMD_CLOSE:
      GST_ELEMENT_PROGRESS (sink, CANCELED, "close", ("Close canceled"));
      break;
    default:
      break;
  }
}

static void
gst_rtsp_client_sink_loop_error_cmd (GstRTSPClientSink * sink, gint cmd)
{
  switch (cmd) {
    case CMD_OPEN:
      GST_ELEMENT_PROGRESS (sink, ERROR, "open", ("Open failed"));
      break;
    case CMD_RECORD:
      GST_ELEMENT_PROGRESS (sink, ERROR, "request", ("RECORD failed"));
      break;
    case CMD_PAUSE:
      GST_ELEMENT_PROGRESS (sink, ERROR, "request", ("PAUSE failed"));
      break;
    case CMD_CLOSE:
      GST_ELEMENT_PROGRESS (sink, ERROR, "close", ("Close failed"));
      break;
    default:
      break;
  }
}

static void
gst_rtsp_client_sink_loop_end_cmd (GstRTSPClientSink * sink, gint cmd,
    GstRTSPResult ret)
{
  if (ret == GST_RTSP_OK)
    gst_rtsp_client_sink_loop_complete_cmd (sink, cmd);
  else if (ret == GST_RTSP_EINTR)
    gst_rtsp_client_sink_loop_cancel_cmd (sink, cmd);
  else
    gst_rtsp_client_sink_loop_error_cmd (sink, cmd);
}

static gboolean
gst_rtsp_client_sink_loop_send_cmd (GstRTSPClientSink * sink, gint cmd,
    gint mask)
{
  gint old;
  gboolean flushed = FALSE;

  /* start new request */
  gst_rtsp_client_sink_loop_start_cmd (sink, cmd);

  GST_DEBUG_OBJECT (sink, "sending cmd %s", cmd_to_string (cmd));

  GST_OBJECT_LOCK (sink);
  old = sink->pending_cmd;
  if (old == CMD_RECONNECT) {
    GST_DEBUG_OBJECT (sink, "ignore, we were reconnecting");
    cmd = CMD_RECONNECT;
  }
  if (old != CMD_WAIT) {
    sink->pending_cmd = CMD_WAIT;
    GST_OBJECT_UNLOCK (sink);
    /* cancel previous request */
    GST_DEBUG_OBJECT (sink, "cancel previous request %s", cmd_to_string (old));
    gst_rtsp_client_sink_loop_cancel_cmd (sink, old);
    GST_OBJECT_LOCK (sink);
  }
  sink->pending_cmd = cmd;
  /* interrupt if allowed */
  if (sink->busy_cmd & mask) {
    GST_DEBUG_OBJECT (sink, "connection flush busy %s",
        cmd_to_string (sink->busy_cmd));
    gst_rtsp_client_sink_connection_flush (sink, TRUE);
    flushed = TRUE;
  } else {
    GST_DEBUG_OBJECT (sink, "not interrupting busy cmd %s",
        cmd_to_string (sink->busy_cmd));
  }
  if (sink->task)
    gst_task_start (sink->task);
  GST_OBJECT_UNLOCK (sink);

  return flushed;
}

static gboolean
gst_rtsp_client_sink_loop (GstRTSPClientSink * sink)
{
  GstFlowReturn ret;

  if (!sink->conninfo.connection || !sink->conninfo.connected)
    goto no_connection;

  ret = gst_rtsp_client_sink_loop_rx (sink);
  if (ret != GST_FLOW_OK)
    goto pause;

  return TRUE;

  /* ERRORS */
no_connection:
  {
    GST_WARNING_OBJECT (sink, "we are not connected");
    ret = GST_FLOW_FLUSHING;
    goto pause;
  }
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (sink, "pausing task, reason %s", reason);
    gst_rtsp_client_sink_loop_send_cmd (sink, CMD_WAIT, CMD_LOOP);
    return FALSE;
  }
}

#ifndef GST_DISABLE_GST_DEBUG
static const gchar *
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

/* Parse a WWW-Authenticate Response header and determine the
 * available authentication methods
 *
 * This code should also cope with the fact that each WWW-Authenticate
 * header can contain multiple challenge methods + tokens
 *
 * At the moment, for Basic auth, we just do a minimal check and don't
 * even parse out the realm */
static void
gst_rtsp_client_sink_parse_auth_hdr (GstRTSPMessage * response,
    GstRTSPAuthMethod * methods, GstRTSPConnection * conn, gboolean * stale)
{
  GstRTSPAuthCredential **credentials, **credential;

  g_return_if_fail (response != NULL);
  g_return_if_fail (methods != NULL);
  g_return_if_fail (stale != NULL);

  credentials =
      gst_rtsp_message_parse_auth_credentials (response,
      GST_RTSP_HDR_WWW_AUTHENTICATE);
  if (!credentials)
    return;

  credential = credentials;
  while (*credential) {
    if ((*credential)->scheme == GST_RTSP_AUTH_BASIC) {
      *methods |= GST_RTSP_AUTH_BASIC;
    } else if ((*credential)->scheme == GST_RTSP_AUTH_DIGEST) {
      GstRTSPAuthParam **param = (*credential)->params;

      *methods |= GST_RTSP_AUTH_DIGEST;

      gst_rtsp_connection_clear_auth_params (conn);
      *stale = FALSE;

      while (*param) {
        if (strcmp ((*param)->name, "stale") == 0
            && g_ascii_strcasecmp ((*param)->value, "TRUE") == 0)
          *stale = TRUE;
        gst_rtsp_connection_set_auth_param (conn, (*param)->name,
            (*param)->value);
        param++;
      }
    }

    credential++;
  }

  gst_rtsp_auth_credentials_free (credentials);
}

/**
 * gst_rtsp_client_sink_setup_auth:
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
gst_rtsp_client_sink_setup_auth (GstRTSPClientSink * sink,
    GstRTSPMessage * response)
{
  gchar *user = NULL;
  gchar *pass = NULL;
  GstRTSPAuthMethod avail_methods = GST_RTSP_AUTH_NONE;
  GstRTSPAuthMethod method;
  GstRTSPResult auth_result;
  GstRTSPUrl *url;
  GstRTSPConnection *conn;
  gboolean stale = FALSE;

  conn = sink->conninfo.connection;

  /* Identify the available auth methods and see if any are supported */
  gst_rtsp_client_sink_parse_auth_hdr (response, &avail_methods, conn, &stale);

  if (avail_methods == GST_RTSP_AUTH_NONE)
    goto no_auth_available;

  /* For digest auth, if the response indicates that the session
   * data are stale, we just update them in the connection object and
   * return TRUE to retry the request */
  if (stale)
    sink->tried_url_auth = FALSE;

  url = gst_rtsp_connection_get_url (conn);

  /* Do we have username and password available? */
  if (url != NULL && !sink->tried_url_auth && url->user != NULL
      && url->passwd != NULL) {
    user = url->user;
    pass = url->passwd;
    sink->tried_url_auth = TRUE;
    GST_DEBUG_OBJECT (sink,
        "Attempting authentication using credentials from the URL");
  } else {
    user = sink->user_id;
    pass = sink->user_pw;
    GST_DEBUG_OBJECT (sink,
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
    auth_result = gst_rtsp_connection_set_auth (conn, method, user, pass);
    /* INVAL indicates an invalid username/passwd were supplied, so we'll just
     * ignore it and end up retrying later */
    if (auth_result == GST_RTSP_OK || auth_result == GST_RTSP_EINVAL) {
      GST_DEBUG_OBJECT (sink, "Attempting %s authentication",
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
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ, (NULL),
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
gst_rtsp_client_sink_try_send (GstRTSPClientSink * sink,
    GstRTSPConnInfo * conninfo, GstRTSPMessage * requests,
    guint n_requests, GstRTSPMessage * response, GstRTSPStatusCode * code)
{
  GstRTSPResult res;
  GstRTSPStatusCode thecode;
  gchar *content_base = NULL;
  gint try = 0;

  g_assert (n_requests == 1 || response == NULL);

again:
  GST_DEBUG_OBJECT (sink, "sending message");

  if (sink->debug && n_requests == 1)
    gst_rtsp_message_dump (&requests[0]);

  g_mutex_lock (&sink->send_lock);

  res =
      gst_rtsp_client_sink_connection_send_messages (sink, conninfo, requests,
      n_requests, sink->tcp_timeout);
  if (res < 0) {
    g_mutex_unlock (&sink->send_lock);
    goto send_error;
  }

  gst_rtsp_connection_reset_timeout (conninfo->connection);

  /* See if we should handle the response */
  if (response == NULL) {
    g_mutex_unlock (&sink->send_lock);
    return GST_RTSP_OK;
  }
next:
  res =
      gst_rtsp_client_sink_connection_receive (sink, conninfo, response,
      sink->tcp_timeout);

  g_mutex_unlock (&sink->send_lock);

  if (res < 0)
    goto receive_error;

  if (sink->debug)
    gst_rtsp_message_dump (response);


  switch (response->type) {
    case GST_RTSP_MESSAGE_REQUEST:
      res = gst_rtsp_client_sink_handle_request (sink, conninfo, response);
      if (res == GST_RTSP_EEOF)
        goto server_eof;
      else if (res < 0)
        goto handle_request_failed;
      g_mutex_lock (&sink->send_lock);
      goto next;
    case GST_RTSP_MESSAGE_RESPONSE:
      /* ok, a response is good */
      GST_DEBUG_OBJECT (sink, "received response message");
      break;
    case GST_RTSP_MESSAGE_DATA:
      /* we ignore data messages */
      GST_DEBUG_OBJECT (sink, "ignoring data message");
      g_mutex_lock (&sink->send_lock);
      goto next;
    default:
      GST_WARNING_OBJECT (sink, "ignoring unknown message type %d",
          response->type);
      g_mutex_lock (&sink->send_lock);
      goto next;
  }

  thecode = response->type_data.response.code;

  GST_DEBUG_OBJECT (sink, "got response message %d", thecode);

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
    g_free (sink->content_base);
    sink->content_base = g_strdup (content_base);
  }

  return GST_RTSP_OK;

  /* ERRORS */
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    if (res != GST_RTSP_EINTR) {
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
          ("Could not send message. (%s)", str));
    } else {
      GST_WARNING_OBJECT (sink, "send interrupted");
    }
    g_free (str);
    return res;
  }
receive_error:
  {
    switch (res) {
      case GST_RTSP_EEOF:
        GST_WARNING_OBJECT (sink, "server closed connection");
        if ((try == 0) && !sink->interleaved && sink->udp_reconnect) {
          try++;
          /* if reconnect succeeds, try again */
          if ((res =
                  gst_rtsp_conninfo_reconnect (sink, &sink->conninfo,
                      FALSE)) == 0)
            goto again;
        }
        /* only try once after reconnect, then fallthrough and error out */
      default:
      {
        gchar *str = gst_rtsp_strresult (res);

        if (res != GST_RTSP_EINTR) {
          GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
              ("Could not receive message. (%s)", str));
        } else {
          GST_WARNING_OBJECT (sink, "receive interrupted");
        }
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
    GST_DEBUG_OBJECT (sink, "we got an eof from the server");
    GST_ELEMENT_WARNING (sink, RESOURCE, READ, (NULL),
        ("The server closed the connection."));
    gst_rtsp_message_unset (response);
    return res;
  }
}

static void
gst_rtsp_client_sink_set_state (GstRTSPClientSink * sink, GstState state)
{
  GST_DEBUG_OBJECT (sink, "Setting internal state to %s",
      gst_element_state_get_name (state));
  gst_element_set_state (GST_ELEMENT (sink->internal_bin), state);
}

/**
 * gst_rtsp_client_sink_send:
 * @src: the rtsp source
 * @conn: the connection to send on
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
 * to retrieve authentication credentials via gst_rtsp_client_sink_setup_auth and retry
 * the request.
 *
 * Returns: #GST_RTSP_OK if the processing was successful.
 */
static GstRTSPResult
gst_rtsp_client_sink_send (GstRTSPClientSink * sink, GstRTSPConnInfo * conninfo,
    GstRTSPMessage * request, GstRTSPMessage * response,
    GstRTSPStatusCode * code)
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

    if ((res =
            gst_rtsp_client_sink_try_send (sink, conninfo, request, 1, response,
                &int_code)) < 0)
      goto error;

    switch (int_code) {
      case GST_RTSP_STS_UNAUTHORIZED:
        if (gst_rtsp_client_sink_setup_auth (sink, response)) {
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
    GST_DEBUG_OBJECT (sink, "got error %d", res);
    return res;
  }
error_response:
  {
    res = GST_RTSP_ERROR;

    switch (response->type_data.response.code) {
      case GST_RTSP_STS_NOT_FOUND:
        GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND, (NULL), ("%s",
                response->type_data.response.reason));
        break;
      case GST_RTSP_STS_UNAUTHORIZED:
        GST_ELEMENT_ERROR (sink, RESOURCE, NOT_AUTHORIZED, (NULL), ("%s",
                response->type_data.response.reason));
        break;
      case GST_RTSP_STS_MOVED_PERMANENTLY:
      case GST_RTSP_STS_MOVE_TEMPORARILY:
      {
        gchar *new_location;
        GstRTSPLowerTrans transports;

        GST_DEBUG_OBJECT (sink, "got redirection");
        /* if we don't have a Location Header, we must error */
        if (gst_rtsp_message_get_header (response, GST_RTSP_HDR_LOCATION,
                &new_location, 0) < 0)
          break;

        /* When we receive a redirect result, we go back to the INIT state after
         * parsing the new URI. The caller should do the needed steps to issue
         * a new setup when it detects this state change. */
        GST_DEBUG_OBJECT (sink, "redirection to %s", new_location);

        /* save current transports */
        if (sink->conninfo.url)
          transports = sink->conninfo.url->transports;
        else
          transports = GST_RTSP_LOWER_TRANS_UNKNOWN;

        gst_rtsp_client_sink_uri_set_uri (GST_URI_HANDLER (sink), new_location,
            NULL);

        /* set old transports */
        if (sink->conninfo.url && transports != GST_RTSP_LOWER_TRANS_UNKNOWN)
          sink->conninfo.url->transports = transports;

        sink->need_redirect = TRUE;
        sink->state = GST_RTSP_STATE_INIT;
        res = GST_RTSP_OK;
        break;
      }
      case GST_RTSP_STS_NOT_ACCEPTABLE:
      case GST_RTSP_STS_NOT_IMPLEMENTED:
      case GST_RTSP_STS_METHOD_NOT_ALLOWED:
        GST_WARNING_OBJECT (sink, "got NOT IMPLEMENTED, disable method %s",
            gst_rtsp_method_as_text (method));
        sink->methods &= ~method;
        res = GST_RTSP_OK;
        break;
      default:
        GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
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

/* parse the response and collect all the supported methods. We need this
 * information so that we don't try to send an unsupported request to the
 * server.
 */
static gboolean
gst_rtsp_client_sink_parse_methods (GstRTSPClientSink * sink,
    GstRTSPMessage * response)
{
  GstRTSPHeaderField field;
  gchar *respoptions;
  gint indx = 0;

  /* reset supported methods */
  sink->methods = 0;

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

    sink->methods |= gst_rtsp_options_from_text (respoptions);

    indx++;
  }

  if (sink->methods == 0) {
    /* neither Allow nor Public are required, assume the server supports
     * at least SETUP. */
    GST_DEBUG_OBJECT (sink, "could not get OPTIONS");
    sink->methods = GST_RTSP_SETUP;
  }

  /* Even if the server replied, and didn't say it supports
   * RECORD|ANNOUNCE, try anyway by assuming it does */
  sink->methods |= GST_RTSP_ANNOUNCE | GST_RTSP_RECORD;

  if (!(sink->methods & GST_RTSP_SETUP))
    goto no_setup;

  return TRUE;

  /* ERRORS */
no_setup:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ, (NULL),
        ("Server does not support SETUP."));
    return FALSE;
  }
}

static GstRTSPResult
gst_rtsp_client_sink_connect_to_server (GstRTSPClientSink * sink,
    gboolean async)
{
  GstRTSPResult res;
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GSocket *conn_socket;
  GSocketAddress *sa;
  GInetAddress *ia;

  sink->need_redirect = FALSE;

  /* can't continue without a valid url */
  if (G_UNLIKELY (sink->conninfo.url == NULL)) {
    res = GST_RTSP_EINVAL;
    goto no_url;
  }
  sink->tried_url_auth = FALSE;

  if ((res = gst_rtsp_conninfo_connect (sink, &sink->conninfo, async)) < 0)
    goto connect_failed;

  conn_socket = gst_rtsp_connection_get_read_socket (sink->conninfo.connection);
  sa = g_socket_get_remote_address (conn_socket, NULL);
  ia = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (sa));

  sink->server_ip = g_inet_address_to_string (ia);

  g_object_unref (sa);

  /* create OPTIONS */
  GST_DEBUG_OBJECT (sink, "create options...");
  res =
      gst_rtsp_client_sink_init_request (sink, &request, GST_RTSP_OPTIONS,
      sink->conninfo.url_str);
  if (res < 0)
    goto create_request_failed;

  /* send OPTIONS */
  GST_DEBUG_OBJECT (sink, "send options...");

  if (async)
    GST_ELEMENT_PROGRESS (sink, CONTINUE, "open",
        ("Retrieving server options"));

  if ((res =
          gst_rtsp_client_sink_send (sink, &sink->conninfo, &request,
              &response, NULL)) < 0)
    goto send_error;

  /* parse OPTIONS */
  if (!gst_rtsp_client_sink_parse_methods (sink, &response))
    goto methods_error;

  /* FIXME: Do we need to handle REDIRECT responses for OPTIONS? */

  /* clean up any messages */
  gst_rtsp_message_unset (&request);
  gst_rtsp_message_unset (&response);

  return res;

  /* ERRORS */
no_url:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND, (NULL),
        ("No valid RTSP URL was provided"));
    goto cleanup_error;
  }
connect_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    if (res != GST_RTSP_EINTR) {
      GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ_WRITE, (NULL),
          ("Failed to connect. (%s)", str));
    } else {
      GST_WARNING_OBJECT (sink, "connect interrupted");
    }
    g_free (str);
    goto cleanup_error;
  }
create_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
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
    res = GST_RTSP_ERROR;
    goto cleanup_error;
  }
cleanup_error:
  {
    if (sink->conninfo.connection) {
      GST_DEBUG_OBJECT (sink, "free connection");
      gst_rtsp_conninfo_close (sink, &sink->conninfo, TRUE);
    }
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);
    return res;
  }
}

static GstRTSPResult
gst_rtsp_client_sink_open (GstRTSPClientSink * sink, gboolean async)
{
  GstRTSPResult ret;

  sink->methods =
      GST_RTSP_SETUP | GST_RTSP_RECORD | GST_RTSP_PAUSE | GST_RTSP_TEARDOWN;

  g_mutex_lock (&sink->open_conn_lock);
  sink->open_conn_start = TRUE;
  g_cond_broadcast (&sink->open_conn_cond);
  GST_DEBUG_OBJECT (sink, "connection to server started");
  g_mutex_unlock (&sink->open_conn_lock);

  if ((ret = gst_rtsp_client_sink_connect_to_server (sink, async)) < 0)
    goto open_failed;

  if (async)
    gst_rtsp_client_sink_loop_end_cmd (sink, CMD_OPEN, ret);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_WARNING_OBJECT (sink, "Failed to connect to server");
    sink->open_error = TRUE;
    if (async)
      gst_rtsp_client_sink_loop_end_cmd (sink, CMD_OPEN, ret);
    return ret;
  }
}

static GstRTSPResult
gst_rtsp_client_sink_close (GstRTSPClientSink * sink, gboolean async,
    gboolean only_close)
{
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GstRTSPResult res = GST_RTSP_OK;
  GList *walk;
  const gchar *control;

  GST_DEBUG_OBJECT (sink, "TEARDOWN...");

  gst_rtsp_client_sink_set_state (sink, GST_STATE_NULL);

  if (sink->state < GST_RTSP_STATE_READY) {
    GST_DEBUG_OBJECT (sink, "not ready, doing cleanup");
    goto close;
  }

  if (only_close)
    goto close;

  /* construct a control url */
  control = get_aggregate_control (sink);

  if (!(sink->methods & (GST_RTSP_RECORD | GST_RTSP_TEARDOWN)))
    goto not_supported;

  /* stop streaming */
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) walk->data;

    if (context->stream_transport) {
      gst_rtsp_stream_transport_set_active (context->stream_transport, FALSE);
      gst_object_unref (context->stream_transport);
      context->stream_transport = NULL;
    }

    if (context->joined) {
      gst_rtsp_stream_leave_bin (context->stream, GST_BIN (sink->internal_bin),
          sink->rtpbin);
      context->joined = FALSE;
    }
  }

  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) walk->data;
    const gchar *setup_url;
    GstRTSPConnInfo *info;

    GST_DEBUG_OBJECT (sink, "Looking at stream %p for teardown",
        context->stream);

    /* try aggregate control first but do non-aggregate control otherwise */
    if (control)
      setup_url = control;
    else if ((setup_url = context->conninfo.location) == NULL) {
      GST_DEBUG_OBJECT (sink, "Skipping TEARDOWN stream %p - no setup URL",
          context->stream);
      continue;
    }

    if (sink->conninfo.connection) {
      info = &sink->conninfo;
    } else if (context->conninfo.connection) {
      info = &context->conninfo;
    } else {
      continue;
    }
    if (!info->connected)
      goto next;

    /* do TEARDOWN */
    GST_DEBUG_OBJECT (sink, "Sending teardown for stream %p at URL %s",
        context->stream, setup_url);
    res =
        gst_rtsp_client_sink_init_request (sink, &request, GST_RTSP_TEARDOWN,
        setup_url);
    if (res < 0)
      goto create_request_failed;

    if (async)
      GST_ELEMENT_PROGRESS (sink, CONTINUE, "close", ("Closing stream"));

    if ((res =
            gst_rtsp_client_sink_send (sink, info, &request,
                &response, NULL)) < 0)
      goto send_error;

    /* FIXME, parse result? */
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);

  next:
    /* early exit when we did aggregate control */
    if (control)
      break;
  }

close:
  /* close connections */
  GST_DEBUG_OBJECT (sink, "closing connection...");
  gst_rtsp_conninfo_close (sink, &sink->conninfo, TRUE);
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *stream = (GstRTSPStreamContext *) walk->data;
    gst_rtsp_conninfo_close (sink, &stream->conninfo, TRUE);
  }

  /* cleanup */
  gst_rtsp_client_sink_cleanup (sink);

  sink->state = GST_RTSP_STATE_INVALID;

  if (async)
    gst_rtsp_client_sink_loop_end_cmd (sink, CMD_CLOSE, res);

  return res;

  /* ERRORS */
create_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
        ("Could not create request. (%s)", str));
    g_free (str);
    goto close;
  }
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    gst_rtsp_message_unset (&request);
    if (res != GST_RTSP_EINTR) {
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
          ("Could not send message. (%s)", str));
    } else {
      GST_WARNING_OBJECT (sink, "TEARDOWN interrupted");
    }
    g_free (str);
    goto close;
  }
not_supported:
  {
    GST_DEBUG_OBJECT (sink,
        "TEARDOWN and PLAY not supported, can't do TEARDOWN");
    goto close;
  }
}

static gboolean
gst_rtsp_client_sink_configure_manager (GstRTSPClientSink * sink)
{
  GstElement *rtpbin;
  GstStateChangeReturn ret;

  rtpbin = sink->rtpbin;

  if (rtpbin == NULL) {
    GObjectClass *klass;

    rtpbin = gst_element_factory_make ("rtpbin", NULL);
    if (rtpbin == NULL)
      goto no_rtpbin;

    gst_bin_add (GST_BIN_CAST (sink->internal_bin), rtpbin);

    sink->rtpbin = rtpbin;

    /* Any more settings we should configure on rtpbin here? */
    g_object_set (sink->rtpbin, "latency", sink->latency, NULL);

    klass = G_OBJECT_GET_CLASS (G_OBJECT (rtpbin));

    if (g_object_class_find_property (klass, "ntp-time-source")) {
      g_object_set (sink->rtpbin, "ntp-time-source", sink->ntp_time_source,
          NULL);
    }

    if (sink->sdes && g_object_class_find_property (klass, "sdes")) {
      g_object_set (sink->rtpbin, "sdes", sink->sdes, NULL);
    }

    g_signal_emit (sink, gst_rtsp_client_sink_signals[SIGNAL_NEW_MANAGER], 0,
        sink->rtpbin);
  }

  ret = gst_element_set_state (rtpbin, GST_STATE_PAUSED);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto start_manager_failure;

  return TRUE;

no_rtpbin:
  {
    GST_WARNING ("no rtpbin element");
    g_warning ("failed to create element 'rtpbin', check your installation");
    return FALSE;
  }
start_manager_failure:
  {
    GST_DEBUG_OBJECT (sink, "could not start session manager");
    gst_bin_remove (GST_BIN_CAST (sink->internal_bin), rtpbin);
    return FALSE;
  }
}

static GstElement *
request_aux_sender (GstElement * rtpbin, guint sessid, GstRTSPClientSink * sink)
{
  GstRTSPStream *stream = NULL;
  GstElement *ret = NULL;
  GList *walk;

  GST_RTSP_STATE_LOCK (sink);
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) walk->data;

    if (sessid == gst_rtsp_stream_get_index (context->stream)) {
      stream = context->stream;
      break;
    }
  }

  if (stream != NULL) {
    GST_DEBUG_OBJECT (sink, "Creating aux sender for stream %u", sessid);
    ret = gst_rtsp_stream_request_aux_sender (stream, sessid);
  }

  GST_RTSP_STATE_UNLOCK (sink);

  return ret;
}

static GstElement *
request_fec_encoder (GstElement * rtpbin, guint sessid,
    GstRTSPClientSink * sink)
{
  GstRTSPStream *stream = NULL;
  GstElement *ret = NULL;
  GList *walk;

  GST_RTSP_STATE_LOCK (sink);
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) walk->data;

    if (sessid == gst_rtsp_stream_get_index (context->stream)) {
      stream = context->stream;
      break;
    }
  }

  if (stream != NULL) {
    ret = gst_rtsp_stream_request_ulpfec_encoder (stream, sessid);
  }

  GST_RTSP_STATE_UNLOCK (sink);

  return ret;
}

static gboolean
gst_rtsp_client_sink_collect_streams (GstRTSPClientSink * sink)
{
  GstRTSPStreamContext *context;
  GList *walk;
  const gchar *base;
  gboolean has_slash;

  GST_DEBUG_OBJECT (sink, "Collecting stream information");

  if (!gst_rtsp_client_sink_configure_manager (sink))
    return FALSE;

  base = get_aggregate_control (sink);
  /* check if the base ends with / */
  has_slash = g_str_has_suffix (base, "/");

  g_mutex_lock (&sink->preroll_lock);
  while (sink->contexts == NULL && !sink->conninfo.flushing) {
    g_cond_wait (&sink->preroll_cond, &sink->preroll_lock);
  }
  g_mutex_unlock (&sink->preroll_lock);

  /* FIXME: Need different locking - need to protect against pad releases
   * and potential state changes ruining things here */
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstPad *srcpad;

    context = (GstRTSPStreamContext *) walk->data;
    if (context->stream)
      continue;

    g_mutex_lock (&sink->preroll_lock);
    while (!context->prerolled && !sink->conninfo.flushing) {
      GST_DEBUG_OBJECT (sink, "Waiting for caps on stream %d", context->index);
      g_cond_wait (&sink->preroll_cond, &sink->preroll_lock);
    }
    if (sink->conninfo.flushing) {
      g_mutex_unlock (&sink->preroll_lock);
      break;
    }
    g_mutex_unlock (&sink->preroll_lock);

    if (context->payloader == NULL)
      continue;

    srcpad = gst_element_get_static_pad (context->payloader, "src");

    GST_DEBUG_OBJECT (sink, "Creating stream object for stream %d",
        context->index);
    context->stream =
        gst_rtsp_client_sink_create_stream (sink, context, context->payloader,
        srcpad);

    /* concatenate the two strings, insert / when not present */
    g_free (context->conninfo.location);
    context->conninfo.location =
        g_strdup_printf ("%s%sstream=%d", base, has_slash ? "" : "/",
        context->index);

    if (sink->rtx_time > 0) {
      /* enable retransmission by setting rtprtxsend as the "aux" element of rtpbin */
      g_signal_connect (sink->rtpbin, "request-aux-sender",
          (GCallback) request_aux_sender, sink);
    }

    g_signal_connect (sink->rtpbin, "request-fec-encoder",
        (GCallback) request_fec_encoder, sink);

    if (!gst_rtsp_stream_join_bin (context->stream,
            GST_BIN (sink->internal_bin), sink->rtpbin, GST_STATE_PAUSED)) {
      goto join_bin_failed;
    }
    context->joined = TRUE;

    /* Block the stream, as it does not have any transport parts yet */
    gst_rtsp_stream_set_blocked (context->stream, TRUE);

    /* Let the stream object receive data */
    gst_pad_remove_probe (srcpad, context->payloader_block_id);

    gst_object_unref (srcpad);
  }

  /* Now wait for the preroll of the rtp bin */
  g_mutex_lock (&sink->preroll_lock);
  while (!sink->prerolled && sink->conninfo.connection
      && !sink->conninfo.flushing) {
    GST_LOG_OBJECT (sink, "Waiting for preroll before continuing");
    g_cond_wait (&sink->preroll_cond, &sink->preroll_lock);
  }
  GST_LOG_OBJECT (sink, "Marking streams as collected");
  sink->streams_collected = TRUE;
  g_mutex_unlock (&sink->preroll_lock);

  return TRUE;

join_bin_failed:

  GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
      ("Could not start stream %d", context->index));
  return FALSE;
}

static GstRTSPResult
gst_rtsp_client_sink_create_transports_string (GstRTSPClientSink * sink,
    GstRTSPStreamContext * context, GSocketFamily family,
    GstRTSPLowerTrans protocols, GstRTSPProfile profiles, gchar ** transports)
{
  GString *result;
  GstRTSPStream *stream = context->stream;
  gboolean first = TRUE;

  /* the default RTSP transports */
  result = g_string_new ("RTP");

  while (profiles != 0) {
    if (!first)
      g_string_append (result, ",RTP");

    if (profiles & GST_RTSP_PROFILE_SAVPF) {
      g_string_append (result, "/SAVPF");
      profiles &= ~GST_RTSP_PROFILE_SAVPF;
    } else if (profiles & GST_RTSP_PROFILE_SAVP) {
      g_string_append (result, "/SAVP");
      profiles &= ~GST_RTSP_PROFILE_SAVP;
    } else if (profiles & GST_RTSP_PROFILE_AVPF) {
      g_string_append (result, "/AVPF");
      profiles &= ~GST_RTSP_PROFILE_AVPF;
    } else if (profiles & GST_RTSP_PROFILE_AVP) {
      g_string_append (result, "/AVP");
      profiles &= ~GST_RTSP_PROFILE_AVP;
    } else {
      GST_WARNING_OBJECT (sink, "Unimplemented profile(s) 0x%x", profiles);
      break;
    }

    if (protocols & GST_RTSP_LOWER_TRANS_UDP) {
      GstRTSPRange ports;

      GST_DEBUG_OBJECT (sink, "adding UDP unicast");
      gst_rtsp_stream_get_server_port (stream, &ports, family);

      g_string_append_printf (result, "/UDP;unicast;client_port=%d-%d",
          ports.min, ports.max);
    } else if (protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST) {
      GstRTSPAddress *addr =
          gst_rtsp_stream_get_multicast_address (stream, family);
      if (addr) {
        GST_DEBUG_OBJECT (sink, "adding UDP multicast");
        g_string_append_printf (result, "/UDP;multicast;client_port=%d-%d",
            addr->port, addr->port + addr->n_ports - 1);
        gst_rtsp_address_free (addr);
      }
    } else if (protocols & GST_RTSP_LOWER_TRANS_TCP) {
      GST_DEBUG_OBJECT (sink, "adding TCP");
      g_string_append_printf (result, "/TCP;unicast;interleaved=%d-%d",
          sink->free_channel, sink->free_channel + 1);
    }

    g_string_append (result, ";mode=RECORD");
    /* FIXME: Support appending too:
       if (sink->append)
       g_string_append (result, ";append");
     */

    first = FALSE;
  }

  if (first) {
    /* No valid transport could be constructed */
    GST_ERROR_OBJECT (sink, "No supported profiles configured");
    goto fail;
  }

  *transports = g_string_free (result, FALSE);

  GST_DEBUG_OBJECT (sink, "prepared transports %s", GST_STR_NULL (*transports));

  return GST_RTSP_OK;
fail:
  g_string_free (result, TRUE);
  return GST_RTSP_ERROR;
}

static GstCaps *
signal_get_srtcp_params (GstRTSPClientSink * sink,
    GstRTSPStreamContext * context)
{
  GstCaps *caps = NULL;

  g_signal_emit (sink, gst_rtsp_client_sink_signals[SIGNAL_REQUEST_RTCP_KEY], 0,
      context->index, &caps);

  if (caps != NULL)
    GST_DEBUG_OBJECT (sink, "SRTP parameters received");

  return caps;
}

static gchar *
gst_rtsp_client_sink_stream_make_keymgmt (GstRTSPClientSink * sink,
    GstRTSPStreamContext * context)
{
  gchar *base64, *result = NULL;
  GstMIKEYMessage *mikey_msg;

  context->srtcpparams = signal_get_srtcp_params (sink, context);
  if (context->srtcpparams == NULL)
    context->srtcpparams = gst_rtsp_stream_get_caps (context->stream);

  mikey_msg = gst_mikey_message_new_from_caps (context->srtcpparams);
  if (mikey_msg) {
    guint send_ssrc, send_rtx_ssrc;
    const GstStructure *s = gst_caps_get_structure (context->srtcpparams, 0);

    /* add policy '0' for our SSRC */
    gst_rtsp_stream_get_ssrc (context->stream, &send_ssrc);
    GST_LOG_OBJECT (sink, "Stream %p ssrc %x", context->stream, send_ssrc);
    gst_mikey_message_add_cs_srtp (mikey_msg, 0, send_ssrc, 0);

    if (gst_structure_get_uint (s, "rtx-ssrc", &send_rtx_ssrc))
      gst_mikey_message_add_cs_srtp (mikey_msg, 0, send_rtx_ssrc, 0);

    base64 = gst_mikey_message_base64_encode (mikey_msg);
    gst_mikey_message_unref (mikey_msg);

    if (base64) {
      result = gst_sdp_make_keymgmt (context->conninfo.location, base64);
      g_free (base64);
    }
  }

  return result;
}

/* masks to be kept in sync with the hardcoded protocol order of preference
 * in code below */
static const guint protocol_masks[] = {
  GST_RTSP_LOWER_TRANS_UDP,
  GST_RTSP_LOWER_TRANS_UDP_MCAST,
  GST_RTSP_LOWER_TRANS_TCP,
  0
};

/* Same for profile_masks */
static const guint profile_masks[] = {
  GST_RTSP_PROFILE_SAVPF,
  GST_RTSP_PROFILE_SAVP,
  GST_RTSP_PROFILE_AVPF,
  GST_RTSP_PROFILE_AVP,
  0
};

static gboolean
do_send_data (GstBuffer * buffer, guint8 channel,
    GstRTSPStreamContext * context)
{
  GstRTSPClientSink *sink = context->parent;
  GstRTSPMessage message = { 0 };
  GstRTSPResult res = GST_RTSP_OK;

  gst_rtsp_message_init_data (&message, channel);

  gst_rtsp_message_set_body_buffer (&message, buffer);

  res =
      gst_rtsp_client_sink_try_send (sink, &sink->conninfo, &message, 1,
      NULL, NULL);

  gst_rtsp_message_unset (&message);

  gst_rtsp_stream_transport_message_sent (context->stream_transport);

  return res == GST_RTSP_OK;
}

static gboolean
do_send_data_list (GstBufferList * buffer_list, guint8 channel,
    GstRTSPStreamContext * context)
{
  GstRTSPClientSink *sink = context->parent;
  GstRTSPResult res = GST_RTSP_OK;
  guint i, n = gst_buffer_list_length (buffer_list);
  GstRTSPMessage *messages = g_newa (GstRTSPMessage, n);

  memset (messages, 0, n * sizeof (GstRTSPMessage));

  for (i = 0; i < n; i++) {
    GstBuffer *buffer = gst_buffer_list_get (buffer_list, i);

    gst_rtsp_message_init_data (&messages[i], channel);

    gst_rtsp_message_set_body_buffer (&messages[i], buffer);
  }

  res =
      gst_rtsp_client_sink_try_send (sink, &sink->conninfo, messages, n,
      NULL, NULL);

  for (i = 0; i < n; i++) {
    gst_rtsp_message_unset (&messages[i]);
    gst_rtsp_stream_transport_message_sent (context->stream_transport);
  }

  return res == GST_RTSP_OK;
}

static GstRTSPResult
gst_rtsp_client_sink_setup_streams (GstRTSPClientSink * sink, gboolean async)
{
  GstRTSPResult res = GST_RTSP_ERROR;
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GstRTSPLowerTrans protocols;
  GstRTSPStatusCode code;
  GSocketFamily family;
  GSocketAddress *sa;
  GSocket *conn_socket;
  GstRTSPUrl *url;
  GList *walk;
  gchar *hval;

  if (sink->conninfo.connection) {
    url = gst_rtsp_connection_get_url (sink->conninfo.connection);
    /* we initially allow all configured lower transports. based on the URL
     * transports and the replies from the server we narrow them down. */
    protocols = url->transports & sink->cur_protocols;
  } else {
    url = NULL;
    protocols = sink->cur_protocols;
  }

  if (protocols == 0)
    goto no_protocols;

  GST_RTSP_STATE_LOCK (sink);

  if (G_UNLIKELY (sink->contexts == NULL))
    goto no_streams;

  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) walk->data;
    GstRTSPStream *stream;

    GstRTSPConnInfo *info;
    GstRTSPProfile profiles;
    GstRTSPProfile cur_profile;
    gchar *transports;
    gint retry = 0;
    guint profile_mask = 0;
    guint mask = 0;
    GstCaps *caps;
    const GstSDPMedia *media;

    stream = context->stream;
    profiles = gst_rtsp_stream_get_profiles (stream);

    caps = gst_rtsp_stream_get_caps (stream);
    if (caps == NULL) {
      GST_DEBUG_OBJECT (sink, "skipping stream %p, no caps", stream);
      continue;
    }
    gst_caps_unref (caps);
    media = gst_sdp_message_get_media (&sink->cursdp, context->sdp_index);
    if (media == NULL) {
      GST_DEBUG_OBJECT (sink, "skipping stream %p, no SDP info", stream);
      continue;
    }

    /* skip setup if we have no URL for it */
    if (context->conninfo.location == NULL) {
      GST_DEBUG_OBJECT (sink, "skipping stream %p, no setup", stream);
      continue;
    }

    if (sink->conninfo.connection == NULL) {
      if (!gst_rtsp_conninfo_connect (sink, &context->conninfo, async)) {
        GST_DEBUG_OBJECT (sink, "skipping stream %p, failed to connect",
            stream);
        continue;
      }
      info = &context->conninfo;
    } else {
      info = &sink->conninfo;
    }
    GST_DEBUG_OBJECT (sink, "doing setup of stream %p with %s", stream,
        context->conninfo.location);

    conn_socket = gst_rtsp_connection_get_read_socket (info->connection);
    sa = g_socket_get_local_address (conn_socket, NULL);
    family = g_socket_address_get_family (sa);
    g_object_unref (sa);

  next_protocol:
    /* first selectable profile */
    while (profile_masks[profile_mask]
        && !(profiles & profile_masks[profile_mask]))
      profile_mask++;
    if (!profile_masks[profile_mask])
      goto no_profiles;

    /* first selectable protocol */
    while (protocol_masks[mask] && !(protocols & protocol_masks[mask]))
      mask++;
    if (!protocol_masks[mask])
      goto no_protocols;

  retry:
    GST_DEBUG_OBJECT (sink, "protocols = 0x%x, protocol mask = 0x%x", protocols,
        protocol_masks[mask]);
    /* create a string with first transport in line */
    transports = NULL;
    cur_profile = profiles & profile_masks[profile_mask];
    res = gst_rtsp_client_sink_create_transports_string (sink, context, family,
        protocols & protocol_masks[mask], cur_profile, &transports);
    if (res < 0 || transports == NULL)
      goto setup_transport_failed;

    if (strlen (transports) == 0) {
      g_free (transports);
      GST_DEBUG_OBJECT (sink, "no transports found");
      mask++;
      profile_mask = 0;
      goto next_protocol;
    }

    GST_DEBUG_OBJECT (sink, "transport is %s", GST_STR_NULL (transports));

    /* create SETUP request */
    res =
        gst_rtsp_client_sink_init_request (sink, &request, GST_RTSP_SETUP,
        context->conninfo.location);
    if (res < 0) {
      g_free (transports);
      goto create_request_failed;
    }

    /* set up keys */
    if (cur_profile == GST_RTSP_PROFILE_SAVP ||
        cur_profile == GST_RTSP_PROFILE_SAVPF) {
      hval = gst_rtsp_client_sink_stream_make_keymgmt (sink, context);
      gst_rtsp_message_take_header (&request, GST_RTSP_HDR_KEYMGMT, hval);
    }

    /* if the user wants a non default RTP packet size we add the blocksize
     * parameter */
    if (sink->rtp_blocksize > 0) {
      hval = g_strdup_printf ("%d", sink->rtp_blocksize);
      gst_rtsp_message_take_header (&request, GST_RTSP_HDR_BLOCKSIZE, hval);
    }

    if (async)
      GST_ELEMENT_PROGRESS (sink, CONTINUE, "request", ("SETUP stream %d",
              context->index));

    {
      GstRTSPTransport *transport;

      gst_rtsp_transport_new (&transport);
      if (gst_rtsp_transport_parse (transports, transport) != GST_RTSP_OK)
        goto parse_transport_failed;
      if (transport->lower_transport != GST_RTSP_LOWER_TRANS_TCP) {
        if (!gst_rtsp_stream_allocate_udp_sockets (stream, family, transport,
                FALSE)) {
          gst_rtsp_transport_free (transport);
          goto allocate_udp_ports_failed;
        }
      }
      if (!gst_rtsp_stream_complete_stream (stream, transport)) {
        gst_rtsp_transport_free (transport);
        goto complete_stream_failed;
      }

      gst_rtsp_transport_free (transport);
      gst_rtsp_stream_set_blocked (stream, FALSE);
    }

    /* FIXME:
     * the creation of the transports string depends on
     * calling stream_get_server_port, which only starts returning
     * something meaningful after a call to stream_allocate_udp_sockets
     * has been made, this function expects a transport that we parse
     * from the transport string ...
     *
     * Significant refactoring is in order, but does not look entirely
     * trivial, for now we put a band aid on and create a second transport
     * string after the stream has been completed, to pass it in
     * the request headers instead of the previous, incomplete one.
     */
    g_free (transports);
    transports = NULL;
    res = gst_rtsp_client_sink_create_transports_string (sink, context, family,
        protocols & protocol_masks[mask], cur_profile, &transports);

    if (res < 0 || transports == NULL)
      goto setup_transport_failed;

    /* select transport */
    gst_rtsp_message_take_header (&request, GST_RTSP_HDR_TRANSPORT, transports);

    /* handle the code ourselves */
    res = gst_rtsp_client_sink_send (sink, info, &request, &response, &code);
    if (res < 0)
      goto send_error;

    switch (code) {
      case GST_RTSP_STS_OK:
        break;
      case GST_RTSP_STS_UNSUPPORTED_TRANSPORT:
        gst_rtsp_message_unset (&request);
        gst_rtsp_message_unset (&response);

        /* Try another profile. If no more, move to the next protocol */
        profile_mask++;
        while (profile_masks[profile_mask]
            && !(profiles & profile_masks[profile_mask]))
          profile_mask++;
        if (profile_masks[profile_mask])
          goto retry;

        /* select next available protocol, give up on this stream if none */
        /* Reset profiles to try: */
        profile_mask = 0;

        mask++;
        while (protocol_masks[mask] && !(protocols & protocol_masks[mask]))
          mask++;
        if (!protocol_masks[mask])
          continue;
        else
          goto retry;
      default:
        goto response_error;
    }

    /* parse response transport */
    {
      gchar *resptrans = NULL;
      GstRTSPTransport *transport;

      gst_rtsp_message_get_header (&response, GST_RTSP_HDR_TRANSPORT,
          &resptrans, 0);
      if (!resptrans) {
        goto no_transport;
      }

      gst_rtsp_transport_new (&transport);

      /* parse transport, go to next stream on parse error */
      if (gst_rtsp_transport_parse (resptrans, transport) != GST_RTSP_OK) {
        GST_WARNING_OBJECT (sink, "failed to parse transport %s", resptrans);
        goto next;
      }

      /* update allowed transports for other streams. once the transport of
       * one stream has been determined, we make sure that all other streams
       * are configured in the same way */
      switch (transport->lower_transport) {
        case GST_RTSP_LOWER_TRANS_TCP:
          GST_DEBUG_OBJECT (sink, "stream %p as TCP interleaved", stream);
          protocols = GST_RTSP_LOWER_TRANS_TCP;
          sink->interleaved = TRUE;
          /* update free channels */
          sink->free_channel =
              MAX (transport->interleaved.min, sink->free_channel);
          sink->free_channel =
              MAX (transport->interleaved.max, sink->free_channel);
          sink->free_channel++;
          break;
        case GST_RTSP_LOWER_TRANS_UDP_MCAST:
          /* only allow multicast for other streams */
          GST_DEBUG_OBJECT (sink, "stream %p as UDP multicast", stream);
          protocols = GST_RTSP_LOWER_TRANS_UDP_MCAST;
          break;
        case GST_RTSP_LOWER_TRANS_UDP:
          /* only allow unicast for other streams */
          GST_DEBUG_OBJECT (sink, "stream %p as UDP unicast", stream);
          protocols = GST_RTSP_LOWER_TRANS_UDP;
          /* Update transport with server destination if not provided by the server */
          if (transport->destination == NULL) {
            transport->destination = g_strdup (sink->server_ip);
          }
          break;
        default:
          GST_DEBUG_OBJECT (sink, "stream %p unknown transport %d", stream,
              transport->lower_transport);
          break;
      }

      if (!retry) {
        GST_DEBUG ("Configuring the stream transport for stream %d",
            context->index);
        if (context->stream_transport == NULL)
          context->stream_transport =
              gst_rtsp_stream_transport_new (stream, transport);
        else
          gst_rtsp_stream_transport_set_transport (context->stream_transport,
              transport);

        if (transport->lower_transport == GST_RTSP_LOWER_TRANS_TCP) {
          /* our callbacks to send data on this TCP connection */
          gst_rtsp_stream_transport_set_callbacks (context->stream_transport,
              (GstRTSPSendFunc) do_send_data,
              (GstRTSPSendFunc) do_send_data, context, NULL);
          gst_rtsp_stream_transport_set_list_callbacks
              (context->stream_transport,
              (GstRTSPSendListFunc) do_send_data_list,
              (GstRTSPSendListFunc) do_send_data_list, context, NULL);
        }

        /* The stream_transport now owns the transport */
        transport = NULL;

        gst_rtsp_stream_transport_set_active (context->stream_transport, TRUE);
      }
    next:
      if (transport)
        gst_rtsp_transport_free (transport);
      /* clean up used RTSP messages */
      gst_rtsp_message_unset (&request);
      gst_rtsp_message_unset (&response);
    }
  }
  GST_RTSP_STATE_UNLOCK (sink);

  /* store the transport protocol that was configured */
  sink->cur_protocols = protocols;

  return res;

no_streams:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("SDP contains no streams"));
    return GST_RTSP_ERROR;
  }
setup_transport_failed:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not setup transport."));
    res = GST_RTSP_ERROR;
    goto cleanup_error;
  }
no_profiles:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    /* no transport possible, post an error and stop */
    GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
        ("Could not connect to server, no profiles left"));
    return GST_RTSP_ERROR;
  }
no_protocols:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    /* no transport possible, post an error and stop */
    GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
        ("Could not connect to server, no protocols left"));
    return GST_RTSP_ERROR;
  }
no_transport:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Server did not select transport."));
    res = GST_RTSP_ERROR;
    goto cleanup_error;
  }
create_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
        ("Could not create request. (%s)", str));
    g_free (str);
    goto cleanup_error;
  }
parse_transport_failed:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not parse transport."));
    res = GST_RTSP_ERROR;
    goto cleanup_error;
  }
allocate_udp_ports_failed:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not parse transport."));
    res = GST_RTSP_ERROR;
    goto cleanup_error;
  }
complete_stream_failed:
  {
    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not parse transport."));
    res = GST_RTSP_ERROR;
    goto cleanup_error;
  }
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_RTSP_STATE_UNLOCK (sink);
    if (res != GST_RTSP_EINTR) {
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
          ("Could not send message. (%s)", str));
    } else {
      GST_WARNING_OBJECT (sink, "send interrupted");
    }
    g_free (str);
    goto cleanup_error;
  }
response_error:
  {
    const gchar *str = gst_rtsp_status_as_text (code);

    GST_RTSP_STATE_UNLOCK (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
        ("Error (%d): %s", code, GST_STR_NULL (str)));
    res = GST_RTSP_ERROR;
    goto cleanup_error;
  }
cleanup_error:
  {
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);
    return res;
  }
}

static GstRTSPResult
gst_rtsp_client_sink_ensure_open (GstRTSPClientSink * sink, gboolean async)
{
  GstRTSPResult res = GST_RTSP_OK;

  if (sink->state < GST_RTSP_STATE_READY) {
    res = GST_RTSP_ERROR;
    if (sink->open_error) {
      GST_DEBUG_OBJECT (sink, "the stream was in error");
      goto done;
    }
    if (async)
      gst_rtsp_client_sink_loop_start_cmd (sink, CMD_OPEN);

    if ((res = gst_rtsp_client_sink_open (sink, async)) < 0) {
      GST_DEBUG_OBJECT (sink, "failed to open stream");
      goto done;
    }
  }

done:
  return res;
}

static gboolean
gst_rtsp_client_sink_is_stopping (GstRTSPClientSink * sink)
{
  gboolean is_stopping;

  GST_OBJECT_LOCK (sink);
  is_stopping = sink->task == NULL;
  GST_OBJECT_UNLOCK (sink);

  return is_stopping;
}

static GstRTSPResult
gst_rtsp_client_sink_record (GstRTSPClientSink * sink, gboolean async)
{
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GstRTSPResult res = GST_RTSP_OK;
  GstSDPMessage *sdp;
  guint sdp_index = 0;
  GstSDPInfo info = { 0, };
  gchar *keymgmt;
  guint i;

  const gchar *proto;
  gchar *sess_id, *client_ip, *str;
  GSocketAddress *sa;
  GInetAddress *ia;
  GSocket *conn_socket;
  GList *walk;

  g_mutex_lock (&sink->preroll_lock);
  if (sink->state == GST_RTSP_STATE_PLAYING) {
    /* Already recording, don't send another request */
    GST_LOG_OBJECT (sink, "Already in RECORD. Skipping duplicate request.");
    g_mutex_unlock (&sink->preroll_lock);
    goto done;
  }
  g_mutex_unlock (&sink->preroll_lock);

  /* Collect all our input streams and create
   * stream objects before actually returning.
   * The streams are blocked at this point as we do not have any transport
   * parts yet. */
  gst_rtsp_client_sink_collect_streams (sink);

  if (gst_rtsp_client_sink_is_stopping (sink)) {
    GST_INFO_OBJECT (sink, "task stopped, shutting down");
    return GST_RTSP_EINTR;
  }

  g_mutex_lock (&sink->block_streams_lock);
  /* Wait for streams to be blocked */
  while (sink->n_streams_blocked < g_list_length (sink->contexts)
      && !gst_rtsp_client_sink_is_stopping (sink)) {
    GST_DEBUG_OBJECT (sink, "waiting for streams to be blocked");
    g_cond_wait (&sink->block_streams_cond, &sink->block_streams_lock);
  }
  g_mutex_unlock (&sink->block_streams_lock);

  if (gst_rtsp_client_sink_is_stopping (sink)) {
    GST_INFO_OBJECT (sink, "task stopped, shutting down");
    return GST_RTSP_EINTR;
  }

  /* Send announce, then setup for all streams */
  gst_sdp_message_init (&sink->cursdp);
  sdp = &sink->cursdp;

  /* some standard things first */
  gst_sdp_message_set_version (sdp, "0");

  /* session ID doesn't have to be super-unique in this case */
  sess_id = g_strdup_printf ("%u", g_random_int ());

  if (sink->conninfo.connection == NULL)
    return GST_RTSP_ERROR;

  conn_socket = gst_rtsp_connection_get_read_socket (sink->conninfo.connection);

  sa = g_socket_get_local_address (conn_socket, NULL);
  ia = g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (sa));
  client_ip = g_inet_address_to_string (ia);
  if (g_socket_address_get_family (sa) == G_SOCKET_FAMILY_IPV6) {
    info.is_ipv6 = TRUE;
    proto = "IP6";
  } else if (g_socket_address_get_family (sa) == G_SOCKET_FAMILY_IPV4)
    proto = "IP4";
  else
    g_assert_not_reached ();
  g_object_unref (sa);

  /* FIXME: Should this actually be the server's IP or ours? */
  info.server_ip = sink->server_ip;

  gst_sdp_message_set_origin (sdp, "-", sess_id, "1", "IN", proto, client_ip);

  gst_sdp_message_set_session_name (sdp, "Session streamed with GStreamer");
  gst_sdp_message_set_information (sdp, "rtspclientsink");
  gst_sdp_message_add_time (sdp, "0", "0", NULL);
  gst_sdp_message_add_attribute (sdp, "tool", "GStreamer");

  /* add stream */
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) walk->data;

    gst_rtsp_sdp_from_stream (sdp, &info, context->stream);
    context->sdp_index = sdp_index++;
  }

  g_free (sess_id);
  g_free (client_ip);

  /* send ANNOUNCE request */
  GST_DEBUG_OBJECT (sink, "create ANNOUNCE request...");
  res =
      gst_rtsp_client_sink_init_request (sink, &request, GST_RTSP_ANNOUNCE,
      sink->conninfo.url_str);
  if (res < 0)
    goto create_request_failed;

  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_CONTENT_TYPE,
      "application/sdp");

  /* add SDP to the request body */
  str = gst_sdp_message_as_text (sdp);
  gst_rtsp_message_take_body (&request, (guint8 *) str, strlen (str));

  /* send ANNOUNCE */
  GST_DEBUG_OBJECT (sink, "sending announce...");

  if (async)
    GST_ELEMENT_PROGRESS (sink, CONTINUE, "record",
        ("Sending server stream info"));

  if ((res =
          gst_rtsp_client_sink_send (sink, &sink->conninfo, &request,
              &response, NULL)) < 0)
    goto send_error;

  /* parse the keymgmt */
  i = 0;
  walk = sink->contexts;
  while (gst_rtsp_message_get_header (&response, GST_RTSP_HDR_KEYMGMT,
          &keymgmt, i++) == GST_RTSP_OK) {
    GstRTSPStreamContext *context = (GstRTSPStreamContext *) walk->data;
    walk = g_list_next (walk);
    if (!gst_rtsp_stream_handle_keymgmt (context->stream, keymgmt))
      goto keymgmt_error;
  }

  /* send setup for all streams */
  if ((res = gst_rtsp_client_sink_setup_streams (sink, async)) < 0)
    goto setup_failed;

  res = gst_rtsp_client_sink_init_request (sink, &request, GST_RTSP_RECORD,
      sink->conninfo.url_str);

  if (res < 0)
    goto create_request_failed;

#if 0                           /* FIXME: Configure a range based on input segments? */
  if (src->need_range) {
    hval = gen_range_header (src, segment);

    gst_rtsp_message_take_header (&request, GST_RTSP_HDR_RANGE, hval);
  }

  if (segment->rate != 1.0) {
    gchar hval[G_ASCII_DTOSTR_BUF_SIZE];

    g_ascii_dtostr (hval, sizeof (hval), segment->rate);
    if (src->skip)
      gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SCALE, hval);
    else
      gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SPEED, hval);
  }
#endif

  if (async)
    GST_ELEMENT_PROGRESS (sink, CONTINUE, "record", ("Starting recording"));
  if ((res =
          gst_rtsp_client_sink_send (sink, &sink->conninfo, &request,
              &response, NULL)) < 0)
    goto send_error;

#if 0                           /* FIXME: Check if servers return these for record: */
  /* parse the RTP-Info header field (if ANY) to get the base seqnum and timestamp
   * for the RTP packets. If this is not present, we assume all starts from 0...
   * This is info for the RTP session manager that we pass to it in caps. */
  hval_idx = 0;
  while (gst_rtsp_message_get_header (&response, GST_RTSP_HDR_RTP_INFO,
          &hval, hval_idx++) == GST_RTSP_OK)
    gst_rtspsrc_parse_rtpinfo (src, hval);

  /* some servers indicate RTCP parameters in PLAY response,
   * rather than properly in SDP */
  if (gst_rtsp_message_get_header (&response, GST_RTSP_HDR_RTCP_INTERVAL,
          &hval, 0) == GST_RTSP_OK)
    gst_rtspsrc_handle_rtcp_interval (src, hval);
#endif

  gst_rtsp_client_sink_set_state (sink, GST_STATE_PLAYING);
  sink->state = GST_RTSP_STATE_PLAYING;

  /* clean up any messages */
  gst_rtsp_message_unset (&request);
  gst_rtsp_message_unset (&response);

done:
  return res;

create_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
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
keymgmt_error:
  {
    GST_ELEMENT_ERROR (sink, STREAM, DECRYPT_NOKEY, (NULL),
        ("Could not handle KeyMgmt"));
  }
setup_failed:
  {
    GST_ERROR_OBJECT (sink, "setup failed");
    goto cleanup_error;
  }
cleanup_error:
  {
    if (sink->conninfo.connection) {
      GST_DEBUG_OBJECT (sink, "free connection");
      gst_rtsp_conninfo_close (sink, &sink->conninfo, TRUE);
    }
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);
    return res;
  }
}

static GstRTSPResult
gst_rtsp_client_sink_pause (GstRTSPClientSink * sink, gboolean async)
{
  GstRTSPResult res = GST_RTSP_OK;
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  GList *walk;
  const gchar *control;

  GST_DEBUG_OBJECT (sink, "PAUSE...");

  if ((res = gst_rtsp_client_sink_ensure_open (sink, async)) < 0)
    goto open_failed;

  if (!(sink->methods & GST_RTSP_PAUSE))
    goto not_supported;

  if (sink->state == GST_RTSP_STATE_READY)
    goto was_paused;

  if (!sink->conninfo.connection || !sink->conninfo.connected)
    goto no_connection;

  /* construct a control url */
  control = get_aggregate_control (sink);

  /* loop over the streams. We might exit the loop early when we could do an
   * aggregate control */
  for (walk = sink->contexts; walk; walk = g_list_next (walk)) {
    GstRTSPStreamContext *stream = (GstRTSPStreamContext *) walk->data;
    GstRTSPConnInfo *info;
    const gchar *setup_url;

    /* try aggregate control first but do non-aggregate control otherwise */
    if (control)
      setup_url = control;
    else if ((setup_url = stream->conninfo.location) == NULL)
      continue;

    if (sink->conninfo.connection) {
      info = &sink->conninfo;
    } else if (stream->conninfo.connection) {
      info = &stream->conninfo;
    } else {
      continue;
    }

    if (async)
      GST_ELEMENT_PROGRESS (sink, CONTINUE, "request",
          ("Sending PAUSE request"));

    if ((res =
            gst_rtsp_client_sink_init_request (sink, &request, GST_RTSP_PAUSE,
                setup_url)) < 0)
      goto create_request_failed;

    if ((res =
            gst_rtsp_client_sink_send (sink, info, &request, &response,
                NULL)) < 0)
      goto send_error;

    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);

    /* exit early when we did agregate control */
    if (control)
      break;
  }

  /* change element states now */
  gst_rtsp_client_sink_set_state (sink, GST_STATE_PAUSED);

no_connection:
  sink->state = GST_RTSP_STATE_READY;

done:
  if (async)
    gst_rtsp_client_sink_loop_end_cmd (sink, CMD_PAUSE, res);

  return res;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (sink, "failed to open stream");
    goto done;
  }
not_supported:
  {
    GST_DEBUG_OBJECT (sink, "PAUSE is not supported");
    goto done;
  }
was_paused:
  {
    GST_DEBUG_OBJECT (sink, "we were already PAUSED");
    goto done;
  }
create_request_failed:
  {
    gchar *str = gst_rtsp_strresult (res);

    GST_ELEMENT_ERROR (sink, LIBRARY, INIT, (NULL),
        ("Could not create request. (%s)", str));
    g_free (str);
    goto done;
  }
send_error:
  {
    gchar *str = gst_rtsp_strresult (res);

    gst_rtsp_message_unset (&request);
    if (res != GST_RTSP_EINTR) {
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
          ("Could not send message. (%s)", str));
    } else {
      GST_WARNING_OBJECT (sink, "PAUSE interrupted");
    }
    g_free (str);
    goto done;
  }
}

static void
gst_rtsp_client_sink_handle_message (GstBin * bin, GstMessage * message)
{
  GstRTSPClientSink *rtsp_client_sink;

  rtsp_client_sink = GST_RTSP_CLIENT_SINK (bin);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ELEMENT:
    {
      const GstStructure *s = gst_message_get_structure (message);

      if (gst_structure_has_name (s, "GstUDPSrcTimeout")) {
        gboolean ignore_timeout;

        GST_DEBUG_OBJECT (bin, "timeout on UDP port");

        GST_OBJECT_LOCK (rtsp_client_sink);
        ignore_timeout = rtsp_client_sink->ignore_timeout;
        rtsp_client_sink->ignore_timeout = TRUE;
        GST_OBJECT_UNLOCK (rtsp_client_sink);

        /* we only act on the first udp timeout message, others are irrelevant
         * and can be ignored. */
        if (!ignore_timeout)
          gst_rtsp_client_sink_loop_send_cmd (rtsp_client_sink, CMD_RECONNECT,
              CMD_LOOP);
        /* eat and free */
        gst_message_unref (message);
        return;
      } else if (gst_structure_has_name (s, "GstRTSPStreamBlocking")) {
        /* An RTSPStream has prerolled */
        GST_DEBUG_OBJECT (rtsp_client_sink, "received GstRTSPStreamBlocking");
        g_mutex_lock (&rtsp_client_sink->block_streams_lock);
        rtsp_client_sink->n_streams_blocked++;
        g_cond_broadcast (&rtsp_client_sink->block_streams_cond);
        g_mutex_unlock (&rtsp_client_sink->block_streams_lock);
      }
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
    case GST_MESSAGE_ASYNC_START:{
      GstObject *sender;

      sender = GST_MESSAGE_SRC (message);

      GST_LOG_OBJECT (rtsp_client_sink,
          "Have async-start from %" GST_PTR_FORMAT, sender);
      if (sender == GST_OBJECT (rtsp_client_sink->internal_bin)) {
        GST_LOG_OBJECT (rtsp_client_sink, "child bin is now ASYNC");
      }
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
    case GST_MESSAGE_ASYNC_DONE:
    {
      GstObject *sender;
      gboolean need_async_done;

      sender = GST_MESSAGE_SRC (message);
      GST_LOG_OBJECT (rtsp_client_sink, "Have async-done from %" GST_PTR_FORMAT,
          sender);

      g_mutex_lock (&rtsp_client_sink->preroll_lock);
      if (sender == GST_OBJECT_CAST (rtsp_client_sink->internal_bin)) {
        GST_LOG_OBJECT (rtsp_client_sink, "child bin is no longer ASYNC");
      }
      need_async_done = rtsp_client_sink->in_async;
      if (rtsp_client_sink->in_async) {
        rtsp_client_sink->in_async = FALSE;
        g_cond_broadcast (&rtsp_client_sink->preroll_cond);
      }
      g_mutex_unlock (&rtsp_client_sink->preroll_lock);

      GST_BIN_CLASS (parent_class)->handle_message (bin, message);

      if (need_async_done) {
        GST_DEBUG_OBJECT (rtsp_client_sink, "Posting ASYNC-DONE");
        gst_element_post_message (GST_ELEMENT_CAST (rtsp_client_sink),
            gst_message_new_async_done (GST_OBJECT_CAST (rtsp_client_sink),
                GST_CLOCK_TIME_NONE));
      }
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      GstObject *sender;

      sender = GST_MESSAGE_SRC (message);

      GST_DEBUG_OBJECT (rtsp_client_sink, "got error from %s",
          GST_ELEMENT_NAME (sender));

      /* FIXME: Ignore errors on RTCP? */
      /* fatal but not our message, forward */
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
    case GST_MESSAGE_STATE_CHANGED:
    {
      if (GST_MESSAGE_SRC (message) ==
          (GstObject *) rtsp_client_sink->internal_bin) {
        GstState newstate, pending;
        gst_message_parse_state_changed (message, NULL, &newstate, &pending);
        g_mutex_lock (&rtsp_client_sink->preroll_lock);
        rtsp_client_sink->prerolled = (newstate >= GST_STATE_PAUSED)
            && pending == GST_STATE_VOID_PENDING;
        g_cond_broadcast (&rtsp_client_sink->preroll_cond);
        g_mutex_unlock (&rtsp_client_sink->preroll_lock);
        GST_DEBUG_OBJECT (bin,
            "Internal bin changed state to %s (pending %s). Prerolled now %d",
            gst_element_state_get_name (newstate),
            gst_element_state_get_name (pending), rtsp_client_sink->prerolled);
      }
      /* fallthrough */
    }
    default:
    {
      GST_BIN_CLASS (parent_class)->handle_message (bin, message);
      break;
    }
  }
}

/* the thread where everything happens */
static void
gst_rtsp_client_sink_thread (GstRTSPClientSink * sink)
{
  gint cmd;

  GST_OBJECT_LOCK (sink);
  cmd = sink->pending_cmd;
  if (cmd == CMD_RECONNECT || cmd == CMD_RECORD || cmd == CMD_PAUSE
      || cmd == CMD_LOOP || cmd == CMD_OPEN)
    sink->pending_cmd = CMD_LOOP;
  else
    sink->pending_cmd = CMD_WAIT;
  GST_DEBUG_OBJECT (sink, "got command %s", cmd_to_string (cmd));

  /* we got the message command, so ensure communication is possible again */
  gst_rtsp_client_sink_connection_flush (sink, FALSE);

  sink->busy_cmd = cmd;
  GST_OBJECT_UNLOCK (sink);

  switch (cmd) {
    case CMD_OPEN:
      if (gst_rtsp_client_sink_open (sink, TRUE) == GST_RTSP_ERROR)
        gst_rtsp_client_sink_loop_send_cmd (sink, CMD_WAIT,
            CMD_ALL & ~CMD_CLOSE);
      break;
    case CMD_RECORD:
      gst_rtsp_client_sink_record (sink, TRUE);
      break;
    case CMD_PAUSE:
      gst_rtsp_client_sink_pause (sink, TRUE);
      break;
    case CMD_CLOSE:
      gst_rtsp_client_sink_close (sink, TRUE, FALSE);
      break;
    case CMD_LOOP:
      gst_rtsp_client_sink_loop (sink);
      break;
    case CMD_RECONNECT:
      gst_rtsp_client_sink_reconnect (sink, FALSE);
      break;
    default:
      break;
  }

  GST_OBJECT_LOCK (sink);
  /* and go back to sleep */
  if (sink->pending_cmd == CMD_WAIT) {
    if (sink->task)
      gst_task_pause (sink->task);
  }
  /* reset waiting */
  sink->busy_cmd = CMD_WAIT;
  GST_OBJECT_UNLOCK (sink);
}

static gboolean
gst_rtsp_client_sink_start (GstRTSPClientSink * sink)
{
  GST_DEBUG_OBJECT (sink, "starting");

  sink->streams_collected = FALSE;
  gst_element_set_locked_state (GST_ELEMENT (sink->internal_bin), TRUE);

  gst_rtsp_client_sink_set_state (sink, GST_STATE_READY);

  GST_OBJECT_LOCK (sink);
  sink->pending_cmd = CMD_WAIT;

  if (sink->task == NULL) {
    sink->task =
        gst_task_new ((GstTaskFunction) gst_rtsp_client_sink_thread, sink,
        NULL);
    if (sink->task == NULL)
      goto task_error;

    gst_task_set_lock (sink->task, GST_RTSP_STREAM_GET_LOCK (sink));
  }
  GST_OBJECT_UNLOCK (sink);

  return TRUE;

  /* ERRORS */
task_error:
  {
    GST_OBJECT_UNLOCK (sink);
    GST_ERROR_OBJECT (sink, "failed to create task");
    return FALSE;
  }
}

static gboolean
gst_rtsp_client_sink_stop (GstRTSPClientSink * sink)
{
  GstTask *task;

  GST_DEBUG_OBJECT (sink, "stopping");

  /* also cancels pending task */
  gst_rtsp_client_sink_loop_send_cmd (sink, CMD_WAIT, CMD_ALL & ~CMD_CLOSE);

  GST_OBJECT_LOCK (sink);
  if ((task = sink->task)) {
    sink->task = NULL;
    GST_OBJECT_UNLOCK (sink);

    gst_task_stop (task);

    g_mutex_lock (&sink->block_streams_lock);
    g_cond_broadcast (&sink->block_streams_cond);
    g_mutex_unlock (&sink->block_streams_lock);

    /* make sure it is not running */
    GST_RTSP_STREAM_LOCK (sink);
    GST_RTSP_STREAM_UNLOCK (sink);

    /* now wait for the task to finish */
    gst_task_join (task);

    /* and free the task */
    gst_object_unref (GST_OBJECT (task));

    GST_OBJECT_LOCK (sink);
  }
  GST_OBJECT_UNLOCK (sink);

  /* ensure synchronously all is closed and clean */
  gst_rtsp_client_sink_close (sink, FALSE, TRUE);

  return TRUE;
}

static GstStateChangeReturn
gst_rtsp_client_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRTSPClientSink *rtsp_client_sink;
  GstStateChangeReturn ret;

  rtsp_client_sink = GST_RTSP_CLIENT_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_rtsp_client_sink_start (rtsp_client_sink))
        goto start_failed;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* init some state */
      rtsp_client_sink->cur_protocols = rtsp_client_sink->protocols;
      /* first attempt, don't ignore timeouts */
      rtsp_client_sink->ignore_timeout = FALSE;
      rtsp_client_sink->open_error = FALSE;

      gst_rtsp_client_sink_set_state (rtsp_client_sink, GST_STATE_PAUSED);

      g_mutex_lock (&rtsp_client_sink->preroll_lock);
      if (rtsp_client_sink->in_async) {
        GST_DEBUG_OBJECT (rtsp_client_sink, "Posting ASYNC-START");
        gst_element_post_message (GST_ELEMENT_CAST (rtsp_client_sink),
            gst_message_new_async_start (GST_OBJECT_CAST (rtsp_client_sink)));
      }
      g_mutex_unlock (&rtsp_client_sink->preroll_lock);

      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* fall-through */
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* unblock the tcp tasks and make the loop waiting */
      if (gst_rtsp_client_sink_loop_send_cmd (rtsp_client_sink, CMD_WAIT,
              CMD_LOOP)) {
        /* make sure it is waiting before we send PLAY below */
        GST_RTSP_STREAM_LOCK (rtsp_client_sink);
        GST_RTSP_STREAM_UNLOCK (rtsp_client_sink);
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtsp_client_sink_set_state (rtsp_client_sink, GST_STATE_READY);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      /* Return ASYNC and preroll input streams */
      g_mutex_lock (&rtsp_client_sink->preroll_lock);
      if (rtsp_client_sink->in_async)
        ret = GST_STATE_CHANGE_ASYNC;
      g_mutex_unlock (&rtsp_client_sink->preroll_lock);
      gst_rtsp_client_sink_loop_send_cmd (rtsp_client_sink, CMD_OPEN, 0);

      /* CMD_OPEN has been scheduled. Wait until the sink thread starts
       * opening connection to the server */
      g_mutex_lock (&rtsp_client_sink->open_conn_lock);
      while (!rtsp_client_sink->open_conn_start) {
        GST_DEBUG_OBJECT (rtsp_client_sink,
            "wait for connection to be started");
        g_cond_wait (&rtsp_client_sink->open_conn_cond,
            &rtsp_client_sink->open_conn_lock);
      }
      rtsp_client_sink->open_conn_start = FALSE;
      g_mutex_unlock (&rtsp_client_sink->open_conn_lock);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:{
      GST_DEBUG_OBJECT (rtsp_client_sink,
          "Switching to playing -sending RECORD");
      gst_rtsp_client_sink_loop_send_cmd (rtsp_client_sink, CMD_RECORD, 0);
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    }
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      /* send pause request and keep the idle task around */
      gst_rtsp_client_sink_loop_send_cmd (rtsp_client_sink, CMD_PAUSE,
          CMD_LOOP);
      ret = GST_STATE_CHANGE_NO_PREROLL;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_rtsp_client_sink_loop_send_cmd (rtsp_client_sink, CMD_CLOSE,
          CMD_PAUSE);
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_rtsp_client_sink_stop (rtsp_client_sink);
      ret = GST_STATE_CHANGE_SUCCESS;
      break;
    default:
      break;
  }

done:
  return ret;

start_failed:
  {
    GST_DEBUG_OBJECT (rtsp_client_sink, "start failed");
    return GST_STATE_CHANGE_FAILURE;
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_rtsp_client_sink_uri_get_type (GType type)
{
  return GST_URI_SINK;
}

static const gchar *const *
gst_rtsp_client_sink_uri_get_protocols (GType type)
{
  static const gchar *protocols[] =
      { "rtsp", "rtspu", "rtspt", "rtsph", "rtsp-sdp",
    "rtsps", "rtspsu", "rtspst", "rtspsh", NULL
  };

  return protocols;
}

static gchar *
gst_rtsp_client_sink_uri_get_uri (GstURIHandler * handler)
{
  GstRTSPClientSink *sink = GST_RTSP_CLIENT_SINK (handler);

  /* FIXME: make thread-safe */
  return g_strdup (sink->conninfo.location);
}

static gboolean
gst_rtsp_client_sink_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstRTSPClientSink *sink;
  GstRTSPResult res;
  GstSDPResult sres;
  GstRTSPUrl *newurl = NULL;
  GstSDPMessage *sdp = NULL;

  sink = GST_RTSP_CLIENT_SINK (handler);

  /* same URI, we're fine */
  if (sink->conninfo.location && uri && !strcmp (uri, sink->conninfo.location))
    goto was_ok;

  if (g_str_has_prefix (uri, "rtsp-sdp://")) {
    sres = gst_sdp_message_new (&sdp);
    if (sres < 0)
      goto sdp_failed;

    GST_DEBUG_OBJECT (sink, "parsing SDP message");
    sres = gst_sdp_message_parse_uri (uri, sdp);
    if (sres < 0)
      goto invalid_sdp;
  } else {
    /* try to parse */
    GST_DEBUG_OBJECT (sink, "parsing URI");
    if ((res = gst_rtsp_url_parse (uri, &newurl)) < 0)
      goto parse_error;
  }

  /* if worked, free previous and store new url object along with the original
   * location. */
  GST_DEBUG_OBJECT (sink, "configuring URI");
  g_free (sink->conninfo.location);
  sink->conninfo.location = g_strdup (uri);
  gst_rtsp_url_free (sink->conninfo.url);
  sink->conninfo.url = newurl;
  g_free (sink->conninfo.url_str);
  if (newurl)
    sink->conninfo.url_str = gst_rtsp_url_get_request_uri (sink->conninfo.url);
  else
    sink->conninfo.url_str = NULL;

  if (sink->uri_sdp)
    gst_sdp_message_free (sink->uri_sdp);
  sink->uri_sdp = sdp;
  sink->from_sdp = sdp != NULL;

  GST_DEBUG_OBJECT (sink, "set uri: %s", GST_STR_NULL (uri));
  GST_DEBUG_OBJECT (sink, "request uri is: %s",
      GST_STR_NULL (sink->conninfo.url_str));

  return TRUE;

  /* Special cases */
was_ok:
  {
    GST_DEBUG_OBJECT (sink, "URI was ok: '%s'", GST_STR_NULL (uri));
    return TRUE;
  }
sdp_failed:
  {
    GST_ERROR_OBJECT (sink, "Could not create new SDP (%d)", sres);
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Could not create SDP");
    return FALSE;
  }
invalid_sdp:
  {
    GST_ERROR_OBJECT (sink, "Not a valid SDP (%d) '%s'", sres,
        GST_STR_NULL (uri));
    gst_sdp_message_free (sdp);
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid SDP");
    return FALSE;
  }
parse_error:
  {
    GST_ERROR_OBJECT (sink, "Not a valid RTSP url '%s' (%d)",
        GST_STR_NULL (uri), res);
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_URI,
        "Invalid RTSP URI");
    return FALSE;
  }
}

static void
gst_rtsp_client_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_rtsp_client_sink_uri_get_type;
  iface->get_protocols = gst_rtsp_client_sink_uri_get_protocols;
  iface->get_uri = gst_rtsp_client_sink_uri_get_uri;
  iface->set_uri = gst_rtsp_client_sink_uri_set_uri;
}
