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
 * SECTION:gstnettimepacket
 * @short_description: Helper structure to construct clock packets used
 *                     by network clocks.
 * @see_also: #GstClock, #GstNetClientClock, #GstNetTimeProvider
 *
 * Various functions for receiving, sending an serializing #GstNetTimePacket
 * structures.
 *
 * Last reviewed on 2005-11-23 (0.9.5)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnettimepacket.h"


/**
 * gst_net_time_packet_new:
 * @buffer: a buffer from which to construct the packet, or NULL
 *
 * Creates a new #GstNetTimePacket from a buffer received over the network. The
 * caller is responsible for ensuring that @buffer is at least
 * #GST_NET_TIME_PACKET_SIZE bytes long.
 *
 * If @buffer is #NULL, the local and remote times will be set to
 * #GST_CLOCK_TIME_NONE.
 *
 * MT safe. Caller owns return value (g_free to free).
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
 * @fd: a file descriptor created by socket(2)
 * @addr: a pointer to a sockaddr to hold the address of the sender
 * @len: a pointer to the size of the data pointed to by @addr
 *
 * Receives a #GstNetTimePacket over a socket. Handles interrupted system calls,
 * but otherwise returns NULL on error. See recvfrom(2) for more information on
 * how to interpret @sockaddr.
 *
 * MT safe. Caller owns return value (g_free to free).
 *
 * Returns: The new #GstNetTimePacket.
 */
GstNetTimePacket *
gst_net_time_packet_receive (gint fd, struct sockaddr * addr, socklen_t * len)
{
  guint8 buffer[GST_NET_TIME_PACKET_SIZE];
  gint ret;

  while (TRUE) {
    ret = recvfrom (fd, buffer, GST_NET_TIME_PACKET_SIZE,
        0, (struct sockaddr *) addr, len);
    if (ret < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto receive_error;
      else
        continue;
    } else if (ret < GST_NET_TIME_PACKET_SIZE) {
      goto short_packet;
    } else {
      return gst_net_time_packet_new (buffer);
    }
  }

receive_error:
  {
    GST_DEBUG ("receive error %d: %s (%d)", ret, g_strerror (errno), errno);
    return NULL;
  }
short_packet:
  {
    GST_DEBUG ("someone sent us a short packet (%d < %d)",
        ret, GST_NET_TIME_PACKET_SIZE);
    return NULL;
  }
}

/**
 * gst_net_time_packet_send:
 * @packet: the #GstNetTimePacket
 * @fd: a file descriptor created by socket(2)
 * @addr: a pointer to a sockaddr to hold the address of the sender
 * @len: the size of the data pointed to by @addr
 *
 * Sends a #GstNetTimePacket over a socket. Essentially a thin wrapper around
 * sendto(2) and gst_net_time_packet_serialize(). 
 *
 * MT safe.
 *
 * Returns: The return value of sendto(2).
 */
gint
gst_net_time_packet_send (const GstNetTimePacket * packet, gint fd,
    struct sockaddr * addr, socklen_t len)
{
  guint8 *buffer;
  gint ret;

  g_return_val_if_fail (packet != NULL, -EINVAL);

  buffer = gst_net_time_packet_serialize (packet);

  ret = sendto (fd, buffer, GST_NET_TIME_PACKET_SIZE, MSG_DONTWAIT, addr, len);

  g_free (buffer);

  return ret;
}
