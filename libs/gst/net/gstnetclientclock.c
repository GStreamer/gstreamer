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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <unistd.h>

#include "gstnettimepacket.h"
#include "gstnetclientclock.h"

GST_DEBUG_CATEGORY (ncc_debug);
#define GST_CAT_DEFAULT (ncc_debug)

#define DEBUGGING_ENABLED

#ifdef DEBUGGING_ENABLED
#define DEBUG(x, args...) g_print (x "\n", ##args)
#else
#define DEBUG(x, args...)       /* nop */
#endif

/* the select call is also performed on the control sockets, that way
 * we can send special commands to unblock or restart the select call */
#define CONTROL_RESTART        'R'      /* restart the select call */
#define CONTROL_STOP           'S'      /* stop the select call */
#define CONTROL_SOCKETS(self)   self->control_sock
#define WRITE_SOCKET(self)      self->control_sock[1]
#define READ_SOCKET(self)       self->control_sock[0]

#define SEND_COMMAND(self, command)             	\
G_STMT_START {                                 	\
  unsigned char c; c = command;                	\
  write (WRITE_SOCKET(self), &c, 1);         	\
} G_STMT_END

#define READ_COMMAND(self, command, res)        	\
G_STMT_START {                                	\
  res = read(READ_SOCKET(self), &command, 1);    \
} G_STMT_END

#define DEFAULT_ADDRESS		"127.0.0.1"
#define DEFAULT_PORT		5637
#define DEFAULT_WINDOW_SIZE	32
#define DEFAULT_TIMEOUT		GST_SECOND

enum
{
  PROP_0,
  PROP_ADDRESS,
  PROP_PORT,
  PROP_WINDOW_SIZE,
  PROP_TIMEOUT
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

static void
gst_net_client_clock_base_init (gpointer g_class)
{
  /* nop */
}

static void
gst_net_client_clock_class_init (GstNetClientClockClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_net_client_clock_finalize;
  gobject_class->get_property = gst_net_client_clock_get_property;
  gobject_class->set_property = gst_net_client_clock_set_property;

  g_object_class_install_property (gobject_class, PROP_ADDRESS,
      g_param_spec_string ("address", "address",
          "The address of the machine providing a time server, "
          "as a dotted quad (x.x.x.x)", DEFAULT_ADDRESS, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PORT,
      g_param_spec_int ("port", "port",
          "The port on which the remote server is listening", 0, G_MAXUINT16,
          DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_WINDOW_SIZE,
      g_param_spec_int ("window-size", "Window size",
          "The size of the window used to calculate rate and offset", 2, 1024,
          DEFAULT_WINDOW_SIZE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TIMEOUT,
      g_param_spec_uint64 ("timeout", "Timeout",
          "The amount of time, in nanoseconds, to wait for replies", 0,
          G_MAXUINT64, DEFAULT_TIMEOUT, G_PARAM_READWRITE));
}

static void
gst_net_client_clock_init (GstNetClientClock * self,
    GstNetClientClockClass * g_class)
{
  self->port = DEFAULT_PORT;
  self->address = g_strdup (DEFAULT_ADDRESS);
  self->window_size = DEFAULT_WINDOW_SIZE;
  self->timeout = DEFAULT_TIMEOUT;

  self->sock = -1;
  self->thread = NULL;

  self->filling = TRUE;
  self->time_index = 0;
  self->local_times = g_new0 (GstClockTime, self->window_size);
  self->remote_times = g_new0 (GstClockTime, self->window_size);

  self->servaddr = NULL;

  READ_SOCKET (self) = -1;
  WRITE_SOCKET (self) = -1;
}

static void
gst_net_client_clock_finalize (GObject * object)
{
  GstNetClientClock *self = GST_NET_CLIENT_CLOCK (object);

  if (self->thread) {
    gst_net_client_clock_stop (self);
    g_assert (self->thread == NULL);
  }

  if (READ_SOCKET (self) != -1) {
    close (READ_SOCKET (self));
    close (WRITE_SOCKET (self));
    READ_SOCKET (self) = -1;
    WRITE_SOCKET (self) = -1;
  }

  g_free (self->address);
  self->address = NULL;

  g_free (self->servaddr);
  self->servaddr = NULL;

  g_free (self->local_times);
  self->local_times = NULL;

  g_free (self->remote_times);
  self->remote_times = NULL;

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
    case PROP_WINDOW_SIZE:
      self->window_size = g_value_get_int (value);
      break;
    case PROP_TIMEOUT:
      self->timeout = g_value_get_uint64 (value);
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
    case PROP_WINDOW_SIZE:
      g_value_set_int (value, self->window_size);
      break;
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, self->timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* http://mathworld.wolfram.com/LeastSquaresFitting.html */
static gboolean
do_linear_regression (GstClockTime * x, GstClockTime * y, gint n, gdouble * m,
    GstClockTime * b, GstClockTime * xbase, gdouble * r_squared)
{
  GstClockTime *newx, *newy;
  GstClockTime xmin, ymin, xbar, ybar;
  GstClockTimeDiff sxx, sxy, syy;
  gint i;

  xbar = ybar = sxx = syy = sxy = 0;

#ifdef DEBUGGING_ENABLED
  DEBUG ("doing regression on:");
  for (i = 0; i < n; i++)
    DEBUG ("  %" G_GUINT64_FORMAT "  %" G_GUINT64_FORMAT, x[i], y[i]);
#endif

  xmin = ymin = G_MAXUINT64;
  for (i = 0; i < n; i++) {
    xmin = MIN (xmin, x[i]);
    ymin = MIN (ymin, y[i]);
  }

  DEBUG ("min x: %" G_GUINT64_FORMAT, xmin);
  DEBUG ("min y: %" G_GUINT64_FORMAT, ymin);

  newx = g_new (GstClockTime, n);
  newy = g_new (GstClockTime, n);

  /* strip off unnecessary bits of precision */
  for (i = 0; i < n; i++) {
    newx[i] = x[i] - xmin;
    newy[i] = y[i] - ymin;
  }

#ifdef DEBUGGING_ENABLED
  DEBUG ("reduced numbers:");
  for (i = 0; i < n; i++)
    DEBUG ("  %" G_GUINT64_FORMAT "  %" G_GUINT64_FORMAT, newx[i], newy[i]);
#endif

  /* have to do this precisely otherwise the results are pretty much useless.
   * should guarantee that none of these accumulators can overflow */

  /* quantities on the order of 1e10 -> 30 bits; window size a max of 2^10, so
     this addition could end up around 2^40 or so -- ample headroom */
  for (i = 0; i < n; i++) {
    xbar += newx[i];
    ybar += newy[i];
  }
  xbar /= n;
  ybar /= n;

  DEBUG ("  xbar  = %" G_GUINT64_FORMAT, xbar);
  DEBUG ("  ybar  = %" G_GUINT64_FORMAT, ybar);

  /* multiplying directly would give quantities on the order of 1e20 -> 60 bits;
     times the window size that's 70 which is too much. Instead we (1) subtract
     off the xbar*ybar in the loop instead of after, to avoid accumulation; (2)
     shift off 4 bits from each multiplicand, giving an expected ceiling of 52
     bits, which should be enough. Need to check the incoming range and domain
     to ensure this is an appropriate loss of precision though. */
  for (i = 0; i < n; i++) {
    sxx += (newx[i] >> 4) * (newx[i] >> 4) - (xbar >> 4) * (xbar >> 4);
    syy += (newy[i] >> 4) * (newy[i] >> 4) - (ybar >> 4) * (ybar >> 4);
    sxy += (newx[i] >> 4) * (newy[i] >> 4) - (xbar >> 4) * (ybar >> 4);
  }

  *m = ((double) sxy) / sxx;
  *xbase = xmin;
  *b = (ybar + ymin) - (GstClockTime) (xbar * *m);
  *r_squared = ((double) sxy * (double) sxy) / ((double) sxx * (double) syy);

  DEBUG ("  m      = %g", *m);
  DEBUG ("  b      = %" G_GUINT64_FORMAT, *b);
  DEBUG ("  xbase  = %" G_GUINT64_FORMAT, *xbase);
  DEBUG ("  r2     = %g", *r_squared);

  g_free (newx);
  g_free (newy);

  return TRUE;
}

static void
gst_net_client_clock_observe_times (GstNetClientClock * self,
    GstClockTime local_1, GstClockTime remote, GstClockTime local_2)
{
  GstClockTime local_avg;
  GstClockTime b, xbase;
  gdouble m, r_squared;

  if (local_2 < local_1)
    goto bogus_observation;

  local_avg = (local_2 + local_1) / 2;

  self->local_times[self->time_index] = local_avg;
  self->remote_times[self->time_index] = remote;

  self->time_index++;
  if (self->time_index == self->window_size) {
    self->filling = FALSE;
    self->time_index = 0;
  }

  if (!self->filling || self->time_index >= 4) {
    /* need to allow tuning of the "4" parameter -- means that we need 4 samples
     * before beginning to adjust the clock */
    do_linear_regression (self->local_times, self->remote_times,
        self->filling ? self->time_index : self->window_size, &m, &b,
        &xbase, &r_squared);

    GST_LOG_OBJECT (self, "adjusting clock to m=%g, b=%" G_GINT64_FORMAT
        " (rsquared=%g)", m, b, r_squared);

    gst_clock_set_calibration (GST_CLOCK (self), xbase, b, m);
  }

  if (self->filling) {
    self->current_timeout = 0;
  } else {
    /* geto formula */
    self->current_timeout =
        (1e-3 / (1 - MIN (r_squared, 0.99999))) * GST_SECOND;
    self->current_timeout = MIN (self->current_timeout, self->timeout);
  }

  return;

bogus_observation:
  {
    GST_WARNING_OBJECT (self, "time packet receive time < send time (%",
        GST_TIME_FORMAT, " < %" GST_TIME_FORMAT ")", GST_TIME_ARGS (local_1),
        GST_TIME_ARGS (local_2));
    return;
  }
}

static gint
gst_net_client_clock_do_select (GstNetClientClock * self, fd_set * readfds)
{
  gint max_sock;
  gint ret;

  while (TRUE) {
    FD_ZERO (readfds);
    FD_SET (self->sock, readfds);
    FD_SET (READ_SOCKET (self), readfds);
    max_sock = MAX (self->sock, READ_SOCKET (self));

    GST_LOG_OBJECT (self, "doing select");
    {
      GstClockTime diff;
      GTimeVal tv, *ptv = &tv;

      diff = gst_clock_get_internal_time (GST_CLOCK (self));
      GST_TIME_TO_TIMEVAL (self->current_timeout, tv);

      ret = select (max_sock + 1, readfds, NULL, NULL, (struct timeval *) ptv);

      diff = gst_clock_get_internal_time (GST_CLOCK (self)) - diff;

      if (diff > self->current_timeout)
        self->current_timeout = 0;
      else
        self->current_timeout -= diff;
    }
    GST_LOG_OBJECT (self, "select returned %d", ret);

    if (ret < 0) {
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
  fd_set read_fds;
  GstNetTimePacket *packet;
  gint ret;

  while (TRUE) {
    ret = gst_net_client_clock_do_select (self, &read_fds);

    if (FD_ISSET (READ_SOCKET (self), &read_fds)) {
      /* got control message */
      while (TRUE) {
        gchar command;
        int res;

        READ_COMMAND (self, command, res);
        if (res < 0) {
          GST_LOG_OBJECT (self, "no more commands");
          break;
        }

        DEBUG ("control message: '%c'", command);
        switch (command) {
          case CONTROL_STOP:
            /* break out of the select loop */
            GST_LOG_OBJECT (self, "stop");
            goto stopped;
          default:
            GST_WARNING_OBJECT (self, "unknown message: '%c'", command);
            g_warning ("netclientclock: unknown control message received");
            continue;
        }

        g_assert_not_reached ();
      }

      continue;
    } else if (ret == 0) {
      /* timed out, let's send another packet */
      DEBUG ("timed out");

      packet = gst_net_time_packet_new (NULL);

      packet->local_time = gst_clock_get_internal_time (GST_CLOCK (self));

      DEBUG ("sending packet, local time = %" GST_TIME_FORMAT,
          GST_TIME_ARGS (packet->local_time));
      gst_net_time_packet_send (packet, self->sock,
          (struct sockaddr *) self->servaddr, sizeof (struct sockaddr_in));

      g_free (packet);

      /* reset timeout */
      self->current_timeout = self->timeout;
      continue;
    } else if (FD_ISSET (self->sock, &read_fds)) {
      /* got data in */
      GstClockTime new_local = gst_clock_get_internal_time (GST_CLOCK (self));

      len = sizeof (struct sockaddr);
      packet = gst_net_time_packet_receive (self->sock,
          (struct sockaddr *) &tmpaddr, &len);

      if (!packet)
        goto receive_error;

      DEBUG ("got packet back");
      DEBUG ("local_1 = %" GST_TIME_FORMAT, GST_TIME_ARGS (packet->local_time));
      DEBUG ("remote = %" GST_TIME_FORMAT, GST_TIME_ARGS (packet->remote_time));
      DEBUG ("local_2 = %" GST_TIME_FORMAT, GST_TIME_ARGS (new_local));

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
  GError *error;

  g_return_val_if_fail (self->address != NULL, FALSE);
  g_return_val_if_fail (self->servaddr == NULL, FALSE);

  if ((ret = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    goto no_socket;

  self->sock = ret;

  len = sizeof (myaddr);
  ret = getsockname (self->sock, (struct sockaddr *) &myaddr, &len);
  if (ret < 0)
    goto getsockname_error;

  GST_DEBUG_OBJECT (self, "socket opened on UDP port %hd",
      ntohs (servaddr.sin_port));

  memset (&servaddr, 0, sizeof (servaddr));
  servaddr.sin_family = AF_INET;        /* host byte order */
  servaddr.sin_port = htons (self->port);       /* short, network byte order */
  if (!inet_aton (self->address, &servaddr.sin_addr))
    goto bad_address;

  self->servaddr = g_malloc (sizeof (struct sockaddr_in));
  memcpy (self->servaddr, &servaddr, sizeof (servaddr));

  GST_DEBUG_OBJECT (self, "will communicate with %s:%d", self->address,
      self->port);

  self->thread = g_thread_create (gst_net_client_clock_thread, self, TRUE,
      &error);
  if (!self->thread)
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
    close (self->sock);
    self->sock = -1;
    return FALSE;
  }
bad_address:
  {
    GST_ERROR_OBJECT (self, "inet_aton failed %d: %s (%d)", ret,
        g_strerror (errno), errno);
    close (self->sock);
    self->sock = -1;
    return FALSE;
  }
no_thread:
  {
    GST_ERROR_OBJECT (self, "could not create thread: %s", error->message);
    close (self->sock);
    self->sock = -1;
    g_free (self->servaddr);
    self->servaddr = NULL;
    g_error_free (error);
    return FALSE;
  }
}

static void
gst_net_client_clock_stop (GstNetClientClock * self)
{
  SEND_COMMAND (self, CONTROL_STOP);
  g_thread_join (self->thread);
  self->thread = NULL;

  if (self->sock != -1) {
    close (self->sock);
    self->sock = -1;
  }
}

GstClock *
gst_net_client_clock_new (gchar * name, const gchar * remote_address,
    gint remote_port, GstClockTime base_time)
{
  GstNetClientClock *ret;
  GstClockTime internal;
  gint iret;

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
  gst_clock_set_calibration (GST_CLOCK (ret), internal, base_time, 1.0);

  {
    GstClockTime now = gst_clock_get_time (GST_CLOCK (ret));

    if (now < base_time || now > base_time + GST_SECOND)
      g_warning ("unable to set the base time, expect sync problems!");
  }

  GST_DEBUG_OBJECT (ret, "creating socket pair");
  if ((iret = socketpair (PF_UNIX, SOCK_STREAM, 0, CONTROL_SOCKETS (ret))) < 0)
    goto no_socket_pair;

  fcntl (READ_SOCKET (ret), F_SETFL, O_NONBLOCK);
  fcntl (WRITE_SOCKET (ret), F_SETFL, O_NONBLOCK);

  if (!gst_net_client_clock_start (ret))
    goto failed_start;

  /* all systems go, cap'n */
  return (GstClock *) ret;

no_socket_pair:
  {
    GST_ERROR_OBJECT (ret, "no socket pair %d: %s (%d)", iret,
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
