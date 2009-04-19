/* GStreamer
 * Copyright (C) <2008> Wim Taymans <wim.taymans@gmail.com>
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

#include "gstrdtbuffer.h"

gboolean
gst_rdt_buffer_validate_data (guint8 * data, guint len)
{
  return TRUE;
}

gboolean
gst_rdt_buffer_validate (GstBuffer * buffer)
{
  return TRUE;
}

guint
gst_rdt_buffer_get_packet_count (GstBuffer * buffer)
{
  GstRDTPacket packet;
  guint count;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  count = 0;
  if (gst_rdt_buffer_get_first_packet (buffer, &packet)) {
    do {
      count++;
    } while (gst_rdt_packet_move_to_next (&packet));
  }
  return count;
}

static gboolean
read_packet_header (GstRDTPacket * packet)
{
  guint8 *data;
  guint size;
  guint offset;
  guint length;
  guint length_offset;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), FALSE);

  data = GST_BUFFER_DATA (packet->buffer);
  size = GST_BUFFER_SIZE (packet->buffer);

  offset = packet->offset;

  /* check if we are at the end of the buffer, we add 3 because we also want to
   * ensure we can read the type, which is always at offset 1 and 2 bytes long. */
  if (offset + 3 > size)
    return FALSE;

  /* read type */
  packet->type = GST_READ_UINT16_BE (&data[offset + 1]);

  length = -1;
  length_offset = -1;

  /* figure out the length of the packet, this depends on the type */
  if (GST_RDT_IS_DATA_TYPE (packet->type)) {
    if (data[offset] & 0x80)
      /* length is present */
      length_offset = 3;
  } else {
    switch (packet->type) {
      case GST_RDT_TYPE_ASMACTION:
        if (data[offset] & 0x80)
          length_offset = 5;
        break;
      case GST_RDT_TYPE_BWREPORT:
        if (data[offset] & 0x80)
          length_offset = 3;
        break;
      case GST_RDT_TYPE_ACK:
        if (data[offset] & 0x80)
          length_offset = 3;
        break;
      case GST_RDT_TYPE_RTTREQ:
        length = 3;
        break;
      case GST_RDT_TYPE_RTTRESP:
        length = 11;
        break;
      case GST_RDT_TYPE_CONGESTION:
        length = 11;
        break;
      case GST_RDT_TYPE_STREAMEND:
        length = 9;
        /* total_reliable */
        if (data[offset] & 0x80)
          length += 2;
        /* stream_id_expansion */
        if ((data[offset] & 0x7c) == 0x7c)
          length += 2;
        /* ext_flag, FIXME, get string length */
        if ((data[offset] & 0x1) == 0x1)
          length += 7;
        break;
      case GST_RDT_TYPE_REPORT:
        if (data[offset] & 0x80)
          length_offset = 3;
        break;
      case GST_RDT_TYPE_LATENCY:
        if (data[offset] & 0x80)
          length_offset = 3;
        break;
      case GST_RDT_TYPE_INFOREQ:
        length = 3;
        /* request_time_ms */
        if (data[offset] & 0x2)
          length += 2;
        break;
      case GST_RDT_TYPE_INFORESP:
        length = 3;
        /* has_rtt_info */
        if (data[offset] & 0x4) {
          length += 4;
          /* is_delayed */
          if (data[offset] & 0x2) {
            length += 4;
          }
        }
        if (data[offset] & 0x1) {
          /* buffer_info_count, FIXME read and skip */
          length += 2;
        }
        break;
      case GST_RDT_TYPE_AUTOBW:
        if (data[offset] & 0x80)
          length_offset = 3;
        break;
      case GST_RDT_TYPE_INVALID:
      default:
        goto unknown_packet;
    }
  }

  if (length != -1) {
    /* we have a fixed length */
    packet->length = length;
  } else if (length_offset != -1) {
    /* we can read the length from an offset */
    packet->length = GST_READ_UINT16_BE (&data[length_offset]);
  } else {
    /* length is remainder of packet */
    packet->length = size - offset;
  }

  /* the length should be smaller than the remaining size */
  if (packet->length + offset > size)
    goto invalid_length;

  return TRUE;

  /* ERRORS */
unknown_packet:
  {
    packet->type = GST_RDT_TYPE_INVALID;
    return FALSE;
  }
invalid_length:
  {
    packet->type = GST_RDT_TYPE_INVALID;
    packet->length = 0;
    return FALSE;
  }
}

gboolean
gst_rdt_buffer_get_first_packet (GstBuffer * buffer, GstRDTPacket * packet)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (packet != NULL, FALSE);

  /* init to 0 */
  packet->buffer = buffer;
  packet->offset = 0;
  packet->type = GST_RDT_TYPE_INVALID;

  if (!read_packet_header (packet))
    return FALSE;

  return TRUE;
}

gboolean
gst_rdt_packet_move_to_next (GstRDTPacket * packet)
{
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type != GST_RDT_TYPE_INVALID, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), FALSE);

  /* if we have an invalid packet, it must be the last, 
   * return FALSE */
  if (packet->type == GST_RDT_TYPE_INVALID)
    goto end;

  /* move to next packet */
  packet->offset += packet->length;

  /* try to read new header */
  if (!read_packet_header (packet))
    goto end;

  return TRUE;

  /* ERRORS */
end:
  {
    packet->type = GST_RDT_TYPE_INVALID;
    return FALSE;
  }
}

GstRDTType
gst_rdt_packet_get_type (GstRDTPacket * packet)
{
  g_return_val_if_fail (packet != NULL, GST_RDT_TYPE_INVALID);
  g_return_val_if_fail (packet->type != GST_RDT_TYPE_INVALID,
      GST_RDT_TYPE_INVALID);

  return packet->type;
}

guint16
gst_rdt_packet_get_length (GstRDTPacket * packet)
{
  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type != GST_RDT_TYPE_INVALID, 0);

  return packet->length;
}

GstBuffer *
gst_rdt_packet_to_buffer (GstRDTPacket * packet)
{
  GstBuffer *result;

  g_return_val_if_fail (packet != NULL, NULL);
  g_return_val_if_fail (packet->type != GST_RDT_TYPE_INVALID, NULL);

  result =
      gst_buffer_create_sub (packet->buffer, packet->offset, packet->length);
  /* timestamp applies to all packets in this buffer */
  GST_BUFFER_TIMESTAMP (result) = GST_BUFFER_TIMESTAMP (packet->buffer);

  return result;
}

gint
gst_rdt_buffer_compare_seqnum (guint16 seqnum1, guint16 seqnum2)
{
  return (gint16) (seqnum2 - seqnum1);
}

guint16
gst_rdt_packet_data_get_seq (GstRDTPacket * packet)
{
  guint header;
  guint8 *bufdata;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (GST_RDT_IS_DATA_TYPE (packet->type), FALSE);

  bufdata = GST_BUFFER_DATA (packet->buffer);

  /* skip header bits */
  header = packet->offset + 1;

  /* read seq_no */
  return GST_READ_UINT16_BE (&bufdata[header]);
}

gboolean
gst_rdt_packet_data_peek_data (GstRDTPacket * packet, guint8 ** data,
    guint * size)
{
  guint header;
  guint8 *bufdata;
  gboolean length_included_flag;
  gboolean need_reliable_flag;
  guint8 stream_id;
  guint8 asm_rule_number;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (GST_RDT_IS_DATA_TYPE (packet->type), FALSE);

  bufdata = GST_BUFFER_DATA (packet->buffer);

  header = packet->offset;

  length_included_flag = (bufdata[header] & 0x80) == 0x80;
  need_reliable_flag = (bufdata[header] & 0x40) == 0x40;
  stream_id = (bufdata[header] & 0x3e) >> 1;

  /* skip seq_no and header bits */
  header += 3;

  if (length_included_flag) {
    /* skip length */
    header += 2;
  }
  asm_rule_number = (bufdata[header] & 0x3f);

  /* skip timestamp and asm_rule_number */
  header += 5;

  if (stream_id == 0x1f) {
    /* skip stream_id_expansion */
    header += 2;
  }
  if (need_reliable_flag) {
    /* skip total_reliable */
    header += 2;
  }
  if (asm_rule_number == 63) {
    /* skip asm_rule_number_expansion */
    header += 2;
  }

  if (data)
    *data = &bufdata[header];
  if (size)
    *size = packet->length - (header - packet->offset);

  return TRUE;
}

guint16
gst_rdt_packet_data_get_stream_id (GstRDTPacket * packet)
{
  guint16 result;
  guint header;
  gboolean length_included_flag;
  guint8 *bufdata;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (GST_RDT_IS_DATA_TYPE (packet->type), 0);

  bufdata = GST_BUFFER_DATA (packet->buffer);

  header = packet->offset;

  length_included_flag = (bufdata[header] & 0x80) == 0x80;
  result = (bufdata[header] & 0x3e) >> 1;
  if (result == 31) {
    /* skip seq_no and header bits */
    header += 3;

    if (length_included_flag) {
      /* skip length */
      header += 2;
    }
    /* skip asm_rule_number and timestamp */
    header += 5;

    /* stream_id_expansion */
    result = GST_READ_UINT16_BE (&bufdata[header]);
  }
  return result;
}

guint32
gst_rdt_packet_data_get_timestamp (GstRDTPacket * packet)
{
  guint header;
  gboolean length_included_flag;
  guint8 *bufdata;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (GST_RDT_IS_DATA_TYPE (packet->type), 0);

  bufdata = GST_BUFFER_DATA (packet->buffer);

  header = packet->offset;

  length_included_flag = (bufdata[header] & 0x80) == 0x80;

  /* skip seq_no and header bits */
  header += 3;

  if (length_included_flag) {
    /* skip length */
    header += 2;
  }
  /* skip asm_rule_number */
  header += 1;

  /* get timestamp */
  return GST_READ_UINT32_BE (&bufdata[header]);
}

guint8
gst_rdt_packet_data_get_flags (GstRDTPacket * packet)
{
  guint header;
  gboolean length_included_flag;
  guint8 *bufdata;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (GST_RDT_IS_DATA_TYPE (packet->type), 0);

  bufdata = GST_BUFFER_DATA (packet->buffer);

  header = packet->offset;

  length_included_flag = (bufdata[header] & 0x80) == 0x80;

  /* skip seq_no and header bits */
  header += 3;

  if (length_included_flag) {
    /* skip length */
    header += 2;
  }
  /* get flags */
  return bufdata[header];
}
