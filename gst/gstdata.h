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

#include <glib-object.h>
#include <gst/gstatomic.h>
#include <gst/gsttypes.h>

G_BEGIN_DECLS
/* type */
#define GST_DATA(data)		((GstData*)(data))
#define GST_DATA_TYPE(data)	(GST_DATA(data)->type)
/* flags */
#define GST_DATA_FLAGS(data)		(GST_DATA(data)->flags)
#define GST_DATA_FLAG_SHIFT(flag)	(1<<(flag))
#define GST_DATA_FLAG_IS_SET(data,flag)	(GST_DATA_FLAGS(data) & (1<<(flag)))
#define GST_DATA_FLAG_SET(data,flag)	G_STMT_START{ (GST_DATA_FLAGS(data) |= (1<<(flag))); }G_STMT_END
#define GST_DATA_FLAG_UNSET(data,flag) 	G_STMT_START{ (GST_DATA_FLAGS(data) &= ~(1<<(flag))); }G_STMT_END
/* Macros for the GType */
#define GST_TYPE_DATA                   (gst_data_get_type ())
typedef struct _GstData GstData;

typedef void (*GstDataFreeFunction) (GstData * data);
typedef GstData *(*GstDataCopyFunction) (const GstData * data);

typedef enum
{
  GST_DATA_READONLY = 1,

  /* insert more */
  GST_DATA_FLAG_LAST = 8
}
GstDataFlags;

/* refcount */
#define GST_DATA_REFCOUNT(data)			((GST_DATA(data))->refcount)
#define GST_DATA_REFCOUNT_VALUE(data)		(gst_atomic_int_read (&(GST_DATA(data))->refcount))

/* copy/free functions */
#define GST_DATA_COPY_FUNC(data) 		(GST_DATA(data)->copy)
#define GST_DATA_FREE_FUNC(data) 		(GST_DATA(data)->free)


struct _GstData
{
  GType type;

  /* refcounting */
  GstAtomicInt refcount;

  guint16 flags;

  /* utility function pointers, can override default */
  GstDataFreeFunction free;	/* free the data */
  GstDataCopyFunction copy;	/* copy the data */

  gpointer _gst_reserved[GST_PADDING];
};

/* function used by subclasses only */
void gst_data_init (GstData * data, GType type, guint16 flags,
    GstDataFreeFunction free, GstDataCopyFunction copy);
void gst_data_dispose (GstData * data);
void gst_data_copy_into (const GstData * data, GstData * target);

/* basic operations on data */
GstData *gst_data_copy (const GstData * data);
gboolean gst_data_is_writable (GstData * data);
GstData *gst_data_copy_on_write (GstData * data);

/* reference counting */
GstData *gst_data_ref (GstData * data);
GstData *gst_data_ref_by_count (GstData * data, gint count);
void gst_data_unref (GstData * data);

/* GType for GstData */
GType
gst_data_get_type (void)
    G_GNUC_CONST;

G_END_DECLS
#endif /* __GST_DATA_H__ */
