/* Gnome-Streamer   
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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



#ifndef __GST_META_H__
#define __GST_META_H__

#include <glib.h> 
 
 
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_META(meta) ((GstMeta *)(meta))


#define GST_META_FLAGS(buf) \
  (GST_META(buf)->flags)
#define GST_META_FLAG_IS_SET(meta,flag) \
  (GST_META_FLAGS(meta) & (flag))
#define GST_META_FLAG_SET(meta,flag) \
  G_STMT_START{ (GST_META_FLAGS(meta) |= (flag)); }G_STMT_END
#define GST_META_FLAG_UNSET(meta,flag) \
  G_STMT_START{ (GST_META_FLAGS(meta) &= ~(flag)); }G_STMT_END


typedef enum {
  GST_META_FREEABLE             = 1 << 0,
} GstMetaFlags;


typedef struct _GstMeta GstMeta;

struct _GstMeta {
  /* locking */
  GMutex *lock;

  /* refcounting */
#ifdef HAVE_ATOMIC_H
  atomic_t refcount;
#else
  int refcount;
#endif

  guint16 type;
  guint16 flags;

  void *data;
  guint16 size;
};


GstMeta *gst_meta_new_size(gint size);
#define gst_meta_new(type) (type *)gst_meta_new_size(sizeof(type))

/* refcounting */
void gst_meta_ref(GstMeta *meta);
void gst_meta_unref(GstMeta *meta);

#ifdef __cplusplus
}
#endif /* __cplusplus */
 
 
#endif /* __GST_BUFFER_H__ */

