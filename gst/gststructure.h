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

#ifndef __GST_STRUCTURE_H__
#define __GST_STRUCTURE_H__

#include <gst/gstconfig.h>
#include <gst/gsttypes.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GST_TYPE_STRUCTURE             (gst_structure_get_type ())
#define GST_STRUCTURE(object)          (G_TYPE_CHECK_INSTANCE_CAST ((object), GST_TYPE_STRUCTURE, GstStructure))
#define GST_IS_STRUCTURE(object)       (G_TYPE_CHECK_INSTANCE_TYPE ((object), GST_TYPE_STRUCTURE))

typedef struct _GstStructure GstStructure;

typedef gboolean (*GstStructureForeachFunc) (GQuark   field_id,
					     GValue * value,
					     gpointer user_data);

struct _GstStructure {
  GType type;

  GQuark name;

  GArray *fields;

  gpointer _gst_reserved[GST_PADDING];
};

GType                   gst_structure_get_type             (void) G_GNUC_CONST;

GstStructure *          gst_structure_empty_new            (const gchar *            name);
GstStructure *          gst_structure_id_empty_new         (GQuark                   quark);
GstStructure *          gst_structure_new                  (const gchar *            name,
					                    const gchar *            firstfield,
							    ...);
GstStructure *          gst_structure_new_valist           (const gchar *            name,
						            const gchar *            firstfield,
							    va_list                  varargs);
GstStructure *          gst_structure_copy                 (const GstStructure      *structure);
void                    gst_structure_free                 (GstStructure            *structure);

G_CONST_RETURN gchar *  gst_structure_get_name             (const GstStructure      *structure);
GQuark			gst_structure_get_name_id          (const GstStructure      *structure);
void                    gst_structure_set_name             (GstStructure            *structure,
							    const gchar             *name);

void                    gst_structure_id_set_value         (GstStructure            *structure,
							    GQuark                   field,
							    const GValue            *value);
void                    gst_structure_set_value            (GstStructure            *structure,
							    const gchar             *fieldname,
							    const GValue            *value);
void                    gst_structure_set                  (GstStructure            *structure,
							    const gchar             *fieldname,
							    ...);
void                    gst_structure_set_valist           (GstStructure            *structure,
							    const gchar             *fieldname,
							    va_list varargs);
G_CONST_RETURN GValue * gst_structure_id_get_value         (const GstStructure      *structure,
							    GQuark                   field);
G_CONST_RETURN GValue * gst_structure_get_value            (const GstStructure      *structure,
							    const gchar             *fieldname);
void                    gst_structure_remove_field         (GstStructure            *structure,
							    const gchar             *fieldname);
void                    gst_structure_remove_fields        (GstStructure            *structure, 
							     const gchar            *fieldname,
							    ...);
void                    gst_structure_remove_fields_valist (GstStructure             *structure, 
							    const gchar             *fieldname,
							    va_list                  varargs);
void                    gst_structure_remove_all_fields    (GstStructure            *structure);

GType                   gst_structure_get_field_type       (const GstStructure      *structure,
							    const gchar             *fieldname);
gboolean                gst_structure_foreach              (GstStructure            *structure,
							    GstStructureForeachFunc  func,
							    gpointer                 user_data);
gint                    gst_structure_n_fields             (const GstStructure      *structure);
gboolean                gst_structure_has_field            (const GstStructure      *structure,
							    const gchar             *fieldname);
gboolean                gst_structure_has_field_typed      (const GstStructure      *structure,
							    const gchar             *fieldname,
							    GType                    type);

/* utility functions */
gboolean                gst_structure_get_boolean          (const GstStructure      *structure,
							    const gchar             *fieldname,
							    gboolean                *value);
gboolean                gst_structure_get_int              (const GstStructure      *structure,
							    const gchar             *fieldname,
							    gint                    *value);
gboolean                gst_structure_get_fourcc           (const GstStructure      *structure,
							    const gchar             *fieldname,
							    guint32                 *value);
gboolean                gst_structure_get_double           (const GstStructure      *structure,
							    const gchar             *fieldname,
							    gdouble                 *value);
G_CONST_RETURN gchar *  gst_structure_get_string           (const GstStructure      *structure,
							    const gchar             *fieldname);

gchar *                 gst_structure_to_string            (const GstStructure      *structure);
GstStructure *          gst_structure_from_string          (const gchar             *string,
							    gchar                  **end);

G_END_DECLS

#endif

