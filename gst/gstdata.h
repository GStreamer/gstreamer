/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstdata.h: Header for GstData objects (used for data passing)
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


#ifndef __GST_DATA_H__
#define __GST_DATA_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_ATOMIC_H
#include <asm/atomic.h>
#endif

#include <glib-object.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
  GST_DATA_UNKNOWN,
  GST_DATA_NONE,		/* uninitialized */
  /* buffer */
  GST_DATA_BUFFER,
  GST_DATA_BUFFERPOOL,
  /* events */
  GST_EVENT_FIRST,
  /* instream events */
  GST_EVENT_EOS,
  GST_EVENT_DISCONTINUOUS,
  GST_EVENT_NEWMEDIA,
  GST_EVENT_LENGTH,
  /* upstream events */
  GST_EVENT_SEEK,
  GST_EVENT_EMPTY,
  GST_EVENT_FLUSH,
  GST_EVENT_LOCK,
  GST_EVENT_UNLOCK,  
  /* custom events */
  GST_EVENT_CUSTOM,
  GST_EVENT_CUSTOM_LAST		= 0xFFFF,
  /* addable events */
} GstDataType;

/* number of types */
#define GST_OFFSET_TYPES 		3					/* number of different GstOffsetType types */
#define GST_OFFSET_INVALID 		(~((guint64) 0))			/* invalid (or unitialized) offset */
#define GST_OFFSET_IS_INVALID(offset)	(offset == GST_OFFSET_INVALID)
typedef enum {
  GST_OFFSET_TIME	= 0,
  GST_OFFSET_BYTES	= 1,
  GST_OFFSET_FRAMES	= 2,
} GstOffsetType;

#define GST_DATA_MASK			(GST_EVENT_CUSTOM_LAST)

#define GST_DATA(data)			((GstData*)(data))

#define GST_DATA_TYPE(data)		(((GstData*)(data))->type)
#define GST_DATA_OFFSET_TIME(data)	(((GstData*)(data))->offset[GST_OFFSET_TIME])
#define GST_DATA_OFFSET_BYTES(data)	(((GstData*)(data))->offset[GST_OFFSET_BYTES])
#define GST_DATA_OFFSET_FRAMES(data)	(((GstData*)(data))->offset[GST_OFFSET_FRAMES])

/* flags */
#define GST_DATA_FLAGS(data)		(GST_DATA(data)->flags)
#define GST_DATA_FLAG_IS_SET(data,flag)	(GST_DATA_FLAGS(data) & (1<<(flag)))
#define GST_DATA_FLAG_SET(data,flag)	G_STMT_START{ (GST_DATA_FLAGS(data) |= (1<<(flag))); }G_STMT_END
#define GST_DATA_FLAG_UNSET(data,flag) 	G_STMT_START{ (GST_DATA_FLAGS(data) &= ~(1<<(flag))); }G_STMT_END
enum {
  GST_DATA_READONLY,
  /* insert more */
  GST_DATA_FLAG_LAST
};
#define GST_DATA_IS_WRITABLE(data)	(!GST_DATA_FLAG_IS_SET((data), GST_DATA_READONLY))

typedef struct _GstData GstData;

typedef void (*GstDataFreeFunction) (GstData *data);

struct _GstData
{
  GstDataType		type;
  /* could go into class */
  GstDataFreeFunction	dispose;
  GstDataFreeFunction	free;
  
  /* refcounting */
#ifdef HAVE_ATOMIC_H
  atomic_t 		refcount;
#else
  gint 			refcount;
  GMutex *		reflock;
#endif
  
  /* flags */
  guint			flags;
  
  /* offset in stream */
  /* FIXME: only instream data needs that */
  guint64		offset[GST_OFFSET_TYPES];
};

/* inheritance */
void			gst_data_init			(GstData *data);
void 			gst_data_copy	 		(GstData *to, const GstData *from);
void 			gst_data_dispose 		(GstData *data);

/* construction */
GstData *		gst_data_new			(GType type);

/* reference counting */
void			gst_data_ref			(GstData* data);
void			gst_data_unref			(GstData* data);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_DATA_H__ */
