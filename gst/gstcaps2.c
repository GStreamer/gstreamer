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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include <gst/gst.h>

static void _gst_caps2_transform_to_string (const GValue *src_value,
    GValue *dest_value);
static void _gst_caps2_value_init (GValue *value);
static void _gst_caps2_value_free (GValue *value);
static void _gst_caps2_value_copy (const GValue *src, GValue *dest);
static gpointer _gst_caps2_value_peek_pointer (const GValue *value);
static void _gst_caps2_from_string_inplace (GstCaps2 *caps,
    const gchar *string);


GType _gst_caps2_type;

void _gst_caps2_initialize (void)
{
  static const GTypeValueTable type_value_table = {
    _gst_caps2_value_init,
    _gst_caps2_value_free,
    _gst_caps2_value_copy,
    _gst_caps2_value_peek_pointer,
    NULL,
    NULL,
    NULL,
    NULL,
  };
  static const GTypeInfo caps2_info = {
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    0,
    NULL,
    &type_value_table,
  };

  _gst_caps2_type = g_type_register_static (G_TYPE_BOXED, "GstCaps2",
      &caps2_info, 0);

  g_value_register_transform_func (_gst_caps2_type, G_TYPE_STRING,
      _gst_caps2_transform_to_string);
}

GType gst_caps2_get_type (void)
{
  return _gst_caps2_type;
}

/* creation/deletion */
GstCaps2 *gst_caps2_new_empty (void)
{
  GstCaps2 *caps = g_new0(GstCaps2, 1);

  caps->type = _gst_caps2_type;
  caps->structs = g_ptr_array_new();

  return caps;
}

GstCaps2 *gst_caps2_new_any (void)
{
  GstCaps2 *caps = g_new0(GstCaps2, 1);

  caps->type = _gst_caps2_type;
  caps->structs = g_ptr_array_new();
  caps->flags = GST_CAPS2_FLAGS_ANY;

  return caps;
}

GstCaps2 *gst_caps2_new_simple (const char *media_type, const char *fieldname,
    ...)
{
  GstCaps2 *caps;
  GstStructure *structure;
  va_list var_args;

  caps = g_new0(GstCaps2, 1);
  caps->type = _gst_caps2_type;
  caps->structs = g_ptr_array_new();

  va_start (var_args, fieldname);
  structure = gst_structure_new_valist (media_type, fieldname, var_args);
  va_end (var_args);

  g_ptr_array_add (caps->structs, structure);
  
  return caps;
}

GstCaps2 *gst_caps2_new_full (const GstStructure *struct1, ...)
{
  GstCaps2 *caps;
  va_list var_args;

  va_start (var_args, struct1);
  caps = gst_caps2_new_full_valist (struct1, var_args);
  va_end (var_args);

  return caps;
}

GstCaps2 *gst_caps2_new_full_valist (const GstStructure *structure,
    va_list var_args)
{
  GstCaps2 *caps;

  caps = g_new0(GstCaps2, 1);
  caps->type = _gst_caps2_type;
  caps->structs = g_ptr_array_new();

  while(structure){
    g_ptr_array_add (caps->structs, (gpointer) structure);
    structure = va_arg (var_args, void *);
  }

  return caps;
}

GstCaps2 *gst_caps2_copy (const GstCaps2 *caps)
{
  GstCaps2 *newcaps;
  GstStructure *structure;
  int i;

  newcaps = g_new0(GstCaps2, 1);
  newcaps->type = _gst_caps2_type;
  newcaps->flags = caps->flags;
  newcaps->structs = g_ptr_array_new();

  for(i=0;i<caps->structs->len;i++){
    structure = gst_caps2_get_nth_cap (caps, i);
    g_ptr_array_add (newcaps->structs, gst_structure_copy(structure));
  }

  return newcaps;
}

void gst_caps2_free (GstCaps2 *caps)
{
  GstStructure *structure;
  int i;
  
  for(i=0;i<caps->structs->len;i++){
    structure = gst_caps2_get_nth_cap (caps, i);
    gst_structure_free (structure);
  }
  g_ptr_array_free(caps->structs, TRUE);
  g_free(caps);
}

const GstCaps2 *gst_caps2_from_static (GstStaticCaps2 *static_caps)
{
  GstCaps2 *caps = (GstCaps2 *)static_caps;

  if (caps->type == 0) {
    caps->type = _gst_caps2_type;
    _gst_caps2_from_string_inplace (caps, static_caps->string);
  }

  return caps;
}

/* manipulation */
void gst_caps2_append (GstCaps2 *caps1, GstCaps2 *caps2)
{
  GstStructure *structure;
  int i;
  
  for(i=0;i<caps2->structs->len;i++){
    structure = gst_caps2_get_nth_cap (caps2, i);
    g_ptr_array_add (caps1->structs, structure);
  }
  g_ptr_array_free(caps2->structs, TRUE);
  g_free(caps2);
}

void gst_caps2_append_cap (GstCaps2 *caps1, GstStructure *structure)
{
  if (structure){
    g_ptr_array_add (caps1->structs, structure);
  }
}

GstCaps2 *gst_caps2_split_one (GstCaps2 *caps)
{
  /* FIXME */

  return NULL;
}

GstStructure *gst_caps2_get_nth_cap (const GstCaps2 *caps, int index)
{
  return g_ptr_array_index(caps->structs, index);
}

GstCaps2 *gst_caps2_copy_1 (const GstCaps2 *caps)
{
  GstCaps2 *newcaps;
  GstStructure *structure;

  newcaps = g_new0(GstCaps2, 1);
  newcaps->type = _gst_caps2_type;
  newcaps->flags = caps->flags;
  newcaps->structs = g_ptr_array_new();

  if (caps->structs->len > 0){
    structure = gst_caps2_get_nth_cap (caps, 0);
    g_ptr_array_add (newcaps->structs, gst_structure_copy(structure));
  }

  return newcaps;
}

/* tests */
gboolean gst_caps2_is_any (const GstCaps2 *caps)
{
  return (caps->flags & GST_CAPS2_FLAGS_ANY);
}

gboolean gst_caps2_is_empty (const GstCaps2 *caps)
{
  if (caps->flags & GST_CAPS2_FLAGS_ANY) return FALSE;

  return (caps->structs == NULL) || (caps->structs->len == 0);
}

gboolean gst_caps2_is_chained (const GstCaps2 *caps)
{
  return (caps->structs->len > 1);
}

gboolean gst_caps2_is_fixed (const GstCaps2 *caps)
{
  GstStructure *structure;
  GstStructureField *field;
  GType type;
  int i;

  if (caps->structs->len > 1) return FALSE;

  structure = gst_caps2_get_nth_cap (caps, 0);

  for(i=0;i<structure->fields->len;i++) {
    field = GST_STRUCTURE_FIELD(structure, i);
    type = G_VALUE_TYPE(&field->value);

    if(type == GST_TYPE_INT_RANGE || type == GST_TYPE_DOUBLE_RANGE) {
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean _gst_cap_is_always_compatible (const GstStructure *struct1,
    const GstStructure *struct2)
{
  int i;
  const GstStructureField *field1;
  const GstStructureField *field2;

  if(struct1->name != struct2->name){
    return FALSE;
  }
  for(i=0;i<struct2->fields->len;i++){
    GValue dest = { 0 };

    /* the reversed order is important */
    field2 = GST_STRUCTURE_FIELD (struct2, i);
    field1 = gst_structure_id_get_field (struct1, field2->name);

    if (field1 == NULL) return FALSE;

    if (gst_value_compare (&field1->value, &field2->value) ==
	GST_VALUE_EQUAL) {
      break;
    }
    if (gst_value_intersect (&dest, &field1->value, &field2->value)){
      g_value_unset (&dest);
      break;
    }

    return FALSE;
  }

  return TRUE;
}

static gboolean _gst_caps_cap_is_always_compatible (const GstStructure
    *struct1, const GstCaps2 *caps2)
{
  int i;

  for(i=0;i<caps2->structs->len;i++){
    GstStructure *struct2 = gst_caps2_get_nth_cap (caps2, i);

    if (_gst_cap_is_always_compatible (struct1, struct2)) {
      return TRUE;
    }
  }

  return FALSE;
}

gboolean gst_caps2_is_always_compatible (const GstCaps2 *caps1,
    const GstCaps2 *caps2)
{
  int i;

  for(i=0;i<caps1->structs->len;i++) {
    GstStructure *struct1 = gst_caps2_get_nth_cap (caps1, i);

    if (_gst_caps_cap_is_always_compatible(struct1, caps2) == FALSE){
      return FALSE;
    }

  }

  return FALSE;
}

static GstStructure *gst_caps2_structure_intersect (const GstStructure *struct1,
    const GstStructure *struct2)
{
  int i;
  GstStructure *dest;
  const GstStructureField *field1;
  const GstStructureField *field2;
  int ret;

  if (struct1->name != struct2->name) return NULL;

  dest = gst_structure_id_empty_new (struct1->name);

  for(i=0;i<struct1->fields->len;i++){
    GValue dest_field = { 0 };

    field1 = GST_STRUCTURE_FIELD (struct1, i);
    field2 = gst_structure_id_get_field (struct2, field1->name);

    if (field2 == NULL) {
      gst_structure_set_field_copy (dest, field1);
    } else {
      if (gst_value_intersect (&dest_field, &field1->value, &field2->value)) {
	gst_structure_set_value (dest, g_quark_to_string(field1->name),
	    &dest_field);
      } else {
        ret = gst_value_compare(&field1->value, &field2->value);
	if (ret == GST_VALUE_EQUAL){
	  gst_structure_set_value (dest, g_quark_to_string(field1->name),
	      &field1->value);
	} else {
	  gst_structure_free (dest);
	  return NULL;
	}
      }
    }
  }

  for(i=0;i<struct1->fields->len;i++){
    field2 = GST_STRUCTURE_FIELD (struct2, i);
    field1 = gst_structure_id_get_field (struct1, field2->name);

    if (field1 == NULL) {
      gst_structure_set_field_copy (dest, field2);
    }
  }

  return dest;
}

#if 0
static GstStructure *gst_caps2_structure_union (const GstStructure *struct1,
    const GstStructure *struct2)
{
  int i;
  GstStructure *dest;
  const GstStructureField *field1;
  const GstStructureField *field2;
  int ret;

  /* FIXME this doesn't actually work */

  if (struct1->name != struct2->name) return NULL;

  dest = gst_structure_id_empty_new (struct1->name);

  for(i=0;i<struct1->fields->len;i++){
    GValue dest_value = { 0 };

    field1 = GST_STRUCTURE_FIELD (struct1, i);
    field2 = gst_structure_id_get_field (struct2, field1->name);

    if (field2 == NULL) {
      continue;
    } else {
      if (gst_value_union (&dest_value, &field1->value, &field2->value)) {
	gst_structure_set_value (dest, g_quark_to_string(field1->name),
	    &dest_value);
      } else {
	ret = gst_value_compare (&field1->value, &field2->value);
      }
    }
  }

  return dest;
}
#endif

/* operations */
GstCaps2 *gst_caps2_intersect (const GstCaps2 *caps1, const GstCaps2 *caps2)
{
  int i,j;
  GstStructure *struct1;
  GstStructure *struct2;
  GstCaps2 *dest;

  dest = gst_caps2_new_empty();
  for(i=0;i<caps1->structs->len;i++){
    struct1 = gst_caps2_get_nth_cap (caps1, i);
    for(j=0;j<caps1->structs->len;j++){
      struct2 = gst_caps2_get_nth_cap (caps2, j);

      gst_caps2_append_cap(dest, gst_caps2_structure_intersect (
	    struct1, struct2));
    }
  }

  /* FIXME: need a simplify function */

  return dest;
}

GstCaps2 *gst_caps2_union (const GstCaps2 *caps1, const GstCaps2 *caps2)
{
  GstCaps2 *dest1;
  GstCaps2 *dest2;

  dest1 = gst_caps2_copy (caps1);
  dest2 = gst_caps2_copy (caps2);
  gst_caps2_append (dest1, dest2);


  /* FIXME: need a simplify function */

  return dest1;
}

GstCaps2 *gst_caps2_normalize (const GstCaps2 *caps)
{

  return NULL;
}

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr gst_caps2_save_thyself (const GstCaps2 *caps, xmlNodePtr parent)
{

  return 0;
}

GstCaps2 *gst_caps2_load_thyself (xmlNodePtr parent)
{

  return NULL;
}
#endif

/* utility */
void gst_caps2_replace (GstCaps2 **caps, const GstCaps2 *newcaps)
{

}

gchar *gst_caps2_to_string (const GstCaps2 *caps)
{
  int i;
  GstStructure *structure;
  GString *s;

  /* FIXME does this leak? */

  if(gst_caps2_is_any(caps)){
    return g_strdup("ANY");
  }
  if(gst_caps2_is_empty(caps)){
    return g_strdup("EMPTY");
  }
  s = g_string_new("");
  structure = gst_caps2_get_nth_cap (caps, 0);
  g_string_append(s, gst_structure_to_string(structure));

  for(i=1;i<caps->structs->len;i++){
    structure = gst_caps2_get_nth_cap (caps, i);

    g_string_append(s, "; ");
    g_string_append(s, gst_structure_to_string(structure));
  }

  return g_string_free(s, FALSE);
}

static void _gst_caps2_from_string_inplace (GstCaps2 *caps,
    const gchar *string)
{
  GstStructure *structure;

  if (strcmp("ANY", string)==0) {
    caps->flags = GST_CAPS2_FLAGS_ANY;
    return;
  }
  if (strcmp("NONE", string)==0) {
    return;
  }

  structure = gst_structure_from_string(string);
  gst_caps2_append_cap (caps, structure);
}

GstCaps2 *gst_caps2_from_string (const gchar *string)
{
  GstCaps2 *caps;

  caps = gst_caps2_new_empty();
  _gst_caps2_from_string_inplace (caps, string);

  return caps;
}

static void _gst_caps2_transform_to_string (const GValue *src_value,
    GValue *dest_value)
{
  g_return_if_fail (src_value != NULL);
  g_return_if_fail (dest_value != NULL);

  dest_value->data[0].v_pointer =
    gst_caps2_to_string (src_value->data[0].v_pointer);
}

static void _gst_caps2_value_init (GValue *value)
{
  value->data[0].v_pointer = gst_caps2_new_empty();
}

static void _gst_caps2_value_free (GValue *value)
{
  gst_caps2_free (value->data[0].v_pointer);
}

static void _gst_caps2_value_copy (const GValue *src, GValue *dest)
{
  dest->data[0].v_pointer = gst_caps2_copy (src->data[0].v_pointer);
}

static gpointer _gst_caps2_value_peek_pointer (const GValue *value)
{
  return value->data[0].v_pointer;
}

