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

#include <string.h>

//#define DEBUG_ENABLED
#include "riff.h"

#define GST_RIFF_ENCODER_BUF_SIZE    1024

#define ADD_CHUNK(riffenc, chunkid, chunksize) \
{ \
  gst_riff_chunk *chunk;\
  chunk = (gst_riff_chunk *)(riffenc->dataleft + riffenc->nextlikely);\
  chunk->id = chunkid; \
  chunk->size = chunksize; \
  riffenc->nextlikely += sizeof(gst_riff_chunk); \
}

#define ADD_LIST(riffenc, listsize, listtype) \
{ \
  gst_riff_list *list;\
  list = (gst_riff_list *)(riffenc->dataleft + riffenc->nextlikely); \
  list->id = GST_RIFF_TAG_LIST; \
  list->size = listsize; \
  list->type = listtype; \
  riffenc->nextlikely += sizeof(gst_riff_list); \
}
  

GstRiff *gst_riff_encoder_new(guint32 type) {
  GstRiff *riff;
  gst_riff_list *list;

  GST_DEBUG (0,"gst_riff_encoder: making %4.4s encoder\n", (char *)&type);
  riff = (GstRiff *)g_malloc(sizeof(GstRiff));
  g_return_val_if_fail(riff != NULL, NULL);

  riff->form = 0;
  riff->chunks = NULL;
  riff->state = GST_RIFF_STATE_INITIAL;
  riff->curoffset = 0;
  riff->incomplete_chunk = NULL;
  riff->dataleft = g_malloc(GST_RIFF_ENCODER_BUF_SIZE);
  riff->dataleft_size = GST_RIFF_ENCODER_BUF_SIZE;
  riff->nextlikely = 0;

  list = (gst_riff_list *)riff->dataleft;
  list->id = GST_RIFF_TAG_RIFF;
  list->size = 0x00FFFFFF;
  list->type = GST_RIFF_RIFF_AVI;

  riff->nextlikely += sizeof(gst_riff_list);
  
  return riff;
}

gint gst_riff_encoder_avih(GstRiff *riff, gst_riff_avih *head, gulong size) {
  gst_riff_chunk *chunk;

  g_return_val_if_fail(riff->state == GST_RIFF_STATE_INITIAL, GST_RIFF_EINVAL);

  GST_DEBUG (0,"gst_riff_encoder: add avih\n");

  ADD_LIST(riff, 0xB8, GST_RIFF_LIST_hdrl);

  ADD_CHUNK(riff, GST_RIFF_TAG_avih, size);
  
  chunk = (gst_riff_chunk *)(riff->dataleft + riff->nextlikely);
  memcpy(chunk, head, size);
  riff->nextlikely += size;

  riff->state = GST_RIFF_STATE_HASAVIH;
  return GST_RIFF_OK;
}

gint gst_riff_encoder_strh(GstRiff *riff, guint32 fcc_type, gst_riff_strh *head, gulong size) {
  gst_riff_chunk *chunk;

  g_return_val_if_fail(riff->state == GST_RIFF_STATE_HASAVIH ||
		       riff->state == GST_RIFF_STATE_HASSTRF, GST_RIFF_EINVAL);

  GST_DEBUG (0,"gst_riff_encoder: add strh type %08x (%4.4s)\n", fcc_type, (char *)&fcc_type);

  ADD_LIST(riff, 108, GST_RIFF_LIST_strl);

  ADD_CHUNK(riff, GST_RIFF_TAG_strh, size);

  chunk = (gst_riff_chunk *)(riff->dataleft + riff->nextlikely);
  head->type = fcc_type;
  memcpy(chunk, head, size);

  riff->nextlikely += size;

  riff->state = GST_RIFF_STATE_HASSTRH;
  return GST_RIFF_OK;
}

gint gst_riff_encoder_strf(GstRiff *riff, void *format, gulong size) {
  gst_riff_chunk *chunk;

  g_return_val_if_fail(riff->state == GST_RIFF_STATE_HASSTRH, GST_RIFF_EINVAL);

  GST_DEBUG (0,"gst_riff_encoder: add strf\n");

  ADD_CHUNK(riff, GST_RIFF_TAG_strf, size);

  chunk = (gst_riff_chunk *)(riff->dataleft + riff->nextlikely);
  memcpy(chunk, format, size);
  riff->nextlikely += size;

  riff->state = GST_RIFF_STATE_HASSTRF;
  return GST_RIFF_OK;
}

gint gst_riff_encoder_chunk(GstRiff *riff, guint32 chunk_type, void *chunkdata, gulong size) {
  gst_riff_chunk *chunk;

  g_return_val_if_fail(riff->state == GST_RIFF_STATE_HASSTRF ||
		       riff->state == GST_RIFF_STATE_MOVI, GST_RIFF_EINVAL);

  if (riff->state != GST_RIFF_STATE_MOVI) {
    ADD_LIST(riff, 0x00FFFFFF, GST_RIFF_LIST_movi);
    riff->state = GST_RIFF_STATE_MOVI;
  }

  GST_DEBUG (0,"gst_riff_encoder: add chunk type %08x (%4.4s)\n", chunk_type, (char *)&chunk_type);
  
  ADD_CHUNK(riff, chunk_type, size);

  if (chunkdata != NULL) {
    chunk = (gst_riff_chunk *)(riff->dataleft + riff->nextlikely);
    memcpy(chunk, chunkdata, size);
    riff->nextlikely += size + (size&1);
  }

  return GST_RIFF_OK;
}

GstBuffer *gst_riff_encoder_get_buffer(GstRiff *riff) {
  GstBuffer *newbuf;

  newbuf = gst_buffer_new();
  GST_BUFFER_DATA(newbuf) = riff->dataleft;
  GST_BUFFER_SIZE(newbuf) = riff->nextlikely;

  return newbuf;
}

GstBuffer *gst_riff_encoder_get_and_reset_buffer(GstRiff *riff) {
  GstBuffer *newbuf;

  newbuf = gst_riff_encoder_get_buffer(riff);

  riff->dataleft = g_malloc(GST_RIFF_ENCODER_BUF_SIZE);
  riff->dataleft_size = GST_RIFF_ENCODER_BUF_SIZE;
  riff->nextlikely = 0;

  return newbuf;
}

