/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2004 Wim Taymans <wim@fluendo.com>
 *
 * gstelement.h: Header for GstElement
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


#ifndef __GST_ELEMENT_FACTORY_H__
#define __GST_ELEMENT_FACTORY_H__

typedef struct _GstElementFactory GstElementFactory;
typedef struct _GstElementFactoryClass GstElementFactoryClass;

#include <gst/gstconfig.h>
#include <gst/gstelement.h>
#include <gst/gstobject.h>
#include <gst/gstplugin.h>
#include <gst/gstpluginfeature.h>
#include <gst/gstiterator.h>

G_BEGIN_DECLS

typedef struct _GstElementDetails GstElementDetails;

/**
 * GstElementDetails:
 * @longname: long, english name
 * @klass: type of element, as hierarchy
 * @description: what the element is about
 * @author: who wrote this thing?
 *
 * This struct defines the public information about a #GstElement. It contains
 * meta-data about the element that is mostly for the benefit of editors.
 */
/* FIXME: need translatable stuff in here (how handle in registry)? */
struct _GstElementDetails
{
  /*< public > */
  gchar *longname;
  gchar *klass;
  gchar *description;
  gchar *author;

  /*< private > */
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GST_ELEMENT_DETAILS:
 * @longname: long, english name
 * @klass: type of element, as hierarchy
 * @description: what the element is about
 * @author: who wrote this thing?
 *
 * Macro to initialize #GstElementDetails.
 */
#define GST_ELEMENT_DETAILS(longname,klass,description,author)		\
  { longname, klass, description, author, {0} }

/**
 * GST_IS_ELEMENT_DETAILS:
 * @details: the #GstElementDetails to check
 *
 * Tests if element details are initialized.
 */
/* FIXME: what about adding '&& (*__gst_reserved==NULL)' */
#define GST_IS_ELEMENT_DETAILS(details) (					\
  (details) && ((details)->longname != NULL) && ((details)->klass != NULL)	\
  && ((details)->description != NULL) && ((details)->author != NULL))

#define GST_TYPE_ELEMENT_FACTORY 		(gst_element_factory_get_type())
#define GST_ELEMENT_FACTORY(obj)  		(G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactory))
#define GST_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ELEMENT_FACTORY,\
						 GstElementFactoryClass))
#define GST_IS_ELEMENT_FACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ELEMENT_FACTORY))
#define GST_IS_ELEMENT_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ELEMENT_FACTORY))

/**
 * GstElementFactory:
 *
 * The opaque #GstElementFactory data structure.
 */
struct _GstElementFactory {
  GstPluginFeature	parent;

  GType			type;			/* unique GType of element or 0 if not loaded */

  GstElementDetails	details;

  GList *		staticpadtemplates;
  guint			numpadtemplates;

  /* URI interface stuff */
  guint			uri_type;
  gchar **		uri_protocols;

  GList *		interfaces;		/* interfaces this element implements */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstElementFactoryClass {
  GstPluginFeatureClass parent_class;

  gpointer _gst_reserved[GST_PADDING];
};

GType 			gst_element_factory_get_type 		(void);

GstElementFactory *	gst_element_factory_find		(const gchar *name);

GType			gst_element_factory_get_element_type	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_longname	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_klass		(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_description  	(GstElementFactory *factory);
G_CONST_RETURN gchar *	gst_element_factory_get_author		(GstElementFactory *factory);
guint			gst_element_factory_get_num_pad_templates (GstElementFactory *factory);
G_CONST_RETURN GList *	gst_element_factory_get_static_pad_templates (GstElementFactory *factory);
gint			gst_element_factory_get_uri_type	(GstElementFactory *factory);
gchar **		gst_element_factory_get_uri_protocols	(GstElementFactory *factory);

GstElement*		gst_element_factory_create		(GstElementFactory *factory,
								 const gchar *name);
GstElement*		gst_element_factory_make		(const gchar *factoryname, const gchar *name);

void                    __gst_element_factory_add_static_pad_template (GstElementFactory *elementfactory,
                                                                 GstStaticPadTemplate *templ);
void                    __gst_element_factory_add_interface     (GstElementFactory *elementfactory,
                                                                 const gchar *interfacename);
gboolean                gst_element_register            	(GstPlugin *plugin, const gchar *name,
		                                                 guint rank, GType type);



G_END_DECLS

#endif /* __GST_ELEMENT_FACTORY_H__ */
