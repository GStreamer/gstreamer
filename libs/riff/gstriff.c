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


#include <gstriff.h>

//#define debug(format,args...) g_print(format,##args)
#define debug(format,args...)


GstRiff *gst_riff_new(GstRiffCallback function, gpointer data) {
  GstRiff *riff;

  riff = (GstRiff *)g_malloc(sizeof(GstRiff));
  g_return_val_if_fail(riff != NULL, NULL);

  riff->form = 0;
  riff->chunks = NULL;
  riff->state = 0;
  riff->curoffset = 0;
  riff->nextlikely = 0;
  riff->new_tag_found = function;
  riff->callback_data = data;
  riff->incomplete_chunk = NULL;
  riff->dataleft = NULL;

  return riff;
}

gint gst_riff_next_buffer(GstRiff *riff,GstBuffer *buf,gulong off) {
  gulong last, size;
  GstRiffChunk *chunk;

  g_return_val_if_fail (riff != NULL, GST_RIFF_EINVAL);
  g_return_val_if_fail (buf != NULL, GST_RIFF_EINVAL);
  g_return_val_if_fail (GST_BUFFER_DATA(buf) != NULL, GST_RIFF_EINVAL);

  size = GST_BUFFER_SIZE(buf);
  last = off + size;

  debug("offset new buffer 0x%08lx size 0x%08x\n", off, GST_BUFFER_SIZE(buf));

  if (riff->dataleft) {
    gulong newsize;

    debug("recovering left data\n");
    newsize = riff->dataleft_size + size;
    riff->dataleft = g_realloc(riff->dataleft, newsize);
    memcpy (riff->dataleft+riff->dataleft_size, GST_BUFFER_DATA(buf), size);
    gst_buffer_unref (buf);

    buf = gst_buffer_new ();
    GST_BUFFER_DATA(buf) = riff->dataleft;
    GST_BUFFER_SIZE(buf) = newsize;
    off -= riff->dataleft_size;
    //last -= riff->dataleft_size;
    riff->dataleft = NULL;
  }

  if (off == 0) {
    guint32 *words = (guin32 *)GST_BUFFER_DATA(buf);

    // don't even try to parse the head if it's not there FIXME
    if (last < 12) {
      riff->state = GST_RIFF_ENOTRIFF;
      return riff->state;
    }

    //g_print("testing is 0x%08lx '%s'\n",words[0],gst_riff_id_to_fourcc(words[0]));
    /* verify this is a valid RIFF file, first of all */
    if (GUINT32_FROM_LE (words[0]) != GST_RIFF_TAG_RIFF) {
      riff->state = GST_RIFF_ENOTRIFF;
      return riff->state;
    }
    riff->form = GUINT32_FROM_LE (words[2]);
    //g_print("form is 0x%08lx '%s'\n",words[2],gst_riff_id_to_fourcc(words[2]));
    riff->nextlikely = 12;	/* skip 'RIFF', length, and form */
		// all OK here
    riff->incomplete_chunk = NULL;
  }

  // if we have an incomplete chunk from the previous buffer
  if (riff->incomplete_chunk) {
    guint leftover;
    debug("have incomplete chunk %08x filled\n", riff->incomplete_chunk_size);
    leftover = riff->incomplete_chunk->size - riff->incomplete_chunk_size;
    if (leftover <= size) {
      debug("we can fill it from %08x with %08x bytes = %08x\n", riff->incomplete_chunk_size, leftover, riff->incomplete_chunk_size+leftover);
      memcpy(riff->incomplete_chunk->data+riff->incomplete_chunk_size, GST_BUFFER_DATA(buf), leftover);

      if (riff->new_tag_found) {
        riff->new_tag_found(riff->incomplete_chunk, riff->callback_data);
      }
      g_free(riff->incomplete_chunk->data);
      g_free(riff->incomplete_chunk);
      riff->incomplete_chunk = NULL;
    }
    else {
      debug("we cannot fill it %08x >= %08lx\n", leftover, size);
      memcpy(riff->incomplete_chunk->data+riff->incomplete_chunk_size, GST_BUFFER_DATA(buf), size);
      riff->incomplete_chunk_size += size;
      return 0;
    }
  }

  if ((riff->nextlikely+12) > last) {
    guint left = last - riff->nextlikely;
    debug("not enough data next 0x%08x  last 0x%08lx %08x %08x\n",riff->nextlikely, last, left, off);

    riff->dataleft = g_malloc(left);
    riff->dataleft_size = left;
    memcpy(riff->dataleft, GST_BUFFER_DATA(buf)+size-left, left);

    return 0;
  }

  debug("next 0x%08x  last 0x%08lx offset %08x\n",riff->nextlikely, last, off);
  /* loop while the next likely chunk header is in this buffer */
  while ((riff->nextlikely+12) <= last) {
    guin32 *words = (guin32 *)((guchar *)GST_BUFFER_DATA(buf) + riff->nextlikely - off );

    // loop over all of the chunks to check which one is finished
    while (riff->chunks) {
      chunk = g_list_nth_data(riff->chunks, 0);

      debug("next 0x%08x  offset 0x%08lx size 0x%08x\n",riff->nextlikely, chunk->offset, chunk->size);
      if (riff->nextlikely >= chunk->offset+chunk->size) {
        //g_print("found END LIST\n");
      	// we have the end of the chunk on the stack, remove it
      	riff->chunks = g_list_remove(riff->chunks, chunk);
      }
      else break;
    }

    debug("next likely chunk is at offset 0x%08x\n",riff->nextlikely);

    chunk = (GstRiffChunk *)g_malloc (sizeof (GstRiffChunk));
    g_return_val_if_fail (chunk != NULL, GST_RIFF_ENOMEM);

    chunk->offset = riff->nextlikely+8;	/* point to the actual data */
    chunk->id = GUINT32_FROM_LE (words[0]));
    chunk->size = GUINT32_FROM_LE (words[1]);
    chunk->data = (gchar *)(words+2);
    // we need word alignment
    //if (chunk->size & 0x01) chunk->size++;
    chunk->form = GUINT32_FROM_LE (words[2]); /* fill in the form,  might not be valid */


    if (chunk->id == GST_RIFF_TAG_LIST) {
      //g_print("found LIST %s\n", gst_riff_id_to_fourcc(chunk->form));
      riff->nextlikely += 12;	
      // we push the list chunk on our 'stack'
      riff->chunks = g_list_prepend(riff->chunks,chunk);
      // send the buffer to the listener if we have received a function
      if (riff->new_tag_found) {
        riff->new_tag_found(chunk, riff->callback_data);
      }
    }
    else {

      debug("chunk id offset %08x is 0x%08lx '%s' and is 0x%08lx long\n",riff->nextlikely, words[0],
            gst_riff_id_to_fourcc(GUINT32_FROM_LE (words[0])), GUINT32_FROM_LE (words[1]));

      riff->nextlikely += 8 + chunk->size;	/* doesn't include hdr */
      // if this buffer is incomplete
      if (riff->nextlikely > last) {
      	guint left = size - (riff->nextlikely - chunk->size - off);

        //g_print("make incomplete buffer %08x\n", left);
        chunk->data = g_malloc(chunk->size);
        memcpy(chunk->data, (gchar *)(words+2), left);
      	riff->incomplete_chunk = chunk;
      	riff->incomplete_chunk_size = left;
      }
      else {
        // send the buffer to the listener if we have received a function
        if (riff->new_tag_found) {
          riff->new_tag_found(chunk, riff->callback_data);
        }
        g_free(chunk);
      }

      //riff->chunks = g_list_prepend(riff->chunks,chunk);
    }
  }

  return 0;
}


gulong gst_riff_fourcc_to_id(gchar *fourcc) {
  g_return_val_if_fail(fourcc != NULL, 0);

  return (fourcc[0] << 0) | (fourcc[1] << 8) |
         (fourcc[2] << 16) | (fourcc[3] << 24);
}

gchar *gst_riff_id_to_fourcc(gulong id) {
  gchar *fourcc = (gchar *)g_malloc(5);

  g_return_val_if_fail(fourcc != NULL, NULL);

  fourcc[0] = (id >> 0) & 0xff;
  fourcc[1] = (id >> 8) & 0xff;
  fourcc[2] = (id >> 16) & 0xff;
  fourcc[3] = (id >> 24) & 0xff;
  fourcc[4] = 0;

  return fourcc;
}
