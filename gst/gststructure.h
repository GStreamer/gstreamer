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

#ifndef __GST_STRUCTURE_H__
#define __GST_STRUCTURE_H__

#include <gst/gstconfig.h>
#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _GstStructure GstStructure;
typedef struct _GstStructureField GstStructureField;

struct _GstStructure {
  int len;

  GQuark name;

  GArray *fields;
};

struct _GstStructureField {
  GQuark name;
  GValue value;
};

#define GST_STRUCTURE_FIELD(structure, index) \
    &g_array_index((structure)->fields, GstStructureField, (index))

GType gst_structure_get_type(void);
void _gst_structure_initialize(void);

GstStructure *gst_structure_empty_new(const gchar *name);
GstStructure *gst_structure_new(const gchar *name,
    const gchar *firstfield, ...);
GstStructure *gst_structure_new_valist(const gchar *name,
    const gchar *firstfield, va_list varargs);
GstStructure *gst_structure_copy(GstStructure *structure);
void gst_structure_free(GstStructure *structure);

const gchar *gst_structure_get_name(GstStructure *structure);
void gst_structure_set_name(GstStructure *structure, const gchar *name);
void gst_structure_set_field(GstStructure *structure,
    GstStructureField *field);

void gst_structure_id_set_value(GstStructure *structure, GQuark field,
    GValue *value);
void gst_structure_set_value(GstStructure *structure, const gchar *field,
    GValue *value);
void gst_structure_set(GstStructure *structure, const gchar *field, ...);
void gst_structure_set_valist(GstStructure *structure, const gchar *field,
    va_list varargs);
const GValue *gst_structure_get_value(GstStructure *structure, const gchar *field);
GstStructureField *gst_structure_get_field(GstStructure *structure,
        const gchar *fieldname);
GstStructureField *gst_structure_id_get_field(GstStructure *structure,
        GQuark fieldname);
void gst_structure_remove_field(GstStructure *structure, const gchar *field);

GType gst_structure_get_field_type(GstStructure *structure,
    const gchar *field);
gint gst_structure_n_fields(GstStructure *structure);
gboolean gst_structure_has_field(GstStructure *structure, const gchar *field);
gboolean gst_structure_has_field_typed(GstStructure *structure,
    const gchar *field, GType type);

/* utility functions */

gboolean gst_structure_get_boolean(GstStructure *structure, const gchar *field,
    gboolean *value);
gboolean gst_structure_get_int(GstStructure *structure, const gchar *field,
    gint *value);
gboolean gst_structure_get_fourcc(GstStructure *structure, const gchar *field,
    guint32 *value);
gboolean gst_structure_get_double(GstStructure *structure, const gchar *field,
    gdouble *value);
const gchar *gst_structure_get_string(GstStructure *structure,
    const gchar *field);

gchar * gst_structure_to_string(GstStructure *structure);
GstStructure * gst_structure_from_string (const gchar *string);


G_END_DECLS

#endif

