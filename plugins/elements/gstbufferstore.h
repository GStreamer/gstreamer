/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttypefind.h: keep an easily accessible list of all buffers
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


#ifndef __GST_BUFFER_STORE_H__
#define __GST_BUFFER_STORE_H__

#include <gst/gstbuffer.h>
#include <gst/gstinfo.h>
#include <gst/gstmarshal.h>

G_BEGIN_DECLS

#define GST_TYPE_BUFFER_STORE		(gst_buffer_store_get_type ())
#define GST_BUFFER_STORE(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_BUFFER_STORE, GstBufferStore))
#define GST_IS_BUFFER_STORE(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_BUFFER_STORE))
#define GST_BUFFER_STORE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_BUFFER_STORE, GstBufferStoreClass))
#define GST_IS_BUFFER_STORE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_BUFFER_STORE))
#define GST_BUFFER_STORE_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_BUFFER_STORE, GstBufferStoreClass))

typedef struct _GstBufferStore 		GstBufferStore;
typedef struct _GstBufferStoreClass 	GstBufferStoreClass;

struct _GstBufferStore {
  GObject		object;

  GList *		buffers;
};

struct _GstBufferStoreClass {
  GObjectClass		parent_class;

  /* signals */
  void			(* cleared)			(GstBufferStore *	store);
  gboolean		(* buffer_added)		(GstBufferStore *	store,
							 GstBuffer *		buffer);
};

GType			gst_buffer_store_get_type	(void);

GstBufferStore *	gst_buffer_store_new		(void);
void			gst_buffer_store_clear		(GstBufferStore *	store);

gboolean      		gst_buffer_store_add_buffer	(GstBufferStore *	store,
							 GstBuffer *		buffer);

GstBuffer *		gst_buffer_store_get_buffer	(GstBufferStore *	store,
							 guint64		offset,
							 guint			size);
guint			gst_buffer_store_get_size	(GstBufferStore *	store,
							 guint64		offset);

G_END_DECLS

#endif /* __GST_BUFFER_STORE_H__ */
