/* Gnome-Streamer
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


/*#define GST_DEBUG_ENABLED */
#include <gstmpegpacketize.h>

GstMPEGPacketize*
gst_mpeg_packetize_new (GstPad *pad)
{
  GstMPEGPacketize *new;

  g_return_val_if_fail (pad != NULL, NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);
  
  new = g_malloc (sizeof (GstMPEGPacketize));
  
  gst_object_ref (GST_OBJECT (pad));
  new->id = 0;
  new->pad = pad;
  new->bs = gst_bytestream_new (pad);
  new->MPEG2 = FALSE;

  return new;
}

void
gst_mpeg_packetize_destroy (GstMPEGPacketize *packetize)
{
  g_return_if_fail (packetize != NULL);

  gst_bytestream_destroy (packetize->bs);
  gst_object_unref (GST_OBJECT (packetize->pad));

  g_free (packetize);
}

static GstData*
parse_packhead (GstMPEGPacketize * packetize)
{
  gint length = 8 + 4;
  guint8 *buf;
  GstBuffer *outbuf;

  GST_DEBUG (0, "packetize: in parse_packhead\n");

  buf = gst_bytestream_peek_bytes (packetize->bs, length);
  if (!buf) return NULL;
  buf += 4;

  GST_DEBUG (0, "code %02x\n", *buf);

  /* start parsing the stream */
  if ((*buf & 0xf0) == 0x40) {
    GST_DEBUG (0, "packetize::parse_packhead setting mpeg2\n");
    packetize->MPEG2 = TRUE;
    length += 2;
    buf = gst_bytestream_peek_bytes (packetize->bs, length);
    if (!buf) return NULL;
  }
  else {
    GST_DEBUG (0, "packetize::parse_packhead setting mpeg1\n");
    packetize->MPEG2 = FALSE;
  }

  outbuf = gst_bytestream_read (packetize->bs, length);
  if (!outbuf) return NULL;

  return GST_DATA (outbuf);
}

static inline GstData*
parse_generic (GstMPEGPacketize *packetize)
{
  guint16 length;
  GstByteStream *bs = packetize->bs;
  guchar *buf;
  GstBuffer *outbuf;

  GST_DEBUG (0, "packetize: in parse_syshead\n");

  buf = gst_bytestream_peek_bytes (bs, 2 + 4);
  if (!buf) return NULL;
  buf += 4;

  length = GUINT16_FROM_BE (*(guint16 *) buf);
  GST_DEBUG (0, "packetize: header_length %d\n", length);

  outbuf = gst_bytestream_read (packetize->bs, 2 + length + 4);
  if (!outbuf) return NULL;

  return GST_DATA (outbuf);
}

/* FIXME mmx-ify me */
static inline gboolean
find_start_code (GstMPEGPacketize *packetize)
{
  GstByteStream *bs = packetize->bs;
  guchar *buf;
  gint offset;
  guint32 code;
  const gint chunksize = 4096;

  buf = gst_bytestream_peek_bytes (bs, chunksize);
  if (!buf)
    return FALSE;
  offset = 4;

  code = GUINT32_FROM_BE (*((guint32 *)buf));

  GST_DEBUG (0, "code = %08x\n", code);

  while ((code & 0xffffff00) != 0x100L) {
    code = (code << 8) | buf[offset++];
    
    GST_DEBUG (0, "  code = %08x\n", code);
    /* g_print ("  code = %08x\n", code); */

    if (offset == chunksize) {
      if (!gst_bytestream_flush (bs, offset))
	return FALSE;
      buf = gst_bytestream_peek_bytes (bs, chunksize);
      if (!buf)
	return FALSE;
      offset = 0;
    }
  }
  packetize->id = code & 0xff;
  if (offset > 4) {
    if (!gst_bytestream_flush (bs, offset - 4))
      return FALSE;
  }
  return TRUE;
}

GstData*
gst_mpeg_packetize_read (GstMPEGPacketize *packetize)
{
  gboolean got_event = FALSE;
  GstData *outbuf = NULL;

  while (outbuf == NULL) {
    if (!find_start_code (packetize))
      got_event = TRUE;
    else {
      GST_DEBUG (0, "packetize: have chunk 0x%02X\n",packetize->id);
      switch (packetize->id) {
        case 0xBA:
  	  outbuf = parse_packhead (packetize);
	  if (!outbuf)
	    got_event = TRUE;
	  break;
        case 0xBB:
	  outbuf = parse_generic (packetize);
	  if (!outbuf)
	    got_event = TRUE;
	  break;
        default:
	  if (packetize->MPEG2 && ((packetize->id < 0xBD) || (packetize->id > 0xFE))) {
	    g_warning ("packetize: ******** unknown id 0x%02X",packetize->id);
	  }
	  else {
	    outbuf = parse_generic (packetize);
	    if (!outbuf)
	      got_event = TRUE;
	  }
      }
    }

    if (got_event) {
      guint32 remaining;
      GstEvent *event;
      gint etype;

      gst_bytestream_get_status (packetize->bs, &remaining, &event);
      etype = event? GST_EVENT_TYPE (event) : GST_EVENT_EOS;

      switch (etype) {
        case GST_EVENT_DISCONTINUOUS:
	  gst_bytestream_flush_fast (packetize->bs, remaining);
	  break;
      }

      return GST_DATA (event);
    }
  }

  return outbuf;
}
