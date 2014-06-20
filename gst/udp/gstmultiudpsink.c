/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) <2009> Jarkko Palviainen <jarkko.palviainen@sesca.com>
 * Copyright (C) <2012> Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include "gstmultiudpsink.h"

#include <string.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifndef G_OS_WIN32
#include <netinet/in.h>
#endif

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

#define DEFAULT_SOCKET             NULL
#define DEFAULT_CLOSE_SOCKET       TRUE
#define DEFAULT_USED_SOCKET        NULL
#define DEFAULT_CLIENTS            NULL
/* FIXME, this should be disabled by default, we don't need to join a multicast
 * group for sending, if this socket is also used for receiving, it should
 * be configured in the element that does the receive. */
#define DEFAULT_AUTO_MULTICAST     TRUE
#define DEFAULT_MULTICAST_IFACE    NULL
#define DEFAULT_TTL                64
#define DEFAULT_TTL_MC             1
#define DEFAULT_LOOP               TRUE
#define DEFAULT_FORCE_IPV4         FALSE
#define DEFAULT_QOS_DSCP           -1
#define DEFAULT_SEND_DUPLICATES    TRUE
#define DEFAULT_BUFFER_SIZE        0
#define DEFAULT_BIND_ADDRESS       NULL
#define DEFAULT_BIND_PORT          0

enum
{
  PROP_0,
  PROP_BYTES_TO_SERVE,
  PROP_BYTES_SERVED,
  PROP_SOCKET,
  PROP_SOCKET_V6,
  PROP_CLOSE_SOCKET,
  PROP_USED_SOCKET,
  PROP_USED_SOCKET_V6,
  PROP_CLIENTS,
  PROP_AUTO_MULTICAST,
  PROP_MULTICAST_IFACE,
  PROP_TTL,
  PROP_TTL_MC,
  PROP_LOOP,
  PROP_FORCE_IPV4,
  PROP_QOS_DSCP,
  PROP_SEND_DUPLICATES,
  PROP_BUFFER_SIZE,
  PROP_BIND_ADDRESS,
  PROP_BIND_PORT,
  PROP_LAST
};

static void gst_multiudpsink_finalize (GObject * object);

static GstFlowReturn gst_multiudpsink_render (GstBaseSink * sink,
    GstBuffer * buffer);

static gboolean gst_multiudpsink_start (GstBaseSink * bsink);
static gboolean gst_multiudpsink_stop (GstBaseSink * bsink);
static gboolean gst_multiudpsink_unlock (GstBaseSink * bsink);
static gboolean gst_multiudpsink_unlock_stop (GstBaseSink * bsink);

static void gst_multiudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multiudpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_multiudpsink_add_internal (GstMultiUDPSink * sink,
    const gchar * host, gint port, gboolean lock);
static void gst_multiudpsink_clear_internal (GstMultiUDPSink * sink,
    gboolean lock);

static guint gst_multiudpsink_signals[LAST_SIGNAL] = { 0 };

#define gst_multiudpsink_parent_class parent_class
G_DEFINE_TYPE (GstMultiUDPSink, gst_multiudpsink, GST_TYPE_BASE_SINK);

static void
gst_multiudpsink_class_init (GstMultiUDPSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

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
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2,
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
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2,
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
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 0);
  /**
   * GstMultiUDPSink::get-stats:
   * @gstmultiudpsink: the sink on which the signal is emitted
   * @host: the hostname/IP address of the client to get stats on
   * @port: the port of the client to get stats on
   *
   * Get the statistics of the client with destination @host and @port.
   *
   * Returns: a GstStructure: bytes_sent, packets_sent,
   *           connect_time (in epoch seconds), disconnect_time (in epoch seconds)
   */
  gst_multiudpsink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, get_stats),
      NULL, NULL, g_cclosure_marshal_generic, GST_TYPE_STRUCTURE, 2,
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
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 2,
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
          client_removed), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BYTES_TO_SERVE,
      g_param_spec_uint64 ("bytes-to-serve", "Bytes to serve",
          "Number of bytes received to serve to clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BYTES_SERVED,
      g_param_spec_uint64 ("bytes-served", "Bytes served",
          "Total number of bytes sent to all clients", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SOCKET,
      g_param_spec_object ("socket", "Socket Handle",
          "Socket to use for UDP sending. (NULL == allocate)",
          G_TYPE_SOCKET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SOCKET_V6,
      g_param_spec_object ("socket-v6", "Socket Handle IPv6",
          "Socket to use for UDPv6 sending. (NULL == allocate)",
          G_TYPE_SOCKET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CLOSE_SOCKET,
      g_param_spec_boolean ("close-socket", "Close socket",
          "Close socket if passed as property on state change",
          DEFAULT_CLOSE_SOCKET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USED_SOCKET,
      g_param_spec_object ("used-socket", "Used Socket Handle",
          "Socket currently in use for UDP sending. (NULL == no socket)",
          G_TYPE_SOCKET, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USED_SOCKET_V6,
      g_param_spec_object ("used-socket-v6", "Used Socket Handle IPv6",
          "Socket currently in use for UDPv6 sending. (NULL == no socket)",
          G_TYPE_SOCKET, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
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
  g_object_class_install_property (gobject_class, PROP_MULTICAST_IFACE,
      g_param_spec_string ("multicast-iface", "Multicast Interface",
          "The network interface on which to join the multicast group",
          DEFAULT_MULTICAST_IFACE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
  /**
   * GstMultiUDPSink::force-ipv4:
   *
   * Force the use of an IPv4 socket.
   *
   * Since: 1.0.2
   */
  g_object_class_install_property (gobject_class, PROP_FORCE_IPV4,
      g_param_spec_boolean ("force-ipv4", "Force IPv4",
          "Forcing the use of an IPv4 socket (DEPRECATED, has no effect anymore)",
          DEFAULT_FORCE_IPV4, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QOS_DSCP,
      g_param_spec_int ("qos-dscp", "QoS diff srv code point",
          "Quality of Service, differentiated services code point (-1 default)",
          -1, 63, DEFAULT_QOS_DSCP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /**
   * GstMultiUDPSink::send-duplicates:
   *
   * When a host/port pair is added mutliple times, send the packet to the host
   * multiple times as well.
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

  g_object_class_install_property (gobject_class, PROP_BIND_ADDRESS,
      g_param_spec_string ("bind-address", "Bind Address",
          "Address to bind the socket to", DEFAULT_BIND_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIND_PORT,
      g_param_spec_int ("bind-port", "Bind Port",
          "Port to bind the socket to", 0, G_MAXUINT16,
          DEFAULT_BIND_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_static_metadata (gstelement_class, "UDP packet sender",
      "Sink/Network",
      "Send data over the network via UDP to one or multiple recipients "
      "which can be added or removed at runtime using action signals",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstbasesink_class->render = gst_multiudpsink_render;
  gstbasesink_class->start = gst_multiudpsink_start;
  gstbasesink_class->stop = gst_multiudpsink_stop;
  gstbasesink_class->unlock = gst_multiudpsink_unlock;
  gstbasesink_class->unlock_stop = gst_multiudpsink_unlock_stop;
  klass->add = gst_multiudpsink_add;
  klass->remove = gst_multiudpsink_remove;
  klass->clear = gst_multiudpsink_clear;
  klass->get_stats = gst_multiudpsink_get_stats;

  GST_DEBUG_CATEGORY_INIT (multiudpsink_debug, "multiudpsink", 0, "UDP sink");
}


static void
gst_multiudpsink_init (GstMultiUDPSink * sink)
{
  guint max_mem;

  g_mutex_init (&sink->client_lock);
  sink->socket = DEFAULT_SOCKET;
  sink->socket_v6 = DEFAULT_SOCKET;
  sink->used_socket = DEFAULT_USED_SOCKET;
  sink->used_socket_v6 = DEFAULT_USED_SOCKET;
  sink->close_socket = DEFAULT_CLOSE_SOCKET;
  sink->external_socket = (sink->socket != NULL);
  sink->auto_multicast = DEFAULT_AUTO_MULTICAST;
  sink->ttl = DEFAULT_TTL;
  sink->ttl_mc = DEFAULT_TTL_MC;
  sink->loop = DEFAULT_LOOP;
  sink->force_ipv4 = DEFAULT_FORCE_IPV4;
  sink->qos_dscp = DEFAULT_QOS_DSCP;
  sink->send_duplicates = DEFAULT_SEND_DUPLICATES;
  sink->multi_iface = g_strdup (DEFAULT_MULTICAST_IFACE);

  sink->cancellable = g_cancellable_new ();

  /* allocate OutputVector and MapInfo for use in the render function, buffers can
   * hold up to a maximum amount of memory so we can create a maximally sized
   * array for them.  */
  max_mem = gst_buffer_get_max_memory ();

  sink->vec = g_new (GOutputVector, max_mem);
  sink->map = g_new (GstMapInfo, max_mem);
}

static GstUDPClient *
create_client (GstMultiUDPSink * sink, const gchar * host, gint port)
{
  GstUDPClient *client;
  GInetAddress *addr;
  GResolver *resolver;
  GError *err = NULL;

  addr = g_inet_address_new_from_string (host);
  if (!addr) {
    GList *results;

    resolver = g_resolver_get_default ();
    results =
        g_resolver_lookup_by_name (resolver, host, sink->cancellable, &err);
    if (!results)
      goto name_resolve;
    addr = G_INET_ADDRESS (g_object_ref (results->data));

    g_resolver_free_addresses (results);
    g_object_unref (resolver);
  }
#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *ip = g_inet_address_to_string (addr);

    GST_DEBUG_OBJECT (sink, "IP address for host %s is %s", host, ip);
    g_free (ip);
  }
#endif

  client = g_slice_new0 (GstUDPClient);
  client->refcount = 1;
  client->host = g_strdup (host);
  client->port = port;
  client->addr = g_inet_socket_address_new (addr, port);
  g_object_unref (addr);

  return client;

name_resolve:
  {
    g_object_unref (resolver);

    return NULL;
  }
}

static void
free_client (GstUDPClient * client)
{
  g_object_unref (client->addr);
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

  if (sink->socket)
    g_object_unref (sink->socket);
  sink->socket = NULL;

  if (sink->socket_v6)
    g_object_unref (sink->socket_v6);
  sink->socket_v6 = NULL;

  if (sink->used_socket)
    g_object_unref (sink->used_socket);
  sink->used_socket = NULL;

  if (sink->used_socket_v6)
    g_object_unref (sink->used_socket_v6);
  sink->used_socket_v6 = NULL;

  if (sink->cancellable)
    g_object_unref (sink->cancellable);
  sink->cancellable = NULL;

  g_free (sink->multi_iface);
  sink->multi_iface = NULL;

  g_free (sink->vec);
  sink->vec = NULL;
  g_free (sink->map);
  sink->map = NULL;

  g_free (sink->bind_address);
  sink->bind_address = NULL;

  g_mutex_clear (&sink->client_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_multiudpsink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstMultiUDPSink *sink;
  GList *clients;
  GOutputVector *vec;
  GstMapInfo *map;
  guint n_mem, i;
  gsize size;
  GstMemory *mem;
  gint num, no_clients;
  GError *err = NULL;

  sink = GST_MULTIUDPSINK_CAST (bsink);

  n_mem = gst_buffer_n_memory (buffer);
  if (n_mem == 0)
    goto no_data;

  /* pre-allocated, the max number of memory blocks is limited so this
   * should not cause overflows */
  vec = sink->vec;
  map = sink->map;

  size = 0;
  for (i = 0; i < n_mem; i++) {
    mem = gst_buffer_peek_memory (buffer, i);
    gst_memory_map (mem, &map[i], GST_MAP_READ);

    vec[i].buffer = map[i].data;
    vec[i].size = map[i].size;

    size += map[i].size;
  }

  sink->bytes_to_serve += size;

  /* grab lock while iterating and sending to clients, this should be
   * fast as UDP never blocks */
  g_mutex_lock (&sink->client_lock);
  GST_LOG_OBJECT (bsink, "about to send %" G_GSIZE_FORMAT " bytes in %u blocks",
      size, n_mem);

  no_clients = 0;
  num = 0;
  for (clients = sink->clients; clients; clients = g_list_next (clients)) {
    GstUDPClient *client;
    GSocket *socket;
    GSocketFamily family;
    gint count;

    client = (GstUDPClient *) clients->data;
    no_clients++;
    GST_LOG_OBJECT (sink, "sending %" G_GSIZE_FORMAT " bytes to client %p",
        size, client);

    family = g_socket_address_get_family (G_SOCKET_ADDRESS (client->addr));
    /* Select socket to send from for this address */
    if (family == G_SOCKET_FAMILY_IPV6 || !sink->used_socket)
      socket = sink->used_socket_v6;
    else
      socket = sink->used_socket;

    count = sink->send_duplicates ? client->refcount : 1;

    while (count--) {
      gssize ret;

      ret =
          g_socket_send_message (socket, client->addr, vec, n_mem,
          NULL, 0, 0, sink->cancellable, &err);

      if (G_UNLIKELY (ret < 0)) {
        if (g_error_matches (err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
          goto flushing;

        /* we continue after posting a warning, next packets might be ok
         * again */
        if (size > UDP_MAX_SIZE) {
          GST_ELEMENT_WARNING (sink, RESOURCE, WRITE,
              ("Attempting to send a UDP packet larger than maximum size "
                  "(%" G_GSIZE_FORMAT " > %d)", size, UDP_MAX_SIZE),
              ("Reason: %s", err ? err->message : "unknown reason"));
        } else {
          GST_ELEMENT_WARNING (sink, RESOURCE, WRITE,
              ("Error sending UDP packet"), ("Reason: %s",
                  err ? err->message : "unknown reason"));
        }
        g_clear_error (&err);
      } else {
        num++;
        client->bytes_sent += ret;
        client->packets_sent++;
        sink->bytes_served += ret;
      }
    }
  }
  g_mutex_unlock (&sink->client_lock);

  /* unmap all memory again */
  for (i = 0; i < n_mem; i++)
    gst_memory_unmap (map[i].memory, &map[i]);

  GST_LOG_OBJECT (sink, "sent %" G_GSIZE_FORMAT " bytes to %d (of %d) clients",
      size, num, no_clients);

  return GST_FLOW_OK;

no_data:
  {
    return GST_FLOW_OK;
  }
flushing:
  {
    GST_DEBUG ("we are flushing");
    g_mutex_unlock (&sink->client_lock);
    g_clear_error (&err);

    /* unmap all memory */
    for (i = 0; i < n_mem; i++)
      gst_memory_unmap (map[i].memory, &map[i]);

    return GST_FLOW_FLUSHING;
  }
}

static void
gst_multiudpsink_set_clients_string (GstMultiUDPSink * sink,
    const gchar * string)
{
  gchar **clients;
  gint i;

  clients = g_strsplit (string, ",", 0);

  g_mutex_lock (&sink->client_lock);
  /* clear all existing clients */
  gst_multiudpsink_clear_internal (sink, FALSE);
  for (i = 0; clients[i]; i++) {
    gchar *host, *p;
    gint64 port = 0;

    host = clients[i];
    p = strstr (clients[i], ":");
    if (p != NULL) {
      *p = '\0';
      port = g_ascii_strtoll (p + 1, NULL, 10);
    }
    if (port != 0)
      gst_multiudpsink_add_internal (sink, host, port, FALSE);
  }
  g_mutex_unlock (&sink->client_lock);

  g_strfreev (clients);
}

static gchar *
gst_multiudpsink_get_clients_string (GstMultiUDPSink * sink)
{
  GString *str;
  GList *clients;

  str = g_string_new ("");

  g_mutex_lock (&sink->client_lock);
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
  g_mutex_unlock (&sink->client_lock);

  return g_string_free (str, FALSE);
}

static void
gst_multiudpsink_setup_qos_dscp (GstMultiUDPSink * sink, GSocket * socket)
{
  /* don't touch on -1 */
  if (sink->qos_dscp < 0)
    return;

  if (socket == NULL)
    return;

#ifdef IP_TOS
  {
    gint tos;
    gint fd;

    fd = g_socket_get_fd (socket);

    GST_DEBUG_OBJECT (sink, "setting TOS to %d", sink->qos_dscp);

    /* Extract and shift 6 bits of DSFIELD */
    tos = (sink->qos_dscp & 0x3f) << 2;

    if (setsockopt (fd, IPPROTO_IP, IP_TOS, &tos, sizeof (tos)) < 0) {
      GST_ERROR_OBJECT (sink, "could not set TOS: %s", g_strerror (errno));
    }
#ifdef IPV6_TCLASS
    if (setsockopt (fd, IPPROTO_IPV6, IPV6_TCLASS, &tos, sizeof (tos)) < 0) {
      GST_ERROR_OBJECT (sink, "could not set TCLASS: %s", g_strerror (errno));
    }
#endif
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
    case PROP_SOCKET:
      if (udpsink->socket != NULL && udpsink->socket != udpsink->used_socket &&
          udpsink->close_socket) {
        GError *err = NULL;

        if (!g_socket_close (udpsink->socket, &err)) {
          GST_ERROR ("failed to close socket %p: %s", udpsink->socket,
              err->message);
          g_clear_error (&err);
        }
      }
      if (udpsink->socket)
        g_object_unref (udpsink->socket);
      udpsink->socket = g_value_dup_object (value);
      GST_DEBUG_OBJECT (udpsink, "setting socket to %p", udpsink->socket);
      break;
    case PROP_SOCKET_V6:
      if (udpsink->socket_v6 != NULL
          && udpsink->socket_v6 != udpsink->used_socket_v6
          && udpsink->close_socket) {
        GError *err = NULL;

        if (!g_socket_close (udpsink->socket_v6, &err)) {
          GST_ERROR ("failed to close socket %p: %s", udpsink->socket_v6,
              err->message);
          g_clear_error (&err);
        }
      }
      if (udpsink->socket_v6)
        g_object_unref (udpsink->socket_v6);
      udpsink->socket_v6 = g_value_dup_object (value);
      GST_DEBUG_OBJECT (udpsink, "setting socket to %p", udpsink->socket_v6);
      break;
    case PROP_CLOSE_SOCKET:
      udpsink->close_socket = g_value_get_boolean (value);
      break;
    case PROP_CLIENTS:
      gst_multiudpsink_set_clients_string (udpsink, g_value_get_string (value));
      break;
    case PROP_AUTO_MULTICAST:
      udpsink->auto_multicast = g_value_get_boolean (value);
      break;
    case PROP_MULTICAST_IFACE:
      g_free (udpsink->multi_iface);

      if (g_value_get_string (value) == NULL)
        udpsink->multi_iface = g_strdup (DEFAULT_MULTICAST_IFACE);
      else
        udpsink->multi_iface = g_value_dup_string (value);
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
    case PROP_FORCE_IPV4:
      udpsink->force_ipv4 = g_value_get_boolean (value);
      break;
    case PROP_QOS_DSCP:
      udpsink->qos_dscp = g_value_get_int (value);
      gst_multiudpsink_setup_qos_dscp (udpsink, udpsink->used_socket);
      gst_multiudpsink_setup_qos_dscp (udpsink, udpsink->used_socket_v6);
      break;
    case PROP_SEND_DUPLICATES:
      udpsink->send_duplicates = g_value_get_boolean (value);
      break;
    case PROP_BUFFER_SIZE:
      udpsink->buffer_size = g_value_get_int (value);
      break;
    case PROP_BIND_ADDRESS:
      g_free (udpsink->bind_address);
      udpsink->bind_address = g_value_dup_string (value);
      break;
    case PROP_BIND_PORT:
      udpsink->bind_port = g_value_get_int (value);
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
    case PROP_SOCKET:
      g_value_set_object (value, udpsink->socket);
      break;
    case PROP_SOCKET_V6:
      g_value_set_object (value, udpsink->socket_v6);
      break;
    case PROP_CLOSE_SOCKET:
      g_value_set_boolean (value, udpsink->close_socket);
      break;
    case PROP_USED_SOCKET:
      g_value_set_object (value, udpsink->used_socket);
      break;
    case PROP_USED_SOCKET_V6:
      g_value_set_object (value, udpsink->used_socket_v6);
      break;
    case PROP_CLIENTS:
      g_value_take_string (value,
          gst_multiudpsink_get_clients_string (udpsink));
      break;
    case PROP_AUTO_MULTICAST:
      g_value_set_boolean (value, udpsink->auto_multicast);
      break;
    case PROP_MULTICAST_IFACE:
      g_value_set_string (value, udpsink->multi_iface);
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
    case PROP_FORCE_IPV4:
      g_value_set_boolean (value, udpsink->force_ipv4);
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
    case PROP_BIND_ADDRESS:
      g_value_set_string (value, udpsink->bind_address);
      break;
    case PROP_BIND_PORT:
      g_value_set_int (value, udpsink->bind_port);
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
  GInetSocketAddress *saddr = G_INET_SOCKET_ADDRESS (client->addr);
  GInetAddress *addr = g_inet_socket_address_get_address (saddr);
  GSocketFamily family = g_socket_address_get_family (G_SOCKET_ADDRESS (saddr));
  GSocket *socket;
  GError *err = NULL;

  GST_DEBUG_OBJECT (sink, "configuring client %p", client);

  if (family == G_SOCKET_FAMILY_IPV6 && !sink->used_socket_v6)
    goto invalid_family;

  /* Select socket to send from for this address */
  if (family == G_SOCKET_FAMILY_IPV6 || !sink->used_socket)
    socket = sink->used_socket_v6;
  else
    socket = sink->used_socket;

  if (g_inet_address_get_is_multicast (addr)) {
    GST_DEBUG_OBJECT (sink, "we have a multicast client %p", client);
    if (sink->auto_multicast) {
      GST_DEBUG_OBJECT (sink, "autojoining group");
      if (!g_socket_join_multicast_group (socket, addr, FALSE,
              sink->multi_iface, &err))
        goto join_group_failed;
    }
    GST_DEBUG_OBJECT (sink, "setting loop to %d", sink->loop);
    g_socket_set_multicast_loopback (socket, sink->loop);
    GST_DEBUG_OBJECT (sink, "setting ttl to %d", sink->ttl_mc);
    g_socket_set_multicast_ttl (socket, sink->ttl_mc);
  } else {
    GST_DEBUG_OBJECT (sink, "setting unicast ttl to %d", sink->ttl);
    g_socket_set_ttl (socket, sink->ttl);
  }
  return TRUE;

  /* ERRORS */
join_group_failed:
  {
    gst_multiudpsink_stop (GST_BASE_SINK (sink));
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Could not join multicast group: %s",
            err ? err->message : "unknown reason"));
    g_clear_error (&err);
    return FALSE;
  }
invalid_family:
  {
    gst_multiudpsink_stop (GST_BASE_SINK (sink));
    GST_ELEMENT_ERROR (sink, RESOURCE, SETTINGS, (NULL),
        ("Invalid address family (got %d)", family));
    return FALSE;
  }
}

/* create a socket for sending to remote machine */
static gboolean
gst_multiudpsink_start (GstBaseSink * bsink)
{
  GstMultiUDPSink *sink;
  GList *clients;
  GstUDPClient *client;
  GError *err = NULL;

  sink = GST_MULTIUDPSINK (bsink);

  sink->external_socket = FALSE;

  if (sink->socket) {
    GST_DEBUG_OBJECT (sink, "using configured socket");
    if (g_socket_get_family (sink->socket) == G_SOCKET_FAMILY_IPV6) {
      sink->used_socket_v6 = G_SOCKET (g_object_ref (sink->socket));
      sink->external_socket = TRUE;
    } else {
      sink->used_socket = G_SOCKET (g_object_ref (sink->socket));
      sink->external_socket = TRUE;
    }
  }

  if (sink->socket_v6) {
    GST_DEBUG_OBJECT (sink, "using configured IPv6 socket");
    g_return_val_if_fail (g_socket_get_family (sink->socket) !=
        G_SOCKET_FAMILY_IPV6, FALSE);

    if (sink->used_socket_v6 && sink->used_socket_v6 != sink->socket_v6) {
      GST_ERROR_OBJECT (sink,
          "Provided different IPv6 sockets in socket and socket-v6 properties");
      return FALSE;
    }

    sink->used_socket_v6 = G_SOCKET (g_object_ref (sink->socket_v6));
    sink->external_socket = TRUE;
  }

  if (!sink->used_socket && !sink->used_socket_v6) {
    GSocketAddress *bind_addr;
    GInetAddress *bind_iaddr;

    if (sink->bind_address) {
      GSocketFamily family;

      bind_iaddr = g_inet_address_new_from_string (sink->bind_address);
      if (!bind_iaddr) {
        GList *results;
        GResolver *resolver;

        resolver = g_resolver_get_default ();
        results =
            g_resolver_lookup_by_name (resolver, sink->bind_address,
            sink->cancellable, &err);
        if (!results) {
          g_object_unref (resolver);
          goto name_resolve;
        }
        bind_iaddr = G_INET_ADDRESS (g_object_ref (results->data));
        g_resolver_free_addresses (results);
        g_object_unref (resolver);
      }

      bind_addr = g_inet_socket_address_new (bind_iaddr, sink->bind_port);
      g_object_unref (bind_iaddr);
      family = g_socket_address_get_family (G_SOCKET_ADDRESS (bind_addr));

      if ((sink->used_socket =
              g_socket_new (family, G_SOCKET_TYPE_DATAGRAM,
                  G_SOCKET_PROTOCOL_UDP, &err)) == NULL) {
        g_object_unref (bind_addr);
        goto no_socket;
      }

      g_socket_bind (sink->used_socket, bind_addr, TRUE, &err);
      if (err != NULL)
        goto bind_error;
    } else {
      /* create sender sockets if none available */
      if ((sink->used_socket = g_socket_new (G_SOCKET_FAMILY_IPV4,
                  G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &err)) == NULL)
        goto no_socket;

      bind_iaddr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);
      bind_addr = g_inet_socket_address_new (bind_iaddr, sink->bind_port);
      g_socket_bind (sink->used_socket, bind_addr, TRUE, &err);
      g_object_unref (bind_addr);
      g_object_unref (bind_iaddr);
      if (err != NULL)
        goto bind_error;

      if ((sink->used_socket_v6 = g_socket_new (G_SOCKET_FAMILY_IPV6,
                  G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP,
                  &err)) == NULL) {
        GST_INFO_OBJECT (sink, "Failed to create IPv6 socket: %s",
            err->message);
        g_clear_error (&err);
      } else {
        bind_iaddr = g_inet_address_new_any (G_SOCKET_FAMILY_IPV6);
        bind_addr = g_inet_socket_address_new (bind_iaddr, sink->bind_port);
        g_socket_bind (sink->used_socket_v6, bind_addr, TRUE, &err);
        g_object_unref (bind_addr);
        g_object_unref (bind_iaddr);
        if (err != NULL)
          goto bind_error;
      }
    }
  }
#ifdef SO_SNDBUF
  {
    socklen_t len;
    gint sndsize, ret;

    len = sizeof (sndsize);
    if (sink->buffer_size != 0) {
      sndsize = sink->buffer_size;

      GST_DEBUG_OBJECT (sink, "setting udp buffer of %d bytes", sndsize);
      /* set buffer size, Note that on Linux this is typically limited to a
       * maximum of around 100K. Also a minimum of 128 bytes is required on
       * Linux. */

      if (sink->used_socket) {
        ret =
            setsockopt (g_socket_get_fd (sink->used_socket), SOL_SOCKET,
            SO_SNDBUF, (void *) &sndsize, len);
        if (ret != 0) {
          GST_ELEMENT_WARNING (sink, RESOURCE, SETTINGS, (NULL),
              ("Could not create a buffer of requested %d bytes, %d: %s",
                  sndsize, ret, g_strerror (errno)));
        }
      }

      if (sink->used_socket_v6) {
        ret =
            setsockopt (g_socket_get_fd (sink->used_socket_v6), SOL_SOCKET,
            SO_SNDBUF, (void *) &sndsize, len);
        if (ret != 0) {
          GST_ELEMENT_WARNING (sink, RESOURCE, SETTINGS, (NULL),
              ("Could not create a buffer of requested %d bytes, %d: %s",
                  sndsize, ret, g_strerror (errno)));
        }
      }
    }

    /* read the value of the receive buffer. Note that on linux this returns 2x the
     * value we set because the kernel allocates extra memory for metadata.
     * The default on Linux is about 100K (which is about 50K without metadata) */
    if (sink->used_socket) {
      ret =
          getsockopt (g_socket_get_fd (sink->used_socket), SOL_SOCKET,
          SO_SNDBUF, (void *) &sndsize, &len);
      if (ret == 0)
        GST_DEBUG_OBJECT (sink, "have UDP buffer of %d bytes", sndsize);
      else
        GST_DEBUG_OBJECT (sink, "could not get UDP buffer size");
    }

    if (sink->used_socket_v6) {
      ret =
          getsockopt (g_socket_get_fd (sink->used_socket_v6), SOL_SOCKET,
          SO_SNDBUF, (void *) &sndsize, &len);
      if (ret == 0)
        GST_DEBUG_OBJECT (sink, "have UDPv6 buffer of %d bytes", sndsize);
      else
        GST_DEBUG_OBJECT (sink, "could not get UDPv6 buffer size");
    }
  }
#endif

#ifdef SO_BINDTODEVICE
  if (sink->multi_iface) {
    if (sink->used_socket) {
      if (setsockopt (g_socket_get_fd (sink->used_socket), SOL_SOCKET,
              SO_BINDTODEVICE, sink->multi_iface,
              strlen (sink->multi_iface)) < 0)
        GST_WARNING_OBJECT (sink, "setsockopt SO_BINDTODEVICE failed: %s",
            strerror (errno));
    }
    if (sink->used_socket_v6) {
      if (setsockopt (g_socket_get_fd (sink->used_socket_v6), SOL_SOCKET,
              SO_BINDTODEVICE, sink->multi_iface,
              strlen (sink->multi_iface)) < 0)
        GST_WARNING_OBJECT (sink, "setsockopt SO_BINDTODEVICE failed (v6): %s",
            strerror (errno));
    }
  }
#endif

  if (sink->used_socket)
    g_socket_set_broadcast (sink->used_socket, TRUE);
  if (sink->used_socket_v6)
    g_socket_set_broadcast (sink->used_socket_v6, TRUE);

  sink->bytes_to_serve = 0;
  sink->bytes_served = 0;

  gst_multiudpsink_setup_qos_dscp (sink, sink->used_socket);
  gst_multiudpsink_setup_qos_dscp (sink, sink->used_socket_v6);

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
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (NULL),
        ("Could not create socket: %s", err->message));
    g_clear_error (&err);
    return FALSE;
  }
bind_error:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (NULL),
        ("Failed to bind socket: %s", err->message));
    g_clear_error (&err);
    return FALSE;
  }
name_resolve:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, (NULL),
        ("Failed to resolve bind address %s: %s", sink->bind_address,
            err->message));
    g_clear_error (&err);
    return FALSE;
  }
}

static gboolean
gst_multiudpsink_stop (GstBaseSink * bsink)
{
  GstMultiUDPSink *udpsink;

  udpsink = GST_MULTIUDPSINK (bsink);

  if (udpsink->used_socket) {
    if (udpsink->close_socket || !udpsink->external_socket) {
      GError *err = NULL;

      if (!g_socket_close (udpsink->used_socket, &err)) {
        GST_ERROR_OBJECT (udpsink, "Failed to close socket: %s", err->message);
        g_clear_error (&err);
      }
    }

    g_object_unref (udpsink->used_socket);
    udpsink->used_socket = NULL;
  }

  if (udpsink->used_socket_v6) {
    if (udpsink->close_socket || !udpsink->external_socket) {
      GError *err = NULL;

      if (!g_socket_close (udpsink->used_socket_v6, &err)) {
        GST_ERROR_OBJECT (udpsink, "Failed to close socket: %s", err->message);
        g_clear_error (&err);
      }
    }

    g_object_unref (udpsink->used_socket_v6);
    udpsink->used_socket_v6 = NULL;
  }

  return TRUE;
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
    g_mutex_lock (&sink->client_lock);

  find = g_list_find_custom (sink->clients, &udpclient,
      (GCompareFunc) client_compare);
  if (find) {
    client = (GstUDPClient *) find->data;

    GST_DEBUG_OBJECT (sink, "found %d existing clients with host %s, port %d",
        client->refcount, host, port);
    client->refcount++;
  } else {
    client = create_client (sink, host, port);
    if (!client)
      goto error;

    g_get_current_time (&now);
    client->connect_time = GST_TIMEVAL_TO_TIME (now);

    if (sink->used_socket)
      gst_multiudpsink_configure_client (sink, client);

    GST_DEBUG_OBJECT (sink, "add client with host %s, port %d", host, port);
    sink->clients = g_list_prepend (sink->clients, client);
  }

  if (lock)
    g_mutex_unlock (&sink->client_lock);

  g_signal_emit (G_OBJECT (sink),
      gst_multiudpsink_signals[SIGNAL_CLIENT_ADDED], 0, host, port);

  GST_DEBUG_OBJECT (sink, "added client on host %s, port %d", host, port);
  return;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (sink, "did not add client on host %s, port %d", host,
        port);
    if (lock)
      g_mutex_unlock (&sink->client_lock);
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

  g_mutex_lock (&sink->client_lock);
  find = g_list_find_custom (sink->clients, &udpclient,
      (GCompareFunc) client_compare);
  if (!find)
    goto not_found;

  client = (GstUDPClient *) find->data;

  GST_DEBUG_OBJECT (sink, "found %d clients with host %s, port %d",
      client->refcount, host, port);

  client->refcount--;
  if (client->refcount == 0) {
    GInetSocketAddress *saddr = G_INET_SOCKET_ADDRESS (client->addr);
    GSocketFamily family = g_socket_address_get_family (client->addr);
    GInetAddress *addr = g_inet_socket_address_get_address (saddr);
    GSocket *socket;

    /* Select socket to send from for this address */
    if (family == G_SOCKET_FAMILY_IPV6 || !sink->used_socket)
      socket = sink->used_socket_v6;
    else
      socket = sink->used_socket;

    GST_DEBUG_OBJECT (sink, "remove client with host %s, port %d", host, port);

    g_get_current_time (&now);
    client->disconnect_time = GST_TIMEVAL_TO_TIME (now);

    if (socket && sink->auto_multicast
        && g_inet_address_get_is_multicast (addr)) {
      GError *err = NULL;

      if (!g_socket_leave_multicast_group (socket, addr, FALSE,
              sink->multi_iface, &err)) {
        GST_DEBUG_OBJECT (sink, "Failed to leave multicast group: %s",
            err->message);
        g_clear_error (&err);
      }
    }

    /* Unlock to emit signal before we delete the actual client */
    g_mutex_unlock (&sink->client_lock);
    g_signal_emit (G_OBJECT (sink),
        gst_multiudpsink_signals[SIGNAL_CLIENT_REMOVED], 0, host, port);
    g_mutex_lock (&sink->client_lock);

    sink->clients = g_list_delete_link (sink->clients, find);

    free_client (client);
  }
  g_mutex_unlock (&sink->client_lock);

  return;

  /* ERRORS */
not_found:
  {
    g_mutex_unlock (&sink->client_lock);
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
    g_mutex_lock (&sink->client_lock);
  g_list_foreach (sink->clients, (GFunc) free_client, sink);
  g_list_free (sink->clients);
  sink->clients = NULL;
  if (lock)
    g_mutex_unlock (&sink->client_lock);
}

void
gst_multiudpsink_clear (GstMultiUDPSink * sink)
{
  gst_multiudpsink_clear_internal (sink, TRUE);
}

GstStructure *
gst_multiudpsink_get_stats (GstMultiUDPSink * sink, const gchar * host,
    gint port)
{
  GstUDPClient *client;
  GstStructure *result = NULL;
  GstUDPClient udpclient;
  GList *find;

  udpclient.host = (gchar *) host;
  udpclient.port = port;

  g_mutex_lock (&sink->client_lock);

  find = g_list_find_custom (sink->clients, &udpclient,
      (GCompareFunc) client_compare);
  if (!find)
    goto not_found;

  GST_DEBUG_OBJECT (sink, "stats for client with host %s, port %d", host, port);

  client = (GstUDPClient *) find->data;

  result = gst_structure_new_empty ("multiudpsink-stats");

  gst_structure_set (result,
      "bytes-sent", G_TYPE_UINT64, client->bytes_sent,
      "packets-sent", G_TYPE_UINT64, client->packets_sent,
      "connect-time", G_TYPE_UINT64, client->connect_time,
      "disconnect-time", G_TYPE_UINT64, client->disconnect_time, NULL);

  g_mutex_unlock (&sink->client_lock);

  return result;

  /* ERRORS */
not_found:
  {
    g_mutex_unlock (&sink->client_lock);
    GST_WARNING_OBJECT (sink, "client with host %s, port %d not found",
        host, port);
    /* Apparently (see comment in gstmultifdsink.c) returning NULL from here may
     * confuse/break python bindings */
    return gst_structure_new_empty ("multiudpsink-stats");
  }
}

static gboolean
gst_multiudpsink_unlock (GstBaseSink * bsink)
{
  GstMultiUDPSink *sink;

  sink = GST_MULTIUDPSINK (bsink);

  g_cancellable_cancel (sink->cancellable);

  return TRUE;
}

static gboolean
gst_multiudpsink_unlock_stop (GstBaseSink * bsink)
{
  GstMultiUDPSink *sink;

  sink = GST_MULTIUDPSINK (bsink);

  g_cancellable_reset (sink->cancellable);

  return TRUE;
}
