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

//
// Define this to add file:line info to each GstBuffer showing
// the location in the source code where the buffer was created.
// 
#define GST_BUFFER_WHERE
//
// Then in gdb, you can `call gst_buffer_print_live()' to get a list
// of allocated GstBuffers and also the file:line where they were
// allocated.
//

#include <gst/gstdata.h>
#include <gst/gstobject.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ATOMIC_H
#include <asm/atomic.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern GType _gst_buffer_type;

#define GST_TYPE_BUFFER		(_gst_buffer_type)
#define GST_BUFFER(buf) 	((GstBuffer *)(buf))
#define GST_IS_BUFFER(buf)	(GST_DATA_TYPE(buf) == GST_TYPE_BUFFER)

#define GST_BUFFER_FLAGS(buf) \
  (GST_BUFFER(buf)->flags)
#define GST_BUFFER_FLAG_IS_SET(buf,flag) \
  (GST_BUFFER_FLAGS(buf) & (1<<(flag)))
#define GST_BUFFER_FLAG_SET(buf,flag) \
  G_STMT_START{ (GST_BUFFER_FLAGS(buf) |= (1<<(flag))); }G_STMT_END
#define GST_BUFFER_FLAG_UNSET(buf,flag) \
  G_STMT_START{ (GST_BUFFER_FLAGS(buf) &= ~(1<<(flag))); }G_STMT_END


#define GST_BUFFER_DATA(buf)		(GST_BUFFER(buf)->data)
#define GST_BUFFER_SIZE(buf)		(GST_BUFFER(buf)->size)
#define GST_BUFFER_OFFSET(buf)		(GST_BUFFER(buf)->offset)
#define GST_BUFFER_MAXSIZE(buf)		(GST_BUFFER(buf)->maxsize)
#define GST_BUFFER_TIMESTAMP(buf)	(GST_BUFFER(buf)->timestamp)
#define GST_BUFFER_MAXAGE(buf)		(GST_BUFFER(buf)->maxage)
#define GST_BUFFER_BUFFERPOOL(buf)	(GST_BUFFER(buf)->pool)
#define GST_BUFFER_PARENT(buf)		(GST_BUFFER(buf)->parent)
#define GST_BUFFER_POOL_PRIVATE(buf)	(GST_BUFFER(buf)->pool_private)
#define GST_BUFFER_COPY_FUNC(buf)	(GST_BUFFER(buf)->copy)
#define GST_BUFFER_FREE_FUNC(buf)	(GST_BUFFER(buf)->free)


#define GST_BUFFER_LOCK(buf)	(g_mutex_lock(GST_BUFFER(buf)->lock))
#define GST_BUFFER_TRYLOCK(buf)	(g_mutex_trylock(GST_BUFFER(buf)->lock))
#define GST_BUFFER_UNLOCK(buf)	(g_mutex_unlock(GST_BUFFER(buf)->lock))


typedef enum {
  GST_BUFFER_READONLY,
  GST_BUFFER_ORIGINAL,
  GST_BUFFER_DONTFREE,

  GST_BUFFER_FLUSH,
  GST_BUFFER_EOS,
  GST_BUFFER_DISCONTINUOUS,
} GstBufferFlags;



typedef struct _GstBuffer GstBuffer;


typedef void       (*GstBufferFreeFunc)	(GstBuffer *buf);
typedef GstBuffer *(*GstBufferCopyFunc)	(GstBuffer *srcbuf);


#include <gst/gstbufferpool.h>

struct _GstBuffer {
  GstData data_type;

  /* locking */
  GMutex *lock;

  /* refcounting */
#ifdef HAVE_ATOMIC_H
  atomic_t refcount;
#define GST_BUFFER_REFCOUNT(buf)	(atomic_read(&(GST_BUFFER((buf))->refcount)))
#else
  int refcount;
#define GST_BUFFER_REFCOUNT(buf)	(GST_BUFFER(buf)->refcount)
#endif

  /* flags */
  guint16 flags;

  /* pointer to data, its size, and offset in original source if known */
  guchar *data;
  guint32 size;
  guint32 maxsize;
  guint32 offset;

#ifdef GST_BUFFER_WHERE
  const gchar *file;
  gint line;
#endif

  /* timestamp */
  gint64 timestamp;
  gint64 maxage;

  /* subbuffer support, who's my parent? */
  GstBuffer *parent;

  /* this is a pointer to the buffer pool (if any) */
  GstBufferPool *pool;
  gpointer pool_private;

  /* utility function pointers */
  GstBufferFreeFunc free;		// free the data associated with the buffer
  GstBufferCopyFunc copy;		// copy the data from one buffer to another
};

#ifdef GST_BUFFER_WHERE

# define GST_WHERE_ARGS const gchar *where_file, gint where_line
# define GST_WHERE_ARGS_ GST_WHERE_ARGS,
# define GST_WHERE_VARS  where_file, where_line
# define GST_WHERE_VARS_ where_file, where_line,

# define gst_buffer_new() \
    gst_buffer_new_loc(__FILE__, __LINE__)
# define gst_buffer_create_sub(parent, offset, size) \
    gst_buffer_create_sub_loc(__FILE__, __LINE__, parent, offset, size)
# define gst_buffer_copy(buffer) \
    gst_buffer_copy_loc(__FILE__, __LINE__, buffer)
# define gst_buffer_merge(buf1, buf2) \
    gst_buffer_merge_loc(__FILE__, __LINE__, buf1, buf2)
# define gst_buffer_span(buf1, offset, buf2, len) \
    gst_buffer_span_loc(__FILE__, __LINE__, buf1, offset, buf2, len)
# define gst_buffer_append(buf, buf2) \
    gst_buffer_append_loc(__FILE__, __LINE__, buf, buf2)

#else /* GST_BUFFER_WHERE */

# define GST_WHERE_ARGS
# define GST_WHERE_ARGS_
# define GST_WHERE_VARS
# define GST_WHERE_VARS_

# define gst_buffer_new() \
    gst_buffer_new_loc()
# define gst_buffer_create_sub(parent, offset, size) \
    gst_buffer_create_sub_loc(parent, offset, size)
# define gst_buffer_copy(buffer) \
    gst_buffer_copy_loc(buffer)
# define gst_buffer_merge(buf1, buf2) \
    gst_buffer_merge_loc(buf1, buf2)
# define gst_buffer_span(buf1, offset, buf2, len) \
    gst_buffer_span_loc(buf1, offset, buf2, len)
# define gst_buffer_append(buf, buf2) \
    gst_buffer_append_loc(buf, buf2)

#endif /* GST_BUFFER_WHERE */

/* initialisation */
void 		_gst_buffer_initialize		(void);
/* creating a new buffer from scratch */
GstBuffer*	gst_buffer_new_loc		(GST_WHERE_ARGS);
GstBuffer*	gst_buffer_new_from_pool 	(GstBufferPool *pool, guint32 offset, guint32 size);

/* creating a subbuffer */
GstBuffer*	gst_buffer_create_sub_loc	(GST_WHERE_ARGS_
						 GstBuffer *parent, guint32 offset, guint32 size);

/* refcounting */
void 		gst_buffer_ref			(GstBuffer *buffer);
void 		gst_buffer_unref		(GstBuffer *buffer);

/* destroying the buffer */
void 		gst_buffer_destroy		(GstBuffer *buffer);

/* copy buffer */
GstBuffer*	gst_buffer_copy_loc		(GST_WHERE_ARGS_
						 GstBuffer *buffer);

/* merge, span, or append two buffers, intelligently */
GstBuffer*	gst_buffer_merge_loc		(GST_WHERE_ARGS_
						 GstBuffer *buf1, GstBuffer *buf2);
GstBuffer*	gst_buffer_span_loc		(GST_WHERE_ARGS_
						 GstBuffer *buf1,guint32 offset,GstBuffer *buf2,guint32 len);
GstBuffer*	gst_buffer_append_loc		(GST_WHERE_ARGS_
						 GstBuffer *buf, GstBuffer *buf2);

gboolean	gst_buffer_is_span_fast		(GstBuffer *buf1, GstBuffer *buf2);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_BUFFER_H__ */
