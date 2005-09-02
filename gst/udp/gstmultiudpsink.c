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
#include "gstudp-marshal.h"
#include "gstmultiudpsink.h"

GST_DEBUG_CATEGORY (multiudpsink_debug);
#define GST_CAT_DEFAULT (multiudpsink_debug)

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/* elementfactory information */
static GstElementDetails gst_multiudpsink_details =
GST_ELEMENT_DETAILS ("UDP packet sender",
    "Sink/Network",
    "Send data over the network via UDP",
    "Wim Taymans <wim@fluendo.com>");

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

enum
{
  PROP_0,
  /* FILL ME */
};

static void gst_multiudpsink_base_init (gpointer g_class);
static void gst_multiudpsink_class_init (GstMultiUDPSink * klass);
static void gst_multiudpsink_init (GstMultiUDPSink * udpsink);
static void gst_multiudpsink_finalize (GObject * object);

static GstFlowReturn gst_multiudpsink_render (GstBaseSink * sink,
    GstBuffer * buffer);
static GstStateChangeReturn gst_multiudpsink_change_state (GstElement *
    element, GstStateChange transition);

static void gst_multiudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multiudpsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

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

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details (element_class, &gst_multiudpsink_details);
}

static void
gst_multiudpsink_class_init (GstMultiUDPSink * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_BASE_SINK);

  gobject_class->set_property = gst_multiudpsink_set_property;
  gobject_class->get_property = gst_multiudpsink_get_property;
  gobject_class->finalize = gst_multiudpsink_finalize;

  gst_multiudpsink_signals[SIGNAL_ADD] =
      g_signal_new ("add", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, add),
      NULL, NULL, gst_udp_marshal_VOID__STRING_INT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_INT);
  gst_multiudpsink_signals[SIGNAL_REMOVE] =
      g_signal_new ("remove", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, remove),
      NULL, NULL, gst_udp_marshal_VOID__STRING_INT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_INT);
  gst_multiudpsink_signals[SIGNAL_CLEAR] =
      g_signal_new ("clear", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, clear),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  gst_multiudpsink_signals[SIGNAL_GET_STATS] =
      g_signal_new ("get-stats", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiUDPSinkClass, get_stats),
      NULL, NULL, gst_udp_marshal_BOXED__STRING_INT, G_TYPE_VALUE_ARRAY, 2,
      G_TYPE_STRING, G_TYPE_INT);

  gst_multiudpsink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiUDPSinkClass, client_added),
      NULL, NULL, gst_udp_marshal_VOID__STRING_INT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_INT);
  gst_multiudpsink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMultiUDPSinkClass,
          client_removed), NULL, NULL, gst_udp_marshal_VOID__STRING_INT,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);

  gstelement_class->change_state = gst_multiudpsink_change_state;

  gstbasesink_class->render = gst_multiudpsink_render;

  GST_DEBUG_CATEGORY_INIT (multiudpsink_debug, "multiudpsink", 0, "UDP sink");
}


static void
gst_multiudpsink_init (GstMultiUDPSink * sink)
{
  sink->client_lock = g_mutex_new ();
}

static void
gst_multiudpsink_finalize (GObject * object)
{
  GstMultiUDPSink *sink;

  sink = GST_MULTIUDPSINK (object);

  g_mutex_free (sink->client_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_multiudpsink_render (GstBaseSink * bsink, GstBuffer * buffer)
{
  GstMultiUDPSink *sink;
  gint ret, size;
  guint8 *data;
  GList *clients;

  sink = GST_MULTIUDPSINK (bsink);

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  GST_DEBUG ("about to send %d bytes", size);

  g_mutex_lock (sink->client_lock);
  for (clients = sink->clients; clients; clients = g_list_next (clients)) {
    GstUDPClient *client;

    client = (GstUDPClient *) clients->data;
    GST_DEBUG ("sending %d bytes to client %p", size, client);

    while (TRUE) {
      ret = sendto (*client->sock, data, size, 0,
          (struct sockaddr *) &client->theiraddr, sizeof (client->theiraddr));

      if (ret < 0) {
        if (errno != EINTR && errno != EAGAIN) {
          goto send_error;
        }
      } else
        break;
    }
  }
  g_mutex_unlock (sink->client_lock);

  GST_DEBUG ("sent %d bytes", size);

  return GST_FLOW_OK;

send_error:
  {
    g_mutex_unlock (sink->client_lock);
    GST_DEBUG ("got send error %s (%d)", g_strerror (errno), errno);
    return GST_FLOW_ERROR;
  }
}

static void
gst_multiudpsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiUDPSink *udpsink;

  udpsink = GST_MULTIUDPSINK (object);

  switch (prop_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_multiudpsink_init_send (GstMultiUDPSink * sink)
{
  guint bc_val;
  gint ret;

  /* create sender socket */
  if ((sink->sock = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
    goto no_socket;

  bc_val = 1;
  if ((ret =
          setsockopt (sink->sock, SOL_SOCKET, SO_BROADCAST, &bc_val,
              sizeof (bc_val))) < 0)
    goto no_broadcast;

  return TRUE;

  /* ERRORS */
no_socket:
  {
    perror ("socket");
    return FALSE;
  }
no_broadcast:
  {
    perror ("setsockopt");
    return FALSE;
  }
}

static void
gst_multiudpsink_close (GstMultiUDPSink * sink)
{
  close (sink->sock);
}

void
gst_multiudpsink_add (GstMultiUDPSink * sink, const gchar * host, gint port)
{
  struct hostent *he;
  struct in_addr addr;
  struct ip_mreq multi_addr;
  GstUDPClient *client;

  client = g_new0 (GstUDPClient, 1);
  client->host = g_strdup (host);
  client->port = port;
  client->sock = &sink->sock;

  memset (&client->theiraddr, 0, sizeof (client->theiraddr));
  client->theiraddr.sin_family = AF_INET;       /* host byte order */
  client->theiraddr.sin_port = htons (port);    /* short, network byte order */

  /* if its an IP address */
  if (inet_aton (host, &addr)) {
    /* check if its a multicast address */
    if ((ntohl (addr.s_addr) & 0xe0000000) == 0xe0000000) {
      client->multi_addr.imr_multiaddr.s_addr = addr.s_addr;
      client->multi_addr.imr_interface.s_addr = INADDR_ANY;

      client->theiraddr.sin_addr = multi_addr.imr_multiaddr;

      /* Joining the multicast group */
      /* FIXME, can we use multicast and unicast over the same
       * socket? if not, search for socket of this multicast group or
       * create a new one. */
      setsockopt (sink->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &multi_addr,
          sizeof (multi_addr));
    } else {
      client->theiraddr.sin_addr = *((struct in_addr *) &addr);
    }
  }
  /* we dont need to lookup for localhost */
  else if (strcmp (host, "localhost") == 0 && inet_aton ("127.0.0.1", &addr)) {
    client->theiraddr.sin_addr = *((struct in_addr *) &addr);
  }
  /* if its a hostname */
  else if ((he = gethostbyname (host))) {
    client->theiraddr.sin_addr = *((struct in_addr *) he->h_addr);
  } else {
    goto host_error;
  }

  g_mutex_lock (sink->client_lock);
  sink->clients = g_list_prepend (sink->clients, client);
  g_mutex_unlock (sink->client_lock);

  return;

  /* ERRORS */
host_error:
  {
    GST_DEBUG ("hostname lookup error?");
    g_free (client->host);
    g_free (client);
    return;
  }
}

static gint
client_compare (GstUDPClient * a, GstUDPClient * b)
{
  if ((a->port == b->port) && (strcmp (a->host, b->host) == 0))
    return 0;

  return 1;
}

static void
free_client (GstUDPClient * client)
{
  g_free (client->host);
  g_free (client);
}

void
gst_multiudpsink_remove (GstMultiUDPSink * sink, const gchar * host, gint port)
{
  GList *find;
  GstUDPClient udpclient;

  udpclient.host = (gchar *) host;
  udpclient.port = port;

  g_mutex_lock (sink->client_lock);
  find = g_list_find_custom (sink->clients, &udpclient,
      (GCompareFunc) client_compare);
  if (find) {
    GstUDPClient *client;

    client = (GstUDPClient *) find->data;

    sink->clients = g_list_delete_link (sink->clients, find);

    free_client (client);
  }
  g_mutex_unlock (sink->client_lock);
}

void
gst_multiudpsink_clear (GstMultiUDPSink * sink)
{
  g_mutex_lock (sink->client_lock);
  g_list_foreach (sink->clients, (GFunc) free_client, sink);
  g_list_free (sink->clients);
  sink->clients = NULL;
  g_mutex_unlock (sink->client_lock);
}

GValueArray *
gst_multiudpsink_get_stats (GstMultiUDPSink * sink, const gchar * host,
    gint port)
{
  return NULL;
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
    return GST_STATE_CHANGE_FAILURE;
  }
}
