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


#ifndef __GST_RIFF_H__
#define __GST_RIFF_H__


#include <gst/gstbuffer.h>
#include <gst/gstplugin.h>


#define GST_RIFF_ENOTRIFF -1		/* not a RIFF file */


typedef struct _GstRiff GstRiff;
typedef struct _GstRiffChunk GstRiffChunk;

struct _GstRiff {
  guint32 form;

  /* list of chunks, most recent at the head */
  GList *chunks;

  /* parse state */
  gint state;
  guint32 curoffset;
  guint32 nextlikely;
};

struct _GstRiffChunk {
  gulong offset;

  guint32 id;
  guint32 size;
};


GstRiff *gst_riff_new();
gint gst_riff_next_buffer(GstRiff *riff,GstBuffer *buf,gulong off);
GList *gst_riff_get_chunk_list(GstRiff *riff);
GstRiffChunk *gst_riff_get_chunk(GstRiff *riff,gchar *fourcc);
GstRiffChunk *gst_riff_get_chunk_number(GstRiff *riff,gint number);

gulong gst_riff_get_nextlikely(GstRiff *riff);

gulong gst_riff_fourcc_to_id(gchar *fourcc);
gchar *gst_riff_id_to_fourcc(gulong id);


#endif /* __GST_RIFF_H__ */
