/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
 * Copyright (C) 2010 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2012 Collabora Ltd. <tim.muller@collabora.co.uk>
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
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
/* THIS IS A PRIVATE API
 * SECTION:gstntppacket
 * @short_description: Helper structure to construct clock packets used
 *                     by network clocks for NTPv4.
 * @see_also: #GstClock, #GstNetClientClock, #GstNtpClock
 *
 * Various functions for receiving, sending an serializing #GstNtpPacket
 * structures.
 */

/* FIXME 2.0: Merge this with GstNetTimePacket! */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>

#ifdef __CYGWIN__
# include <unistd.h>
# include <fcntl.h>
#endif

#include <gst/gst.h>
#include <string.h>

#include "gstntppacket.h"

G_DEFINE_BOXED_TYPE (GstNtpPacket, gst_ntp_packet,
    gst_ntp_packet_copy, gst_ntp_packet_free);


static inline GstClockTime
ntp_timestamp_to_gst_clock_time (guint32 seconds, guint32 fraction)
{
  return gst_util_uint64_scale (seconds, GST_SECOND, 1) +
      gst_util_uint64_scale (fraction, GST_SECOND,
      G_GUINT64_CONSTANT (1) << 32);
}

static inline guint32
gst_clock_time_to_ntp_timestamp_seconds (GstClockTime gst)
{
  GstClockTime seconds = gst_util_uint64_scale (gst, 1, GST_SECOND);

  return seconds;
}

static inline guint32
gst_clock_time_to_ntp_timestamp_fraction (GstClockTime gst)
{
  GstClockTime seconds = gst_util_uint64_scale (gst, 1, GST_SECOND);

  return gst_util_uint64_scale (gst - seconds, G_GUINT64_CONSTANT (1) << 32,
      GST_SECOND);
}

/**
 * gst_ntp_packet_new:
 * @buffer: (array): a buffer from which to construct the packet, or NULL
 * @error: a #GError
 *
 * Creates a new #GstNtpPacket from a buffer received over the network. The
 * caller is responsible for ensuring that @buffer is at least
 * #GST_NTP_PACKET_SIZE bytes long.
 *
 * If @buffer is #NULL, the local and remote times will be set to
 * #GST_CLOCK_TIME_NONE.
 *
 * MT safe. Caller owns return value (gst_ntp_packet_free to free).
 *
 * Returns: The new #GstNtpPacket.
 */
GstNtpPacket *
gst_ntp_packet_new (const guint8 * buffer, GError ** error)
{
  GstNtpPacket *ret;

  g_assert (sizeof (GstClockTime) == 8);

  if (buffer) {
    guint8 version = (buffer[0] >> 3) & 0x7;
    guint8 stratum = buffer[1];
    gint8 poll_interval = buffer[2];

    if (version != 4) {
      g_set_error (error, GST_NTP_ERROR, GST_NTP_ERROR_WRONG_VERSION,
          "Invalid NTP version %d", version);
      return NULL;
    }

    /* Kiss-o'-Death packet! */
    if (stratum == 0) {
      gchar code[5] = { buffer[3 * 4 + 0], buffer[3 * 4 + 1], buffer[3 * 4 + 2],
        buffer[3 * 4 + 3], 0
      };

      /* AUTH, AUTO, CRYP, DENY, RSTR, NKEY => DENY */
      if (strcmp (code, "AUTH") == 0 ||
          strcmp (code, "AUTO") == 0 ||
          strcmp (code, "CRYP") == 0 ||
          strcmp (code, "DENY") == 0 ||
          strcmp (code, "RSTR") == 0 || strcmp (code, "NKEY") == 0) {
        g_set_error (error, GST_NTP_ERROR, GST_NTP_ERROR_KOD_DENY,
            "Kiss-o'-Death denied '%s'", code);
      } else if (strcmp (code, "RATE") == 0) {
        g_set_error (error, GST_NTP_ERROR, GST_NTP_ERROR_KOD_RATE,
            "Kiss-o'-Death '%s'", code);
      } else {
        g_set_error (error, GST_NTP_ERROR, GST_NTP_ERROR_KOD_UNKNOWN,
            "Kiss-o'-Death unknown '%s'", code);
      }

      return NULL;
    }

    ret = g_new0 (GstNtpPacket, 1);
    ret->origin_time =
        ntp_timestamp_to_gst_clock_time (GST_READ_UINT32_BE (buffer + 6 * 4),
        GST_READ_UINT32_BE (buffer + 7 * 4));
    ret->receive_time =
        ntp_timestamp_to_gst_clock_time (GST_READ_UINT32_BE (buffer + 8 * 4),
        GST_READ_UINT32_BE (buffer + 9 * 4));
    ret->transmit_time =
        ntp_timestamp_to_gst_clock_time (GST_READ_UINT32_BE (buffer + 10 * 4),
        GST_READ_UINT32_BE (buffer + 11 * 4));

    /* Wireshark considers everything >= 3 as invalid */
    if (poll_interval >= 3)
      ret->poll_interval = GST_CLOCK_TIME_NONE;
    else if (poll_interval >= 0)
      ret->poll_interval = GST_SECOND << poll_interval;
    else
      ret->poll_interval = GST_SECOND >> (-poll_interval);
  } else {
    ret = g_new0 (GstNtpPacket, 1);
    ret->origin_time = 0;
    ret->receive_time = 0;
    ret->transmit_time = 0;
    ret->poll_interval = 0;
  }

  return ret;
}

/**
 * gst_ntp_packet_free:
 * @packet: the #GstNtpPacket
 *
 * Free @packet.
 */
void
gst_ntp_packet_free (GstNtpPacket * packet)
{
  g_free (packet);
}

/**
 * gst_ntp_packet_copy:
 * @packet: the #GstNtpPacket
 *
 * Make a copy of @packet.
 *
 * Returns: a copy of @packet, free with gst_ntp_packet_free().
 */
GstNtpPacket *
gst_ntp_packet_copy (const GstNtpPacket * packet)
{
  GstNtpPacket *ret;

  ret = g_new0 (GstNtpPacket, 1);
  ret->origin_time = packet->origin_time;
  ret->receive_time = packet->receive_time;
  ret->transmit_time = packet->transmit_time;

  return ret;
}

/**
 * gst_ntp_packet_serialize:
 * @packet: the #GstNtpPacket
 *
 * Serialized a #GstNtpPacket into a newly-allocated sequence of
 * #GST_NTP_PACKET_SIZE bytes, in network byte order. The value returned is
 * suitable for passing to write(2) or sendto(2) for communication over the
 * network.
 *
 * MT safe. Caller owns return value (g_free to free).
 *
 * Returns: A newly allocated sequence of #GST_NTP_PACKET_SIZE bytes.
 */
guint8 *
gst_ntp_packet_serialize (const GstNtpPacket * packet)
{
  guint8 *ret;

  g_assert (sizeof (GstClockTime) == 8);

  ret = g_new0 (guint8, GST_NTP_PACKET_SIZE);
  /* Leap Indicator: unknown
   * Version: 4
   * Mode: Client
   */
  ret[0] = (3 << 6) | (4 << 3) | (3 << 0);
  /* Stratum: unsynchronized */
  ret[1] = 16;
  /* Polling interval: invalid */
  ret[2] = 3;
  /* Precision: 0 */
  ret[3] = 0;
  /* Root delay: 0 */
  GST_WRITE_UINT32_BE (ret + 4, 0);
  /* Root disperson: 0 */
  GST_WRITE_UINT32_BE (ret + 2 * 4, 0);
  /* Reference ID: \0 */
  GST_WRITE_UINT32_BE (ret + 3 * 4, 0);
  /* Reference Timestamp: 0 */
  GST_WRITE_UINT32_BE (ret + 4 * 4, 0);
  GST_WRITE_UINT32_BE (ret + 5 * 4, 0);
  /* Origin timestamp (local time) */
  GST_WRITE_UINT32_BE (ret + 6 * 4,
      gst_clock_time_to_ntp_timestamp_seconds (packet->origin_time));
  GST_WRITE_UINT32_BE (ret + 7 * 4,
      gst_clock_time_to_ntp_timestamp_fraction (packet->origin_time));
  /* Receive timestamp (remote time) */
  GST_WRITE_UINT32_BE (ret + 8 * 4,
      gst_clock_time_to_ntp_timestamp_seconds (packet->receive_time));
  GST_WRITE_UINT32_BE (ret + 9 * 4,
      gst_clock_time_to_ntp_timestamp_fraction (packet->receive_time));
  /* Transmit timestamp (remote time) */
  GST_WRITE_UINT32_BE (ret + 10 * 4,
      gst_clock_time_to_ntp_timestamp_seconds (packet->transmit_time));
  GST_WRITE_UINT32_BE (ret + 11 * 4,
      gst_clock_time_to_ntp_timestamp_fraction (packet->transmit_time));

  return ret;
}

/**
 * gst_ntp_packet_receive:
 * @socket: socket to receive the time packet on
 * @src_address: (out): address of variable to return sender address
 * @error: return address for a #GError, or NULL
 *
 * Receives a #GstNtpPacket over a socket. Handles interrupted system
 * calls, but otherwise returns NULL on error.
 *
 * Returns: (transfer full): a new #GstNtpPacket, or NULL on error. Free
 *    with gst_ntp_packet_free() when done.
 */
GstNtpPacket *
gst_ntp_packet_receive (GSocket * socket,
    GSocketAddress ** src_address, GError ** error)
{
  gchar buffer[GST_NTP_PACKET_SIZE];
  GError *err = NULL;
  gssize ret;

  g_return_val_if_fail (G_IS_SOCKET (socket), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  while (TRUE) {
    ret = g_socket_receive_from (socket, src_address, buffer,
        GST_NTP_PACKET_SIZE, NULL, &err);

    if (ret < 0) {
      if (err->code == G_IO_ERROR_WOULD_BLOCK) {
        g_error_free (err);
        err = NULL;
        continue;
      } else {
        goto receive_error;
      }
    } else if (ret < GST_NTP_PACKET_SIZE) {
      goto short_packet;
    } else {
      return gst_ntp_packet_new ((const guint8 *) buffer, error);
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
        ret, GST_NTP_PACKET_SIZE);
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
        "short time packet (%d < %d)", (int) ret, GST_NTP_PACKET_SIZE);
    return NULL;
  }
}

/**
 * gst_ntp_packet_send:
 * @packet: the #GstNtpPacket to send
 * @socket: socket to send the time packet on
 * @dest_address: address to send the time packet to
 * @error: return address for a #GError, or NULL
 *
 * Sends a #GstNtpPacket over a socket.
 *
 * MT safe.
 *
 * Returns: TRUE if successful, FALSE in case an error occurred.
 */
gboolean
gst_ntp_packet_send (const GstNtpPacket * packet,
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
  buffer = gst_ntp_packet_serialize (packet);

  res = g_socket_send_to (socket, dest_address, (const gchar *) buffer,
      GST_NTP_PACKET_SIZE, NULL, error);

  /* datagram packets should be sent as a whole or not at all */
  g_assert (res < 0 || res == GST_NTP_PACKET_SIZE);

  g_free (buffer);

  if (was_blocking)
    g_socket_set_blocking (socket, TRUE);

  return (res == GST_NTP_PACKET_SIZE);
}

GQuark
gst_ntp_error_quark (void)
{
  static GQuark quark;

  /* Thread-safe because GQuark is */
  if (!quark)
    quark = g_quark_from_static_string ("gst-ntp-error-quark");

  return quark;
}
