/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org>
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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
 * SECTION:gstrtpbuffer
 * @short_description: Helper methods for dealing with RTP buffers
 * @see_also: #GstBaseRTPPayload, #GstBaseRTPDepayload, gstrtcpbuffer
 *
 * <refsect2>
 * <para>
 * The GstRTPBuffer helper functions makes it easy to parse and create regular 
 * #GstBuffer objects that contain RTP payloads. These buffers are typically of
 * 'application/x-rtp' #GstCaps.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2006-07-17 (0.10.10)
 */

#include "gstrtpbuffer.h"

#include <stdlib.h>
#include <string.h>

#define GST_RTP_HEADER_LEN 12

/* Note: we use bitfields here to make sure the compiler doesn't add padding
 * between fields on certain architectures; can't assume aligned access either
 */
typedef struct _GstRTPHeader
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned int csrc_count:4;    /* CSRC count */
  unsigned int extension:1;     /* header extension flag */
  unsigned int padding:1;       /* padding flag */
  unsigned int version:2;       /* protocol version */
  unsigned int payload_type:7;  /* payload type */
  unsigned int marker:1;        /* marker bit */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned int version:2;       /* protocol version */
  unsigned int padding:1;       /* padding flag */
  unsigned int extension:1;     /* header extension flag */
  unsigned int csrc_count:4;    /* CSRC count */
  unsigned int marker:1;        /* marker bit */
  unsigned int payload_type:7;  /* payload type */
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  unsigned int seq:16;          /* sequence number */
  unsigned int timestamp:32;    /* timestamp */
  unsigned int ssrc:32;         /* synchronization source */
  guint8 csrclist[4];           /* optional CSRC list, 32 bits each */
} GstRTPHeader;

#define GST_RTP_HEADER_VERSION(data)      (((GstRTPHeader *)(data))->version)
#define GST_RTP_HEADER_PADDING(data)      (((GstRTPHeader *)(data))->padding)
#define GST_RTP_HEADER_EXTENSION(data)    (((GstRTPHeader *)(data))->extension)
#define GST_RTP_HEADER_CSRC_COUNT(data)   (((GstRTPHeader *)(data))->csrc_count)
#define GST_RTP_HEADER_MARKER(data)       (((GstRTPHeader *)(data))->marker)
#define GST_RTP_HEADER_PAYLOAD_TYPE(data) (((GstRTPHeader *)(data))->payload_type)
#define GST_RTP_HEADER_SEQ(data)          (((GstRTPHeader *)(data))->seq)
#define GST_RTP_HEADER_TIMESTAMP(data)    (((GstRTPHeader *)(data))->timestamp)
#define GST_RTP_HEADER_SSRC(data)         (((GstRTPHeader *)(data))->ssrc)
#define GST_RTP_HEADER_CSRC_LIST_OFFSET(data,i)        \
    data + G_STRUCT_OFFSET(GstRTPHeader, csrclist) +   \
    ((i) * sizeof(guint32))
#define GST_RTP_HEADER_CSRC_SIZE(data)   (GST_RTP_HEADER_CSRC_COUNT(data) * sizeof (guint32))

/**
 * gst_rtp_buffer_allocate_data:
 * @buffer: a #GstBuffer
 * @payload_len: the length of the payload
 * @pad_len: the amount of padding
 * @csrc_count: the number of CSRC entries
 *
 * Allocate enough data in @buffer to hold an RTP packet with @csrc_count CSRCs,
 * a payload length of @payload_len and padding of @pad_len.
 * MALLOCDATA of @buffer will be overwritten and will not be freed. 
 * All other RTP header fields will be set to 0/FALSE.
 */
void
gst_rtp_buffer_allocate_data (GstBuffer * buffer, guint payload_len,
    guint8 pad_len, guint8 csrc_count)
{
  guint len;
  guint8 *data;

  g_return_if_fail (csrc_count <= 15);
  g_return_if_fail (GST_IS_BUFFER (buffer));

  len = GST_RTP_HEADER_LEN + csrc_count * sizeof (guint32)
      + payload_len + pad_len;

  data = g_malloc (len);
  GST_BUFFER_MALLOCDATA (buffer) = data;
  GST_BUFFER_DATA (buffer) = data;
  GST_BUFFER_SIZE (buffer) = len;

  /* fill in defaults */
  GST_RTP_HEADER_VERSION (data) = GST_RTP_VERSION;
  GST_RTP_HEADER_PADDING (data) = FALSE;
  GST_RTP_HEADER_EXTENSION (data) = FALSE;
  GST_RTP_HEADER_CSRC_COUNT (data) = csrc_count;
  memset (GST_RTP_HEADER_CSRC_LIST_OFFSET (data, 0), 0,
      csrc_count * sizeof (guint32));
  GST_RTP_HEADER_MARKER (data) = FALSE;
  GST_RTP_HEADER_PAYLOAD_TYPE (data) = 0;
  GST_RTP_HEADER_SEQ (data) = 0;
  GST_RTP_HEADER_TIMESTAMP (data) = 0;
  GST_RTP_HEADER_SSRC (data) = 0;
}

/**
 * gst_rtp_buffer_new_take_data:
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
gst_rtp_buffer_new_take_data (gpointer data, guint len)
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
 * gst_rtp_buffer_new_copy_data:
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
gst_rtp_buffer_new_copy_data (gpointer data, guint len)
{
  return gst_rtp_buffer_new_take_data (g_memdup (data, len), len);
}

/**
 * gst_rtp_buffer_new_allocate:
 * @payload_len: the length of the payload
 * @pad_len: the amount of padding
 * @csrc_count: the number of CSRC entries
 *
 * Allocate a new #GstBuffer with enough data to hold an RTP packet with
 * @csrc_count CSRCs, a payload length of @payload_len and padding of @pad_len.
 * All other RTP header fields will be set to 0/FALSE.
 *
 * Returns: A newly allocated buffer that can hold an RTP packet with given
 * parameters.
 */
GstBuffer *
gst_rtp_buffer_new_allocate (guint payload_len, guint8 pad_len,
    guint8 csrc_count)
{
  GstBuffer *result;

  g_return_val_if_fail (csrc_count <= 15, NULL);

  result = gst_buffer_new ();
  gst_rtp_buffer_allocate_data (result, payload_len, pad_len, csrc_count);

  return result;
}

/**
 * gst_rtp_buffer_new_allocate_len:
 * @packet_len: the total length of the packet
 * @pad_len: the amount of padding
 * @csrc_count: the number of CSRC entries
 *
 * Create a new #GstBuffer that can hold an RTP packet that is exactly
 * @packet_len long. The length of the payload depends on @pad_len and
 * @csrc_count and can be calculated with gst_rtp_buffer_calc_payload_len().
 * All RTP header fields will be set to 0/FALSE.
 *
 * Returns: A newly allocated buffer that can hold an RTP packet of @packet_len.
 */
GstBuffer *
gst_rtp_buffer_new_allocate_len (guint packet_len, guint8 pad_len,
    guint8 csrc_count)
{
  guint len;

  g_return_val_if_fail (csrc_count <= 15, NULL);

  len = gst_rtp_buffer_calc_payload_len (packet_len, pad_len, csrc_count);

  return gst_rtp_buffer_new_allocate (len, pad_len, csrc_count);
}

/**
 * gst_rtp_buffer_calc_header_len:
 * @csrc_count: the number of CSRC entries
 *
 * Calculate the header length of an RTP packet with @csrc_count CSRC entries.
 * An RTP packet can have at most 15 CSRC entries.
 *
 * Returns: The length of an RTP header with @csrc_count CSRC entries.
 */
guint
gst_rtp_buffer_calc_header_len (guint8 csrc_count)
{
  g_return_val_if_fail (csrc_count <= 15, 0);

  return GST_RTP_HEADER_LEN + (csrc_count * sizeof (guint32));
}

/**
 * gst_rtp_buffer_calc_packet_len:
 * @payload_len: the length of the payload
 * @pad_len: the amount of padding
 * @csrc_count: the number of CSRC entries
 *
 * Calculate the total length of an RTP packet with a payload size of @payload_len,
 * a padding of @pad_len and a @csrc_count CSRC entries.
 *
 * Returns: The total length of an RTP header with given parameters.
 */
guint
gst_rtp_buffer_calc_packet_len (guint payload_len, guint8 pad_len,
    guint8 csrc_count)
{
  g_return_val_if_fail (csrc_count <= 15, 0);

  return payload_len + GST_RTP_HEADER_LEN + (csrc_count * sizeof (guint32))
      + pad_len;
}

/**
 * gst_rtp_buffer_calc_payload_len:
 * @packet_len: the length of the total RTP packet
 * @pad_len: the amount of padding
 * @csrc_count: the number of CSRC entries
 *
 * Calculate the length of the payload of an RTP packet with size @packet_len,
 * a padding of @pad_len and a @csrc_count CSRC entries.
 *
 * Returns: The length of the payload of an RTP packet  with given parameters.
 */
guint
gst_rtp_buffer_calc_payload_len (guint packet_len, guint8 pad_len,
    guint8 csrc_count)
{
  g_return_val_if_fail (csrc_count <= 15, 0);

  return packet_len - GST_RTP_HEADER_LEN - (csrc_count * sizeof (guint32))
      - pad_len;
}

/*
 * validate_data:
 * @data: the data to validate
 * @len: the length of @data to validate
 * @payload: the payload if @data represents the header only
 * @payload_len: the len of the payload
 *
 * Checks if @data is a valid RTP packet.
 *
 * Returns: TRUE if @data is a valid RTP packet
 */
static gboolean
validate_data (guint8 * data, guint len, guint8 * payload, guint payload_len)
{
  guint8 padding;
  guint8 csrc_count;
  guint header_len;
  guint8 version;

  g_return_val_if_fail (data != NULL, FALSE);

  header_len = GST_RTP_HEADER_LEN;
  if (G_UNLIKELY (len < header_len))
    goto wrong_length;

  /* check version */
  version = (data[0] & 0xc0);
  if (G_UNLIKELY (version != (GST_RTP_VERSION << 6)))
    goto wrong_version;

  /* calc header length with csrc */
  csrc_count = (data[0] & 0x0f);
  header_len += csrc_count * sizeof (guint32);

  /* calc extension length when present. */
  if (data[0] & 0x10) {
    guint8 *extpos;
    guint16 extlen;

    /* this points to the extension bits and header length */
    extpos = &data[header_len];

    /* skip the header and check that we have enough space */
    header_len += 4;
    if (G_UNLIKELY (len < header_len))
      goto wrong_length;

    /* skip id */
    extpos += 2;
    /* read length as the number of 32 bits words */
    extlen = GST_READ_UINT16_BE (extpos);

    header_len += extlen * sizeof (guint32);
  }

  /* check for padding */
  if (data[0] & 0x20) {
    if (payload)
      padding = payload[payload_len - 1];
    else
      padding = data[len - 1];
  } else {
    padding = 0;
  }

  /* check if padding and header not bigger than packet length */
  if (G_UNLIKELY (len < padding + header_len))
    goto wrong_padding;

  return TRUE;

  /* ERRORS */
wrong_length:
  {
    GST_DEBUG ("len < header_len check failed (%d < %d)", len, header_len);
    goto dump_packet;
  }
wrong_version:
  {
    GST_DEBUG ("version check failed (%d != %d)", version, GST_RTP_VERSION);
    goto dump_packet;
  }
wrong_padding:
  {
    GST_DEBUG ("padding check failed (%d - %d < %d)", len, header_len, padding);
    goto dump_packet;
  }
dump_packet:
  {
    GST_MEMDUMP ("buffer", data, len);
    return FALSE;
  }
}

/**
 * gst_rtp_buffer_validate_data:
 * @data: the data to validate
 * @len: the length of @data to validate
 *
 * Check if the @data and @size point to the data of a valid RTP packet.
 * This function checks the length, version and padding of the packet data.
 * Use this function to validate a packet before using the other functions in
 * this module.
 *
 * Returns: TRUE if the data points to a valid RTP packet.
 */
gboolean
gst_rtp_buffer_validate_data (guint8 * data, guint len)
{
  return validate_data (data, len, NULL, 0);
}

/**
 * gst_rtp_buffer_validate:
 * @buffer: the buffer to validate
 *
 * Check if the data pointed to by @buffer is a valid RTP packet using
 * gst_rtp_buffer_validate_data().
 * Use this function to validate a packet before using the other functions in
 * this module.
 *
 * Returns: TRUE if @buffer is a valid RTP packet.
 */
gboolean
gst_rtp_buffer_validate (GstBuffer * buffer)
{
  guint8 *data;
  guint len;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  data = GST_BUFFER_DATA (buffer);
  len = GST_BUFFER_SIZE (buffer);

  return validate_data (data, len, NULL, 0);
}

/**
 * gst_rtp_buffer_list_validate:
 * @list: the buffer list to validate
 *
 * Check if all RTP packets in the @list are valid using gst_rtp_buffer_validate_data().
 * Use this function to validate an list before using the other functions in
 * this module.
 *
 * Returns: TRUE if @list consists only of valid RTP packets.
 *
 * Since: 0.10.24
 */
gboolean
gst_rtp_buffer_list_validate (GstBufferList * list)
{
  guint16 prev_seqnum = 0;
  GstBufferListIterator *it;
  guint i = 0;

  g_return_val_if_fail (GST_IS_BUFFER_LIST (list), FALSE);

  it = gst_buffer_list_iterate (list);
  g_return_val_if_fail (it != NULL, FALSE);

  /* iterate through all the RTP packets in the list */
  while (gst_buffer_list_iterator_next_group (it)) {
    GstBuffer *rtpbuf;
    GstBuffer *paybuf;
    guint8 *packet_header;
    guint8 *packet_payload;
    guint payload_size;
    guint packet_size;
    guint j, n_buffers;

    /* each group should consists of at least 1 buffer: The first buffer always
     * contains the complete RTP header. Next buffers contain the payload */
    n_buffers = gst_buffer_list_iterator_n_buffers (it);
    if (n_buffers < 1)
      goto invalid_list;

    /* get the RTP header (and if n_buffers == 1 also the payload) */
    rtpbuf = gst_buffer_list_iterator_next (it);
    packet_header = GST_BUFFER_DATA (rtpbuf);
    if (packet_header == NULL)
      goto invalid_list;

    /* check the sequence number */
    if (G_UNLIKELY (i == 0)) {
      prev_seqnum = g_ntohs (GST_RTP_HEADER_SEQ (packet_header));
      i++;
    } else {
      if (++prev_seqnum != g_ntohs (GST_RTP_HEADER_SEQ (packet_header)))
        goto invalid_list;
    }

    packet_size = GST_BUFFER_SIZE (rtpbuf);
    packet_payload = NULL;
    payload_size = 0;

    /* get the payload buffers */
    for (j = 1; j < n_buffers; j++) {
      /* get the payload */
      paybuf = gst_buffer_list_iterator_next (it);

      if ((packet_payload = GST_BUFFER_DATA (paybuf)) == NULL)
        goto invalid_list;

      if ((payload_size = GST_BUFFER_SIZE (paybuf)) == 0)
        goto invalid_list;

      /* the size of the RTP packet within the current group */
      packet_size += payload_size;
    }

    /* validate packet */
    if (!validate_data (packet_header, packet_size, packet_payload,
            payload_size)) {
      goto invalid_list;
    }
  }

  gst_buffer_list_iterator_free (it);

  return TRUE;

  /* ERRORS */
invalid_list:
  {
    gst_buffer_list_iterator_free (it);
    return FALSE;
  }
}

/**
 * gst_rtp_buffer_set_packet_len:
 * @buffer: the buffer
 * @len: the new packet length
 *
 * Set the total @buffer size to @len. The data in the buffer will be made
 * larger if needed. Any padding will be removed from the packet. 
 */
void
gst_rtp_buffer_set_packet_len (GstBuffer * buffer, guint len)
{
  guint oldlen;
  guint8 *data;

  oldlen = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  if (oldlen < len) {
    data = g_realloc (GST_BUFFER_MALLOCDATA (buffer), len);
    GST_BUFFER_MALLOCDATA (buffer) = data;
    GST_BUFFER_DATA (buffer) = data;
  }
  GST_BUFFER_SIZE (buffer) = len;

  /* remove any padding */
  GST_RTP_HEADER_PADDING (data) = FALSE;
}

/**
 * gst_rtp_buffer_get_packet_len:
 * @buffer: the buffer
 *
 * Return the total length of the packet in @buffer.
 *
 * Returns: The total length of the packet in @buffer.
 */
guint
gst_rtp_buffer_get_packet_len (GstBuffer * buffer)
{
  return GST_BUFFER_SIZE (buffer);
}

/**
 * gst_rtp_buffer_get_header_len:
 * @buffer: the buffer
 *
 * Return the total length of the header in @buffer. This include the length of
 * the fixed header, the CSRC list and the extension header.
 *
 * Returns: The total length of the header in @buffer.
 */
guint
gst_rtp_buffer_get_header_len (GstBuffer * buffer)
{
  guint len;
  guint8 *data;

  data = GST_BUFFER_DATA (buffer);

  len = GST_RTP_HEADER_LEN + GST_RTP_HEADER_CSRC_SIZE (data);
  if (GST_RTP_HEADER_EXTENSION (data))
    len += GST_READ_UINT16_BE (data + len + 2) * 4 + 4;

  return len;
}

/**
 * gst_rtp_buffer_get_version:
 * @buffer: the buffer
 *
 * Get the version number of the RTP packet in @buffer.
 *
 * Returns: The version of @buffer.
 */
guint8
gst_rtp_buffer_get_version (GstBuffer * buffer)
{
  return GST_RTP_HEADER_VERSION (GST_BUFFER_DATA (buffer));
}

/**
 * gst_rtp_buffer_set_version:
 * @buffer: the buffer
 * @version: the new version
 *
 * Set the version of the RTP packet in @buffer to @version.
 */
void
gst_rtp_buffer_set_version (GstBuffer * buffer, guint8 version)
{
  g_return_if_fail (version < 0x04);

  GST_RTP_HEADER_VERSION (GST_BUFFER_DATA (buffer)) = version;
}

/**
 * gst_rtp_buffer_get_padding:
 * @buffer: the buffer
 *
 * Check if the padding bit is set on the RTP packet in @buffer.
 *
 * Returns: TRUE if @buffer has the padding bit set.
 */
gboolean
gst_rtp_buffer_get_padding (GstBuffer * buffer)
{
  return GST_RTP_HEADER_PADDING (GST_BUFFER_DATA (buffer));
}

/**
 * gst_rtp_buffer_set_padding:
 * @buffer: the buffer
 * @padding: the new padding
 *
 * Set the padding bit on the RTP packet in @buffer to @padding.
 */
void
gst_rtp_buffer_set_padding (GstBuffer * buffer, gboolean padding)
{
  GST_RTP_HEADER_PADDING (GST_BUFFER_DATA (buffer)) = padding;
}

/**
 * gst_rtp_buffer_pad_to:
 * @buffer: the buffer
 * @len: the new amount of padding
 *
 * Set the amount of padding in the RTP packet in @buffer to
 * @len. If @len is 0, the padding is removed.
 *
 * NOTE: This function does not work correctly.
 */
void
gst_rtp_buffer_pad_to (GstBuffer * buffer, guint len)
{
  guint8 *data;

  data = GST_BUFFER_DATA (buffer);

  if (len > 0)
    GST_RTP_HEADER_PADDING (data) = TRUE;
  else
    GST_RTP_HEADER_PADDING (data) = FALSE;

  /* FIXME, set the padding byte at the end of the payload data */
}

/**
 * gst_rtp_buffer_get_extension:
 * @buffer: the buffer
 *
 * Check if the extension bit is set on the RTP packet in @buffer.
 * 
 * Returns: TRUE if @buffer has the extension bit set.
 */
gboolean
gst_rtp_buffer_get_extension (GstBuffer * buffer)
{
  return GST_RTP_HEADER_EXTENSION (GST_BUFFER_DATA (buffer));
}

/**
 * gst_rtp_buffer_set_extension:
 * @buffer: the buffer
 * @extension: the new extension
 *
 * Set the extension bit on the RTP packet in @buffer to @extension.
 */
void
gst_rtp_buffer_set_extension (GstBuffer * buffer, gboolean extension)
{
  GST_RTP_HEADER_EXTENSION (GST_BUFFER_DATA (buffer)) = extension;
}

/**
 * gst_rtp_buffer_get_extension_data:
 * @buffer: the buffer
 * @bits: location for result bits
 * @data: location for data
 * @wordlen: location for length of @data in 32 bits words
 *
 * Get the extension data. @bits will contain the extension 16 bits of custom
 * data. @data will point to the data in the extension and @wordlen will contain
 * the length of @data in 32 bits words.
 *
 * If @buffer did not contain an extension, this function will return %FALSE
 * with @bits, @data and @wordlen unchanged.
 * 
 * Returns: TRUE if @buffer had the extension bit set.
 *
 * Since: 0.10.15
 */
gboolean
gst_rtp_buffer_get_extension_data (GstBuffer * buffer, guint16 * bits,
    gpointer * data, guint * wordlen)
{
  guint len;
  guint8 *pdata;

  pdata = GST_BUFFER_DATA (buffer);

  if (!GST_RTP_HEADER_EXTENSION (pdata))
    return FALSE;

  /* move to the extension */
  len = GST_RTP_HEADER_LEN + GST_RTP_HEADER_CSRC_SIZE (pdata);
  pdata += len;

  if (bits)
    *bits = GST_READ_UINT16_BE (pdata);
  if (wordlen)
    *wordlen = GST_READ_UINT16_BE (pdata + 2);
  if (data)
    *data = pdata + 4;

  return TRUE;
}

/**
 * gst_rtp_buffer_set_extension_data:
 * @buffer: the buffer
 * @bits: the bits specific for the extension
 * @length: the length that counts the number of 32-bit words in
 * the extension, excluding the extension header ( therefore zero is a valid length)
 *
 * Set the extension bit of the rtp buffer and fill in the @bits and @length of the
 * extension header. It will refuse to set the extension data if the buffer is not
 * large enough.
 *
 * Returns: True if done.
 *
 * Since: 0.10.18
 */
gboolean
gst_rtp_buffer_set_extension_data (GstBuffer * buffer, guint16 bits,
    guint16 length)
{
  guint32 min_size = 0;
  guint8 *data;

  data = GST_BUFFER_DATA (buffer);

  /* check if the buffer is big enough to hold the extension */
  min_size =
      GST_RTP_HEADER_LEN + GST_RTP_HEADER_CSRC_SIZE (data) + 4 +
      length * sizeof (guint32);
  if (G_UNLIKELY (min_size > GST_BUFFER_SIZE (buffer)))
    goto too_small;

  /* now we can set the extension bit */
  gst_rtp_buffer_set_extension (buffer, TRUE);

  data += GST_RTP_HEADER_LEN + GST_RTP_HEADER_CSRC_SIZE (data);
  GST_WRITE_UINT16_BE (data, bits);
  GST_WRITE_UINT16_BE (data + 2, length);

  return TRUE;

  /* ERRORS */
too_small:
  {
    g_warning
        ("rtp buffer too small: need more than %d bytes but only have %d bytes",
        min_size, GST_BUFFER_SIZE (buffer));
    return FALSE;
  }
}

/**
 * gst_rtp_buffer_get_ssrc:
 * @buffer: the buffer
 *
 * Get the SSRC of the RTP packet in @buffer.
 * 
 * Returns: the SSRC of @buffer in host order.
 */
guint32
gst_rtp_buffer_get_ssrc (GstBuffer * buffer)
{
  return g_ntohl (GST_RTP_HEADER_SSRC (GST_BUFFER_DATA (buffer)));
}

/**
 * gst_rtp_buffer_list_get_ssrc:
 * @list: the buffer list
 *
 * Get the SSRC of the first RTP packet in @list.
 * All RTP packets within @list have the same SSRC.
 *
 * Returns: the SSRC of @list in host order.
 *
 * Since: 0.10.24
 */
guint32
gst_rtp_buffer_list_get_ssrc (GstBufferList * list)
{
  GstBuffer *buffer;

  buffer = gst_buffer_list_get (list, 0, 0);
  g_return_val_if_fail (buffer != NULL, 0);

  return g_ntohl (GST_RTP_HEADER_SSRC (GST_BUFFER_DATA (buffer)));
}

/**
 * gst_rtp_buffer_set_ssrc:
 * @buffer: the buffer
 * @ssrc: the new SSRC
 *
 * Set the SSRC on the RTP packet in @buffer to @ssrc.
 */
void
gst_rtp_buffer_set_ssrc (GstBuffer * buffer, guint32 ssrc)
{
  GST_RTP_HEADER_SSRC (GST_BUFFER_DATA (buffer)) = g_htonl (ssrc);
}

static GstBufferListItem
set_ssrc_header (GstBuffer ** buffer, guint group, guint idx, guint32 * ssrc)
{
  GST_RTP_HEADER_SSRC (GST_BUFFER_DATA (*buffer)) = g_htonl (*ssrc);
  return GST_BUFFER_LIST_SKIP_GROUP;
}

/**
 * gst_rtp_buffer_list_set_ssrc:
 * @list: the buffer list
 * @ssrc: the new SSRC
 *
 * Set the SSRC on each RTP packet in @list to @ssrc.
 *
 * Since: 0.10.24
 */
void
gst_rtp_buffer_list_set_ssrc (GstBufferList * list, guint32 ssrc)
{
  gst_buffer_list_foreach (list, (GstBufferListFunc) set_ssrc_header, &ssrc);
}

/**
 * gst_rtp_buffer_get_csrc_count:
 * @buffer: the buffer
 *
 * Get the CSRC count of the RTP packet in @buffer.
 * 
 * Returns: the CSRC count of @buffer.
 */
guint8
gst_rtp_buffer_get_csrc_count (GstBuffer * buffer)
{
  return GST_RTP_HEADER_CSRC_COUNT (GST_BUFFER_DATA (buffer));
}

/**
 * gst_rtp_buffer_get_csrc:
 * @buffer: the buffer
 * @idx: the index of the CSRC to get
 *
 * Get the CSRC at index @idx in @buffer.
 * 
 * Returns: the CSRC at index @idx in host order.
 */
guint32
gst_rtp_buffer_get_csrc (GstBuffer * buffer, guint8 idx)
{
  guint8 *data;

  data = GST_BUFFER_DATA (buffer);

  g_return_val_if_fail (idx < GST_RTP_HEADER_CSRC_COUNT (data), 0);

  return GST_READ_UINT32_BE (GST_RTP_HEADER_CSRC_LIST_OFFSET (data, idx));
}

/**
 * gst_rtp_buffer_set_csrc:
 * @buffer: the buffer
 * @idx: the CSRC index to set
 * @csrc: the CSRC in host order to set at @idx
 *
 * Modify the CSRC at index @idx in @buffer to @csrc.
 */
void
gst_rtp_buffer_set_csrc (GstBuffer * buffer, guint8 idx, guint32 csrc)
{
  guint8 *data;

  data = GST_BUFFER_DATA (buffer);

  g_return_if_fail (idx < GST_RTP_HEADER_CSRC_COUNT (data));

  GST_WRITE_UINT32_BE (GST_RTP_HEADER_CSRC_LIST_OFFSET (data, idx), csrc);
}

/**
 * gst_rtp_buffer_get_marker:
 * @buffer: the buffer
 *
 * Check if the marker bit is set on the RTP packet in @buffer.
 *
 * Returns: TRUE if @buffer has the marker bit set.
 */
gboolean
gst_rtp_buffer_get_marker (GstBuffer * buffer)
{
  return GST_RTP_HEADER_MARKER (GST_BUFFER_DATA (buffer));
}

/**
 * gst_rtp_buffer_set_marker:
 * @buffer: the buffer
 * @marker: the new marker
 *
 * Set the marker bit on the RTP packet in @buffer to @marker.
 */
void
gst_rtp_buffer_set_marker (GstBuffer * buffer, gboolean marker)
{
  GST_RTP_HEADER_MARKER (GST_BUFFER_DATA (buffer)) = marker;
}

/**
 * gst_rtp_buffer_get_payload_type:
 * @buffer: the buffer
 *
 * Get the payload type of the RTP packet in @buffer.
 *
 * Returns: The payload type.
 */
guint8
gst_rtp_buffer_get_payload_type (GstBuffer * buffer)
{
  return GST_RTP_HEADER_PAYLOAD_TYPE (GST_BUFFER_DATA (buffer));
}

/**
 * gst_rtp_buffer_list_get_payload_type:
 * @list: the buffer list
 *
 * Get the payload type of the first RTP packet in @list.
 * All packets in @list should have the same payload type.
 *
 * Returns: The payload type.
 *
 * Since: 0.10.24
 */
guint8
gst_rtp_buffer_list_get_payload_type (GstBufferList * list)
{
  GstBuffer *buffer;

  buffer = gst_buffer_list_get (list, 0, 0);
  g_return_val_if_fail (buffer != NULL, 0);

  return GST_RTP_HEADER_PAYLOAD_TYPE (GST_BUFFER_DATA (buffer));
}

/**
 * gst_rtp_buffer_set_payload_type:
 * @buffer: the buffer
 * @payload_type: the new type
 *
 * Set the payload type of the RTP packet in @buffer to @payload_type.
 */
void
gst_rtp_buffer_set_payload_type (GstBuffer * buffer, guint8 payload_type)
{
  g_return_if_fail (payload_type < 0x80);

  GST_RTP_HEADER_PAYLOAD_TYPE (GST_BUFFER_DATA (buffer)) = payload_type;
}

static GstBufferListItem
set_pt_header (GstBuffer ** buffer, guint group, guint idx, guint8 * pt)
{
  GST_RTP_HEADER_PAYLOAD_TYPE (GST_BUFFER_DATA (*buffer)) = *pt;
  return GST_BUFFER_LIST_SKIP_GROUP;
}

/**
 * gst_rtp_buffer_list_set_payload_type:
 * @list: the buffer list
 * @payload_type: the new type
 *
 * Set the payload type of each RTP packet in @list to @payload_type.
 *
 * Since: 0.10.24
 */
void
gst_rtp_buffer_list_set_payload_type (GstBufferList * list, guint8 payload_type)
{
  g_return_if_fail (payload_type < 0x80);

  gst_buffer_list_foreach (list, (GstBufferListFunc) set_pt_header,
      &payload_type);
}

/**
 * gst_rtp_buffer_get_seq:
 * @buffer: the buffer
 *
 * Get the sequence number of the RTP packet in @buffer.
 *
 * Returns: The sequence number in host order.
 */
guint16
gst_rtp_buffer_get_seq (GstBuffer * buffer)
{
  return g_ntohs (GST_RTP_HEADER_SEQ (GST_BUFFER_DATA (buffer)));
}

/**
 * gst_rtp_buffer_set_seq:
 * @buffer: the buffer
 * @seq: the new sequence number
 *
 * Set the sequence number of the RTP packet in @buffer to @seq.
 */
void
gst_rtp_buffer_set_seq (GstBuffer * buffer, guint16 seq)
{
  GST_RTP_HEADER_SEQ (GST_BUFFER_DATA (buffer)) = g_htons (seq);
}

static GstBufferListItem
set_seq_header (GstBuffer ** buffer, guint group, guint idx, guint16 * seq)
{
  GST_RTP_HEADER_SEQ (GST_BUFFER_DATA (*buffer)) = g_htons (*seq);
  (*seq)++;
  return GST_BUFFER_LIST_SKIP_GROUP;
}

/**
 * gst_rtp_buffer_list_set_seq:
 * @list: the buffer list
 * @seq: the new sequence number
 *
 * Set the sequence number of each RTP packet in @list to @seq.
 *
 * Returns: The seq number of the last packet in the list + 1.
 *
 * Since: 0.10.24
 */
guint16
gst_rtp_buffer_list_set_seq (GstBufferList * list, guint16 seq)
{
  gst_buffer_list_foreach (list, (GstBufferListFunc) set_seq_header, &seq);
  return seq;
}

/**
 * gst_rtp_buffer_list_get_seq:
 * @list: the buffer list
 *
 * Get the sequence number of the first RTP packet in @list.
 * All packets within @list have the same sequence number.
 *
 * Returns: The seq number
 *
 * Since: 0.10.24
 */
guint16
gst_rtp_buffer_list_get_seq (GstBufferList * list)
{
  GstBuffer *buffer;

  buffer = gst_buffer_list_get (list, 0, 0);
  g_return_val_if_fail (buffer != NULL, 0);

  return g_ntohl (GST_RTP_HEADER_SEQ (GST_BUFFER_DATA (buffer)));
}


/**
 * gst_rtp_buffer_get_timestamp:
 * @buffer: the buffer
 *
 * Get the timestamp of the RTP packet in @buffer.
 *
 * Returns: The timestamp in host order.
 */
guint32
gst_rtp_buffer_get_timestamp (GstBuffer * buffer)
{
  return g_ntohl (GST_RTP_HEADER_TIMESTAMP (GST_BUFFER_DATA (buffer)));
}

/**
 * gst_rtp_buffer_list_get_timestamp:
 * @list: the buffer list
 *
 * Get the timestamp of the first RTP packet in @list.
 * All packets within @list have the same timestamp.
 *
 * Returns: The timestamp in host order.
 *
 * Since: 0.10.24
 */
guint32
gst_rtp_buffer_list_get_timestamp (GstBufferList * list)
{
  GstBuffer *buffer;

  buffer = gst_buffer_list_get (list, 0, 0);
  g_return_val_if_fail (buffer != NULL, 0);

  return g_ntohl (GST_RTP_HEADER_TIMESTAMP (GST_BUFFER_DATA (buffer)));
}

/**
 * gst_rtp_buffer_set_timestamp:
 * @buffer: the buffer
 * @timestamp: the new timestamp
 *
 * Set the timestamp of the RTP packet in @buffer to @timestamp.
 */
void
gst_rtp_buffer_set_timestamp (GstBuffer * buffer, guint32 timestamp)
{
  GST_RTP_HEADER_TIMESTAMP (GST_BUFFER_DATA (buffer)) = g_htonl (timestamp);
}


static GstBufferListItem
set_timestamp_header (GstBuffer ** buffer, guint group, guint idx,
    guint32 * timestamp)
{
  GST_RTP_HEADER_TIMESTAMP (GST_BUFFER_DATA (*buffer)) = g_htonl (*timestamp);
  return GST_BUFFER_LIST_SKIP_GROUP;
}

/**
 * gst_rtp_buffer_list_set_timestamp:
 * @list: the buffer list
 * @timestamp: the new timestamp
 *
 * Set the timestamp of each RTP packet in @list to @timestamp.
 *
 * Since: 0.10.24
 */
void
gst_rtp_buffer_list_set_timestamp (GstBufferList * list, guint32 timestamp)
{
  gst_buffer_list_foreach (list, (GstBufferListFunc) set_timestamp_header,
      &timestamp);
}

/**
 * gst_rtp_buffer_get_payload_subbuffer:
 * @buffer: the buffer
 * @offset: the offset in the payload
 * @len: the length in the payload
 *
 * Create a subbuffer of the payload of the RTP packet in @buffer. @offset bytes
 * are skipped in the payload and the subbuffer will be of size @len.
 * If @len is -1 the total payload starting from @offset if subbuffered.
 *
 * Returns: A new buffer with the specified data of the payload.
 *
 * Since: 0.10.10
 */
GstBuffer *
gst_rtp_buffer_get_payload_subbuffer (GstBuffer * buffer, guint offset,
    guint len)
{
  guint poffset, plen;

  plen = gst_rtp_buffer_get_payload_len (buffer);
  /* we can't go past the length */
  if (G_UNLIKELY (offset >= plen))
    goto wrong_offset;

  /* apply offset */
  poffset = gst_rtp_buffer_get_header_len (buffer) + offset;
  plen -= offset;

  /* see if we need to shrink the buffer based on @len */
  if (len != -1 && len < plen)
    plen = len;

  return gst_buffer_create_sub (buffer, poffset, plen);

  /* ERRORS */
wrong_offset:
  {
    g_warning ("offset=%u should be less then plen=%u", offset, plen);
    return NULL;
  }
}

/**
 * gst_rtp_buffer_get_payload_buffer:
 * @buffer: the buffer
 *
 * Create a buffer of the payload of the RTP packet in @buffer. This function
 * will internally create a subbuffer of @buffer so that a memcpy can be
 * avoided.
 *
 * Returns: A new buffer with the data of the payload.
 */
GstBuffer *
gst_rtp_buffer_get_payload_buffer (GstBuffer * buffer)
{
  return gst_rtp_buffer_get_payload_subbuffer (buffer, 0, -1);
}

/**
 * gst_rtp_buffer_get_payload_len:
 * @buffer: the buffer
 *
 * Get the length of the payload of the RTP packet in @buffer.
 *
 * Returns: The length of the payload in @buffer.
 */
guint
gst_rtp_buffer_get_payload_len (GstBuffer * buffer)
{
  guint len, size;
  guint8 *data;

  size = GST_BUFFER_SIZE (buffer);
  data = GST_BUFFER_DATA (buffer);

  len = size - gst_rtp_buffer_get_header_len (buffer);

  if (GST_RTP_HEADER_PADDING (data))
    len -= data[size - 1];

  return len;
}

/**
 * gst_rtp_buffer_list_get_payload_len:
 * @list: the buffer list
 *
 * Get the length of the payload of the RTP packet in @list.
 *
 * Returns: The length of the payload in @list.
 *
 * Since: 0.10.24
 */
guint
gst_rtp_buffer_list_get_payload_len (GstBufferList * list)
{
  guint len;
  GstBufferListIterator *it;

  it = gst_buffer_list_iterate (list);
  len = 0;

  while (gst_buffer_list_iterator_next_group (it)) {
    guint i;
    GstBuffer *buf;

    i = 0;
    while ((buf = gst_buffer_list_iterator_next (it))) {
      /* skip the RTP header */
      if (!i++)
        continue;
      /* take the size of the current buffer */
      len += GST_BUFFER_SIZE (buf);
    }
  }

  gst_buffer_list_iterator_free (it);

  return len;
}

/**
 * gst_rtp_buffer_get_payload:
 * @buffer: the buffer
 *
 * Get a pointer to the payload data in @buffer. This pointer is valid as long
 * as a reference to @buffer is held.
 *
 * Returns: A pointer to the payload data in @buffer.
 */
gpointer
gst_rtp_buffer_get_payload (GstBuffer * buffer)
{
  return GST_BUFFER_DATA (buffer) + gst_rtp_buffer_get_header_len (buffer);
}

/**
 * gst_rtp_buffer_default_clock_rate:
 * @payload_type: the static payload type
 *
 * Get the default clock-rate for the static payload type @payload_type.
 *
 * Returns: the default clock rate or -1 if the payload type is not static or
 * the clock-rate is undefined.
 *
 * Since: 0.10.13
 */
guint32
gst_rtp_buffer_default_clock_rate (guint8 payload_type)
{
  const GstRTPPayloadInfo *info;
  guint32 res;

  info = gst_rtp_payload_info_for_pt (payload_type);
  if (!info)
    return -1;

  res = info->clock_rate;
  /* 0 means unknown so we have to return -1 from this function */
  if (res == 0)
    res = -1;

  return res;
}

/**
 * gst_rtp_buffer_compare_seqnum:
 * @seqnum1: a sequence number
 * @seqnum2: a sequence number
 *
 * Compare two sequence numbers, taking care of wraparounds. This function
 * returns the difference between @seqnum1 and @seqnum2.
 *
 * Returns: a negative value if @seqnum1 is bigger than @seqnum2, 0 if they
 * are equal or a positive value if @seqnum1 is smaller than @segnum2.
 *
 * Since: 0.10.15
 */
gint
gst_rtp_buffer_compare_seqnum (guint16 seqnum1, guint16 seqnum2)
{
  return (gint16) (seqnum2 - seqnum1);
}

/**
 * gst_rtp_buffer_ext_timestamp:
 * @exttimestamp: a previous extended timestamp
 * @timestamp: a new timestamp
 *
 * Update the @exttimestamp field with @timestamp. For the first call of the
 * method, @exttimestamp should point to a location with a value of -1.
 *
 * This function makes sure that the returned value is a constantly increasing
 * value even in the case where there is a timestamp wraparound.
 *
 * Returns: The extended timestamp of @timestamp.
 *
 * Since: 0.10.15
 */
guint64
gst_rtp_buffer_ext_timestamp (guint64 * exttimestamp, guint32 timestamp)
{
  guint64 result, diff, ext;

  g_return_val_if_fail (exttimestamp != NULL, -1);

  ext = *exttimestamp;

  if (ext == -1) {
    result = timestamp;
  } else {
    /* pick wraparound counter from previous timestamp and add to new timestamp */
    result = timestamp + (ext & ~(G_GINT64_CONSTANT (0xffffffff)));

    /* check for timestamp wraparound */
    if (result < ext)
      diff = ext - result;
    else
      diff = result - ext;

    if (diff > G_MAXINT32) {
      /* timestamp went backwards more than allowed, we wrap around and get
       * updated extended timestamp. */
      result += (G_GINT64_CONSTANT (1) << 32);
    }
  }
  *exttimestamp = result;

  return result;
}

/**
 * gst_rtp_buffer_get_extension_onebyte_header:
 * @buffer: the buffer
 * @id: The ID of the header extension to be read (between 1 and 14).
 * @nth: Read the nth extension packet with the requested ID
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Parses RFC 5285 style header extensions with a one byte header. It will
 * return the nth extension with the requested id.
 *
 * Returns: TRUE if @buffer had the requested header extension
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_get_extension_onebyte_header (GstBuffer * buffer, guint8 id,
    guint nth, gpointer * data, guint * size)
{
  guint16 bits;
  guint8 *pdata;
  guint wordlen;
  gulong offset = 0;
  guint count = 0;

  g_return_val_if_fail (id > 0 && id < 15, FALSE);

  if (!gst_rtp_buffer_get_extension_data (buffer, &bits, (gpointer) & pdata,
          &wordlen))
    return FALSE;

  if (bits != 0xBEDE)
    return FALSE;

  for (;;) {
    guint8 read_id, read_len;

    if (offset + 1 >= wordlen * 4)
      break;

    read_id = GST_READ_UINT8 (pdata + offset) >> 4;
    read_len = (GST_READ_UINT8 (pdata + offset) & 0x0F) + 1;
    offset += 1;

    /* ID 0 means its padding, skip */
    if (read_id == 0)
      continue;

    /* ID 15 is special and means we should stop parsing */
    if (read_id == 15)
      break;

    /* Ignore extension headers where the size does not fit */
    if (offset + read_len > wordlen * 4)
      break;

    /* If we have the right one */
    if (id == read_id) {
      if (nth == count) {
        if (data)
          *data = pdata + offset;
        if (size)
          *size = read_len;

        return TRUE;
      }

      count++;
    }
    offset += read_len;

    if (offset >= wordlen * 4)
      break;
  }

  return FALSE;
}

/**
 * gst_rtp_buffer_get_extension_twobytes_header:
 * @buffer: the buffer
 * @appbits: Application specific bits
 * @id: The ID of the header extension to be read (between 1 and 14).
 * @nth: Read the nth extension packet with the requested ID
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Parses RFC 5285 style header extensions with a two bytes header. It will
 * return the nth extension with the requested id.
 *
 * Returns: TRUE if @buffer had the requested header extension
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_get_extension_twobytes_header (GstBuffer * buffer,
    guint8 * appbits, guint8 id, guint nth, gpointer * data, guint * size)
{
  guint16 bits;
  guint8 *pdata;
  guint wordlen;
  guint bytelen;
  gulong offset = 0;
  guint count = 0;

  if (!gst_rtp_buffer_get_extension_data (buffer, &bits, (gpointer) & pdata,
          &wordlen))
    return FALSE;

  if (bits >> 4 != 0x100)
    return FALSE;

  bytelen = wordlen * 4;

  for (;;) {
    guint8 read_id, read_len;

    if (offset + 2 >= bytelen)
      break;

    read_id = GST_READ_UINT8 (pdata + offset);
    offset += 1;

    if (read_id == 0)
      continue;

    read_len = GST_READ_UINT8 (pdata + offset);
    offset += 1;

    /* Ignore extension headers where the size does not fit */
    if (offset + read_len > bytelen)
      break;

    /* If we have the right one, return it */
    if (id == read_id) {
      if (nth == count) {
        if (data)
          *data = pdata + offset;
        if (size)
          *size = read_len;
        if (appbits)
          *appbits = bits;

        return TRUE;
      }

      count++;
    }
    offset += read_len;
  }

  return FALSE;
}

static guint
get_onebyte_header_end_offset (guint8 * pdata, guint wordlen)
{
  guint offset = 0;
  guint bytelen = wordlen * 4;
  guint paddingcount = 0;

  while (offset + 1 < bytelen) {
    guint8 read_id, read_len;

    read_id = GST_READ_UINT8 (pdata + offset) >> 4;
    read_len = (GST_READ_UINT8 (pdata + offset) & 0x0F) + 1;
    offset += 1;

    /* ID 0 means its padding, skip */
    if (read_id == 0) {
      paddingcount++;
      continue;
    }

    paddingcount = 0;

    /* ID 15 is special and means we should stop parsing */
    /* It also means we can't add an extra packet */
    if (read_id == 15)
      return 0;

    /* Ignore extension headers where the size does not fit */
    if (offset + read_len > bytelen)
      return 0;

    offset += read_len;
  }

  return offset - paddingcount;
}

/**
 * gst_rtp_buffer_add_extension_onebyte_header:
 * @buffer: the buffer
 * @id: The ID of the header extension (between 1 and 14).
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Adds a RFC 5285 header extension with a one byte header to the end of the
 * RTP header. If there is already a RFC 5285 header extension with a one byte
 * header, the new extension will be appended.
 * It will not work if there is already a header extension that does not follow
 * the mecanism described in RFC 5285 or if there is a header extension with
 * a two bytes header as described in RFC 5285. In that case, use
 * gst_rtp_buffer_add_extension_twobytes_header()
 *
 * Returns: %TRUE if header extension could be added
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_add_extension_onebyte_header (GstBuffer * buffer, guint8 id,
    gpointer data, guint size)
{
  guint16 bits;
  guint8 *pdata;
  guint wordlen;
  gboolean has_bit;

  g_return_val_if_fail (id > 0 && id < 15, FALSE);
  g_return_val_if_fail (size >= 1 && size <= 16, FALSE);
  g_return_val_if_fail (gst_buffer_is_writable (buffer), FALSE);

  has_bit = gst_rtp_buffer_get_extension_data (buffer, &bits,
      (gpointer) & pdata, &wordlen);

  if (has_bit) {
    gulong offset = 0;
    guint8 *nextext;
    guint extlen;

    if (bits != 0xBEDE)
      return FALSE;

    offset = get_onebyte_header_end_offset (pdata, wordlen);
    if (offset == 0)
      return FALSE;

    nextext = pdata + offset;
    offset = nextext - GST_BUFFER_DATA (buffer);

    /* Don't add extra header if there isn't enough space */
    if (GST_BUFFER_SIZE (buffer) < offset + size + 1)
      return FALSE;

    nextext[0] = (id << 4) | (0x0F & (size - 1));
    memcpy (nextext + 1, data, size);

    extlen = nextext - pdata + size + 1;
    if (extlen % 4) {
      wordlen = extlen / 4 + 1;
      memset (nextext + size + 1, 0, 4 - extlen % 4);
    } else {
      wordlen = extlen / 4;
    }

    gst_rtp_buffer_set_extension_data (buffer, 0xBEDE, wordlen);
  } else {
    wordlen = (size + 1) / 4 + (((size + 1) % 4) ? 1 : 0);

    gst_rtp_buffer_set_extension_data (buffer, 0xBEDE, wordlen);

    gst_rtp_buffer_get_extension_data (buffer, &bits,
        (gpointer) & pdata, &wordlen);

    pdata[0] = (id << 4) | (0x0F & (size - 1));
    memcpy (pdata + 1, data, size);

    if ((size + 1) % 4)
      memset (pdata + size + 1, 0, 4 - ((size + 1) % 4));
  }

  return TRUE;
}


static guint
get_twobytes_header_end_offset (guint8 * pdata, guint wordlen)
{
  guint offset = 0;
  guint bytelen = wordlen * 4;
  guint paddingcount = 0;

  while (offset + 2 < bytelen) {
    guint8 read_id, read_len;

    read_id = GST_READ_UINT8 (pdata + offset);
    offset += 1;

    /* ID 0 means its padding, skip */
    if (read_id == 0) {
      paddingcount++;
      continue;
    }

    paddingcount = 0;

    read_len = GST_READ_UINT8 (pdata + offset);
    offset += 1;

    /* Ignore extension headers where the size does not fit */
    if (offset + read_len > bytelen)
      return 0;

    offset += read_len;
  }

  return offset - paddingcount;
}

/**
 * gst_rtp_buffer_add_extension_twobytes_header:
 * @buffer: the buffer
 * @appbits: Application specific bits
 * @id: The ID of the header extension
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Adds a RFC 5285 header extension with a two bytes header to the end of the
 * RTP header. If there is already a RFC 5285 header extension with a two bytes
 * header, the new extension will be appended.
 * It will not work if there is already a header extension that does not follow
 * the mecanism described in RFC 5285 or if there is a header extension with
 * a one byte header as described in RFC 5285. In that case, use
 * gst_rtp_buffer_add_extension_onebyte_header()
 *
 * Returns: %TRUE if header extension could be added
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_add_extension_twobytes_header (GstBuffer * buffer,
    guint8 appbits, guint8 id, gpointer data, guint size)
{
  guint16 bits;
  guint8 *pdata;
  guint wordlen;
  gboolean has_bit;

  g_return_val_if_fail ((appbits & 0xF0) == 0, FALSE);
  g_return_val_if_fail (size < 256, FALSE);
  g_return_val_if_fail (gst_buffer_is_writable (buffer), FALSE);

  has_bit = gst_rtp_buffer_get_extension_data (buffer, &bits,
      (gpointer) & pdata, &wordlen);

  if (has_bit) {
    gulong offset = 0;
    guint8 *nextext;
    guint extlen;

    if (bits != ((0x100 << 4) | (appbits & 0x0f)))
      return FALSE;

    offset = get_twobytes_header_end_offset (pdata, wordlen);

    nextext = pdata + offset;

    offset = nextext - GST_BUFFER_DATA (buffer);

    /* Don't add extra header if there isn't enough space */
    if (GST_BUFFER_SIZE (buffer) < offset + size + 2)
      return FALSE;

    nextext[0] = id;
    nextext[1] = size;
    memcpy (nextext + 2, data, size);

    extlen = nextext - pdata + size + 2;
    if (extlen % 4) {
      wordlen = extlen / 4 + 1;
      memset (nextext + size + 2, 0, 4 - extlen % 4);
    } else {
      wordlen = extlen / 4;
    }

    gst_rtp_buffer_set_extension_data (buffer, (0x100 << 4) | (appbits & 0x0F),
        wordlen);
  } else {
    wordlen = (size + 2) / 4 + (((size + 2) % 4) ? 1 : 0);

    gst_rtp_buffer_set_extension_data (buffer, (0x100 << 4) | (appbits & 0x0F),
        wordlen);

    gst_rtp_buffer_get_extension_data (buffer, &bits,
        (gpointer) & pdata, &wordlen);

    pdata[0] = id;
    pdata[1] = size;
    memcpy (pdata + 2, data, size);
    if ((size + 2) % 4)
      memset (pdata + size + 2, 0, 4 - ((size + 2) % 4));
  }

  return TRUE;
}

/**
 * gst_rtp_buffer_list_get_extension_onebyte_header:
 * @bufferlist: the bufferlist
 * @group_idx: The index of the group in the #GstBufferList
 * @id: The ID of the header extension to be read (between 1 and 14).
 * @nth: Read the nth extension packet with the requested ID
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Parses RFC 5285 style header extensions with a one byte header. It will
 * return the nth extension with the requested id.
 *
 * Returns: TRUE if @buffer had the requested header extension
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_list_get_extension_onebyte_header (GstBufferList * bufferlist,
    guint group_idx, guint8 id, guint nth, gpointer * data, guint * size)
{
  GstBuffer *buffer;

  buffer = gst_buffer_list_get (bufferlist, group_idx, 0);

  if (!buffer)
    return FALSE;

  return gst_rtp_buffer_get_extension_onebyte_header (buffer, id, nth, data,
      size);
}


/**
 * gst_rtp_buffer_list_get_extension_twobytes_header:
 * @bufferlist: the bufferlist
 * @group_idx: The index of the group in the #GstBufferList
 * @appbits: Application specific bits
 * @id: The ID of the header extension to be read (between 1 and 14).
 * @nth: Read the nth extension packet with the requested ID
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Parses RFC 5285 style header extensions with a two bytes header. It will
 * return the nth extension with the requested id.
 *
 * Returns: TRUE if @buffer had the requested header extension
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_list_get_extension_twobytes_header (GstBufferList * bufferlist,
    guint group_idx, guint8 * appbits, guint8 id, guint nth,
    gpointer * data, guint * size)
{
  GstBuffer *buffer;

  buffer = gst_buffer_list_get (bufferlist, group_idx, 0);

  if (!buffer)
    return FALSE;

  return gst_rtp_buffer_get_extension_twobytes_header (buffer, appbits, id,
      nth, data, size);
}

/**
 * gst_rtp_buffer_list_add_extension_onebyte_header:
 * @it: a #GstBufferListIterator pointing right after the #GstBuffer where
 * the header extension should be added
 * @id: The ID of the header extension (between 1 and 14).
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Adds a RFC 5285 header extension with a one byte header to the end of the
 * RTP header. If there is already a RFC 5285 header extension with a one byte
 * header, the new extension will be appended.
 * It will not work if there is already a header extension that does not follow
 * the mecanism described in RFC 5285 or if there is a header extension with
 * a two bytes header as described in RFC 5285. In that case, use
 * gst_rtp_buffer_list_add_extension_twobytes_header()
 *
 * This function will not modify the data section of the RTP buffer, only
 * the header.
 *
 * Returns: %TRUE if header extension could be added
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_list_add_extension_onebyte_header (GstBufferListIterator * it,
    guint8 id, gpointer data, guint size)
{
  GstBuffer *buffer;
  guint16 bits;
  guint8 *pdata;
  guint wordlen;
  gboolean retval;
  guint endoffset = 0;

  g_return_val_if_fail (gst_buffer_list_iterator_n_buffers (it) == 1, FALSE);
  g_return_val_if_fail (id > 0 && id < 15, FALSE);
  g_return_val_if_fail (size >= 1 && size <= 16, FALSE);

  buffer = gst_buffer_list_iterator_steal (it);

  if (GST_RTP_HEADER_EXTENSION (GST_BUFFER_DATA (buffer))) {
    gst_rtp_buffer_get_extension_data (buffer, &bits, (gpointer) & pdata,
        &wordlen);

    if (bits != 0xBEDE)
      return FALSE;

    endoffset = get_onebyte_header_end_offset (pdata, wordlen);
    if (endoffset == 0)
      return FALSE;
    endoffset += pdata - GST_BUFFER_DATA (buffer);
  } else {
    endoffset = GST_BUFFER_SIZE (buffer) + 4;
  }

  if (endoffset + size + 1 > GST_BUFFER_SIZE (buffer)) {
    guint newsize;
    GstBuffer *newbuffer;

    newsize = endoffset + size + 1;
    if (newsize % 4)
      newsize += 4 - (newsize % 4);
    newbuffer = gst_buffer_new_and_alloc (newsize);
    memcpy (GST_BUFFER_DATA (newbuffer), GST_BUFFER_DATA (buffer),
        GST_BUFFER_SIZE (buffer));
    gst_buffer_copy_metadata (newbuffer, buffer, GST_BUFFER_COPY_ALL);
    gst_buffer_unref (buffer);
    buffer = newbuffer;
  } else {
    buffer = gst_buffer_make_writable (buffer);
  }

  retval = gst_rtp_buffer_add_extension_onebyte_header (buffer, id, data, size);

  gst_buffer_list_iterator_take (it, buffer);

  return retval;
}

/**
 * gst_rtp_buffer_list_add_extension_twobytes_header:
 * @it: a #GstBufferListIterator pointing right after the #GstBuffer where
 * the header extension should be added
 * @appbits: Application specific bits
 * @id: The ID of the header extension
 * @data: location for data
 * @size: the size of the data in bytes
 *
 * Adds a RFC 5285 header extension with a two bytes header to the end of the
 * RTP header. If there is already a RFC 5285 header extension with a two bytes
 * header, the new extension will be appended.
 * It will not work if there is already a header extension that does not follow
 * the mecanism described in RFC 5285 or if there is a header extension with
 * a one byte header as described in RFC 5285. In that case, use
 * gst_rtp_buffer_add_extension_onebyte_header()
 *
 * This function will not modify the data section of the RTP buffer, only
 * the header.
 *
 * Returns: %TRUE if header extension could be added
 *
 * Since: 0.10.31
 */

gboolean
gst_rtp_buffer_list_add_extension_twobytes_header (GstBufferListIterator * it,
    guint8 appbits, guint8 id, gpointer data, guint size)
{
  GstBuffer *buffer;
  guint16 bits;
  guint8 *pdata;
  guint wordlen;
  gboolean retval;
  guint endoffset;

  g_return_val_if_fail ((appbits & 0xF0) == 0, FALSE);
  g_return_val_if_fail (size < 256, FALSE);
  g_return_val_if_fail (gst_buffer_list_iterator_n_buffers (it) == 1, FALSE);

  buffer = gst_buffer_list_iterator_steal (it);

  if (GST_RTP_HEADER_EXTENSION (GST_BUFFER_DATA (buffer))) {
    gst_rtp_buffer_get_extension_data (buffer, &bits, (gpointer) & pdata,
        &wordlen);

    if (bits != ((0x100 << 4) | (appbits & 0x0f)))
      return FALSE;

    endoffset = get_twobytes_header_end_offset (pdata, wordlen);
    if (endoffset == 0)
      return FALSE;
    endoffset += pdata - GST_BUFFER_DATA (buffer);
  } else {
    endoffset = GST_BUFFER_SIZE (buffer) + 4;
  }

  if (endoffset + size + 2 > GST_BUFFER_SIZE (buffer)) {
    guint newsize;
    GstBuffer *newbuffer;

    newsize = endoffset + size + 2;
    if (newsize % 4)
      newsize += 4 - newsize % 4;
    newbuffer = gst_buffer_new_and_alloc (newsize);
    memcpy (GST_BUFFER_DATA (newbuffer), GST_BUFFER_DATA (buffer),
        GST_BUFFER_SIZE (buffer));
    gst_buffer_copy_metadata (newbuffer, buffer, GST_BUFFER_COPY_ALL);
    gst_buffer_unref (buffer);
    buffer = newbuffer;
  } else {
    buffer = gst_buffer_make_writable (buffer);
  }

  retval = gst_rtp_buffer_add_extension_twobytes_header (buffer, appbits, id,
      data, size);

  gst_buffer_list_iterator_take (it, buffer);

  return retval;
}

/**
 * gst_rtp_buffer_list_from_buffer:
 * @buffer: a #GstBuffer containing a RTP packet
 *
 * Splits a #GstBuffer into a #GstBufferList containing separate
 * buffers for the header and data sections.
 *
 * Returns: a #GstBufferList
 */

GstBufferList *
gst_rtp_buffer_list_from_buffer (GstBuffer * buffer)
{
  GstBufferList *bufferlist;
  GstBuffer *sub;
  GstBufferListIterator *it;
  guint8 *payload;

  bufferlist = gst_buffer_list_new ();

  it = gst_buffer_list_iterate (bufferlist);
  gst_buffer_list_iterator_add_group (it);

  payload = gst_rtp_buffer_get_payload (buffer);
  sub = gst_buffer_create_sub (buffer, 0, payload - GST_BUFFER_DATA (buffer));
  gst_buffer_list_iterator_add (it, sub);

  sub = gst_rtp_buffer_get_payload_buffer (buffer);
  gst_buffer_list_iterator_add (it, sub);

  gst_buffer_list_iterator_free (it);

  return bufferlist;
}
