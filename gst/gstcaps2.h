/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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

#ifndef __GST_CAPS2_H__
#define __GST_CAPS2_H__

#include <gst/gstconfig.h>
#include <gst/gststructure.h>

G_BEGIN_DECLS

#define GST_CAPS2_FLAGS_ANY	(1<<0)

extern GType _gst_caps2_type;

typedef struct _GstCaps2 GstCaps2;
typedef struct _GstStaticCaps2 GstStaticCaps2;

struct _GstCaps2 {
  GType type;

  guint16 flags;
  GPtrArray *structs;
};

struct _GstStaticCaps2 {
  GstCaps2 caps;
  const char *string;
};

#define GST_CAPS2_ANY gst_caps2_new_any()
#define GST_CAPS2_NONE gst_caps2_new_empty()

#define GST_TYPE_CAPS2 gst_caps2_get_type()

void _gst_caps2_initialize (void);
GType gst_caps2_get_type (void);

/* creation/deletion */
GstCaps2 *gst_caps2_new_empty (void);
GstCaps2 *gst_caps2_new_any (void);
GstCaps2 *gst_caps2_new_simple (const char *media_type, const char *fieldname, ...);
GstCaps2 *gst_caps2_new_full (const GstStructure *struct1, ...);
GstCaps2 *gst_caps2_new_full_valist (const GstStructure *structure, va_list var_args);
GstCaps2 *gst_caps2_copy (const GstCaps2 *caps);
void gst_caps2_free (GstCaps2 *caps);
const GstCaps2 *gst_caps2_from_static (GstStaticCaps2 *caps);

/* manipulation */
void gst_caps2_append (GstCaps2 *caps1, GstCaps2 *caps2);
void gst_caps2_append_cap (GstCaps2 *caps1, GstStructure *structure);
GstCaps2 *gst_caps2_split_one (GstCaps2 *caps);
GstStructure *gst_caps2_get_nth_cap (const GstCaps2 *caps, int index);
GstCaps2 *gst_caps2_copy_1 (const GstCaps2 *caps);

/* tests */
gboolean gst_caps2_is_any (const GstCaps2 *caps);
gboolean gst_caps2_is_empty (const GstCaps2 *caps);
gboolean gst_caps2_is_chained (const GstCaps2 *caps);
gboolean gst_caps2_is_fixed (const GstCaps2 *caps);
gboolean gst_caps2_is_always_compatible (const GstCaps2 *caps1,
    const GstCaps2 *caps2);

/* operations */
GstCaps2 *gst_caps2_intersect (const GstCaps2 *caps1, const GstCaps2 *caps2);
GstCaps2 *gst_caps2_union (const GstCaps2 *caps1, const GstCaps2 *caps2);
GstCaps2 *gst_caps2_normalize (const GstCaps2 *caps);

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr gst_caps2_save_thyself (const GstCaps2 *caps, xmlNodePtr parent);
GstCaps2 *gst_caps2_load_thyself (xmlNodePtr parent);
#endif

/* utility */
void gst_caps2_replace (GstCaps2 **caps, const GstCaps2 *newcaps);
gchar *gst_caps2_to_string (const GstCaps2 *caps);
GstCaps2 *gst_caps2_from_string (const gchar *string);


G_END_DECLS

#endif


