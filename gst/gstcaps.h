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

#include <gst/gstconfig.h>

#include <gst/gstprops.h>

typedef struct _GstCaps GstCaps;

#define GST_CAPS(caps) \
  ((GstCaps *)(caps))

#define GST_CAPS_LOCK(caps)    (g_mutex_lock(GST_CAPS(caps)->lock))
#define GST_CAPS_TRYLOCK(caps) (g_mutex_trylock(GST_CAPS(caps)->lock))
#define GST_CAPS_UNLOCK(caps)  (g_mutex_unlock(GST_CAPS(caps)->lock))

#define GST_CAPS_IS_FIXED(caps)		((caps)->fixed)
#define GST_CAPS_IS_CHAINED(caps)  	((caps)->next)

struct _GstCaps {
  gchar 	*name;			/* the name of this caps */
  guint16 	id;			/* type id (major type) */

  guint 	refcount;		
  GMutex 	*lock;			/* global lock for this capability */
  gboolean 	fixed;			/* this caps doesn't contain variable properties */

  GstProps 	*properties;		/* properties for this capability */

  GstCaps 	*next;
};

#define GST_CAPS_NEW(name, type, a...)          \
gst_caps_new (                                  \
  name,                                         \
  type,                                         \
  gst_props_new (                               \
    a,                                          \
    NULL))

#define GST_CAPS_FACTORY(factoryname, a...) 	\
static GstCaps* 				\
factoryname (void)                              \
{                                               \
  static GstCaps *caps = NULL;			\
  if (!caps) {                              	\
    caps = gst_caps_chain (a, NULL);      	\
  }                                             \
  return caps;                              	\
}

#define GST_CAPS_GET(fact) (fact)()


/* initialize the subsystem */
void		_gst_caps_initialize			(void);

GstCaps*	gst_caps_new				(const gchar *name, const gchar *mime, GstProps *props);
GstCaps*	gst_caps_new_id				(const gchar *name, const guint16 id, GstProps *props);

GstCaps*	gst_caps_unref				(GstCaps *caps);
GstCaps*	gst_caps_ref				(GstCaps *caps);
void		gst_caps_destroy			(GstCaps *caps);

void		gst_caps_debug				(GstCaps *caps);

GstCaps*	gst_caps_copy				(GstCaps *caps);
GstCaps*	gst_caps_copy_1				(GstCaps *caps);
GstCaps*	gst_caps_copy_on_write			(GstCaps *caps);

const gchar*	gst_caps_get_name			(GstCaps *caps);
void		gst_caps_set_name			(GstCaps *caps, const gchar *name);

const gchar*	gst_caps_get_mime			(GstCaps *caps);
void		gst_caps_set_mime			(GstCaps *caps, const gchar *mime);

guint16		gst_caps_get_type_id			(GstCaps *caps);
void		gst_caps_set_type_id			(GstCaps *caps, guint16 type_id);

GstCaps*	gst_caps_set_props			(GstCaps *caps, GstProps *props);
GstProps*	gst_caps_get_props			(GstCaps *caps);

#define		gst_caps_set(caps, name, args...)	gst_props_set ((caps)->properties, name, args)

#define		gst_caps_get_int(caps, name)		gst_props_get_int ((caps)->properties, name)
#define		gst_caps_get_float(caps, name)		gst_props_get_float ((caps)->properties, name)
#define		gst_caps_get_fourcc_int(caps, name)	gst_props_get_fourcc_int ((caps)->properties, name)
#define		gst_caps_get_boolean(caps, name)	gst_props_get_boolean ((caps)->properties, name)
#define		gst_caps_get_string(caps, name)		gst_props_get_string ((caps)->properties, name)

#define		gst_caps_has_property(caps, name)	gst_props_has_property ((caps)->properties, name)
#define		gst_caps_has_fixed_property(caps, name)	gst_props_has_fixed_property ((caps)->properties, name)

GstCaps*	gst_caps_get_by_name			(GstCaps *caps, const gchar *name);

GstCaps*	gst_caps_chain				(GstCaps *caps, ...); 
GstCaps*	gst_caps_append				(GstCaps *caps, GstCaps *capstoadd); 
GstCaps*	gst_caps_prepend			(GstCaps *caps, GstCaps *capstoadd); 

gboolean	gst_caps_check_compatibility		(GstCaps *fromcaps, GstCaps *tocaps);
GstCaps*	gst_caps_intersect			(GstCaps *caps1, GstCaps *caps2);
GstCaps*	gst_caps_normalize			(GstCaps *caps);

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr      gst_caps_save_thyself			(GstCaps *caps, xmlNodePtr parent);
GstCaps*	gst_caps_load_thyself			(xmlNodePtr parent);
#endif

#endif /* __GST_CAPS_H__ */
