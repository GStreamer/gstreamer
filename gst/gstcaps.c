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

#define CAPS_POISON(caps) do{ \
  GstCaps *_newcaps = gst_caps_copy (caps); \
  gst_caps_free(caps); \
  caps = _newcaps; \
} while (0)
#define STRUCTURE_POISON(structure) do{ \
  GstStructure *_newstruct = gst_structure_copy (structure); \
  gst_structure_free(structure); \
  structure = _newstruct; \
} while (0)


static void _gst_caps_transform_to_string (const GValue *src_value,
    GValue *dest_value);
static void _gst_caps_value_init (GValue *value);
static void _gst_caps_value_free (GValue *value);
static void _gst_caps_value_copy (const GValue *src, GValue *dest);
static gpointer _gst_caps_value_peek_pointer (const GValue *value);
static gboolean _gst_caps_from_string_inplace (GstCaps *caps,
    const gchar *string);


GType _gst_caps_type;

void _gst_caps_initialize (void)
{
  static const GTypeValueTable type_value_table = {
    _gst_caps_value_init,
    _gst_caps_value_free,
    _gst_caps_value_copy,
    _gst_caps_value_peek_pointer,
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

  _gst_caps_type = g_type_register_static (G_TYPE_BOXED, "GstCaps",
      &caps2_info, 0);

  g_value_register_transform_func (_gst_caps_type, G_TYPE_STRING,
      _gst_caps_transform_to_string);
}

GType gst_caps_get_type (void)
{
  return _gst_caps_type;
}

/* creation/deletion */
GstCaps *gst_caps_new_empty (void)
{
  GstCaps *caps = g_new0(GstCaps, 1);

  caps->type = _gst_caps_type;
  caps->structs = g_ptr_array_new();

  return caps;
}

GstCaps *gst_caps_new_any (void)
{
  GstCaps *caps = g_new0(GstCaps, 1);

  caps->type = _gst_caps_type;
  caps->structs = g_ptr_array_new();
  caps->flags = GST_CAPS_FLAGS_ANY;

  return caps;
}

GstCaps *gst_caps_new_simple (const char *media_type, const char *fieldname,
    ...)
{
  GstCaps *caps;
  GstStructure *structure;
  va_list var_args;

  caps = g_new0(GstCaps, 1);
  caps->type = _gst_caps_type;
  caps->structs = g_ptr_array_new();

  va_start (var_args, fieldname);
  structure = gst_structure_new_valist (media_type, fieldname, var_args);
  va_end (var_args);

  gst_caps_append_structure (caps, structure);
  
  return caps;
}

GstCaps *gst_caps_new_full (GstStructure *struct1, ...)
{
  GstCaps *caps;
  va_list var_args;

  va_start (var_args, struct1);
  caps = gst_caps_new_full_valist (struct1, var_args);
  va_end (var_args);

  return caps;
}

GstCaps *gst_caps_new_full_valist (GstStructure *structure,
    va_list var_args)
{
  GstCaps *caps;

  caps = g_new0(GstCaps, 1);
  caps->type = _gst_caps_type;
  caps->structs = g_ptr_array_new();

  while(structure){
    gst_caps_append_structure (caps, structure);
    structure = va_arg (var_args, GstStructure *);
  }

  return caps;
}

GstCaps *gst_caps_copy (const GstCaps *caps)
{
  GstCaps *newcaps;
  GstStructure *structure;
  int i;

  g_return_val_if_fail(caps != NULL, NULL);

  newcaps = g_new0(GstCaps, 1);
  newcaps->type = _gst_caps_type;
  newcaps->flags = caps->flags;
  newcaps->structs = g_ptr_array_new();

  for(i=0;i<caps->structs->len;i++){
    structure = gst_caps_get_structure (caps, i);
    gst_caps_append_structure (newcaps, gst_structure_copy(structure));
  }

  return newcaps;
}

void gst_caps_free (GstCaps *caps)
{
  GstStructure *structure;
  int i;
  
  g_return_if_fail(caps != NULL);

  for(i=0;i<caps->structs->len;i++){
    structure = gst_caps_get_structure (caps, i);
    gst_structure_free (structure);
  }
  g_ptr_array_free(caps->structs, TRUE);
#ifdef USE_POISONING
  memset (caps, 0xff, sizeof(GstCaps));
#endif
  g_free(caps);
}

const GstCaps *gst_static_caps_get (GstStaticCaps *static_caps)
{
  GstCaps *caps = (GstCaps *)static_caps;
  gboolean ret;

  if (caps->type == 0) {
    caps->type = _gst_caps_type;
    caps->structs = g_ptr_array_new();
    ret = _gst_caps_from_string_inplace (caps, static_caps->string);

    if (!ret) {
      g_critical ("Could not convert static caps \"%s\"", static_caps->string);
    }
  }

  return caps;
}

/* manipulation */
void gst_caps_append (GstCaps *caps1, GstCaps *caps2)
{
  GstStructure *structure;
  int i;

  g_return_if_fail (caps1 != NULL);
  g_return_if_fail (caps2 != NULL);

#ifdef USE_POISONING
  CAPS_POISON (caps2);
#endif
  for(i=0;i<caps2->structs->len;i++){
    structure = gst_caps_get_structure (caps2, i);
    gst_caps_append_structure (caps1, structure);
  }
  g_ptr_array_free(caps2->structs, TRUE);
#ifdef USE_POISONING
  memset (caps2, 0xff, sizeof(GstCaps));
#endif
  g_free(caps2);
}

void gst_caps_append_structure (GstCaps *caps, GstStructure *structure)
{
  g_return_if_fail(caps != NULL);

  if (structure){
#if 0 /* disable this, since too many plugins rely on undefined behavior */
#ifdef USE_POISONING
    STRUCTURE_POISON (structure);
#endif
#endif
    g_ptr_array_add (caps->structs, structure);
  }
}

GstCaps *gst_caps_split_one (GstCaps *caps)
{
  /* FIXME */
  g_critical ("unimplemented");

  return NULL;
}

int gst_caps_get_size (const GstCaps *caps)
{
  g_return_val_if_fail (caps != NULL, 0);

  return caps->structs->len;
}

GstStructure *gst_caps_get_structure (const GstCaps *caps, int index)
{
  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (index >= 0, NULL);
  g_return_val_if_fail (index < caps->structs->len, NULL);

  return g_ptr_array_index(caps->structs, index);
}

GstCaps *gst_caps_copy_1 (const GstCaps *caps)
{
  GstCaps *newcaps;
  GstStructure *structure;

  g_return_val_if_fail(caps != NULL, NULL);

  newcaps = g_new0(GstCaps, 1);
  newcaps->type = _gst_caps_type;
  newcaps->flags = caps->flags;
  newcaps->structs = g_ptr_array_new();

  if (caps->structs->len > 0){
    structure = gst_caps_get_structure (caps, 0);
    gst_caps_append_structure (newcaps, gst_structure_copy(structure));
  }

  return newcaps;
}

void gst_caps_set_simple (GstCaps *caps, char *field, ...)
{
  GstStructure *structure;
  va_list var_args;

  g_return_if_fail (caps != NULL);
  g_return_if_fail (caps->structs->len == 1);

  structure = gst_caps_get_structure (caps, 0);

  va_start (var_args, field);
  gst_structure_set_valist (structure, field, var_args);
  va_end(var_args);
}

void gst_caps_set_simple_valist (GstCaps *caps, char *field, va_list varargs)
{
  GstStructure *structure;

  g_return_if_fail (caps != NULL);
  g_return_if_fail (caps->structs->len != 1);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set_valist (structure, field, varargs);
}

/* tests */
gboolean gst_caps_is_any (const GstCaps *caps)
{
  g_return_val_if_fail(caps != NULL, FALSE);

  return (caps->flags & GST_CAPS_FLAGS_ANY);
}

gboolean gst_caps_is_empty (const GstCaps *caps)
{
  g_return_val_if_fail(caps != NULL, FALSE);

  if (caps->flags & GST_CAPS_FLAGS_ANY) return FALSE;

  return (caps->structs == NULL) || (caps->structs->len == 0);
}

gboolean gst_caps_is_chained (const GstCaps *caps)
{
  g_return_val_if_fail(caps != NULL, FALSE);

  return (caps->structs->len > 1);
}

static gboolean
_gst_caps_is_fixed_foreach (GQuark field_id, GValue *value, gpointer unused)
{
  GType type = G_VALUE_TYPE (value);
  if (G_TYPE_IS_FUNDAMENTAL (type)) return TRUE;
  if (type == GST_TYPE_FOURCC) return TRUE;
  return FALSE;
}

gboolean gst_caps_is_fixed (const GstCaps *caps)
{
  GstStructure *structure;

  g_return_val_if_fail(caps != NULL, FALSE);

  if (caps->structs->len != 1) return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  return gst_structure_foreach (structure, _gst_caps_is_fixed_foreach, NULL);
}

static gboolean
_gst_structure_is_equal_foreach (GQuark field_id, 
    GValue *val2, gpointer data)
{
  GstStructure *struct1 = (GstStructure *) data;
  const GValue *val1 = gst_structure_id_get_value (struct1, field_id);

  if (val1 == NULL) return FALSE;
  if (gst_value_compare (val1, val2) == GST_VALUE_EQUAL) {
    return TRUE;
  }

  return FALSE;
}

gboolean gst_caps_is_equal_fixed (const GstCaps *caps1, const GstCaps *caps2)
{
  GstStructure *struct1, *struct2;

  g_return_val_if_fail (gst_caps_is_fixed(caps1), FALSE);
  g_return_val_if_fail (gst_caps_is_fixed(caps2), FALSE);

  struct1 = gst_caps_get_structure (caps1, 0);
  struct2 = gst_caps_get_structure (caps2, 0);

  if (struct1->name != struct2->name) {
    return FALSE;
  }
  if (struct1->fields->len != struct2->fields->len) {
    return FALSE;
  }

  return gst_structure_foreach (struct1, _gst_structure_is_equal_foreach,
      struct2);
}

static gboolean
_gst_structure_field_has_compatible (GQuark field_id, 
    GValue *val2, gpointer data)
{
  GValue dest = { 0 };
  GstStructure *struct1 = (GstStructure *) data;
  const GValue *val1 = gst_structure_id_get_value (struct1, field_id);

  if (val1 == NULL) return FALSE;
  if (gst_value_intersect (&dest, val1, val2)) {
    g_value_unset (&dest);
    return TRUE;
  }

  return FALSE;
}

static gboolean
_gst_cap_is_always_compatible (const GstStructure *struct1,
    const GstStructure *struct2)
{
  if(struct1->name != struct2->name){
    return FALSE;
  }

  /* the reversed order is important */
  return gst_structure_foreach ((GstStructure *) struct2, 
      _gst_structure_field_has_compatible, (gpointer) struct1);
}

static gboolean
_gst_caps_cap_is_always_compatible (const GstStructure *struct1,
    const GstCaps *caps2)
{
  int i;

  for(i=0;i<caps2->structs->len;i++){
    GstStructure *struct2 = gst_caps_get_structure (caps2, i);

    if (_gst_cap_is_always_compatible (struct1, struct2)) {
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
gst_caps_is_always_compatible (const GstCaps *caps1, const GstCaps *caps2)
{
  int i;

  g_return_val_if_fail (caps1 != NULL, FALSE);
  g_return_val_if_fail (caps2 != NULL, FALSE);
  /* FIXME: is this right ? */
  g_return_val_if_fail (!gst_caps_is_empty (caps1), FALSE);
  g_return_val_if_fail (!gst_caps_is_empty (caps2), FALSE);
  
  if (gst_caps_is_any (caps2))
    return TRUE;
  if (gst_caps_is_any (caps1))
    return FALSE;
  
  for(i=0;i<caps1->structs->len;i++) {
    GstStructure *struct1 = gst_caps_get_structure (caps1, i);

    if (_gst_caps_cap_is_always_compatible(struct1, caps2) == FALSE){
      return FALSE;
    }

  }

  return FALSE;
}

typedef struct {
  GstStructure *dest;
  const GstStructure *intersect;
  gboolean first_run;
} IntersectData;

static gboolean
gst_caps_structure_intersect_field (GQuark id, GValue *val1, gpointer data)
{
  IntersectData *idata = (IntersectData *) data;
  GValue dest_value = { 0 };
  const GValue *val2 = gst_structure_id_get_value (idata->intersect, id);

  if (val2 == NULL) {
    gst_structure_id_set_value (idata->dest, id, val1);
  } else if (idata->first_run) {
    if (gst_value_intersect (&dest_value, val1, val2)) {
      gst_structure_id_set_value (idata->dest, id, &dest_value);
      g_value_unset (&dest_value);
    } else {
      return FALSE;
    }
  }
  
  return TRUE;
}

static GstStructure *gst_caps_structure_intersect (const GstStructure *struct1,
    const GstStructure *struct2)
{
  IntersectData data;

  g_return_val_if_fail(struct1 != NULL, NULL);
  g_return_val_if_fail(struct2 != NULL, NULL);

  if (struct1->name != struct2->name) return NULL;

  data.dest = gst_structure_id_empty_new (struct1->name);
  data.intersect = struct2;
  data.first_run = TRUE;
  if (!gst_structure_foreach ((GstStructure *) struct1, 
	gst_caps_structure_intersect_field, &data))
    goto error;
  
  data.intersect = struct1;
  data.first_run = FALSE;
  if (!gst_structure_foreach ((GstStructure *) struct2, 
	gst_caps_structure_intersect_field, &data))
    goto error;

  return data.dest;

error:
  gst_structure_free (data.dest);
  return NULL;
}

#if 0
static GstStructure *gst_caps_structure_union (const GstStructure *struct1,
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
GstCaps *gst_caps_intersect (const GstCaps *caps1, const GstCaps *caps2)
{
  int i,j;
  GstStructure *struct1;
  GstStructure *struct2;
  GstCaps *dest;
#if 0
  GstCaps *caps;
#endif

  g_return_val_if_fail (caps1 != NULL, NULL);
  g_return_val_if_fail (caps2 != NULL, NULL);

  if (gst_caps_is_empty (caps1) || gst_caps_is_empty (caps2)){
    return gst_caps_new_empty ();
  }
  if (gst_caps_is_any (caps1)) return gst_caps_copy (caps2);
  if (gst_caps_is_any (caps2)) return gst_caps_copy (caps1);

  dest = gst_caps_new_empty();
  for(i=0;i<caps1->structs->len;i++){
    struct1 = gst_caps_get_structure (caps1, i);
    for(j=0;j<caps2->structs->len;j++){
      GstStructure *istruct;

      struct2 = gst_caps_get_structure (caps2, j);
      istruct = gst_caps_structure_intersect (struct1, struct2);

      gst_caps_append_structure(dest, istruct);
    }
  }

#if 0
  caps = gst_caps_simplify (dest);
  gst_caps_free (dest);

  return caps;
#else
  return dest;
#endif
}

GstCaps *gst_caps_union (const GstCaps *caps1, const GstCaps *caps2)
{
  GstCaps *dest1;
  GstCaps *dest2;

  dest1 = gst_caps_copy (caps1);
  dest2 = gst_caps_copy (caps2);
  gst_caps_append (dest1, dest2);


  /* FIXME: need a simplify function */

  return dest1;
}

typedef struct _NormalizeForeach {
  GstCaps *caps;
  GstStructure *structure;
} NormalizeForeach;

static gboolean
_gst_caps_normalize_foreach (GQuark field_id, GValue *value, gpointer ptr)
{
  NormalizeForeach *nf = (NormalizeForeach *) ptr;
  GValue val = { 0 };
  int i;

  if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    for (i=1; i<gst_value_list_get_size (value); i++) {
      const GValue *v = gst_value_list_get_value (value, i);
      GstStructure *structure = gst_structure_copy (nf->structure);

      gst_structure_id_set_value (structure, field_id, v);
      gst_caps_append_structure (nf->caps, structure);
    }

    gst_value_init_and_copy (&val, gst_value_list_get_value (value, 0));
    gst_structure_id_set_value (nf->structure, field_id, &val);
    g_value_unset (&val);

    return FALSE;
  }
  return TRUE;
}

GstCaps *gst_caps_normalize (const GstCaps *caps)
{
  NormalizeForeach nf;
  GstCaps *newcaps;
  int i;

  g_return_val_if_fail(caps != NULL, NULL);

  newcaps = gst_caps_copy (caps);
  nf.caps = newcaps;

  for(i=0;i<newcaps->structs->len;i++){
    nf.structure = gst_caps_get_structure (newcaps, i);

    while (!gst_structure_foreach (nf.structure, _gst_caps_normalize_foreach,
          &nf));
  }

  return newcaps;
}

static gboolean
simplify_foreach (GQuark field_id, GValue *value, gpointer user_data)
{
  GstStructure *s2 = (GstStructure *) user_data;
  const GValue *v2;

  v2 = gst_structure_id_get_value (s2, field_id);
  if (v2 == NULL) return FALSE;

  if (gst_value_compare (value, v2) == GST_VALUE_EQUAL) return TRUE;
  return FALSE;
}

static gboolean
gst_caps_structure_simplify (GstStructure *struct1, const GstStructure *struct2)
{
  /* FIXME this is just a simple compare.  Better would be to merge
   * the two structures */
  if (struct1->name != struct2->name) return FALSE;
  if (struct1->fields->len != struct2->fields->len) return FALSE;

  return gst_structure_foreach (struct1, simplify_foreach, (void *)struct2);
}

GstCaps *gst_caps_simplify (const GstCaps *caps)
{
  int i;
  int j;
  GstCaps *newcaps;
  GstStructure *structure;
  GstStructure *struct2;

  if (gst_caps_get_size (caps) < 2) {
    return gst_caps_copy (caps);
  }

  newcaps = gst_caps_new_empty ();

  for(i=0;i<gst_caps_get_size (caps);i++){
    structure = gst_caps_get_structure (caps, i);

    for(j=0;j<gst_caps_get_size (newcaps);j++){
      struct2 = gst_caps_get_structure (caps, i);
      if (gst_caps_structure_simplify (struct2, structure)) {
        break;
      }
    }
    if (j==gst_caps_get_size (newcaps)) {
      gst_caps_append_structure (newcaps, gst_structure_copy(structure));
    }
  }

  return newcaps;
}

#ifndef GST_DISABLE_LOADSAVE
xmlNodePtr gst_caps_save_thyself (const GstCaps *caps, xmlNodePtr parent)
{

  return 0;
}

GstCaps *gst_caps_load_thyself (xmlNodePtr parent)
{

  return NULL;
}
#endif

/* utility */
void gst_caps_replace (GstCaps **caps, GstCaps *newcaps)
{
#if 0 /* disable this, since too many plugins rely on undefined behavior */
#ifdef USE_POISONING
  //if (newcaps) CAPS_POISON (newcaps);
#endif
#endif
  if (*caps) gst_caps_free(*caps);
  *caps = newcaps;
}

gchar *gst_caps_to_string (const GstCaps *caps)
{
  int i;
  GstStructure *structure;
  GString *s;

  /* FIXME does this leak? */

  if (caps == NULL) {
    return g_strdup("NULL");
  }
  if(gst_caps_is_any(caps)){
    return g_strdup("ANY");
  }
  if(gst_caps_is_empty(caps)){
    return g_strdup("EMPTY");
  }
  s = g_string_new("");
  structure = gst_caps_get_structure (caps, 0);
  g_string_append(s, gst_structure_to_string(structure));

  for(i=1;i<caps->structs->len;i++){
    structure = gst_caps_get_structure (caps, i);

    g_string_append(s, "; ");
    g_string_append(s, gst_structure_to_string(structure));
  }

  return g_string_free(s, FALSE);
}

static gboolean _gst_caps_from_string_inplace (GstCaps *caps,
    const gchar *string)
{
  GstStructure *structure;
  gchar *s;

  if (strcmp("ANY", string)==0) {
    caps->flags = GST_CAPS_FLAGS_ANY;
    return TRUE;
  }
  if (strcmp("NONE", string)==0) {
    return TRUE;
  }

  structure = gst_structure_from_string(string, &s);
  if (structure == NULL) {
    return FALSE;
  }
  gst_caps_append_structure (caps, structure);

  while (*s == ';') {
    s++;
    while (g_ascii_isspace(*s))s++;
    structure = gst_structure_from_string(s, &s);
    if (structure == NULL) {
      return FALSE;
    }
    gst_caps_append_structure (caps, structure);
    while (g_ascii_isspace(*s))s++;
  }

  if (*s != 0){
    return FALSE;
  }

  return TRUE;
}

GstCaps *gst_caps_from_string (const gchar *string)
{
  GstCaps *caps;

  caps = gst_caps_new_empty();
  if (_gst_caps_from_string_inplace (caps, string)) {
    return caps;
  } else {
    gst_caps_free (caps);
    return NULL;
  }
}

static void _gst_caps_transform_to_string (const GValue *src_value,
    GValue *dest_value)
{
  g_return_if_fail (src_value != NULL);
  g_return_if_fail (dest_value != NULL);

  dest_value->data[0].v_pointer =
    gst_caps_to_string (src_value->data[0].v_pointer);
}

static void _gst_caps_value_init (GValue *value)
{
  value->data[0].v_pointer = gst_caps_new_empty();
}

static void _gst_caps_value_free (GValue *value)
{
  if (value->data[0].v_pointer) gst_caps_free (value->data[0].v_pointer);
}

static void _gst_caps_value_copy (const GValue *src, GValue *dest)
{
  if (dest->data[0].v_pointer) {
    gst_caps_free (dest->data[0].v_pointer);
  }
  if (src->data[0].v_pointer) {
    dest->data[0].v_pointer = gst_caps_copy (src->data[0].v_pointer);
  } else {
    dest->data[0].v_pointer = NULL;
  }
}

static gpointer _gst_caps_value_peek_pointer (const GValue *value)
{
  return value->data[0].v_pointer;
}

/* fixate utility functions */

gboolean gst_caps_structure_fixate_field_nearest_int (GstStructure *structure,
    const char *field_name, int target)
{
  const GValue *value;

  g_return_val_if_fail(gst_structure_has_field (structure, field_name), FALSE);

  value = gst_structure_get_value (structure, field_name);

  if (G_VALUE_TYPE (value) == G_TYPE_INT) {
    /* already fixed */
    return FALSE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_INT_RANGE) {
    int x;
    x = gst_value_get_int_range_min (value);
    if (target < x) target = x;
    x = gst_value_get_int_range_max (value);
    if (target > x) target = x;
    gst_structure_set (structure, field_name, G_TYPE_INT, target, NULL);
    return TRUE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    const GValue *list_value;
    int i, n;
    int best = 0;
    int best_index = -1;

    n = gst_value_list_get_size (value);
    for(i=0;i<n;i++){
      list_value = gst_value_list_get_value (value, i);
      if (G_VALUE_TYPE (list_value) == G_TYPE_INT) {
	int x = g_value_get_int (list_value);
	if (best_index == -1 || (ABS(target-x) < ABS(best-x))) {
	  best_index = i;
	  best = x;
	}
      }
    }
    if(best_index != -1) {
      gst_structure_set (structure, field_name, G_TYPE_INT, best, NULL);
      return TRUE;
    }
    return FALSE;
  }

  return FALSE;
}

gboolean gst_caps_structure_fixate_field_nearest_double (GstStructure
    *structure, const char *field_name, double target)
{
  const GValue *value;

  g_return_val_if_fail(gst_structure_has_field (structure, field_name), FALSE);

  value = gst_structure_get_value (structure, field_name);

  if (G_VALUE_TYPE (value) == G_TYPE_DOUBLE) {
    /* already fixed */
    return FALSE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_DOUBLE_RANGE) {
    double x;
    x = gst_value_get_double_range_min (value);
    if (target < x) target = x;
    x = gst_value_get_double_range_max (value);
    if (target > x) target = x;
    gst_structure_set (structure, field_name, G_TYPE_DOUBLE, target, NULL);
    return TRUE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    const GValue *list_value;
    int i, n;
    double best = 0;
    int best_index = -1;

    n = gst_value_list_get_size (value);
    for(i=0;i<n;i++){
      list_value = gst_value_list_get_value (value, i);
      if (G_VALUE_TYPE (list_value) == G_TYPE_DOUBLE) {
	double x = g_value_get_double (list_value);
	if (best_index == -1 || (ABS(target-x) < ABS(best-x))) {
	  best_index = i;
	  best = x;
	}
      }
    }
    if(best_index != -1) {
      gst_structure_set (structure, field_name, G_TYPE_DOUBLE, best, NULL);
      return TRUE;
    }
    return FALSE;
  }

  return FALSE;

}

