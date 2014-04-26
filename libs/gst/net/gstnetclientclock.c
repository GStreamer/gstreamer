/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2005 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2012 Collabora Ltd. <tim.muller@collabora.co.uk>
 *
 * gstnetclientclock.h: clock that synchronizes itself to a time provider over
 * the network
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
 * SECTION:gstnetclientclock
 * @short_description: Special clock that synchronizes to a remote time
 *                     provider.
 * @see_also: #GstClock, #GstNetTimeProvider, #GstPipeline
 *
 * This object implements a custom #GstClock that synchronizes its time
 * to a remote time provider such as #GstNetTimeProvider.
 *
 * A new clock is created with gst_net_client_clock_new() which takes the
 * address and port of the remote time provider along with a name and
 * an initial time.
 *
 * This clock will poll the time provider and will update its calibration
 * parameters based on the local and remote observations.
 *
 * The "round-trip" property limits the maximum round trip packets can take.
 *
 * Various parameters of the clock can be configured with the parent #GstClock
 * "timeout", "window-size" and "window-threshold" object properties.
 *
 * A #GstNetClientClock is typically set on a #GstPipeline with 
 * gst_pipeline_use_clock().
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnettimepacket.h"
#include "gstnetclientclock.h"

#include <gio/gio.h>

GST_DEBUG_CATEGORY_STATIC (ncc_debug);
#define GST_CAT_DEFAULT (ncc_debug)

#define DEFAULT_ADDRESS         "127.0.0.1"
#define DEFAULT_PORT            5637
#define DEFAULT_TIMEOUT         GST_SECOND
#define DEFAULT_ROUNDTRIP_LIMIT GST_SECOND

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_ROUNDTRIP_LIMIT
};

#define GST_NET_CLIENT_CLOCK_GET_PRIVATE(obj)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_NET_CLIENT_CLOCK, GstNetClientClockPrivate))

struct _GstNetClientClockPrivate
{
  GThread *thread;

  GSocket *socket;
  GSocketAddress *servaddr;
  GCancellable *cancel;

  GstClockTime timeout_expiration;
  GstClockTime roundtrip_limit;
  GstClockTime rtt_avg;

  gchar *address;
  gint port;
};

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (ncc_debug, "netclock", 0, "Network client clock");
#define gst_net_client_clock_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstNetClientClock, gst_net_client_clock,
    GST_TYPE_SYSTEM_CLOCK, _do_init);

static void gst_net_client_clock_finalize (GObject * object);
static void gst_net_client_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_net_client_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_net_client_clock_stop (GstNetClientClock * self);

static void
gst_net_client_clock_class_init (GstNetClientClockClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstNetClientClockPrivate));

  gobject_class->finalize = gst_net_client_clock_finalize;
  gobject_class->get_property = gst_net_client_clock_get_property;
  gobject_class->set_property = gst_net_client_clock_set_property;

  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "address",
          "The IP address of the machine providing a time server",
          DEFAULT_ADDRESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port on which the remote server is listening", 0, G_MAXUINT16,
          DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstNetClientClock::round-trip-limit:
   *
   * Maximum allowed round-trip for packets. If this property is set to a nonzero
   * value, all packets with a round-trip interval larger than this limit will be
   * ignored. This is useful for networks with severe and fluctuating transport
   * delays. Filtering out these packets increases stability of the synchronization.
   * On the other hand, the lower the limit, the higher the amount of filtered
   * packets. Empirical tests are typically necessary to estimate a good value
   * for the limit.
   * If the property is set to zero, the limit is disabled.
   *
   * Since: 1.4
   */
  g_object_class_install_property (gobject_class, PROP_ROUNDTRIP_LIMIT,
      g_param_spec_uint64 ("round-trip-limit", "round-trip limit",
          "Maximum tolerable round-trip interval for packets, in nanoseconds "
          "(0 = no limit)", 0, G_MAXUINT64, DEFAULT_ROUNDTRIP_LIMIT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_net_client_clock_init (GstNetClientClock * self)
{
  GstClock *clock = GST_CLOCK_CAST (self);
  GstNetClientClockPrivate *priv;

  self->priv = priv = GST_NET_CLIENT_CLOCK_GET_PRIVATE (self);

  priv->port = DEFAULT_PORT;
  priv->address = g_strdup (DEFAULT_ADDRESS);

  gst_clock_set_timeout (clock, DEFAULT_TIMEOUT);

  priv->thread = NULL;

  priv->servaddr = NULL;
  priv->rtt_avg = GST_CLOCK_TIME_NONE;
  priv->roundtrip_limit = DEFAULT_ROUNDTRIP_LIMIT;
}

static void
gst_net_client_clock_finalize (GObject * object)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);

  if (self->priv->thread) {
    gst_net_client_clock_stop (self);
  }

  g_free (self->priv->address);
  self->priv->address = NULL;

  if (self->priv->servaddr != NULL) {
    g_object_unref (self->priv->servaddr);
    self->priv->servaddr = NULL;
  }

  if (self->priv->socket != NULL) {
    g_socket_close (self->priv->socket, NULL);
    g_object_unref (self->priv->socket);
    self->priv->socket = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_net_client_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);

  switch (prop_id) {
    case PROP_ADDRESS:
      g_free (self->priv->address);
      self->priv->address = g_value_dup_string (value);
      if (self->priv->address == NULL)
        self->priv->address = g_strdup (DEFAULT_ADDRESS);
      break;
    case PROP_PORT:
      self->priv->port = g_value_get_int (value);
      break;
    case PROP_ROUNDTRIP_LIMIT:
      self->priv->roundtrip_limit = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_net_client_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);

  switch (prop_id) {
    case PROP_ADDRESS:
      g_value_set_string (value, self->priv->address);
      break;
    case PROP_PORT:
      g_value_set_int (value, self->priv->port);
      break;
    case PROP_ROUNDTRIP_LIMIT:
      g_value_set_uint64 (value, self->priv->roundtrip_limit);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_net_client_clock_observe_times (GstNetClientClock * self,
    GstClockTime local_1, GstClockTime remote, GstClockTime local_2)
{
  GstNetClientClockPrivate *priv = self->priv;
  GstClockTime current_timeout;
  GstClockTime local_avg;
  gdouble r_squared;
  GstClock *clock;
  GstClockTime rtt;

  if (local_2 < local_1) {
    GST_LOG_OBJECT (self, "Dropping observation: receive time %" GST_TIME_FORMAT
        " < send time %" GST_TIME_FORMAT, GST_TIME_ARGS (local_1),
        GST_TIME_ARGS (local_2));
    goto bogus_observation;
  }

  rtt = GST_CLOCK_DIFF (local_1, local_2);

  if ((self->priv->roundtrip_limit > 0) && (rtt > self->priv->roundtrip_limit)) {
    GST_LOG_OBJECT (self,
        "Dropping observation: RTT %" GST_TIME_FORMAT " > limit %"
        GST_TIME_FORMAT, GST_TIME_ARGS (rtt),
        GST_TIME_ARGS (self->priv->roundtrip_limit));
    goto bogus_observation;
  }

  /* Track an average round trip time, for a bit of smoothing */
  /* Always update before discarding a sample, so genuine changes in
   * the network get picked up, eventually */
  if (priv->rtt_avg == GST_CLOCK_TIME_NONE)
    priv->rtt_avg = rtt;
  else if (rtt < priv->rtt_avg) /* Shorter RTTs carry more weight than longer */
    priv->rtt_avg = (3 * priv->rtt_avg + rtt) / 4;
  else
    priv->rtt_avg = (7 * priv->rtt_avg + rtt) / 8;

  if (rtt > 2 * priv->rtt_avg) {
    GST_LOG_OBJECT (self,
        "Dropping observation, long RTT %" GST_TIME_FORMAT " > 2 * avg %"
        GST_TIME_FORMAT, GST_TIME_ARGS (rtt), GST_TIME_ARGS (priv->rtt_avg));
    goto bogus_observation;
  }

  local_avg = (local_2 + local_1) / 2;

  GST_LOG_OBJECT (self, "local1 %" G_GUINT64_FORMAT " remote %" G_GUINT64_FORMAT
      " localavg %" G_GUINT64_FORMAT " local2 %" G_GUINT64_FORMAT,
      local_1, remote, local_avg, local_2);

  clock = GST_CLOCK_CAST (self);

  if (gst_clock_add_observation (GST_CLOCK (self), local_avg, remote,
          &r_squared)) {
    /* ghetto formula - shorter timeout for bad correlations */
    current_timeout = (1e-3 / (1 - MIN (r_squared, 0.99999))) * GST_SECOND;
    current_timeout = MIN (current_timeout, gst_clock_get_timeout (clock));
  } else {
    current_timeout = 0;
  }

  GST_INFO ("next timeout: %" GST_TIME_FORMAT, GST_TIME_ARGS (current_timeout));
  self->priv->timeout_expiration = gst_util_get_timestamp () + current_timeout;

  return;

bogus_observation:
  /* Schedule a new packet again soon */
  self->priv->timeout_expiration = gst_util_get_timestamp () + (GST_SECOND / 4);
  return;
}

static gpointer
gst_net_client_clock_thread (gpointer data)
{
  GstNetClientClock *self = data;
  GstNetTimePacket *packet;
  GSocket *socket = self->priv->socket;
  GError *err = NULL;
  GstClock *clock = data;

  GST_INFO_OBJECT (self, "net client clock thread running, socket=%p", socket);

  g_socket_set_blocking (socket, TRUE);
  g_socket_set_timeout (socket, 0);

  while (!g_cancellable_is_cancelled (self->priv->cancel)) {
    GstClockTime expiration_time = self->priv->timeout_expiration;
    GstClockTime now = gst_util_get_timestamp ();
    gint64 socket_timeout;

    if (now >= expiration_time || (expiration_time - now) <= GST_MSECOND) {
      socket_timeout = 0;
    } else {
      socket_timeout = (expiration_time - now) / GST_USECOND;
    }

    GST_TRACE_OBJECT (self, "timeout: %" G_GINT64_FORMAT "us", socket_timeout);

    if (!g_socket_condition_timed_wait (socket, G_IO_IN, socket_timeout,
            self->priv->cancel, &err)) {
      /* cancelled, timeout or error */
      if (err->code == G_IO_ERROR_CANCELLED) {
        GST_INFO_OBJECT (self, "cancelled");
        g_clear_error (&err);
        break;
      } else if (err->code == G_IO_ERROR_TIMED_OUT) {
        /* timed out, let's send another packet */
        GST_DEBUG_OBJECT (self, "timed out");

        packet = gst_net_time_packet_new (NULL);

        packet->local_time = gst_clock_get_internal_time (GST_CLOCK (self));

        GST_DEBUG_OBJECT (self,
            "sending packet, local time = %" GST_TIME_FORMAT,
            GST_TIME_ARGS (packet->local_time));

        gst_net_time_packet_send (packet, self->priv->socket,
            self->priv->servaddr, NULL);

        g_free (packet);

        /* reset timeout (but are expecting a response sooner anyway) */
        self->priv->timeout_expiration =
            gst_util_get_timestamp () + gst_clock_get_timeout (clock);
      } else {
        GST_DEBUG_OBJECT (self, "socket error: %s", err->message);
        g_usleep (G_USEC_PER_SEC / 10); /* throttle */
      }
      g_clear_error (&err);
    } else {
      GstClockTime new_local;

      /* got packet */

      new_local = gst_clock_get_internal_time (GST_CLOCK (self));

      packet = gst_net_time_packet_receive (socket, NULL, &err);

      if (packet != NULL) {
        GST_LOG_OBJECT (self, "got packet back");
        GST_LOG_OBJECT (self, "local_1 = %" GST_TIME_FORMAT,
            GST_TIME_ARGS (packet->local_time));
        GST_LOG_OBJECT (self, "remote = %" GST_TIME_FORMAT,
            GST_TIME_ARGS (packet->remote_time));
        GST_LOG_OBJECT (self, "local_2 = %" GST_TIME_FORMAT,
            GST_TIME_ARGS (new_local));

        /* observe_times will reset the timeout */
        gst_net_client_clock_observe_times (self, packet->local_time,
            packet->remote_time, new_local);

        g_free (packet);
      } else if (err != NULL) {
        GST_WARNING_OBJECT (self, "receive error: %s", err->message);
        g_clear_error (&err);
      }
    }
  }

  GST_INFO_OBJECT (self, "shutting down net client clock thread");
  return NULL;
}

static gboolean
gst_net_client_clock_start (GstNetClientClock * self)
{
  GSocketAddress *servaddr;
  GSocketAddress *myaddr;
  GSocketAddress *anyaddr;
  GInetAddress *inetaddr;
  GSocket *socket;
  GError *error = NULL;
  GSocketFamily family;

  g_return_val_if_fail (self->priv->address != NULL, FALSE);
  g_return_val_if_fail (self->priv->servaddr == NULL, FALSE);

  /* create target address */
  inetaddr = g_inet_address_new_from_string (self->priv->address);
  if (inetaddr == NULL)
    goto bad_address;

  family = g_inet_address_get_family (inetaddr);

  servaddr = g_inet_socket_address_new (inetaddr, self->priv->port);
  g_object_unref (inetaddr);

  g_assert (servaddr != NULL);

  GST_DEBUG_OBJECT (self, "will communicate with %s:%d", self->priv->address,
      self->priv->port);

  socket = g_socket_new (family, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, &error);

  if (socket == NULL)
    goto no_socket;

  GST_DEBUG_OBJECT (self, "binding socket");
  inetaddr = g_inet_address_new_any (family);
  anyaddr = g_inet_socket_address_new (inetaddr, 0);
  g_socket_bind (socket, anyaddr, TRUE, &error);
  g_object_unref (anyaddr);
  g_object_unref (inetaddr);

  if (error != NULL)
    goto bind_error;

  /* check address we're bound to, mostly for debugging purposes */
  myaddr = g_socket_get_local_address (socket, &error);

  if (myaddr == NULL)
    goto getsockname_error;

  GST_DEBUG_OBJECT (self, "socket opened on UDP port %hd",
      g_inet_socket_address_get_port (G_INET_SOCKET_ADDRESS (myaddr)));

  g_object_unref (myaddr);

  self->priv->cancel = g_cancellable_new ();
  self->priv->socket = socket;
  self->priv->servaddr = G_SOCKET_ADDRESS (servaddr);

  self->priv->thread = g_thread_try_new ("GstNetClientClock",
      gst_net_client_clock_thread, self, &error);

  if (error != NULL)
    goto no_thread;

  return TRUE;

  /* ERRORS */
no_socket:
  {
    GST_ERROR_OBJECT (self, "socket_new() failed: %s", error->message);
    g_error_free (error);
    return FALSE;
  }
bind_error:
  {
    GST_ERROR_OBJECT (self, "bind failed: %s", error->message);
    g_error_free (error);
    g_object_unref (socket);
    return FALSE;
  }
getsockname_error:
  {
    GST_ERROR_OBJECT (self, "get_local_address() failed: %s", error->message);
    g_error_free (error);
    g_object_unref (socket);
    return FALSE;
  }
bad_address:
  {
    GST_ERROR_OBJECT (self, "inet_address_new_from_string('%s') failed",
        self->priv->address);
    return FALSE;
  }
no_thread:
  {
    GST_ERROR_OBJECT (self, "could not create thread: %s", error->message);
    g_object_unref (self->priv->servaddr);
    self->priv->servaddr = NULL;
    g_object_unref (self->priv->socket);
    self->priv->socket = NULL;
    g_error_free (error);
    return FALSE;
  }
}

static void
gst_net_client_clock_stop (GstNetClientClock * self)
{
  if (self->priv->thread == NULL)
    return;

  GST_INFO_OBJECT (self, "stopping...");
  g_cancellable_cancel (self->priv->cancel);

  g_thread_join (self->priv->thread);
  self->priv->thread = NULL;

  g_object_unref (self->priv->cancel);
  self->priv->cancel = NULL;

  g_object_unref (self->priv->servaddr);
  self->priv->servaddr = NULL;

  g_object_unref (self->priv->socket);
  self->priv->socket = NULL;

  GST_INFO_OBJECT (self, "stopped");
}

/**
 * gst_net_client_clock_new:
 * @name: a name for the clock
 * @remote_address: the address of the remote clock provider
 * @remote_port: the port of the remote clock provider
 * @base_time: initial time of the clock
 *
 * Create a new #GstNetClientClock that will report the time
 * provided by the #GstNetTimeProvider on @remote_address and 
 * @remote_port.
 *
 * Returns: a new #GstClock that receives a time from the remote
 * clock.
 */
GstClock *
gst_net_client_clock_new (const gchar * name, const gchar * remote_address,
    gint remote_port, GstClockTime base_time)
{
  /* FIXME: gst_net_client_clock_new() should be a thin wrapper for g_object_new() */
  GstNetClientClock *ret;
  GstClockTime internal;

  g_return_val_if_fail (remote_address != NULL, NULL);
  g_return_val_if_fail (remote_port > 0, NULL);
  g_return_val_if_fail (remote_port <= G_MAXUINT16, NULL);
  g_return_val_if_fail (base_time != GST_CLOCK_TIME_NONE, NULL);

  ret = g_object_new (GST_TYPE_NET_CLIENT_CLOCK, "address", remote_address,
      "port", remote_port, NULL);

  /* gst_clock_get_time() values are guaranteed to be increasing. because no one
   * has called get_time on this clock yet we are free to adjust to any value
   * without worrying about worrying about MAX() issues with the clock's
   * internal time.
   */

  /* update our internal time so get_time() give something around base_time.
     assume that the rate is 1 in the beginning. */
  internal = gst_clock_get_internal_time (GST_CLOCK (ret));
  gst_clock_set_calibration (GST_CLOCK (ret), internal, base_time, 1, 1);

  {
    GstClockTime now = gst_clock_get_time (GST_CLOCK (ret));

    if (GST_CLOCK_DIFF (now, base_time) > 0 ||
        GST_CLOCK_DIFF (now, base_time + GST_SECOND) < 0) {
      g_warning ("unable to set the base time, expect sync problems!");
    }
  }

  if (!gst_net_client_clock_start (ret))
    goto failed_start;

  /* all systems go, cap'n */
  return (GstClock *) ret;

failed_start:
  {
    /* already printed a nice error */
    gst_object_unref (ret);
    return NULL;
  }
}
