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

#include <gst/gstprops.h>

G_BEGIN_DECLS

typedef struct _GstCaps GstCaps;

#define	GST_CAPS_TRACE_NAME "GstCaps"

extern GType _gst_caps_type;

#define GST_TYPE_CAPS  (_gst_caps_type)

typedef enum {
  GST_CAPS_UNUSED 	= (1 << 0),	/* unused flag */
  GST_CAPS_FLOATING 	= (1 << 1)	/* caps is floating */
} GstCapsFlags;

#define GST_CAPS(caps)  	((GstCaps *)(caps))

#define GST_CAPS_FLAGS(caps)		((caps)->flags)
#define GST_CAPS_FLAG_IS_SET(caps,flag)	(GST_CAPS_FLAGS (caps) &   (flag))
#define GST_CAPS_FLAG_SET(caps,flag)	(GST_CAPS_FLAGS (caps) |=  (flag))
#define GST_CAPS_FLAG_UNSET(caps,flag)	(GST_CAPS_FLAGS (caps) &= ~(flag))

#define GST_CAPS_REFCOUNT(caps)		((caps)->refcount)
#define GST_CAPS_PROPERTIES(caps)  	((caps)->properties)
#define GST_CAPS_NEXT(caps)  		((caps)->next)

#define GST_CAPS_IS_FIXED(caps)		(((caps)->properties == NULL) || \
					 (GST_PROPS_IS_FIXED ((caps)->properties)))
#define GST_CAPS_IS_FLOATING(caps)	(GST_CAPS_FLAG_IS_SET ((caps), GST_CAPS_FLOATING))
#define GST_CAPS_IS_CHAINED(caps)  	(GST_CAPS_NEXT (caps) != NULL)

#define GST_CAPS_NONE			NULL
#define GST_CAPS_ANY			(gst_caps_get_any())

struct _GstCaps {
  /* --- public --- */
  gchar 	*name;			/* the name of this caps */
  GQuark 	 id;			/* type id (major type) representing 
					   the mime type, it's stored as a GQuark 
					   for speed/space reasons */

  guint16 	 flags;			/* flags */
  guint 	 refcount;		

  GstProps 	*properties;		/* properties for this capability */
  GstCaps 	*next;			/* not with a GList for efficiency */
};

/* factory macros which make it easier for plugins to instantiate */

#ifdef G_HAVE_ISO_VARARGS
#define GST_CAPS_NEW(name, type, ...)           \
gst_caps_new (                                  \
  name,                                         \
  type,                                         \
  gst_props_new (                               \
    __VA_ARGS__,                                \
    NULL))

#define GST_CAPS_FACTORY(factoryname, ...) 	\
static GstCaps* 				\
factoryname (void)                              \
{                                               \
  static GstCaps *caps = NULL;			\
  if (!caps) {                              	\
    caps = gst_caps_chain (__VA_ARGS__, NULL); 	\
  }                                             \
  return gst_caps_ref(caps);                   	\
}
#elif defined(G_HAVE_GNUC_VARARGS)
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
  return gst_caps_ref(caps);                   	\
}
#endif

/* get caps from a factory */
#define GST_CAPS_GET(fact) (fact)()


/* initialize the subsystem */
void		_gst_caps_initialize			(void);

/* creating new caps */
GType		gst_caps_get_type			(void);
GstCaps*	gst_caps_new				(const gchar *name, const gchar *mime, GstProps *props);
GstCaps*	gst_caps_new_id				(const gchar *name, const GQuark id, GstProps *props);
GstCaps*	gst_caps_get_any			(void);
/* replace pointer to caps, doing proper refcounting */
void		gst_caps_replace			(GstCaps **oldcaps, GstCaps *newcaps);
void		gst_caps_replace_sink			(GstCaps **oldcaps, GstCaps *newcaps);

/* caps lifecycle control */
GstCaps*	gst_caps_unref				(GstCaps *caps);
GstCaps*	gst_caps_ref				(GstCaps *caps);
void		gst_caps_sink				(GstCaps *caps);

/* write debug lines to the log */
void		gst_caps_debug				(GstCaps *caps, const gchar *label);

/* copy caps */
GstCaps*	gst_caps_copy				(GstCaps *caps);
GstCaps*	gst_caps_copy_1				(GstCaps *caps);
GstCaps*	gst_caps_copy_on_write			(GstCaps *caps);

const gchar*	gst_caps_get_name			(GstCaps *caps);
void		gst_caps_set_name			(GstCaps *caps, const gchar *name);

const gchar*	gst_caps_get_mime			(GstCaps *caps);
void		gst_caps_set_mime			(GstCaps *caps, const gchar *mime);

GstCaps*	gst_caps_set_props			(GstCaps *caps, GstProps *props);
GstProps*	gst_caps_get_props			(GstCaps *caps);

#ifdef G_HAVE_ISO_VARARGS
#define		gst_caps_set(caps, ...)			gst_props_set ((caps)->properties, __VA_ARGS__)
#define		gst_caps_get(caps, ...)			gst_props_get ((caps)->properties, __VA_ARGS__)
#elif defined(G_HAVE_GNUC_VARARGS)
#define		gst_caps_set(caps, name, args...)	gst_props_set ((caps)->properties, name, ##args)
#define		gst_caps_get(caps, name, args...)	gst_props_get ((caps)->properties, name, ##args)
#endif

#define		gst_caps_get_int(caps,name,res)		gst_props_entry_get_int(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_float(caps,name,res)	gst_props_entry_get_float(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_fourcc_int(caps,name,res)	gst_props_entry_get_fourcc_int(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_boolean(caps,name,res)	gst_props_entry_get_boolean(gst_props_get_entry((caps)->properties,name),res)
#define		gst_caps_get_string(caps,name,res)	gst_props_entry_get_string(gst_props_get_entry((caps)->properties,name),res)

gboolean	gst_caps_has_property			(GstCaps *caps, const gchar *name);
gboolean	gst_caps_has_property_typed		(GstCaps *caps, const gchar *name, GstPropsType type);
gboolean	gst_caps_has_fixed_property		(GstCaps *caps, const gchar *name);

GstCaps*	gst_caps_get_by_name			(GstCaps *caps, const gchar *name);

/* use and construct chained caps */
GstCaps*	gst_caps_next				(GstCaps *caps); 
GstCaps*	gst_caps_chain				(GstCaps *caps, ...); 
GstCaps*	gst_caps_append				(GstCaps *caps, GstCaps *capstoadd); 
GstCaps*	gst_caps_prepend			(GstCaps *caps, GstCaps *capstoadd); 

/* see if fromcaps is a subset of tocaps */
gboolean	gst_caps_is_always_compatible		(GstCaps *fromcaps, GstCaps *tocaps);

/* operations on caps */
GstCaps*	gst_caps_intersect			(GstCaps *caps1, GstCaps *caps2);
GstCaps*	gst_caps_union				(GstCaps *caps1, GstCaps *caps2);
GstCaps*	gst_caps_normalize			(GstCaps *caps);

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr      gst_caps_save_thyself			(GstCaps *caps, xmlNodePtr parent);
GstCaps*	gst_caps_load_thyself			(xmlNodePtr parent);
#endif

/* for debugging purposes */
gchar *		gst_caps_to_string			(GstCaps *caps);
GstCaps *	gst_caps_from_string			(gchar *str);

G_END_DECLS

#endif /* __GST_CAPS_H__ */
