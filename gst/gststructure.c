/* GStreamer
 * Copyright (C) 2003 David A. Schleef <ds@schleef.org>
 *
 * gststructure.c: lists of { GQuark, GValue } tuples
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

#include "gst_private.h"
#include <gst/gst.h>
#include <gobject/gvaluecollector.h>

typedef struct _GstStructureField GstStructureField;

struct _GstStructureField
{
  GQuark name;
  GValue value;
};

#define GST_STRUCTURE_FIELD(structure, index) \
    &g_array_index((structure)->fields, GstStructureField, (index))

static void gst_structure_set_field (GstStructure * structure,
    GstStructureField * field);
static GstStructureField *gst_structure_get_field (const GstStructure *
    structure, const gchar * fieldname);
static GstStructureField *gst_structure_id_get_field (const GstStructure *
    structure, GQuark field);
static void gst_structure_transform_to_string (const GValue * src_value,
    GValue * dest_value);
static GstStructure *gst_structure_copy_conditional (const GstStructure *
    structure);
static gboolean gst_structure_parse_value (gchar * str, gchar ** after,
    GValue * value, GType default_type);
static gboolean gst_structure_parse_simple_string (gchar * s, gchar ** end);

GType
gst_structure_get_type (void)
{
  static GType gst_structure_type;

  if (!gst_structure_type) {
    gst_structure_type = g_boxed_type_register_static ("GstStructure",
        (GBoxedCopyFunc) gst_structure_copy_conditional,
        (GBoxedFreeFunc) gst_structure_free);

    g_value_register_transform_func (gst_structure_type, G_TYPE_STRING,
        gst_structure_transform_to_string);
  }

  return gst_structure_type;
}

/**
 * gst_structure_id_empty_new:
 * @quark: name of new structure
 *
 * Creates a new, empty #GstStructure with the given name.
 *
 * Returns: a new, empty #GstStructure
 */
GstStructure *
gst_structure_id_empty_new (GQuark quark)
{
  GstStructure *structure;

  g_return_val_if_fail (quark != 0, NULL);

  structure = g_new0 (GstStructure, 1);
  structure->type = gst_structure_get_type ();
  structure->name = quark;
  structure->fields = g_array_new (FALSE, TRUE, sizeof (GstStructureField));

  return structure;
}

/**
 * gst_structure_empty_new:
 * @name: name of new structure
 *
 * Creates a new, empty #GstStructure with the given name.
 *
 * Returns: a new, empty #GstStructure
 */
GstStructure *
gst_structure_empty_new (const gchar * name)
{
  GstStructure *structure;

  g_return_val_if_fail (name != NULL, NULL);

  structure = g_new0 (GstStructure, 1);
  structure->type = gst_structure_get_type ();
  structure->name = g_quark_from_string (name);
  structure->fields = g_array_new (FALSE, TRUE, sizeof (GstStructureField));

  return structure;
}

/**
 * gst_structure_new:
 * @name: name of new structure
 * @firstfield: name of first field to set
 * @...: additional arguments
 *
 * Creates a new #GstStructure with the given name.  Parses the
 * list of variable arguments and sets fields to the values listed.
 * Variable arguments should be passed as field name, field type,
 * and value.  Last variable argument should be NULL.
 *
 * Returns: a new #GstStructure
 */
GstStructure *
gst_structure_new (const gchar * name, const gchar * firstfield, ...)
{
  GstStructure *structure;
  va_list varargs;

  g_return_val_if_fail (name != NULL, NULL);

  va_start (varargs, firstfield);

  structure = gst_structure_new_valist (name, firstfield, varargs);

  va_end (varargs);

  return structure;
}

/**
 * gst_structure_new_valist:
 * @name: name of new structure
 * @firstfield: name of first field to set
 * @varargs: variable argument list
 *
 * Creates a new #GstStructure with the given name.  Structure fields
 * are set according to the varargs in a manner similar to
 * @gst_structure_new.
 *
 * Returns: a new #GstStructure
 */
GstStructure *
gst_structure_new_valist (const gchar * name,
    const gchar * firstfield, va_list varargs)
{
  GstStructure *structure;

  g_return_val_if_fail (name != NULL, NULL);

  structure = gst_structure_empty_new (name);
  gst_structure_set_valist (structure, firstfield, varargs);

  return structure;
}

/**
 * gst_structure_copy:
 * @structure: a #GstStructure to duplicate
 *
 * Duplicates a #GstStructure and all its fields and values.
 *
 * Returns: a new #GstStructure.
 */
GstStructure *
gst_structure_copy (const GstStructure * structure)
{
  GstStructure *new_structure;
  GstStructureField *field;
  int i;

  g_return_val_if_fail (structure != NULL, NULL);

  new_structure = gst_structure_empty_new (g_quark_to_string (structure->name));
  new_structure->name = structure->name;

  for (i = 0; i < structure->fields->len; i++) {
    GstStructureField new_field = { 0 };

    field = GST_STRUCTURE_FIELD (structure, i);

    new_field.name = field->name;
    gst_value_init_and_copy (&new_field.value, &field->value);
    g_array_append_val (new_structure->fields, new_field);
  }

  return new_structure;
}

/**
 * gst_structure_free: 
 * @structure: the #GstStructure to free
 *
 * Frees a #GstStructure and all its fields and values.
 */
void
gst_structure_free (GstStructure * structure)
{
  GstStructureField *field;
  int i;

  g_return_if_fail (structure != NULL);

  for (i = 0; i < structure->fields->len; i++) {
    field = GST_STRUCTURE_FIELD (structure, i);

    if (G_IS_VALUE (&field->value)) {
      g_value_unset (&field->value);
    }
  }
  g_array_free (structure->fields, TRUE);
#ifdef USE_POISONING
  memset (structure, 0xff, sizeof (GstStructure));
#endif
  g_free (structure);
}

/**
 * gst_structure_get_name:
 * @structure: a #GstStructure
 *
 * Accessor fuction.
 *
 * Returns: the name of the structure.
 */
const gchar *
gst_structure_get_name (const GstStructure * structure)
{
  g_return_val_if_fail (structure != NULL, NULL);

  return g_quark_to_string (structure->name);
}

/**
 * gst_structure_get_name:
 * @structure: a #GstStructure
 *
 * Accessor fuction.
 *
 * Returns: the quark representing the name of the structure.
 */
GQuark
gst_structure_get_name_id (const GstStructure * structure)
{
  g_return_val_if_fail (structure != NULL, 0);

  return structure->name;
}

/**
 * gst_structure_set_name:
 * @structure: a #GstStructure
 * @name: the new name of the structure
 *
 * Sets the name of the structure to the given name.  The string
 * provided is copied before being used.
 */
void
gst_structure_set_name (GstStructure * structure, const gchar * name)
{
  g_return_if_fail (structure != NULL);
  g_return_if_fail (name != NULL);

  structure->name = g_quark_from_string (name);
}

/**
 * gst_structure_id_set_value:
 * @structure: a #GstStructure
 * @field: a #GQuark representing a field
 * @value: the new value of the field
 *
 * Sets the field with the given ID to the provided value.  If the field
 * does not exist, it is created.  If the field exists, the previous
 * value is freed.
 */
void
gst_structure_id_set_value (GstStructure * structure,
    GQuark field, const GValue * value)
{
  GstStructureField gsfield = { 0, {0,} };

  g_return_if_fail (structure != NULL);
  g_return_if_fail (G_IS_VALUE (value));

  gsfield.name = field;
  gst_value_init_and_copy (&gsfield.value, value);

  gst_structure_set_field (structure, &gsfield);
}

/**
 * gst_structure_set_value:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to set
 * @value: the new value of the field
 *
 * Sets the field with the given name to the provided value.  If the field
 * does not exist, it is created.  If the field exists, the previous
 * value is freed.
 */
void
gst_structure_set_value (GstStructure * structure,
    const gchar * fieldname, const GValue * value)
{
  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  g_return_if_fail (G_IS_VALUE (value));

  gst_structure_id_set_value (structure, g_quark_from_string (fieldname),
      value);
}

/**
 * gst_structure_set:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to set
 * @...: variable arguments
 *
 * Parses the variable arguments and sets fields accordingly.
 * Variable arguments should be in the form field name, field type
 * (as a GType), value.  The last variable argument should be NULL.
 */
void
gst_structure_set (GstStructure * structure, const gchar * field, ...)
{
  va_list varargs;

  g_return_if_fail (structure != NULL);

  va_start (varargs, field);

  gst_structure_set_valist (structure, field, varargs);

  va_end (varargs);
}

/**
 * gst_structure_set_valist:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to set
 * @varargs: variable arguments
 *
 * va_list form of #gst_structure_set.
 */
void
gst_structure_set_valist (GstStructure * structure,
    const gchar * fieldname, va_list varargs)
{
  GType type;
  int i;
  double d;
  char *s;

  g_return_if_fail (structure != NULL);

  while (fieldname) {
    GstStructureField field = { 0 };

    field.name = g_quark_from_string (fieldname);

    type = va_arg (varargs, GType);

    switch (type) {
      case G_TYPE_INT:
        i = va_arg (varargs, int);

        g_value_init (&field.value, G_TYPE_INT);
        g_value_set_int (&field.value, i);
        break;
      case G_TYPE_DOUBLE:
        d = va_arg (varargs, double);

        g_value_init (&field.value, G_TYPE_DOUBLE);
        g_value_set_double (&field.value, d);
        break;
      case G_TYPE_BOOLEAN:
        i = va_arg (varargs, int);

        g_value_init (&field.value, G_TYPE_BOOLEAN);
        g_value_set_boolean (&field.value, i);
        break;
      case G_TYPE_STRING:
        s = va_arg (varargs, char *);

        g_value_init (&field.value, G_TYPE_STRING);
        g_value_set_string (&field.value, s);
        break;
      default:
        if (type == GST_TYPE_FOURCC) {
          i = va_arg (varargs, int);

          g_value_init (&field.value, GST_TYPE_FOURCC);
          gst_value_set_fourcc (&field.value, i);
        } else if (type == GST_TYPE_INT_RANGE) {
          int min, max;
          min = va_arg (varargs, int);
          max = va_arg (varargs, int);

          g_value_init (&field.value, GST_TYPE_INT_RANGE);
          gst_value_set_int_range (&field.value, min, max);
        } else if (type == GST_TYPE_DOUBLE_RANGE) {
          double min, max;
          min = va_arg (varargs, double);
          max = va_arg (varargs, double);

          g_value_init (&field.value, GST_TYPE_DOUBLE_RANGE);
          gst_value_set_double_range (&field.value, min, max);
        } else if (type == GST_TYPE_BUFFER) {
          GstBuffer *buffer = va_arg (varargs, GstBuffer *);

          g_value_init (&field.value, GST_TYPE_BUFFER);
          g_value_set_boxed (&field.value, buffer);
        } else if (type == GST_TYPE_FRACTION) {
          gint n, d;
          n = va_arg (varargs, int);
          d = va_arg (varargs, int);

          g_value_init (&field.value, GST_TYPE_FRACTION);
          gst_value_set_fraction (&field.value, n, d);
        } else {
          g_critical ("unimplemented vararg field type %d\n", (int) type);
          return;
        }
        break;
    }

    gst_structure_set_field (structure, &field);

    fieldname = va_arg (varargs, gchar *);
  }
}

/**
 * gst_structure_set_field:
 * @structure: a #GstStructure
 * @field: the #GstStructureField to set
 *
 * Sets a field in the structure.  If the structure currently contains
 * a field with the same name, it is replaced with the provided field.
 * Otherwise, the field is added to the structure.  The field's value
 * is not deeply copied.
 *
 * This function is intended mainly for internal use.  The function
 * #gst_structure_set() is recommended instead of this one.
 */
static void
gst_structure_set_field (GstStructure * structure, GstStructureField * field)
{
  GstStructureField *f;
  int i;

  for (i = 0; i < structure->fields->len; i++) {
    f = GST_STRUCTURE_FIELD (structure, i);

    if (f->name == field->name) {
      g_value_unset (&f->value);
      memcpy (f, field, sizeof (GstStructureField));
      return;
    }
  }

  g_array_append_val (structure->fields, *field);
}

/* FIXME: is this private ? if so remove gtk-doc
 * gst_structure_id_get_field:
 * @structure: a #GstStructure
 * @field_id: the GQuark of the field to get
 *
 * Gets the specified field from the structure.  If there is no
 * field with the given ID, NULL is returned.
 *
 * Returns: the #GstStructureField with the given ID
 */
static GstStructureField *
gst_structure_id_get_field (const GstStructure * structure, GQuark field_id)
{
  GstStructureField *field;
  int i;

  g_return_val_if_fail (structure != NULL, NULL);

  for (i = 0; i < structure->fields->len; i++) {
    field = GST_STRUCTURE_FIELD (structure, i);

    if (field->name == field_id)
      return field;
  }

  return NULL;
}

/**
 * gst_structure_get_field:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to get
 *
 * Gets the specified field from the structure.  If there is no
 * field with the given ID, NULL is returned.
 *
 * Returns: the #GstStructureField with the given name
 */
static GstStructureField *
gst_structure_get_field (const GstStructure * structure,
    const gchar * fieldname)
{
  g_return_val_if_fail (structure != NULL, NULL);
  g_return_val_if_fail (fieldname != NULL, NULL);

  return gst_structure_id_get_field (structure,
      g_quark_from_string (fieldname));
}

/**
 * gst_structure_get_value:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to get
 *
 * Accessor function.
 *
 * Returns: the #GValue corresponding to the field with the given name.
 */
const GValue *
gst_structure_get_value (const GstStructure * structure,
    const gchar * fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, NULL);
  g_return_val_if_fail (fieldname != NULL, NULL);

  field = gst_structure_get_field (structure, fieldname);
  if (field == NULL)
    return NULL;

  return &field->value;
}

/**
 * gst_structure_id_get_value:
 * @structure: a #GstStructure
 * @field: the #GQuark of the field to get
 *
 * Accessor function.
 *
 * Returns: the #GValue corresponding to the field with the given name 
 *          identifier.
 */
const GValue *
gst_structure_id_get_value (const GstStructure * structure, GQuark field)
{
  GstStructureField *gsfield;

  g_return_val_if_fail (structure != NULL, NULL);

  gsfield = gst_structure_id_get_field (structure, field);
  if (gsfield == NULL)
    return NULL;

  return &gsfield->value;
}

/**
 * gst_structure_remove_field:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to remove
 *
 * Removes the field with the given name.  If the field with the given
 * name does not exist, the structure is unchanged.
 */
void
gst_structure_remove_field (GstStructure * structure, const gchar * fieldname)
{
  GstStructureField *field;
  GQuark id;
  int i;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);

  id = g_quark_from_string (fieldname);

  for (i = 0; i < structure->fields->len; i++) {
    field = GST_STRUCTURE_FIELD (structure, i);

    if (field->name == id) {
      if (G_IS_VALUE (&field->value)) {
        g_value_unset (&field->value);
      }
      structure->fields = g_array_remove_index (structure->fields, i);
      return;
    }
  }
}

/**
 * gst_structure_remove_fields:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to remove
 * @...: NULL-terminated list of more fieldnames to remove
 *
 * Removes the field with the given names. If a field does not exist, the
 * argument is ignored.
 */
void
gst_structure_remove_fields (GstStructure * structure,
    const gchar * fieldname, ...)
{
  va_list varargs;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);

  va_start (varargs, fieldname);

  gst_structure_remove_fields_valist (structure, fieldname, varargs);

  va_end (varargs);
}

/**
 * gst_structure_remove_fields_valist:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to remove
 * @varargs: NULL-terminated list of more fieldnames to remove
 *
 * Removes the field with the given names. If a field does not exist, the
 * argument is ignored.
 */
void
gst_structure_remove_fields_valist (GstStructure * structure,
    const gchar * fieldname, va_list varargs)
{
  gchar *field = (gchar *) fieldname;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);

  while (field) {
    gst_structure_remove_field (structure, field);
    field = va_arg (varargs, char *);
  }
}

/**
 * gst_structure_remove_all_fields:
 * @structure: a #GstStructure
 *
 * Removes all fields in a GstStructure. 
 */
void
gst_structure_remove_all_fields (GstStructure * structure)
{
  GstStructureField *field;
  int i;

  g_return_if_fail (structure != NULL);

  for (i = structure->fields->len - 1; i >= 0; i--) {
    field = GST_STRUCTURE_FIELD (structure, i);

    if (G_IS_VALUE (&field->value)) {
      g_value_unset (&field->value);
    }
    structure->fields = g_array_remove_index (structure->fields, i);
  }
}

/**
 * gst_structure_get_field_type:
 * @structure: a #GstStructure
 * @fieldname: the name of the field
 *
 * Finds the field with the given name, and returns the type of the
 * value it contains.  If the field is not found, G_TYPE_INVALID is
 * returned.
 *
 * Returns: the #GValue of the field
 */
GType
gst_structure_get_field_type (const GstStructure * structure,
    const gchar * fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, G_TYPE_INVALID);
  g_return_val_if_fail (fieldname != NULL, G_TYPE_INVALID);

  field = gst_structure_get_field (structure, fieldname);
  if (field == NULL)
    return G_TYPE_INVALID;

  return G_VALUE_TYPE (&field->value);
}

/**
 * gst_structure_n_fields:
 * @structure: a #GstStructure
 *
 * Accessor function.
 *
 * Returns: the number of fields in the structure
 */
gint
gst_structure_n_fields (const GstStructure * structure)
{
  g_return_val_if_fail (structure != NULL, 0);

  return structure->fields->len;
}

/**
 * gst_structure_foreach:
 * @structure: a #GstStructure
 * @func: a function to call for each field
 * @user_data: private data
 *
 * Calls the provided function once for each field in the #GstStructure.
 *
 * Returns: TRUE if the supplied function returns TRUE For each of the fields,
 * FALSE otherwise.
 */
gboolean
gst_structure_foreach (GstStructure * structure,
    GstStructureForeachFunc func, gpointer user_data)
{
  int i;
  GstStructureField *field;
  gboolean ret;

  for (i = 0; i < structure->fields->len; i++) {
    field = GST_STRUCTURE_FIELD (structure, i);

    ret = func (field->name, &field->value, user_data);
    if (!ret)
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_structure_has_field:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 *
 * Accessor function.
 *
 * Returns: TRUE if the structure contains a field with the given name
 */
gboolean
gst_structure_has_field (const GstStructure * structure,
    const gchar * fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, 0);
  g_return_val_if_fail (fieldname != NULL, 0);

  field = gst_structure_get_field (structure, fieldname);

  return (field != NULL);
}

/**
 * gst_structure_has_field_typed:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @type: the type of a value
 *
 * Accessor function.
 *
 * Returns: TRUE if the structure contains a field with the given name and type
 */
gboolean
gst_structure_has_field_typed (const GstStructure * structure,
    const gchar * fieldname, GType type)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, 0);
  g_return_val_if_fail (fieldname != NULL, 0);

  field = gst_structure_get_field (structure, fieldname);
  if (field == NULL)
    return FALSE;

  return (G_VALUE_TYPE (&field->value) == type);
}


/* utility functions */

/**
 * gst_structure_get_boolean:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @value: a pointer to a #gboolean to set
 *
 * Sets the boolean pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean
gst_structure_get_boolean (const GstStructure * structure,
    const gchar * fieldname, gboolean * value)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!G_VALUE_HOLDS_BOOLEAN (&field->value))
    return FALSE;

  *value = g_value_get_boolean (&field->value);

  return TRUE;
}

/**
 * gst_structure_get_int:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @value: a pointer to an int to set
 *
 * Sets the int pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean
gst_structure_get_int (const GstStructure * structure,
    const gchar * fieldname, gint * value)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!G_VALUE_HOLDS_INT (&field->value))
    return FALSE;

  *value = g_value_get_int (&field->value);

  return TRUE;
}

/**
 * gst_structure_get_fourcc:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @value: a pointer to a #GstFourcc to set
 *
 * Sets the #GstFourcc pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean
gst_structure_get_fourcc (const GstStructure * structure,
    const gchar * fieldname, guint32 * value)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!GST_VALUE_HOLDS_FOURCC (&field->value))
    return FALSE;

  *value = gst_value_get_fourcc (&field->value);

  return TRUE;
}

/**
 * gst_structure_get_double:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @value: a pointer to a #GstFourcc to set
 *
 * Sets the double pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean
gst_structure_get_double (const GstStructure * structure,
    const gchar * fieldname, gdouble * value)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!G_VALUE_HOLDS_DOUBLE (&field->value))
    return FALSE;

  *value = g_value_get_double (&field->value);

  return TRUE;
}

/**
 * gst_structure_get_string:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 *
 * Finds the field corresponding to @fieldname, and returns the string
 * contained in the field's value.  Caller is responsible for making
 * sure the field exists and has the correct type.
 *
 * The string should not be modified, and remains valid until the next
 * call to a gst_structure_*() function with the given structure.
 *
 * Returns: a pointer to the string
 */
const gchar *
gst_structure_get_string (const GstStructure * structure,
    const gchar * fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, NULL);
  g_return_val_if_fail (fieldname != NULL, NULL);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!G_VALUE_HOLDS_STRING (&field->value))
    return FALSE;

  return g_value_get_string (&field->value);
}

typedef struct _GstStructureAbbreviation
{
  char *type_name;
  GType type;
}
GstStructureAbbreviation;

static GstStructureAbbreviation gst_structure_abbrs[] = {
  {"int", G_TYPE_INT},
  {"i", G_TYPE_INT},
  {"float", G_TYPE_FLOAT},
  {"f", G_TYPE_FLOAT},
  {"double", G_TYPE_DOUBLE},
  {"d", G_TYPE_DOUBLE},
/* these are implemented with strcmp below */
//{ "buffer",   GST_TYPE_BUFFER },
//{ "fourcc",   GST_TYPE_FOURCC },
//{ "4",   GST_TYPE_FOURCC },
//{ "fraction",   GST_TYPE_FRACTION },
  {"boolean", G_TYPE_BOOLEAN},
  {"bool", G_TYPE_BOOLEAN},
  {"b", G_TYPE_BOOLEAN},
  {"string", G_TYPE_STRING},
  {"str", G_TYPE_STRING},
  {"s", G_TYPE_STRING}
};

static GType
gst_structure_from_abbr (const char *type_name)
{
  int i;

  g_return_val_if_fail (type_name != NULL, G_TYPE_INVALID);

  for (i = 0; i < G_N_ELEMENTS (gst_structure_abbrs); i++) {
    if (strcmp (type_name, gst_structure_abbrs[i].type_name) == 0) {
      return gst_structure_abbrs[i].type;
    }
  }

  /* FIXME shouldn't be a special case */
  if (strcmp (type_name, "fourcc") == 0 || strcmp (type_name, "4") == 0) {
    return GST_TYPE_FOURCC;
  }
  if (strcmp (type_name, "buffer") == 0) {
    return GST_TYPE_BUFFER;
  }
  if (strcmp (type_name, "fraction") == 0) {
    return GST_TYPE_FRACTION;
  }

  return g_type_from_name (type_name);
}

static const char *
gst_structure_to_abbr (GType type)
{
  int i;

  g_return_val_if_fail (type != G_TYPE_INVALID, NULL);

  for (i = 0; i < G_N_ELEMENTS (gst_structure_abbrs); i++) {
    if (type == gst_structure_abbrs[i].type) {
      return gst_structure_abbrs[i].type_name;
    }
  }

  /* FIXME shouldn't be a special case */
  if (type == GST_TYPE_FOURCC) {
    return "fourcc";
  }
  if (type == GST_TYPE_BUFFER) {
    return "buffer";
  }
  if (type == GST_TYPE_FRACTION) {
    return "fraction";
  }

  return g_type_name (type);
}

static GType
gst_structure_value_get_generic_type (GValue * val)
{
  if (G_VALUE_TYPE (val) == GST_TYPE_LIST
      || G_VALUE_TYPE (val) == GST_TYPE_FIXED_LIST) {
    GArray *array = g_value_peek_pointer (val);

    if (array->len > 0) {
      GValue *value = &g_array_index (array, GValue, 0);

      return gst_structure_value_get_generic_type (value);
    } else {
      return G_TYPE_INT;
    }
  } else if (G_VALUE_TYPE (val) == GST_TYPE_INT_RANGE) {
    return G_TYPE_INT;
  } else if (G_VALUE_TYPE (val) == GST_TYPE_DOUBLE_RANGE) {
    return G_TYPE_DOUBLE;
  }
  return G_VALUE_TYPE (val);
}

#define GST_ASCII_IS_STRING(c) (g_ascii_isalnum((c)) || ((c) == '_') || \
      ((c) == '-') || ((c) == '+') || ((c) == '/') || ((c) == ':') || \
      ((c) == '.'))

/**
 * gst_structure_to_string:
 * @structure: a #GstStructure
 *
 * Converts @structure to a human-readable representation.
 * 
 * Returns: a pointer to string allocated by g_malloc()
 */
gchar *
gst_structure_to_string (const GstStructure * structure)
{
  GstStructureField *field;
  GString *s;
  int i;

  /* NOTE:  This function is potentially called by the debug system,
   * so any calls to gst_log() (and GST_DEBUG(), GST_LOG(), etc.)
   * should be careful to avoid recursion.  This includes any functions
   * called by gst_structure_to_string.  In particular, calls should
   * not use the GST_PTR_FORMAT extension.  */

  g_return_val_if_fail (structure != NULL, NULL);

  s = g_string_new ("");
  /* FIXME this string may need to be escaped */
  g_string_append_printf (s, "%s", g_quark_to_string (structure->name));
  for (i = 0; i < structure->fields->len; i++) {
    char *t;
    GType type;

    field = GST_STRUCTURE_FIELD (structure, i);

    t = gst_value_serialize (&field->value);
    type = gst_structure_value_get_generic_type (&field->value);

    g_string_append_printf (s, ", %s=(%s)%s", g_quark_to_string (field->name),
        gst_structure_to_abbr (type), t);
    g_free (t);
  }
  return g_string_free (s, FALSE);
}

/*
 * r will still point to the string. if end == next, the string will not be 
 * null-terminated. In all other cases it will be.
 * end = pointer to char behind end of string, next = pointer to start of
 * unread data.
 * THIS FUNCTION MODIFIES THE STRING AND DETECTS INSIDE A NONTERMINATED STRING 
 */
static gboolean
gst_structure_parse_string (gchar * s, gchar ** end, gchar ** next)
{
  gchar *w;

  if (*s == 0)
    return FALSE;

  if (*s != '"') {
    int ret;

    ret = gst_structure_parse_simple_string (s, end);
    *next = *end;

    return ret;
  }

  w = s;
  s++;
  while (*s != '"') {
    if (*s == 0)
      return FALSE;

    if (*s == '\\') {
      s++;
    }

    *w = *s;
    w++;
    s++;
  }
  s++;

  *end = w;
  *next = s;

  return TRUE;
}

static gboolean
gst_structure_parse_range (gchar * s, gchar ** after, GValue * value,
    GType type)
{
  GValue value1 = { 0 };
  GValue value2 = { 0 };
  GType range_type;
  gboolean ret;


  if (*s != '[')
    return FALSE;
  s++;

  ret = gst_structure_parse_value (s, &s, &value1, type);
  if (ret == FALSE)
    return FALSE;

  while (g_ascii_isspace (*s))
    s++;

  if (*s != ',')
    return FALSE;
  s++;

  while (g_ascii_isspace (*s))
    s++;

  ret = gst_structure_parse_value (s, &s, &value2, type);
  if (ret == FALSE)
    return FALSE;

  while (g_ascii_isspace (*s))
    s++;

  if (*s != ']')
    return FALSE;
  s++;

  if (G_VALUE_TYPE (&value1) != G_VALUE_TYPE (&value2))
    return FALSE;

  if (G_VALUE_TYPE (&value1) == G_TYPE_DOUBLE) {
    range_type = GST_TYPE_DOUBLE_RANGE;
  } else if (G_VALUE_TYPE (&value1) == G_TYPE_INT) {
    range_type = GST_TYPE_INT_RANGE;
  } else {
    return FALSE;
  }

  g_value_init (value, range_type);
  if (range_type == GST_TYPE_DOUBLE_RANGE) {
    gst_value_set_double_range (value, g_value_get_double (&value1),
        g_value_get_double (&value2));
  } else {
    gst_value_set_int_range (value, g_value_get_int (&value1),
        g_value_get_int (&value2));
  }

  *after = s;
  return TRUE;
}

static gboolean
gst_structure_parse_any_list (gchar * s, gchar ** after, GValue * value,
    GType type, GType list_type, char begin, char end)
{
  GValue list_value = { 0 };
  gboolean ret;
  GArray *array;

  g_value_init (value, list_type);
  array = g_value_peek_pointer (value);

  if (*s != begin)
    return FALSE;
  s++;

  while (g_ascii_isspace (*s))
    s++;
  if (*s == end) {
    s++;
    *after = s;
    return TRUE;
  }

  ret = gst_structure_parse_value (s, &s, &list_value, type);
  if (ret == FALSE)
    return FALSE;

  g_array_append_val (array, list_value);

  while (g_ascii_isspace (*s))
    s++;

  while (*s != end) {
    if (*s != ',')
      return FALSE;
    s++;

    while (g_ascii_isspace (*s))
      s++;

    memset (&list_value, 0, sizeof (list_value));
    ret = gst_structure_parse_value (s, &s, &list_value, type);
    if (ret == FALSE)
      return FALSE;

    g_array_append_val (array, list_value);
    while (g_ascii_isspace (*s))
      s++;
  }

  s++;

  *after = s;
  return TRUE;
}

static gboolean
gst_structure_parse_list (gchar * s, gchar ** after, GValue * value, GType type)
{
  return gst_structure_parse_any_list (s, after, value, type, GST_TYPE_LIST,
      '{', '}');
}

static gboolean
gst_structure_parse_fixed_list (gchar * s, gchar ** after, GValue * value,
    GType type)
{
  return gst_structure_parse_any_list (s, after, value, type,
      GST_TYPE_FIXED_LIST, '<', '>');
}

static gboolean
gst_structure_parse_simple_string (gchar * str, gchar ** end)
{
  char *s = str;

  while (GST_ASCII_IS_STRING (*s)) {
    s++;
  }

  *end = s;

  return (s != str);
}

static gboolean
gst_structure_parse_field (gchar * str,
    gchar ** after, GstStructureField * field)
{
  gchar *name;
  gchar *name_end;
  gchar *s;
  gchar c;

  s = str;

  while (g_ascii_isspace (*s))
    s++;
  name = s;
  if (!gst_structure_parse_simple_string (s, &name_end))
    return FALSE;

  s = name_end;
  while (g_ascii_isspace (*s))
    s++;

  if (*s != '=')
    return FALSE;
  s++;

  c = *name_end;
  *name_end = 0;
  field->name = g_quark_from_string (name);
  *name_end = c;

  if (!gst_structure_parse_value (s, &s, &field->value, G_TYPE_INVALID))
    return FALSE;

  *after = s;
  return TRUE;
}

static gboolean
gst_structure_parse_value (gchar * str,
    gchar ** after, GValue * value, GType default_type)
{
  gchar *type_name;
  gchar *type_end;
  gchar *value_s;
  gchar *value_end;
  gchar *s;
  gchar c;
  int ret = 0;
  GType type = default_type;


  s = str;
  while (g_ascii_isspace (*s))
    s++;

  type_name = NULL;
  if (*s == '(') {
    type = G_TYPE_INVALID;

    s++;
    while (g_ascii_isspace (*s))
      s++;
    type_name = s;
    if (!gst_structure_parse_simple_string (s, &type_end))
      return FALSE;
    s = type_end;
    while (g_ascii_isspace (*s))
      s++;
    if (*s != ')')
      return FALSE;
    s++;
    while (g_ascii_isspace (*s))
      s++;

    c = *type_end;
    *type_end = 0;
    type = gst_structure_from_abbr (type_name);
    *type_end = c;

    if (type == G_TYPE_INVALID)
      return FALSE;
  }

  while (g_ascii_isspace (*s))
    s++;
  if (*s == '[') {
    ret = gst_structure_parse_range (s, &s, value, type);
  } else if (*s == '{') {
    ret = gst_structure_parse_list (s, &s, value, type);
  } else if (*s == '<') {
    ret = gst_structure_parse_fixed_list (s, &s, value, type);
  } else {
    value_s = s;
    if (!gst_structure_parse_string (s, &value_end, &s))
      return FALSE;

    c = *value_end;
    *value_end = 0;
    if (type == G_TYPE_INVALID) {
      GType try_types[] = { G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_STRING };
      int i;

      for (i = 0; i < 3; i++) {
        g_value_init (value, try_types[i]);
        ret = gst_value_deserialize (value, value_s);
        if (ret)
          break;
        g_value_unset (value);
      }
    } else {
      g_value_init (value, type);

      ret = gst_value_deserialize (value, value_s);
    }
    *value_end = c;
  }

  *after = s;

  return ret;
}

/**
 * gst_structure_from_string:
 * @string: a string representation of a #GstStructure.
 * @end: FIXME, deduce from code
 *
 * Creates a #GstStructure from a string representation.
 * 
 * Returns: a new #GstStructure
 */
GstStructure *
gst_structure_from_string (const gchar * string, gchar ** end)
{
  char *name;
  char *copy;
  char *w;
  char *r;
  char save;
  GstStructure *structure = NULL;
  GstStructureField field = { 0 };

  g_return_val_if_fail (string != NULL, NULL);

  copy = g_strdup (string);
  r = copy;

  name = r;
  if (!gst_structure_parse_string (r, &w, &r))
    goto error;

  while (g_ascii_isspace (*r))
    r++;
  if (*r != 0 && *r != ';' && *r != ',')
    goto error;

  save = *w;
  *w = 0;
  structure = gst_structure_empty_new (name);
  *w = save;

  while (*r && (*r != ';')) {
    if (*r != ',')
      goto error;
    r++;
    while (*r && g_ascii_isspace (*r))
      r++;

    memset (&field, 0, sizeof (field));
    if (!gst_structure_parse_field (r, &r, &field))
      goto error;
    gst_structure_set_field (structure, &field);
    while (*r && g_ascii_isspace (*r))
      r++;
  }

  if (end)
    *end = (char *) string + (r - copy);

  g_free (copy);
  return structure;

error:
  if (structure)
    gst_structure_free (structure);
  g_free (copy);
  return NULL;
}

static void
gst_structure_transform_to_string (const GValue * src_value,
    GValue * dest_value)
{
  g_return_if_fail (src_value != NULL);
  g_return_if_fail (dest_value != NULL);

  dest_value->data[0].v_pointer =
      gst_structure_to_string (src_value->data[0].v_pointer);
}

static GstStructure *
gst_structure_copy_conditional (const GstStructure * structure)
{
  if (structure)
    return gst_structure_copy (structure);
  return NULL;
}
