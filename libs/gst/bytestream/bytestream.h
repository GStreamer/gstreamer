/* GStreamer
 * Copyright (C) 2001 Erik Walthinsen <omega@temple-baptist.com>
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

#ifndef __GST_BYTESTREAM_H__
#define __GST_BYTESTREAM_H__

#include <glib.h>
#include <gst/gstpad.h>
#include <gst/gstevent.h>

G_BEGIN_DECLS

typedef struct _GstByteStream GstByteStream;

struct _GstByteStream {
  GstPad 	*pad;

  GstEvent 	*event;

  GSList 	*buflist;
  guint32 	 headbufavail;
  guint32 	 listavail;

  /* we keep state of assembled pieces */
  guint8	*assembled;
  guint32	 assembled_len; /* only valid when assembled != NULL */

  /* this is needed for gst_bytestream_tell */
  guint64	 offset;
  guint64	 last_ts;

  /* if we are in the seek state (waiting for DISCONT) */
  gboolean	 in_seek;

  gpointer _gst_reserved[GST_PADDING];
};

GstByteStream*		gst_bytestream_new		(GstPad *pad);
void			gst_bytestream_destroy		(GstByteStream *bs);

void			gst_bytestream_reset		(GstByteStream *bs);
guint32			gst_bytestream_read		(GstByteStream *bs, GstBuffer** buf, guint32 len);
guint64			gst_bytestream_tell		(GstByteStream *bs);
guint64			gst_bytestream_length		(GstByteStream *bs);
gboolean		gst_bytestream_size_hint	(GstByteStream *bs, guint32 size);
gboolean		gst_bytestream_seek		(GstByteStream *bs, gint64 offset, GstSeekType type);
guint32			gst_bytestream_peek		(GstByteStream *bs, GstBuffer** buf, guint32 len);
guint32			gst_bytestream_peek_bytes	(GstByteStream *bs, guint8** data, guint32 len);
gboolean		gst_bytestream_flush		(GstByteStream *bs, guint32 len);
void                    gst_bytestream_flush_fast       (GstByteStream *bs, guint32 len);
void                    gst_bytestream_get_status	(GstByteStream *bs, guint32 *avail_out, GstEvent **event_out);
guint64			gst_bytestream_get_timestamp	(GstByteStream *bs);

void 			gst_bytestream_print_status	(GstByteStream *bs);

G_END_DECLS

#endif /* __GST_BYTESTREAM_H__ */
