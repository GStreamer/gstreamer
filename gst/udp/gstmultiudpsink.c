/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) <2009> Jarkko Palviainen <jarkko.palviainen@sesca.com>
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
 * SECTION:element-multiudpsink
 * @see_also: udpsink, multifdsink
 *
 * multiudpsink is a network sink that sends UDP packets to multiple
 * clients.
 * It can be combined with rtp payload encoders to implement RTP streaming.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstudp-marshal.h"
#include "gstmultiudpsink.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>
#include <string.h>

#include "gst/glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (multiudpsink_debug);
#define GST_CAT_DEFAULT (multiudpsink_debug)

#define UDP_MAX_SIZE 65507

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* MultiUDPSink signals and args */
enum
{
  /* methods */
  SIGNAL_ADD,
  SIGNAL_REMOVE,
  SIGNAL_CLEAR,
  SIGNAL_GET_STATS,

  /* signals */
  SIGNAL_CLIENT_ADDED,
  SIGNAL_CLIENT_REMOVED,

  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SOCKFD             -1
#define DEFAULT_CLOSEFD            TRUE
#define DEFAULT_SOCK               -1
#define DEFAULT_CLIENTS            NULL
#define DEFAULT_FAMILY             0
/* FIXME, this should be disabled by default, we don't need to join a multicast
 * group for sending, if this socket is also used for receiving, it should
 * be configured in the element that does the receive. */
#define DEFAULT_AUTO_MULTICAST     TRUE
#define DEFAULT_TTL                64
#define DEFAULT_TTL_MC             1
#define DEFAULT_LOOP               TRUE
#define DEFAULT_QOS_DSCP           -1
#define DEFAULT_SEND_DUPLICATES    TRUE
#define DEFAULT_BUFFER_SIZE        0

enum
{
  PROP_0,
  PROP_BYTES_TO_SERVE,
  PROP_BYTES_SERVED,
  PROP_SOCKFD,
  PROP_CLOSEFD,
  PROP_SOCK,
  PROP_CLIENTS,
  PROP_AUTO_MULTICAST,
  PROP_TTL,
  PROP_TTL_MC,
  PROP_LOOP,
  PROP_QOS_DSCP,
  PROP_SEND_DUPLICATES,
  PROP_BUFFER_SIZE,
  PROP_LAST
};

#define CLOSE_IF_REQUESTED(udpctx)                                        \
G_STMT_START {                                                            \
  if ((!udpctx->externalfd) || (udpctx->externalfd && udpctx->closefd)) { \
    CLOSE_SOCKET(udpctx->sock);                                           \
    if (udpctx->sock == udpctx->sockfd)                                   \
      udpctx->sockfd = DEFAULT_SOCKFD;                                    \
  }                                                                       \
  udpctx->sock = DEFAULT_SOCK;                                            \
} G_STMT_END

static void gst_multiudpsink_base_init (gpointer g_class);
static void gst_multiudpsink_class_init (GstMultiUDPSinkClass * klass);
static void gst_multiudpsink_init (GstMultiUDPSink * udpsink);
static void gst_multiudpsink_finalize (GObject * object);

static GstFlowReturn gst_multiudpsink_render (GstBaseSink * sink,
    GstBuffer * buffer);
#ifndef G_OS_WIN32              /* sendmsg() is not available on Windows */
static GstFlowReturn gst_multiudpsink_render_list (GstBaseSink * bsink,
    GstBufferList * list);
#endif
static GstStateChangeReturn gst_multiudpsink_change_state (GstElement *
    element, GstStateChange transition);

static void gst_multiudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multiudpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_multiudpsink_add_internal (GstMultiUDPSink * sink,
    const gchar * host, gint port, gboolean lock);
static void gst_multiudpsink_clear_internal (GstMultiUDPSink * sink,
    gboolean lock);

static GstElementClass *parent_class = NULL;

static guint gst_multiudpsink_signals[LAST_SIGNAL] = { 0 };

GType
gst_multiudpsink_get_type (void)
{
  static GType multiudpsink_type = 0;

  if (!multiudpsink_type) {
    static const GTypeInfo multiudpsink_info = {
      sizeof (GstMultiUDPSinkClass),
      gst_multiudpsink_base_init,
      NULL,
      (GClassInitFunc) gst_multiudpsink_class_init,
      NULL,
      NULL,
      sizeof (GstMultiUDPSink),
      0,
      (GInstanceInitFunc) gst_multiudpsink_init,
      NULL
    };

    multiudpsink_type =
        g_type_register_static (GST_TYPE_BASE_SINK, "GstMultiUDPSink",
        &multiudpsink_info, 0);
  }
  return multiudpsink_type;
}

static void
gst_multiudpsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_details_simple (element_class, "UDP packet sender",
      "Sink/Network",
      "Send data over the network via UDP",
      "Wim Taymans <wim.taymans@gmail.com>");
}

static void
gst_multiudpsink_class_init (GstMultiUDPSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_multiudpsink_set_property;
  gobject_class->get_property = gst_multiudpsink_get_property;
  gobject_class->finalize = gst_multiudpsink_finalize;

  /**
   * GstMultiUDPSink::add:
   * @gstmultiudpsink: the sink on which the signal is emitted
   * @host: the hostname/IP address of the client to add
   * @port: the port of the client to add
   *
   * Add a client with destination @host and @port to the list of
   * clients. When the same host/port pair is added multiple times, the
   * send-duplicates property defines if the packets are sent multiple times to
   * the same host/port pair or not.
   *
   * When a host/port pair is added multiple times, an equal amount of remove
   * calls must be performed to actually remove the host/port pair from the list
   * of destinations.
   */
  gst_multiudpsink_signals[SIGNAL_ADD] =
      g_signal_new ("add", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, add),
      NULL, NULL, gst_udp_marshal_VOID__STRING_INT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_INT);
  /**
   * GstMultiUDPSink::remove:
   * @gstmultiudpsink: the sink on which the signal is emitted
   * @host: the hostname/IP address of the client to remove
   * @port: the port of the client to remove
   *
   * Remove the client with destination @host and @port from the list of
   * clients.
   */
  gst_multiudpsink_signals[SIGNAL_REMOVE] =
      g_signal_new ("remove", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, remove),
      NULL, NULL, gst_udp_marshal_VOID__STRING_INT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_INT);
  /**
   * GstMultiUDPSink::clear:
   * @gstmultiudpsink: the sink on which the signal is emitted
   *
   * Clear the list of clients.
   */
  gst_multiudpsink_signals[SIGNAL_CLEAR] =
      g_signal_new ("clear", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, clear),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  /**
   * GstMultiUDPSink::get-stats:
   * @gstmultiudpsink: the sink on which the signal is emitted
   * @host: the hostname/IP address of the client to get stats on
   * @port: the port of the client to get stats on
   *
   * Get the statistics of the client with destination @host and @port.
   *
   * Returns: a GValueArray of uint64: bytes_sent, packets_sent,
   *           connect_time (in epoch seconds), disconnect_time (in epoch seconds)
   */
  gst_multiudpsink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, get_stats),
      NULL, NULL, gst_udp_marshal_BOXED__STRING_INT, G_TYPE_VALUE_ARRAY, 2,
      G_TYPE_STRING, G_TYPE_INT);
  /**
   * GstMultiUDPSink::client-added:
   * @gstmultiudpsink: the sink emitting the signal
   * @host: the hostname/IP address of the added client
   * @port: the port of the added client
   *
   * Signal emited when a new client is added to the list of
   * clients.
   */
  gst_multiudpsink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiUDPSinkClass, client_added),
      NULL, NULL, gst_udp_marshal_VOID__STRING_INT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_INT);
  /**
   * GstMultiUDPSink::client-removed:
   * @gstmultiudpsink: the sink emitting the signal
   * @host: the hostname/IP address of the removed client
   * @port: the port of the removed client
   *
   * Signal emited when a client is removed from the list of
   * clients.
   */
  gst_multiudpsink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiUDPSinkClass,
          client_removed), NULL, NULL, gst_udp_marshal_VOID__STRING_INT,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BYTES_TO_SERVE,
      g_param_spec_uint64 ("bytes-to-serve", "Bytes to serve",
          "Number of bytes received to serve to clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BYTES_SERVED,
      g_param_spec_uint64 ("bytes-served", "Bytes served",
          "Total number of bytes sent to all clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SOCKFD,
      g_param_spec_int ("sockfd", "Socket Handle",
          "Socket to use for UDP sending. (-1 == allocate)",
          -1, G_MAXINT, DEFAULT_SOCKFD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CLOSEFD,
      g_param_spec_boolean ("closefd", "Close sockfd",
          "Close sockfd if passed as property on state change",
          DEFAULT_CLOSEFD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SOCK,
      g_param_spec_int ("sock", "Socket Handle",
          "Socket currently in use for UDP sending. (-1 == no socket)",
          -1, G_MAXINT, DEFAULT_SOCK,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CLIENTS,
      g_param_spec_string ("clients", "Clients",
          "A comma separated list of host:port pairs with destinations",
          DEFAULT_CLIENTS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AUTO_MULTICAST,
      g_param_spec_boolean ("auto-multicast",
          "Automatically join/leave multicast groups",
          "Automatically join/leave the multicast groups, FALSE means user"
          " has to do it himself", DEFAULT_AUTO_MULTICAST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TTL,
      g_param_spec_int ("ttl", "Unicast TTL",
          "Used for setting the unicast TTL parameter",
          0, 255, DEFAULT_TTL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TTL_MC,
      g_param_spec_int ("ttl-mc", "Multicast TTL",
          "Used for setting the multicast TTL parameter",
          0, 255, DEFAULT_TTL_MC, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_LOOP,
      g_param_spec_boolean ("loop", "Multicast Loopback",
          "Used for setting the multicast loop parameter. TRUE = enable,"
          " FALSE = disable", DEFAULT_LOOP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QOS_DSCP,
      g_param_spec_int ("qos-dscp", "QoS diff srv code point",
          "Quality of Service, differentiated services code point (-1 default)",
          -1, 63, DEFAULT_QOS_DSCP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiUDPSink::send-duplicates
   *
   * When a host/port pair is added mutliple times, send the packet to the host
   * multiple times as well.
   *
   * Since: 0.10.26
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEND_DUPLICATES,
      g_param_spec_boolean ("send-duplicates", "Send Duplicates",
          "When a distination/port pair is added multiple times, send packets "
          "multiple times as well", DEFAULT_SEND_DUPLICATES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFFER_SIZE,
      g_param_spec_int ("buffer-size", "Buffer Size",
          "Size of the kernel send buffer in bytes, 0=default", 0, G_MAXINT,
          DEFAULT_BUFFER_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = gst_multiudpsink_change_state;

  gstbasesink_class->render = gst_multiudpsink_render;
#ifndef G_OS_WIN32
  gstbasesink_class->render_list = gst_multiudpsink_render_list;
#endif
  klass->add = gst_multiudpsink_add;
  klass->remove = gst_multiudpsink_remove;
  klass->clear = gst_multiudpsink_clear;
  klass->get_stats = gst_multiudpsink_get_stats;

  GST_DEBUG_CATEGORY_INIT (multiudpsink_debug, "multiudpsink", 0, "UDP sink");
}


static void
gst_multiudpsink_init (GstMultiUDPSink * sink)
{
  WSA_STARTUP (sink);

  sink->client_lock = g_mutex_new ();
  sink->sock = DEFAULT_SOCK;
  sink->sockfd = DEFAULT_SOCKFD;
  sink->closefd = DEFAULT_CLOSEFD;
  sink->externalfd = (sink->sockfd != -1);
  sink->auto_multicast = DEFAULT_AUTO_MULTICAST;
  sink->ttl = DEFAULT_TTL;
  sink->ttl_mc = DEFAULT_TTL_MC;
  sink->loop = DEFAULT_LOOP;
  sink->qos_dscp = DEFAULT_QOS_DSCP;
  sink->ss_family = DEFAULT_FAMILY;
  sink->send_duplicates = DEFAULT_SEND_DUPLICATES;
}

static GstUDPClient *
create_client (GstMultiUDPSink * sink, const gchar * host, gint port)
{
  GstUDPClient *client;

  client = g_slice_new0 (GstUDPClient);
  client->refcount = 1;
  client->host = g_strdup (host);
  client->port = port;

  return client;
}

static void
free_client (GstUDPClient * client)
{
  g_free (client->host);
  g_slice_free (GstUDPClient, client);
}

static gint
client_compare (GstUDPClient * a, GstUDPClient * b)
{
  if ((a->port == b->port) && (strcmp (a->host, b->host) == 0))
    return 0;

  return 1;
}

static void
gst_multiudpsink_finalize (GObject * object)
{
  GstMultiUDPSink *sink;

  sink = GST_MULTIUDPSINK (object);

  g_list_foreach (sink->clients, (GFunc) free_client, NULL);
  g_list_free (sink->clients);

  if (sink->sockfd >= 0 && sink->closefd)
    CLOSE_SOCKET (sink->sockfd);

  g_mutex_free (sink->client_lock);

  WSA_CLEANUP (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
socket_error_is_ignorable (void)
{
#ifdef G_OS_WIN32
  /* Windows doesn't seem to have an EAGAIN for sockets */
  return WSAGetLastError () == WSAEINTR;
#else
  return errno == EINTR || errno == EAGAIN;
#endif
}

static int
socket_last_error_code (void)
{
#ifdef G_OS_WIN32
  return WSAGetLastError ();
#else
  return errno;
#endif
}

static gchar *
socket_last_error_message (void)
{
#ifdef G_OS_WIN32
  int errorcode = WSAGetLastError ();
  wchar_t buf[1024];
  DWORD result =
      FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, errorcode, 0, (LPSTR) buf, sizeof (buf) / sizeof (wchar_t), NULL);
  if (FAILED (result)) {
    return g_strdup ("failed to get error message from system");
  } else {
    gchar *res =
        g_convert ((gchar *) buf, -1, "UTF-16", "UTF-8", NULL, NULL, NULL);
    /* g_convert() internally calls windows functions which reset the
       windows error code, so fix it up again like this */
    WSASetLastError (errorcode);
    return res;
  }
#else
  return g_strdup (g_strerror (errno));
#endif
}

static GstFlowReturn
gst_multiudpsink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstMultiUDPSink *sink;
  gint ret, size, num = 0, no_clients = 0;
  guint8 *data;
  GList *clients;
  gint len;

  sink = GST_MULTIUDPSINK (bsink);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  if (size > UDP_MAX_SIZE) {
    GST_WARNING ("Attempting to send a UDP packet larger than maximum "
        "size (%d > %d)", size, UDP_MAX_SIZE);
  }

  sink->bytes_to_serve += size;

  /* grab lock while iterating and sending to clients, this should be
   * fast as UDP never blocks */
  g_mutex_lock (sink->client_lock);
  GST_LOG_OBJECT (bsink, "about to send %d bytes", size);

  for (clients = sink->clients; clients; clients = g_list_next (clients)) {
    GstUDPClient *client;
    gint count;

    client = (GstUDPClient *) clients->data;
    no_clients++;
    GST_LOG_OBJECT (sink, "sending %d bytes to client %p", size, client);

    count = sink->send_duplicates ? client->refcount : 1;

    while (count--) {
      while (TRUE) {
        len = gst_udp_get_sockaddr_length (&client->theiraddr);

        ret = sendto (*client->sock,
#ifdef G_OS_WIN32
            (char *) data,
#else
            data,
#endif
            size, 0, (struct sockaddr *) &client->theiraddr, len);

        if (ret < 0) {
          /* some error, just warn, it's likely recoverable and we don't want to
           * break streaming. We break so that we stop retrying for this client. */
          if (!socket_error_is_ignorable ()) {
            gchar *errormessage = socket_last_error_message ();
            GST_WARNING_OBJECT (sink, "client %p gave error %d (%s)", client,
                socket_last_error_code (), errormessage);
            g_free (errormessage);
            break;
          }
        } else {
          num++;
          client->bytes_sent += ret;
          client->packets_sent++;
          sink->bytes_served += ret;
          break;
        }
      }
    }
  }
  g_mutex_unlock (sink->client_lock);

  GST_LOG_OBJECT (sink, "sent %d bytes to %d (of %d) clients", size, num,
      no_clients);

  return GST_FLOW_OK;
}

#ifndef G_OS_WIN32
static GstFlowReturn
gst_multiudpsink_render_list (GstBaseSink * bsink, GstBufferList * list)
{
  GstMultiUDPSink *sink;
  GList *clients;
  gint ret, size = 0, num = 0, no_clients = 0;
  struct iovec *iov;
  struct msghdr msg = { 0 };

  GstBufferListIterator *it;
  guint gsize;
  GstBuffer *buf;

  sink = GST_MULTIUDPSINK (bsink);

  g_return_val_if_fail (list != NULL, GST_FLOW_ERROR);

  it = gst_buffer_list_iterate (list);
  g_return_val_if_fail (it != NULL, GST_FLOW_ERROR);

  while (gst_buffer_list_iterator_next_group (it)) {
    msg.msg_iovlen = 0;
    size = 0;

    if ((gsize = gst_buffer_list_iterator_n_buffers (it)) == 0) {
      goto invalid_list;
    }

    iov = (struct iovec *) g_malloc (gsize * sizeof (struct iovec));
    msg.msg_iov = iov;

    while ((buf = gst_buffer_list_iterator_next (it))) {
      if (GST_BUFFER_SIZE (buf) > UDP_MAX_SIZE) {
        GST_WARNING ("Attempting to send a UDP packet larger than maximum "
            "size (%d > %d)", GST_BUFFER_SIZE (buf), UDP_MAX_SIZE);
      }

      msg.msg_iov[msg.msg_iovlen].iov_len = GST_BUFFER_SIZE (buf);
      msg.msg_iov[msg.msg_iovlen].iov_base = GST_BUFFER_DATA (buf);
      msg.msg_iovlen++;
      size += GST_BUFFER_SIZE (buf);
    }

    sink->bytes_to_serve += size;

    /* grab lock while iterating and sending to clients, this should be
     * fast as UDP never blocks */
    g_mutex_lock (sink->client_lock);
    GST_LOG_OBJECT (bsink, "about to send %d bytes", size);

    for (clients = sink->clients; clients; clients = g_list_next (clients)) {
      GstUDPClient *client;
      gint count;

      client = (GstUDPClient *) clients->data;
      no_clients++;
      GST_LOG_OBJECT (sink, "sending %d bytes to client %p", size, client);

      count = sink->send_duplicates ? client->refcount : 1;

      while (count--) {
        while (TRUE) {
          msg.msg_name = (void *) &client->theiraddr;
          msg.msg_namelen = sizeof (client->theiraddr);
          ret = sendmsg (*client->sock, &msg, 0);

          if (ret < 0) {
            if (!socket_error_is_ignorable ()) {
              break;
            }
          } else {
            num++;
            client->bytes_sent += ret;
            client->packets_sent++;
            sink->bytes_served += ret;
            break;
          }
        }
      }
    }
    g_mutex_unlock (sink->client_lock);

    g_free (iov);
    msg.msg_iov = NULL;

    GST_LOG_OBJECT (sink, "sent %d bytes to %d (of %d) clients", size, num,
        no_clients);
  }

  gst_buffer_list_iterator_free (it);

  return GST_FLOW_OK;

invalid_list:
  gst_buffer_list_iterator_free (it);
  return GST_FLOW_ERROR;
}
#endif

static void
gst_multiudpsink_set_clients_string (GstMultiUDPSink * sink,
    const gchar * string)
{
  gchar **clients;
  gint i;

  clients = g_strsplit (string, ",", 0);

  g_mutex_lock (sink->client_lock);
  /* clear all existing clients */
  gst_multiudpsink_clear_internal (sink, FALSE);
  for (i = 0; clients[i]; i++) {
    gchar *host, *p;
    gint port = 0;

    host = clients[i];
    p = strstr (clients[i], ":");
    if (p != NULL) {
      *p = '\0';
      port = atoi (p + 1);
    }
    if (port != 0)
      gst_multiudpsink_add_internal (sink, host, port, FALSE);
  }
  g_mutex_unlock (sink->client_lock);

  g_strfreev (clients);
}

static gchar *
gst_multiudpsink_get_clients_string (GstMultiUDPSink * sink)
{
  GString *str;
  GList *clients;

  str = g_string_new ("");

  g_mutex_lock (sink->client_lock);
  clients = sink->clients;
  while (clients) {
    GstUDPClient *client;
    gint count;

    client = (GstUDPClient *) clients->data;

    clients = g_list_next (clients);

    count = client->refcount;
    while (count--) {
      g_string_append_printf (str, "%s:%d%s", client->host, client->port,
          (clients || count > 1 ? "," : ""));
    }
  }
  g_mutex_unlock (sink->client_lock);

  return g_string_free (str, FALSE);
}

static void
gst_multiudpsink_setup_qos_dscp (GstMultiUDPSink * sink)
{
  gint tos;

  /* don't touch on -1 */
  if (sink->qos_dscp < 0)
    return;

  if (sink->sock < 0)
    return;

  GST_DEBUG_OBJECT (sink, "setting TOS to %d", sink->qos_dscp);

  /* Extract and shift 6 bits of DSFIELD */
  tos = (sink->qos_dscp & 0x3f) << 2;

  if (setsockopt (sink->sock, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0) {
    gchar *errormessage = socket_last_error_message ();
    GST_ERROR_OBJECT (sink, "could not set TOS: %s", errormessage);
    g_free (errormessage);
  }
#ifdef IPV6_TCLASS
  if (setsockopt (sink->sock, IPPROTO_IPV6, IPV6_TCLASS, &tos,
          sizeof (tos)) < 0) {
    gchar *errormessage = socket_last_error_message ();
    GST_ERROR_OBJECT (sink, "could not set TCLASS: %s", errormessage);
    g_free (errormessage);
  }
#endif
}

static void
gst_multiudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiUDPSink *udpsink;

  udpsink = GST_MULTIUDPSINK (object);

  switch (prop_id) {
    case PROP_SOCKFD:
      if (udpsink->sockfd >= 0 && udpsink->sockfd != udpsink->sock &&
          udpsink->closefd)
        CLOSE_SOCKET (udpsink->sockfd);
      udpsink->sockfd = g_value_get_int (value);
      GST_DEBUG_OBJECT (udpsink, "setting SOCKFD to %d", udpsink->sockfd);
      break;
    case PROP_CLOSEFD:
      udpsink->closefd = g_value_get_boolean (value);
      break;
    case PROP_CLIENTS:
      gst_multiudpsink_set_clients_string (udpsink, g_value_get_string (value));
      break;
    case PROP_AUTO_MULTICAST:
      udpsink->auto_multicast = g_value_get_boolean (value);
      break;
    case PROP_TTL:
      udpsink->ttl = g_value_get_int (value);
      break;
    case PROP_TTL_MC:
      udpsink->ttl_mc = g_value_get_int (value);
      break;
    case PROP_LOOP:
      udpsink->loop = g_value_get_boolean (value);
      break;
    case PROP_QOS_DSCP:
      udpsink->qos_dscp = g_value_get_int (value);
      gst_multiudpsink_setup_qos_dscp (udpsink);
      break;
    case PROP_SEND_DUPLICATES:
      udpsink->send_duplicates = g_value_get_boolean (value);
      break;
    case PROP_BUFFER_SIZE:
      udpsink->buffer_size = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_multiudpsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMultiUDPSink *udpsink;

  udpsink = GST_MULTIUDPSINK (object);

  switch (prop_id) {
    case PROP_BYTES_TO_SERVE:
      g_value_set_uint64 (value, udpsink->bytes_to_serve);
      break;
    case PROP_BYTES_SERVED:
      g_value_set_uint64 (value, udpsink->bytes_served);
      break;
    case PROP_SOCKFD:
      g_value_set_int (value, udpsink->sockfd);
      break;
    case PROP_CLOSEFD:
      g_value_set_boolean (value, udpsink->closefd);
      break;
    case PROP_SOCK:
      g_value_set_int (value, udpsink->sock);
      break;
    case PROP_CLIENTS:
      g_value_take_string (value,
          gst_multiudpsink_get_clients_string (udpsink));
      break;
    case PROP_AUTO_MULTICAST:
      g_value_set_boolean (value, udpsink->auto_multicast);
      break;
    case PROP_TTL:
      g_value_set_int (value, udpsink->ttl);
      break;
    case PROP_TTL_MC:
      g_value_set_int (value, udpsink->ttl_mc);
      break;
    case PROP_LOOP:
      g_value_set_boolean (value, udpsink->loop);
      break;
    case PROP_QOS_DSCP:
      g_value_set_int (value, udpsink->qos_dscp);
      break;
    case PROP_SEND_DUPLICATES:
      g_value_set_boolean (value, udpsink->send_duplicates);
      break;
    case PROP_BUFFER_SIZE:
      g_value_set_int (value, udpsink->buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_multiudpsink_configure_client (GstMultiUDPSink * sink,
    GstUDPClient * client)
{
  GST_DEBUG_OBJECT (sink, "configuring client %p", client);

  if (gst_udp_is_multicast (&client->theiraddr)) {
    GST_DEBUG_OBJECT (sink, "we have a multicast client %p", client);
    if (sink->auto_multicast) {
      GST_DEBUG_OBJECT (sink, "autojoining group");
      if (gst_udp_join_group (*(client->sock), &client->theiraddr, NULL)
          != 0)
        goto join_group_failed;
    }
    GST_DEBUG_OBJECT (sink, "setting loop to %d", sink->loop);
    if (gst_udp_set_loop (sink->sock, sink->ss_family, sink->loop) != 0)
      goto loop_failed;
    GST_DEBUG_OBJECT (sink, "setting ttl to %d", sink->ttl_mc);
    if (gst_udp_set_ttl (sink->sock, sink->ss_family, sink->ttl_mc, TRUE) != 0)
      goto ttl_failed;
  } else {
    GST_DEBUG_OBJECT (sink, "setting unicast ttl to %d", sink->ttl);
    if (gst_udp_set_ttl (sink->sock, sink->ss_family, sink->ttl, FALSE) != 0)
      goto ttl_failed;
  }
  return TRUE;

  /* ERRORS */
join_group_failed:
  {
    gchar *errormessage = socket_last_error_message ();
    int errorcode = socket_last_error_code ();
    CLOSE_IF_REQUESTED (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not join multicast group (%d): %s", errorcode, errormessage));
    g_free (errormessage);
    return FALSE;
  }
ttl_failed:
  {
    gchar *errormessage = socket_last_error_message ();
    int errorcode = socket_last_error_code ();
    CLOSE_IF_REQUESTED (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not set TTL socket option (%d): %s", errorcode, errormessage));
    g_free (errormessage);
    return FALSE;
  }
loop_failed:
  {
    gchar *errormessage = socket_last_error_message ();
    int errorcode = socket_last_error_code ();
    CLOSE_IF_REQUESTED (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not set loopback socket option (%d): %s",
            errorcode, errormessage));
    g_free (errormessage);
    return FALSE;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_multiudpsink_init_send (GstMultiUDPSink * sink)
{
  guint bc_val;
  GList *clients;
  GstUDPClient *client;
  int sndsize, ret;
  socklen_t len;

  if (sink->sockfd == -1) {
    GST_DEBUG_OBJECT (sink, "creating sockets");
    /* create sender socket try IP6, fall back to IP4 */
    sink->ss_family = AF_INET6;
    if ((sink->sock = socket (AF_INET6, SOCK_DGRAM, 0)) == -1) {
      sink->ss_family = AF_INET;
      if ((sink->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
        goto no_socket;
    }

    GST_DEBUG_OBJECT (sink, "have socket");
    sink->externalfd = FALSE;
  } else {
    struct sockaddr_storage myaddr;

    GST_DEBUG_OBJECT (sink, "using configured socket");
    /* we use the configured socket, try to get some info about it */
    len = sizeof (myaddr);
    if (getsockname (sink->sockfd, (struct sockaddr *) &myaddr, &len) < 0)
      goto getsockname_error;

    sink->ss_family = myaddr.ss_family;
    /* we use the configured socket */
    sink->sock = sink->sockfd;
    sink->externalfd = TRUE;
  }

  len = sizeof (sndsize);
  if (sink->buffer_size != 0) {
    sndsize = sink->buffer_size;

    GST_DEBUG_OBJECT (sink, "setting udp buffer of %d bytes", sndsize);
    /* set buffer size, Note that on Linux this is typically limited to a
     * maximum of around 100K. Also a minimum of 128 bytes is required on
     * Linux. */
    ret =
        setsockopt (sink->sockfd, SOL_SOCKET, SO_SNDBUF, (void *) &sndsize,
        len);
    if (ret != 0) {
      GST_ELEMENT_WARNING (sink, RESOURCE, SETTINGS, (NULL),
          ("Could not create a buffer of requested %d bytes, %d: %s (%d)",
              sndsize, ret, g_strerror (errno), errno));
    }
  }

  /* read the value of the receive buffer. Note that on linux this returns 2x the
   * value we set because the kernel allocates extra memory for metadata.
   * The default on Linux is about 100K (which is about 50K without metadata) */
  ret =
      getsockopt (sink->sockfd, SOL_SOCKET, SO_SNDBUF, (void *) &sndsize, &len);
  if (ret == 0)
    GST_DEBUG_OBJECT (sink, "have udp buffer of %d bytes", sndsize);
  else
    GST_DEBUG_OBJECT (sink, "could not get udp buffer size");


  bc_val = 1;
  if (setsockopt (sink->sock, SOL_SOCKET, SO_BROADCAST, &bc_val,
          sizeof (bc_val)) < 0)
    goto no_broadcast;

  sink->bytes_to_serve = 0;
  sink->bytes_served = 0;

  gst_multiudpsink_setup_qos_dscp (sink);

  /* look for multicast clients and join multicast groups appropriately
     set also ttl and multicast loopback delivery appropriately  */
  for (clients = sink->clients; clients; clients = g_list_next (clients)) {
    client = (GstUDPClient *) clients->data;

    if (!gst_multiudpsink_configure_client (sink, client))
      return FALSE;
  }
  return TRUE;

  /* ERRORS */
no_socket:
  {
    gchar *errormessage = socket_last_error_message ();
    int errorcode = socket_last_error_code ();
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (NULL),
        ("Could not create socket (%d): %s", errorcode, errormessage));
    g_free (errormessage);
    return FALSE;
  }
getsockname_error:
  {
    gchar *errormessage = socket_last_error_message ();
    int errorcode = socket_last_error_code ();
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (NULL),
        ("Could not getsockname (%d): %s", errorcode, errormessage));
    g_free (errormessage);
    return FALSE;
  }
no_broadcast:
  {
    gchar *errormessage = socket_last_error_message ();
    int errorcode = socket_last_error_code ();
    CLOSE_IF_REQUESTED (sink);
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not set broadcast socket option (%d): %s",
            errorcode, errormessage));
    g_free (errormessage);
    return FALSE;
  }
}

static void
gst_multiudpsink_close (GstMultiUDPSink * sink)
{
  CLOSE_IF_REQUESTED (sink);
}

static void
gst_multiudpsink_add_internal (GstMultiUDPSink * sink, const gchar * host,
    gint port, gboolean lock)
{
  GstUDPClient *client;
  GstUDPClient udpclient;
  GTimeVal now;
  GList *find;

  udpclient.host = (gchar *) host;
  udpclient.port = port;

  GST_DEBUG_OBJECT (sink, "adding client on host %s, port %d", host, port);

  if (lock)
    g_mutex_lock (sink->client_lock);

  find = g_list_find_custom (sink->clients, &udpclient,
      (GCompareFunc) client_compare);
  if (find) {
    client = (GstUDPClient *) find->data;

    GST_DEBUG_OBJECT (sink, "found %d existing clients with host %s, port %d",
        client->refcount, host, port);
    client->refcount++;
  } else {
    client = create_client (sink, host, port);

    client->sock = &sink->sock;

    if (gst_udp_get_addr (host, port, &client->theiraddr) < 0)
      goto getaddrinfo_error;

    g_get_current_time (&now);
    client->connect_time = GST_TIMEVAL_TO_TIME (now);

    if (*client->sock > 0) {
      gst_multiudpsink_configure_client (sink, client);
    }

    GST_DEBUG_OBJECT (sink, "add client with host %s, port %d", host, port);
    sink->clients = g_list_prepend (sink->clients, client);
  }

  if (lock)
    g_mutex_unlock (sink->client_lock);

  g_signal_emit (G_OBJECT (sink),
      gst_multiudpsink_signals[SIGNAL_CLIENT_ADDED], 0, host, port);

  GST_DEBUG_OBJECT (sink, "added client on host %s, port %d", host, port);
  return;

  /* ERRORS */
getaddrinfo_error:
  {
    GST_DEBUG_OBJECT (sink, "did not add client on host %s, port %d", host,
        port);
    GST_WARNING_OBJECT (sink, "getaddrinfo lookup error?");
    free_client (client);
    if (lock)
      g_mutex_unlock (sink->client_lock);
    return;
  }
}

void
gst_multiudpsink_add (GstMultiUDPSink * sink, const gchar * host, gint port)
{
  gst_multiudpsink_add_internal (sink, host, port, TRUE);
}

void
gst_multiudpsink_remove (GstMultiUDPSink * sink, const gchar * host, gint port)
{
  GList *find;
  GstUDPClient udpclient;
  GstUDPClient *client;
  GTimeVal now;

  udpclient.host = (gchar *) host;
  udpclient.port = port;

  g_mutex_lock (sink->client_lock);
  find = g_list_find_custom (sink->clients, &udpclient,
      (GCompareFunc) client_compare);
  if (!find)
    goto not_found;

  client = (GstUDPClient *) find->data;

  GST_DEBUG_OBJECT (sink, "found %d clients with host %s, port %d",
      client->refcount, host, port);

  client->refcount--;
  if (client->refcount == 0) {
    GST_DEBUG_OBJECT (sink, "remove client with host %s, port %d", host, port);

    g_get_current_time (&now);
    client->disconnect_time = GST_TIMEVAL_TO_TIME (now);

    if (*(client->sock) != -1 && sink->auto_multicast
        && gst_udp_is_multicast (&client->theiraddr))
      gst_udp_leave_group (*(client->sock), &client->theiraddr);

    /* Unlock to emit signal before we delete the actual client */
    g_mutex_unlock (sink->client_lock);
    g_signal_emit (G_OBJECT (sink),
        gst_multiudpsink_signals[SIGNAL_CLIENT_REMOVED], 0, host, port);
    g_mutex_lock (sink->client_lock);

    sink->clients = g_list_delete_link (sink->clients, find);

    free_client (client);
  }
  g_mutex_unlock (sink->client_lock);

  return;

  /* ERRORS */
not_found:
  {
    g_mutex_unlock (sink->client_lock);
    GST_WARNING_OBJECT (sink, "client at host %s, port %d not found",
        host, port);
    return;
  }
}

static void
gst_multiudpsink_clear_internal (GstMultiUDPSink * sink, gboolean lock)
{
  GST_DEBUG_OBJECT (sink, "clearing");
  /* we only need to remove the client structure, there is no additional
   * socket or anything to free for UDP */
  if (lock)
    g_mutex_lock (sink->client_lock);
  g_list_foreach (sink->clients, (GFunc) free_client, sink);
  g_list_free (sink->clients);
  sink->clients = NULL;
  if (lock)
    g_mutex_unlock (sink->client_lock);
}

void
gst_multiudpsink_clear (GstMultiUDPSink * sink)
{
  gst_multiudpsink_clear_internal (sink, TRUE);
}

GValueArray *
gst_multiudpsink_get_stats (GstMultiUDPSink * sink, const gchar * host,
    gint port)
{
  GstUDPClient *client;
  GValueArray *result = NULL;
  GstUDPClient udpclient;
  GList *find;
  GValue value = { 0 };

  udpclient.host = (gchar *) host;
  udpclient.port = port;

  g_mutex_lock (sink->client_lock);

  find = g_list_find_custom (sink->clients, &udpclient,
      (GCompareFunc) client_compare);
  if (!find)
    goto not_found;

  GST_DEBUG_OBJECT (sink, "stats for client with host %s, port %d", host, port);

  client = (GstUDPClient *) find->data;

  /* Result is a value array of (bytes_sent, packets_sent,
   * connect_time, disconnect_time), all as uint64 */
  result = g_value_array_new (4);

  g_value_init (&value, G_TYPE_UINT64);
  g_value_set_uint64 (&value, client->bytes_sent);
  result = g_value_array_append (result, &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_UINT64);
  g_value_set_uint64 (&value, client->packets_sent);
  result = g_value_array_append (result, &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_UINT64);
  g_value_set_uint64 (&value, client->connect_time);
  result = g_value_array_append (result, &value);
  g_value_unset (&value);

  g_value_init (&value, G_TYPE_UINT64);
  g_value_set_uint64 (&value, client->disconnect_time);
  result = g_value_array_append (result, &value);
  g_value_unset (&value);

  g_mutex_unlock (sink->client_lock);

  return result;

  /* ERRORS */
not_found:
  {
    g_mutex_unlock (sink->client_lock);
    GST_WARNING_OBJECT (sink, "client with host %s, port %d not found",
        host, port);
    /* Apparently (see comment in gstmultifdsink.c) returning NULL from here may
     * confuse/break python bindings */
    return g_value_array_new (0);
  }
}

static GstStateChangeReturn
gst_multiudpsink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMultiUDPSink *sink;

  sink = GST_MULTIUDPSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_multiudpsink_init_send (sink))
        goto no_init;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_multiudpsink_close (sink);
      break;
    default:
      break;
  }
  return ret;

  /* ERRORS */
no_init:
  {
    /* _init_send() posted specific error already */
    return GST_STATE_CHANGE_FAILURE;
  }
}
