/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 *
 * gstiterator.h: Header for GstIterator
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

#ifndef __GST_ITERATOR_H__
#define __GST_ITERATOR_H__

#include <glib-object.h> /* for GValue in the fold */
#include "gstconfig.h"

G_BEGIN_DECLS

typedef enum {
  GST_ITERATOR_DONE	= 0, /* no more items in the iterator */
  GST_ITERATOR_OK	= 1, /* item retrieved */
  GST_ITERATOR_RESYNC	= 2, /* datastructures changed while iterating */
  GST_ITERATOR_ERROR	= 3, /* some error happened */
} GstIteratorResult;

typedef struct _GstIterator GstIterator;

typedef enum {
  GST_ITERATOR_ITEM_SKIP	= 0, /* skip item */
  GST_ITERATOR_ITEM_PASS	= 1, /* return item */
  GST_ITERATOR_ITEM_END		= 2, /* stop after this item */
} GstIteratorItem;

typedef void		  (*GstIteratorDisposeFunction)	(gpointer owner);

typedef GstIteratorResult (*GstIteratorNextFunction)	(GstIterator *it, gpointer *result);
typedef GstIteratorItem	  (*GstIteratorItemFunction)	(GstIterator *it, gpointer item);
typedef void		  (*GstIteratorResyncFunction)	(GstIterator *it);
typedef void		  (*GstIteratorFreeFunction)	(GstIterator *it);

typedef gboolean	  (*GstIteratorFoldFunction)    (gpointer item, GValue *ret, gpointer user_data);

#define GST_ITERATOR(it)		((GstIterator*)(it))
#define GST_ITERATOR_LOCK(it)		(GST_ITERATOR(it)->lock)
#define GST_ITERATOR_COOKIE(it)		(GST_ITERATOR(it)->cookie)
#define GST_ITERATOR_ORIG_COOKIE(it)	(GST_ITERATOR(it)->master_cookie)

struct _GstIterator {
  GstIteratorNextFunction next;
  GstIteratorItemFunction item;
  GstIteratorResyncFunction resync;
  GstIteratorFreeFunction free;

  GstIterator *pushed;		/* pushed iterator */

  GMutex   *lock;
  guint32   cookie;		/* cookie of the iterator */
  guint32  *master_cookie;	/* pointer to guint32 holding the cookie when this
				   iterator was created */

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

/* creating iterators */
GstIterator*		gst_iterator_new		(guint size,
							 GMutex *lock,
							 guint32 *master_cookie,
							 GstIteratorNextFunction next,
							 GstIteratorItemFunction item,
							 GstIteratorResyncFunction resync,
							 GstIteratorFreeFunction free);

GstIterator*		gst_iterator_new_list		(GMutex *lock,
							 guint32 *master_cookie,
							 GList **list,
							 gpointer owner,
							 GstIteratorItemFunction item,
							 GstIteratorDisposeFunction free);

/* using iterators */
GstIteratorResult	gst_iterator_next		(GstIterator *it, gpointer *result);
void			gst_iterator_resync		(GstIterator *it);
void			gst_iterator_free		(GstIterator *it);

void			gst_iterator_push		(GstIterator *it, GstIterator *other);

/* higher-order functions that operate on iterators */
GstIterator*		gst_iterator_filter		(GstIterator *it, GCompareFunc func,
                                                         gpointer user_data);
GstIteratorResult	gst_iterator_fold		(GstIterator *iter,
                                                         GstIteratorFoldFunction func,
                                                         GValue *ret, gpointer user_data);
GstIteratorResult	gst_iterator_foreach		(GstIterator *iter,
                                                         GFunc func, gpointer user_data);
gpointer		gst_iterator_find_custom	(GstIterator *it, GCompareFunc func,
                                                         gpointer user_data);

G_END_DECLS

#endif /* __GST_ITERATOR_H__ */
