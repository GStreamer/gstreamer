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

/*#define GST_DEBUG_ENABLED */
#include "gstmpegpacketize.h"

GstMPEGPacketize *
gst_mpeg_packetize_new (GstPad * pad, GstMPEGPacketizeType type)
{
  GstMPEGPacketize *new;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  new = g_malloc (sizeof (GstMPEGPacketize));

  gst_object_ref (GST_OBJECT (pad));
  new->resync = TRUE;
  new->id = 0;
  new->pad = pad;
  new->bs = gst_bytestream_new (pad);
  new->MPEG2 = FALSE;
  new->type = type;

  return new;
}

void
gst_mpeg_packetize_destroy (GstMPEGPacketize * packetize)
{
  g_return_if_fail (packetize != NULL);

  gst_bytestream_destroy (packetize->bs);
  gst_object_unref (GST_OBJECT (packetize->pad));

  g_free (packetize);
}

static GstData *
parse_packhead (GstMPEGPacketize * packetize)
{
  gint length = 8 + 4;
  guint8 *buf;
  GstBuffer *outbuf;
  guint32 got_bytes;

  GST_DEBUG ("packetize: in parse_packhead");

  got_bytes = gst_bytestream_peek_bytes (packetize->bs, &buf, length);
  if (got_bytes < length)
    return NULL;

  buf += 4;

  GST_DEBUG ("code %02x", *buf);

  /* start parsing the stream */
  if ((*buf & 0xc0) == 0x40) {
    GST_DEBUG ("packetize::parse_packhead setting mpeg2");
    packetize->MPEG2 = TRUE;
    length += 2;
    got_bytes = gst_bytestream_peek_bytes (packetize->bs, &buf, length);
    if (got_bytes < length)
      return NULL;
  } else {
    GST_DEBUG ("packetize::parse_packhead setting mpeg1");
    packetize->MPEG2 = FALSE;
  }

  got_bytes = gst_bytestream_read (packetize->bs, &outbuf, length);
  if (got_bytes < length)
    return NULL;

  return GST_DATA (outbuf);
}

static GstData *
parse_end (GstMPEGPacketize * packetize)
{
  guint32 got_bytes;
  GstBuffer *outbuf;

  got_bytes = gst_bytestream_read (packetize->bs, &outbuf, 4);
  if (got_bytes < 4)
    return NULL;

  return GST_DATA (outbuf);
}

static inline GstData *
parse_generic (GstMPEGPacketize * packetize)
{
  GstByteStream *bs = packetize->bs;
  guchar *buf;
  GstBuffer *outbuf;
  guint32 got_bytes;
  gint16 length = 6;

  GST_DEBUG ("packetize: in parse_generic");

  got_bytes = gst_bytestream_peek_bytes (bs, (guint8 **) & buf, length);
  if (got_bytes < 6)
    return NULL;

  buf += 4;

  length += GST_READ_UINT16_BE (buf);
  GST_DEBUG ("packetize: header_length %d", length);

  got_bytes = gst_bytestream_read (packetize->bs, &outbuf, length);
  if (got_bytes < length)
    return NULL;

  return GST_DATA (outbuf);
}

static inline GstData *
parse_chunk (GstMPEGPacketize * packetize)
{
  GstByteStream *bs = packetize->bs;
  guchar *buf;
  gint offset;
  guint32 code;
  gint chunksize;
  GstBuffer *outbuf = NULL;

  chunksize = gst_bytestream_peek_bytes (bs, (guint8 **) & buf, 4096);
  if (chunksize == 0)
    return NULL;

  offset = 4;

  code = GST_READ_UINT32_BE (buf + offset);

  GST_DEBUG ("code = %08x", code);

  while ((code & 0xffffff00) != 0x100L) {
    code = (code << 8) | buf[offset++];

    GST_DEBUG ("  code = %08x", code);

    if (offset == chunksize) {
      chunksize =
          gst_bytestream_peek_bytes (bs, (guint8 **) & buf, offset + 4096);
      if (chunksize == 0)
        return NULL;
      chunksize += offset;
    }
  }
  if (offset > 4) {
    chunksize = gst_bytestream_read (bs, &outbuf, offset - 4);
    if (chunksize == 0)
      return NULL;
  }
  return GST_DATA (outbuf);
}


/* FIXME mmx-ify me */
static inline gboolean
find_start_code (GstMPEGPacketize * packetize)
{
  GstByteStream *bs = packetize->bs;
  guchar *buf;
  gint offset;
  guint32 code;
  gint chunksize;

  chunksize = gst_bytestream_peek_bytes (bs, (guint8 **) & buf, 4096);
  if (chunksize < 5)
    return FALSE;

  offset = 4;

  code = GST_READ_UINT32_BE (buf);

  GST_DEBUG ("code = %08x %p %08x", code, buf, chunksize);

  while ((code & 0xffffff00) != 0x100L) {
    code = (code << 8) | buf[offset++];

    GST_DEBUG ("  code = %08x %p %08x", code, buf, chunksize);

    if (offset == chunksize) {
      gst_bytestream_flush_fast (bs, offset);

      chunksize = gst_bytestream_peek_bytes (bs, (guint8 **) & buf, 4096);
      if (chunksize == 0)
        return FALSE;

      offset = 0;
    }
  }
  packetize->id = code & 0xff;
  if (offset > 4) {
    gst_bytestream_flush_fast (bs, offset - 4);
  }
  return TRUE;
}

GstData *
gst_mpeg_packetize_read (GstMPEGPacketize * packetize)
{
  gboolean got_event = FALSE;
  GstData *outbuf = NULL;

  g_return_val_if_fail (packetize != NULL, NULL);

  while (outbuf == NULL) {
    if (!find_start_code (packetize))
      got_event = TRUE;
    else {
      GST_DEBUG ("packetize: have chunk 0x%02X", packetize->id);
      if (packetize->type == GST_MPEG_PACKETIZE_SYSTEM) {
        if (packetize->resync) {
          if (packetize->id != PACK_START_CODE) {
            gst_bytestream_flush_fast (packetize->bs, 4);
            continue;
          }

          packetize->resync = FALSE;
        }
        switch (packetize->id) {
          case PACK_START_CODE:
            outbuf = parse_packhead (packetize);
            if (!outbuf)
              got_event = TRUE;
            break;
          case SYS_HEADER_START_CODE:
            outbuf = parse_generic (packetize);
            if (!outbuf)
              got_event = TRUE;
            break;
          case ISO11172_END_START_CODE:
            outbuf = parse_end (packetize);
            if (!outbuf)
              got_event = TRUE;
            break;
          default:
            if (packetize->MPEG2 && ((packetize->id < 0xBD)
                    || (packetize->id > 0xFE))) {
              gst_bytestream_flush (packetize->bs, 4);
              g_warning ("packetize: ******** unknown id 0x%02X",
                  packetize->id);
            } else {
              outbuf = parse_generic (packetize);
              if (!outbuf)
                got_event = TRUE;
            }
        }
      } else if (packetize->type == GST_MPEG_PACKETIZE_VIDEO) {
        outbuf = parse_chunk (packetize);
      } else {
        g_assert_not_reached ();
      }
    }

    if (got_event) {
      guint32 remaining;
      GstEvent *event;
      gint etype;

      gst_bytestream_get_status (packetize->bs, &remaining, &event);
      etype = event ? GST_EVENT_TYPE (event) : GST_EVENT_EOS;

      switch (etype) {
        case GST_EVENT_DISCONTINUOUS:
          GST_DEBUG ("packetize: discont\n");
          gst_bytestream_flush_fast (packetize->bs, remaining);
          break;
      }

      return GST_DATA (event);
    }
  }

  return outbuf;
}
