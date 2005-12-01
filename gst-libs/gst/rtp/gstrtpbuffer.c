/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org> 
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

#include "gstrtpbuffer.h"

#define GST_RTP_HEADER_LEN 12

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
  guint16 seq;                  /* sequence number */
  guint32 timestamp;            /* timestamp */
  guint32 ssrc;                 /* synchronization source */
  guint32 csrc[1];              /* optional CSRC list */
} GstRTPHeader;

#define GST_RTP_HEADER_VERSION(buf)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->version)
#define GST_RTP_HEADER_PADDING(buf)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->padding)
#define GST_RTP_HEADER_EXTENSION(buf)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->extension)
#define GST_RTP_HEADER_CSRC_COUNT(buf)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->csrc_count)
#define GST_RTP_HEADER_MARKER(buf)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->marker)
#define GST_RTP_HEADER_PAYLOAD_TYPE(buf)(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->payload_type)
#define GST_RTP_HEADER_SEQ(buf)		(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->seq)
#define GST_RTP_HEADER_TIMESTAMP(buf)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->timestamp)
#define GST_RTP_HEADER_SSRC(buf)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->ssrc)
#define GST_RTP_HEADER_CSRC(buf,i)	(((GstRTPHeader *)(GST_BUFFER_DATA (buf)))->csrc[i])

#define GST_RTP_HEADER_CSRC_SIZE(buf)	(GST_RTP_HEADER_CSRC_COUNT(buf) * sizeof (guint32))

void
gst_rtp_buffer_allocate_data (GstBuffer * buffer, guint payload_len,
    guint8 pad_len, guint8 csrc_count)
{
  guint len;

  g_return_if_fail (csrc_count <= 15);
  g_return_if_fail (GST_IS_BUFFER (buffer));

  len = GST_RTP_HEADER_LEN + csrc_count * sizeof (guint32)
      + payload_len + pad_len;

  GST_BUFFER_MALLOCDATA (buffer) = g_malloc (len);
  GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer);
  GST_BUFFER_SIZE (buffer) = len;

  /* fill in defaults */
  GST_RTP_HEADER_VERSION (buffer) = GST_RTP_VERSION;
  GST_RTP_HEADER_PADDING (buffer) = FALSE;
  GST_RTP_HEADER_EXTENSION (buffer) = FALSE;
  GST_RTP_HEADER_CSRC_COUNT (buffer) = 0;
  GST_RTP_HEADER_MARKER (buffer) = FALSE;
  GST_RTP_HEADER_PAYLOAD_TYPE (buffer) = 0;
  GST_RTP_HEADER_SEQ (buffer) = 0;
  GST_RTP_HEADER_TIMESTAMP (buffer) = 0;
  GST_RTP_HEADER_SSRC (buffer) = 0;
}

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

GstBuffer *
gst_rtp_buffer_new_copy_data (gpointer data, guint len)
{
  return gst_rtp_buffer_new_take_data (g_memdup (data, len), len);
}

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

GstBuffer *
gst_rtp_buffer_new_allocate_len (guint packet_len, guint8 pad_len,
    guint8 csrc_count)
{
  guint len;

  g_return_val_if_fail (csrc_count <= 15, NULL);

  len = gst_rtp_buffer_calc_payload_len (packet_len, pad_len, csrc_count);

  return gst_rtp_buffer_new_allocate (len, pad_len, csrc_count);
}

guint
gst_rtp_buffer_calc_header_len (guint8 csrc_count)
{
  g_return_val_if_fail (csrc_count <= 15, 0);

  return GST_RTP_HEADER_LEN + (csrc_count * sizeof (guint32));
}

guint
gst_rtp_buffer_calc_packet_len (guint payload_len, guint8 pad_len,
    guint8 csrc_count)
{
  g_return_val_if_fail (csrc_count <= 15, 0);

  return payload_len + GST_RTP_HEADER_LEN + (csrc_count * sizeof (guint32))
      + pad_len;
}

guint
gst_rtp_buffer_calc_payload_len (guint packet_len, guint8 pad_len,
    guint8 csrc_count)
{
  g_return_val_if_fail (csrc_count <= 15, 0);

  return packet_len - GST_RTP_HEADER_LEN - (csrc_count * sizeof (guint32))
      - pad_len;
}

gboolean
gst_rtp_buffer_validate_data (guint8 * data, guint len)
{
  guint8 padding;
  guint8 csrc_count;
  guint header_len;
  guint8 version;

  g_return_val_if_fail (data != NULL, FALSE);

  header_len = GST_RTP_HEADER_LEN;
  if (len < header_len) {
    GST_DEBUG ("len < header_len check failed (%d < %d)", len, header_len);
    return FALSE;
  }

  /* check version */
  version = (data[0] & 0xc0) >> 6;
  if (version != GST_RTP_VERSION) {
    GST_DEBUG ("version check failed (%d != %d)", version, GST_RTP_VERSION);
    return FALSE;
  }

  /* calc header length with csrc */
  csrc_count = (data[0] & 0x0f);
  header_len += csrc_count * sizeof (guint32);

  /* check for padding */
  if (data[0] & 0x40)
    padding = data[len - 1];
  else
    padding = 0;

  /* check if padding not bigger than packet and header */
  if (len - header_len <= padding) {
    GST_DEBUG ("padding check failed (%d - %d <= %d)",
        len, header_len, padding);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_rtp_buffer_validate (GstBuffer * buffer)
{
  guint8 *data;
  guint len;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);

  data = GST_BUFFER_DATA (buffer);
  len = GST_BUFFER_SIZE (buffer);

  return gst_rtp_buffer_validate_data (data, len);
}


void
gst_rtp_buffer_set_packet_len (GstBuffer * buffer, guint len)
{
  guint oldlen;

  g_return_if_fail (GST_IS_BUFFER (buffer));

  oldlen = GST_BUFFER_SIZE (buffer);

  if (oldlen < len) {
    guint8 *newdata;

    newdata = g_realloc (GST_BUFFER_MALLOCDATA (buffer), len);
    GST_BUFFER_MALLOCDATA (buffer) = newdata;
    GST_BUFFER_DATA (buffer) = newdata;
  }
  GST_BUFFER_SIZE (buffer) = len;

  /* remove any padding */
  GST_RTP_HEADER_PADDING (buffer) = FALSE;

}

guint
gst_rtp_buffer_get_packet_len (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);

  return GST_BUFFER_SIZE (buffer);
}

guint8
gst_rtp_buffer_get_version (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  return GST_RTP_HEADER_VERSION (buffer);
}

void
gst_rtp_buffer_set_version (GstBuffer * buffer, guint8 version)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (version < 0x04);
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  GST_RTP_HEADER_VERSION (buffer) = version;
}


gboolean
gst_rtp_buffer_get_padding (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, FALSE);

  return GST_RTP_HEADER_PADDING (buffer);
}

void
gst_rtp_buffer_set_padding (GstBuffer * buffer, gboolean padding)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  GST_RTP_HEADER_PADDING (buffer) = padding;
}

void
gst_rtp_buffer_pad_to (GstBuffer * buffer, guint len)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  if (len > 0)
    GST_RTP_HEADER_PADDING (buffer) = TRUE;
  else
    GST_RTP_HEADER_PADDING (buffer) = FALSE;
}


gboolean
gst_rtp_buffer_get_extension (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, FALSE);

  return GST_RTP_HEADER_EXTENSION (buffer);
}

void
gst_rtp_buffer_set_extension (GstBuffer * buffer, gboolean extension)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  GST_RTP_HEADER_EXTENSION (buffer) = extension;
}

guint32
gst_rtp_buffer_get_ssrc (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  return g_ntohl (GST_RTP_HEADER_SSRC (buffer));
}

void
gst_rtp_buffer_set_ssrc (GstBuffer * buffer, guint32 ssrc)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  GST_RTP_HEADER_SSRC (buffer) = g_htonl (ssrc);
}

guint8
gst_rtp_buffer_get_csrc_count (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  return GST_RTP_HEADER_CSRC_COUNT (buffer);
}

guint32
gst_rtp_buffer_get_csrc (GstBuffer * buffer, guint8 idx)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);
  g_return_val_if_fail (GST_RTP_HEADER_CSRC_COUNT (buffer) < idx, 0);

  return g_ntohl (GST_RTP_HEADER_CSRC (buffer, idx));
}

void
gst_rtp_buffer_set_csrc (GstBuffer * buffer, guint8 idx, guint32 csrc)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);
  g_return_if_fail (GST_RTP_HEADER_CSRC_COUNT (buffer) < idx);

  GST_RTP_HEADER_CSRC (buffer, idx) = g_htonl (csrc);
}

gboolean
gst_rtp_buffer_get_marker (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, FALSE);

  return GST_RTP_HEADER_MARKER (buffer);
}

void
gst_rtp_buffer_set_marker (GstBuffer * buffer, gboolean marker)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  GST_RTP_HEADER_MARKER (buffer) = marker;
}


guint8
gst_rtp_buffer_get_payload_type (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  return GST_RTP_HEADER_PAYLOAD_TYPE (buffer);
}

void
gst_rtp_buffer_set_payload_type (GstBuffer * buffer, guint8 payload_type)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);
  g_return_if_fail (payload_type < 0x80);

  GST_RTP_HEADER_PAYLOAD_TYPE (buffer) = payload_type;
}


guint16
gst_rtp_buffer_get_seq (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  return g_ntohs (GST_RTP_HEADER_SEQ (buffer));
}

void
gst_rtp_buffer_set_seq (GstBuffer * buffer, guint16 seq)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  GST_RTP_HEADER_SEQ (buffer) = g_htons (seq);
}


guint32
gst_rtp_buffer_get_timestamp (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  return g_ntohl (GST_RTP_HEADER_TIMESTAMP (buffer));
}

void
gst_rtp_buffer_set_timestamp (GstBuffer * buffer, guint32 timestamp)
{
  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_BUFFER_DATA (buffer) != NULL);

  GST_RTP_HEADER_TIMESTAMP (buffer) = g_htonl (timestamp);
}

GstBuffer *
gst_rtp_buffer_get_payload_buffer (GstBuffer * buffer)
{
  guint len;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  len = gst_rtp_buffer_get_payload_len (buffer);

  return gst_buffer_create_sub (buffer, GST_RTP_HEADER_LEN
      + GST_RTP_HEADER_CSRC_SIZE (buffer), len);
}

guint
gst_rtp_buffer_get_payload_len (GstBuffer * buffer)
{
  guint len;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), 0);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, 0);

  len = GST_BUFFER_SIZE (buffer)
      - GST_RTP_HEADER_LEN - GST_RTP_HEADER_CSRC_SIZE (buffer);

  if (GST_RTP_HEADER_PADDING (buffer))
    len -= ((guint8 *) GST_BUFFER_DATA (buffer))[GST_BUFFER_SIZE (buffer) - 1];

  return len;
}

gpointer
gst_rtp_buffer_get_payload (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (GST_BUFFER_DATA (buffer) != NULL, NULL);

  return GST_BUFFER_DATA (buffer) + GST_RTP_HEADER_LEN
      + GST_RTP_HEADER_CSRC_SIZE (buffer);
}
