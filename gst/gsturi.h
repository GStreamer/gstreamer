/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gsturi.h: Header for uri to element mappings
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


#ifndef __GST_URI_H__
#define __GST_URI_H__

#include <glib.h>
#include <gst/gstelement.h>
#include <gst/gstpluginfeature.h>

G_BEGIN_DECLS

/* uri handler functions */
#define GST_TYPE_URI_HANDLER		(gst_uri_handler_get_type ())
#define GST_URI_HANDLER(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_URI_HANDLER, GstURIHandler))
#define GST_IS_URI_HANDLER(obj) 	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_URI_HANDLER))
#define GST_URI_HANDLER_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_URI_HANDLER, GstURIHandlerClass))
#define GST_IS_URI_HANDLER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_URI_HANDLER))
#define GST_URI_HANDLER_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_URI_HANDLER, GstURIHandlerClass))

typedef struct _GstURIHandler GstURIHandler;
typedef struct _GstURIHandlerClass GstURIHandlerClass;

struct _GstURIHandler {
  GstPluginFeature feature;

  /* --- public ---- */
  gchar *uri;              /* The uri that is described */
  gchar *longdesc;         /* description of the uri */
  gchar *element;          /* The element that can handle this uri */
  gchar *property;         /* The property on the element to set the uri */
};

struct _GstURIHandlerClass {
  GstPluginFeatureClass parent;
};

GType			gst_uri_handler_get_type	(void);

GstURIHandler*		gst_uri_handler_new		(const gchar *name, 
		          				 const gchar *uri, const gchar *longdesc, 
							 const gchar *element, gchar *property);

GstURIHandler*		gst_uri_handler_find		(const gchar *name);
GstURIHandler*		gst_uri_handler_find_by_uri	(const gchar *uri);

GstElement*		gst_uri_handler_create		(GstURIHandler *handler, const gchar *name);
GstElement*		gst_uri_handler_make_by_uri	(const gchar *uri, const gchar *name);

/* filters */
gboolean	 	gst_uri_handler_uri_filter 	(GstPluginFeature *feature, const gchar *uri);

G_END_DECLS

#endif /* __GST_URI_H */
