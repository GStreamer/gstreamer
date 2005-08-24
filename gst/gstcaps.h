/* GStreamer
 * Copyright (C) 2003 David A. Schleef <ds@schleef.org>
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
#include <gst/gststructure.h>

G_BEGIN_DECLS

#define GST_TYPE_CAPS             (gst_caps_get_type())
#define GST_CAPS(object)          ((GstCaps*)object)
#define GST_IS_CAPS(object)       ((object) && (GST_CAPS(object)->type == GST_TYPE_CAPS))

#define GST_CAPS_FLAGS_ANY	  (1 << 0)

#define GST_CAPS_ANY              gst_caps_new_any()
#define GST_CAPS_NONE             gst_caps_new_empty()

#define GST_STATIC_CAPS_ANY       GST_STATIC_CAPS("ANY")
#define GST_STATIC_CAPS_NONE      GST_STATIC_CAPS("NONE")

#define GST_CAPS_IS_SIMPLE(caps) (gst_caps_get_size(caps) == 1)

#ifndef GST_DISABLE_DEPRECATED
#define GST_DEBUG_CAPS(string, caps) \
  GST_DEBUG ( string "%s: " GST_PTR_FORMAT, caps)
#endif

#define GST_STATIC_CAPS(string) \
{ \
  /* caps */ { 0 }, \
  /* string */ string, \
}

typedef struct _GstCaps GstCaps;
typedef struct _GstStaticCaps GstStaticCaps;

/* refcount */
#define GST_CAPS_REFCOUNT(caps)                 ((GST_CAPS(caps))->refcount)
#define GST_CAPS_REFCOUNT_VALUE(caps)           (g_atomic_int_get (&(GST_CAPS(caps))->refcount))

struct _GstCaps {
  GType type;

  /*< public >*/ /* with COW */
  /* refcounting */
  gint           refcount;

  guint16 flags;
  GPtrArray *structs;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstStaticCaps {
  /*< public >*/
  GstCaps caps;
  const char *string;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType                    gst_caps_get_type                              (void);
GstCaps *                gst_caps_new_empty                             (void);
GstCaps *                gst_caps_new_any                               (void);
GstCaps *                gst_caps_new_simple                            (const char    *media_type,
									 const char    *fieldname,
									 ...);
GstCaps *                gst_caps_new_full                              (GstStructure  *struct1,
									                 ...);
GstCaps *                gst_caps_new_full_valist                       (GstStructure  *structure,
									 va_list        var_args);

/* reference counting */
GstCaps *                gst_caps_ref					(GstCaps* caps);
GstCaps *                gst_caps_copy					(const GstCaps * caps);
GstCaps *                gst_caps_make_writable				(GstCaps *caps);
void                     gst_caps_unref					(GstCaps* caps);

GstCaps *		 gst_static_caps_get                            (GstStaticCaps *static_caps);

/* manipulation */
void                     gst_caps_append                                (GstCaps       *caps1,
									 GstCaps       *caps2);
void                     gst_caps_append_structure                      (GstCaps       *caps,
									 GstStructure  *structure);
int                      gst_caps_get_size                              (const GstCaps *caps);
GstStructure *           gst_caps_get_structure                         (const GstCaps *caps,
									 int            index);
GstCaps *		 gst_caps_copy_nth                              (const GstCaps * caps, gint nth);
void			 gst_caps_truncate                              (GstCaps * caps);
void                     gst_caps_set_simple                            (GstCaps       *caps,
									 char          *field, ...);
void                     gst_caps_set_simple_valist                     (GstCaps       *caps,
									 char          *field,
									 va_list        varargs);

/* tests */
gboolean                 gst_caps_is_any                                (const GstCaps *caps);
gboolean                 gst_caps_is_empty                              (const GstCaps *caps);
gboolean                 gst_caps_is_fixed                              (const GstCaps *caps);
gboolean                 gst_caps_is_always_compatible                  (const GstCaps *caps1,
									const GstCaps *caps2);
gboolean		 gst_caps_is_subset				(const GstCaps *subset,
									 const GstCaps *superset);
gboolean		 gst_caps_is_equal				(const GstCaps *caps1,
									 const GstCaps *caps2);
gboolean		gst_caps_is_equal_fixed				(const GstCaps * caps1,
									 const GstCaps * caps2);


/* operations */
GstCaps *                gst_caps_intersect                             (const GstCaps *caps1,
									 const GstCaps *caps2);
GstCaps *                gst_caps_subtract				(const GstCaps *minuend,
									 const GstCaps *subtrahend);
GstCaps *                gst_caps_union                                 (const GstCaps *caps1,
									 const GstCaps *caps2);
GstCaps *                gst_caps_normalize                             (const GstCaps *caps);
gboolean                 gst_caps_do_simplify                           (GstCaps *caps);

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr               gst_caps_save_thyself                          (const GstCaps *caps,
									 xmlNodePtr     parent);
GstCaps *                gst_caps_load_thyself                          (xmlNodePtr     parent);
#endif

/* utility */
void                     gst_caps_replace                               (GstCaps      **caps,
									 GstCaps       *newcaps);
gchar *                  gst_caps_to_string                             (const GstCaps *caps);
GstCaps *                gst_caps_from_string                           (const gchar   *string);

G_END_DECLS

#endif /* __GST_CAPS_H__ */
