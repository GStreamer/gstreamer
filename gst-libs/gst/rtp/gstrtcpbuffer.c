/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
 *
 * gstrtcpbuffer.h: various helper functions to manipulate buffers
 *     with RTCP payload.
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
 * SECTION:gstrtcpbuffer
 * @short_description: Helper methods for dealing with RTCP buffers
 * @see_also: gstbasertppayload, gstbasertpdepayload
 *
 * Note: The API in this module is not yet declared stable.
 *
 * <refsect2>
 * <para>
 * The GstRTPCBuffer helper functions makes it easy to parse and create regular 
 * #GstBuffer objects that contain compound RTCP packets. These buffers are typically
 * of 'application/x-rtcp' #GstCaps.
 * </para>
 * <para>
 * An RTCP buffer consists of 1 or more #GstRTCPPacket structures that you can
 * retrieve with gst_rtcp_buffer_get_first_packet(). #GstRTCPPacket acts as a pointer
 * into the RTCP buffer; you can move to the next packet with
 * gst_rtcp_packet_move_to_next().
 * </para>
 * </refsect2>
 *
 * Since: 0.10.13
 *
 * Last reviewed on 2007-03-26 (0.10.13)
 */

#include "gstrtcpbuffer.h"

/**
 * gst_rtcp_buffer_new_take_data:
 * @data: data for the new buffer
 * @len: the length of data
 *
 * Create a new buffer and set the data and size of the buffer to @data and @len
 * respectively. @data will be freed when the buffer is unreffed, so this
 * function transfers ownership of @data to the new buffer.
 *
 * Returns: A newly allocated buffer with @data and of size @len.
 */
GstBuffer *
gst_rtcp_buffer_new_take_data (gpointer data, guint len)
{
  GstBuffer *result;

  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (len > 0, NULL);

  result = gst_buffer_new ();

  GST_BUFFER_MALLOCDATA (result) = data;
  GST_BUFFER_DATA (result) = data;
  GST_BUFFER_SIZE (result) = len;

  return result;
}

/**
 * gst_rtcp_buffer_new_copy_data:
 * @data: data for the new buffer
 * @len: the length of data
 *
 * Create a new buffer and set the data to a copy of @len
 * bytes of @data and the size to @len. The data will be freed when the buffer
 * is freed.
 *
 * Returns: A newly allocated buffer with a copy of @data and of size @len.
 */
GstBuffer *
gst_rtcp_buffer_new_copy_data (gpointer data, guint len)
{
  return gst_rtcp_buffer_new_take_data (g_memdup (data, len), len);
}

/**
 * gst_rtcp_buffer_validate_data:
 * @data: the data to validate
 * @len: the length of @data to validate
 *
 * Check if the @data and @size point to the data of a valid RTCP (compound)
 * packet. 
 * Use this function to validate a packet before using the other functions in
 * this module.
 *
 * Returns: TRUE if the data points to a valid RTCP packet.
 */
gboolean
gst_rtcp_buffer_validate_data (guint8 * data, guint len)
{
  guint16 header_mask;
  guint16 header_len;
  guint8 version;
  guint data_len;
  gboolean padding;
  guint8 pad_bytes;

  g_return_val_if_fail (data != NULL, FALSE);

  /* we need 4 bytes for the type and length */
  if (G_UNLIKELY (len < 4))
    goto wrong_length;

  /* first packet must be RR or SR  and version must be 2 */
  header_mask = ((data[0] << 8) | data[1]) & GST_RTCP_VALID_MASK;
  if (G_UNLIKELY (header_mask != GST_RTCP_VALID_VALUE))
    goto wrong_mask;

  /* no padding when mask succeeds */
  padding = FALSE;

  /* store len */
  data_len = len;

  while (TRUE) {
    /* get packet length */
    header_len = (((data[2] << 8) | data[3]) + 1) << 2;
    if (data_len < header_len)
      goto wrong_length;

    /* move to next compount packet */
    data += header_len;
    data_len -= header_len;

    /* we are at the end now */
    if (data_len < 4)
      break;

    /* check version of new packet */
    version = data[0] & 0xc0;
    if (version != (GST_RTCP_VERSION << 6))
      goto wrong_version;

    /* padding only allowed on last packet */
    if ((padding = data[0] & 0x20))
      break;
  }
  if (data_len > 0) {
    /* some leftover bytes, check padding */
    if (!padding)
      goto wrong_length;

    /* get padding */
    pad_bytes = data[len - 1];
    if (data_len != pad_bytes)
      goto wrong_padding;
  }
  return TRUE;

  /* ERRORS */
wrong_length:
  {
    GST_DEBUG ("len check failed");
    return FALSE;
  }
wrong_mask:
  {
    GST_DEBUG ("mask check failed (%04x != %04x)", header_mask,
        GST_RTCP_VALID_VALUE);
    return FALSE;
  }
wrong_version:
  {
    GST_DEBUG ("wrong version (%d < 2)", version >> 6);
    return FALSE;
  }
wrong_padding:
  {
    GST_DEBUG ("padding check failed");
    return FALSE;
  }
}

/**
 * gst_rtcp_buffer_validate:
 * @buffer: the buffer to validate
 *
 * Check if the data pointed to by @buffer is a valid RTCP packet using
 * gst_rtcp_buffer_validate_data().
 *  
 * Returns: TRUE if @buffer is a valid RTCP packet.
 */
gboolean
gst_rtcp_buffer_validate (GstBuffer * buffer)
{
  guint8 *data;
  guint len;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  data = GST_BUFFER_DATA (buffer);
  len = GST_BUFFER_SIZE (buffer);

  return gst_rtcp_buffer_validate_data (data, len);
}

/**
 * gst_rtcp_buffer_get_packet_count:
 * @buffer: a valid RTCP buffer
 *
 * Get the number of RTCP packets in @buffer.
 *
 * Returns: the number of RTCP packets in @buffer.
 */
guint
gst_rtcp_buffer_get_packet_count (GstBuffer * buffer)
{
  GstRTCPPacket packet;
  guint count;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  count = gst_rtcp_buffer_get_first_packet (buffer, &packet);
  while (gst_rtcp_packet_move_to_next (&packet))
    count++;

  return count;
}

/**
 * read_packet_header:
 * @packet: a packet
 *
 * Read the packet headers for the packet pointed to by @packet.
 *
 * Returns: TRUE if @packet pointed to a valid header.
 */
static gboolean
read_packet_header (GstRTCPPacket * packet)
{
  guint8 *data;
  guint size;
  guint offset;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), FALSE);

  data = GST_BUFFER_DATA (packet->buffer);
  size = GST_BUFFER_SIZE (packet->buffer);

  offset = packet->offset;

  /* check if we are at the end of the buffer, we add 4 because we also want to
   * ensure we can read the header. */
  if (offset + 4 > size)
    return FALSE;

  if ((data[offset] & 0xc0) != (GST_RTCP_VERSION << 6))
    return FALSE;

  /* read count, type and length */
  packet->padding = (data[offset] & 0x20) == 0x20;
  packet->count = data[offset] & 0x1f;
  packet->type = data[offset + 1];
  packet->length = (data[offset + 2] << 8) | data[offset + 3];
  packet->chunk_offset = 4;
  packet->item_offset = 4;

  return TRUE;
}

/**
 * gst_rtcp_buffer_get_first_packet:
 * @buffer: a valid RTCP buffer
 * @packet: a #GstRTCPPacket
 *
 * Initialize a new #GstRTCPPacket pointer that points to the first packet in
 * @buffer.
 *
 * Returns: TRUE if the packet existed in @buffer.
 */
gboolean
gst_rtcp_buffer_get_first_packet (GstBuffer * buffer, GstRTCPPacket * packet)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (packet != NULL, FALSE);

  /* init to 0 */
  packet->buffer = buffer;
  packet->offset = 0;
  packet->type = GST_RTCP_TYPE_INVALID;

  if (!read_packet_header (packet))
    return FALSE;

  return TRUE;
}

/**
 * gst_rtcp_packet_move_to_next:
 * @packet: a #GstRTCPPacket
 *
 * Move the packet pointer @packet to the next packet in the payload.
 * Use gst_rtcp_buffer_get_first_packet() to initialize @packet.
 *
 * Returns: TRUE if @packet is pointing to a valid packet after calling this
 * function.
 */
gboolean
gst_rtcp_packet_move_to_next (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type != GST_RTCP_TYPE_INVALID, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), FALSE);

  /* if we have a padding packet, it must be the last, set pointer to end of
   * buffer and return FALSE */
  if (packet->padding)
    goto end;

  /* move to next packet. Add 4 because the header is not included in length */
  packet->offset += (packet->length << 2) + 4;

  /* try to read new header */
  if (!read_packet_header (packet))
    goto end;

  return TRUE;

  /* ERRORS */
end:
  {
    packet->type = GST_RTCP_TYPE_INVALID;
    packet->offset = GST_BUFFER_SIZE (packet->buffer);
    return FALSE;
  }
}

/**
 * gst_rtcp_buffer_add_packet:
 * @buffer: a valid RTCP buffer
 * @type: the #GstRTCPType of the new packet
 * @packet: pointer to new packet
 *
 * Add a new packet of @type to @buffer. @packet will point to the newly created 
 * packet.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_buffer_add_packet (GstBuffer * buffer, GstRTCPType type,
    GstRTCPPacket * packet)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (type != GST_RTCP_TYPE_INVALID);
  g_return_if_fail (packet != NULL);

  g_warning ("not implemented");
}

/**
 * gst_rtcp_packet_remove:
 * @packet: a #GstRTCPPacket
 *
 * Removes the packet pointed to by @packet.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_packet_remove (GstRTCPPacket * packet)
{
  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type != GST_RTCP_TYPE_INVALID);

  g_warning ("not implemented");
}

/**
 * gst_rtcp_packet_get_padding:
 * @packet: a valid #GstRTCPPacket
 *
 * Get the packet padding of the packet pointed to by @packet.
 *
 * Returns: If the packet has the padding bit set.
 */
gboolean
gst_rtcp_packet_get_padding (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type != GST_RTCP_TYPE_INVALID, FALSE);

  return packet->padding;
}

/**
 * gst_rtcp_packet_get_type:
 * @packet: a valid #GstRTCPPacket
 *
 * Get the packet type of the packet pointed to by @packet.
 *
 * Returns: The packet type.
 */
GstRTCPType
gst_rtcp_packet_get_type (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, GST_RTCP_TYPE_INVALID);
  g_return_val_if_fail (packet->type != GST_RTCP_TYPE_INVALID,
      GST_RTCP_TYPE_INVALID);

  return packet->type;
}

/**
 * gst_rtcp_packet_get_count:
 * @packet: a valid #GstRTCPPacket
 *
 * Get the count field in @packet.
 *
 * Returns: The count field in @packet or -1 if @packet does not point to a
 * valid packet.
 */
guint8
gst_rtcp_packet_get_count (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, -1);
  g_return_val_if_fail (packet->type != GST_RTCP_TYPE_INVALID, -1);

  return packet->count;
}

/**
 * gst_rtcp_packet_get_length:
 * @packet: a valid #GstRTCPPacket
 *
 * Get the length field of @packet. This is the length of the packet in 
 * 32-bit words minus one.
 *
 * Returns: The length field of @packet.
 */
guint16
gst_rtcp_packet_get_length (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type != GST_RTCP_TYPE_INVALID, 0);

  return packet->length;
}

/**
 * gst_rtcp_packet_sr_get_sender_info:
 * @packet: a valid SR #GstRTCPPacket
 * @ssrc: result SSRC
 * @ntptime: result NTP time
 * @rtptime: result RTP time
 * @packet_count: result packet count
 * @octet_count: result octect count
 *
 * Parse the SR sender info and store the values.
 */
void
gst_rtcp_packet_sr_get_sender_info (GstRTCPPacket * packet, guint32 * ssrc,
    guint64 * ntptime, guint32 * rtptime, guint32 * packet_count,
    guint32 * octet_count)
{
  guint8 *data;

  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_SR);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  data = GST_BUFFER_DATA (packet->buffer);

  /* skip header */
  data += packet->offset + 4;
  if (ssrc)
    *ssrc = GST_READ_UINT32_BE (data);
  data += 4;
  if (ntptime)
    *ntptime = GST_READ_UINT64_BE (data);
  data += 8;
  if (rtptime)
    *rtptime = GST_READ_UINT32_BE (data);
  data += 4;
  if (packet_count)
    *packet_count = GST_READ_UINT32_BE (data);
  data += 4;
  if (octet_count)
    *octet_count = GST_READ_UINT32_BE (data);
}

/**
 * gst_rtcp_packet_sr_set_sender_info:
 * @packet: a valid SR #GstRTCPPacket
 * @ssrc: the SSRC 
 * @ntptime: the NTP time
 * @rtptime: the RTP time
 * @packet_count: the packet count
 * @octet_count: the octect count
 *
 * Set the given values in the SR packet @packet.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_packet_sr_set_sender_info (GstRTCPPacket * packet, guint32 ssrc,
    guint64 ntptime, guint32 rtptime, guint32 packet_count, guint32 octet_count)
{
  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_SR);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  g_warning ("not implemented");
}

/**
 * gst_rtcp_packet_rr_get_ssrc:
 * @packet: a valid RR #GstRTCPPacket
 *
 * Get the ssrc field of the RR @packet.
 *
 * Returns: the ssrc.
 */
guint32
gst_rtcp_packet_rr_get_ssrc (GstRTCPPacket * packet)
{
  guint8 *data;
  guint32 ssrc;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_RR, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  data = GST_BUFFER_DATA (packet->buffer);

  /* skip header */
  data += packet->offset + 4;
  ssrc = GST_READ_UINT32_BE (data);

  return ssrc;
}

/**
 * gst_rtcp_packet_rr_set_ssrc:
 * @packet: a valid RR #GstRTCPPacket
 * @ssrc: the SSRC to set
 *
 * Set the ssrc field of the RR @packet.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_packet_rr_set_ssrc (GstRTCPPacket * packet, guint32 ssrc)
{
  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RR);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  g_warning ("not implemented");
}

/**
 * gst_rtcp_packet_get_rb_count:
 * @packet: a valid SR or RR #GstRTCPPacket
 *
 * Get the number of report blocks in @packet.
 *
 * Returns: The number of report blocks in @packet.
 */
guint
gst_rtcp_packet_get_rb_count (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_RR ||
      packet->type == GST_RTCP_TYPE_SR, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  return packet->count;
}

/**
 * gst_rtcp_packet_get_rb:
 * @packet: a valid SR or RR #GstRTCPPacket
 * @nth: the nth report block in @packet
 * @ssrc: result for data source being reported
 * @fractionlost: result for fraction lost since last SR/RR
 * @packetslost: result for the cumululative number of packets lost
 * @exthighestseq: result for the extended last sequence number received
 * @jitter: result for the interarrival jitter
 * @lsr: result for the last SR packet from this source
 * @dlsr: result for the delay since last SR packet
 *
 * Parse the values of the @nth report block in @packet and store the result in
 * the values.
 */
void
gst_rtcp_packet_get_rb (GstRTCPPacket * packet, guint nth, guint32 * ssrc,
    guint8 * fractionlost, gint32 * packetslost, guint32 * exthighestseq,
    guint32 * jitter, guint32 * lsr, guint32 * dlsr)
{
  guint8 *data;
  guint32 tmp;

  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RR ||
      packet->type == GST_RTCP_TYPE_SR);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  data = GST_BUFFER_DATA (packet->buffer);

  /* skip header */
  data += packet->offset + 4;
  if (packet->type == GST_RTCP_TYPE_RR)
    data += 4;
  else
    data += 36;

  /* move to requested index */
  data += (nth * 36);

  if (ssrc)
    *ssrc = GST_READ_UINT32_BE (data);
  data += 4;
  tmp = GST_READ_UINT32_BE (data);
  data += 4;
  if (fractionlost)
    *fractionlost = (tmp >> 24);
  if (packetslost) {
    /* sign extend */
    if (tmp & 0x00800000)
      tmp |= 0xff000000;
    else
      tmp &= 0x00ffffff;
    *packetslost = (gint32) tmp;
  }
  data += 4;
  if (exthighestseq)
    *exthighestseq = GST_READ_UINT32_BE (data);
  data += 4;
  if (jitter)
    *jitter = GST_READ_UINT32_BE (data);
  data += 4;
  if (lsr)
    *lsr = GST_READ_UINT32_BE (data);
  data += 4;
  if (dlsr)
    *dlsr = GST_READ_UINT32_BE (data);
}

/**
 * gst_rtcp_packet_add_rb:
 * @packet: a valid SR or RR #GstRTCPPacket
 * @ssrc: data source being reported
 * @fractionlost: fraction lost since last SR/RR
 * @packetslost: the cumululative number of packets lost
 * @exthighestseq: the extended last sequence number received
 * @jitter: the interarrival jitter
 * @lsr: the last SR packet from this source
 * @dlsr: the delay since last SR packet
 *
 * Add a new report block to @packet with the given values.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_packet_add_rb (GstRTCPPacket * packet, guint32 ssrc,
    guint8 fractionlost, gint32 packetslost, guint32 exthighestseq,
    guint32 jitter, guint32 lsr, guint32 dlsr)
{
  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RR ||
      packet->type == GST_RTCP_TYPE_SR);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  g_warning ("not implemented");
}

/**
 * gst_rtcp_packet_set_rb:
 * @packet: a valid SR or RR #GstRTCPPacket
 * @nth: the nth report block to set
 * @ssrc: data source being reported
 * @fractionlost: fraction lost since last SR/RR
 * @packetslost: the cumululative number of packets lost
 * @exthighestseq: the extended last sequence number received
 * @jitter: the interarrival jitter
 * @lsr: the last SR packet from this source
 * @dlsr: the delay since last SR packet
 *
 * Set the @nth new report block in @packet with the given values.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_packet_set_rb (GstRTCPPacket * packet, guint nth, guint32 ssrc,
    guint8 fractionlost, gint32 packetslost, guint32 exthighestseq,
    guint32 jitter, guint32 lsr, guint32 dlsr)
{
  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RR ||
      packet->type == GST_RTCP_TYPE_SR);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  g_warning ("not implemented");
}


/**
 * gst_rtcp_packet_sdes_get_chunk_count:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Get the number of chunks in the SDES packet @packet.
 *
 * Returns: The number of chunks in @packet.
 */
guint
gst_rtcp_packet_sdes_get_chunk_count (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  return packet->count;
}

/**
 * gst_rtcp_packet_sdes_first_chunk:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the first SDES chunk in @packet.
 *
 * Returns: TRUE if there was a first chunk.
 */
gboolean
gst_rtcp_packet_sdes_first_chunk (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  if (packet->count == 0)
    return FALSE;

  packet->chunk_offset = 4;
  packet->item_offset = 4;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_next_chunk:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the next SDES chunk in @packet.
 *
 * Returns: TRUE if there was a next chunk.
 */
gboolean
gst_rtcp_packet_sdes_next_chunk (GstRTCPPacket * packet)
{
  guint8 *data;
  guint offset;
  guint len;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  /* move to SDES */
  data = GST_BUFFER_DATA (packet->buffer);
  data += packet->offset;
  /* move to chunk */
  offset = packet->chunk_offset;
  /* skip SSRC */
  offset += 4;

  /* don't overrun */
  len = (packet->length << 2);

  while (offset < len) {
    if (data[offset] == 0) {
      /* end of list, round to next 32-bit word */
      offset = (offset + 3) & ~3;
      break;
    }
    offset += data[offset + 1] + 2;
  }
  if (offset >= len)
    return FALSE;

  packet->chunk_offset = offset;
  packet->item_offset = 4;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_get_ssrc:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Get the SSRC of the current SDES chunk.
 *
 * Returns: the SSRC of the current chunk.
 */
guint32
gst_rtcp_packet_sdes_get_ssrc (GstRTCPPacket * packet)
{
  guint32 ssrc;
  guint8 *data;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  /* move to SDES */
  data = GST_BUFFER_DATA (packet->buffer);
  data += packet->offset;
  /* move to chunk */
  data += packet->chunk_offset;

  ssrc = GST_READ_UINT32_BE (data);

  return ssrc;
}

/**
 * gst_rtcp_packet_sdes_first_item:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the first SDES item in the current chunk.
 *
 * Returns: TRUE if there was a first item.
 */
gboolean
gst_rtcp_packet_sdes_first_item (GstRTCPPacket * packet)
{
  guint8 *data;
  guint len, offset;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  /* move to SDES */
  data = GST_BUFFER_DATA (packet->buffer);
  data += packet->offset;
  /* move to chunk */
  offset = packet->chunk_offset;
  /* skip SSRC */
  offset += 4;

  /* don't overrun */
  len = (packet->length << 2);
  if (offset >= len)
    return FALSE;

  if (data[offset] == 0)
    return FALSE;

  packet->item_offset = 4;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_next_item:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the next SDES item in the current chunk.
 *
 * Returns: TRUE if there was a next item.
 */
gboolean
gst_rtcp_packet_sdes_next_item (GstRTCPPacket * packet)
{
  guint8 *data;
  guint len, offset, item_len;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  /* move to SDES */
  data = GST_BUFFER_DATA (packet->buffer);
  data += packet->offset;
  /* move to chunk */
  offset = packet->chunk_offset;
  /* move to item */
  offset += packet->item_offset;

  item_len = data[offset + 1] + 2;
  /* skip item */
  offset += item_len;

  /* don't overrun */
  len = (packet->length << 2);
  if (offset >= len)
    return FALSE;

  /* check for end of list */
  if (data[offset] == 0)
    return FALSE;

  packet->item_offset += item_len;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_get_item:
 * @packet: a valid SDES #GstRTCPPacket
 * @type: result of the item type
 * @len: result length of the item data
 * @data: result item data
 *
 * Get the data of the current SDES chunk item.
 *
 * Returns: TRUE if there was valid data.
 */
gboolean
gst_rtcp_packet_sdes_get_item (GstRTCPPacket * packet,
    GstRTCPSDESType * type, guint8 * len, gchar ** data)
{
  guint8 *bdata;
  guint offset;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  /* move to SDES */
  bdata = GST_BUFFER_DATA (packet->buffer);
  bdata += packet->offset;
  /* move to chunk */
  offset = packet->chunk_offset;
  /* move to item */
  offset += packet->item_offset;

  if (bdata[offset] == 0)
    return FALSE;

  if (type)
    *type = bdata[offset];
  if (len)
    *len = bdata[offset + 1];
  if (data)
    *data = g_strndup ((const gchar *) &bdata[offset + 2], bdata[offset + 1]);

  return TRUE;
}

/**
 * gst_rtcp_packet_bye_get_ssrc_count:
 * @packet: a valid BYE #GstRTCPPacket
 *
 * Get the number of SSRC fields in @packet.
 *
 * Returns: The number of SSRC fields in @packet.
 */
guint
gst_rtcp_packet_bye_get_ssrc_count (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, -1);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, -1);

  return packet->count;
}

/**
 * gst_rtcp_packet_bye_get_nth_ssrc:
 * @packet: a valid BYE #GstRTCPPacket
 * @nth: the nth SSRC to get
 *
 * Get the @nth SSRC of the BYE @packet.
 *
 * Returns: The @nth SSRC of @packet.
 */
guint32
gst_rtcp_packet_bye_get_nth_ssrc (GstRTCPPacket * packet, guint nth)
{
  guint8 *data;
  guint offset;
  guint32 ssrc;
  guint8 sc;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  /* get amount of sources and check that we don't read too much */
  sc = packet->count;
  if (nth >= sc)
    return 0;

  /* get offset in 32-bits words into packet, skip the header */
  offset = 1 + nth;
  /* check that we don't go past the packet length */
  if (offset > packet->length)
    return 0;

  /* scale to bytes */
  offset <<= 2;
  offset += packet->offset;

  /* check if the packet is valid */
  if (offset + 4 > GST_BUFFER_SIZE (packet->buffer))
    return 0;

  data = GST_BUFFER_DATA (packet->buffer);
  data += offset;

  ssrc = GST_READ_UINT32_BE (data);

  return ssrc;
}

/**
 * gst_rtcp_packet_bye_add_ssrc:
 * @packet: a valid BYE #GstRTCPPacket
 * @ssrc: an SSRC to add
 *
 * Add @ssrc to the BYE @packet.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_packet_bye_add_ssrc (GstRTCPPacket * packet, guint32 ssrc)
{
  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_BYE);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  g_warning ("not implemented");
}

/**
 * gst_rtcp_packet_bye_add_ssrcs:
 * @packet: a valid BYE #GstRTCPPacket
 * @ssrc: an array of SSRCs to add
 * @len: number of elements in @ssrc
 *
 * Adds @len SSRCs in @ssrc to BYE @packet.
 *
 * Note: Not implemented.
 */
void
gst_rtcp_packet_bye_add_ssrcs (GstRTCPPacket * packet, guint32 * ssrc,
    guint len)
{
  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_BYE);
  g_return_if_fail (GST_IS_BUFFER (packet->buffer));

  g_warning ("not implemented");
}

/* get the offset in packet of the reason length */
static guint
get_reason_offset (GstRTCPPacket * packet)
{
  guint sc;
  guint offset;

  /* get amount of sources */
  sc = packet->count;

  offset = 1 + sc;
  /* check that we don't go past the packet length */
  if (offset > packet->length)
    return 0;

  /* scale to bytes */
  offset <<= 2;
  offset += packet->offset;

  /* check if the packet is valid */
  if (offset + 1 > GST_BUFFER_SIZE (packet->buffer))
    return 0;

  return offset;
}

/**
 * gst_rtcp_packet_bye_get_reason_len:
 * @packet: a valid BYE #GstRTCPPacket
 *
 * Get the length of the reason string.
 *
 * Returns: The length of the reason string or 0 when there is no reason string
 * present.
 */
guint8
gst_rtcp_packet_bye_get_reason_len (GstRTCPPacket * packet)
{
  guint8 *data;
  guint roffset;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), 0);

  roffset = get_reason_offset (packet);
  if (roffset == 0)
    return 0;

  data = GST_BUFFER_DATA (packet->buffer);

  return data[roffset];
}

/**
 * gst_rtcp_packet_bye_get_reason:
 * @packet: a valid BYE #GstRTCPPacket
 *
 * Get the reason in @packet.
 *
 * Returns: The reason for the BYE @packet or NULL if the packet did not contain
 * a reason string. The string must be freed with g_free() after usage.
 */
gchar *
gst_rtcp_packet_bye_get_reason (GstRTCPPacket * packet)
{
  guint8 *data;
  guint roffset;
  guint8 len;

  g_return_val_if_fail (packet != NULL, NULL);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, 0);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), NULL);

  roffset = get_reason_offset (packet);
  if (roffset == 0)
    return NULL;

  data = GST_BUFFER_DATA (packet->buffer);

  /* get length of reason string */
  len = data[roffset];
  if (len == 0)
    return NULL;

  /* move to string */
  roffset += 1;

  /* check if enough data to copy */
  if (roffset + len > GST_BUFFER_SIZE (packet->buffer))
    return NULL;

  return g_strndup ((gconstpointer) (data + roffset), len);
}

/**
 * gst_rtcp_packet_bye_set_reason:
 * @packet: a valid BYE #GstRTCPPacket
 * @reason: a reason string
 *
 * Set the reason string to @reason in @packet.
 *
 * Returns: TRUE if the string could be set.
 *
 * Note: Not implemented.
 */
gboolean
gst_rtcp_packet_bye_set_reason (GstRTCPPacket * packet, const gchar * reason)
{
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (packet->buffer), FALSE);

  g_warning ("not implemented");

  return FALSE;
}
