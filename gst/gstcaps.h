/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
typedef GstCapsFactory *GstCapsListFactory[];

typedef enum {
  GST_CAPS_ALWAYS	= 1,
  GST_CAPS_MAYBE	= 2,
} GstCapsDefinition;

struct _GstCaps {
  guint16 id;			/* type id (major type) */

  GstProps *properties;		/* properties for this capability */
};

/* initialize the subsystem */
void 		_gst_caps_initialize		(void);

GstCaps*	gst_caps_new			(gchar *mime);
GstCaps*	gst_caps_register		(GstCapsFactory *factory);

gboolean 	gst_caps_check_compatibility 	(GstCaps *caps1, GstCaps *caps2);

xmlNodePtr      gst_caps_save_thyself    	(GstCaps *caps, xmlNodePtr parent);
GstCaps* 	gst_caps_load_thyself    	(xmlNodePtr parent);

#endif /* __GST_CAPS_H__ */
