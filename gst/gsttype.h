/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsttype.h: Header for type management
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


#ifndef __GST_TYPE_H__
#define __GST_TYPE_H__

#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>


/* type of function used to check a stream for equality with type */
typedef GstCaps *(*GstTypeFindFunc) (GstBuffer *buf,gpointer priv);

typedef struct _GstType GstType;
typedef struct _GstTypeFactory GstTypeFactory;

struct _GstType {
  guint16 id;			/* type id (assigned) */

  gchar *mime;			/* MIME type */
  gchar *exts;			/* space-delimited list of extensions */

  GSList *typefindfuncs;	/* typefind functions */

};

struct _GstTypeFactory {
  gchar *mime;
  gchar *exts;
  GstTypeFindFunc typefindfunc;
};


/* initialize the subsystem */
void 		_gst_type_initialize		(void);

/* create a new type, or find/merge an existing one */
guint16 	gst_type_register		(GstTypeFactory *factory);

/* look up a type by mime or extension */
guint16 	gst_type_find_by_mime		(const gchar *mime);
guint16 	gst_type_find_by_ext		(const gchar *ext);

/* get GstType by id */
GstType*	gst_type_find_by_id		(guint16 id);

/* get the list of registered types (returns list of GstType!) */
GList*		gst_type_get_list		(void);

xmlNodePtr 	gst_typefactory_save_thyself	(GstTypeFactory *factory, xmlNodePtr parent);
GstTypeFactory*	gst_typefactory_load_thyself	(xmlNodePtr parent);

#endif /* __GST_TYPE_H__ */
