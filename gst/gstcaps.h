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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstCaps GstCaps;

extern GType _gst_caps_type;

#define GST_TYPE_CAPS  (_get_caps_type)


#define GST_CAPS(caps) \
  ((GstCaps *)(caps))

#define GST_CAPS_IS_FIXED(caps)		((caps)->fixed)
#define GST_CAPS_IS_CHAINED(caps)  	((caps)->next)

/* CR1: id is an int corresponding to the quark for the mime type because
 * it's really fast when doing a first-pass check for caps compatibility */
struct _GstCaps {
  gchar 	*name;			/* the name of this caps */
  guint16 	id;			/* type id (major type) representing 
					   the mime type */

  guint 	refcount;		
  gboolean 	fixed;			/* this caps doesn't contain variable properties */

  GstProps 	*properties;		/* properties for this capability */

  GstCaps 	*next;			/* not with a GList for efficiency */
};

/* factory macros which make it easier for plugins to instantiate */

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

void		gst_caps_debug				(GstCaps *caps, const gchar *label);

GstCaps*	gst_caps_copy				(GstCaps *caps);
GstCaps*	gst_caps_copy_first			(GstCaps *caps);
GstCaps*	gst_caps_copy_on_write			(GstCaps *caps);

const gchar*	gst_caps_get_name			(GstCaps *caps);
void		gst_caps_set_name			(GstCaps *caps, const gchar *name);

const gchar*	gst_caps_get_mime			(GstCaps *caps);
void		gst_caps_set_mime			(GstCaps *caps, const gchar *mime);

guint16		gst_caps_get_type_id			(GstCaps *caps);
void		gst_caps_set_type_id			(GstCaps *caps, guint16 type_id);

GstCaps*	gst_caps_set_props			(GstCaps *caps, GstProps *props);
GstProps*	gst_caps_get_props			(GstCaps *caps);

#define		gst_caps_set(caps, name, args...)	gst_props_set ((caps)->properties, name, ##args)
#define		gst_caps_get(caps, name, args...)	gst_props_get ((caps)->properties, name, ##args)

#define		gst_caps_get_int(caps,name,res)		gst_props_entry_get_int(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_float(caps,name,res)	gst_props_entry_get_float(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_fourcc_int(caps,name,res)	gst_props_entry_get_fourcc_int(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_boolean(caps,name,res)	gst_props_entry_get_boolean(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_string(caps,name,res)	gst_props_entry_get_string(gst_props_get_entry((caps)->properties,name),res)

#define		gst_caps_has_property(caps, name)	gst_props_has_property ((caps)->properties, name)
#define		gst_caps_has_property_typed(caps, name)	gst_props_has_property_typed ((caps)->properties, name)
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_CAPS_H__ */
