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
#include <gst/gstpluginfeature.h>


/* type of function used to check a stream for equality with type */
typedef GstCaps *(*GstTypeFindFunc) (GstBuffer *buf, gpointer priv);

typedef struct _GstType GstType;
typedef struct _GstTypeDefinition GstTypeDefinition;
typedef struct _GstTypeFactory GstTypeFactory;
typedef struct _GstTypeFactoryClass GstTypeFactoryClass;

struct _GstType {
  guint16 id;			/* type id (assigned) */

  gchar *mime;			/* MIME type */
  gchar *exts;			/* space-delimited list of extensions */

  GSList *factories;		/* factories providing this type */
};

struct _GstTypeDefinition {
  gchar *name;
  gchar *mime;
  gchar *exts;
  GstTypeFindFunc typefindfunc;
};

#define GST_TYPE_TYPE_FACTORY \
  (gst_type_factory_get_type())
#define GST_TYPE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TYPE_FACTORY,GstTypeFactory))
#define GST_TYPE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TYPE_FACTORY,GstTypeFactoryClass))
#define GST_IS_TYPE_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TYPE_FACTORY))
#define GST_IS_TYPE_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TYPE_FACTORY))

struct _GstTypeFactory {
  GstPluginFeature feature;

  gchar *mime;
  gchar *exts;
  GstTypeFindFunc typefindfunc;
};

struct _GstTypeFactoryClass {
  GstPluginFeatureClass parent;
};


GType			gst_type_factory_get_type	(void);

GstTypeFactory*		gst_type_factory_new		(GstTypeDefinition *definition);

GstTypeFactory*		gst_type_factory_find		(const gchar *name);


/* create a new type, or find/merge an existing one */
guint16			gst_type_register		(GstTypeFactory *factory);

/* look up a type by mime or extension */
guint16			gst_type_find_by_mime		(const gchar *mime);
guint16			gst_type_find_by_ext		(const gchar *ext);

/* get GstType by id */
GstType*		gst_type_find_by_id		(guint16 id);

/* get the list of registered types (returns list of GstType!) */
const GList*		gst_type_get_list		(void);

#endif /* __GST_TYPE_H__ */
