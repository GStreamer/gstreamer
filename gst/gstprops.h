/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstprops.h: Header for properties subsystem
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


#ifndef __GST_PROPS_H__
#define __GST_PROPS_H__

#include <gst/gstconfig.h>

#include <glib-object.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstProps GstProps;
extern GType _gst_props_type;

#define GST_TYPE_PROPS	(_get_props_type)

typedef enum {
   GST_PROPS_END_TYPE = 0,

   GST_PROPS_INVALID_TYPE,

   GST_PROPS_INT_TYPE,
   GST_PROPS_FLOAT_TYPE,
   GST_PROPS_FOURCC_TYPE,
   GST_PROPS_BOOL_TYPE,
   GST_PROPS_STRING_TYPE,

   GST_PROPS_VAR_TYPE,   /* after this marker start the variable properties */

   GST_PROPS_LIST_TYPE,
   GST_PROPS_FLOAT_RANGE_TYPE,
   GST_PROPS_INT_RANGE_TYPE,

   GST_PROPS_LAST_TYPE = GST_PROPS_END_TYPE + 16,
} GstPropsType;

#define GST_MAKE_FOURCC(a,b,c,d) 	(guint32)((a)|(b)<<8|(c)<<16|(d)<<24)
#define GST_STR_FOURCC(f)		(guint32)(((f)[0])|((f)[1]<<8)|((f)[2]<<16)|((f)[3]<<24))

#define GST_PROPS_LIST(a...) 		GST_PROPS_LIST_TYPE,##a,NULL
#define GST_PROPS_INT(a) 		GST_PROPS_INT_TYPE,(a)
#define GST_PROPS_INT_RANGE(a,b) 	GST_PROPS_INT_RANGE_TYPE,(a),(b)
#define GST_PROPS_FLOAT(a) 		GST_PROPS_FLOAT_TYPE,(a)
#define GST_PROPS_FLOAT_RANGE(a,b) 	GST_PROPS_FLOAT_RANGE_TYPE,(a),(b)
#define GST_PROPS_FOURCC(a) 		GST_PROPS_FOURCC_TYPE,(a)
#define GST_PROPS_BOOLEAN(a) 		GST_PROPS_BOOL_TYPE,(a)
#define GST_PROPS_STRING(a) 		GST_PROPS_STRING_TYPE,(a)

#define GST_PROPS_INT_POSITIVE		GST_PROPS_INT_RANGE(0,G_MAXINT)
#define GST_PROPS_INT_NEGATIVE		GST_PROPS_INT_RANGE(G_MININT,0)
#define GST_PROPS_INT_ANY		GST_PROPS_INT_RANGE(G_MININT,G_MAXINT)

typedef struct _GstPropsEntry GstPropsEntry;

struct _GstProps {
  gint refcount;
  gboolean fixed;

  GList *properties;		/* real properties for this property */
};

/* initialize the subsystem */
void 			_gst_props_initialize		(void);

GstProps*		gst_props_new			(const gchar *firstname, ...);
GstProps*		gst_props_newv			(const gchar *firstname, va_list var_args);
GstProps*		gst_props_empty_new		(void);

void            	gst_props_unref                 (GstProps *props);
void            	gst_props_ref                   (GstProps *props);
void            	gst_props_destroy               (GstProps *props);

void            	gst_props_debug 		(GstProps *props);

GstProps*       	gst_props_copy                  (GstProps *props);
GstProps*       	gst_props_copy_on_write         (GstProps *props);

GstProps*		gst_props_merge			(GstProps *props, GstProps *tomerge);

gboolean 		gst_props_check_compatibility 	(GstProps *fromprops, GstProps *toprops);
GstProps* 		gst_props_intersect	 	(GstProps *props1, GstProps *props2);
GList* 			gst_props_normalize	 	(GstProps *props);

GstProps*		gst_props_set			(GstProps *props, const gchar *name, ...);
gboolean		gst_props_get			(GstProps *props, gchar *first_name, ...);
gboolean		gst_props_get_safe		(GstProps *props, gchar *first_name, ...);

gboolean 		gst_props_has_property		(GstProps *props, const gchar *name);
gboolean 		gst_props_has_property_typed 	(GstProps *props, const gchar *name, GstPropsType type);
gboolean 		gst_props_has_fixed_property	(GstProps *props, const gchar *name);

const GstPropsEntry* 	gst_props_get_entry		(GstProps *props, const gchar *name);
void			gst_props_add_entry		(GstProps *props, GstPropsEntry *entry);

/* working with props entries */
GstPropsEntry*		gst_props_entry_new		(const gchar *name, ...);

GstPropsType		gst_props_entry_get_type	(const GstPropsEntry *entry);
const gchar*		gst_props_entry_get_name	(const GstPropsEntry *entry);
gboolean		gst_props_entry_is_fixed	(const GstPropsEntry *entry);

gboolean		gst_props_entry_get		(const GstPropsEntry *entry, ...);

gboolean		gst_props_entry_get_int		(const GstPropsEntry *entry, gint *val);
gboolean		gst_props_entry_get_float	(const GstPropsEntry *entry, gfloat *val);
gboolean		gst_props_entry_get_fourcc_int	(const GstPropsEntry *entry, guint32 *val);
gboolean		gst_props_entry_get_boolean	(const GstPropsEntry *entry, gboolean *val);
gboolean		gst_props_entry_get_string	(const GstPropsEntry *entry, const gchar **val);
gboolean		gst_props_entry_get_int_range	(const GstPropsEntry *entry, gint *min, gint *max);
gboolean		gst_props_entry_get_float_range	(const GstPropsEntry *entry, gfloat *min, gfloat *max);
gboolean		gst_props_entry_get_list	(const GstPropsEntry *entry, const GList **val);


#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr 		gst_props_save_thyself 		(GstProps *props, xmlNodePtr parent);
GstProps* 		gst_props_load_thyself 		(xmlNodePtr parent);
#endif


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_PROPS_H__ */
