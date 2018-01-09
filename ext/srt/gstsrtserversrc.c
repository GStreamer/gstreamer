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
 * SECTION:element-srtserversrc
 * @title: srtserversrc
 *
 * srtserversrc is a network source that reads <ulink url="http://www.srtalliance.org/">SRT</ulink>
 * packets from the network. Although SRT is a protocol based on UDP, srtserversrc works like
 * a server socket of connection-oriented protocol, but it accepts to only one client connection.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v srtserversrc uri="srt://:7001" ! fakesink
 * ]| This pipeline shows how to bind SRT server by setting #GstSRTServerSrc:uri property. 
 * </refsect2>
 * 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsrtserversrc.h"
#include "gstsrt.h"
#include <gio/gio.h>

#define SRT_DEFAULT_POLL_TIMEOUT 100

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_CAT_DEFAULT gst_debug_srt_server_src
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

struct _GstSRTServerSrcPrivate
{
  SRTSOCKET sock;
  SRTSOCKET client_sock;
  GSocketAddress *client_sockaddr;

  gint poll_id;
  gint poll_timeout;

  gboolean has_client;
  gboolean cancelled;
};

#define GST_SRT_SERVER_SRC_GET_PRIVATE(obj)  \
       (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_SRT_SERVER_SRC, GstSRTServerSrcPrivate))

enum
{
  PROP_POLL_TIMEOUT = 1,

  /*< private > */
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum
{
  SIG_CLIENT_ADDED,
  SIG_CLIENT_CLOSED,

  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

#define gst_srt_server_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstSRTServerSrc, gst_srt_server_src,
    GST_TYPE_SRT_BASE_SRC, G_ADD_PRIVATE (GstSRTServerSrc)
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "srtserversrc", 0,
        "SRT Server Source"));

static void
gst_srt_server_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (object);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);

  switch (prop_id) {
    case PROP_POLL_TIMEOUT:
      g_value_set_int (value, priv->poll_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_server_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (object);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);

  switch (prop_id) {
    case PROP_POLL_TIMEOUT:
      priv->poll_timeout = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_srt_server_src_finalize (GObject * object)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (object);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);

  if (priv->poll_id != SRT_ERROR) {
    srt_epoll_release (priv->poll_id);
    priv->poll_id = SRT_ERROR;
  }

  if (priv->sock != SRT_ERROR) {
    srt_close (priv->sock);
    priv->sock = SRT_ERROR;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstFlowReturn
gst_srt_server_src_fill (GstPushSrc * src, GstBuffer * outbuf)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (src);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);
  GstFlowReturn ret = GST_FLOW_OK;
  GstMapInfo info;
  SRTSOCKET ready[2];
  gint recv_len;
  struct sockaddr client_sa;
  size_t client_sa_len;

  while (!priv->has_client) {
    GST_DEBUG_OBJECT (self, "poll wait (timeout: %d)", priv->poll_timeout);

    /* Make SRT server socket non-blocking */
    srt_setsockopt (priv->sock, 0, SRTO_SNDSYN, &(int) {
        0}, sizeof (int));

    if (srt_epoll_wait (priv->poll_id, ready, &(int) {
            2}, 0, 0, priv->poll_timeout, 0, 0, 0, 0) == -1) {
      int srt_errno = srt_getlasterror (NULL);

      /* Assuming that timeout error is normal */
      if (srt_errno != SRT_ETIMEOUT) {
        GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
            ("SRT error: %s", srt_getlasterror_str ()), (NULL));

        return GST_FLOW_ERROR;
      }

      /* Mimicking cancellable */
      if (srt_errno == SRT_ETIMEOUT && priv->cancelled) {
        GST_DEBUG_OBJECT (self, "Cancelled waiting for client");
        return GST_FLOW_FLUSHING;
      }

      continue;
    }

    priv->client_sock =
        srt_accept (priv->sock, &client_sa, (int *) &client_sa_len);

    GST_DEBUG_OBJECT (self, "checking client sock");
    if (priv->client_sock == SRT_INVALID_SOCK) {
      GST_WARNING_OBJECT (self,
          "detected invalid SRT client socket (reason: %s)",
          srt_getlasterror_str ());
      srt_clearlasterror ();
    } else {
      priv->has_client = TRUE;
      g_clear_object (&priv->client_sockaddr);
      priv->client_sockaddr = g_socket_address_new_from_native (&client_sa,
          client_sa_len);
      g_signal_emit (self, signals[SIG_CLIENT_ADDED], 0,
          priv->client_sock, priv->client_sockaddr);
    }
  }

  GST_DEBUG_OBJECT (self, "filling buffer");

  if (!gst_buffer_map (outbuf, &info, GST_MAP_WRITE)) {
    GST_ELEMENT_ERROR (src, RESOURCE, WRITE,
        ("Could not map the output stream"), (NULL));
    ret = GST_FLOW_ERROR;
    goto out;
  }

  recv_len = srt_recvmsg (priv->client_sock, (char *) info.data,
      gst_buffer_get_size (outbuf));

  gst_buffer_unmap (outbuf, &info);

  if (recv_len == SRT_ERROR) {
    GST_WARNING_OBJECT (self, "%s", srt_getlasterror_str ());

    g_signal_emit (self, signals[SIG_CLIENT_CLOSED], 0,
        priv->client_sock, priv->client_sockaddr);

    srt_close (priv->client_sock);
    priv->client_sock = SRT_INVALID_SOCK;
    g_clear_object (&priv->client_sockaddr);
    priv->has_client = FALSE;
    gst_buffer_resize (outbuf, 0, 0);
    ret = GST_FLOW_OK;
    goto out;
  } else if (recv_len == 0) {
    ret = GST_FLOW_EOS;
    goto out;
  }

  GST_BUFFER_PTS (outbuf) =
      gst_clock_get_time (GST_ELEMENT_CLOCK (src)) -
      GST_ELEMENT_CAST (src)->base_time;

  gst_buffer_resize (outbuf, 0, recv_len);

  GST_LOG_OBJECT (src,
      "filled buffer from _get of size %" G_GSIZE_FORMAT ", ts %"
      GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT
      ", offset %" G_GINT64_FORMAT ", offset_end %" G_GINT64_FORMAT,
      gst_buffer_get_size (outbuf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)),
      GST_BUFFER_OFFSET (outbuf), GST_BUFFER_OFFSET_END (outbuf));

out:
  return ret;
}

static gboolean
gst_srt_server_src_start (GstBaseSrc * src)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (src);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);
  GstSRTBaseSrc *base = GST_SRT_BASE_SRC (src);
  GstUri *uri = gst_uri_ref (base->uri);
  GError *error = NULL;
  struct sockaddr sa;
  size_t sa_len;
  GSocketAddress *socket_address;
  const gchar *host;
  int lat = base->latency;

  if (gst_uri_get_port (uri) == GST_URI_NO_PORT) {
    GST_ELEMENT_ERROR (src, RESOURCE, OPEN_WRITE, NULL, (("Invalid port")));
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
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, ("Invalid URI"),
        ("failed to extract host or port from the given URI"));
    goto failed;
  }

  sa_len = g_socket_address_get_native_size (socket_address);
  if (!g_socket_address_to_native (socket_address, &sa, sa_len, &error)) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, ("Invalid URI"),
        ("cannot resolve address (reason: %s)", error->message));
    goto failed;
  }

  priv->sock = srt_socket (sa.sa_family, SOCK_DGRAM, 0);
  if (priv->sock == SRT_ERROR) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT, (NULL),
        ("failed to create poll id for SRT socket (reason: %s)",
            srt_getlasterror_str ()));
    goto failed;
  }

  /* Make sure TSBPD mode is enable (SRT mode) */
  srt_setsockopt (priv->sock, 0, SRTO_TSBPDMODE, &(int) {
      1}, sizeof (int));

  /* This is a sink, we're always a receiver */
  srt_setsockopt (priv->sock, 0, SRTO_SENDER, &(int) {
      0}, sizeof (int));

  srt_setsockopt (priv->sock, 0, SRTO_TSBPDDELAY, &lat, sizeof (int));

  if (base->passphrase != NULL && base->passphrase[0] != '\0') {
    srt_setsockopt (priv->sock, 0, SRTO_PASSPHRASE,
        base->passphrase, strlen (base->passphrase));
    srt_setsockopt (priv->sock, 0, SRTO_PBKEYLEN,
        &base->key_length, sizeof (int));
  }

  priv->poll_id = srt_epoll_create ();
  if (priv->poll_id == -1) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT, (NULL),
        ("failed to create poll id for SRT socket (reason: %s)",
            srt_getlasterror_str ()));
    goto failed;
  }

  srt_epoll_add_usock (priv->poll_id, priv->sock, &(int) {
      SRT_EPOLL_IN});

  if (srt_bind (priv->sock, &sa, sa_len) == SRT_ERROR) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, (NULL),
        ("failed to bind SRT server socket (reason: %s)",
            srt_getlasterror_str ()));
    goto failed;
  }

  if (srt_listen (priv->sock, 1) == SRT_ERROR) {
    GST_ELEMENT_ERROR (self, RESOURCE, OPEN_READ, (NULL),
        ("failed to listen SRT socket (reason: %s)", srt_getlasterror_str ()));
    goto failed;
  }

  g_clear_pointer (&uri, gst_uri_unref);
  g_clear_object (&socket_address);

  return TRUE;

failed:
  if (priv->poll_id != SRT_ERROR) {
    srt_epoll_release (priv->poll_id);
    priv->poll_id = SRT_ERROR;
  }

  if (priv->sock != SRT_ERROR) {
    srt_close (priv->sock);
    priv->sock = SRT_ERROR;
  }

  g_clear_error (&error);
  g_clear_pointer (&uri, gst_uri_unref);
  g_clear_object (&socket_address);

  return FALSE;
}

static gboolean
gst_srt_server_src_stop (GstBaseSrc * src)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (src);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);

  if (priv->client_sock != SRT_INVALID_SOCK) {
    g_signal_emit (self, signals[SIG_CLIENT_ADDED], 0,
        priv->client_sock, priv->client_sockaddr);
    srt_close (priv->client_sock);
    g_clear_object (&priv->client_sockaddr);
    priv->client_sock = SRT_INVALID_SOCK;
    priv->has_client = FALSE;
  }

  if (priv->poll_id != SRT_ERROR) {
    srt_epoll_remove_usock (priv->poll_id, priv->sock);
    srt_epoll_release (priv->poll_id);
    priv->poll_id = SRT_ERROR;
  }

  if (priv->sock != SRT_INVALID_SOCK) {
    GST_DEBUG_OBJECT (self, "closing SRT connection");
    srt_close (priv->sock);
    priv->sock = SRT_INVALID_SOCK;
  }

  priv->cancelled = FALSE;

  return TRUE;
}

static gboolean
gst_srt_server_src_unlock (GstBaseSrc * src)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (src);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);

  priv->cancelled = TRUE;

  return TRUE;
}

static gboolean
gst_srt_server_src_unlock_stop (GstBaseSrc * src)
{
  GstSRTServerSrc *self = GST_SRT_SERVER_SRC (src);
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);

  priv->cancelled = FALSE;

  return TRUE;
}

static void
gst_srt_server_src_class_init (GstSRTServerSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->set_property = gst_srt_server_src_set_property;
  gobject_class->get_property = gst_srt_server_src_get_property;
  gobject_class->finalize = gst_srt_server_src_finalize;

  /**
   * GstSRTServerSrc:poll-timeout:
   * 
   * The timeout(ms) value when polling SRT socket. For #GstSRTServerSrc,
   * this value shouldn't be set as -1 (infinite) because "srt_epoll_wait"
   * isn't cancellable unless closing the socket.
   */
  properties[PROP_POLL_TIMEOUT] =
      g_param_spec_int ("poll-timeout", "Poll timeout",
      "Return poll wait after timeout miliseconds", 0, G_MAXINT32,
      SRT_DEFAULT_POLL_TIMEOUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);

  /**
   * GstSRTServerSrc::client-added:
   * @gstsrtserversrc: the srtserversrc element that emitted this signal
   * @sock: the client socket descriptor that was added to srtserversrc
   * @addr: the pointer of "struct sockaddr" that describes the @sock
   * @addr_len: the length of @addr
   * 
   * The given socket descriptor was added to srtserversrc.
   */
  signals[SIG_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTServerSrcClass, client_added),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  /**
   * GstSRTServerSrc::client-closed:
   * @gstsrtserversrc: the srtserversrc element that emitted this signal
   * @sock: the client socket descriptor that was added to srtserversrc
   * @addr: the pointer of "struct sockaddr" that describes the @sock
   * @addr_len: the length of @addr
   *
   * The given socket descriptor was closed.
   */
  signals[SIG_CLIENT_CLOSED] =
      g_signal_new ("client-closed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstSRTServerSrcClass, client_closed),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      2, G_TYPE_INT, G_TYPE_SOCKET_ADDRESS);

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);
  gst_element_class_set_metadata (gstelement_class,
      "SRT Server source", "Source/Network",
      "Receive data over the network via SRT",
      "Justin Kim <justin.kim@collabora.com>");

  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_srt_server_src_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_srt_server_src_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_srt_server_src_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_srt_server_src_unlock_stop);

  gstpushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_srt_server_src_fill);
}

static void
gst_srt_server_src_init (GstSRTServerSrc * self)
{
  GstSRTServerSrcPrivate *priv = GST_SRT_SERVER_SRC_GET_PRIVATE (self);

  priv->sock = SRT_INVALID_SOCK;
  priv->client_sock = SRT_INVALID_SOCK;
  priv->poll_id = SRT_ERROR;
  priv->poll_timeout = SRT_DEFAULT_POLL_TIMEOUT;
}
