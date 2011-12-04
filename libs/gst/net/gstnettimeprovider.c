/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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
 * SECTION:gstnettimeprovider
 * @short_description: Special object that exposed the time of a clock
 *                     on the network.
 * @see_also: #GstClock, #GstNetClientClock, #GstPipeline
 *
 * This object exposes the time of a #GstClock on the network.
 *
 * A #GstNetTimeProvider is created with gst_net_time_provider_new() which
 * takes a #GstClock, an address and a port number as arguments.
 *
 * After creating the object, a client clock such as #GstNetClientClock can
 * query the exposed clock over the network for its values.
 *
 * The #GstNetTimeProvider typically wraps the clock used by a #GstPipeline.
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnettimeprovider.h"
#include "gstnettimepacket.h"

#include <glib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined (_MSC_VER) && _MSC_VER >= 1400
#include <io.h>
#endif

#ifndef G_OS_WIN32
#include <sys/ioctl.h>
#endif

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

GST_DEBUG_CATEGORY_STATIC (ntp_debug);
#define GST_CAT_DEFAULT (ntp_debug)

#ifdef G_OS_WIN32
#define close(sock) closesocket(sock)
#endif

#define DEFAULT_ADDRESS         "0.0.0.0"
#define DEFAULT_PORT            5637

#define IS_ACTIVE(self) (g_atomic_int_get (&((self)->active.active)))

#ifdef G_OS_WIN32
#define setsockopt(sock, sol_flags, reuse_flags, ru, sizeofru) setsockopt (sock, sol_flags, reuse_flags, (char *)ru, sizeofru)
#endif
enum
{
  PROP_0,
  PROP_PORT,
  PROP_ADDRESS,
  PROP_CLOCK,
  PROP_ACTIVE
      /* FILL ME */
};

#define GST_NET_TIME_PROVIDER_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_NET_TIME_PROVIDER, GstNetTimeProviderPrivate))

struct _GstNetTimeProviderPrivate
{
  GstPollFD sock;
  GstPoll *fdset;
};

static gboolean gst_net_time_provider_start (GstNetTimeProvider * bself);
static void gst_net_time_provider_stop (GstNetTimeProvider * bself);

static gpointer gst_net_time_provider_thread (gpointer data);

static void gst_net_time_provider_finalize (GObject * object);
static void gst_net_time_provider_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_net_time_provider_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

#define _do_init(type) \
  GST_DEBUG_CATEGORY_INIT (ntp_debug, "nettime", 0, "Network time provider");

GST_BOILERPLATE_FULL (GstNetTimeProvider, gst_net_time_provider, GstObject,
    GST_TYPE_OBJECT, _do_init);

#ifdef G_OS_WIN32
static int
inet_aton (const char *c, struct in_addr *paddr)
{
  paddr->s_addr = inet_addr (c);
  if (paddr->s_addr == INADDR_NONE)
    return 0;

  return 1;
}
#endif

static void
gst_net_time_provider_base_init (gpointer g_class)
{
  /* Do nothing here */
}

static void
gst_net_time_provider_class_init (GstNetTimeProviderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  g_assert (sizeof (GstClockTime) == 8);

  g_type_class_add_private (klass, sizeof (GstNetTimeProviderPrivate));

  gobject_class->finalize = gst_net_time_provider_finalize;
  gobject_class->set_property = gst_net_time_provider_set_property;
  gobject_class->get_property = gst_net_time_provider_get_property;

  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port to receive the packets from, 0=allocate", 0, G_MAXUINT16,
          DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "address",
          "The address to bind on, as a dotted quad (x.x.x.x)",
          DEFAULT_ADDRESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CLOCK,
      g_param_spec_object ("clock", "Clock",
          "The clock to export over the network", GST_TYPE_CLOCK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ACTIVE,
      g_param_spec_boolean ("active", "Active",
          "TRUE if the clock will respond to queries over the network", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_net_time_provider_init (GstNetTimeProvider * self,
    GstNetTimeProviderClass * g_class)
{
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
  self->priv = GST_NET_TIME_PROVIDER_GET_PRIVATE (self);

  self->port = DEFAULT_PORT;
  self->priv->sock.fd = -1;
  self->address = g_strdup (DEFAULT_ADDRESS);
  self->thread = NULL;
  self->active.active = TRUE;
}

static void
gst_net_time_provider_finalize (GObject * object)
{
  GstNetTimeProvider *self = GST_NET_TIME_PROVIDER (object);

  if (self->thread) {
    gst_net_time_provider_stop (self);
    g_assert (self->thread == NULL);
  }

  if (self->priv->fdset) {
    gst_poll_free (self->priv->fdset);
    self->priv->fdset = NULL;
  }

  g_free (self->address);
  self->address = NULL;

  if (self->clock)
    gst_object_unref (self->clock);
  self->clock = NULL;

#ifdef G_OS_WIN32
  WSACleanup ();
#endif

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gpointer
gst_net_time_provider_thread (gpointer data)
{
  GstNetTimeProvider *self = data;
  struct sockaddr_in tmpaddr;
  socklen_t len;
  GstNetTimePacket *packet;
  gint ret;

  while (TRUE) {
    GST_LOG_OBJECT (self, "doing select");
    ret = gst_poll_wait (self->priv->fdset, GST_CLOCK_TIME_NONE);
    GST_LOG_OBJECT (self, "select returned %d", ret);

    if (ret <= 0) {
      if (errno == EBUSY) {
        GST_LOG_OBJECT (self, "stop");
        goto stopped;
      } else if (errno != EAGAIN && errno != EINTR)
        goto select_error;
      else
        continue;
    } else {
      /* got data in */
      len = sizeof (struct sockaddr);

      packet = gst_net_time_packet_receive (self->priv->sock.fd,
          (struct sockaddr *) &tmpaddr, &len);

      if (!packet)
        goto receive_error;

      if (IS_ACTIVE (self)) {
        /* do what we were asked to and send the packet back */
        packet->remote_time = gst_clock_get_time (self->clock);

        /* ignore errors */
        gst_net_time_packet_send (packet, self->priv->sock.fd,
            (struct sockaddr *) &tmpaddr, len);
      }

      g_free (packet);

      continue;
    }

    g_assert_not_reached ();

    /* log errors and keep going */
  select_error:
    {
      GST_DEBUG_OBJECT (self, "select error %d: %s (%d)", ret,
          g_strerror (errno), errno);
      continue;
    }
  stopped:
    {
      GST_DEBUG_OBJECT (self, "shutting down");
      /* close socket */
      return NULL;
    }
  receive_error:
    {
      GST_DEBUG_OBJECT (self, "receive error");
      continue;
    }

    g_assert_not_reached ();

  }

  g_assert_not_reached ();

  return NULL;
}

static void
gst_net_time_provider_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstNetTimeProvider *self = GST_NET_TIME_PROVIDER (object);
  GstClock **clock_p = &self->clock;

  switch (prop_id) {
    case PROP_PORT:
      self->port = g_value_get_int (value);
      break;
    case PROP_ADDRESS:
      g_free (self->address);
      if (g_value_get_string (value) == NULL)
        self->address = g_strdup (DEFAULT_ADDRESS);
      else
        self->address = g_strdup (g_value_get_string (value));
      break;
    case PROP_CLOCK:
      gst_object_replace ((GstObject **) clock_p,
          (GstObject *) g_value_get_object (value));
      break;
    case PROP_ACTIVE:
      g_atomic_int_set (&self->active.active, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_net_time_provider_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstNetTimeProvider *self = GST_NET_TIME_PROVIDER (object);

  switch (prop_id) {
    case PROP_PORT:
      g_value_set_int (value, self->port);
      break;
    case PROP_ADDRESS:
      g_value_set_string (value, self->address);
      break;
    case PROP_CLOCK:
      g_value_set_object (value, self->clock);
      break;
    case PROP_ACTIVE:
      g_value_set_boolean (value, IS_ACTIVE (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_net_time_provider_start (GstNetTimeProvider * self)
{
  gint ru;
  struct sockaddr_in my_addr;
  socklen_t len;
  int port;
  gint ret;
  GError *error = NULL;

  if ((ret = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    goto no_socket;

  self->priv->sock.fd = ret;

  ru = 1;
  ret =
      setsockopt (self->priv->sock.fd, SOL_SOCKET, SO_REUSEADDR, &ru,
      sizeof (ru));
  if (ret < 0)
    goto setsockopt_error;

  memset (&my_addr, 0, sizeof (my_addr));
  my_addr.sin_family = AF_INET; /* host byte order */
  my_addr.sin_port = htons ((gint16) self->port);       /* short, network byte order */
  my_addr.sin_addr.s_addr = INADDR_ANY;
  if (self->address) {
    ret = inet_aton (self->address, &my_addr.sin_addr);
    if (ret == 0)
      goto invalid_address_error;
  }

  GST_DEBUG_OBJECT (self, "binding on port %d", self->port);
  ret =
      bind (self->priv->sock.fd, (struct sockaddr *) &my_addr,
      sizeof (my_addr));
  if (ret < 0)
    goto bind_error;

  len = sizeof (my_addr);
  ret = getsockname (self->priv->sock.fd, (struct sockaddr *) &my_addr, &len);
  if (ret < 0)
    goto getsockname_error;

  port = ntohs (my_addr.sin_port);
  GST_DEBUG_OBJECT (self, "bound, on port %d", port);

  if (port != self->port) {
    self->port = port;
    GST_DEBUG_OBJECT (self, "notifying %d", port);
    g_object_notify (G_OBJECT (self), "port");
  }

  gst_poll_add_fd (self->priv->fdset, &self->priv->sock);
  gst_poll_fd_ctl_read (self->priv->fdset, &self->priv->sock, TRUE);

#if !GLIB_CHECK_VERSION (2, 31, 0)
  self->thread = g_thread_create (gst_net_time_provider_thread, self, TRUE,
      &error);
#else
  self->thread = g_thread_try_new ("GstNetTimeProvider",
      gst_net_time_provider_thread, self, &error);
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
setsockopt_error:
  {
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    GST_ERROR_OBJECT (self, "setsockopt failed %d: %s (%d)", ret,
        g_strerror (errno), errno);
    return FALSE;
  }
invalid_address_error:
  {
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    GST_ERROR_OBJECT (self, "invalid network address %s: %s (%d)",
        self->address, g_strerror (errno), errno);
    return FALSE;
  }
bind_error:
  {
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    GST_ERROR_OBJECT (self, "bind failed %d: %s (%d)", ret,
        g_strerror (errno), errno);
    return FALSE;
  }
getsockname_error:
  {
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    GST_ERROR_OBJECT (self, "getsockname failed %d: %s (%d)", ret,
        g_strerror (errno), errno);
    return FALSE;
  }
no_thread:
  {
    gst_poll_remove_fd (self->priv->fdset, &self->priv->sock);
    close (self->priv->sock.fd);
    self->priv->sock.fd = -1;
    GST_ERROR_OBJECT (self, "could not create thread: %s", error->message);
    g_error_free (error);
    return FALSE;
  }
}

static void
gst_net_time_provider_stop (GstNetTimeProvider * self)
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
 * gst_net_time_provider_new:
 * @clock: a #GstClock to export over the network
 * @address: an address to bind on as a dotted quad (xxx.xxx.xxx.xxx), or NULL
 *           to bind to all addresses
 * @port: a port to bind on, or 0 to let the kernel choose
 *
 * Allows network clients to get the current time of @clock.
 *
 * Returns: the new #GstNetTimeProvider, or NULL on error
 */
GstNetTimeProvider *
gst_net_time_provider_new (GstClock * clock, const gchar * address, gint port)
{
  GstNetTimeProvider *ret;

  g_return_val_if_fail (clock && GST_IS_CLOCK (clock), NULL);
  g_return_val_if_fail (port >= 0 && port <= G_MAXUINT16, NULL);

  ret = g_object_new (GST_TYPE_NET_TIME_PROVIDER, "clock", clock, "address",
      address, "port", port, NULL);

  if ((ret->priv->fdset = gst_poll_new (TRUE)) == NULL)
    goto no_fdset;

  if (!gst_net_time_provider_start (ret))
    goto failed_start;

  /* all systems go, cap'n */
  return ret;

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
