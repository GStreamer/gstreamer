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

#include <gnome-xml/parser.h>
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
void 		_gst_caps_initialize			(void);

GstCaps*	gst_caps_new				(gchar *name, gchar *mime);
GstCaps*	gst_caps_new_with_props			(gchar *name, gchar *mime, GstProps *props);
GstCaps*	gst_caps_register			(GstCapsFactory *factory);
GstCaps*	gst_caps_register_count			(GstCapsFactory *factory, guint *count);

GstCaps*	gst_caps_set_props			(GstCaps *caps, GstProps *props);
GstProps*	gst_caps_get_props			(GstCaps *caps);

gboolean 	gst_caps_check_compatibility 		(GstCaps *fromcaps, GstCaps *tocaps);
gboolean 	gst_caps_list_check_compatibility 	(GList *fromcaps, GList *tocaps);

xmlNodePtr      gst_caps_save_thyself    		(GstCaps *caps, xmlNodePtr parent);
GstCaps* 	gst_caps_load_thyself    		(xmlNodePtr parent);

#endif /* __GST_CAPS_H__ */
