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

G_BEGIN_DECLS typedef enum
{
  GST_URI_UNKNOWN,
  GST_URI_SINK,
  GST_URI_SRC
} GstURIType;

#define GST_URI_TYPE_IS_VALID(type) ((type) == GST_URI_SRC || (type) == GST_URI_SINK)

/* uri handler functions */
#define GST_TYPE_URI_HANDLER		(gst_uri_handler_get_type ())
#define GST_URI_HANDLER(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_URI_HANDLER, GstURIHandler))
#define GST_IS_URI_HANDLER(obj) 	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_URI_HANDLER))
#define GST_URI_HANDLER_GET_INTERFACE(obj) 	(G_TYPE_INSTANCE_GET_INTERFACE ((obj), GST_TYPE_URI_HANDLER, GstURIHandlerInterface))
#define GST_URI_HANDLER_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), GST_TYPE_URI_HANDLER, GstURIHandler))

typedef struct _GstURIHandler GstURIHandler;
typedef struct _GstURIHandlerInterface GstURIHandlerInterface;

struct _GstURIHandlerInterface
{
  GTypeInterface parent;

  /* signals */
  void (*new_uri) (GstURIHandler * handler, const gchar * uri);
  /* idea for the future ?
     gboolean           (* require_password)                    (GstURIHandler *        handler,
     gchar **           username,
     gchar **           password);
   */

  /* vtable */

  /* querying capabilities */
    GstURIType (*get_type) (void);
  gchar **(*get_protocols) (void);

  /* using the interface */
  G_CONST_RETURN gchar *(*get_uri) (GstURIHandler * handler);
    gboolean (*set_uri) (GstURIHandler * handler, const gchar * uri);

  /* we might want to add functions here to query features, someone with gnome-vfs knowledge go ahead */

  gpointer _gst_reserved[GST_PADDING];
};

/* general URI functions */

gboolean gst_uri_protocol_is_valid (const gchar * protocol);
gboolean gst_uri_is_valid (const gchar * uri);
gchar *gst_uri_get_protocol (const gchar * uri);
gchar *gst_uri_get_location (const gchar * uri);
gchar *gst_uri_construct (const gchar * protocol, const gchar * location);

GstElement *gst_element_make_from_uri (const GstURIType type,
    const gchar * uri, const gchar * elementname);

/* accessing the interface */
GType gst_uri_handler_get_type (void);

guint gst_uri_handler_get_uri_type (GstURIHandler * handler);
gchar **gst_uri_handler_get_protocols (GstURIHandler * handler);
G_CONST_RETURN gchar *gst_uri_handler_get_uri (GstURIHandler * handler);
gboolean gst_uri_handler_set_uri (GstURIHandler * handler, const gchar * uri);
void gst_uri_handler_new_uri (GstURIHandler * handler, const gchar * uri);

G_END_DECLS
#endif /* __GST_URI_H__ */
