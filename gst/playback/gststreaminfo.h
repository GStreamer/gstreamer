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


#ifndef __GST_STREAMINFO_H__
#define __GST_STREAMINFO_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_STREAM_INFO 		(gst_stream_info_get_type())
#define GST_STREAM_INFO(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_STREAM_INFO,GstStreamInfo))
#define GST_STREAM_INFO_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_STREAM_INFO,GstStreamInfoClass))
#define GST_IS_STREAM_INFO(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_STREAM_INFO))
#define GST_IS_STREAM_INFO_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_STREAM_INFO))

typedef struct _GstStreamInfo GstStreamInfo;
typedef struct _GstStreamInfoClass GstStreamInfoClass;

typedef enum {
  GST_STREAM_TYPE_UNKNOWN = 0,
  GST_STREAM_TYPE_AUDIO   = 1,
  GST_STREAM_TYPE_VIDEO   = 2,
  GST_STREAM_TYPE_TEXT    = 3,
} GstStreamType;

struct _GstStreamInfo {
  GObject 	 parent;

  GstPad 	*pad;
  GstStreamType	 type;
  gchar 	*decoder;
};

struct _GstStreamInfoClass {
  GObjectClass 	 parent_class;
};

GType gst_stream_info_get_type (void);

GstStreamInfo* gst_stream_info_new (GstPad *pad, GstStreamType type, gchar *decoder);


G_END_DECLS

#endif /* __GST_STREAMINFO_H__ */

