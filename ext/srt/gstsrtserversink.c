/* GStreamer SRT plugin based on libsrt
 * Copyright (C) 2017, Collabora Ltd.
 *   Author:Justin Kim <justin.kim@collabora.com>
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
 * SECTION:element-srtserversink
 * @title: srtserversink
 *
 * srtserversink is a network sink that sends <ulink url="http://www.srtalliance.org/">SRT</ulink>
 * packets to the network. Although SRT is an UDP-based protocol, srtserversink works like
 * a server socket of connection-oriented protocol.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v audiotestsrc ! srtserversink
 * ]| This pipeline shows how to serve SRT packets through the default port.
 * </refsect2>
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsrtserversink.h"
#include "gstsrt.h"
#include <srt/srt.h>
#include <gio/gio.h>

#define SRT_DEFAULT_POLL_TIMEOUT -1

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_CAT_DEFAULT gst_debug_srt_server_sink
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

struct _GstSRTServerSinkPrivate
{
  gboolean cancelled;

  SRTSOCKET sock;
  gint poll_id;
  gint poll_timeout;

  GMainLoop *loop;
  GMainContext *context;
  GSource *server_source;
  GThread *thread;

  GList *clients;
};

#define GST_SRT_SERVER_SINK_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_SRT_SERVER_SINK, GstSRTServerSinkPrivate))

enum
{
  PROP_POLL_TIMEOUT = 1,
  PROP_STATS,
  /*< private > */
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum
{
  SIG_CLIENT_ADDED,
  SIG_CLIENT_REMOVED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define gst_srt_server_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSRTServerSink, gst_srt_server_sink,
    GST_TYPE_SRT_BASE_SINK, G_ADD_PRIVATE (GstSRTServerSink)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "srtserversink", 0,
        "SRT Server Sink"));

typedef struct
{
  int sock;
  GSocketAddress *sockaddr;
  gboolean sent_headers;
} SRTClient;

static SRTClient *
srt_client_new (void)
{
  SRTClient *client = g_new0 (SRTClient, 1);
  client->sock = SRT_INVALID_SOCK;
  return client;
}

static void
srt_client_free (SRTClient * client)
{
  g_return_if_fail (client != NULL);

  g_clear_object (&client->sockaddr);

  if (client->sock != SRT_INVALID_SOCK) {
    srt_close (client->sock);
  }

  g_free (client);
}

static void
srt_emit_client_removed (SRTClient * client, gpointer user_data)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (user_data);
  g_return_if_fail (client != NULL && GST_IS_SRT_SERVER_SINK (self));

  g_signal_emit (self, signals[SIG_CLIENT_REMOVED], 0, client->sock,
      client->sockaddr);
}

static void
gst_srt_server_sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (object);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);

  switch (prop_id) {
    case PROP_POLL_TIMEOUT:
      g_value_set_int (value, priv->poll_timeout);
      break;
    case PROP_STATS:
    {
      GList *item;

      GST_OBJECT_LOCK (self);
      for (item = priv->clients; item; item = item->next) {
        SRTClient *client = item->data;
        GValue tmp = G_VALUE_INIT;

        g_value_init (&tmp, GST_TYPE_STRUCTURE);
        g_value_take_boxed (&tmp, gst_srt_base_sink_get_stats (client->sockaddr,
                client->sock));
        gst_value_array_append_and_take_value (value, &tmp);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_server_sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (object);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);

  switch (prop_id) {
    case PROP_POLL_TIMEOUT:
      priv->poll_timeout = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
idle_listen_callback (gpointer data)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (data);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);
  gboolean ret = TRUE;

  SRTClient *client;
  SRTSOCKET ready[2];
  struct sockaddr sa;
  int sa_len;

  if (srt_epoll_wait (priv->poll_id, ready, &(int) {
          2}, 0, 0, priv->poll_timeout, 0, 0, 0, 0) == -1) {
    int srt_errno = srt_getlasterror (NULL);

    if (srt_errno != SRT_ETIMEOUT) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
          ("SRT error: %s", srt_getlasterror_str ()), (NULL));
      ret = FALSE;
      goto out;
    }

    /* Mimicking cancellable */
    if (srt_errno == SRT_ETIMEOUT && priv->cancelled) {
      GST_DEBUG_OBJECT (self, "Cancelled waiting for client");
      ret = FALSE;
      goto out;
    }
  }

  client = srt_client_new ();
  client->sock = srt_accept (priv->sock, &sa, &sa_len);

  if (client->sock == SRT_INVALID_SOCK) {
    GST_WARNING_OBJECT (self, "detected invalid SRT client socket (reason: %s)",
        srt_getlasterror_str ());
    srt_clearlasterror ();
    srt_client_free (client);
    ret = FALSE;
    goto out;
  }

  client->sockaddr = g_socket_address_new_from_native (&sa, sa_len);

  GST_OBJECT_LOCK (self);
  priv->clients = g_list_append (priv->clients, client);
  GST_OBJECT_UNLOCK (self);

  g_signal_emit (self, signals[SIG_CLIENT_ADDED], 0, client->sock,
      client->sockaddr);
  GST_DEBUG_OBJECT (self, "client added");

out:
  return ret;
}

static gpointer
thread_func (gpointer data)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (data);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);

  g_main_loop_run (priv->loop);

  return NULL;
}

static gboolean
gst_srt_server_sink_start (GstBaseSink * sink)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (sink);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);
  GstSRTBaseSink *base = GST_SRT_BASE_SINK (sink);
  GstUri *uri = gst_uri_ref (GST_SRT_BASE_SINK (self)->uri);
  GSocketAddress *socket_address = NULL;
  GError *error = NULL;
  gboolean ret = TRUE;
  struct sockaddr sa;
  size_t sa_len;
  const gchar *host;
  int lat = base->latency;

  if (gst_uri_get_port (uri) == GST_URI_NO_PORT) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, NULL, (("Invalid port")));
    return FALSE;
  }

  host = gst_uri_get_host (uri);
  if (host == NULL) {
    GInetAddress *any = g_inet_address_new_any (G_SOCKET_FAMILY_IPV4);

    socket_address = g_inet_socket_address_new (any, gst_uri_get_port (uri));
    g_object_unref (any);
  } else {
    socket_address =
        g_inet_socket_address_new_from_string (host, gst_uri_get_port (uri));
  }

  if (socket_address == NULL) {
    GST_WARNING_OBJECT (self,
        "failed to extract host or port from the given URI");
    goto failed;
  }

  sa_len = g_socket_address_get_native_size (socket_address);
  if (!g_socket_address_to_native (socket_address, &sa, sa_len, &error)) {
    GST_WARNING_OBJECT (self, "cannot resolve address (reason: %s)",
        error->message);
    goto failed;
  }

  priv->sock = srt_socket (sa.sa_family, SOCK_DGRAM, 0);
  if (priv->sock == SRT_INVALID_SOCK) {
    GST_WARNING_OBJECT (self, "failed to create SRT socket (reason: %s)",
        srt_getlasterror_str ());
    goto failed;
  }

  /* Make SRT non-blocking */
  srt_setsockopt (priv->sock, 0, SRTO_SNDSYN, &(int) {
      0}, sizeof (int));

  /* Make sure TSBPD mode is enable (SRT mode) */
  srt_setsockopt (priv->sock, 0, SRTO_TSBPDMODE, &(int) {
      1}, sizeof (int));

  /* This is a sink, we're always a sender */
  srt_setsockopt (priv->sock, 0, SRTO_SENDER, &(int) {
      1}, sizeof (int));

  srt_setsockopt (priv->sock, 0, SRTO_TSBPDDELAY, &lat, sizeof (int));

  if (base->passphrase != NULL && base->passphrase[0] != '\0') {
    srt_setsockopt (priv->sock, 0, SRTO_PASSPHRASE,
        base->passphrase, strlen (base->passphrase));
    srt_setsockopt (priv->sock, 0, SRTO_PBKEYLEN,
        &base->key_length, sizeof (int));
  }

  priv->poll_id = srt_epoll_create ();
  if (priv->poll_id == -1) {
    GST_WARNING_OBJECT (self,
        "failed to create poll id for SRT socket (reason: %s)",
        srt_getlasterror_str ());
    goto failed;
  }
  srt_epoll_add_usock (priv->poll_id, priv->sock, &(int) {
      SRT_EPOLL_IN});

  if (srt_bind (priv->sock, &sa, sa_len) == SRT_ERROR) {
    GST_WARNING_OBJECT (self, "failed to bind SRT server socket (reason: %s)",
        srt_getlasterror_str ());
    goto failed;
  }

  if (srt_listen (priv->sock, 1) == SRT_ERROR) {
    GST_WARNING_OBJECT (self, "failed to listen SRT socket (reason: %s)",
        srt_getlasterror_str ());
    goto failed;
  }

  priv->context = g_main_context_new ();

  priv->server_source = g_idle_source_new ();
  g_source_set_callback (priv->server_source,
      (GSourceFunc) idle_listen_callback, gst_object_ref (self),
      (GDestroyNotify) gst_object_unref);

  g_source_attach (priv->server_source, priv->context);
  priv->loop = g_main_loop_new (priv->context, TRUE);

  priv->thread = g_thread_try_new ("srtserversink", thread_func, self, &error);
  if (error != NULL) {
    GST_WARNING_OBJECT (self, "failed to create thread (reason: %s)",
        error->message);
    ret = FALSE;
  }

  g_clear_pointer (&uri, gst_uri_unref);
  g_clear_object (&socket_address);

  return ret;

failed:
  if (priv->poll_id != SRT_ERROR) {
    srt_epoll_release (priv->poll_id);
    priv->poll_id = SRT_ERROR;
  }

  if (priv->sock != SRT_INVALID_SOCK) {
    srt_close (priv->sock);
    priv->sock = SRT_INVALID_SOCK;
  }

  g_clear_error (&error);
  g_clear_pointer (&uri, gst_uri_unref);
  g_clear_object (&socket_address);

  return FALSE;
}

static gboolean
send_buffer_internal (GstSRTBaseSink * sink,
    const GstMapInfo * mapinfo, gpointer user_data)
{
  SRTClient *client = user_data;

  if (srt_sendmsg2 (client->sock, (char *) mapinfo->data, mapinfo->size,
          0) == SRT_ERROR) {
    GST_WARNING_OBJECT (sink, "%s", srt_getlasterror_str ());
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_srt_server_sink_send_buffer (GstSRTBaseSink * sink,
    const GstMapInfo * mapinfo)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (sink);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);
  GList *clients = priv->clients;

  GST_OBJECT_LOCK (sink);
  while (clients != NULL) {
    SRTClient *client = clients->data;
    clients = clients->next;

    if (!client->sent_headers) {
      if (!gst_srt_base_sink_send_headers (sink, send_buffer_internal, client))
        goto err;

      client->sent_headers = TRUE;
    }

    if (!send_buffer_internal (sink, mapinfo, client))
      goto err;

    continue;

  err:
    priv->clients = g_list_remove (priv->clients, client);
    GST_OBJECT_UNLOCK (sink);
    g_signal_emit (self, signals[SIG_CLIENT_REMOVED], 0, client->sock,
        client->sockaddr);
    srt_client_free (client);
    GST_OBJECT_LOCK (sink);
  }
  GST_OBJECT_UNLOCK (sink);

  return TRUE;
}

static gboolean
gst_srt_server_sink_stop (GstBaseSink * sink)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (sink);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);
  GList *clients;

  GST_DEBUG_OBJECT (self, "closing client sockets");

  GST_OBJECT_LOCK (sink);
  clients = priv->clients;
  priv->clients = NULL;
  GST_OBJECT_UNLOCK (sink);

  g_list_foreach (clients, (GFunc) srt_emit_client_removed, self);
  g_list_free_full (clients, (GDestroyNotify) srt_client_free);

  GST_DEBUG_OBJECT (self, "closing SRT connection");
  srt_epoll_remove_usock (priv->poll_id, priv->sock);
  srt_epoll_release (priv->poll_id);
  srt_close (priv->sock);

  if (priv->loop) {
    g_main_loop_quit (priv->loop);
    g_thread_join (priv->thread);
    g_clear_pointer (&priv->loop, g_main_loop_unref);
    g_clear_pointer (&priv->thread, g_thread_unref);
  }

  if (priv->server_source) {
    g_source_destroy (priv->server_source);
    g_clear_pointer (&priv->server_source, g_source_unref);
  }

  g_clear_pointer (&priv->context, g_main_context_unref);

  return GST_BASE_SINK_CLASS (parent_class)->stop (sink);
}

static gboolean
gst_srt_server_sink_unlock (GstBaseSink * sink)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (sink);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);

  priv->cancelled = TRUE;

  return TRUE;
}

static gboolean
gst_srt_server_sink_unlock_stop (GstBaseSink * sink)
{
  GstSRTServerSink *self = GST_SRT_SERVER_SINK (sink);
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);

  priv->cancelled = FALSE;

  return TRUE;
}

static void
gst_srt_server_sink_class_init (GstSRTServerSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);
  GstSRTBaseSinkClass *gstsrtbasesink_class = GST_SRT_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_srt_server_sink_set_property;
  gobject_class->get_property = gst_srt_server_sink_get_property;

  properties[PROP_POLL_TIMEOUT] =
      g_param_spec_int ("poll-timeout", "Poll Timeout",
      "Return poll wait after timeout miliseconds (-1 = infinite)", -1,
      G_MAXINT32, SRT_DEFAULT_POLL_TIMEOUT,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  properties[PROP_STATS] = gst_param_spec_array ("stats", "Statistics",
      "Array of GstStructures containing SRT statistics",
      g_param_spec_boxed ("stats", "Statistics",
          "Statistics for one client", GST_TYPE_STRUCTURE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS),
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  /**
   * GstSRTServerSink::client-added:
   * @gstsrtserversink: the srtserversink element that emitted this signal
   * @sock: the client socket descriptor that was added to srtserversink
   * @addr: the pointer of "struct sockaddr" that describes the @sock
   * @addr_len: the length of @addr
   * 
   * The given socket descriptor was added to srtserversink.
   */
  signals[SIG_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTServerSinkClass, client_added),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  /**
   * GstSRTServerSink::client-removed:
   * @gstsrtserversink: the srtserversink element that emitted this signal
   * @sock: the client socket descriptor that was added to srtserversink
   * @addr: the pointer of "struct sockaddr" that describes the @sock
   * @addr_len: the length of @addr
   *
   * The given socket descriptor was removed from srtserversink.
   */
  signals[SIG_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTServerSinkClass,
          client_removed), NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);
  gst_element_class_set_metadata (gstelement_class,
      "SRT server sink", "Sink/Network",
      "Send data over the network via SRT",
      "Justin Kim <justin.kim@collabora.com>");

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_srt_server_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_srt_server_sink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_srt_server_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_srt_server_sink_unlock_stop);

  gstsrtbasesink_class->send_buffer =
      GST_DEBUG_FUNCPTR (gst_srt_server_sink_send_buffer);
}

static void
gst_srt_server_sink_init (GstSRTServerSink * self)
{
  GstSRTServerSinkPrivate *priv = GST_SRT_SERVER_SINK_GET_PRIVATE (self);
  priv->poll_timeout = SRT_DEFAULT_POLL_TIMEOUT;
}
