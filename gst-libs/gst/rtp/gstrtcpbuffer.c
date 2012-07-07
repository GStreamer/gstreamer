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
 * @see_also: #GstRTPBasePayload, #GstRTPBaseDepayload, #gstrtpbuffer
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
 * Last reviewed on 2007-03-26 (0.10.13)
 */

#include <string.h>

#include "gstrtcpbuffer.h"

/**
 * gst_rtcp_buffer_new_take_data:
 * @data: (array length=len) (element-type guint8): data for the new buffer
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

  result = gst_buffer_new_wrapped (data, len);

  return result;
}

/**
 * gst_rtcp_buffer_new_copy_data:
 * @data: (array length=len) (element-type guint8): data for the new buffer
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
 * @data: (array length=len): the data to validate
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
  guint header_len;
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
    pad_bytes = data[data_len - 1];
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
  gboolean res;
  GstMapInfo map;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  res = gst_rtcp_buffer_validate_data (map.data, map.size);
  gst_buffer_unmap (buffer, &map);

  return res;
}

/**
 * gst_rtcp_buffer_new:
 * @mtu: the maximum mtu size.
 *
 * Create a new buffer for constructing RTCP packets. The packet will have a
 * maximum size of @mtu.
 *
 * Returns: A newly allocated buffer.
 */
GstBuffer *
gst_rtcp_buffer_new (guint mtu)
{
  GstBuffer *result;
  guint8 *data;

  g_return_val_if_fail (mtu > 0, NULL);

  data = g_malloc0 (mtu);

  result = gst_buffer_new_wrapped_full (0, data, mtu, 0, 0, data, g_free);

  return result;
}

/**
 * gst_rtcp_buffer_map:
 * @buffer: a buffer with an RTCP packet
 * @flags: flags for the mapping
 * @rtcp: resulting #GstRTCPBuffer
 *
 * Open @buffer for reading or writing, depending on @flags. The resulting RTCP
 * buffer state is stored in @rtcp.
 */
gboolean
gst_rtcp_buffer_map (GstBuffer * buffer, GstMapFlags flags,
    GstRTCPBuffer * rtcp)
{
  g_return_val_if_fail (rtcp != NULL, FALSE);
  g_return_val_if_fail (rtcp->buffer == NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (flags & GST_MAP_READ, FALSE);

  rtcp->buffer = buffer;
  gst_buffer_map (buffer, &rtcp->map, flags);

  return TRUE;
}

/**
 * gst_rtcp_buffer_unmap:
 * @rtcp: a buffer with an RTCP packet
 *
 * Finish @rtcp after being constructured. This function is usually called
 * after gst_rtcp_buffer_map() and after adding the RTCP items to the new buffer.
 *
 * The function adjusts the size of @rtcp with the total length of all the
 * added packets.
 */
gboolean
gst_rtcp_buffer_unmap (GstRTCPBuffer * rtcp)
{
  g_return_val_if_fail (rtcp != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (rtcp->buffer), FALSE);

  if (rtcp->map.flags & GST_MAP_WRITE) {
    /* shrink size */
    gst_buffer_resize (rtcp->buffer, 0, rtcp->map.size);
  }

  gst_buffer_unmap (rtcp->buffer, &rtcp->map);
  rtcp->buffer = NULL;

  return TRUE;
}

/**
 * gst_rtcp_buffer_get_packet_count:
 * @rtcp: a valid RTCP buffer
 *
 * Get the number of RTCP packets in @rtcp.
 *
 * Returns: the number of RTCP packets in @rtcp.
 */
guint
gst_rtcp_buffer_get_packet_count (GstRTCPBuffer * rtcp)
{
  GstRTCPPacket packet;
  guint count;

  g_return_val_if_fail (rtcp != NULL, 0);
  g_return_val_if_fail (GST_IS_BUFFER (rtcp->buffer), 0);
  g_return_val_if_fail (rtcp != NULL, 0);
  g_return_val_if_fail (rtcp->map.flags & GST_MAP_READ, 0);

  count = 0;
  if (gst_rtcp_buffer_get_first_packet (rtcp, &packet)) {
    do {
      count++;
    } while (gst_rtcp_packet_move_to_next (&packet));
  }

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
  gsize maxsize;
  guint offset;

  g_return_val_if_fail (packet != NULL, FALSE);

  data = packet->rtcp->map.data;
  maxsize = packet->rtcp->map.size;

  offset = packet->offset;

  /* check if we are at the end of the buffer, we add 4 because we also want to
   * ensure we can read the header. */
  if (offset + 4 > maxsize)
    return FALSE;

  if ((data[offset] & 0xc0) != (GST_RTCP_VERSION << 6))
    return FALSE;

  /* read count, type and length */
  packet->padding = (data[offset] & 0x20) == 0x20;
  packet->count = data[offset] & 0x1f;
  packet->type = data[offset + 1];
  packet->length = (data[offset + 2] << 8) | data[offset + 3];
  packet->item_offset = 4;
  packet->item_count = 0;
  packet->entry_offset = 4;

  return TRUE;
}

/**
 * gst_rtcp_buffer_get_first_packet:
 * @rtcp: a valid RTCP buffer
 * @packet: a #GstRTCPPacket
 *
 * Initialize a new #GstRTCPPacket pointer that points to the first packet in
 * @rtcp.
 *
 * Returns: TRUE if the packet existed in @rtcp.
 */
gboolean
gst_rtcp_buffer_get_first_packet (GstRTCPBuffer * rtcp, GstRTCPPacket * packet)
{
  g_return_val_if_fail (rtcp != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (rtcp->buffer), FALSE);
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (rtcp != NULL, 0);
  g_return_val_if_fail (rtcp->map.flags & GST_MAP_READ, 0);

  /* init to 0 */
  packet->rtcp = rtcp;
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
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, FALSE);

  /* if we have a padding or invalid packet, it must be the last, 
   * return FALSE */
  if (packet->type == GST_RTCP_TYPE_INVALID || packet->padding)
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
    return FALSE;
  }
}

/**
 * gst_rtcp_buffer_add_packet:
 * @rtcp: a valid RTCP buffer
 * @type: the #GstRTCPType of the new packet
 * @packet: pointer to new packet
 *
 * Add a new packet of @type to @rtcp. @packet will point to the newly created 
 * packet.
 *
 * Returns: %TRUE if the packet could be created. This function returns %FALSE
 * if the max mtu is exceeded for the buffer.
 */
gboolean
gst_rtcp_buffer_add_packet (GstRTCPBuffer * rtcp, GstRTCPType type,
    GstRTCPPacket * packet)
{
  guint len;
  gsize maxsize;
  guint8 *data;
  gboolean result;

  g_return_val_if_fail (rtcp != NULL, FALSE);
  g_return_val_if_fail (GST_IS_BUFFER (rtcp->buffer), FALSE);
  g_return_val_if_fail (type != GST_RTCP_TYPE_INVALID, FALSE);
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (rtcp->map.flags & GST_MAP_WRITE, FALSE);

  /* find free space */
  if (gst_rtcp_buffer_get_first_packet (rtcp, packet))
    while (gst_rtcp_packet_move_to_next (packet));

  maxsize = rtcp->map.maxsize;

  /* packet->offset is now pointing to the next free offset in the buffer to
   * start a compount packet. Next we figure out if we have enough free space in
   * the buffer to continue. */
  switch (type) {
    case GST_RTCP_TYPE_SR:
      len = 28;
      break;
    case GST_RTCP_TYPE_RR:
      len = 8;
      break;
    case GST_RTCP_TYPE_SDES:
      len = 4;
      break;
    case GST_RTCP_TYPE_BYE:
      len = 4;
      break;
    case GST_RTCP_TYPE_APP:
      len = 12;
      break;
    case GST_RTCP_TYPE_RTPFB:
      len = 12;
      break;
    case GST_RTCP_TYPE_PSFB:
      len = 12;
      break;
    default:
      goto unknown_type;
  }
  if (packet->offset + len >= maxsize)
    goto no_space;

  rtcp->map.size += len;

  data = rtcp->map.data + packet->offset;

  data[0] = (GST_RTCP_VERSION << 6);
  data[1] = type;
  /* length is stored in multiples of 32 bit words minus the length of the
   * header */
  len = (len - 4) >> 2;
  data[2] = len >> 8;
  data[3] = len & 0xff;

  /* now try to position to the packet */
  result = read_packet_header (packet);

  return result;

  /* ERRORS */
unknown_type:
  {
    g_warning ("unknown type %d", type);
    return FALSE;
  }
no_space:
  {
    return FALSE;
  }
}

/**
 * gst_rtcp_packet_remove:
 * @packet: a #GstRTCPPacket
 *
 * Removes the packet pointed to by @packet and moves pointer to the next one
 *
 * Returns: TRUE if @packet is pointing to a valid packet after calling this
 * function.
 */
gboolean
gst_rtcp_packet_remove (GstRTCPPacket * packet)
{
  gboolean ret = FALSE;
  guint offset = 0;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type != GST_RTCP_TYPE_INVALID, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  /* The next packet starts at offset + length + 4 (the header) */
  offset = packet->offset + (packet->length << 2) + 4;

  /* Overwrite this packet with the rest of the data */
  memmove (packet->rtcp->map.data + packet->offset,
      packet->rtcp->map.data + offset, packet->rtcp->map.size - offset);

  packet->rtcp->map.size -= offset - packet->offset;

  /* try to read next header */
  ret = read_packet_header (packet);
  if (!ret)
    packet->type = GST_RTCP_TYPE_INVALID;

  return ret;
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
 * Returns: The packet type or GST_RTCP_TYPE_INVALID when @packet is not
 * pointing to a valid packet.
 */
GstRTCPType
gst_rtcp_packet_get_type (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, GST_RTCP_TYPE_INVALID);

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
 * @octet_count: result octet count
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
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_READ);

  data = packet->rtcp->map.data;

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
 * @octet_count: the octet count
 *
 * Set the given values in the SR packet @packet.
 */
void
gst_rtcp_packet_sr_set_sender_info (GstRTCPPacket * packet, guint32 ssrc,
    guint64 ntptime, guint32 rtptime, guint32 packet_count, guint32 octet_count)
{
  guint8 *data;

  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_SR);
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE);

  data = packet->rtcp->map.data;

  /* skip header */
  data += packet->offset + 4;
  GST_WRITE_UINT32_BE (data, ssrc);
  data += 4;
  GST_WRITE_UINT64_BE (data, ntptime);
  data += 8;
  GST_WRITE_UINT32_BE (data, rtptime);
  data += 4;
  GST_WRITE_UINT32_BE (data, packet_count);
  data += 4;
  GST_WRITE_UINT32_BE (data, octet_count);
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
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

  data = packet->rtcp->map.data;

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
 */
void
gst_rtcp_packet_rr_set_ssrc (GstRTCPPacket * packet, guint32 ssrc)
{
  guint8 *data;

  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RR);
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE);

  data = packet->rtcp->map.data;

  /* skip header */
  data += packet->offset + 4;
  GST_WRITE_UINT32_BE (data, ssrc);
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
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

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
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_READ);

  data = packet->rtcp->map.data;

  /* skip header */
  data += packet->offset + 4;
  if (packet->type == GST_RTCP_TYPE_RR)
    data += 4;
  else
    data += 24;

  /* move to requested index */
  data += (nth * 24);

  if (ssrc)
    *ssrc = GST_READ_UINT32_BE (data);
  data += 4;
  tmp = GST_READ_UINT32_BE (data);
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
 * Returns: %TRUE if the packet was created. This function can return %FALSE if
 * the max MTU is exceeded or the number of report blocks is greater than
 * #GST_RTCP_MAX_RB_COUNT.
 */
gboolean
gst_rtcp_packet_add_rb (GstRTCPPacket * packet, guint32 ssrc,
    guint8 fractionlost, gint32 packetslost, guint32 exthighestseq,
    guint32 jitter, guint32 lsr, guint32 dlsr)
{
  guint8 *data;
  guint maxsize, offset;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_RR ||
      packet->type == GST_RTCP_TYPE_SR, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  if (packet->count >= GST_RTCP_MAX_RB_COUNT)
    goto no_space;

  data = packet->rtcp->map.data;
  maxsize = packet->rtcp->map.maxsize;

  /* skip header */
  offset = packet->offset + 4;
  if (packet->type == GST_RTCP_TYPE_RR)
    offset += 4;
  else
    offset += 24;

  /* move to current index */
  offset += (packet->count * 24);

  /* we need 24 free bytes now */
  if (offset + 24 >= maxsize)
    goto no_space;

  /* increment packet count and length */
  packet->count++;
  data[packet->offset]++;
  packet->length += 6;
  data[packet->offset + 2] = (packet->length) >> 8;
  data[packet->offset + 3] = (packet->length) & 0xff;
  packet->rtcp->map.size += 6 * 4;

  /* move to new report block offset */
  data += offset;

  GST_WRITE_UINT32_BE (data, ssrc);
  data += 4;
  GST_WRITE_UINT32_BE (data, (fractionlost << 24) | (packetslost & 0xffffff));
  data += 4;
  GST_WRITE_UINT32_BE (data, exthighestseq);
  data += 4;
  GST_WRITE_UINT32_BE (data, jitter);
  data += 4;
  GST_WRITE_UINT32_BE (data, lsr);
  data += 4;
  GST_WRITE_UINT32_BE (data, dlsr);

  return TRUE;

no_space:
  {
    return FALSE;
  }
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
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE);

  g_warning ("not implemented");
}


/**
 * gst_rtcp_packet_sdes_get_item_count:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Get the number of items in the SDES packet @packet.
 *
 * Returns: The number of items in @packet.
 */
guint
gst_rtcp_packet_sdes_get_item_count (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);

  return packet->count;
}

/**
 * gst_rtcp_packet_sdes_first_item:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the first SDES item in @packet.
 *
 * Returns: TRUE if there was a first item.
 */
gboolean
gst_rtcp_packet_sdes_first_item (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);

  packet->item_offset = 4;
  packet->item_count = 0;
  packet->entry_offset = 4;

  if (packet->count == 0)
    return FALSE;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_next_item:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the next SDES item in @packet.
 *
 * Returns: TRUE if there was a next item.
 */
gboolean
gst_rtcp_packet_sdes_next_item (GstRTCPPacket * packet)
{
  guint8 *data;
  guint offset;
  guint len;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, FALSE);

  /* if we are at the last item, we are done */
  if (packet->item_count == packet->count)
    return FALSE;

  /* move to SDES */
  data = packet->rtcp->map.data;
  data += packet->offset;
  /* move to item */
  offset = packet->item_offset;
  /* skip SSRC */
  offset += 4;

  /* don't overrun */
  len = (packet->length << 2);

  while (offset < len) {
    if (data[offset] == 0) {
      /* end of list, round to next 32-bit word */
      offset = (offset + 4) & ~3;
      break;
    }
    offset += data[offset + 1] + 2;
  }
  if (offset >= len)
    return FALSE;

  packet->item_offset = offset;
  packet->item_count++;
  packet->entry_offset = 4;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_get_ssrc:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Get the SSRC of the current SDES item.
 *
 * Returns: the SSRC of the current item.
 */
guint32
gst_rtcp_packet_sdes_get_ssrc (GstRTCPPacket * packet)
{
  guint32 ssrc;
  guint8 *data;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, 0);
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

  /* move to SDES */
  data = packet->rtcp->map.data;
  data += packet->offset;
  /* move to item */
  data += packet->item_offset;

  ssrc = GST_READ_UINT32_BE (data);

  return ssrc;
}

/**
 * gst_rtcp_packet_sdes_first_entry:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the first SDES entry in the current item.
 *
 * Returns: %TRUE if there was a first entry.
 */
gboolean
gst_rtcp_packet_sdes_first_entry (GstRTCPPacket * packet)
{
  guint8 *data;
  guint len, offset;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, FALSE);

  /* move to SDES */
  data = packet->rtcp->map.data;
  data += packet->offset;
  /* move to item */
  offset = packet->item_offset;
  /* skip SSRC */
  offset += 4;

  packet->entry_offset = 4;

  /* don't overrun */
  len = (packet->length << 2);
  if (offset >= len)
    return FALSE;

  if (data[offset] == 0)
    return FALSE;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_next_entry:
 * @packet: a valid SDES #GstRTCPPacket
 *
 * Move to the next SDES entry in the current item.
 *
 * Returns: %TRUE if there was a next entry.
 */
gboolean
gst_rtcp_packet_sdes_next_entry (GstRTCPPacket * packet)
{
  guint8 *data;
  guint len, offset, item_len;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, FALSE);

  /* move to SDES */
  data = packet->rtcp->map.data;
  data += packet->offset;
  /* move to item */
  offset = packet->item_offset;
  /* move to entry */
  offset += packet->entry_offset;

  item_len = data[offset + 1] + 2;
  /* skip item */
  offset += item_len;

  /* don't overrun */
  len = (packet->length << 2);
  if (offset >= len)
    return FALSE;

  packet->entry_offset += item_len;

  /* check for end of list */
  if (data[offset] == 0)
    return FALSE;

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_get_entry:
 * @packet: a valid SDES #GstRTCPPacket
 * @type: result of the entry type
 * @len: (out): result length of the entry data
 * @data: (out) (array length=len) (transfer none): result entry data
 *
 * Get the data of the current SDES item entry. @type (when not NULL) will
 * contain the type of the entry. @data (when not NULL) will point to @len
 * bytes.
 *
 * When @type refers to a text item, @data will point to a UTF8 string. Note
 * that this UTF8 string is NOT null-terminated. Use
 * gst_rtcp_packet_sdes_copy_entry() to get a null-terminated copy of the entry.
 *
 * Returns: %TRUE if there was valid data.
 */
gboolean
gst_rtcp_packet_sdes_get_entry (GstRTCPPacket * packet,
    GstRTCPSDESType * type, guint8 * len, guint8 ** data)
{
  guint8 *bdata;
  guint offset;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, FALSE);

  /* move to SDES */
  bdata = packet->rtcp->map.data;
  bdata += packet->offset;
  /* move to item */
  offset = packet->item_offset;
  /* move to entry */
  offset += packet->entry_offset;

  if (bdata[offset] == 0)
    return FALSE;

  if (type)
    *type = bdata[offset];
  if (len)
    *len = bdata[offset + 1];
  if (data)
    *data = &bdata[offset + 2];

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_copy_entry:
 * @packet: a valid SDES #GstRTCPPacket
 * @type: result of the entry type
 * @len: (out): result length of the entry data
 * @data: (out) (array length=len): result entry data
 *
 * This function is like gst_rtcp_packet_sdes_get_entry() but it returns a
 * null-terminated copy of the data instead. use g_free() after usage.
 *
 * Returns: %TRUE if there was valid data.
 */
gboolean
gst_rtcp_packet_sdes_copy_entry (GstRTCPPacket * packet,
    GstRTCPSDESType * type, guint8 * len, guint8 ** data)
{
  guint8 *tdata;
  guint8 tlen;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, FALSE);

  if (!gst_rtcp_packet_sdes_get_entry (packet, type, &tlen, &tdata))
    return FALSE;

  if (len)
    *len = tlen;
  if (data)
    *data = (guint8 *) g_strndup ((gchar *) tdata, tlen);

  return TRUE;
}

/**
 * gst_rtcp_packet_sdes_add_item:
 * @packet: a valid SDES #GstRTCPPacket
 * @ssrc: the SSRC of the new item to add
 *
 * Add a new SDES item for @ssrc to @packet.
 *
 * Returns: %TRUE if the item could be added, %FALSE if the maximum amount of
 * items has been exceeded for the SDES packet or the MTU has been reached.
 */
gboolean
gst_rtcp_packet_sdes_add_item (GstRTCPPacket * packet, guint32 ssrc)
{
  guint8 *data;
  guint offset;
  gsize maxsize;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  /* increment item count when possible */
  if (packet->count >= GST_RTCP_MAX_SDES_ITEM_COUNT)
    goto no_space;

  /* pretend there is a next packet for the next call */
  packet->count++;

  /* jump over current item */
  gst_rtcp_packet_sdes_next_item (packet);

  /* move to SDES */
  data = packet->rtcp->map.data;
  maxsize = packet->rtcp->map.maxsize;
  data += packet->offset;
  /* move to current item */
  offset = packet->item_offset;

  /* we need 2 free words now */
  if (offset + 8 >= maxsize)
    goto no_next;

  /* write SSRC */
  GST_WRITE_UINT32_BE (&data[offset], ssrc);
  /* write 0 entry with padding */
  GST_WRITE_UINT32_BE (&data[offset + 4], 0);

  /* update count */
  data[0] = (data[0] & 0xe0) | packet->count;
  /* update length, we added 2 words */
  packet->length += 2;
  data[2] = (packet->length) >> 8;
  data[3] = (packet->length) & 0xff;

  packet->rtcp->map.size += 8;

  return TRUE;

  /* ERRORS */
no_space:
  {
    return FALSE;
  }
no_next:
  {
    packet->count--;
    return FALSE;
  }
}

/**
 * gst_rtcp_packet_sdes_add_entry:
 * @packet: a valid SDES #GstRTCPPacket
 * @type: the #GstRTCPSDESType of the SDES entry
 * @len: the data length
 * @data: (array length=len): the data
 *
 * Add a new SDES entry to the current item in @packet.
 *
 * Returns: %TRUE if the item could be added, %FALSE if the MTU has been
 * reached.
 */
gboolean
gst_rtcp_packet_sdes_add_entry (GstRTCPPacket * packet, GstRTCPSDESType type,
    guint8 len, const guint8 * data)
{
  guint8 *bdata;
  guint offset, padded;
  gsize maxsize;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_SDES, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  /* move to SDES */
  bdata = packet->rtcp->map.data;
  maxsize = packet->rtcp->map.maxsize;
  bdata += packet->offset;
  /* move to item */
  offset = packet->item_offset;
  /* move to entry */
  offset += packet->entry_offset;

  /* add 1 byte end and up to 3 bytes padding to fill a full 32 bit word */
  padded = (offset + 2 + len + 1 + 3) & ~3;

  /* we need enough space for type, len, data and padding */
  if (packet->offset + padded >= maxsize)
    goto no_space;

  packet->rtcp->map.size = packet->offset + padded;

  bdata[offset] = type;
  bdata[offset + 1] = len;
  memcpy (&bdata[offset + 2], data, len);
  bdata[offset + 2 + len] = 0;

  /* calculate new packet length */
  packet->length = (padded - 4) >> 2;
  bdata[2] = (packet->length) >> 8;
  bdata[3] = (packet->length) & 0xff;

  /* position to new next entry */
  packet->entry_offset += 2 + len;

  return TRUE;

  /* ERRORS */
no_space:
  {
    return FALSE;
  }
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
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

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
  if (offset + 4 > packet->rtcp->map.size)
    return 0;

  data = packet->rtcp->map.data;
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
 * Returns: %TRUE if the ssrc was added. This function can return %FALSE if
 * the max MTU is exceeded or the number of sources blocks is greater than
 * #GST_RTCP_MAX_BYE_SSRC_COUNT.
 */
gboolean
gst_rtcp_packet_bye_add_ssrc (GstRTCPPacket * packet, guint32 ssrc)
{
  guint8 *data;
  gsize maxsize;
  guint offset;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  if (packet->count >= GST_RTCP_MAX_BYE_SSRC_COUNT)
    goto no_space;

  data = packet->rtcp->map.data;
  maxsize = packet->rtcp->map.maxsize;

  /* skip header */
  offset = packet->offset + 4;

  /* move to current index */
  offset += (packet->count * 4);

  if (offset + 4 >= maxsize)
    goto no_space;

  /* increment packet count and length */
  packet->count++;
  data[packet->offset]++;
  packet->length += 1;
  data[packet->offset + 2] = (packet->length) >> 8;
  data[packet->offset + 3] = (packet->length) & 0xff;

  packet->rtcp->map.size += 4;

  /* move to new SSRC offset and write ssrc */
  data += offset;
  GST_WRITE_UINT32_BE (data, ssrc);

  return TRUE;

  /* ERRORS */
no_space:
  {
    return FALSE;
  }
}

/**
 * gst_rtcp_packet_bye_add_ssrcs:
 * @packet: a valid BYE #GstRTCPPacket
 * @ssrc: an array of SSRCs to add
 * @len: number of elements in @ssrc
 *
 * Adds @len SSRCs in @ssrc to BYE @packet.
 *
 * Returns: %TRUE if the all the SSRCs were added. This function can return %FALSE if
 * the max MTU is exceeded or the number of sources blocks is greater than
 * #GST_RTCP_MAX_BYE_SSRC_COUNT.
 */
gboolean
gst_rtcp_packet_bye_add_ssrcs (GstRTCPPacket * packet, guint32 * ssrc,
    guint len)
{
  guint i;
  gboolean res;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  res = TRUE;
  for (i = 0; i < len && res; i++) {
    res = gst_rtcp_packet_bye_add_ssrc (packet, ssrc[i]);
  }
  return res;
}

/* get the offset in packet of the reason length */
static guint
get_reason_offset (GstRTCPPacket * packet)
{
  guint offset;

  /* get amount of sources plus header */
  offset = 1 + packet->count;

  /* check that we don't go past the packet length */
  if (offset > packet->length)
    return 0;

  /* scale to bytes */
  offset <<= 2;
  offset += packet->offset;

  /* check if the packet is valid */
  if (offset + 1 > packet->rtcp->map.size)
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
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

  roffset = get_reason_offset (packet);
  if (roffset == 0)
    return 0;

  data = packet->rtcp->map.data;

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
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, NULL);
  g_return_val_if_fail (packet->rtcp != NULL, NULL);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, NULL);

  roffset = get_reason_offset (packet);
  if (roffset == 0)
    return NULL;

  data = packet->rtcp->map.data;

  /* get length of reason string */
  len = data[roffset];
  if (len == 0)
    return NULL;

  /* move to string */
  roffset += 1;

  /* check if enough data to copy */
  if (roffset + len > packet->rtcp->map.size)
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
 */
gboolean
gst_rtcp_packet_bye_set_reason (GstRTCPPacket * packet, const gchar * reason)
{
  guint8 *data;
  guint roffset;
  gsize maxsize;
  guint8 len, padded;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_BYE, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  if (reason == NULL)
    return TRUE;

  len = strlen (reason);
  if (len == 0)
    return TRUE;

  /* make room for the string before we get the offset */
  packet->length++;

  roffset = get_reason_offset (packet);
  if (roffset == 0)
    goto no_space;

  data = packet->rtcp->map.data;
  maxsize = packet->rtcp->map.maxsize;

  /* we have 1 byte length and we need to pad to 4 bytes */
  padded = ((len + 1) + 3) & ~3;

  /* we need enough space for the padded length */
  if (roffset + padded >= maxsize)
    goto no_space;

  data[roffset] = len;
  memcpy (&data[roffset + 1], reason, len);

  /* update packet length, we made room for 1 double word already */
  packet->length += (padded >> 2) - 1;
  data[packet->offset + 2] = (packet->length) >> 8;
  data[packet->offset + 3] = (packet->length) & 0xff;

  packet->rtcp->map.size += padded;

  return TRUE;

  /* ERRORS */
no_space:
  {
    packet->length--;
    return FALSE;
  }
}

/**
 * gst_rtcp_packet_fb_get_sender_ssrc:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 *
 * Get the sender SSRC field of the RTPFB or PSFB @packet.
 *
 * Returns: the sender SSRC.
 */
guint32
gst_rtcp_packet_fb_get_sender_ssrc (GstRTCPPacket * packet)
{
  guint8 *data;
  guint32 ssrc;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail ((packet->type == GST_RTCP_TYPE_RTPFB ||
          packet->type == GST_RTCP_TYPE_PSFB), 0);
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

  data = packet->rtcp->map.data;

  /* skip header */
  data += packet->offset + 4;
  ssrc = GST_READ_UINT32_BE (data);

  return ssrc;
}

/**
 * gst_rtcp_packet_fb_set_sender_ssrc:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 * @ssrc: a sender SSRC
 *
 * Set the sender SSRC field of the RTPFB or PSFB @packet.
 */
void
gst_rtcp_packet_fb_set_sender_ssrc (GstRTCPPacket * packet, guint32 ssrc)
{
  guint8 *data;

  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RTPFB ||
      packet->type == GST_RTCP_TYPE_PSFB);
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_READ);

  data = packet->rtcp->map.data;

  /* skip header */
  data += packet->offset + 4;
  GST_WRITE_UINT32_BE (data, ssrc);
}

/**
 * gst_rtcp_packet_fb_get_media_ssrc:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 *
 * Get the media SSRC field of the RTPFB or PSFB @packet.
 *
 * Returns: the media SSRC.
 */
guint32
gst_rtcp_packet_fb_get_media_ssrc (GstRTCPPacket * packet)
{
  guint8 *data;
  guint32 ssrc;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail ((packet->type == GST_RTCP_TYPE_RTPFB ||
          packet->type == GST_RTCP_TYPE_PSFB), 0);
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

  data = packet->rtcp->map.data;

  /* skip header and sender ssrc */
  data += packet->offset + 8;
  ssrc = GST_READ_UINT32_BE (data);

  return ssrc;
}

/**
 * gst_rtcp_packet_fb_set_media_ssrc:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 * @ssrc: a media SSRC
 *
 * Set the media SSRC field of the RTPFB or PSFB @packet.
 */
void
gst_rtcp_packet_fb_set_media_ssrc (GstRTCPPacket * packet, guint32 ssrc)
{
  guint8 *data;

  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RTPFB ||
      packet->type == GST_RTCP_TYPE_PSFB);
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE);

  data = packet->rtcp->map.data;

  /* skip header and sender ssrc */
  data += packet->offset + 8;
  GST_WRITE_UINT32_BE (data, ssrc);
}

/**
 * gst_rtcp_packet_fb_get_type:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 *
 * Get the feedback message type of the FB @packet.
 *
 * Returns: The feedback message type.
 */
GstRTCPFBType
gst_rtcp_packet_fb_get_type (GstRTCPPacket * packet)
{
  g_return_val_if_fail (packet != NULL, GST_RTCP_FB_TYPE_INVALID);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_RTPFB ||
      packet->type == GST_RTCP_TYPE_PSFB, GST_RTCP_FB_TYPE_INVALID);

  return packet->count;
}

/**
 * gst_rtcp_packet_fb_set_type:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 * @type: the #GstRTCPFBType to set
 *
 * Set the feedback message type of the FB @packet.
 */
void
gst_rtcp_packet_fb_set_type (GstRTCPPacket * packet, GstRTCPFBType type)
{
  guint8 *data;

  g_return_if_fail (packet != NULL);
  g_return_if_fail (packet->type == GST_RTCP_TYPE_RTPFB ||
      packet->type == GST_RTCP_TYPE_PSFB);
  g_return_if_fail (packet->rtcp != NULL);
  g_return_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE);

  data = packet->rtcp->map.data;

  data[packet->offset] = (data[packet->offset] & 0xe0) | type;
  packet->count = type;
}

/**
 * gst_rtcp_ntp_to_unix:
 * @ntptime: an NTP timestamp
 *
 * Converts an NTP time to UNIX nanoseconds. @ntptime can typically be
 * the NTP time of an SR RTCP message and contains, in the upper 32 bits, the
 * number of seconds since 1900 and, in the lower 32 bits, the fractional
 * seconds. The resulting value will be the number of nanoseconds since 1970.
 *
 * Returns: the UNIX time for @ntptime in nanoseconds.
 */
guint64
gst_rtcp_ntp_to_unix (guint64 ntptime)
{
  guint64 unixtime;

  /* conversion from NTP timestamp (seconds since 1900) to seconds since
   * 1970. */
  unixtime = ntptime - (G_GUINT64_CONSTANT (2208988800) << 32);
  /* conversion to nanoseconds */
  unixtime =
      gst_util_uint64_scale (unixtime, GST_SECOND,
      (G_GINT64_CONSTANT (1) << 32));

  return unixtime;
}

/**
 * gst_rtcp_unix_to_ntp:
 * @unixtime: an UNIX timestamp in nanoseconds
 *
 * Converts a UNIX timestamp in nanoseconds to an NTP time. The caller should
 * pass a value with nanoseconds since 1970. The NTP time will, in the upper
 * 32 bits, contain the number of seconds since 1900 and, in the lower 32
 * bits, the fractional seconds. The resulting value can be used as an ntptime
 * for constructing SR RTCP packets.
 *
 * Returns: the NTP time for @unixtime.
 */
guint64
gst_rtcp_unix_to_ntp (guint64 unixtime)
{
  guint64 ntptime;

  /* convert clock time to NTP time. upper 32 bits should contain the seconds
   * and the lower 32 bits, the fractions of a second. */
  ntptime =
      gst_util_uint64_scale (unixtime, (G_GINT64_CONSTANT (1) << 32),
      GST_SECOND);
  /* conversion from UNIX timestamp (seconds since 1970) to NTP (seconds
   * since 1900). */
  ntptime += (G_GUINT64_CONSTANT (2208988800) << 32);

  return ntptime;
}

/**
 * gst_rtcp_sdes_type_to_name:
 * @type: a #GstRTCPSDESType
 *
 * Converts @type to the string equivalent. The string is typically used as a
 * key in a #GstStructure containing SDES items.
 *
 * Returns: the string equivalent of @type
 */
const gchar *
gst_rtcp_sdes_type_to_name (GstRTCPSDESType type)
{
  const gchar *result;

  switch (type) {
    case GST_RTCP_SDES_CNAME:
      result = "cname";
      break;
    case GST_RTCP_SDES_NAME:
      result = "name";
      break;
    case GST_RTCP_SDES_EMAIL:
      result = "email";
      break;
    case GST_RTCP_SDES_PHONE:
      result = "phone";
      break;
    case GST_RTCP_SDES_LOC:
      result = "location";
      break;
    case GST_RTCP_SDES_TOOL:
      result = "tool";
      break;
    case GST_RTCP_SDES_NOTE:
      result = "note";
      break;
    case GST_RTCP_SDES_PRIV:
      result = "priv";
      break;
    default:
      result = NULL;
      break;
  }
  return result;
}

/**
 * gst_rtcp_sdes_name_to_type:
 * @name: a SDES name
 *
 * Convert @name into a @GstRTCPSDESType. @name is typically a key in a
 * #GstStructure containing SDES items.
 *
 * Returns: the #GstRTCPSDESType for @name or #GST_RTCP_SDES_PRIV when @name
 * is a private sdes item.
 */
GstRTCPSDESType
gst_rtcp_sdes_name_to_type (const gchar * name)
{
  if (name == NULL || strlen (name) == 0)
    return GST_RTCP_SDES_INVALID;

  if (strcmp ("cname", name) == 0)
    return GST_RTCP_SDES_CNAME;

  if (strcmp ("name", name) == 0)
    return GST_RTCP_SDES_NAME;

  if (strcmp ("email", name) == 0)
    return GST_RTCP_SDES_EMAIL;

  if (strcmp ("phone", name) == 0)
    return GST_RTCP_SDES_PHONE;

  if (strcmp ("location", name) == 0)
    return GST_RTCP_SDES_LOC;

  if (strcmp ("tool", name) == 0)
    return GST_RTCP_SDES_TOOL;

  if (strcmp ("note", name) == 0)
    return GST_RTCP_SDES_NOTE;

  return GST_RTCP_SDES_PRIV;
}

/**
 * gst_rtcp_packet_fb_get_fci_length:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 *
 * Get the length of the Feedback Control Information attached to a
 * RTPFB or PSFB @packet.
 *
 * Returns: The length of the FCI in 32-bit words.
 */
guint16
gst_rtcp_packet_fb_get_fci_length (GstRTCPPacket * packet)
{
  guint8 *data;

  g_return_val_if_fail (packet != NULL, 0);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_RTPFB ||
      packet->type == GST_RTCP_TYPE_PSFB, 0);
  g_return_val_if_fail (packet->rtcp != NULL, 0);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, 0);

  data = packet->rtcp->map.data + packet->offset + 2;

  return GST_READ_UINT16_BE (data) - 2;
}

/**
 * gst_rtcp_packet_fb_set_fci_length:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 * @wordlen: Length of the FCI in 32-bit words
 *
 * Set the length of the Feedback Control Information attached to a
 * RTPFB or PSFB @packet.
 *
 * Returns: %TRUE if there was enough space in the packet to add this much FCI
 */
gboolean
gst_rtcp_packet_fb_set_fci_length (GstRTCPPacket * packet, guint16 wordlen)
{
  guint8 *data;

  g_return_val_if_fail (packet != NULL, FALSE);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_RTPFB ||
      packet->type == GST_RTCP_TYPE_PSFB, FALSE);
  g_return_val_if_fail (packet->rtcp != NULL, FALSE);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_WRITE, FALSE);

  if (packet->rtcp->map.maxsize < packet->offset + ((wordlen + 3) * 4))
    return FALSE;

  data = packet->rtcp->map.data + packet->offset + 2;
  wordlen += 2;
  GST_WRITE_UINT16_BE (data, wordlen);

  packet->rtcp->map.size += wordlen * 4;

  return TRUE;
}

/**
 * gst_rtcp_packet_fb_get_fci:
 * @packet: a valid RTPFB or PSFB #GstRTCPPacket
 *
 * Get the Feedback Control Information attached to a RTPFB or PSFB @packet.
 *
 * Returns: a pointer to the FCI
 */
guint8 *
gst_rtcp_packet_fb_get_fci (GstRTCPPacket * packet)
{
  guint8 *data;

  g_return_val_if_fail (packet != NULL, NULL);
  g_return_val_if_fail (packet->type == GST_RTCP_TYPE_RTPFB ||
      packet->type == GST_RTCP_TYPE_PSFB, NULL);
  g_return_val_if_fail (packet->rtcp != NULL, NULL);
  g_return_val_if_fail (packet->rtcp->map.flags & GST_MAP_READ, NULL);

  data = packet->rtcp->map.data + packet->offset;

  if (GST_READ_UINT16_BE (data + 2) <= 2)
    return NULL;

  return data + 12;
}
