/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2010 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2012 Collabora Ltd. <tim.muller@collabora.co.uk>
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
 * SECTION:gstnettimepacket
 * @title: GstNetTimePacket
 * @short_description: Helper structure to construct clock packets used
 *                     by network clocks.
 * @see_also: #GstClock, #GstNetClientClock, #GstNetTimeProvider
 *
 * Various functions for receiving, sending an serializing #GstNetTimePacket
 * structures.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>

#ifdef __CYGWIN__
# include <unistd.h>
# include <fcntl.h>
#endif

#include "gstnettimepacket.h"

G_DEFINE_BOXED_TYPE (GstNetTimePacket, gst_net_time_packet,
    gst_net_time_packet_copy, gst_net_time_packet_free);

/**
 * gst_net_time_packet_new:
 * @buffer: (array): a buffer from which to construct the packet, or NULL
 *
 * Creates a new #GstNetTimePacket from a buffer received over the network. The
 * caller is responsible for ensuring that @buffer is at least
 * #GST_NET_TIME_PACKET_SIZE bytes long.
 *
 * If @buffer is %NULL, the local and remote times will be set to
 * #GST_CLOCK_TIME_NONE.
 *
 * MT safe. Caller owns return value (gst_net_time_packet_free to free).
 *
 * Returns: The new #GstNetTimePacket.
 */
GstNetTimePacket *
gst_net_time_packet_new (const guint8 * buffer)
{
  GstNetTimePacket *ret;

  g_assert (sizeof (GstClockTime) == 8);

  ret = g_new0 (GstNetTimePacket, 1);

  if (buffer) {
    ret->local_time = GST_READ_UINT64_BE (buffer);
    ret->remote_time = GST_READ_UINT64_BE (buffer + sizeof (GstClockTime));
  } else {
    ret->local_time = GST_CLOCK_TIME_NONE;
    ret->remote_time = GST_CLOCK_TIME_NONE;
  }

  return ret;
}

/**
 * gst_net_time_packet_free:
 * @packet: the #GstNetTimePacket
 *
 * Free @packet.
 */
void
gst_net_time_packet_free (GstNetTimePacket * packet)
{
  g_free (packet);
}

/**
 * gst_net_time_packet_copy:
 * @packet: the #GstNetTimePacket
 *
 * Make a copy of @packet.
 *
 * Returns: a copy of @packet, free with gst_net_time_packet_free().
 */
GstNetTimePacket *
gst_net_time_packet_copy (const GstNetTimePacket * packet)
{
  GstNetTimePacket *ret;

  ret = g_new0 (GstNetTimePacket, 1);
  ret->local_time = packet->local_time;
  ret->remote_time = packet->remote_time;

  return ret;
}

/**
 * gst_net_time_packet_serialize:
 * @packet: the #GstNetTimePacket
 *
 * Serialized a #GstNetTimePacket into a newly-allocated sequence of
 * #GST_NET_TIME_PACKET_SIZE bytes, in network byte order. The value returned is
 * suitable for passing to write(2) or sendto(2) for communication over the
 * network.
 *
 * MT safe. Caller owns return value (g_free to free).
 *
 * Returns: A newly allocated sequence of #GST_NET_TIME_PACKET_SIZE bytes.
 */
guint8 *
gst_net_time_packet_serialize (const GstNetTimePacket * packet)
{
  guint8 *ret;

  g_assert (sizeof (GstClockTime) == 8);

  ret = g_new0 (guint8, GST_NET_TIME_PACKET_SIZE);

  GST_WRITE_UINT64_BE (ret, packet->local_time);
  GST_WRITE_UINT64_BE (ret + sizeof (GstClockTime), packet->remote_time);

  return ret;
}

/**
 * gst_net_time_packet_receive:
 * @socket: socket to receive the time packet on
 * @src_address: (out): address of variable to return sender address
 * @error: return address for a #GError, or NULL
 *
 * Receives a #GstNetTimePacket over a socket. Handles interrupted system
 * calls, but otherwise returns NULL on error.
 *
 * Returns: (transfer full): a new #GstNetTimePacket, or NULL on error. Free
 *    with gst_net_time_packet_free() when done.
 */
GstNetTimePacket *
gst_net_time_packet_receive (GSocket * socket,
    GSocketAddress ** src_address, GError ** error)
{
  gchar buffer[GST_NET_TIME_PACKET_SIZE];
  GError *err = NULL;
  gssize ret;

  g_return_val_if_fail (G_IS_SOCKET (socket), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  while (TRUE) {
    ret = g_socket_receive_from (socket, src_address, buffer,
        GST_NET_TIME_PACKET_SIZE, NULL, &err);

    if (ret < 0) {
      if (err->code == G_IO_ERROR_WOULD_BLOCK) {
        g_error_free (err);
        err = NULL;
        continue;
      } else {
        goto receive_error;
      }
    } else if (ret < GST_NET_TIME_PACKET_SIZE) {
      goto short_packet;
    } else {
      return gst_net_time_packet_new ((const guint8 *) buffer);
    }
  }

receive_error:
  {
    GST_DEBUG ("receive error: %s", err->message);
    g_propagate_error (error, err);
    return NULL;
  }
short_packet:
  {
    GST_DEBUG ("someone sent us a short packet (%" G_GSSIZE_FORMAT " < %d)",
        ret, GST_NET_TIME_PACKET_SIZE);
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
        "short time packet (%d < %d)", (int) ret, GST_NET_TIME_PACKET_SIZE);
    return NULL;
  }
}

/**
 * gst_net_time_packet_send:
 * @packet: the #GstNetTimePacket to send
 * @socket: socket to send the time packet on
 * @dest_address: address to send the time packet to
 * @error: return address for a #GError, or NULL
 *
 * Sends a #GstNetTimePacket over a socket.
 *
 * MT safe.
 *
 * Returns: TRUE if successful, FALSE in case an error occurred.
 */
gboolean
gst_net_time_packet_send (const GstNetTimePacket * packet,
    GSocket * socket, GSocketAddress * dest_address, GError ** error)
{
  gboolean was_blocking;
  guint8 *buffer;
  gssize res;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (G_IS_SOCKET (socket), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (dest_address), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  was_blocking = g_socket_get_blocking (socket);

  if (was_blocking)
    g_socket_set_blocking (socket, FALSE);

  /* FIXME: avoid pointless alloc/free, serialise into stack-allocated buffer */
  buffer = gst_net_time_packet_serialize (packet);

  res = g_socket_send_to (socket, dest_address, (const gchar *) buffer,
      GST_NET_TIME_PACKET_SIZE, NULL, error);

  /* datagram packets should be sent as a whole or not at all */
  g_assert (res < 0 || res == GST_NET_TIME_PACKET_SIZE);

  g_free (buffer);

  if (was_blocking)
    g_socket_set_blocking (socket, TRUE);

  return (res == GST_NET_TIME_PACKET_SIZE);
}
