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

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstByteStream GstByteStream;

struct _GstByteStream {
  GstPad *pad;

  GstEvent *    event;

  GSList 	*buflist;
  guint32 	headbufavail;
  guint32 	listavail;

  // we keep state of assembled pieces
  guint8	*assembled;
  guint32	assembled_len;
};

GstByteStream*		gst_bytestream_new		(GstPad *pad);
void			gst_bytestream_destroy		(GstByteStream *bs);

GstBuffer*		gst_bytestream_read		(GstByteStream *bs, guint32 len);
GstBuffer*		gst_bytestream_peek		(GstByteStream *bs, guint32 len);
guint8*			gst_bytestream_peek_bytes	(GstByteStream *bs, guint32 len);
gboolean		gst_bytestream_flush		(GstByteStream *bs, guint32 len);
void                    gst_bytestream_flush_fast       (GstByteStream * bs, guint32 len);
void                    gst_bytestream_get_status	(GstByteStream *bs, guint32 *avail_out, GstEvent **event_out);

void 			gst_bytestream_print_status	(GstByteStream *bs);

#endif /* __GST_BYTESTREAM_H__ */
