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

G_BEGIN_DECLS

typedef struct _GstBuffer GstBuffer;
typedef struct _GstBufferPool GstBufferPool;

#define GST_BUFFER_TRACE_NAME		"GstBuffer"
#define GST_BUFFER_POOL_TRACE_NAME	"GstBufferPool"

extern GType _gst_buffer_type;
extern GType _gst_buffer_pool_type;

#define GST_TYPE_BUFFER         		(_gst_buffer_type)
#define GST_TYPE_BUFFER_POOL    		(_gst_buffer_pool_type)

#define GST_BUFFER(buf)         		((GstBuffer *)(buf))
#define GST_BUFFER_POOL(pool)   		((GstBufferPool *)(pool))
#define GST_IS_BUFFER(buf)      		(GST_DATA_TYPE(buf) == GST_TYPE_BUFFER)
#define GST_IS_BUFFER_POOL(buf) 		(GST_DATA_TYPE(buf) == GST_TYPE_BUFFER_POOL)

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
#define GST_BUFFER_FORMAT(buf)			(GST_BUFFER(buf)->format)
#define GST_BUFFER_OFFSET(buf)			(GST_BUFFER(buf)->offset)
#define GST_BUFFER_OFFSET_END(buf)		(GST_BUFFER(buf)->offset_end)
#define GST_BUFFER_BUFFERPOOL(buf)		(GST_BUFFER(buf)->pool)
#define GST_BUFFER_POOL_PRIVATE(buf)		(GST_BUFFER(buf)->pool_private)

#define GST_BUFFER_OFFSET_NONE	((guint64)-1)
#define GST_BUFFER_MAXSIZE_NONE	((guint)0)

#define GST_BUFFER_DURATION_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buffer)))
#define GST_BUFFER_TIMESTAMP_IS_VALID(buffer)	(GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
#define GST_BUFFER_OFFSET_IS_VALID(buffer)	(GST_BUFFER_OFFSET (buffer) != GST_BUFFER_OFFSET_NONE)
#define GST_BUFFER_OFFSET_END_IS_VALID(buffer)	(GST_BUFFER_OFFSET_END (buffer) != GST_BUFFER_OFFSET_NONE)
#define GST_BUFFER_MAXSIZE_IS_VALID(buffer)	(GST_BUFFER_MAXSIZE (buffer) != GST_BUFFER_MAXSIZE_NONE)

typedef enum {
  GST_BUFFER_READONLY   = GST_DATA_READONLY,
  GST_BUFFER_SUBBUFFER  = GST_DATA_FLAG_LAST,
  GST_BUFFER_ORIGINAL,
  GST_BUFFER_DONTFREE,
  GST_BUFFER_DISCONTINUOUS,
  GST_BUFFER_KEY_UNIT,
  GST_BUFFER_PREROLL,
  GST_BUFFER_FLAG_LAST 	= GST_DATA_FLAG_LAST + 8
} GstBufferFlag;

struct _GstBuffer {
  GstData 		 data_type;

  /* pointer to data and its size */
  guint8 		*data;			/* pointer to buffer data */
  guint 		 size;			/* size of buffer data */
  guint			 maxsize;		/* max size of this buffer */

  /* timestamp */
  GstClockTime		 timestamp;		
  GstClockTime		 duration;		

  /* media specific offset
   * for video frames, this could be the number of frames,
   * for audio data, this could be the number of audio samples,
   * for file data or compressed data, this could be the number of bytes
   * offset_end is the last offset contained in the buffer. The format specifies
   * the meaning of both of them exactly.
   */
  guint64		 offset;
  guint64		 offset_end;

  /* this is a pointer to the buffer pool (if any) */
  GstBufferPool		*pool;
  /* pointer to pool private data of parent buffer in case of a subbuffer */
  gpointer 		 pool_private;

  GST_STRUCT_PADDING
};

/* bufferpools */

typedef GstBuffer*	(*GstBufferPoolBufferNewFunction)	(GstBufferPool *pool, guint64 offset, 
								 guint size, gpointer user_data);
typedef GstBuffer* 	(*GstBufferPoolBufferCopyFunction)	(GstBufferPool *pool, 
								 const GstBuffer *buffer, 
								 gpointer user_data);
typedef void	 	(*GstBufferPoolBufferFreeFunction)	(GstBufferPool *pool, 
								 GstBuffer *buffer, 
								 gpointer user_data);

struct _GstBufferPool {
  GstData 				data;

  gboolean				active;

  GstBufferPoolBufferNewFunction 	buffer_new;
  GstBufferPoolBufferCopyFunction	buffer_copy;
  GstBufferPoolBufferFreeFunction	buffer_free;

  gpointer				user_data;

  GST_STRUCT_PADDING
};

/* allocation */
GType		gst_buffer_get_type		(void);
GstBuffer*	gst_buffer_new	 		(void);
GstBuffer*	gst_buffer_new_and_alloc	(guint size);

/* creating a new buffer from a pool */
GstBuffer*	gst_buffer_new_from_pool 	(GstBufferPool *pool, 
						 guint64 offset, guint size);

#define		gst_buffer_set_data(buf, data, size) 	\
G_STMT_START { 					     	\
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
#define		gst_buffer_free(buf)		gst_data_free (GST_DATA (buf))

/* creating a subbuffer */
GstBuffer*	gst_buffer_create_sub		(GstBuffer *parent, guint offset, guint size);

/* merge, span, or append two buffers, intelligently */
GstBuffer*	gst_buffer_merge		(GstBuffer *buf1, GstBuffer *buf2);
gboolean	gst_buffer_is_span_fast		(GstBuffer *buf1, GstBuffer *buf2);
GstBuffer*	gst_buffer_span			(GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len);

/* --- private --- */
void		_gst_buffer_initialize		(void);

/* functions used by subclasses and bufferpools */
void		gst_buffer_default_free 	(GstBuffer *buffer);
GstBuffer*	gst_buffer_default_copy 	(GstBuffer *buffer);

/* creating a new buffer pools */
GType		gst_buffer_pool_get_type		(void);
GstBufferPool*	gst_buffer_pool_new			(GstDataFreeFunction free,
							 GstDataCopyFunction copy,
							 GstBufferPoolBufferNewFunction buffer_new,
                                                	 GstBufferPoolBufferCopyFunction buffer_copy,
                                                	 GstBufferPoolBufferFreeFunction buffer_free,
							 gpointer user_data);

/* function used by subclasses and bufferpools */
void		gst_buffer_pool_default_free		(GstBufferPool *pool);

/* check if pool is usable */
gboolean	gst_buffer_pool_is_active		(GstBufferPool *pool);
void		gst_buffer_pool_set_active		(GstBufferPool *pool, gboolean active);

#define		gst_buffer_pool_ref(pool)		GST_BUFFER_POOL (gst_data_ref (GST_DATA (pool)))
#define		gst_buffer_pool_ref_by_count(pool,c)	GST_BUFFER_POOL (gst_data_ref_by_count (GST_DATA (pool), c))
#define		gst_buffer_pool_unref(pool)		gst_data_unref (GST_DATA (pool))

/* bufferpool operations */
#define		gst_buffer_pool_copy(pool)		GST_BUFFER_POOL (gst_data_copy (GST_DATA (pool)))
#define		gst_buffer_pool_is_writable(pool)	GST_BUFFER_POOL (gst_data_is_writable (GST_DATA (pool)))
#define		gst_buffer_pool_copy_on_write(pool)	GST_BUFFER_POOL (gst_data_copy_on_write (GST_DATA (pool)))
#define		gst_buffer_pool_free(pool)		gst_data_free (GST_DATA (pool))

void 		gst_buffer_pool_set_user_data		(GstBufferPool *pool, gpointer user_data);
gpointer	gst_buffer_pool_get_user_data		(GstBufferPool *pool);

G_END_DECLS


#endif /* __GST_BUFFER_H__ */
