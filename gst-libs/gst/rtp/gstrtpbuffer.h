/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org>
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

#ifndef __GST_RTPBUFFER_H__
#define __GST_RTPBUFFER_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstRTPBuffer GstRTPBuffer;
typedef struct _GstRTPBufferClass GstRTPBufferClass;

#define GST_TYPE_RTPBUFFER            (gst_rtpbuffer_get_type())
#define GST_IS_RTPBUFFER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTPBUFFER))
#define GST_IS_RTPBUFFER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTPBUFFER))
#define GST_RTPBUFFER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTPBUFFER, GstRTPBufferClass))
#define GST_RTPBUFFER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTPBUFFER, GstRTPBuffer))
#define GST_RTPBUFFER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTPBUFFER, GstRTPBufferClass))

/* buffer for use rtp packets
 *
 * It contains the payload type, timestamp, timestamp increment 
 * and mark of the rtp packet
 */

struct _GstRTPBuffer {
  GstBuffer buffer;

  guint8 pt;
  guint16 seqnum;
  guint32 timestamp;
  guint32 timestampinc;
  gboolean mark;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstRTPBufferClass {
  GstBufferClass  buffer_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* creating buffers */
GType		gst_rtpbuffer_get_type		(void);

GstRTPBuffer*	gst_rtpbuffer_new		(void);

G_END_DECLS

#endif /* __GST_RTPBUFFER_H__ */

