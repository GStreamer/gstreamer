/* GStreamer
 * Copyright (C) 2001 Erik Walthinsen <omega@temple-baptist.com>
 *
 * gstbytestream.c: adds a convenient bytestream based API to a pad.
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
#  include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gst/gstinfo.h>
#include <gst/gstplugin.h>
#include <gst/gstversion.h>
#include "bytestream.h"

GST_DEBUG_CATEGORY_STATIC(debug_bs);
#define GST_CAT_DEFAULT debug_bs

static guint8 *gst_bytestream_assemble (GstByteStream * bs, guint32 len);

static inline void
gst_bytestream_init (GstByteStream *bs)
{
  bs->event = NULL;
  bs->buflist = NULL;
  bs->headbufavail = 0;
  bs->listavail = 0;
  bs->assembled = NULL;
  bs->offset = 0LL;
  bs->in_seek = FALSE;
}
static inline void
gst_bytestream_exit (GstByteStream *bs)
{
  GSList *walk;

  if (bs->event)
    gst_event_unref (bs->event);
  
  walk = bs->buflist;
  while (walk) {
    gst_buffer_unref (GST_BUFFER (walk->data));
    walk = g_slist_next (walk);
  }
  g_slist_free (bs->buflist);

  g_free (bs->assembled);
}
/**
 * gst_bytestream_new:
 * @pad: the pad to attach the bytestream to
 *
 * creates a bytestream from the given pad
 *
 * Returns: a new #GstByteStream object
 */
GstByteStream *
gst_bytestream_new (GstPad * pad)
{
  GstByteStream *bs = g_new (GstByteStream, 1);

  bs->pad = pad;
  gst_bytestream_init (bs);

  return bs;
}

/**
 * gst_bytestream_destroy:
 * @bs: the bytestream object to destroy
 *
 * destroy the bytestream object and free its resources.
 */
void
gst_bytestream_destroy (GstByteStream * bs)
{
  gst_bytestream_exit (bs);
  g_free (bs);
}
void
gst_bytestream_reset (GstByteStream *bs)
{
  /* free all data */
  gst_bytestream_exit (bs);
  /* reset data to clean state */
  gst_bytestream_init (bs);
}
/* HOW THIS WORKS:
 *
 * The fundamental structure is a singly-linked list of buffers.  The
 * buffer on the front is the oldest, and thus the first to read data
 * from.  The number of bytes left to be read in this buffer is stored
 * in bs->headbufavail.  The number of bytes available in the entire
 * list (including the head buffer) is in bs->listavail.
 *
 * When a request is made for data (peek), _fill_bytes is called with
 * the number of bytes needed, but only if the listavail indicates
 * that there aren't already enough.  This calls _get_next_buf until
 * the listavail is sufficient to satisfy the demand.
 *
 * _get_next_buf pulls a buffer from the pad the bytestream is attached
 * to, and shoves it in the list.  There are actually two things it can
 * do.  If there's already a buffer in the list, and the _is_span_fast()
 * test returns true, it will merge it with that last buffer.  Otherwise
 * it will simply tack it onto the end of the list.
 *
 * The _peek itself first checks the simple case of the request fitting
 * within the head buffer, and if so creates a subbuffer and returns.
 * Otherwise, it creates a new buffer and allocates space for the request
 * and calls _assemble to fill it.  We know we have to copy because this
 * case only happens when the _merge wasn't feasible during _get_next_buf.
 *
 * The _flush method repeatedly inspects the head buffer and flushes as
 * much data from it as it needs to, up to the size of the buffer.  If
 * the flush decimates the buffer, it's stripped, unref'd, and removed.
 */


/* get the next buffer
 * if the buffer can be merged with the head buffer, do so
 * else add it onto the head of the 
 */
static gboolean
gst_bytestream_get_next_buf (GstByteStream *bs)
{
  GstBuffer *nextbuf, *lastbuf, *headbuf;
  GSList *end;

  /* if there is an event pending, return FALSE */
  if (bs->event)
    return FALSE;

  GST_DEBUG ("get_next_buf: pulling buffer");
  nextbuf = GST_BUFFER (gst_pad_pull (bs->pad));

  if (!nextbuf)
    return FALSE;

  if (GST_IS_EVENT (nextbuf)) {
    GstEvent *event = GST_EVENT (nextbuf);
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
      case GST_EVENT_DISCONTINUOUS:
        GST_DEBUG ("get_next_buf: received EOS event.");
        bs->event = event;
        return FALSE;
      default:
        GST_DEBUG ("get_next_buf: received event %d, forwarding",
                   GST_EVENT_TYPE (event));
        gst_pad_event_default (bs->pad, event);
        return TRUE;
    }
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (nextbuf))
    bs->last_ts = GST_BUFFER_TIMESTAMP (nextbuf);

  GST_DEBUG ("get_next_buf: got buffer of %d bytes", GST_BUFFER_SIZE (nextbuf));

  /* first see if there are any buffers in the list at all */
  if (bs->buflist) {
    GST_DEBUG ("gst_next_buf: there is at least one buffer in the list");
    /* now find the end of the list */
    end = g_slist_last (bs->buflist);
    /* get the buffer that's there */
    lastbuf = GST_BUFFER (end->data);

    /* see if we can marge cheaply */
    if (gst_buffer_is_span_fast (lastbuf, nextbuf)) {
      GST_DEBUG ("get_next_buf: merging new buffer with last buf on list");
      /* it is, let's merge them (this is really an append, but...) */
      end->data = gst_buffer_merge (lastbuf, nextbuf);
      /* add to the length of the list */
      bs->listavail += GST_BUFFER_SIZE (nextbuf);

      /* have to check to see if we merged with the head buffer */
      if (end == bs->buflist) {
	bs->headbufavail += GST_BUFFER_SIZE (nextbuf);
      }

      gst_buffer_unref (lastbuf);
      gst_buffer_unref (nextbuf);

      /* if we can't, we just append this buffer */
    }
    else {
      GST_DEBUG ("get_next_buf: adding new buffer to the end of the list");
      end = g_slist_append (end, nextbuf);
      /* also need to increment length of list and buffer count */
      bs->listavail += GST_BUFFER_SIZE (nextbuf);
    }

    /* if there are no buffers in the list */
  }
  else {
    GST_DEBUG ("get_next_buf: buflist is empty, adding new buffer to list");
    /* put this on the end of the list */
    bs->buflist = g_slist_append (bs->buflist, nextbuf);
    /* and increment the number of bytes in the list */
    bs->listavail = GST_BUFFER_SIZE (nextbuf);
    /* set the head buffer avail to the size */
    bs->headbufavail = GST_BUFFER_SIZE (nextbuf);
  }

  /* a zero offset is a indication that we might need to set the timestamp */ 
  if (bs->offset == 0LL){
    headbuf = GST_BUFFER (bs->buflist->data);
    bs->offset = GST_BUFFER_OFFSET(headbuf);
  }
  
  return TRUE;
}

static gboolean
gst_bytestream_fill_bytes (GstByteStream *bs, guint32 len)
{
  /* as long as we don't have enough, we get more buffers */
  while (bs->listavail < len) {
    GST_DEBUG ("fill_bytes: there are %d bytes in the list, we need %d", bs->listavail, len);
    if (!gst_bytestream_get_next_buf (bs))
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_bytestream_peek:
 * @bs: the bytestream to peek
 * @buf: pointer to a variable that can hold a buffer pointer.
 * @len: the number of bytes to peek
 *
 * Peeks len bytes into the bytestream, the result is returned as
 * a #GstBuffer. Unref the buffer after usage.
 * This function can return less bytes than requested. In that case,
 * an event might have happened which you can retrieve with
 * gst_bytestream_get_status().
 *
 * Returns: The number of bytes successfully peeked.
 */
guint32
gst_bytestream_peek (GstByteStream *bs, GstBuffer **buf, guint32 len)
{
  GstBuffer *headbuf, *retbuf = NULL;

  g_return_val_if_fail (bs != NULL, 0);
  g_return_val_if_fail (buf != NULL, 0);
  g_return_val_if_fail (len > 0, 0);

  GST_DEBUG ("peek: asking for %d bytes", len);

  /* make sure we have enough */
  GST_DEBUG ("peek: there are %d bytes in the list", bs->listavail);
  if (len > bs->listavail) {
    if (!gst_bytestream_fill_bytes (bs, len)) {
      /* we must have an event coming up */
      if (bs->listavail > 0) {
        /* we have some data left, len will be shrunk to the amount of data available */
        len = bs->listavail;
      }
      else {
        /* there is no data */
        *buf = retbuf;
        return 0;
      }
    }
    GST_DEBUG ("peek: there are now %d bytes in the list", bs->listavail);
  }
  gst_bytestream_print_status (bs);

  /* extract the head buffer */
  headbuf = GST_BUFFER (bs->buflist->data);

  /* if the requested bytes are in the current buffer */
  GST_DEBUG ("peek: headbufavail is %d", bs->headbufavail);
  if (len <= bs->headbufavail) {
    GST_DEBUG ("peek: there are enough bytes in headbuf (need %d, have %d)", len, bs->headbufavail);
    /* create a sub-buffer of the headbuf */
    retbuf = gst_buffer_create_sub (headbuf, GST_BUFFER_SIZE (headbuf) - bs->headbufavail, len);
    GST_BUFFER_OFFSET (retbuf) = GST_BUFFER_OFFSET (headbuf) + GST_BUFFER_SIZE (headbuf) - bs->headbufavail;

  }
  /* otherwise we need to figure out how to assemble one */
  else {
    GST_DEBUG ("peek: current buffer is not big enough for len %d", len);

    retbuf = gst_buffer_new ();
    GST_BUFFER_SIZE (retbuf) = len;
    GST_BUFFER_DATA (retbuf) = gst_bytestream_assemble (bs, len);
    GST_BUFFER_TIMESTAMP (retbuf) = bs->last_ts;
  }

  *buf = retbuf;
  return len;
}

/**
 * gst_bytestream_peek_bytes:
 * @bs: the bytestream to peek
 * @data: pointer to a variable that can hold a guint8 pointer.
 * @len: the number of bytes to peek
 *
 * Peeks len bytes into the bytestream, the result is returned as
 * a pointer to a guint8*. The data pointed to be data should not
 * be freed and will become invalid after performing the next bytestream
 * operation.
 * This function can return less bytes than requested. In that case,
 * an event might have happened which you can retrieve with
 * gst_bytestream_get_status().
 *
 * Returns: The number of bytes successfully peeked.
 */
guint32
gst_bytestream_peek_bytes (GstByteStream *bs, guint8** data, guint32 len)
{
  GstBuffer *headbuf;

  g_return_val_if_fail (bs != NULL, 0);
  g_return_val_if_fail (data != NULL, 0);
  g_return_val_if_fail (len > 0, 0);

  GST_DEBUG ("peek_bytes: asking for %d bytes", len);
  if (bs->assembled) {
    if (bs->assembled_len >= len) {
      *data = bs->assembled;
      return len;
    }
    g_free (bs->assembled);
    bs->assembled = NULL;
  }

  /* make sure we have enough */
  GST_DEBUG ("peek_bytes: there are %d bytes in the list", bs->listavail);
  if (len > bs->listavail) {
    if (!gst_bytestream_fill_bytes (bs, len)){
      /* we must have an event coming up */
      if (bs->listavail > 0){
        /* we have some data left, len will be shrunk to the amount of data available */
        len = bs->listavail;
      }
      else {
        /* there is no data */
        *data = NULL;
        return 0;
      }
    }
    GST_DEBUG ("peek_bytes: there are now %d bytes in the list", bs->listavail);
  }
  gst_bytestream_print_status (bs);

  /* extract the head buffer */
  headbuf = GST_BUFFER (bs->buflist->data);

  /* if the requested bytes are in the current buffer */
  GST_DEBUG ("peek_bytes: headbufavail is %d", bs->headbufavail);
  if (len <= bs->headbufavail) {
    GST_DEBUG ("peek_bytes: there are enough bytes in headbuf (need %d, have %d)", len, bs->headbufavail);
    /* create a sub-buffer of the headbuf */
    *data = GST_BUFFER_DATA (headbuf) + (GST_BUFFER_SIZE (headbuf) - bs->headbufavail);

  }
  /* otherwise we need to figure out how to assemble one */
  else {
    GST_DEBUG ("peek_bytes: current buffer is not big enough for len %d", len);

    *data = gst_bytestream_assemble (bs, len);
    bs->assembled = *data;
    bs->assembled_len = len;
  }

  return len;
}

static guint8*
gst_bytestream_assemble (GstByteStream *bs, guint32 len)
{
  guint8 *data = g_malloc (len);
  GSList *walk;
  guint32 copied = 0;
  GstBuffer *buf;

  /* copy the data from the curbuf */
  buf = GST_BUFFER (bs->buflist->data);
  GST_DEBUG ("assemble: copying %d bytes from curbuf at %d to *data", bs->headbufavail,
	    GST_BUFFER_SIZE (buf) - bs->headbufavail);
  memcpy (data, GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf) - bs->headbufavail, bs->headbufavail);
  copied += bs->headbufavail;

  /* asumption is made that the buffers all exist in the list */
  walk = g_slist_next (bs->buflist);
  while (copied < len) {
    buf = GST_BUFFER (walk->data);
    if (GST_BUFFER_SIZE (buf) < (len - copied)) {
      GST_DEBUG ("assemble: copying %d bytes from buf to output offset %d", GST_BUFFER_SIZE (buf), copied);
      memcpy (data + copied, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      copied += GST_BUFFER_SIZE (buf);
    }
    else {
      GST_DEBUG ("assemble: copying %d bytes from buf to output offset %d", len - copied, copied);
      memcpy (data + copied, GST_BUFFER_DATA (buf), len - copied);
      copied = len;
    }
    walk = g_slist_next (walk);
  }

  return data;
}

/**
 * gst_bytestream_flush:
 * @bs: the bytestream to flush
 * @len: the number of bytes to flush
 *
 * Flush len bytes from the bytestream. 
 * This function can return FALSE when the number of
 * bytes could not be flushed due to an event. In that case,
 * you can get the number of available bytes before the event
 * with gst_bytestream_get_status().
 *
 * Returns: TRUE if the number of bytes could be flushed.
 */
gboolean
gst_bytestream_flush (GstByteStream *bs, guint32 len)
{
  GST_DEBUG ("flush: flushing %d bytes", len);

  if (len == 0)
    return TRUE;

  /* make sure we have enough */
  GST_DEBUG ("flush: there are %d bytes in the list", bs->listavail);
  if (len > bs->listavail) {
    if (!gst_bytestream_fill_bytes (bs, len)) {
      return FALSE;
    }
    GST_DEBUG ("flush: there are now %d bytes in the list", bs->listavail);
  }

  gst_bytestream_flush_fast (bs, len);

  return TRUE;
}

/**
 * gst_bytestream_flush_fast:
 * @bs: the bytestream to flush
 * @len: the number of bytes to flush
 *
 * Flushes len bytes from the bytestream. This function
 * is faster than gst_bytestream_flush() but only works
 * when you have recently peeked no less than len bytes
 * with gst_bytestream_peek() or gst_bytestream_peek_bytes().
 */
void
gst_bytestream_flush_fast (GstByteStream *bs, guint32 len)
{
  GstBuffer *headbuf;

  if (len == 0)
    return;
		  
  g_assert (len <= bs->listavail);

  if (bs->assembled) {
    g_free (bs->assembled);
    bs->assembled = NULL;
  }

  /* update the byte offset */
  bs->offset += len;

  /* repeat until we've flushed enough data */
  while (len > 0) {
    headbuf = GST_BUFFER (bs->buflist->data);

    GST_DEBUG ("flush: analyzing buffer that's %d bytes long, offset %" G_GUINT64_FORMAT, GST_BUFFER_SIZE (headbuf),
	      GST_BUFFER_OFFSET (headbuf));

    /* if there's enough to complete the flush */
    if (bs->headbufavail > len) {
      /* just trim it off */
      GST_DEBUG ("flush: trimming %d bytes off end of headbuf", len);
      bs->headbufavail -= len;
      bs->listavail -= len;
      len = 0;

      /* otherwise we have to trim the whole buffer */
    }
    else {
      GST_DEBUG ("flush: removing head buffer completely");
      /* remove it from the list */
      bs->buflist = g_slist_delete_link (bs->buflist, bs->buflist);
      /* trim it from the avail size */
      bs->listavail -= bs->headbufavail;
      /* record that we've trimmed this many bytes */
      len -= bs->headbufavail;
      /* unref it */
      gst_buffer_unref (headbuf);

      /* record the new headbufavail */
      if (bs->buflist) {
	bs->headbufavail = GST_BUFFER_SIZE (GST_BUFFER (bs->buflist->data));
	GST_DEBUG ("flush: next headbuf is %d bytes", bs->headbufavail);
      }
      else {
	GST_DEBUG ("flush: no more bytes at all");
      }
    }

    GST_DEBUG ("flush: bottom of while(), len is now %d", len);
  }
}

/**
 * gst_bytestream_seek:
 * @bs: the bytestream to seek
 * @offset: the byte offset to seek to
 * @method: the seek method.
 *
 * Perform a seek on the bytestream to the given offset.
 * The method can be one of GST_SEEK_METHOD_CUR, GST_SEEK_METHOD_SET,
 * GST_SEEK_METHOD_END.
 * This seek will also flush any pending data in the bytestream or
 * peer elements.
 *
 * Returns: TRUE when the seek succeeded.
 */
gboolean
gst_bytestream_seek (GstByteStream *bs, gint64 offset, GstSeekType method)
{
  GstRealPad *peer;
  
  g_return_val_if_fail (bs != NULL, FALSE);
  
  peer = GST_RPAD_PEER (bs->pad);

  GST_DEBUG ("bs: send event\n");
  if (gst_pad_send_event (GST_PAD (peer), gst_event_new_seek (
			  GST_FORMAT_BYTES | 
			  (method & GST_SEEK_METHOD_MASK) | 
			  GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, 
			  offset))) 
  {
    gst_bytestream_flush_fast (bs, bs->listavail);

    /* we set the seek flag here. We cannot pull the pad here
     * bacause a seek might occur outisde of the pads cothread context */
    bs->in_seek = TRUE;
    
    return TRUE;
  }
  GST_DEBUG ("bs: send event failed\n");
  return FALSE;
}

/**
 * gst_bytestream_tell
 * @bs: a bytestream
 *
 * Get the current byteoffset in the bytestream.
 *
 * Returns: the offset or -1 on error.
 */
guint64
gst_bytestream_tell (GstByteStream *bs)
{
  GstFormat format;
  gint64 value;
  
  g_return_val_if_fail (bs != NULL, -1);

  format = GST_FORMAT_BYTES;

  if (gst_pad_query (GST_PAD_PEER (bs->pad), GST_QUERY_POSITION, &format, &value)) {
    return value - bs->listavail;
  }
  
  return -1;
}

/**
 * gst_bytestream_length
 * @bs: a bytestream
 *
 * Get the total length of the bytestream.
 *
 * Returns: the total length or -1 on error.
 */
guint64
gst_bytestream_length (GstByteStream *bs)
{
  GstFormat format;
  gint64 value;
  
  g_return_val_if_fail (bs != NULL, -1);

  format = GST_FORMAT_BYTES;

  if (gst_pad_query (GST_PAD_PEER (bs->pad), GST_QUERY_TOTAL, &format, &value)) 
    return value;
  
  return -1;
}

/**
 * gst_bytestream_read:
 * @bs: the bytestream to read
 * @buf: pointer to a variable that can hold a buffer pointer.
 * @len: the number of bytes to read
 *
 * Read len bytes from the bytestream, the result is returned as
 * a #GstBuffer. Unref the buffer after usage.
 * This function can return less bytes than requested. In that case,
 * an event might have happened which you can retrieve with
 * gst_bytestream_get_status().
 *
 * Returns: The number of bytes successfully read.
 */
guint32
gst_bytestream_read (GstByteStream *bs, GstBuffer** buf, guint32 len)
{
  guint32 len_peeked;

  g_return_val_if_fail (bs != NULL, -1);
  
  len_peeked = gst_bytestream_peek (bs, buf, len);
  if (len_peeked == 0)
    return 0;

  gst_bytestream_flush_fast (bs, len_peeked);

  return len_peeked;
}

/**
 * gst_bytestream_size_hint
 * @bs: a bytestream
 * @size: the size to hint
 *
 * Give a hint that we are going to read chunks of the given size.
 * Giving size hints to the peer element might improve performance
 * since less buffers need to be merged.
 *
 * Returns: TRUE if the hint was accepted
 */
gboolean
gst_bytestream_size_hint (GstByteStream *bs, guint32 size)
{
  GstEvent *event;

  g_return_val_if_fail (bs != NULL, FALSE);

  event = gst_event_new_size (GST_FORMAT_BYTES, size);

  return gst_pad_send_event (GST_PAD_PEER (bs->pad), event);
}

/**
 * gst_bytestream_get_status
 * @bs: a bytestream
 * @avail_out: total number of bytes buffered
 * @event_out: an event
 *
 * When an event occurs, the bytestream operations return a value less
 * than the requested length. You must retrieve the event using this API 
 * before reading more bytes from the stream.
 */
void
gst_bytestream_get_status (GstByteStream *bs,
			   guint32 	 *avail_out,
			   GstEvent 	**event_out)
{
  if (avail_out)
    *avail_out = bs->listavail;

  if (event_out) {
    *event_out = bs->event;
    bs->event = NULL;
  }
}

/**
 * gst_bytestream_get_timestamp
 * @bs: a bytestream
 *
 * Get the timestamp of the first data in the bytestream.  If no data
 * exists 1 byte is read to load a new buffer.
 *
 * This function will not check input buffer boundries.  It is  possible
 * the next read could span two or more input buffers with different
 * timestamps.
 *
 * Returns: a timestamp
 */
guint64
gst_bytestream_get_timestamp (GstByteStream *bs)
{
  GstBuffer *headbuf;

  g_return_val_if_fail (bs != NULL, 0);

  GST_DEBUG ("get_timestamp: getting timestamp");

  /* make sure we have a buffer */
  if (bs->listavail == 0) {
    GST_DEBUG ("gst_timestamp: fetching a buffer");
    if (!gst_bytestream_fill_bytes (bs, 1))
      return 0;
  }

  /* extract the head buffer */
  headbuf = GST_BUFFER (bs->buflist->data);

  return GST_BUFFER_TIMESTAMP (headbuf);
}

/**
 * gst_bytestream_print_status
 * @bs: a bytestream
 *
 * Print the current status of the bytestream object. mainly
 * used for debugging purposes.
 */
void
gst_bytestream_print_status (GstByteStream * bs)
{
  GSList *walk;
  GstBuffer *buf;

  GST_DEBUG ("STATUS: head buffer has %d bytes available", bs->headbufavail);
  GST_DEBUG ("STATUS: list has %d bytes available", bs->listavail);
  walk = bs->buflist;
  while (walk) {
    buf = GST_BUFFER (walk->data);
    walk = g_slist_next (walk);

    GST_DEBUG ("STATUS: buffer starts at %" G_GUINT64_FORMAT " and is %d bytes long", 
	      GST_BUFFER_OFFSET (buf), GST_BUFFER_SIZE (buf));
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  GST_DEBUG_CATEGORY_INIT (debug_bs, "bytestream", 0, "bytestream library");

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstbytestream",
  "a byte-oriented layer on top of buffer-passing",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
