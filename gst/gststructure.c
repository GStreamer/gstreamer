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

#include <gst/gst.h>
#include <gobject/gvaluecollector.h>

//#define G_TYPE_FOURCC G_TYPE_FLOAT

static GType _gst_structure_type;

static void _gst_structure_transform_to_string(const GValue *src_value,
    GValue *dest_value);

static void _gst_structure_value_init (GValue *value);
static void _gst_structure_value_free (GValue *value);
static void _gst_structure_value_copy (const GValue *src, GValue *dest);
static gpointer _gst_structure_value_peek_pointer (const GValue *value);


GType gst_structure_get_type(void)
{
  return _gst_structure_type;
}

void _gst_structure_initialize(void)
{
  static const GTypeValueTable type_value_table = {
    _gst_structure_value_init,
    _gst_structure_value_free,
    _gst_structure_value_copy,
    _gst_structure_value_peek_pointer,
    NULL,
    NULL,
    NULL,
    NULL,
  };
  static const GTypeInfo structure_info = {
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0, /* sizeof(GstStructure), */
    0,
    NULL, /* _gst_structure_init, */
    &type_value_table,
  };

  _gst_structure_type = g_type_register_static(G_TYPE_BOXED, "GstStructure",
      &structure_info, 0);
#if 0
  _gst_structure_type = g_boxed_type_register_static("GstStructure",
      (GBoxedCopyFunc) gst_structure_copy,
      (GBoxedFreeFunc) gst_structure_free);
#endif

  g_value_register_transform_func(_gst_structure_type, G_TYPE_STRING,
      _gst_structure_transform_to_string);
}

/**
 * gst_structure_id_empty_new:
 * @name: name of new structure
 *
 * Creates a new, empty #GstStructure with the given name.
 *
 * Returns: a new, empty #GstStructure
 */
GstStructure *gst_structure_id_empty_new(GQuark quark)
{
  GstStructure *structure;

  g_return_val_if_fail(quark != 0, NULL);

  structure = g_new0(GstStructure, 1);
  structure->name = quark;
  structure->fields = g_array_new(FALSE,TRUE,sizeof(GstStructureField));

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
GstStructure *gst_structure_empty_new(const gchar *name)
{
  GstStructure *structure;

  g_return_val_if_fail(name != NULL, NULL);

  structure = g_new0(GstStructure, 1);
  structure->name = g_quark_from_string(name);
  structure->fields = g_array_new(FALSE,TRUE,sizeof(GstStructureField));

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
GstStructure *gst_structure_new(const gchar *name,
    const gchar *firstfield, ...)
{
  GstStructure *structure;
  va_list varargs;

  g_return_val_if_fail(name != NULL, NULL);

  va_start(varargs, firstfield);

  structure = gst_structure_new_valist(name,firstfield,varargs);

  va_end(varargs);

  return structure;
}

/**
 * gst_structure_new_valist:
 * @name: name of new structure
 * @firstfield: name of first field to set
 * @varags: variable argument list
 *
 * Creates a new #GstStructure with the given name.  Structure fields
 * are set according to the varargs in a manner similar to
 * @gst_structure_new.
 *
 * Returns: a new #GstStructure
 */
GstStructure *gst_structure_new_valist(const gchar *name,
    const gchar *firstfield, va_list varargs)
{
  GstStructure *structure;

  g_return_val_if_fail(name != NULL, NULL);

  structure = gst_structure_empty_new(name);
  gst_structure_set_valist(structure, firstfield, varargs);

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
GstStructure *gst_structure_copy(GstStructure *structure)
{
  GstStructure *new_structure;
  GstStructureField *field;
  int i;

  g_return_val_if_fail(structure != NULL, NULL);

  new_structure = gst_structure_empty_new(g_quark_to_string(structure->name));
  new_structure->fields = g_array_set_size(new_structure->fields,
      structure->fields->len);
  new_structure->name = structure->name;

  for(i=0;i<structure->fields->len;i++){
    GstStructureField new_field = { 0 };

    field = GST_STRUCTURE_FIELD(structure, i);

    new_field.name = field->name;
    g_value_init(&new_field.value, G_VALUE_TYPE(&field->value));
    g_value_copy(&field->value, &new_field.value);

    g_array_append_val(new_structure->fields, new_field);
  }

  return structure;
}

/**
 * gst_structure_free: 
 * @structure: the #GstStructure to free
 *
 * Frees a #GstStructure and all its fields and values.
 */
void gst_structure_free(GstStructure *structure)
{
  GstStructureField *field;
  int i;

  return;

  g_return_if_fail(structure != NULL);

  for(i=0;i<structure->fields->len;i++){
    field = GST_STRUCTURE_FIELD(structure, i);

    if(G_IS_VALUE(&field->value)){
      g_value_unset(&field->value);
    }
  }
  g_free(structure);
}

/**
 * gst_structure_get_name:
 * @structure: a #GstStructure
 *
 * Accessor fuction.
 *
 * Returns: the name of the structure.
 */
const gchar *gst_structure_get_name(GstStructure *structure)
{
  g_return_val_if_fail(structure != NULL, NULL);

  return g_quark_to_string(structure->name);
}

/**
 * gst_structure_set_name:
 * @structure: a #GstStructure
 * @name: the new name of the structure
 *
 * Sets the name of the structure to the given name.  The string
 * provided is copied before being used.
 */
void gst_structure_set_name(GstStructure *structure, const gchar *name)
{
  g_return_if_fail(structure != NULL);
  g_return_if_fail(name != NULL);

  structure->name = g_quark_from_string(name);
}

/**
 * gst_structure_id_set_value:
 * @structure: a #GstStructure
 * @field_id: a #GQuark representing a field
 * @value: the new value of the field
 *
 * Sets the field with the given ID to the provided value.  If the field
 * does not exist, it is created.  If the field exists, the previous
 * value is freed.
 */
void gst_structure_id_set_value(GstStructure *structure, GQuark fieldname,
    const GValue *value)
{
  GstStructureField field = { 0, { 0, } };

  g_return_if_fail(structure != NULL);
  g_return_if_fail(G_IS_VALUE(value));

  field.name = fieldname;
  g_value_init(&field.value, G_VALUE_TYPE (value));
  g_value_copy(value, &field.value);

  gst_structure_set_field(structure, &field);
}

/**
 * gst_structure_set_value:
 * @structure: a #GstStructure
 * @field: the name of the field to set
 * @value: the new value of the field
 *
 * Sets the field with the given name to the provided value.  If the field
 * does not exist, it is created.  If the field exists, the previous
 * value is freed.
 */
void gst_structure_set_value(GstStructure *structure, const gchar *field,
    const GValue *value)
{
  g_return_if_fail(structure != NULL);
  g_return_if_fail(field != NULL);
  g_return_if_fail(G_IS_VALUE(value));

  gst_structure_id_set_value(structure, g_quark_from_string(field), value);
}

/**
 * gst_structure_set:
 * @structure: a #GstStructure
 * @field: the name of the field to set
 * @...: variable arguments
 *
 * Parses the variable arguments and sets fields accordingly.
 * Variable arguments should be in the form field name, field type
 * (as a GType), value.  The last variable argument should be NULL.
 */
void gst_structure_set(GstStructure *structure, const gchar *field, ...)
{
  va_list varargs;

  g_return_if_fail(structure != NULL);

  va_start(varargs, field);

  gst_structure_set_valist(structure,field,varargs);

  va_end(varargs);
}

/**
 * gst_structure_set:
 * @structure: a #GstStructure
 * @field: the name of the field to set
 * @varargs: variable arguments
 *
 * va_list form of #gst_structure_set.
 */
void gst_structure_set_valist(GstStructure *structure, const gchar *fieldname,
    va_list varargs)
{
  GType type;
  int i;
  double d;
  char *s;

  g_return_if_fail(structure != NULL);

  while(fieldname){
    GstStructureField field = { 0 };

    field.name = g_quark_from_string(fieldname);

    type = va_arg (varargs, GType); 

    switch(type){
      case G_TYPE_INT:
	i = va_arg(varargs, int);
        g_value_init(&field.value, G_TYPE_INT);
        g_value_set_int(&field.value, i);
	break;
      case G_TYPE_DOUBLE:
	d = va_arg(varargs, double);
        g_value_init(&field.value, G_TYPE_DOUBLE);
        g_value_set_double(&field.value, d);
	break;
      case G_TYPE_BOOLEAN:
	i = va_arg(varargs, int);
        g_value_init(&field.value, G_TYPE_BOOLEAN);
        g_value_set_boolean(&field.value, i);
	break;
      case G_TYPE_STRING:
	s = va_arg(varargs, char *);
        g_value_init(&field.value, G_TYPE_STRING);
        g_value_set_string(&field.value, s);
	break;
      default:
	if(type == GST_TYPE_FOURCC){
	  i = va_arg(varargs, int);
	  g_value_init(&field.value, GST_TYPE_FOURCC);
	  gst_value_set_fourcc(&field.value, i);
	  break;
	}else{
	  g_critical("unimplemented vararg field type %d\n", (int)type);
	}
	break;
    }

    gst_structure_set_field(structure, &field);

    fieldname = va_arg (varargs, gchar *);
  }
}

/**
 * gst_structure_set_field_copy:
 * @structure: a #GstStructure
 * @field: the #GstStructureField to set
 *
 * Sets a field in the structure.  If the structure currently contains
 * a field with the same name, it is replaced with the provided field.
 * Otherwise, the field is added to the structure.  The field's value
 * is deeply copied.
 *
 * This function is intended mainly for internal use.  The function
 * #gst_structure_set() is recommended instead of this one.
 */
void gst_structure_set_field_copy (GstStructure *structure,
    const GstStructureField *field)
{
  GstStructureField f = { 0 };
  GType type = G_VALUE_TYPE (&field->value);

  f.name = field->name;
  g_value_init (&f.value, type);
  g_value_copy (&field->value, &f.value);

  gst_structure_set_field (structure, &f);
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
void gst_structure_set_field(GstStructure *structure, GstStructureField *field)
{
  GstStructureField *f;
  int i;

  for(i=0;i<structure->fields->len;i++){
    f = GST_STRUCTURE_FIELD(structure, i);

    if(f->name == field->name){
      g_value_unset(&f->value);
      memcpy(f,field,sizeof(GstStructureField));
      return;
    }
  }

  g_array_append_val(structure->fields, *field);
}

/**
 * gst_structure_id_get_field:
 * @structure: a #GstStructure
 * @field_id: the GQuark of the field to get
 *
 * Gets the specified field from the structure.  If there is no
 * field with the given ID, NULL is returned.
 *
 * Returns: the #GstStructureField with the given ID
 */
GstStructureField *gst_structure_id_get_field(const GstStructure *structure,
    GQuark field_id)
{
  GstStructureField *field;
  int i;

  g_return_val_if_fail(structure != NULL, NULL);

  for(i=0;i<structure->fields->len;i++){
    field = GST_STRUCTURE_FIELD(structure, i);

    if(field->name == field_id) return field;
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
GstStructureField *
gst_structure_get_field(const GstStructure *structure, const gchar *fieldname)
{
  g_return_val_if_fail(structure != NULL, NULL);
  g_return_val_if_fail(fieldname != NULL, NULL);

  return gst_structure_id_get_field(structure,
      g_quark_from_string(fieldname));
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
gst_structure_get_value(const GstStructure *structure, const gchar *fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, NULL);
  g_return_val_if_fail(fieldname != NULL, NULL);

  field = gst_structure_get_field(structure, fieldname);
  if(field == NULL) return NULL;

  return &field->value;
}

#if 0
void gst_structure_get(GstStructure *structure, const gchar *fieldname, ...)
{

}
#endif

/**
 * gst_structure_remove_field:
 * @structure: a #GstStructure
 * @fieldname: the name of the field to remove
 *
 * Removes the field with the given name.  If the field with the given
 * name does not exist, the structure is unchanged.
 */
void
gst_structure_remove_field(GstStructure *structure, const gchar *fieldname)
{
  GstStructureField *field;
  GQuark id;
  int i;

  g_return_if_fail(structure != NULL);
  g_return_if_fail(fieldname != NULL);

  id = g_quark_from_string(fieldname);

  for(i=0;i<structure->fields->len;i++){
    field = GST_STRUCTURE_FIELD(structure, i);

    if(field->name == id){
      if(G_IS_VALUE(&field->value)){
        g_value_unset(&field->value);
      }
      structure->fields = g_array_remove_index(structure->fields, i);
      return;
    }
  }
}

/**
 * gst_structure_remove_all_fields:
 * @structure: a #GstStructure
 *
 * Removes all fields in a GstStructure. 
 */
void
gst_structure_remove_all_fields(GstStructure *structure)
{
  GstStructureField *field;
  int i;

  g_return_if_fail(structure != NULL);

  for (i = structure->fields->len - 1; i >= 0; i-- ) {
    field = GST_STRUCTURE_FIELD(structure, i);

    if (G_IS_VALUE (&field->value)) {
      g_value_unset(&field->value);
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
 * value it contains.  If the field is not found, G_TYPE_NONE is
 * returned.
 *
 * Returns: the #GValue of the field
 */
GType
gst_structure_get_field_type(const GstStructure *structure, const gchar *fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, G_TYPE_NONE);
  g_return_val_if_fail(fieldname != NULL, G_TYPE_NONE);

  field = gst_structure_get_field(structure, fieldname);
  if(field == NULL) return G_TYPE_NONE;

  return G_VALUE_TYPE(&field->value);
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
gst_structure_n_fields(const GstStructure *structure)
{
  g_return_val_if_fail(structure != NULL, 0);

  return structure->fields->len;
}

/**
 * gst_structure_field_foreach:
 * @structure: a #GstStructure
 * @func: a function to call for each field
 * @user_data: private data
 *
 * Calls the provided function once for each field in the #GstStructure.
 */
void
gst_structure_field_foreach (GstStructure *structure,
    GstStructureForeachFunc func, gpointer user_data)
{
  int i;
  GstStructureField *field;

  for(i=0;i<structure->fields->len;i++){
    field = GST_STRUCTURE_FIELD(structure, i);

    func (structure, field->name, &field->value, user_data);
  }
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
gst_structure_has_field(const GstStructure *structure, const gchar *fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, 0);
  g_return_val_if_fail(fieldname != NULL, 0);

  field = gst_structure_get_field(structure, fieldname);

  return (field != NULL);
}

/**
 * gst_structure_has_field:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @type: the type of a value
 *
 * Accessor function.
 *
 * Returns: TRUE if the structure contains a field with the given name and type
 */
gboolean
gst_structure_has_field_typed(const GstStructure *structure, const gchar *fieldname,
    GType type)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, 0);
  g_return_val_if_fail(fieldname != NULL, 0);

  field = gst_structure_get_field(structure, fieldname);
  if(field == NULL) return FALSE;

  return (G_VALUE_TYPE(&field->value) == type);
}


/* utility functions */

/**
 * gst_structure_get_boolean:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @ptr: a pointer to a #gboolean to set
 *
 * Sets the boolean pointed to by @ptr corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean
gst_structure_get_boolean(const GstStructure *structure, const gchar *fieldname,
    gboolean *value)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, FALSE);
  g_return_val_if_fail(fieldname != NULL, FALSE);

  field = gst_structure_get_field(structure, fieldname);

  if(field == NULL) return FALSE;
  if(!G_VALUE_HOLDS_BOOLEAN(&field->value))return FALSE;

  *value = g_value_get_boolean(&field->value);

  return TRUE;
}

/**
 * gst_structure_get_int:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @ptr: a pointer to an int to set
 *
 * Sets the int pointed to by @ptr corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean
gst_structure_get_int(const GstStructure *structure, const gchar *fieldname,
    gint *value)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, FALSE);
  g_return_val_if_fail(fieldname != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  field = gst_structure_get_field(structure, fieldname);

  if(field == NULL) return FALSE;
  if(!G_VALUE_HOLDS_INT(&field->value))return FALSE;

  *value = g_value_get_int(&field->value);

  return TRUE;
}

/**
 * gst_structure_get_fourcc:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @ptr: a pointer to a #GstFourcc to set
 *
 * Sets the #GstFourcc pointed to by @ptr corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean
gst_structure_get_fourcc(const GstStructure *structure, const gchar *fieldname,
    guint32 *value)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, FALSE);
  g_return_val_if_fail(fieldname != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  field = gst_structure_get_field(structure, fieldname);

  if(field == NULL) return FALSE;
  if(!G_VALUE_HOLDS_UINT(&field->value))return FALSE;

  *value = g_value_get_uint(&field->value);

  return TRUE;
}

/**
 * gst_structure_get_double:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @ptr: a pointer to a #GstFourcc to set
 *
 * Sets the double pointed to by @ptr corresponding to the value of the
 * given field.  Caller is responsible for making sure the field exists
 * and has the correct type.
 *
 * Returns: TRUE if the value could be set correctly
 */
gboolean gst_structure_get_double(const GstStructure *structure,
    const gchar *fieldname, gdouble *value)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, FALSE);
  g_return_val_if_fail(fieldname != NULL, FALSE);
  g_return_val_if_fail(value != NULL, FALSE);

  field = gst_structure_get_field(structure, fieldname);

  if(field == NULL) return FALSE;
  if(!G_VALUE_HOLDS_DOUBLE(&field->value))return FALSE;

  *value = g_value_get_double(&field->value);

  return TRUE;
}

/**
 * gst_structure_get_string:
 * @structure: a #GstStructure
 * @fieldname: the name of a field
 * @ptr: a pointer to a #GstFourcc to set
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
gst_structure_get_string(const GstStructure *structure, const gchar *fieldname)
{
  GstStructureField *field;

  g_return_val_if_fail(structure != NULL, NULL);
  g_return_val_if_fail(fieldname != NULL, NULL);

  field = gst_structure_get_field(structure, fieldname);

  if(field == NULL) return FALSE;
  if(!G_VALUE_HOLDS_STRING(&field->value))return FALSE;

  return g_value_get_string(&field->value);
}

/**
 * gst_structure_to_string:
 * @structure: a #GstStructure
 *
 * Converts @structure to a human-readable representation.
 * 
 * Returns: a pointer to string allocated by g_malloc()
 */
gchar *
gst_structure_to_string(const GstStructure *structure)
{
  GstStructureField *field;
  GString *s;
  int i;

  g_return_val_if_fail(structure != NULL, NULL);

  s = g_string_new("");
  g_string_append_printf(s, "\"%s\"", g_quark_to_string(structure->name));
  for(i=0;i<structure->fields->len;i++){
    GValue s_val = { 0 };

    field = GST_STRUCTURE_FIELD(structure, i);

    g_value_init(&s_val, G_TYPE_STRING);

    g_value_transform(&field->value, &s_val);
    g_string_append_printf(s, ", %s:%s", g_quark_to_string(field->name),
	g_value_get_string(&s_val));
    g_value_unset(&s_val);
  }
  return g_string_free(s, FALSE);
}

/**
 * gst_structure_from_string:
 * @structure: a #GstStructure
 *
 * Creates a #GstStructure from a string representation.
 * 
 * Returns: a new #GstStructure
 */
GstStructure *
gst_structure_from_string (const gchar *string)
{
  /* FIXME */

  g_return_val_if_fail(string != NULL, NULL);

  g_assert_not_reached();

  return NULL;
}

static void
_gst_structure_transform_to_string(const GValue *src_value, GValue *dest_value)
{
  g_return_if_fail(src_value != NULL);
  g_return_if_fail(dest_value != NULL);

  dest_value->data[0].v_pointer =
    gst_structure_to_string (src_value->data[0].v_pointer);
}


static void _gst_structure_value_init (GValue *value)
{
  value->data[0].v_pointer = gst_structure_empty_new("");
}

static void _gst_structure_value_free (GValue *value)
{
  gst_structure_free(value->data[0].v_pointer);

}

static void _gst_structure_value_copy (const GValue *src, GValue *dest)
{
  dest->data[0].v_pointer = gst_structure_copy(src->data[0].v_pointer);
}

static gpointer _gst_structure_value_peek_pointer (const GValue *value)
{
  return value->data[0].v_pointer;
}

