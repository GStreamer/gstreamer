/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstbuffer.h: Header for GstBuffer object
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


#ifndef __GST_BUFFER_H__
#define __GST_BUFFER_H__

#include <gst/gstminiobject.h>
#include <gst/gstclock.h>
#include <gst/gstcaps.h>

G_BEGIN_DECLS

typedef struct _GstBuffer GstBuffer;
typedef struct _GstBufferClass GstBufferClass;

#define GST_BUFFER_TRACE_NAME		"GstBuffer"

#define GST_TYPE_BUFFER				(gst_buffer_get_type())
#define GST_IS_BUFFER(obj)                      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_BUFFER))
#define GST_IS_BUFFER_CLASS(klass)              (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_BUFFER))
#define GST_BUFFER_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BUFFER, GstBufferClass))
#define GST_BUFFER(obj)                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_BUFFER, GstBuffer))
#define GST_BUFFER_CLASS(klass)                 (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_BUFFER, GstBufferClass))
#define GST_BUFFER_CAST(obj)                    ((GstBuffer *)(obj))

#define GST_BUFFER_FLAGS(buf)                   GST_MINI_OBJECT_FLAGS(buf)
#define GST_BUFFER_FLAG_IS_SET(buf,flag)        GST_MINI_OBJECT_FLAG_IS_SET (buf, flag)
#define GST_BUFFER_FLAG_SET(buf,flag)           GST_MINI_OBJECT_FLAG_SET (buf, flag)
#define GST_BUFFER_FLAG_UNSET(buf,flag)         GST_MINI_OBJECT_FLAG_UNSET (buf, flag)

#define GST_BUFFER_DATA(buf)			(GST_BUFFER_CAST(buf)->data)
#define GST_BUFFER_SIZE(buf)			(GST_BUFFER_CAST(buf)->size)
#define GST_BUFFER_TIMESTAMP(buf)		(GST_BUFFER_CAST(buf)->timestamp)
#define GST_BUFFER_DURATION(buf)		(GST_BUFFER_CAST(buf)->duration)
#define GST_BUFFER_CAPS(buf)			(GST_BUFFER_CAST(buf)->caps)
#define GST_BUFFER_OFFSET(buf)			(GST_BUFFER_CAST(buf)->offset)
#define GST_BUFFER_OFFSET_END(buf)		(GST_BUFFER_CAST(buf)->offset_end)
#define GST_BUFFER_MALLOCDATA(buf)		(GST_BUFFER_CAST(buf)->malloc_data)

#define GST_BUFFER_OFFSET_NONE	((guint64)-1)

#define GST_BUFFER_DURATION_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)))
#define GST_BUFFER_TIMESTAMP_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
#define GST_BUFFER_OFFSET_IS_VALID(buffer)	(GST_BUFFER_OFFSET (buffer) != GST_BUFFER_OFFSET_NONE)
#define GST_BUFFER_OFFSET_END_IS_VALID(buffer)	(GST_BUFFER_OFFSET_END (buffer) != GST_BUFFER_OFFSET_NONE)

/**
 * GstBufferFlag:
 * @GST_BUFFER_FLAG_READONLY: the buffer is read-only.
 * @GST_BUFFER_FLAG_ORIGINAL: buffer is not a copy of another buffer.
 * @GST_BUFFER_FLAG_PREROLL: the buffer is part of a preroll and should not be
 * displayed.
 * @GST_BUFFER_FLAG_DISCONT: the buffer marks a discontinuity in the stream.
 * @GST_BUFFER_FLAG_IN_CAPS: the buffer has been added as a field in a #GstCaps.
 * @GST_BUFFER_FLAG_GAP: the buffer has been created to fill a gap in the stream.
 * @GST_BUFFER_FLAG_DELTA_UNIT: this unit cannot be decoded independently.
 * Since 0.8.5
 * @GST_BUFFER_FLAG_LAST: additional flags can be added starting from this flag.
 *
 * A set of buffer flags used to describe properties of a #GstBuffer.
 */
typedef enum {
  GST_BUFFER_FLAG_READONLY = GST_MINI_OBJECT_FLAG_READONLY,
  GST_BUFFER_FLAG_ORIGINAL = (GST_MINI_OBJECT_FLAG_LAST << 0),		/* original data, not copied, not currently used  */
  GST_BUFFER_FLAG_PREROLL = (GST_MINI_OBJECT_FLAG_LAST << 1),		/* sample should not be displayed */
  GST_BUFFER_FLAG_DISCONT = (GST_MINI_OBJECT_FLAG_LAST << 2),		/* buffer is first after discontinuity in the stream */
  GST_BUFFER_FLAG_IN_CAPS = (GST_MINI_OBJECT_FLAG_LAST << 3),		/* buffer is also part of caps */
  GST_BUFFER_FLAG_GAP = (GST_MINI_OBJECT_FLAG_LAST << 4),		/* buffer has been created to fill a gap in the stream */
  GST_BUFFER_FLAG_DELTA_UNIT = (GST_MINI_OBJECT_FLAG_LAST << 5),	/* can't be used as sync point in stream */
  GST_BUFFER_FLAG_LAST = (GST_MINI_OBJECT_FLAG_LAST << 8)
} GstBufferFlag;

struct _GstBuffer {
  GstMiniObject		 mini_object;

  /*< public >*/ /* with COW */
  /* pointer to data and its size */
  guint8		*data;			/* pointer to buffer data */
  guint			 size;			/* size of buffer data */

  /* timestamp */
  GstClockTime		 timestamp;
  GstClockTime		 duration;

  /* the media type of this buffer */
  GstCaps		*caps;

  /* media specific offset
   * for video frames, this could be the number of frames,
   * for audio data, this could be the number of audio samples,
   * for file data or compressed data, this could be the number of bytes
   * offset_end is the last offset contained in the buffer. The format specifies
   * the meaning of both of them exactly.
   */
  guint64		 offset;
  guint64		 offset_end;

  /*< private >*/
  guint8                *malloc_data;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBufferClass {
  GstMiniObjectClass    mini_object_class;

};

/* allocation */
GType		gst_buffer_get_type		(void);
GstBuffer*	gst_buffer_new			(void);
GstBuffer*	gst_buffer_new_and_alloc	(guint size);

#define		gst_buffer_set_data(buf, data, size)	\
G_STMT_START {						\
  GST_BUFFER_DATA (buf) = data;				\
  GST_BUFFER_SIZE (buf) = size;				\
} G_STMT_END

/* refcounting */
#define		gst_buffer_ref(buf)		GST_BUFFER_CAST (gst_mini_object_ref (GST_MINI_OBJECT (buf)))
#define		gst_buffer_unref(buf)		gst_mini_object_unref (GST_MINI_OBJECT (buf))
/* copy buffer */
#define		gst_buffer_copy(buf)		GST_BUFFER_CAST (gst_mini_object_copy (GST_MINI_OBJECT (buf)))
#define		gst_buffer_is_writable(buf)	gst_mini_object_is_writable (GST_MINI_OBJECT (buf))
#define		gst_buffer_make_writable(buf)   GST_BUFFER_CAST (gst_mini_object_make_writable (GST_MINI_OBJECT (buf)))

#define		gst_buffer_replace(obuf,nbuf)	gst_mini_object_replace ((GstMiniObject **)(obuf), GST_MINI_OBJECT (nbuf))

GstCaps*	gst_buffer_get_caps		(GstBuffer *buffer);
void		gst_buffer_set_caps		(GstBuffer *buffer, GstCaps *caps);

/* creating a subbuffer */
GstBuffer*	gst_buffer_create_sub		(GstBuffer *parent, guint offset, guint size);

/* span, two buffers, intelligently */
gboolean	gst_buffer_is_span_fast		(GstBuffer *buf1, GstBuffer *buf2);
GstBuffer*	gst_buffer_span			(GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len);

#define		gst_value_set_buffer(v,b)	gst_value_set_mini_object(v, GST_MINI_OBJECT(b))
#define		gst_value_take_buffer(v,b)	gst_value_take_mini_object(v, GST_MINI_OBJECT(b))
#define		gst_value_get_buffer(v)		GST_BUFFER (gst_value_get_mini_object(v))

/* --- protected --- */
void		_gst_buffer_initialize		(void);

G_END_DECLS

#endif /* __GST_BUFFER_H__ */
