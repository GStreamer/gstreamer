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

#include <gst/gstdata.h>
#include <gst/gstclock.h>
#include <gst/gstcaps.h>

G_BEGIN_DECLS

typedef struct _GstBuffer GstBuffer;

typedef void (*GstBufferFreeDataFunc) (GstBuffer *buffer);

#define GST_BUFFER_TRACE_NAME		"GstBuffer"

extern GType _gst_buffer_type;

#define GST_TYPE_BUFFER				(gst_buffer_get_type())

#define GST_BUFFER(buf)				((GstBuffer *)(buf))
#define GST_IS_BUFFER(buf)			(GST_DATA_TYPE(buf) == GST_TYPE_BUFFER)

#define GST_BUFFER_REFCOUNT(buf)		GST_DATA_REFCOUNT(buf)
#define GST_BUFFER_REFCOUNT_VALUE(buf)		GST_DATA_REFCOUNT_VALUE(buf)
#define GST_BUFFER_COPY_FUNC(buf)		GST_DATA_COPY_FUNC(buf)
#define GST_BUFFER_FREE_FUNC(buf)		GST_DATA_FREE_FUNC(buf)

#define GST_BUFFER_FLAGS(buf)                   GST_DATA_FLAGS(buf)
#define GST_BUFFER_FLAG_IS_SET(buf,flag)        GST_DATA_FLAG_IS_SET (buf, flag)
#define GST_BUFFER_FLAG_SET(buf,flag)           GST_DATA_FLAG_SET (buf, flag)
#define GST_BUFFER_FLAG_UNSET(buf,flag)         GST_DATA_FLAG_UNSET (buf, flag)

#define GST_BUFFER_DATA(buf)			(GST_BUFFER(buf)->data)
#define GST_BUFFER_SIZE(buf)			(GST_BUFFER(buf)->size)
#define GST_BUFFER_MAXSIZE(buf)			(GST_BUFFER(buf)->maxsize)
#define GST_BUFFER_TIMESTAMP(buf)		(GST_BUFFER(buf)->timestamp)
#define GST_BUFFER_DURATION(buf)		(GST_BUFFER(buf)->duration)
#define GST_BUFFER_CAPS(buf)			(GST_BUFFER(buf)->caps)
#define GST_BUFFER_OFFSET(buf)			(GST_BUFFER(buf)->offset)
#define GST_BUFFER_OFFSET_END(buf)		(GST_BUFFER(buf)->offset_end)
#define GST_BUFFER_FREE_DATA_FUNC(buf)          (GST_BUFFER(buf)->free_data)
#define GST_BUFFER_PRIVATE(buf)                 (GST_BUFFER(buf)->buffer_private)

#define GST_BUFFER_OFFSET_NONE	((guint64)-1)
#define GST_BUFFER_MAXSIZE_NONE	((guint)0)

#define GST_BUFFER_DURATION_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)))
#define GST_BUFFER_TIMESTAMP_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
#define GST_BUFFER_OFFSET_IS_VALID(buffer)	(GST_BUFFER_OFFSET (buffer) != GST_BUFFER_OFFSET_NONE)
#define GST_BUFFER_OFFSET_END_IS_VALID(buffer)	(GST_BUFFER_OFFSET_END (buffer) != GST_BUFFER_OFFSET_NONE)
#define GST_BUFFER_MAXSIZE_IS_VALID(buffer)	(GST_BUFFER_MAXSIZE (buffer) != GST_BUFFER_MAXSIZE_NONE)

/**
 * GstBufferFlag:
 * @GST_BUFFER_READONLY: the buffer is read-only.
 * @GST_BUFFER_SUBBUFFER: the buffer is a subbuffer, the parent buffer can be
 * found with the GST_BUFFER_POOL_PRIVATE() macro.
 * @GST_BUFFER_ORIGINAL: buffer is not a copy of another buffer.
 * @GST_BUFFER_DONTFREE: do not try to free the data when this buffer is
 * unreferenced.
 * @GST_BUFFER_PREROLL: the buffer is part of a preroll and should not be 
 * displayed.
 * @GST_BUFFER_DISCONT: the buffer marks a discontinuity in the stream.
 * @GST_BUFFER_IN_CAPS: the buffer has been added as a field in a #GstCaps.
 * @GST_BUFFER_GAP: the buffer has been created to fill a gap in the stream.
 * @GST_BUFFER_DELTA_UNIT: this unit cannot be decoded independently.
 * Since 0.8.5
 * @GST_BUFFER_FLAG_LAST: additional flags can be added starting from this flag.
 *
 * A set of buffer flags used to describe properties of a #GstBuffer.
 */
typedef enum {
  GST_BUFFER_READONLY   = GST_DATA_READONLY,
  GST_BUFFER_SUBBUFFER  = GST_DATA_FLAG_LAST,
  GST_BUFFER_ORIGINAL,		/* original data, not copied, not currently used  */
  GST_BUFFER_DONTFREE,		/* buffer data is managed by somebody else and cannot be freeed */
  GST_BUFFER_PREROLL,		/* sample should not be displayed */
  GST_BUFFER_DISCONT,		/* buffer is first after discontinuity in the stream */
  GST_BUFFER_IN_CAPS,		/* buffer is also part of caps */
  GST_BUFFER_GAP,		/* buffer has been created to fill a gap in the stream */
  GST_BUFFER_DELTA_UNIT,	/* can't be used as sync point in stream */
  GST_BUFFER_FLAG_LAST 	= GST_DATA_FLAG_LAST + 8
} GstBufferFlag;

struct _GstBuffer {
  GstData		 data_type;

  /*< public >*/ /* with COW */
  /* pointer to data and its size */
  guint8		*data;			/* pointer to buffer data */
  guint			 size;			/* size of buffer data */
  guint			 maxsize;		/* max size of this buffer */

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

  /*< protected >*/
  GstBufferFreeDataFunc  free_data;
  gpointer		 buffer_private;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
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
#define		gst_buffer_ref(buf)		GST_BUFFER (gst_data_ref (GST_DATA (buf)))
#define		gst_buffer_ref_by_count(buf,c)	GST_BUFFER (gst_data_ref_by_count (GST_DATA (buf), c))
#define		gst_buffer_unref(buf)		gst_data_unref (GST_DATA (buf))
/* copy buffer */
#define		gst_buffer_copy(buf)		GST_BUFFER (gst_data_copy (GST_DATA (buf)))
#define		gst_buffer_is_writable(buf)	gst_data_is_writable (GST_DATA (buf))
#define		gst_buffer_copy_on_write(buf)   GST_BUFFER (gst_data_copy_on_write (GST_DATA (buf)))

#define		gst_buffer_replace(obuf,nbuf)  	gst_data_replace ((GstData **)(obuf), GST_DATA (nbuf))

GstCaps*	gst_buffer_get_caps		(GstBuffer *buffer);
void		gst_buffer_set_caps		(GstBuffer *buffer, GstCaps *caps);

/* creating a subbuffer */
GstBuffer*	gst_buffer_create_sub		(GstBuffer *parent, guint offset, guint size);

/* span, two buffers, intelligently */
gboolean	gst_buffer_is_span_fast		(GstBuffer *buf1, GstBuffer *buf2);
GstBuffer*	gst_buffer_span			(GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len);

/* --- protected --- */
void		_gst_buffer_initialize		(void);

void		gst_buffer_default_free		(GstBuffer *buffer);
GstBuffer*	gst_buffer_default_copy		(GstBuffer *buffer);

G_END_DECLS

#endif /* __GST_BUFFER_H__ */
