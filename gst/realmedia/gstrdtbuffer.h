/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
 *
 * gstrdtbuffer.h: various helper functions to manipulate buffers
 *     with RDT payload.
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

#ifndef __GST_RDTBUFFER_H__
#define __GST_RDTBUFFER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/** 
 * GstRDTType:
 * @GST_RDT_TYPE_INVALID:
 * @GST_RDT_TYPE_ASMACTION:
 * @GST_RDT_TYPE_ACK:
 * @GST_RDT_TYPE_RTTREQ:
 * @GST_RDT_TYPE_RTTRESP:
 * @GST_RDT_TYPE_CONGESTION:
 * @GST_RDT_TYPE_STREAMEND:
 * @GST_RDT_TYPE_LATENCY:
 * @GST_RDT_TYPE_INFOREQ:
 * @GST_RDT_TYPE_INFORESP:
 * @GST_RDT_TYPE_AUTOBW:
 *
 * Different RDT packet types.
 */
typedef enum
{
  GST_RDT_TYPE_INVALID     = 0xffff,
  GST_RDT_TYPE_ASMACTION   = 0xff00,
  GST_RDT_TYPE_BWREPORT    = 0xff01,
  GST_RDT_TYPE_ACK         = 0xff02,
  GST_RDT_TYPE_RTTREQ      = 0xff03,
  GST_RDT_TYPE_RTTRESP     = 0xff04,
  GST_RDT_TYPE_CONGESTION  = 0xff05,
  GST_RDT_TYPE_STREAMEND   = 0xff06,
  GST_RDT_TYPE_REPORT      = 0xff07,
  GST_RDT_TYPE_LATENCY     = 0xff08,
  GST_RDT_TYPE_INFOREQ     = 0xff09,
  GST_RDT_TYPE_INFORESP    = 0xff0a,
  GST_RDT_TYPE_AUTOBW      = 0xff0b
} GstRDTType;

/**
 * GST_RDT_IS_DATA_TYPE:
 * @t: the #GstRDTType to check
 * 
 * Check if @t is a data packet type.
 */
#define GST_RDT_IS_DATA_TYPE(t) ((t) < 0xff00)

typedef struct _GstRDTPacket GstRDTPacket;

/**
 * GstRDTPacket:
 * @buffer: pointer to RDT buffer
 * @offset: offset of packet in buffer data
 *
 * Data structure that points to a packet at @offset in @buffer. 
 * The size of the structure is made public to allow stack allocations.
 */
struct _GstRDTPacket
{ 
  GstBuffer   *buffer;
  guint        offset;
  
  /*< private >*/
  GstRDTType   type;         /* type of current packet */
  guint16      length;       /* length of current packet in bytes */
  GstMapInfo   map;          /* last mapped data */
};

/* validate buffers */
gboolean        gst_rdt_buffer_validate_data      (guint8 *data, guint len);
gboolean        gst_rdt_buffer_validate           (GstBuffer *buffer);

/* retrieving packets */
guint           gst_rdt_buffer_get_packet_count   (GstBuffer *buffer);
gboolean        gst_rdt_buffer_get_first_packet   (GstBuffer *buffer, GstRDTPacket *packet);
gboolean        gst_rdt_packet_move_to_next       (GstRDTPacket *packet);

/* working with packets */
GstRDTType      gst_rdt_packet_get_type           (GstRDTPacket *packet);
guint16         gst_rdt_packet_get_length         (GstRDTPacket *packet);
GstBuffer*      gst_rdt_packet_to_buffer          (GstRDTPacket *packet);


/* data packets */
guint16         gst_rdt_packet_data_get_seq       (GstRDTPacket *packet);
guint8 *        gst_rdt_packet_data_map           (GstRDTPacket *packet, guint *size);
gboolean        gst_rdt_packet_data_unmap         (GstRDTPacket *packet);
guint16         gst_rdt_packet_data_get_stream_id (GstRDTPacket *packet);
guint32         gst_rdt_packet_data_get_timestamp (GstRDTPacket *packet);

guint8          gst_rdt_packet_data_get_flags     (GstRDTPacket * packet);

/* utils */
gint            gst_rdt_buffer_compare_seqnum     (guint16 seqnum1, guint16 seqnum2);

G_END_DECLS

#endif /* __GST_RDTBUFFER_H__ */

