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

G_BEGIN_DECLS

typedef struct _GstBuffer GstBuffer;
typedef struct _GstBufferPool GstBufferPool;

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
#define GST_BUFFER_OFFSET(buf)			(GST_BUFFER(buf)->offset)
#define GST_BUFFER_BUFFERPOOL(buf)		(GST_BUFFER(buf)->pool)
#define GST_BUFFER_POOL_PRIVATE(buf)		(GST_BUFFER(buf)->pool_private)

enum {
  GST_BUFFER_READONLY   = GST_DATA_FLAG_LAST,
  GST_BUFFER_SUBBUFFER,
  GST_BUFFER_ORIGINAL,
  GST_BUFFER_DONTFREE,
  GST_BUFFER_DISCONTINOUS,
  GST_BUFFER_KEY_UNIT,
  GST_BUFFER_PREROLL,

  GST_BUFFER_FLAG_LAST 	= GST_DATA_FLAG_LAST + 8,
};

struct _GstBuffer {
  GstData 		 data_type;

  /* pointer to data and its size */
  guint8 		*data;			/* pointer to buffer data */
  guint 		 size;			/* size of buffer data */
  guint64		 maxsize;		/* max size of this buffer */

  guint64		 timestamp;		
  guint64		 offset;

  /* this is a pointer to the buffer pool (if any) */
  GstBufferPool		*pool;
  /* pointer to pool private data of parent buffer in case of a subbuffer */
  gpointer 		 pool_private;
};

/* bufferpools */

typedef GstBuffer*	(*GstBufferPoolBufferNewFunction)	(GstBufferPool *pool, guint64 offset, guint size, gpointer user_data);
typedef GstBuffer* 	(*GstBufferPoolBufferCopyFunction)	(GstBufferPool *pool, const GstBuffer *buffer, gpointer user_data);
typedef void	 	(*GstBufferPoolBufferFreeFunction)	(GstBufferPool *pool, GstBuffer *buffer, gpointer user_data);

struct _GstBufferPool {
  GstData 				data;

  gboolean				active;

  GstBufferPoolBufferNewFunction 	buffer_new;
  GstBufferPoolBufferCopyFunction	buffer_copy;
  GstBufferPoolBufferFreeFunction	buffer_free;

  gpointer				user_data;
};


/*< private >*/
void		_gst_buffer_initialize		(void);

void		_gst_buffer_free 		(GstBuffer *buf);
GstBuffer*	_gst_buffer_copy 		(GstBuffer *buf);

void		gst_buffer_print_stats		(void);

/* refcounting */
#define		gst_buffer_ref(buf)		GST_BUFFER (gst_data_ref (GST_DATA (buf)))
#define		gst_buffer_ref_by_count(buf,c)	GST_BUFFER (gst_data_ref_by_count (GST_DATA (buf), c))
#define		gst_buffer_unref(buf)		gst_data_unref (GST_DATA (buf))
/* copy buffer */
#define		gst_buffer_copy(buffer)		GST_BUFFER (gst_data_copy (GST_DATA (buffer)))
#define		gst_buffer_copy_on_write(buffer) GST_BUFFER (gst_data_copy_on_write (GST_DATA (buffer)))
#define		gst_buffer_free(buffer)		gst_data_free (GST_DATA (buffer))

/* allocation */
GstBuffer*	gst_buffer_new	 		(void);
GstBuffer*	gst_buffer_new_and_alloc	(guint size);

/* creating a new buffer from a pool */
GstBuffer*	gst_buffer_new_from_pool 	(GstBufferPool *pool, guint64 offset, guint size);

/* creating a subbuffer */
GstBuffer*	gst_buffer_create_sub		(GstBuffer *parent, guint offset, guint size);

/* merge, span, or append two buffers, intelligently */
GstBuffer*	gst_buffer_merge		(GstBuffer *buf1, GstBuffer *buf2);
gboolean	gst_buffer_is_span_fast		(GstBuffer *buf1, GstBuffer *buf2);
GstBuffer*	gst_buffer_span			(GstBuffer *buf1, guint32 offset, GstBuffer *buf2, guint32 len);


/* creating a new buffer pools */
GstBufferPool*	gst_buffer_pool_new			(GstDataFreeFunction free,
							 GstDataCopyFunction copy,
							 GstBufferPoolBufferNewFunction buffer_create,
                                                	 GstBufferPoolBufferCopyFunction buffer_copy,
                                                	 GstBufferPoolBufferFreeFunction buffer_free,
							 gpointer user_data);

gboolean	gst_buffer_pool_is_active		(GstBufferPool *pool);
void		gst_buffer_pool_set_active		(GstBufferPool *pool, gboolean active);

GstBufferPool*	gst_buffer_pool_get_default		(guint size, guint numbuffers);

#define		gst_buffer_pool_ref(buf)		GST_BUFFER_POOL (gst_data_ref (GST_DATA (buf)))
#define		gst_buffer_pool_ref_by_count(buf,c)	GST_BUFFER_POOL (gst_data_ref_by_count (GST_DATA (buf), c))
#define		gst_buffer_pool_unref(buf)		gst_data_unref (GST_DATA (buf))

/* bufferpool operations */
#define		gst_buffer_pool_copy(pool)		GST_BUFFER_POOL (gst_data_copy (GST_DATA (pool)))
#define		gst_buffer_pool_copy_on_write(pool)	GST_BUFFER_POOL (gst_data_copy_on_write (GST_DATA (pool)))
#define		gst_buffer_pool_free(pool)		gst_data_free (GST_DATA (pool))

void 		gst_buffer_pool_set_user_data		(GstBufferPool *pool, gpointer user_data);
gpointer	gst_buffer_pool_get_user_data		(GstBufferPool *pool);


G_END_DECLS


#endif /* __GST_BUFFER_H__ */
