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

#define GST_TYPE_CAPS              gst_caps_get_type()
#define GST_CAPS(object)          (G_TYPE_CHECK_INSTANCE_CAST ((object), GST_TYPE_CAPS, GstCaps))
#define GST_IS_CAPS(object)       (G_TYPE_CHECK_INSTANCE_TYPE ((object), GST_TYPE_CAPS))

#define GST_CAPS_FLAGS_ANY	  (1 << 0)

#define GST_CAPS_ANY              gst_caps_new_any()
#define GST_CAPS_NONE             gst_caps_new_empty()

#define GST_STATIC_CAPS_ANY       GST_STATIC_CAPS("ANY")
#define GST_STATIC_CAPS_NONE      GST_STATIC_CAPS("NONE")

#define GST_CAPS_IS_SIMPLE(caps) (gst_caps_get_size(caps) == 1)
#define gst_caps_is_simple(caps) GST_CAPS_IS_SIMPLE(caps)

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

struct _GstCaps {
  GType type;

  guint16 flags;
  GPtrArray *structs;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstStaticCaps {
  GstCaps caps;
  const char *string;
  gpointer _gst_reserved[GST_PADDING];
};

GType                    gst_caps_get_type                              (void) G_GNUC_CONST;
GstCaps *                gst_caps_new_empty                             (void);
GstCaps *                gst_caps_new_any                               (void);
GstCaps *                gst_caps_new_simple                            (const char    *media_type,
									 const char    *fieldname,
									 ...);
GstCaps *                gst_caps_new_full                              (GstStructure  *struct1,
									                 ...);
GstCaps *                gst_caps_new_full_valist                       (GstStructure  *structure,
									 va_list        var_args);
GstCaps *                gst_caps_copy                                  (const GstCaps *caps);
void                     gst_caps_free                                  (GstCaps       *caps);
G_CONST_RETURN GstCaps * gst_static_caps_get                            (GstStaticCaps *caps);

/* manipulation */
void                     gst_caps_append                                (GstCaps       *caps1,
									 GstCaps       *caps2);
void                     gst_caps_append_structure                      (GstCaps       *caps1,
									 GstStructure  *structure);
GstCaps *                gst_caps_split_one                             (GstCaps       *caps);
int                      gst_caps_get_size                              (const GstCaps *caps);
GstStructure *           gst_caps_get_structure                         (const GstCaps *caps,
									 int            index);
#ifndef GST_DISABLE_DEPRECATED
GstCaps *                gst_caps_copy_1                                (const GstCaps *caps);
#endif
void                     gst_caps_set_simple                            (GstCaps       *caps,
									 char          *field, ...);
void                     gst_caps_set_simple_valist                     (GstCaps       *caps,
									 char          *field,
									 va_list        varargs);

/* tests */
gboolean                 gst_caps_is_any                                (const GstCaps *caps);
gboolean                 gst_caps_is_empty                              (const GstCaps *caps);
#ifndef GST_DISABLE_DEPRECATED
gboolean                 gst_caps_is_chained                            (const GstCaps *caps);
#endif
gboolean                 gst_caps_is_fixed                              (const GstCaps *caps);
gboolean                 gst_caps_is_equal_fixed                        (const GstCaps *caps1,
									 const GstCaps *caps2);
gboolean                 gst_caps_is_always_compatible                  (const GstCaps *caps1,
						                 	 const GstCaps *caps2);

/* operations */
GstCaps *                gst_caps_intersect                             (const GstCaps *caps1,
									 const GstCaps *caps2);
GstCaps *                gst_caps_union                                 (const GstCaps *caps1,
									 const GstCaps *caps2);
GstCaps *                gst_caps_normalize                             (const GstCaps *caps);
GstCaps *                gst_caps_simplify                              (const GstCaps *caps);

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

gboolean                 gst_caps_structure_fixate_field_nearest_int    (GstStructure *structure,
									 const char   *field_name,
									 int           target);
gboolean                 gst_caps_structure_fixate_field_nearest_double (GstStructure *structure,
									 const char   *field_name,
									 double        target);

G_END_DECLS

#endif /* __GST_CAPS_H__ */
