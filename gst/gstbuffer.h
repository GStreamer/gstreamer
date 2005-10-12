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

/**
 * GST_BUFFER_TRACE_NAME:
 *
 * The name used for tracing memory allocations.
 */
#define GST_BUFFER_TRACE_NAME		"GstBuffer"

#define GST_TYPE_BUFFER				(gst_buffer_get_type())
#define GST_IS_BUFFER(obj)                      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_BUFFER))
#define GST_IS_BUFFER_CLASS(klass)              (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_BUFFER))
#define GST_BUFFER_GET_CLASS(obj)               (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BUFFER, GstBufferClass))
#define GST_BUFFER(obj)                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_BUFFER, GstBuffer))
#define GST_BUFFER_CLASS(klass)                 (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_BUFFER, GstBufferClass))
#define GST_BUFFER_CAST(obj)                    ((GstBuffer *)(obj))

/**
 * GST_BUFFER_FLAGS:
 * @buf: a #GstBuffer to retrieve the flags from.
 *
 * Gets the #GstBufferFlag flags from this buffer.
 */
#define GST_BUFFER_FLAGS(buf)                   GST_MINI_OBJECT_FLAGS(buf)
/**
 * GST_BUFFER_FLAG_IS_SET:
 * @buf: a #GstBuffer to query flags of.
 * @flag: the #GstBufferFlag to check.
 *
 * Gives the status of a given flag of a buffer.
 */
#define GST_BUFFER_FLAG_IS_SET(buf,flag)        GST_MINI_OBJECT_FLAG_IS_SET (buf, flag)
/**
 * GST_BUFFER_FLAG_SET:
 * @buf: a #GstBuffer to modify flags of.
 * @flag: the #GstBufferFlag to set.
 *
 * Sets a buffer flag.
 */
#define GST_BUFFER_FLAG_SET(buf,flag)           GST_MINI_OBJECT_FLAG_SET (buf, flag)
/**
 * GST_BUFFER_FLAG_UNSET:
 * @buf: a #GstBuffer to modify flags of.
 * @flag: the #GstBufferFlag to clear.
 *
 * Clears a buffer flag.
 */
#define GST_BUFFER_FLAG_UNSET(buf,flag)         GST_MINI_OBJECT_FLAG_UNSET (buf, flag)

/**
 * GST_BUFFER_DATA:
 * @buf: a #GstBuffer to get data pointer of.
 *
 * Retrieves a pointer to the data element of this buffer.
 */
#define GST_BUFFER_DATA(buf)			(GST_BUFFER_CAST(buf)->data)
/**
 * GST_BUFFER_SIZE:
 * @buf: a #GstBuffer to get data size of.
 *
 * Gets the size of the data in this buffer.
 */
#define GST_BUFFER_SIZE(buf)			(GST_BUFFER_CAST(buf)->size)
/**
 * GST_BUFFER_TIMESTAMP:
 * @buf: a #GstBuffer to get the timestamp of.:
 *
 * Gets the timestamp for this buffer.
 */
#define GST_BUFFER_TIMESTAMP(buf)		(GST_BUFFER_CAST(buf)->timestamp)
/**
 * GST_BUFFER_DURATION:
 * @buf: a #GstBuffer to get the duration from.
 *
 * Gets the duration in nanoseconds of the data in the buffer.
 * Value will be %GST_CLOCK_TIME_NONE if the duration is unknown.
 */
#define GST_BUFFER_DURATION(buf)		(GST_BUFFER_CAST(buf)->duration)
/**
 * GST_BUFFER_CAPS:
 * @buf: a #GstBuffer to get the caps of.
 *
 * Gets the caps for this buffer.
 */
#define GST_BUFFER_CAPS(buf)			(GST_BUFFER_CAST(buf)->caps)
/**
 * GST_BUFFER_OFFSET:
 * @buf: a #GstBuffer to get the offset of.
 *
 * Gets the offset in the source file of the beginning of this buffer.
 */
#define GST_BUFFER_OFFSET(buf)			(GST_BUFFER_CAST(buf)->offset)
/**
 * GST_BUFFER_OFFSET_END:
 * @buf: a #GstBuffer to get the offset of.
 *
 * Gets the offset in the source file of the end of this buffer.
 */
#define GST_BUFFER_OFFSET_END(buf)		(GST_BUFFER_CAST(buf)->offset_end)
/**
 * GST_BUFFER_MALLOCDATA:
 * @buf: a #GstBuffer to get access to the malloc_data field
 *
 * If the buffers data should be automatically freed by buffer management at the
 * end of the buffers lifecycle, also set the data to the mallocdata field.
 */
#define GST_BUFFER_MALLOCDATA(buf)		(GST_BUFFER_CAST(buf)->malloc_data)

/**
 * GST_BUFFER_OFFSET_NONE:
 *
 * Constant for no-offset return results.
 */
#define GST_BUFFER_OFFSET_NONE	((guint64)-1)

/**
 * GST_BUFFER_DURATION_IS_VALID:
 * @buffer: the #GstBuffer to check for the duration
 *
 * Tests if the duration is known.
 */
#define GST_BUFFER_DURATION_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)))
/**
 * GST_BUFFER_TIMESTAMP_IS_VALID:
 * @buffer: the #GstBuffer to check for the timestamp
 *
 * Tests if the timestamp is known.
 */
#define GST_BUFFER_TIMESTAMP_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
/**
 * GST_BUFFER_OFFSET_IS_VALID:
 * @buffer: the #GstBuffer to check for the start offset
 *
 * Tests if the start offset is known.
 */
#define GST_BUFFER_OFFSET_IS_VALID(buffer)	(GST_BUFFER_OFFSET (buffer) != GST_BUFFER_OFFSET_NONE)
/**
 * GST_BUFFER_OFFSET_END_IS_VALID:
 * @buffer: the #GstBuffer to check for the end offset
 *
 * Tests if the end offset is known.
 */
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
  GST_BUFFER_FLAG_READONLY   = GST_MINI_OBJECT_FLAG_READONLY,
  GST_BUFFER_FLAG_ORIGINAL   = (GST_MINI_OBJECT_FLAG_LAST << 0),	/* original data, not copied, not currently used  */
  GST_BUFFER_FLAG_PREROLL    = (GST_MINI_OBJECT_FLAG_LAST << 1),	/* sample should not be displayed */
  GST_BUFFER_FLAG_DISCONT    = (GST_MINI_OBJECT_FLAG_LAST << 2),	/* buffer is first after discontinuity in the stream */
  GST_BUFFER_FLAG_IN_CAPS    = (GST_MINI_OBJECT_FLAG_LAST << 3),	/* buffer is also part of caps */
  GST_BUFFER_FLAG_GAP        = (GST_MINI_OBJECT_FLAG_LAST << 4),	/* buffer has been created to fill a gap in the stream */
  GST_BUFFER_FLAG_DELTA_UNIT = (GST_MINI_OBJECT_FLAG_LAST << 5),	/* can't be used as sync point in stream */
  /* padding */
  GST_BUFFER_FLAG_LAST       = (GST_MINI_OBJECT_FLAG_LAST << 8)
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

  guint8                *malloc_data;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBufferClass {
  GstMiniObjectClass    mini_object_class;

};

/* allocation */
GType		gst_buffer_get_type		(void);
GstBuffer*	gst_buffer_new			(void);
GstBuffer*	gst_buffer_new_and_alloc	(guint size);

/**
 * gst_buffer_set_data:
 * @buf: The buffer to modify
 * @data: The data to set on the buffer
 * @size: The size to set on the buffer
 *
 * A convenience function to set the data and size on a buffer.
 */
#define		gst_buffer_set_data(buf, data, size)	\
G_STMT_START {						\
  GST_BUFFER_DATA (buf) = data;				\
  GST_BUFFER_SIZE (buf) = size;				\
} G_STMT_END

/* refcounting */
/**
 * gst_buffer_ref:
 * @buf: a #GstBuffer to increase the refcount of.
 *
 * Increases the refcount of the given buffer by one.
 */
#define		gst_buffer_ref(buf)		GST_BUFFER_CAST (gst_mini_object_ref (GST_MINI_OBJECT (buf)))
/**
 * gst_buffer_unref:
 * @buf: a #GstBuffer to decrease the refcount of.
 *
 * Decreases the refcount of the buffer. If the refcount reaches 0, the buffer
 * will be freed.
 */
#define		gst_buffer_unref(buf)		gst_mini_object_unref (GST_MINI_OBJECT (buf))

/* copy buffer */
/**
 * gst_buffer_copy:
 * @buf: a #GstBuffer to copy.
 *
 * Copies the given buffer using the copy function of the parent #GstData
 * structure.
 */
#define		gst_buffer_copy(buf)		GST_BUFFER_CAST (gst_mini_object_copy (GST_MINI_OBJECT (buf)))
/**
 * gst_buffer_is_writable:
 * @buf: a #GstBuffer to check
 *
 * Tests if you can safely write data into a buffer's data array.
 */
#define		gst_buffer_is_writable(buf)	gst_mini_object_is_writable (GST_MINI_OBJECT (buf))
/**
 * gst_buffer_make_writable:
 * @buf: a #GstBuffer to make writable
 *
 * Makes a writable buffer from the given buffer.
 */
#define		gst_buffer_make_writable(buf)   GST_BUFFER_CAST (gst_mini_object_make_writable (GST_MINI_OBJECT (buf)))

/**
 * gst_buffer_replace:
 * @obuf: a #GstBuffer to receive the data
 * @nbuf: a #GstBuffer to take the data from
 *
 * Replaces the data in @obuf with the one in @nbuf
 */
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
