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


#include <stdlib.h>
#include <gstriff.h>


GstRiff *gst_riff_new() {
  GstRiff *riff;

  riff = (GstRiff *)malloc(sizeof(GstRiff));
  g_return_val_if_fail(riff != NULL, NULL);

  riff->form = 0;
  riff->chunks = NULL;
  riff->state = 0;
  riff->curoffset = 0;
  riff->nextlikely = 0;

  return riff;
}

gint gst_riff_next_buffer(GstRiff *riff,GstBuffer *buf,gulong off) {
  gulong last;
  GstRiffChunk *chunk;

  g_return_val_if_fail(riff != NULL, 0);
  g_return_val_if_fail(buf != NULL, 0);
  g_return_val_if_fail(GST_BUFFER_DATA(buf) != NULL, 0);

  last = off + GST_BUFFER_SIZE(buf);

  if (off == 0) {
    gulong *words = (gulong *)GST_BUFFER_DATA(buf);

    /* verify this is a valid RIFF file, first of all */
    if (words[0] != gst_riff_fourcc_to_id("RIFF")) {
      riff->state = GST_RIFF_ENOTRIFF;
      return riff->state;
    }
    riff->form = words[2];
/*    g_print("form is 0x%08x '%s'\n",words[2],gst_riff_id_to_fourcc(words[2])); */
    riff->nextlikely = 12;	/* skip 'RIFF', length, and form */
  }

  /* loop while the next likely chunk header is in this buffer */
  while ((riff->nextlikely+8) < last) {
    gulong *words = (gulong *)((guchar *)GST_BUFFER_DATA(buf) + riff->nextlikely);

/*    g_print("next likely chunk is at offset 0x%08x\n",riff->nextlikely); */
    chunk = (GstRiffChunk *)malloc(sizeof(GstRiffChunk));
    g_return_val_if_fail(chunk != NULL,0);
    chunk->offset = riff->nextlikely+8;	/* point to the actual data */
    chunk->id = words[0];
    chunk->size = words[1];
/*    g_print("chunk id is 0x%08x '%s' and is 0x%08x long\n",words[0], */
/*            gst_riff_id_to_fourcc(words[0]),words[1]); */
    riff->nextlikely += 8 + chunk->size;	/* doesn't include hdr */
    riff->chunks = g_list_prepend(riff->chunks,chunk);
  }

  return 0;
}


gulong gst_riff_fourcc_to_id(gchar *fourcc) {
  g_return_val_if_fail(fourcc != NULL,0);

  return (fourcc[0] << 0) | (fourcc[1] << 8) |
         (fourcc[2] << 16) | (fourcc[3] << 24);
}

gchar *gst_riff_id_to_fourcc(gulong id) {
  gchar *fourcc = (gchar *)malloc(5);

  g_return_val_if_fail(fourcc != NULL, NULL);

  fourcc[0] = (id >> 0) & 0xff;
  fourcc[1] = (id >> 8) & 0xff;
  fourcc[2] = (id >> 16) & 0xff;
  fourcc[3] = (id >> 24) & 0xff;
  fourcc[4] = 0;

  return fourcc;
}

GList *gst_riff_get_chunk_list(GstRiff *riff) {
  g_return_val_if_fail(riff != NULL, NULL);

  return riff->chunks;
}

GstRiffChunk *gst_riff_get_chunk(GstRiff *riff,gchar *fourcc) {
  GList *chunk;

  g_return_val_if_fail(riff != NULL, NULL);
  g_return_val_if_fail(fourcc != NULL, NULL);

  chunk = riff->chunks;
  while (chunk) {
    if (((GstRiffChunk *)(chunk->data))->id == gst_riff_fourcc_to_id(fourcc))
      return (GstRiffChunk *)(chunk->data);
    chunk = g_list_next(chunk);
  }

  return NULL;
}

gulong gst_riff_get_nextlikely(GstRiff *riff) {
  g_return_val_if_fail(riff != NULL, 0);

  return riff->nextlikely;
}

/*
    guchar *hchar = (guchar *)(buf->data);
    gulong hlong = *(gulong *)(buf->data);

    g_print("header is 0x%08x native, %02x %02x %02x %02x, '%c%c%c%c'\n",
            hlong,
            hchar[0],hchar[1],hchar[2],hchar[3],
            hchar[0],hchar[1],hchar[2],hchar[3]);
    g_print("header 0x%08x translates to '%s'\n",hlong,
            gst_riff_id_to_fourcc(hlong));
    g_print("header 0x%08x trancodes to 0x%08x\n",hlong,
            gst_riff_fourcc_to_id(gst_riff_id_to_fourcc(hlong)));
*/
