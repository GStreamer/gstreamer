/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstcaps.h: Header for caps subsystem
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


#ifndef __GST_CAPS_H__
#define __GST_CAPS_H__

#include <parser.h> // NOTE: this is xml-config's fault

// Include compatability defines: if libxml hasn't already defined these,
// we have an old version 1.x
#ifndef xmlChildrenNode
#define xmlChildrenNode childs
#define xmlRootNode root
#endif

#include <gst/gstprops.h>

typedef struct _GstCaps GstCaps;
typedef gpointer GstCapsFactoryEntry;
typedef GstCapsFactoryEntry GstCapsFactory[];

struct _GstCaps {
  gchar *name;			/* the name of this caps */

  guint16 id;			/* type id (major type) */

  GstProps *properties;		/* properties for this capability */
};

/* initialize the subsystem */
void		_gst_caps_initialize			(void);

GstCaps*	gst_caps_new				(const gchar *name, const gchar *mime);
GstCaps*	gst_caps_new_with_props			(const gchar *name, const gchar *mime, GstProps *props);
GstCaps*	gst_caps_register			(GstCapsFactory *factory);
GstCaps*	gst_caps_register_count			(GstCapsFactory *factory, guint *counter);

const gchar*	gst_caps_get_name			(GstCaps *caps);
void		gst_caps_set_name			(GstCaps *caps, const gchar *name);

const gchar*	gst_caps_get_mime			(GstCaps *caps);
void		gst_caps_set_mime			(GstCaps *caps, const gchar *mime);

guint16		gst_caps_get_type_id			(GstCaps *caps);
void		gst_caps_set_type_id			(GstCaps *caps, guint16 typeid);

GstCaps*	gst_caps_set_props			(GstCaps *caps, GstProps *props);
GstProps*	gst_caps_get_props			(GstCaps *caps);

gboolean	gst_caps_check_compatibility		(GstCaps *fromcaps, GstCaps *tocaps);
gboolean	gst_caps_list_check_compatibility	(GList *fromcaps, GList *tocaps);

xmlNodePtr      gst_caps_save_thyself			(GstCaps *caps, xmlNodePtr parent);
GstCaps*	gst_caps_load_thyself			(xmlNodePtr parent);

#endif /* __GST_CAPS_H__ */
