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

/**
 * SECTION:gststructure
 * @short_description: Generic structure containing fields of names and values
 * @see_also: #GstCaps, #GstMessage, #GstEvent, #GstQuery
 *
 * A #GstStructure is a collection of key/value pairs. The keys are expressed as
 * GQuarks and the values can be of any GType.
 *
 * In addition to the key/value pairs, a #GstStructure also has a name.
 * 
 * #GstStructure is used by various GStreamer subsystems to store information in
 * a flexible an extensible way. A #GstStructure does not have a refcount because it
 * usually is part of a higher level object such as #GstCaps. It provides a means to 
 * enforce mutability using the refcount of the parent with the 
 * gst_structure_set_parent_refcount() method.
 *
 * A #GstStructure can be created with gst_structure_empty_new() or gst_structure_new(),
 * which both take a name and an optional set of key/value pairs along with the types
 * of the values.
 * 
 * Field values can be changed with gst_structure_set_value() or gst_structure_set().
 *
 * Field values can be retrieved with gst_structure_get_value() or the more convenient
 * gst_structure_get_*() functions.
 *
 * Fields can be removed with gst_structure_remove_field() or gst_structure_remove_fields().
 *
 * Last reviewed on 2005-11-09 (0.9.4)
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

#define IS_MUTABLE(structure) \
    (!(structure)->parent_refcount || \
     g_atomic_int_get ((structure)->parent_refcount) == 1)

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
  static GType gst_structure_type = 0;

  if (!gst_structure_type) {
    gst_structure_type = g_boxed_type_register_static ("GstStructure",
        (GBoxedCopyFunc) gst_structure_copy_conditional,
        (GBoxedFreeFunc) gst_structure_free);

    g_value_register_transform_func (gst_structure_type, G_TYPE_STRING,
        gst_structure_transform_to_string);
  }

  return gst_structure_type;
}

static GstStructure *
gst_structure_id_empty_new_with_size (GQuark quark, guint prealloc)
{
  GstStructure *structure;

  structure = g_new0 (GstStructure, 1);
  structure->type = gst_structure_get_type ();
  structure->name = quark;
  structure->fields =
      g_array_sized_new (FALSE, TRUE, sizeof (GstStructureField), prealloc);

  return structure;
}

/**
 * gst_structure_id_empty_new:
 * @quark: name of new structure
 *
 * Creates a new, empty #GstStructure with the given name as a GQuark.
 *
 * Returns: a new, empty #GstStructure
 */
GstStructure *
gst_structure_id_empty_new (GQuark quark)
{
  g_return_val_if_fail (quark != 0, NULL);

  return gst_structure_id_empty_new_with_size (quark, 0);
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
  g_return_val_if_fail (name != NULL, NULL);

  return gst_structure_id_empty_new_with_size (g_quark_from_string (name), 0);
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
 * gst_structure_set_parent_refcount:
 * @structure: a #GstStructure
 * @refcount: a pointer to the parent's refcount
 *
 * Sets the parent_refcount field of #GstStructure. This field is used to
 * determine whether a structure is mutable or not. This function should only be
 * called by code implementing parent objects of #GstStructure, as described in
 * the MT Refcounting section of the design documents.
 */
void
gst_structure_set_parent_refcount (GstStructure * structure, int *refcount)
{
  g_return_if_fail (structure != NULL);

  /* if we have a parent_refcount already, we can only clear
   * if with a NULL refcount */
  if (structure->parent_refcount)
    g_return_if_fail (refcount == NULL);
  else
    g_return_if_fail (refcount != NULL);

  structure->parent_refcount = refcount;
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
  guint i;

  g_return_val_if_fail (structure != NULL, NULL);

  new_structure =
      gst_structure_id_empty_new_with_size (structure->name,
      structure->fields->len);

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
 * Frees a #GstStructure and all its fields and values. The structure must not
 * have a parent when this function is called.
 */
void
gst_structure_free (GstStructure * structure)
{
  GstStructureField *field;
  guint i;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (structure->parent_refcount == NULL);

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
 * Get the name of @structure as a string.
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
 * gst_structure_has_name:
 * @structure: a #GstStructure
 * @name: structure name to check for
 *
 * Checks if the structure has the given name
 *
 * Returns: TRUE if @name matches the name of the structure.
 */
gboolean
gst_structure_has_name (const GstStructure * structure, const gchar * name)
{
  const gchar *structure_name;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  structure_name = g_quark_to_string (structure->name);

  return (structure_name && strcmp (structure_name, name) == 0);
}

/**
 * gst_structure_get_name_id:
 * @structure: a #GstStructure
 *
 * Get the name of @structure as a GQuark.
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
  g_return_if_fail (IS_MUTABLE (structure));

  structure->name = g_quark_from_string (name);
}

/**
 * gst_structure_id_set_value:
 * @structure: a #GstStructure
 * @field: a #GQuark representing a field
 * @value: the new value of the field
 *
 * Sets the field with the given GQuark @field to @value.  If the field
 * does not exist, it is created.  If the field exists, the previous
 * value is replaced and freed.
 */
void
gst_structure_id_set_value (GstStructure * structure,
    GQuark field, const GValue * value)
{
  GstStructureField gsfield = { 0, {0,} };

  g_return_if_fail (structure != NULL);
  g_return_if_fail (G_IS_VALUE (value));
  g_return_if_fail (IS_MUTABLE (structure));

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
 * Sets the field with the given name @field to @value.  If the field
 * does not exist, it is created.  If the field exists, the previous
 * value is replaced and freed.
 */
void
gst_structure_set_value (GstStructure * structure,
    const gchar * fieldname, const GValue * value)
{
  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  g_return_if_fail (G_IS_VALUE (value));
  g_return_if_fail (IS_MUTABLE (structure));

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
 * (as a GType), value(s).  The last variable argument should be NULL.
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
  gchar *err = NULL;
  GType type;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (IS_MUTABLE (structure));

  while (fieldname) {
    GstStructureField field = { 0 };

    field.name = g_quark_from_string (fieldname);

    type = va_arg (varargs, GType);

#if GLIB_CHECK_VERSION(2,8,0)
    if (type == G_TYPE_DATE) {
      g_warning ("Don't use G_TYPE_DATE, use GST_TYPE_DATE instead\n");
      type = GST_TYPE_DATE;
    }
#endif

    g_value_init (&field.value, type);
    G_VALUE_COLLECT (&field.value, varargs, 0, &err);
    if (err) {
      g_critical ("%s", err);
      return;
    }
    gst_structure_set_field (structure, &field);

    fieldname = va_arg (varargs, gchar *);
  }
}

/* If the structure currently contains a field with the same name, it is
 * replaced with the provided field. Otherwise, the field is added to the
 * structure. The field's value is not deeply copied.
 */
static void
gst_structure_set_field (GstStructure * structure, GstStructureField * field)
{
  GstStructureField *f;
  guint i;

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

/* If there is no field with the given ID, NULL is returned.
 */
static GstStructureField *
gst_structure_id_get_field (const GstStructure * structure, GQuark field_id)
{
  GstStructureField *field;
  guint i;

  g_return_val_if_fail (structure != NULL, NULL);

  for (i = 0; i < structure->fields->len; i++) {
    field = GST_STRUCTURE_FIELD (structure, i);

    if (field->name == field_id)
      return field;
  }

  return NULL;
}

/* If there is no field with the given ID, NULL is returned.
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
 * Get the value of the field with name @fieldname.
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
 * Get the value of the field with GQuark @field.
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
  guint i;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  g_return_if_fail (IS_MUTABLE (structure));

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
 * Removes the fields with the given names. If a field does not exist, the
 * argument is ignored.
 */
void
gst_structure_remove_fields (GstStructure * structure,
    const gchar * fieldname, ...)
{
  va_list varargs;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  /* mutability checked in remove_field */

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
 * va_list form of #gst_structure_remove_fields.
 */
void
gst_structure_remove_fields_valist (GstStructure * structure,
    const gchar * fieldname, va_list varargs)
{
  gchar *field = (gchar *) fieldname;

  g_return_if_fail (structure != NULL);
  g_return_if_fail (fieldname != NULL);
  /* mutability checked in remove_field */

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
  g_return_if_fail (IS_MUTABLE (structure));

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
 * Get the number of fields in the structure.
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
 * gst_structure_nth_field_name:
 * @structure: a #GstStructure
 * @index: the index to get the name of
 *
 * Get the name of the given field number, counting from 0 onwards.
 *
 * Returns: the name of the given field number
 */
const gchar *
gst_structure_nth_field_name (const GstStructure * structure, guint index)
{
  GstStructureField *field;

  field = GST_STRUCTURE_FIELD (structure, index);
  return g_quark_to_string (field->name);
}

/**
 * gst_structure_foreach:
 * @structure: a #GstStructure
 * @func: a function to call for each field
 * @user_data: private data
 *
 * Calls the provided function once for each field in the #GstStructure. The
 * function must not modify the fields. Also see gst_structure_map_in_place().
 *
 * Returns: TRUE if the supplied function returns TRUE For each of the fields,
 * FALSE otherwise.
 */
gboolean
gst_structure_foreach (const GstStructure * structure,
    GstStructureForeachFunc func, gpointer user_data)
{
  guint i;
  GstStructureField *field;
  gboolean ret;

  g_return_val_if_fail (structure != NULL, FALSE);

  for (i = 0; i < structure->fields->len; i++) {
    field = GST_STRUCTURE_FIELD (structure, i);

    ret = func (field->name, &field->value, user_data);
    if (!ret)
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_structure_map_in_place:
 * @structure: a #GstStructure
 * @func: a function to call for each field
 * @user_data: private data
 *
 * Calls the provided function once for each field in the #GstStructure. In
 * contrast to gst_structure_foreach(), the function may modify the fields. The
 * structure must be mutable.
 *
 * Returns: TRUE if the supplied function returns TRUE For each of the fields,
 * FALSE otherwise.
 */
gboolean
gst_structure_map_in_place (GstStructure * structure,
    GstStructureMapFunc func, gpointer user_data)
{
  guint i;
  GstStructureField *field;
  gboolean ret;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (IS_MUTABLE (structure), FALSE);

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
 * Check if @structure contains a field named @fieldname.
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
 * Check if @structure contains a field named @fieldname and with GType @type.
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
 * Returns: TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a boolean, this function
 * returns FALSE.
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
 * Returns: TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain an int, this function
 * returns FALSE.
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
 * Returns: TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a fourcc, this function
 * returns FALSE.
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
 * gst_structure_get_date:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @value: a pointer to a #GDate to set
 *
 * Sets the date pointed to by @value corresponding to the date of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a data, this function
 * returns FALSE.
 */
gboolean
gst_structure_get_date (const GstStructure * structure, const gchar * fieldname,
    GDate ** value)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!GST_VALUE_HOLDS_DATE (&field->value))
    return FALSE;

  *value = g_value_dup_boxed (&field->value);

  return TRUE;
}

/**
 * gst_structure_get_clock_time:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @value: a pointer to a #GstClockTime to set
 *
 * Sets the clock time pointed to by @value corresponding to the clock time
 * of the given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a #GstClockTime, this 
 * function returns FALSE.
 */
gboolean
gst_structure_get_clock_time (const GstStructure * structure,
    const gchar * fieldname, GstClockTime * value)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!G_VALUE_HOLDS_UINT64 (&field->value))
    return FALSE;

  *value = g_value_get_uint64 (&field->value);

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
 * Returns: TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a double, this 
 * function returns FALSE.
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
 * Returns: a pointer to the string or NULL when the field did not exist
 * or did not contain a string.
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
    return NULL;
  if (!G_VALUE_HOLDS_STRING (&field->value))
    return NULL;

  return g_value_get_string (&field->value);
}

/**
 * gst_structure_get_enum:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @enumtype: the enum type of a field
 * @value: a pointer to an int to set
 *
 * Sets the int pointed to by @value corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists,
 * has the correct type and that the enumtype is correct.
 *
 * Returns: TRUE if the value could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain an enum of the given
 * type, this function returns FALSE.
 */
gboolean
gst_structure_get_enum (const GstStructure * structure,
    const gchar * fieldname, GType enumtype, gint * value)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (enumtype != G_TYPE_INVALID, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!G_VALUE_HOLDS_ENUM (&field->value))
    return FALSE;
  if (!G_TYPE_CHECK_VALUE_TYPE (&field->value, enumtype))
    return FALSE;

  *value = g_value_get_enum (&field->value);

  return TRUE;
}

/**
 * gst_structure_get_fraction:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @value_numerator: a pointer to an int to set
 * @value_denominator: a pointer to an int to set
 *
 * Sets the integers pointed to by @value_numerator and @value_denominator 
 * corresponding to the value of the given field.  Caller is responsible 
 * for making sure the field exists and has the correct type.
 *
 * Returns: TRUE if the values could be set correctly. If there was no field
 * with @fieldname or the existing field did not contain a GstFraction, this 
 * function returns FALSE.
 */
gboolean
gst_structure_get_fraction (const GstStructure * structure,
    const gchar * fieldname, gint * value_numerator, gint * value_denominator)
{
  GstStructureField *field;

  g_return_val_if_fail (structure != NULL, FALSE);
  g_return_val_if_fail (fieldname != NULL, FALSE);
  g_return_val_if_fail (value_numerator != NULL, FALSE);
  g_return_val_if_fail (value_denominator != NULL, FALSE);

  field = gst_structure_get_field (structure, fieldname);

  if (field == NULL)
    return FALSE;
  if (!GST_VALUE_HOLDS_FRACTION (&field->value))
    return FALSE;

  *value_numerator = gst_value_get_fraction_numerator (&field->value);
  *value_denominator = gst_value_get_fraction_denominator (&field->value);

  return TRUE;
}

typedef struct _GstStructureAbbreviation
{
  char *type_name;
  GType type;
}
GstStructureAbbreviation;

static GstStructureAbbreviation *
gst_structure_get_abbrs (gint * n_abbrs)
{
  static GstStructureAbbreviation *abbrs = NULL;
  static gint num = 0;

  if (abbrs == NULL) {
    /* dynamically generate the array */
    GstStructureAbbreviation dyn_abbrs[] = {
      {"int", G_TYPE_INT}
      ,
      {"i", G_TYPE_INT}
      ,
      {"float", G_TYPE_FLOAT}
      ,
      {"f", G_TYPE_FLOAT}
      ,
      {"double", G_TYPE_DOUBLE}
      ,
      {"d", G_TYPE_DOUBLE}
      ,
      {"buffer", GST_TYPE_BUFFER}
      ,
      {"fourcc", GST_TYPE_FOURCC}
      ,
      {"4", GST_TYPE_FOURCC}
      ,
      {"fraction", GST_TYPE_FRACTION}
      ,
      {"boolean", G_TYPE_BOOLEAN}
      ,
      {"bool", G_TYPE_BOOLEAN}
      ,
      {"b", G_TYPE_BOOLEAN}
      ,
      {"string", G_TYPE_STRING}
      ,
      {"str", G_TYPE_STRING}
      ,
      {"s", G_TYPE_STRING}
    };
    num = G_N_ELEMENTS (dyn_abbrs);
    /* permanently allocate and copy the array now */
    abbrs = g_new0 (GstStructureAbbreviation, num);
    memcpy (abbrs, dyn_abbrs, sizeof (GstStructureAbbreviation) * num);
  }
  *n_abbrs = num;

  return abbrs;
}

static GType
gst_structure_from_abbr (const char *type_name)
{
  int i;
  GstStructureAbbreviation *abbrs;
  gint n_abbrs;

  g_return_val_if_fail (type_name != NULL, G_TYPE_INVALID);

  abbrs = gst_structure_get_abbrs (&n_abbrs);

  for (i = 0; i < n_abbrs; i++) {
    if (strcmp (type_name, abbrs[i].type_name) == 0) {
      return abbrs[i].type;
    }
  }

  /* this is the fallback */
  return g_type_from_name (type_name);
}

static const char *
gst_structure_to_abbr (GType type)
{
  int i;
  GstStructureAbbreviation *abbrs;
  gint n_abbrs;

  g_return_val_if_fail (type != G_TYPE_INVALID, NULL);

  abbrs = gst_structure_get_abbrs (&n_abbrs);

  for (i = 0; i < n_abbrs; i++) {
    if (type == abbrs[i].type) {
      return abbrs[i].type_name;
    }
  }

  return g_type_name (type);
}

static GType
gst_structure_value_get_generic_type (GValue * val)
{
  if (G_VALUE_TYPE (val) == GST_TYPE_LIST
      || G_VALUE_TYPE (val) == GST_TYPE_ARRAY) {
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
  } else if (G_VALUE_TYPE (val) == GST_TYPE_FRACTION_RANGE) {
    return GST_TYPE_FRACTION;
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
 * Converts @structure to a human-readable string representation.
 *
 * Returns: a pointer to string allocated by g_malloc(). g_free() after
 * usage.
 */
gchar *
gst_structure_to_string (const GstStructure * structure)
{
  GstStructureField *field;
  GString *s;
  guint i;

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
        gst_structure_to_abbr (type), GST_STR_NULL (t));
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
    g_value_init (value, range_type);
    gst_value_set_double_range (value, g_value_get_double (&value1),
        g_value_get_double (&value2));
  } else if (G_VALUE_TYPE (&value1) == G_TYPE_INT) {
    range_type = GST_TYPE_INT_RANGE;
    g_value_init (value, range_type);
    gst_value_set_int_range (value, g_value_get_int (&value1),
        g_value_get_int (&value2));
  } else if (G_VALUE_TYPE (&value1) == GST_TYPE_FRACTION) {
    range_type = GST_TYPE_FRACTION_RANGE;
    g_value_init (value, range_type);
    gst_value_set_fraction_range (value, &value1, &value2);
  } else {
    return FALSE;
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
gst_structure_parse_array (gchar * s, gchar ** after, GValue * value,
    GType type)
{
  return gst_structure_parse_any_list (s, after, value, type,
      GST_TYPE_ARRAY, '<', '>');
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

  while (g_ascii_isspace (*s) || (s[0] == '\\' && g_ascii_isspace (s[1])))
    s++;
  name = s;
  if (!gst_structure_parse_simple_string (s, &name_end))
    return FALSE;

  s = name_end;
  while (g_ascii_isspace (*s) || (s[0] == '\\' && g_ascii_isspace (s[1])))
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
    ret = gst_structure_parse_array (s, &s, value, type);
  } else {
    value_s = s;
    if (!gst_structure_parse_string (s, &value_end, &s))
      return FALSE;

    c = *value_end;
    *value_end = 0;
    if (type == G_TYPE_INVALID) {
      GType try_types[] =
          { G_TYPE_INT, G_TYPE_DOUBLE, GST_TYPE_FRACTION, G_TYPE_STRING };
      int i;

      for (i = 0; i < 4; i++) {
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
 * @end: pointer to store the end of the string in.
 *
 * Creates a #GstStructure from a string representation.
 * If end is not NULL, a pointer to the place inside the given string
 * where parsing ended will be returned.
 *
 * Returns: a new #GstStructure or NULL when the string could not
 * be parsed. Free after usage.
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

  while (g_ascii_isspace (*r) || (r[0] == '\\' && g_ascii_isspace (r[1])))
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
    while (*r && (g_ascii_isspace (*r) || (r[0] == '\\'
                && g_ascii_isspace (r[1]))))
      r++;

    memset (&field, 0, sizeof (field));
    if (!gst_structure_parse_field (r, &r, &field))
      goto error;
    gst_structure_set_field (structure, &field);
    while (*r && (g_ascii_isspace (*r) || (r[0] == '\\'
                && g_ascii_isspace (r[1]))))
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

/* fixate utility functions */

/**
 * gst_structure_fixate_field_nearest_int:
 * @structure: a #GstStructure
 * @field_name: a field in @structure
 * @target: the target value of the fixation
 *
 * Fixates a #GstStructure by changing the given field to the nearest
 * integer to @target that is a subset of the existing field.
 *
 * Returns: TRUE if the structure could be fixated
 */
gboolean
gst_structure_fixate_field_nearest_int (GstStructure * structure,
    const char *field_name, int target)
{
  const GValue *value;

  g_return_val_if_fail (gst_structure_has_field (structure, field_name), FALSE);
  g_return_val_if_fail (IS_MUTABLE (structure), FALSE);

  value = gst_structure_get_value (structure, field_name);

  if (G_VALUE_TYPE (value) == G_TYPE_INT) {
    /* already fixed */
    return FALSE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_INT_RANGE) {
    int x;

    x = gst_value_get_int_range_min (value);
    if (target < x)
      target = x;
    x = gst_value_get_int_range_max (value);
    if (target > x)
      target = x;
    gst_structure_set (structure, field_name, G_TYPE_INT, target, NULL);
    return TRUE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    const GValue *list_value;
    int i, n;
    int best = 0;
    int best_index = -1;

    n = gst_value_list_get_size (value);
    for (i = 0; i < n; i++) {
      list_value = gst_value_list_get_value (value, i);
      if (G_VALUE_TYPE (list_value) == G_TYPE_INT) {
        int x = g_value_get_int (list_value);

        if (best_index == -1 || (ABS (target - x) < ABS (target - best))) {
          best_index = i;
          best = x;
        }
      }
    }
    if (best_index != -1) {
      gst_structure_set (structure, field_name, G_TYPE_INT, best, NULL);
      return TRUE;
    }
    return FALSE;
  }

  return FALSE;
}

/**
 * gst_structure_fixate_field_nearest_double:
 * @structure: a #GstStructure
 * @field_name: a field in @structure
 * @target: the target value of the fixation
 *
 * Fixates a #GstStructure by changing the given field to the nearest
 * double to @target that is a subset of the existing field.
 *
 * Returns: TRUE if the structure could be fixated
 */
gboolean
gst_structure_fixate_field_nearest_double (GstStructure * structure,
    const char *field_name, double target)
{
  const GValue *value;

  g_return_val_if_fail (gst_structure_has_field (structure, field_name), FALSE);
  g_return_val_if_fail (IS_MUTABLE (structure), FALSE);

  value = gst_structure_get_value (structure, field_name);

  if (G_VALUE_TYPE (value) == G_TYPE_DOUBLE) {
    /* already fixed */
    return FALSE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_DOUBLE_RANGE) {
    double x;

    x = gst_value_get_double_range_min (value);
    if (target < x)
      target = x;
    x = gst_value_get_double_range_max (value);
    if (target > x)
      target = x;
    gst_structure_set (structure, field_name, G_TYPE_DOUBLE, target, NULL);
    return TRUE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    const GValue *list_value;
    int i, n;
    double best = 0;
    int best_index = -1;

    n = gst_value_list_get_size (value);
    for (i = 0; i < n; i++) {
      list_value = gst_value_list_get_value (value, i);
      if (G_VALUE_TYPE (list_value) == G_TYPE_DOUBLE) {
        double x = g_value_get_double (list_value);

        if (best_index == -1 || (ABS (target - x) < ABS (target - best))) {
          best_index = i;
          best = x;
        }
      }
    }
    if (best_index != -1) {
      gst_structure_set (structure, field_name, G_TYPE_DOUBLE, best, NULL);
      return TRUE;
    }
    return FALSE;
  }

  return FALSE;

}

/**
 * gst_structure_fixate_field_boolean:
 * @structure: a #GstStructure
 * @field_name: a field in @structure
 * @target: the target value of the fixation
 *
 * Fixates a #GstStructure by changing the given @field_name field to the given
 * @target boolean if that field is not fixed yet.
 *
 * Returns: TRUE if the structure could be fixated
 */
gboolean
gst_structure_fixate_field_boolean (GstStructure * structure,
    const char *field_name, gboolean target)
{
  const GValue *value;

  g_return_val_if_fail (gst_structure_has_field (structure, field_name), FALSE);
  g_return_val_if_fail (IS_MUTABLE (structure), FALSE);

  value = gst_structure_get_value (structure, field_name);

  if (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN) {
    /* already fixed */
    return FALSE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    const GValue *list_value;
    int i, n;
    int best = 0;
    int best_index = -1;

    n = gst_value_list_get_size (value);
    for (i = 0; i < n; i++) {
      list_value = gst_value_list_get_value (value, i);
      if (G_VALUE_TYPE (list_value) == G_TYPE_BOOLEAN) {
        gboolean x = g_value_get_boolean (list_value);

        if (best_index == -1 || x == target) {
          best_index = i;
          best = x;
        }
      }
    }
    if (best_index != -1) {
      gst_structure_set (structure, field_name, G_TYPE_BOOLEAN, best, NULL);
      return TRUE;
    }
    return FALSE;
  }

  return FALSE;
}

/**
 * gst_structure_fixate_field_nearest_fraction:
 * @structure: a #GstStructure
 * @field_name: a field in @structure
 * @target_numerator: The numerator of the target value of the fixation
 * @target_denominator: The denominator of the target value of the fixation
 *
 * Fixates a #GstStructure by changing the given field to the nearest
 * fraction to @target_numerator/@target_denominator that is a subset 
 * of the existing field.
 *
 * Returns: TRUE if the structure could be fixated
 */
gboolean
gst_structure_fixate_field_nearest_fraction (GstStructure * structure,
    const char *field_name, const gint target_numerator,
    const gint target_denominator)
{
  const GValue *value;

  g_return_val_if_fail (gst_structure_has_field (structure, field_name), FALSE);
  g_return_val_if_fail (IS_MUTABLE (structure), FALSE);

  value = gst_structure_get_value (structure, field_name);

  if (G_VALUE_TYPE (value) == GST_TYPE_FRACTION) {
    /* already fixed */
    return FALSE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_FRACTION_RANGE) {
    const GValue *x, *new_value;
    GValue target = { 0 };
    g_value_init (&target, GST_TYPE_FRACTION);
    gst_value_set_fraction (&target, target_numerator, target_denominator);

    new_value = &target;
    x = gst_value_get_fraction_range_min (value);
    if (gst_value_compare (&target, x) == GST_VALUE_LESS_THAN)
      new_value = x;
    x = gst_value_get_fraction_range_max (value);
    if (gst_value_compare (&target, x) == GST_VALUE_GREATER_THAN)
      new_value = x;

    gst_structure_set_value (structure, field_name, new_value);
    g_value_unset (&target);
    return TRUE;
  } else if (G_VALUE_TYPE (value) == GST_TYPE_LIST) {
    const GValue *list_value;
    int i, n;
    const GValue *best = NULL;
    GValue best_diff = { 0 };
    GValue cur_diff = { 0 };
    GValue target = { 0 };
    gboolean res = FALSE;

    g_value_init (&best_diff, GST_TYPE_FRACTION);
    g_value_init (&cur_diff, GST_TYPE_FRACTION);
    g_value_init (&target, GST_TYPE_FRACTION);

    gst_value_set_fraction (&target, target_numerator, target_denominator);

    n = gst_value_list_get_size (value);
    for (i = 0; i < n; i++) {
      list_value = gst_value_list_get_value (value, i);
      if (G_VALUE_TYPE (list_value) == GST_TYPE_FRACTION) {
        if (best == NULL) {
          best = list_value;
          gst_value_set_fraction (&best_diff, 0, 1);
        } else {
          if (gst_value_compare (list_value, &target) == GST_VALUE_LESS_THAN)
            gst_value_fraction_subtract (&cur_diff, &target, list_value);
          else
            gst_value_fraction_subtract (&cur_diff, list_value, &target);

          if (gst_value_compare (&cur_diff, &best_diff) == GST_VALUE_LESS_THAN) {
            best = list_value;
            g_value_copy (&cur_diff, &best_diff);
          }
        }
      }
    }
    if (best != NULL) {
      gst_structure_set_value (structure, field_name, best);
      res = TRUE;
    }
    g_value_unset (&best_diff);
    g_value_unset (&cur_diff);
    g_value_unset (&target);
    return res;
  }

  return FALSE;
}
