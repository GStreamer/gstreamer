/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * dataprotocol.c: Functions implementing the GStreamer Data Protocol
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/dataprotocol/dataprotocol.h>
#include <glib/gprintf.h>       /* g_sprintf */
#include <string.h>             /* strlen */
#include "dp-private.h"

/* debug category */
GST_DEBUG_CATEGORY (data_protocol_debug);
#define GST_CAT_DEFAULT data_protocol_debug

/* calculate a CCITT 16 bit CRC check value for a given byte array */
/*
 * this code snippet is adapted from a web page I found
 * it is identical except for cleanups, and a final XOR with 0xffff
 * as outlined in the uecp spec
 *
 * XMODEM    x^16 + x^12 + x^5 + 1
 */

#define POLY       0x1021
#define CRC_INIT   0xFFFF

static guint16
gst_dp_crc (const guint8 * buffer, register guint length)
{
  static gboolean initialized = FALSE;
  static guint16 crc_register, crc_table[256];
  unsigned long i, j, k;

  if (!initialized) {
    for (i = 0; i < 256; i++) {
      j = i << 8;
      for (k = 8; k--;) {
        j = j & 0x8000 ? (j << 1) ^ POLY : j << 1;
      }

      crc_table[i] = (guint16) j;
    }
    initialized = TRUE;
  }

  crc_register = CRC_INIT;      /* always init register */

  /* calc CRC */
  for (; length--;) {
    crc_register = (guint16) ((crc_register << 8) ^
        crc_table[((crc_register >> 8) & 0x00ff) ^ *buffer++]);
  }
  return (0xffff ^ crc_register);
}

/* debugging function; dumps byte array values per 8 bytes */
/* FIXME: would be nice to merge this with gst_util_dump_mem () */
void
gst_dp_dump_byte_array (guint8 * array, guint length)
{
  int i;
  int n = 8;                    /* number of bytes per line */
  gchar *line = g_malloc (3 * n);

  GST_LOG ("dumping byte array of length %d", length);
  for (i = 0; i < length; ++i) {
    g_sprintf (line + 3 * (i % n), "%02x ", array[i]);
    if (i % n == (n - 1)) {
      GST_LOG ("%03d: %s", i - (n - 1), line);
    }
  }
  if (i % n != 0) {
    GST_LOG ("%03d: %s", (i / n) * n, line);
  }
  g_free (line);
}

/**
 * gst_dp_init:
 *
 * Initialize GStreamer Data Protocol library.
 *
 * Should be called before using these functions; either from source linking
 * to this source file or from plugin_init.
 */
void
gst_dp_init (void)
{
  static gboolean _gst_dp_initialized = FALSE;

  if (_gst_dp_initialized)
    return;

  _gst_dp_initialized = TRUE;

  GST_DEBUG_CATEGORY_INIT (data_protocol_debug, "gdp", 0,
      "GStreamer Data Protocol");
}

/*** PUBLIC FUNCTIONS ***/

/**
 * gst_dp_header_payload_length:
 * @header: the byte header of the packet array
 *
 * Returns: the length of the payload this header describes.
 */
guint32
gst_dp_header_payload_length (const guint8 * header)
{
  return GST_DP_HEADER_PAYLOAD_LENGTH (header);
}

/**
 * gst_dp_header_payload_type:
 * @header: the byte header of the packet array
 *
 * Returns: the #GstDPPayloadType the payload this header describes.
 */
GstDPPayloadType
gst_dp_header_payload_type (const guint8 * header)
{
  return GST_DP_HEADER_PAYLOAD_TYPE (header);
}

/**
 * gst_dp_header_from_buffer:
 * @buffer: a #GstBuffer to create a header for
 * @flags: the #GDPHeaderFlags to create the header with
 * @length: a guint pointer to store the header length in
 * @header: a guint8 * pointer to store a newly allocated header byte array in
 *
 * Creates a GDP header from the given buffer.
 *
 * Returns: %TRUE if the header was successfully created
 */

gboolean
gst_dp_header_from_buffer (const GstBuffer * buffer, GstDPHeaderFlag flags,
    guint * length, guint8 ** header)
{
  guint8 *h;
  guint16 crc;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), FALSE);
  g_return_val_if_fail (GST_BUFFER_REFCOUNT_VALUE (buffer) > 0, FALSE);
  g_return_val_if_fail (header, FALSE);

  *length = GST_DP_HEADER_LENGTH;
  h = g_malloc (GST_DP_HEADER_LENGTH);

  /* version, flags, type */
  h[0] = (guint8) GST_DP_VERSION_MAJOR;
  h[1] = (guint8) GST_DP_VERSION_MINOR;
  h[2] = (guint8) flags;
  h[3] = GST_DP_PAYLOAD_BUFFER;

  /* buffer properties */
  GST_WRITE_UINT32_BE (h + 4, GST_BUFFER_SIZE (buffer));
  GST_WRITE_UINT64_BE (h + 8, GST_BUFFER_TIMESTAMP (buffer));
  GST_WRITE_UINT64_BE (h + 16, GST_BUFFER_DURATION (buffer));
  GST_WRITE_UINT64_BE (h + 24, GST_BUFFER_OFFSET (buffer));
  GST_WRITE_UINT64_BE (h + 32, GST_BUFFER_OFFSET_END (buffer));

  /* ABI padding */
  GST_WRITE_UINT64_BE (h + 40, (guint64) 0);
  GST_WRITE_UINT64_BE (h + 48, (guint64) 0);

  /* CRC */
  crc = 0;
  if (flags & GST_DP_HEADER_FLAG_CRC_HEADER) {
    /* we don't crc the last four bytes of the header since they are crc's */
    crc = gst_dp_crc (h, 56);
  }
  GST_WRITE_UINT16_BE (h + 56, crc);

  crc = 0;
  if (flags & GST_DP_HEADER_FLAG_CRC_PAYLOAD) {
    crc = gst_dp_crc (GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer));
  }
  GST_WRITE_UINT16_BE (h + 58, crc);

  GST_LOG ("created header from buffer:");
  gst_dp_dump_byte_array (h, GST_DP_HEADER_LENGTH);
  *header = h;
  return TRUE;
}

 /**
 * gst_dp_packet_from_caps:
 * @caps: a #GstCaps to create a packet for
 * @flags: the #GDPHeaderFlags to create the header with
 * @length: a guint pointer to store the header length in
 * @header: a guint8 pointer to store a newly allocated header byte array in
 * @payload: a guint8 pointer to store a newly allocated payload byte array in
 *
 * Creates a GDP packet from the given caps.
 *
 * Returns: %TRUE if the packet was successfully created
 */
gboolean
gst_dp_packet_from_caps (const GstCaps * caps, GstDPHeaderFlag flags,
    guint * length, guint8 ** header, guint8 ** payload)
{
  guint8 *h;
  guint16 crc;
  gchar *string;

  /* FIXME: GST_IS_CAPS doesn't work
     g_return_val_if_fail (GST_IS_CAPS (caps), FALSE); */
  g_return_val_if_fail (caps, FALSE);
  g_return_val_if_fail (header, FALSE);
  g_return_val_if_fail (payload, FALSE);

  *length = GST_DP_HEADER_LENGTH;
  h = g_malloc (GST_DP_HEADER_LENGTH);

  string = gst_caps_to_string (caps);

  /* version, flags, type */
  h[0] = (guint8) GST_DP_VERSION_MAJOR;
  h[1] = (guint8) GST_DP_VERSION_MINOR;
  h[2] = (guint8) flags;
  h[3] = GST_DP_PAYLOAD_CAPS;

  /* buffer properties */
  GST_WRITE_UINT32_BE (h + 4, strlen (string) + 1);     /* include trailing 0 */
  GST_WRITE_UINT64_BE (h + 8, (guint64) 0);
  GST_WRITE_UINT64_BE (h + 16, (guint64) 0);
  GST_WRITE_UINT64_BE (h + 24, (guint64) 0);
  GST_WRITE_UINT64_BE (h + 32, (guint64) 0);

  /* ABI padding */
  GST_WRITE_UINT64_BE (h + 40, (guint64) 0);
  GST_WRITE_UINT64_BE (h + 48, (guint64) 0);

  /* CRC */
  crc = 0;
  if (flags & GST_DP_HEADER_FLAG_CRC_HEADER) {
    crc = gst_dp_crc (h, 56);
  }
  GST_WRITE_UINT16_BE (h + 56, crc);

  crc = 0;
  if (flags & GST_DP_HEADER_FLAG_CRC_PAYLOAD) {
    crc = gst_dp_crc (string, strlen (string) + 1);
  }
  GST_WRITE_UINT16_BE (h + 58, crc);

  GST_LOG ("created header from caps:");
  gst_dp_dump_byte_array (h, GST_DP_HEADER_LENGTH);
  *header = h;
  *payload = string;
  return TRUE;
}

/**
 * gst_dp_packet_from_event:
 * @event: a #GstEvent to create a packet for
 * @flags: the #GDPHeaderFlags to create the header with
 * @length: a guint pointer to store the header length in
 * @header: a guint8 pointer to store a newly allocated header byte array in
 * @payload: a guint8 pointer to store a newly allocated payload byte array in
 *
 * Creates a GDP packet from the given event.
 *
 * Returns: %TRUE if the packet was successfully created
 */
gboolean
gst_dp_packet_from_event (const GstEvent * event, GstDPHeaderFlag flags,
    guint * length, guint8 ** header, guint8 ** payload)
{
  guint8 *h;
  guint16 crc;
  guint pl_length;              /* length of payload */

  g_return_val_if_fail (event, FALSE);
  g_return_val_if_fail (GST_IS_EVENT (event), FALSE);
  g_return_val_if_fail (header, FALSE);
  g_return_val_if_fail (payload, FALSE);

  *length = GST_DP_HEADER_LENGTH;
  h = g_malloc0 (GST_DP_HEADER_LENGTH);

  /* first construct payload, since we need the length */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_UNKNOWN:
      g_warning ("Unknown event, ignoring");
      *length = 0;
      g_free (h);
      return FALSE;
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH:
    case GST_EVENT_EMPTY:
    case GST_EVENT_DISCONTINUOUS:
      GST_WRITE_UINT64_BE (h + 8, GST_EVENT_TIMESTAMP (event));
      pl_length = 0;
      *payload = NULL;
      break;
    case GST_EVENT_SEEK:
      pl_length = 4 + 8 + 4;
      *payload = g_malloc0 (pl_length);
      GST_WRITE_UINT32_BE (*payload, (guint32) GST_EVENT_SEEK_TYPE (event));
      GST_WRITE_UINT64_BE (*payload + 4,
          (guint64) GST_EVENT_SEEK_OFFSET (event));
      GST_WRITE_UINT32_BE (*payload + 12,
          (guint32) GST_EVENT_SEEK_ACCURACY (event));
      break;
    case GST_EVENT_SEEK_SEGMENT:
      pl_length = 4 + 8 + 8 + 4;
      *payload = g_malloc0 (pl_length);
      GST_WRITE_UINT32_BE (*payload, (guint32) GST_EVENT_SEEK_TYPE (event));
      GST_WRITE_UINT64_BE (*payload + 4,
          (guint64) GST_EVENT_SEEK_OFFSET (event));
      GST_WRITE_UINT64_BE (*payload + 12,
          (guint64) GST_EVENT_SEEK_ENDOFFSET (event));
      GST_WRITE_UINT32_BE (*payload + 20,
          (guint32) GST_EVENT_SEEK_ACCURACY (event));
      break;
    case GST_EVENT_QOS:
    case GST_EVENT_SEGMENT_DONE:
    case GST_EVENT_SIZE:
    case GST_EVENT_RATE:
    case GST_EVENT_FILLER:
    case GST_EVENT_TS_OFFSET:
    case GST_EVENT_INTERRUPT:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_TAG:
      g_warning ("Unhandled event type %d, ignoring", GST_EVENT_TYPE (event));
      return FALSE;
    default:
      g_warning ("Unknown event type %d, ignoring", GST_EVENT_TYPE (event));
      *length = 0;
      g_free (h);
      return FALSE;
  }

  /* version, flags, type */
  h[0] = (guint8) GST_DP_VERSION_MAJOR;
  h[1] = (guint8) GST_DP_VERSION_MINOR;
  h[2] = (guint8) flags;
  h[3] = GST_DP_PAYLOAD_EVENT_NONE + GST_EVENT_TYPE (event);

  /* length */
  GST_WRITE_UINT32_BE (h + 4, (guint32) pl_length);
  /* timestamp */
  GST_WRITE_UINT64_BE (h + 8, GST_EVENT_TIMESTAMP (event));

  /* ABI padding */
  GST_WRITE_UINT64_BE (h + 40, (guint64) 0);
  GST_WRITE_UINT64_BE (h + 48, (guint64) 0);

  /* CRC */
  crc = 0;
  if (flags & GST_DP_HEADER_FLAG_CRC_HEADER) {
    crc = gst_dp_crc (h, 56);
  }
  GST_WRITE_UINT16_BE (h + 56, crc);

  crc = 0;
  /* events can have a NULL payload */
  if (*payload && flags & GST_DP_HEADER_FLAG_CRC_PAYLOAD) {
    crc = gst_dp_crc (*payload, strlen (*payload) + 1);
  }
  GST_WRITE_UINT16_BE (h + 58, crc);

  GST_LOG ("created header from event:");
  gst_dp_dump_byte_array (h, GST_DP_HEADER_LENGTH);
  *header = h;
  return TRUE;
}


/**
 * gst_dp_buffer_from_header:
 * @header_length: the length of the packet header
 * @header: the byte array of the packet header
 *
 * Creates a newly allocated #GstBuffer from the given header.
 * The buffer data needs to be copied into it before validating.
 *
 * Use this function if you want to pre-allocate a buffer based on the
 * packet header to read the packet payload in to.
 *
 * Returns: %TRUE if the buffer was successfully created
 */
GstBuffer *
gst_dp_buffer_from_header (guint header_length, const guint8 * header)
{
  GstBuffer *buffer;

  g_return_val_if_fail (GST_DP_HEADER_PAYLOAD_TYPE (header) ==
      GST_DP_PAYLOAD_BUFFER, FALSE);
  buffer =
      gst_buffer_new_and_alloc ((guint) GST_DP_HEADER_PAYLOAD_LENGTH (header));
  GST_BUFFER_TIMESTAMP (buffer) = GST_DP_HEADER_TIMESTAMP (header);
  GST_BUFFER_DURATION (buffer) = GST_DP_HEADER_DURATION (header);
  GST_BUFFER_OFFSET (buffer) = GST_DP_HEADER_OFFSET (header);
  GST_BUFFER_OFFSET_END (buffer) = GST_DP_HEADER_OFFSET_END (header);

  return buffer;
}

/**
 * gst_dp_caps_from_packet:
 * @header_length: the length of the packet header
 * @header: the byte array of the packet header
 * @payload: the byte array of the packet payload
 *
 * Creates a newly allocated #GstCaps from the given packet.
 *
 * Returns: %TRUE if the caps was successfully created
 */
GstCaps *
gst_dp_caps_from_packet (guint header_length, const guint8 * header,
    const guint8 * payload)
{
  GstCaps *caps;
  const gchar *string;

  g_return_val_if_fail (header, FALSE);
  g_return_val_if_fail (payload, FALSE);
  g_return_val_if_fail (GST_DP_HEADER_PAYLOAD_TYPE (header) ==
      GST_DP_PAYLOAD_CAPS, FALSE);

  string = payload;
  caps = gst_caps_from_string (string);
  return caps;
}

/**
 * gst_dp_event_from_packet:
 * @header_length: the length of the packet header
 * @header: the byte array of the packet header
 * @payload: the byte array of the packet payload
 *
 * Creates a newly allocated #GstEvent from the given packet.
 *
 * Returns: %TRUE if the event was successfully created
 */
GstEvent *
gst_dp_event_from_packet (guint header_length, const guint8 * header,
    const guint8 * payload)
{
  GstEvent *event = NULL;
  GstEventType type;

  g_return_val_if_fail (header, FALSE);
  /* payload can be NULL, e.g. for an EOS event */

  type = GST_DP_HEADER_PAYLOAD_TYPE (header) - GST_DP_PAYLOAD_EVENT_NONE;
  switch (type) {
    case GST_EVENT_UNKNOWN:
      g_warning ("Unknown event, ignoring");
      return FALSE;
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH:
    case GST_EVENT_EMPTY:
    case GST_EVENT_DISCONTINUOUS:
      event = gst_event_new (type);
      GST_EVENT_TIMESTAMP (event) = GST_DP_HEADER_TIMESTAMP (header);
      break;
    case GST_EVENT_SEEK:
    {
      GstSeekType type;
      gint64 offset;
      GstSeekAccuracy accuracy;

      type = (GstSeekType) GST_READ_UINT32_BE (payload);
      offset = (gint64) GST_READ_UINT64_BE (payload + 4);
      accuracy = (GstSeekAccuracy) GST_READ_UINT32_BE (payload + 12);
      event = gst_event_new_seek (type, offset);
      GST_EVENT_TIMESTAMP (event) = GST_DP_HEADER_TIMESTAMP (header);
      GST_EVENT_SEEK_ACCURACY (event) = accuracy;
      break;
    }
    case GST_EVENT_SEEK_SEGMENT:
    {
      GstSeekType type;
      gint64 offset, endoffset;
      GstSeekAccuracy accuracy;

      type = (GstSeekType) GST_READ_UINT32_BE (payload);
      offset = (gint64) GST_READ_UINT64_BE (payload + 4);
      endoffset = (gint64) GST_READ_UINT64_BE (payload + 12);
      accuracy = (GstSeekAccuracy) GST_READ_UINT32_BE (payload + 20);
      event = gst_event_new_segment_seek (type, offset, endoffset);
      GST_EVENT_TIMESTAMP (event) = GST_DP_HEADER_TIMESTAMP (header);
      GST_EVENT_SEEK_ACCURACY (event) = accuracy;
      break;
    }
    case GST_EVENT_QOS:
    case GST_EVENT_SEGMENT_DONE:
    case GST_EVENT_SIZE:
    case GST_EVENT_RATE:
    case GST_EVENT_FILLER:
    case GST_EVENT_TS_OFFSET:
    case GST_EVENT_INTERRUPT:
    case GST_EVENT_NAVIGATION:
    case GST_EVENT_TAG:
      g_warning ("Unhandled event type %d, ignoring", GST_EVENT_TYPE (event));
      return FALSE;
    default:
      g_warning ("Unknown event type %d, ignoring", GST_EVENT_TYPE (event));
      return FALSE;
  }

  return event;
}

/**
 * gst_dp_validate_header:
 * @header_length: the length of the packet header
 * @header: the byte array of the packet header
 *
 * Validates the given packet header by checking the CRC checksum.
 *
 * Returns: %TRUE if the CRC matches, or no CRC checksum is present
 */
gboolean
gst_dp_validate_header (guint header_length, const guint8 * header)
{
  guint16 crc_read, crc_calculated;

  if (!(GST_DP_HEADER_FLAGS (header) & GST_DP_HEADER_FLAG_CRC_HEADER))
    return TRUE;
  crc_read = GST_DP_HEADER_CRC_HEADER (header);
  /* don't included the last two crc fields for the crc check */
  crc_calculated = gst_dp_crc (header, header_length - 4);
  if (crc_read != crc_calculated) {
    GST_WARNING ("header crc mismatch: read %02x, calculated %02x", crc_read,
        crc_calculated);
    return FALSE;
  }
  GST_LOG ("header crc validation: %02x", crc_read);
  return TRUE;
}

/**
 * gst_dp_validate_payload:
 * @header_length: the length of the packet header
 * @header: the byte array of the packet header
 * @payload: the byte array of the packet payload
 *
 * Validates the given packet payload using the given packet header
 * by checking the CRC checksum.
 *
 * Returns: %TRUE if the CRC matches, or no CRC checksum is present
 */
gboolean
gst_dp_validate_payload (guint header_length, const guint8 * header,
    const guint8 * payload)
{
  guint16 crc_read, crc_calculated;

  if (!(GST_DP_HEADER_FLAGS (header) & GST_DP_HEADER_FLAG_CRC_PAYLOAD))
    return TRUE;
  crc_read = GST_DP_HEADER_CRC_PAYLOAD (header);
  crc_calculated = gst_dp_crc (payload, GST_DP_HEADER_PAYLOAD_LENGTH (header));
  if (crc_read != crc_calculated) {
    GST_WARNING ("payload crc mismatch: read %02x, calculated %02x", crc_read,
        crc_calculated);
    return FALSE;
  }
  GST_LOG ("payload crc validation: %02x", crc_read);
  return TRUE;
}

/**
 * gst_dp_validate_packet:
 * @header_length: the length of the packet header
 * @header: the byte array of the packet header
 * @payload: the byte array of the packet payload
 *
 * Validates the given packet by checking version information and checksums.
 *
 * Returns: %TRUE if the packet validates
 */
gboolean
gst_dp_validate_packet (guint header_length, const guint8 * header,
    const guint8 * payload)
{
  if (!gst_dp_validate_header (header_length, header))
    return FALSE;
  if (!gst_dp_validate_payload (header_length, header, payload))
    return FALSE;

  return TRUE;
}

/*** PLUGIN STUFF ***/
static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_dp_init ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstdataprotocol",
    "a data protocol to serialize buffers, caps and events",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
