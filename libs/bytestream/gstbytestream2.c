#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gst/gstinfo.h>
#include "gstbytestream2.h"

static void gst_bytestream2_print_status(GstByteStream2 *bs);
guint8 *gst_bytestream2_assemble(GstByteStream2 *bs, guint32 len);

/**
 * gst_bytestream_new:
 * @pad: the pad to attach the bytestream to
 *
 * creates a bytestream from the given pad
 *
 * Returns: a new #GstByteStream object
 */
GstByteStream2*
gst_bytestream2_new (GstPad *pad)
{
  GstByteStream2 *bs = g_new (GstByteStream2, 1);

  bs->pad = pad;
  bs->flushptr = 0LL;
  bs->size = 0LL;

  bs->curbuf = NULL;
  bs->curbufavail = 0;

  bs->buflist = NULL;
  bs->listcount = 0;
  bs->listavail = 0;

  return bs;
}

// 0 ..... ---------|----|---.......---|----------- ..... N
//                     f
//                  ^tail          ^head
//                     cba
//                     \------la-------/
// \ ..... -----------size-------------/

// get the next buffer
// if the buffer can be merged with the head buffer, do so
// else add it onto the head of the 
static gboolean
gst_bytestream2_get_next_buf(GstByteStream2 *bs) {
  GstBuffer *nextbuf, *lastbuf;
  GSList *end;

  g_print("get_next_buf: pulling buffer\n");
  nextbuf = gst_pad_pull(bs->pad);
  g_print("get_next_buf: got buffer of %d bytes\n",GST_BUFFER_SIZE(nextbuf));

  // first check to see if there's a curbuf
  if (bs->curbuf == NULL) {
    g_print("get_next_buf: no curbuf, filling\n");
    // there isn't, let's fill it
    bs->curbuf = nextbuf;
    bs->curbufavail = GST_BUFFER_SIZE(nextbuf);

  } else {
    // there is, first check to see if there's a list of buffers at all
    if (bs->buflist) {
      g_print("gst_next_buf: there's a buflist, search for the end\n");
      // now find the end of the list
      end = g_slist_last(bs->buflist);
      // get the buffer that's there
      lastbuf = GST_BUFFER(end->data);

      // see if we can marge cheaply
      if (gst_buffer_is_span_fast(lastbuf,nextbuf)) {
        g_print("get_next_buf: merging new buffer with last buf on list\n");
        // it is, let's merge them
        end->data = gst_buffer_merge(lastbuf,nextbuf);
        // add to the length of the list, but not buffer count
        bs->listavail += GST_BUFFER_SIZE(nextbuf);
        // we can ditch the nextbuf then
        gst_buffer_unref(nextbuf);

      // if we can't, we just append this buffer
      } else {
        g_print("get_next_buf: adding new buffer to the end of the list\n");
        end = g_slist_append(end,nextbuf);
        // also need to increment length of list and buffer count
        bs->listcount++;
        bs->listavail += GST_BUFFER_SIZE(nextbuf);
      }

    // if there are no buffers in the list
    } else {
      g_print("get_next_buf: buflist is empty\n");
      // first see if we can merge with curbuf
      if (gst_buffer_is_span_fast(bs->curbuf,nextbuf)) {
        g_print("get_next_buf: merging new buffer with curbuf\n");
        // it is, merge them
        bs->curbuf = gst_buffer_merge(bs->curbuf,nextbuf);
        // add to the length of curbuf that's available
        bs->curbufavail += GST_BUFFER_SIZE(nextbuf);
        // we can unref nextbuf now
        gst_buffer_unref(nextbuf);

      // instead we tack this onto the (empty) list
      } else {
        g_print("get_next_buf: adding new buffer to list\n");
        // put this on the end of the list
        bs->buflist = g_slist_append(bs->buflist,nextbuf);
        // and increment the number of bytes in the list
        bs->listcount++;
        bs->listavail += GST_BUFFER_SIZE(nextbuf);
      }
    }
  }

  return TRUE;
}


static gboolean
gst_bytestream2_fill_bytes(GstByteStream2 *bs, guint32 len) {
//  GSList *walk;
//  GstBuffer *buf;

  // as long as we don't have enough, we get more buffers
  while ((bs->curbufavail + bs->listavail) < len) {
    g_print("fill_bytes: there are %d bytes in curbuf and %d in the list, we need %d\n",bs->curbufavail,bs->listavail,len);
    gst_bytestream2_get_next_buf(bs);
  }

  return TRUE;
}


GstBuffer *
gst_bytestream2_peek (GstByteStream2 *bs, guint32 len) {
  GstBuffer *retbuf = NULL;

  g_return_val_if_fail(bs != NULL, NULL);
  g_return_val_if_fail(len > 0, NULL);

  g_print("peek: asking for %d bytes\n",len);

  // make sure we have enough
  g_print("peek: there are %d in curbuf and %d in the list\n",bs->curbufavail,bs->listavail);
  if (len > bs->listavail) {
    gst_bytestream2_fill_bytes(bs,len);
    g_print("peek: there are now %d in curbuf and %d in the list\n",bs->curbufavail,bs->listavail);
  }

  // if the requested bytes are in the current buffer
  g_print("peek: curbufavail is %d\n",bs->curbufavail);
  if (len <= bs->curbufavail) {
    g_print("peek: there are enough bytes in curbuf (need %d, have %d)\n",len,bs->curbufavail);
    // create a sub-buffer of the curbuf
    retbuf = gst_buffer_create_sub(bs->curbuf, GST_BUFFER_SIZE(bs->curbuf) - bs->curbufavail, len);

  // otherwise we need to figure out how to assemble one
  } else {
    g_print("peek: current buffer is not big enough for len %d\n",len);

    retbuf = gst_buffer_new();
    GST_BUFFER_SIZE(retbuf) = len;
    GST_BUFFER_DATA(retbuf) = gst_bytestream2_assemble(bs,len);
    GST_BUFFER_OFFSET(retbuf) = GST_BUFFER_OFFSET(bs->curbuf) + (GST_BUFFER_SIZE(bs->curbuf) - bs->curbufavail);
  }

  return retbuf;
}

guint8 *
gst_bytestream2_assemble(GstByteStream2 *bs, guint32 len)
{
  guint8 *data = g_malloc(len);
  GSList *walk;
  guint32 copied = 0;
  GstBuffer *buf;

  // copy the data from the curbuf
  g_print("copying %d bytes from curbuf at %d to *data\n",bs->curbufavail,
          GST_BUFFER_SIZE(bs->curbuf) - bs->curbufavail);
  memcpy(data,GST_BUFFER_DATA(bs->curbuf) + GST_BUFFER_SIZE(bs->curbuf) - bs->curbufavail,
         bs->curbufavail);
  copied += bs->curbufavail;

  // asumption is made that the buffers all exist in the list
  walk = bs->buflist;
  while (copied < len) {
    buf = GST_BUFFER(walk->data);
    if (GST_BUFFER_SIZE(buf) < (len-copied)) {
      g_print("coping %d bytes from buf to output offset %d\n",GST_BUFFER_SIZE(buf),copied);
      memcpy(data+copied,GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
      copied += GST_BUFFER_SIZE(buf);
    } else {
      g_print("coping %d bytes from buf to output offset %d\n",len-copied,copied);
      memcpy(data+copied,GST_BUFFER_DATA(buf),len-copied);
      copied = len;
    }
    walk = g_slist_next(walk);
  }

  return data;
}

gboolean
gst_bytestream2_flush(GstByteStream2 *bs, guint32 len)
{
  GSList *walk;

  g_print("flush: flushing %d bytes\n",len);

  // if the flush is totally in the curbuf, we can just trim those bytes
  // note that if len == curbufavail, this doesn't trigger because we must refill curbuf
  if (len < bs->curbufavail) {
    g_print("trimming %d bytes from curbuf[avail]\n",len);
    bs->curbufavail -= len;

  // otherwise we have to flush at least one full buffer
  } else {
    // we can unref the curbuf and trim that many bytes off
    gst_buffer_unref(bs->curbuf);
    len -= bs->curbufavail;
    g_print("unreffed curbuf, leaving %d bytes still to flush \n",len);

    // repeat until we've flushed enough data
    walk = bs->buflist;
    do {
  g_print("flush: there are %d in curbuf and %d in the list\n",bs->curbufavail,bs->listavail);
      // if the list is empty, so is curbuf
      if (bs->buflist == NULL) {
        g_print("buffer list is totally empty, pulling a new buffer\n");
        gst_bytestream2_get_next_buf(bs);
      // else we can move a buffer down into curbuf
      } else {
        g_print("still some buffers in the list, retrieving from there\n");
        // retrieve the next buffer
        bs->curbuf = GST_BUFFER(bs->buflist->data);
        bs->curbufavail = GST_BUFFER_SIZE(bs->curbuf);
        // pull it off the list
        bs->buflist = g_slist_delete_link(bs->buflist,bs->buflist);
        bs->listavail -= GST_BUFFER_SIZE(bs->curbuf);
      }
      g_print("next buffer in list is at offset %d, is %d bytes long\n",GST_BUFFER_OFFSET(bs->curbuf),
GST_BUFFER_SIZE(bs->curbuf));

      // figure out how much of it (if any) is left
      if (len < GST_BUFFER_SIZE(bs->curbuf)) {
        g_print("removing first %d bytes from the new curbuf\n",len);
        // the buffer is bigger than the remaining bytes to be flushed
        bs->curbufavail = GST_BUFFER_SIZE(bs->curbuf) - len;
        len = 0;
      } else {
        g_print("buffer is totally contained in flush region, unreffing\n");
        // the buffer is only part of the total, unref it
        len -= GST_BUFFER_SIZE(bs->curbuf);
        gst_buffer_unref(bs->curbuf);
        bs->curbuf = NULL;
        bs->curbufavail = 0;
      }
    } while ((len > 0) || (bs->curbuf == NULL));
  }
}

GstBuffer *
gst_bytestream2_read(GstByteStream2 *bs, guint32 len)
{
  GstBuffer *buf = gst_bytestream2_peek(bs,len);
  gst_bytestream2_flush(bs,len);
  return buf;
}

static void
gst_bytestream2_print_status(GstByteStream2 *bs) {
  GSList *walk;
  GstBuffer *buf;

  g_print("flush pointer is at %d\n",bs->flushptr);

  g_print("list has %d bytes available\n",bs->listavail);
  walk = bs->buflist;
  while (walk) {
    buf = GST_BUFFER(walk->data);
    walk = g_slist_next(walk);

    g_print("buffer starts at %d and is %d bytes long\n",GST_BUFFER_OFFSET(buf),GST_BUFFER_SIZE(buf));
  }
}
