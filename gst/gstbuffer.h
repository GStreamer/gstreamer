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

#include <gst/gstobject.h>
#include <gst/gstmeta.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ATOMIC_H
#include <asm/atomic.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_BUFFER(buf) \
  ((GstBuffer *)(buf))

#define GST_BUFFER_FLAGS(buf) \
  (GST_BUFFER(buf)->flags)
#define GST_BUFFER_FLAG_IS_SET(buf,flag) \
  (GST_BUFFER_FLAGS(buf) & (flag))
#define GST_BUFFER_FLAG_SET(buf,flag) \
  G_STMT_START{ (GST_BUFFER_FLAGS(buf) |= (flag)); }G_STMT_END
#define GST_BUFFER_FLAG_UNSET(buf,flag) \
  G_STMT_START{ (GST_BUFFER_FLAGS(buf) &= ~(flag)); }G_STMT_END


#define GST_BUFFER_TYPE(buf)		(GST_BUFFER(buf)->type)
#define GST_BUFFER_DATA(buf)		(GST_BUFFER(buf)->data)
#define GST_BUFFER_SIZE(buf)		(GST_BUFFER(buf)->size)
#define GST_BUFFER_OFFSET(buf)		(GST_BUFFER(buf)->offset)
#define GST_BUFFER_MAXSIZE(buf)		(GST_BUFFER(buf)->maxsize)
#define GST_BUFFER_TIMESTAMP(buf)	(GST_BUFFER(buf)->timestamp)


#define GST_BUFFER_LOCK(buf)	(g_mutex_lock(GST_BUFFER(buf)->lock))
#define GST_BUFFER_TRYLOCK(buf)	(g_mutex_trylock(GST_BUFFER(buf)->lock))
#define GST_BUFFER_UNLOCK(buf)	(g_mutex_unlock(GST_BUFFER(buf)->lock))


typedef enum {
  GST_BUFFER_READONLY,
  GST_BUFFER_ORIGINAL,
  GST_BUFFER_DONTFREE,
  GST_BUFFER_FLUSH,
  GST_BUFFER_EOS,
} GstBufferFlags;


typedef struct _GstBuffer GstBuffer;

#include <gst/gstbufferpool.h>

struct _GstBuffer {
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

  /* data type of this buffer */
  guint16 type;
  /* flags */
  guint16 flags;

  /* pointer to data, its size, and offset in original source if known */
  guchar *data;
  guint32 size;
  guint32 maxsize;
  guint32 offset;

  /* timestamp */
  guint64 timestamp;
  /* max age */
  guint64 maxage;

  /* pointer to metadata, is really lame right now */
  GSList *metas;

  /* subbuffer support, who's my parent? */
  GstBuffer *parent;

  /* this is a pointer to the buffer pool (if any) */
  GstBufferPool *pool;
};

/* initialisation */
void 		_gst_buffer_initialize		(void);
/* creating a new buffer from scratch */
GstBuffer*	gst_buffer_new			(void);
GstBuffer*	gst_buffer_new_from_pool	(GstBufferPool *pool);

/* creating a subbuffer */
GstBuffer*	gst_buffer_create_sub		(GstBuffer *parent, guint32 offset, guint32 size);

/* adding data to a buffer */
GstBuffer*	gst_buffer_append		(GstBuffer *buffer, GstBuffer *append);

/* refcounting */
void 		gst_buffer_ref			(GstBuffer *buffer);
void 		gst_buffer_ref_by_count		(GstBuffer *buffer, int count);
void 		gst_buffer_unref		(GstBuffer *buffer);

/* destroying the buffer */
void 		gst_buffer_destroy		(GstBuffer *buffer);

/* add, retrieve, and remove metadata from the buffer */
void 		gst_buffer_add_meta		(GstBuffer *buffer, GstMeta *meta);
void 		gst_buffer_remove_meta		(GstBuffer *buffer, GstMeta *meta);
GstMeta*	gst_buffer_get_first_meta	(GstBuffer *buffer);
GSList*		gst_buffer_get_metas		(GstBuffer *buffer);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_BUFFER_H__ */
