/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2005 Andy Wingo <wingo@pobox.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
 * Various parameters of the clock can be configured with the parent #GstClock
 * "timeout", "window-size" and "window-threshold" object properties.
 *
 * A #GstNetClientClock is typically set on a #GstPipeline with 
 * gst_pipeline_use_clock().
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnettimepacket.h"
#include "gstnetclientclock.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined (_MSC_VER) && _MSC_VER >= 1400
#include <io.h>
#endif

GST_DEBUG_CATEGORY_STATIC (ncc_debug);
#define GST_CAT_DEFAULT (ncc_debug)

#define DEFAULT_ADDRESS         "127.0.0.1"
#define DEFAULT_PORT            5637
#define DEFAULT_TIMEOUT         GST_SECOND

#ifdef G_OS_WIN32
#define getsockname(sock,addr,len) getsockname(sock,addr,(int *)len)
#endif

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
};

#define GST_NET_CLIENT_CLOCK_GET_PRIVATE(obj)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_NET_CLIENT_CLOCK, GstNetClientClockPrivate))

struct _GstNetClientClockPrivate
{
  GstPollFD sock;
  GstPoll *fdset;
};

#define _do_init(type) \
  GST_DEBUG_CATEGORY_INIT (ncc_debug, "netclock", 0, "Network client clock");

GST_BOILERPLATE_FULL (GstNetClientClock, gst_net_client_clock,
    GstSystemClock, GST_TYPE_SYSTEM_CLOCK, _do_init);

static void gst_net_client_clock_finalize (GObject * object);
static void gst_net_client_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_net_client_clock_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_net_client_clock_stop (GstNetClientClock * self);

#ifdef G_OS_WIN32
static int
inet_aton (const char *c, struct in_addr *paddr)
{
  /* note that inet_addr is deprecated on unix because
   * inet_addr returns -1 (INADDR_NONE) for the valid 255.255.255.255
   * address. */
  paddr->s_addr = inet_addr (c);
  if (paddr->s_addr == INADDR_NONE)
    return 0;

  return 1;
}
#endif

static void
gst_net_client_clock_base_init (gpointer g_class)
{
  /* nop */
}

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
          "The address of the machine providing a time server, "
          "as a dotted quad (x.x.x.x)", DEFAULT_ADDRESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port on which the remote server is listening", 0, G_MAXUINT16,
          DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_net_client_clock_init (GstNetClientClock * self,
    GstNetClientClockClass * g_class)
{
  GstClock *clock = GST_CLOCK_CAST (self);

#ifdef G_OS_WIN32
  WSADATA w;
  int error = WSAStartup (0x0202, &w);

  if (error) {
    GST_DEBUG_OBJECT (self, "Error on WSAStartup");
  }
  if (w.wVersion != 0x0202) {
    WSACleanup ();
  }
#endif
  self->priv = GST_NET_CLIENT_CLOCK_GET_PRIVATE (self);

  self->port = DEFAULT_PORT;
  self->address = g_strdup (DEFAULT_ADDRESS);

  clock->timeout = DEFAULT_TIMEOUT;

  self->priv->sock.fd = -1;
  self->thread = NULL;

  self->servaddr = NULL;
}

static void
gst_net_client_clock_finalize (GObject * object)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);

  if (self->thread) {
    gst_net_client_clock_stop (self);
    g_assert (self->thread == NULL);
  }

  if (self->priv->fdset) {
    gst_poll_free (self->priv->fdset);
    self->priv->fdset = NULL;
  }

  g_free (self->address);
  self->address = NULL;

  g_free (self->servaddr);
  self->servaddr = NULL;

#ifdef G_OS_WIN32
  WSACleanup ();
#endif

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_net_client_clock_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);

  switch (prop_id) {
    case PROP_ADDRESS:
      g_free (self->address);
      if (g_value_get_string (value) == NULL)
        self->address = g_strdup (DEFAULT_ADDRESS);
      else
        self->address = g_strdup (g_value_get_string (value));
      break;
    case PROP_PORT:
      self->port = g_value_get_int (value);
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
      g_value_set_string (value, self->address);
      break;
    case PROP_PORT:
      g_value_set_int (value, self->port);
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
  GstClockTime local_avg;
  gdouble r_squared;
  GstClock *clock;

  if (local_2 < local_1)
    goto bogus_observation;

  local_avg = (local_2 + local_1) / 2;

  clock = GST_CLOCK_CAST (self);

  gst_clock_add_observation (GST_CLOCK (self), local_avg, remote, &r_squared);

  GST_CLOCK_SLAVE_LOCK (self);
  if (clock->filling) {
    self->current_timeout = 0;
  } else {
    /* geto formula */
    self->current_timeout =
        (1e-3 / (1 - MIN (r_squared, 0.99999))) * GST_SECOND;
    self->current_timeout = MIN (self->current_timeout, clock->timeout);
  }
  GST_CLOCK_SLAVE_UNLOCK (clock);

  return;

bogus_observation:
  {
    GST_WARNING_OBJECT (self, "time packet receive time < send time (%"
        GST_TIME_FORMAT " < %" GST_TIME_FORMAT ")", GST_TIME_ARGS (local_1),
        GST_TIME_ARGS (local_2));
    return;
  }
}

static gint
gst_net_client_clock_do_select (GstNetClientClock * self)
{
  while (TRUE) {
    GstClockTime diff;
    gint ret;

    GST_LOG_OBJECT (self, "doing select");

    diff = gst_clock_get_internal_time (GST_CLOCK (self));
    ret = gst_poll_wait (self->priv->fdset, self->current_timeout);
    diff = gst_clock_get_internal_time (GST_CLOCK (self)) - diff;

    if (diff > self->current_timeout)
      self->current_timeout = 0;
    else
      self->current_timeout -= diff;

    GST_LOG_OBJECT (self, "select returned %d", ret);

    if (ret < 0 && errno != EBUSY) {
      if (errno != EAGAIN && errno != EINTR)
        goto select_error;
      else
        continue;
    } else {
      return ret;
    }

    g_assert_not_reached ();

    /* log errors and keep going */
  select_error:
    {
      GST_WARNING_OBJECT (self, "select error %d: %s (%d)", ret,
          g_strerror (errno), errno);
      continue;
    }
  }

  g_assert_not_reached ();
  return -1;
}

static gpointer
gst_net_client_clock_thread (gpointer data)
{
  GstNetClientClock *self = data;
  struct sockaddr_in tmpaddr;
  socklen_t len;
  GstNetTimePacket *packet;
  gint ret;
  GstClock *clock = data;

  while (TRUE) {
    ret = gst_net_client_clock_do_select (self);

    if (ret < 0 && errno == EBUSY) {
      GST_LOG_OBJECT (self, "stop");
      goto stopped;
    } else if (ret == 0) {
      /* timed out, let's send another packet */
      GST_DEBUG_OBJECT (self, "timed out");

      packet = gst_net_time_packet_new (NULL);

      packet->local_time = gst_clock_get_internal_time (GST_CLOCK (self));

      GST_DEBUG_OBJECT (self, "sending packet, local time = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (packet->local_time));
      gst_net_time_packet_send (packet, self->priv->sock.fd,
          (struct sockaddr *) self->servaddr, sizeof (struct sockaddr_in));

      g_free (packet);

      /* reset timeout */
      self->current_timeout = clock->timeout;
      continue;
    } else if (gst_poll_fd_can_read (self->priv->fdset, &self->priv->sock)) {
      /* got data in */
      GstClockTime new_local = gst_clock_get_internal_time (GST_CLOCK (self));

      len = sizeof (struct sockaddr);
      packet = gst_net_time_packet_receive (self->priv->sock.fd,
          (struct sockaddr *) &tmpaddr, &len);

      if (!packet)
        goto receive_error;

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
      continue;
    } else {
      GST_WARNING_OBJECT (self, "unhandled select return state?");
      continue;
    }

    g_assert_not_reached ();

  stopped:
    {
      GST_DEBUG_OBJECT (self, "shutting down");
      /* socket gets closed in _stop() */
      return NULL;
    }
  receive_error:
    {
      GST_WARNING_OBJECT (self, "receive error");
      continue;
    }

    g_assert_not_reached ();

  }

  g_assert_not_reached ();

  return NULL;
}

static gboolean
gst_net_client_clock_start (GstNetClientClock * self)
{
  struct sockaddr_in servaddr, myaddr;
  socklen_t len;
  gint ret;
  GError *error = NULL;

  g_return_val_if_fail (self->address != NULL, FALSE);
  g_return_val_if_fail (self->servaddr == NULL, FALSE);

  if ((ret = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    goto no_socket;

  self->priv->sock.fd = ret;

  len = sizeof (myaddr);
  ret = getsockname (self->priv->sock.fd, (struct sockaddr *) &myaddr, &len);
  if (ret < 0)
    goto getsockname_error;

  memset (&servaddr, 0, sizeof (servaddr));
  servaddr.sin_family = AF_INET;        /* host byte order */
  servaddr.sin_port = htons (self->port);       /* short, network byte order */

  GST_DEBUG_OBJECT (self, "socket opened on UDP port %hd",
      ntohs (servaddr.sin_port));

  if (!inet_aton (self->address, &servaddr.sin_addr))
    goto bad_address;

  self->servaddr = g_malloc (sizeof (struct sockaddr_in));
  memcpy (self->servaddr, &servaddr, sizeof (servaddr));

  GST_DEBUG_OBJECT (self, "will communicate with %s:%d", self->address,
      self->port);

  gst_poll_add_fd (self->priv->fdset, &self->priv->sock);
  gst_poll_fd_ctl_read (self->priv->fdset, &self->priv->sock, TRUE);

#if !GLIB_CHECK_VERSION (2, 31, 0)
  self->thread = g_thread_create (gst_net_client_clock_thread, self, TRUE,
      &error);
#else
  self->thread = g_thread_try_new ("GstNetClientClock",
      gst_net_client_clock_thread, self, &error);
#endif

  if (error != NULL)
    goto no_thread;

  return TRUE;

  /* ERRORS */
no_socket:
  {
    GST_ERROR_OBJECT (self, "socket failed %d: %s (%d)", ret,
        g_strerror (errno), errno);
    return FALSE;
  }
getsockname_error:
  {
    GST_ERROR_OBJECT (self, "getsockname failed %d: %s (%d)", ret,
        g_strerror (errno), errno);
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    return FALSE;
  }
bad_address:
  {
    GST_ERROR_OBJECT (self, "inet_aton failed %d: %s (%d)", ret,
        g_strerror (errno), errno);
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    return FALSE;
  }
no_thread:
  {
    GST_ERROR_OBJECT (self, "could not create thread: %s", error->message);
    gst_poll_remove_fd (self->priv->fdset, &self->priv->sock);
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    g_free (self->servaddr);
    self->servaddr = NULL;
    g_error_free (error);
    return FALSE;
  }
}

static void
gst_net_client_clock_stop (GstNetClientClock * self)
{
  gst_poll_set_flushing (self->priv->fdset, TRUE);
  g_thread_join (self->thread);
  self->thread = NULL;

  if (self->priv->sock.fd != -1) {
    gst_poll_remove_fd (self->priv->fdset, &self->priv->sock);
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
  }
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
gst_net_client_clock_new (gchar * name, const gchar * remote_address,
    gint remote_port, GstClockTime base_time)
{
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

  if ((ret->priv->fdset = gst_poll_new (TRUE)) == NULL)
    goto no_fdset;

  if (!gst_net_client_clock_start (ret))
    goto failed_start;

  /* all systems go, cap'n */
  return (GstClock *) ret;

no_fdset:
  {
    GST_ERROR_OBJECT (ret, "could not create an fdset: %s (%d)",
        g_strerror (errno), errno);
    gst_object_unref (ret);
    return NULL;
  }
failed_start:
  {
    /* already printed a nice error */
    gst_object_unref (ret);
    return NULL;
  }
}
