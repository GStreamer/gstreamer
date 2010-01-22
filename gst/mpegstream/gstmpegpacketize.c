/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>

#include "gstmpegpacketize.h"

GST_DEBUG_CATEGORY_STATIC (gstmpegpacketize_debug);
#define GST_CAT_DEFAULT (gstmpegpacketize_debug)

GstMPEGPacketize *
gst_mpeg_packetize_new (GstMPEGPacketizeType type)
{
  GstMPEGPacketize *new;

  new = g_new0 (GstMPEGPacketize, 1);
  new->resync = TRUE;
  new->id = 0;
  new->cache_head = 0;
  new->cache_tail = 0;
  new->cache_size = 0x4000;
  new->cache = g_malloc (new->cache_size);
  new->cache_byte_pos = 0;
  new->MPEG2 = FALSE;
  new->type = type;

#ifndef GST_DISABLE_GST_DEBUG
  if (gstmpegpacketize_debug == NULL) {
    GST_DEBUG_CATEGORY_INIT (gstmpegpacketize_debug, "mpegpacketize", 0,
        "MPEG parser element packetizer");
  }
#endif

  return new;
}

void
gst_mpeg_packetize_flush_cache (GstMPEGPacketize * packetize)
{
  g_return_if_fail (packetize != NULL);

  packetize->cache_byte_pos += packetize->cache_tail;

  packetize->resync = TRUE;
  packetize->cache_head = 0;
  packetize->cache_tail = 0;

  GST_DEBUG ("flushed packetize cache");
}

void
gst_mpeg_packetize_destroy (GstMPEGPacketize * packetize)
{
  g_return_if_fail (packetize != NULL);

  g_free (packetize->cache);
  g_free (packetize);
}

guint64
gst_mpeg_packetize_tell (GstMPEGPacketize * packetize)
{
  return packetize->cache_byte_pos + packetize->cache_head;
}

void
gst_mpeg_packetize_put (GstMPEGPacketize * packetize, GstBuffer * buf)
{
  int cache_len = packetize->cache_tail - packetize->cache_head;

  if (packetize->cache_head == 0 && cache_len == 0 &&
      GST_BUFFER_OFFSET_IS_VALID (buf)) {
    packetize->cache_byte_pos = GST_BUFFER_OFFSET (buf);
    GST_DEBUG ("cache byte position now %" G_GINT64_FORMAT,
        packetize->cache_byte_pos);
  }

  if (cache_len + GST_BUFFER_SIZE (buf) > packetize->cache_size) {
    /* the buffer does not fit into the cache so grow the cache */

    guint8 *new_cache;

    /* get the new size of the cache */
    do {
      packetize->cache_size *= 2;
    } while (cache_len + GST_BUFFER_SIZE (buf) > packetize->cache_size);

    /* allocate new cache - do not realloc to avoid copying data twice */
    new_cache = g_malloc (packetize->cache_size);

    /* copy the data to the beginning of the new cache and update the cache info */
    memcpy (new_cache, packetize->cache + packetize->cache_head, cache_len);
    g_free (packetize->cache);
    packetize->cache = new_cache;
    packetize->cache_byte_pos += packetize->cache_head;
    packetize->cache_head = 0;
    packetize->cache_tail = cache_len;
  } else if (packetize->cache_tail + GST_BUFFER_SIZE (buf) >
      packetize->cache_size) {
    /* the buffer does not fit into the end of the cache so move the cache data
       to the beginning of the cache */

    memmove (packetize->cache, packetize->cache + packetize->cache_head,
        packetize->cache_tail - packetize->cache_head);
    packetize->cache_byte_pos += packetize->cache_head;
    packetize->cache_tail -= packetize->cache_head;
    packetize->cache_head = 0;
  }

  /* copy the buffer to the cache */
  memcpy (packetize->cache + packetize->cache_tail, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  packetize->cache_tail += GST_BUFFER_SIZE (buf);

  gst_buffer_unref (buf);
}

static guint
peek_cache (GstMPEGPacketize * packetize, guint length, guint8 ** buf)
{
  *buf = packetize->cache + packetize->cache_head;

  if (packetize->cache_tail - packetize->cache_head < length)
    return packetize->cache_tail - packetize->cache_head;

  return length;
}

static void
skip_cache (GstMPEGPacketize * packetize, guint length)
{
  g_assert (packetize->cache_tail - packetize->cache_head >= length);

  packetize->cache_head += length;
}

static GstFlowReturn
read_cache (GstMPEGPacketize * packetize, guint length, GstBuffer ** outbuf)
{
  if (packetize->cache_tail - packetize->cache_head < length)
    return GST_FLOW_RESEND;
  if (length == 0)
    return GST_FLOW_RESEND;

  *outbuf = gst_buffer_new_and_alloc (length);

  memcpy (GST_BUFFER_DATA (*outbuf), packetize->cache + packetize->cache_head,
      length);
  packetize->cache_head += length;

  return GST_FLOW_OK;
}

static GstFlowReturn
parse_packhead (GstMPEGPacketize * packetize, GstBuffer ** outbuf)
{
  guint length = 8 + 4;
  guint8 *buf;
  guint got_bytes;

  GST_DEBUG ("packetize: in parse_packhead");

  *outbuf = NULL;

  got_bytes = peek_cache (packetize, length, &buf);
  if (got_bytes < length)
    return GST_FLOW_RESEND;

  buf += 4;

  GST_DEBUG ("code %02x", *buf);

  /* start parsing the stream */
  if ((*buf & 0xc0) == 0x40) {
    GST_DEBUG ("packetize::parse_packhead setting mpeg2");
    packetize->MPEG2 = TRUE;
    length += 2;
    got_bytes = peek_cache (packetize, length, &buf);
    if (got_bytes < length)
      return GST_FLOW_RESEND;
  } else {
    GST_DEBUG ("packetize::parse_packhead setting mpeg1");
    packetize->MPEG2 = FALSE;
  }

  return read_cache (packetize, length, outbuf);
}

static GstFlowReturn
parse_end (GstMPEGPacketize * packetize, GstBuffer ** outbuf)
{
  return read_cache (packetize, 4, outbuf);
}

static GstFlowReturn
parse_generic (GstMPEGPacketize * packetize, GstBuffer ** outbuf)
{
  guchar *buf;
  guint length = 6;
  guint got_bytes;

  GST_DEBUG ("packetize: in parse_generic");

  got_bytes = peek_cache (packetize, length, &buf);
  if (got_bytes < length)
    return GST_FLOW_RESEND;

  buf += 4;

  length += GST_READ_UINT16_BE (buf);
  GST_DEBUG ("packetize: header_length %d", length);

  return read_cache (packetize, length, outbuf);
}

static GstFlowReturn
parse_chunk (GstMPEGPacketize * packetize, GstBuffer ** outbuf)
{
  guchar *buf;
  gint offset;
  guint32 code;
  guint chunksize;

  chunksize = peek_cache (packetize, 4096, &buf);
  if (chunksize == 0)
    return GST_FLOW_RESEND;

  offset = 4;

  code = GST_READ_UINT32_BE (buf + offset);

  GST_DEBUG ("code = %08x", code);

  while ((code & 0xffffff00) != 0x100L) {
    code = (code << 8) | buf[offset++];

    GST_DEBUG ("  code = %08x", code);

    if (offset == chunksize) {
      chunksize = peek_cache (packetize, offset + 4096, &buf);
      if (chunksize == 0)
        return GST_FLOW_RESEND;
      chunksize += offset;
    }
  }
  if (offset > 4) {
    return read_cache (packetize, offset - 4, outbuf);
  }
  return GST_FLOW_RESEND;
}


/* FIXME mmx-ify me */
static gboolean
find_start_code (GstMPEGPacketize * packetize)
{
  guint8 *buf;
  gint offset;
  guint32 code;
  gint chunksize;

  chunksize = peek_cache (packetize, 4096, &buf);
  if (chunksize < 5)
    return FALSE;

  offset = 4;

  code = GST_READ_UINT32_BE (buf);

  GST_DEBUG ("code = %08x %p %08x", code, buf, chunksize);

  while ((code & 0xffffff00) != 0x100L) {
    code = (code << 8) | buf[offset++];

    GST_DEBUG ("  code = %08x %p %08x", code, buf, chunksize);

    if (offset == chunksize) {
      skip_cache (packetize, offset);

      chunksize = peek_cache (packetize, 4096, &buf);
      if (chunksize == 0)
        return FALSE;

      offset = 0;
    }
  }
  packetize->id = code & 0xff;
  if (offset > 4) {
    skip_cache (packetize, offset - 4);
  }
  return TRUE;
}

GstFlowReturn
gst_mpeg_packetize_read (GstMPEGPacketize * packetize, GstBuffer ** outbuf)
{
  g_return_val_if_fail (packetize != NULL, GST_FLOW_ERROR);

  *outbuf = NULL;

  while (*outbuf == NULL) {
    if (!find_start_code (packetize))
      return GST_FLOW_RESEND;

    GST_DEBUG ("packetize: have chunk 0x%02X", packetize->id);
    if (packetize->type == GST_MPEG_PACKETIZE_SYSTEM) {
      if (packetize->resync) {
        if (packetize->id != PACK_START_CODE) {
          skip_cache (packetize, 4);
          continue;
        }

        packetize->resync = FALSE;
      }
      switch (packetize->id) {
        case PACK_START_CODE:
          return parse_packhead (packetize, outbuf);
        case SYS_HEADER_START_CODE:
          return parse_generic (packetize, outbuf);
        case ISO11172_END_START_CODE:
          return parse_end (packetize, outbuf);
        default:
          if (packetize->MPEG2 && ((packetize->id < 0xBD)
                  || (packetize->id > 0xFE))) {
            skip_cache (packetize, 4);
            g_warning ("packetize: ******** unknown id 0x%02X", packetize->id);
          } else {
            return parse_generic (packetize, outbuf);
          }
      }
    } else if (packetize->type == GST_MPEG_PACKETIZE_VIDEO) {
      return parse_chunk (packetize, outbuf);
    } else {
      g_assert_not_reached ();
    }
  }

  g_assert_not_reached ();
  return GST_FLOW_ERROR;
}
