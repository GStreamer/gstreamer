/* GStreamer
 * Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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


#ifndef __GST_NTP_PACKET_H__
#define __GST_NTP_PACKET_H__

#include <gst/gst.h>
#include <gio/gio.h>

G_BEGIN_DECLS

/**
 * GST_NTP_PACKET_SIZE:
 *
 * The size of the packets sent between NTP clocks.
 */
#define GST_NTP_PACKET_SIZE 48

typedef struct _GstNtpPacket GstNtpPacket;

/**
 * GstNtpPacket:
 * @origin_time: the time the client packet was sent for the server
 * @receive_time: the time the client packet was received
 * @transmit_time: the time the packet was sent
 * @poll_interval: maximum poll interval
 *
 * Content of a #GstNtpPacket.
 */
struct _GstNtpPacket {
  GstClockTime origin_time;
  GstClockTime receive_time;
  GstClockTime transmit_time;

  GstClockTime poll_interval;
};

GType gst_ntp_packet_get_type(void) G_GNUC_INTERNAL;

enum {
  GST_NTP_ERROR_WRONG_VERSION,
  GST_NTP_ERROR_KOD_DENY,
  GST_NTP_ERROR_KOD_RATE,
  GST_NTP_ERROR_KOD_UNKNOWN
};

GQuark gst_ntp_error_quark (void) G_GNUC_INTERNAL;
#define GST_NTP_ERROR (gst_ntp_error_quark ())

GstNtpPacket*           gst_ntp_packet_new         (const guint8 *buffer,
                                                    GError      ** error) G_GNUC_INTERNAL;
GstNtpPacket*           gst_ntp_packet_copy        (const GstNtpPacket *packet) G_GNUC_INTERNAL;
void                    gst_ntp_packet_free        (GstNtpPacket *packet) G_GNUC_INTERNAL;

guint8*                 gst_ntp_packet_serialize   (const GstNtpPacket *packet) G_GNUC_INTERNAL;

GstNtpPacket*           gst_ntp_packet_receive     (GSocket         * socket,
                                                    GSocketAddress ** src_address,
                                                    GError         ** error) G_GNUC_INTERNAL;

gboolean                gst_ntp_packet_send        (const GstNtpPacket * packet,
                                                    GSocket            * socket,
                                                    GSocketAddress     * dest_address,
                                                    GError            ** error) G_GNUC_INTERNAL;

G_END_DECLS

#endif /* __GST_NET_TIME_PACKET_H__ */
