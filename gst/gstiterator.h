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

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
  GST_ITERATOR_DONE		= 0,
  GST_ITERATOR_OK		= 1,
  GST_ITERATOR_RESYNC		= 2,
  GST_ITERATOR_ERROR		= 3,
} GstIteratorResult;

typedef struct _GstIterator GstIterator;

typedef GstIteratorResult (*GstIteratorNextFunction)	(GstIterator *it, gpointer *result);
typedef void		  (*GstIteratorResyncFunction)	(GstIterator *it);
typedef void		  (*GstIteratorFreeFunction)	(GstIterator *it);

#define GST_ITERATOR(it)  		((GstIterator*)(it))
#define GST_ITERATOR_LOCK(it)  		(GST_ITERATOR(it)->lock)
#define GST_ITERATOR_COOKIE(it) 	(GST_ITERATOR(it)->cookie)
#define GST_ITERATOR_ORIG_COOKIE(it) 	(GST_ITERATOR(it)->master_cookie)

struct _GstIterator {
  GstIteratorNextFunction next;
  GstIteratorResyncFunction resync;
  GstIteratorFreeFunction free;

  GMutex *lock;	
  guint32   cookie;		/* cookie of the iterator */
  guint32  *master_cookie;	/* pointer to guint32 holding the cookie when this
				   iterator was created */
};
	
GstIterator* 		gst_iterator_new		(guint size, 
							 GMutex *lock, 
							 guint32 *master_cookie,
  							 GstIteratorNextFunction next,
  							 GstIteratorResyncFunction resync,
  							 GstIteratorFreeFunction free);

GstIteratorResult	gst_iterator_next		(GstIterator *it, gpointer *result);
void			gst_iterator_resync		(GstIterator *it);
void			gst_iterator_free		(GstIterator *it);

void 			gst_iterator_foreach 		(GstIterator *it, GFunc function, 
							 gpointer user_data);
gpointer 		gst_iterator_find_custom 	(GstIterator *it, gpointer user_data,
							 GCompareFunc func);
GstIterator*		gst_iterator_filter		(GstIterator *it, gpointer user_data,
							 GCompareFunc func);

G_END_DECLS

#endif /* __GST_ITERATOR_H__ */
