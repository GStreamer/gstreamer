/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbytestream.h: Header for various utility functions
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

#include <gst/gstpad.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstByteStream GstByteStream;

struct _GstByteStream 
{
  GstPad *pad;
  GstBuffer *buffer;
  guint64 index;
  guint64 pos;
  guint64 size;
};

GstByteStream*		gst_bytestream_new		(GstPad *pad);
void			gst_bytestream_destroy		(GstByteStream *bs);

GstBuffer*		gst_bytestream_peek		(GstByteStream *bs, guint64 len);
GstBuffer*		gst_bytestream_read		(GstByteStream *bs, guint64 len);
gboolean		gst_bytestream_seek		(GstByteStream *bs, guint64 offset);
gint			gst_bytestream_flush		(GstByteStream *bs, guint64 len);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_BYTESTREAM_H__ */
