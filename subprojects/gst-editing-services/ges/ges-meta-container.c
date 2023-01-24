/* GStreamer Editing Services
 * Copyright (C) 2012 Paul Lange <palango@gmx.de>
 * Copyright (C) <2014> Thibault Saunier <thibault.saunier@collabora.com>
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

#include <glib-object.h>
#include <gst/gst.h>

#include "ges-meta-container.h"
#include "ges-marker-list.h"

/**
 * SECTION: gesmetacontainer
 * @title: GESMetaContainer Interface
 * @short_description: An interface for storing metadata
 *
 * A #GObject that implements #GESMetaContainer can have metadata set on
 * it, that is data that is unimportant to its function within GES, but
 * may hold some useful information. In particular,
 * ges_meta_container_set_meta() can be used to store any #GValue under
 * any generic field (specified by a string key). The same method can also
 * be used to remove the field by passing %NULL. A number of convenience
 * methods are also provided to make it easier to set common value types.
 * The metadata can then be read with ges_meta_container_get_meta() and
 * similar convenience methods.
 *
 * ## Registered Fields
 *
 * By default, any #GValue can be set for a metadata field. However, you
 * can register some fields as static, that is they only allow values of a
 * specific type to be set under them, using
 * ges_meta_container_register_meta() or
 * ges_meta_container_register_static_meta(). The set #GESMetaFlag will
 * determine whether the value can be changed, but even if it can be
 * changed, it must be changed to a value of the same type.
 *
 * Internally, some GES objects will be initialized with static metadata
 * fields. These will correspond to some standard keys, such as
 * #GES_META_VOLUME.
 */

static GQuark ges_meta_key;

G_DEFINE_INTERFACE_WITH_CODE (GESMetaContainer, ges_meta_container,
    G_TYPE_OBJECT, ges_meta_key =
    g_quark_from_static_string ("ges-meta-container-data"););

enum
{
  NOTIFY_SIGNAL,
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

typedef struct RegisteredMeta
{
  GType item_type;
  GESMetaFlag flags;
} RegisteredMeta;

typedef struct ContainerData
{
  GstStructure *structure;
  GHashTable *static_items;
} ContainerData;

static void
ges_meta_container_default_init (GESMetaContainerInterface * iface)
{

  /**
   * GESMetaContainer::notify-meta:
   * @container: A #GESMetaContainer
   * @key: The key for the @container field that changed
   * @value: (nullable): The new value under @key
   *
   * This is emitted for a meta container whenever the metadata under one
   * of its fields changes, is set for the first time, or is removed. In
   * the latter case, @value will be %NULL.
   */
  _signals[NOTIFY_SIGNAL] =
      g_signal_new ("notify-meta", G_TYPE_FROM_INTERFACE (iface),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED |
      G_SIGNAL_NO_HOOKS, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_VALUE);
}

static void
_free_meta_container_data (ContainerData * data)
{
  gst_structure_free (data->structure);
  g_hash_table_unref (data->static_items);

  g_free (data);
}

static void
_free_static_item (RegisteredMeta * item)
{
  g_free (item);
}

static ContainerData *
_create_container_data (GESMetaContainer * container)
{
  ContainerData *data = g_new (ContainerData, 1);
  data->structure = gst_structure_new_empty ("metadatas");
  data->static_items = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) (GDestroyNotify) _free_static_item);
  g_object_set_qdata_full (G_OBJECT (container), ges_meta_key, data,
      (GDestroyNotify) _free_meta_container_data);

  return data;
}

static GstStructure *
_meta_container_get_structure (GESMetaContainer * container)
{
  ContainerData *data;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data)
    data = _create_container_data (container);

  return data->structure;
}

typedef struct
{
  GESMetaForeachFunc func;
  const GESMetaContainer *container;
  gpointer data;
} MetadataForeachData;

static gboolean
structure_foreach_wrapper (GQuark field_id, const GValue * value,
    gpointer user_data)
{
  MetadataForeachData *data = (MetadataForeachData *) user_data;

  data->func (data->container, g_quark_to_string (field_id), value, data->data);
  return TRUE;
}

static gboolean
_append_foreach (GQuark field_id, const GValue * value, GESMetaContainer * self)
{
  ges_meta_container_set_meta (self, g_quark_to_string (field_id), value);

  return TRUE;
}

/**
 * ges_meta_container_foreach:
 * @container: A #GESMetaContainer
 * @func: (scope call): A function to call on each of @container's set
 * metadata fields
 * @user_data: (closure): User data to send to @func
 *
 * Calls the given function on each of the meta container's set metadata
 * fields.
 */
void
ges_meta_container_foreach (GESMetaContainer * container,
    GESMetaForeachFunc func, gpointer user_data)
{
  GstStructure *structure;
  MetadataForeachData foreach_data;

  g_return_if_fail (GES_IS_META_CONTAINER (container));
  g_return_if_fail (func != NULL);

  structure = _meta_container_get_structure (container);

  foreach_data.func = func;
  foreach_data.container = container;
  foreach_data.data = user_data;

  gst_structure_foreach (structure,
      (GstStructureForeachFunc) structure_foreach_wrapper, &foreach_data);
}

static gboolean
_register_meta (GESMetaContainer * container, GESMetaFlag flags,
    const gchar * meta_item, GType type)
{
  ContainerData *data;
  RegisteredMeta *static_item;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data)
    data = _create_container_data (container);
  else if (g_hash_table_lookup (data->static_items, meta_item)) {
    GST_WARNING_OBJECT (container, "Static meta %s already registered",
        meta_item);

    return FALSE;
  }

  static_item = g_new0 (RegisteredMeta, 1);
  static_item->item_type = type;
  static_item->flags = flags;
  g_hash_table_insert (data->static_items, g_strdup (meta_item), static_item);

  return TRUE;
}

/* _can_write_value should have been checked before calling */
static gboolean
_set_value (GESMetaContainer * container, const gchar * meta_item,
    const GValue * value)
{
  GstStructure *structure;
  gchar *val = gst_value_serialize (value);

  if (val == NULL) {
    GST_WARNING_OBJECT (container, "Could not set value on item: %s",
        meta_item);

    g_free (val);
    return FALSE;
  }

  structure = _meta_container_get_structure (container);

  GST_DEBUG_OBJECT (container, "Setting meta_item %s value: %s::%s",
      meta_item, G_VALUE_TYPE_NAME (value), val);

  gst_structure_set_value (structure, meta_item, value);
  g_signal_emit (container, _signals[NOTIFY_SIGNAL], 0, meta_item, value);

  g_free (val);
  return TRUE;
}

static gboolean
_can_write_value (GESMetaContainer * container, const gchar * item_name,
    GType type)
{
  ContainerData *data;
  RegisteredMeta *static_item = NULL;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data) {
    _create_container_data (container);
    return TRUE;
  }

  static_item = g_hash_table_lookup (data->static_items, item_name);

  if (static_item == NULL)
    return TRUE;

  if ((static_item->flags & GES_META_WRITABLE) == FALSE) {
    GST_WARNING_OBJECT (container, "Can not write %s", item_name);
    return FALSE;
  }

  if (static_item->item_type != type) {
    GST_WARNING_OBJECT (container, "Can not set value of type %s on %s "
        "its type is: %s", g_type_name (static_item->item_type), item_name,
        g_type_name (type));
    return FALSE;
  }

  return TRUE;
}

#define CREATE_SETTER(name, value_ctype, value_gtype,  setter_name)     \
gboolean                                                                \
ges_meta_container_set_ ## name (GESMetaContainer *container,      \
                           const gchar *meta_item, value_ctype value)   \
{                                                                       \
  GValue gval = { 0 };                                                  \
  gboolean ret;                                                         \
                                                                        \
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);      \
  g_return_val_if_fail (meta_item != NULL, FALSE);                      \
                                                                        \
  if (_can_write_value (container, meta_item, value_gtype) == FALSE)    \
    return FALSE;                                                       \
                                                                        \
  g_value_init (&gval, value_gtype);                                    \
  g_value_set_ ##setter_name (&gval, value);                            \
                                                                        \
  ret = _set_value (container, meta_item, &gval);                       \
  g_value_unset (&gval);                                                \
  return ret;                                                           \
}

/**
 * ges_meta_container_set_boolean:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given boolean value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (boolean, gboolean, G_TYPE_BOOLEAN, boolean);

/**
 * ges_meta_container_set_int:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given int value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (int, gint, G_TYPE_INT, int);

/**
 * ges_meta_container_set_uint:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given uint value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (uint, guint, G_TYPE_UINT, uint);

/**
 * ges_meta_container_set_int64:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given int64 value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (int64, gint64, G_TYPE_INT64, int64);

/**
 * ges_meta_container_set_uint64:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given uint64 value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (uint64, guint64, G_TYPE_UINT64, uint64);

/**
 * ges_meta_container_set_float:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given float value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (float, float, G_TYPE_FLOAT, float);

/**
 * ges_meta_container_set_double:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given double value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (double, double, G_TYPE_DOUBLE, double);

/**
 * ges_meta_container_set_date:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given date value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (date, const GDate *, G_TYPE_DATE, boxed);

/**
 * ges_meta_container_set_date_time:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given date time value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (date_time, const GstDateTime *, GST_TYPE_DATE_TIME, boxed);

/**
 * ges_meta_container_set_string:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given string value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
CREATE_SETTER (string, const gchar *, G_TYPE_STRING, string);

/**
 * ges_meta_container_set_meta:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @value: (nullable): The value to set under @meta_item, or %NULL to
 * remove the corresponding field
 *
 * Sets the value of the specified field of the meta container to a
 * copy of the given value. If the given @value is %NULL, the field
 * given by @meta_item is removed and %TRUE is returned.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 */
gboolean
ges_meta_container_set_meta (GESMetaContainer * container,
    const gchar * meta_item, const GValue * value)
{
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  if (value == NULL) {
    GstStructure *structure = _meta_container_get_structure (container);
    gst_structure_remove_field (structure, meta_item);

    g_signal_emit (container, _signals[NOTIFY_SIGNAL], 0, meta_item, value);

    return TRUE;
  }

  if (_can_write_value (container, meta_item, G_VALUE_TYPE (value)) == FALSE)
    return FALSE;

  return _set_value (container, meta_item, value);
}

/**
 * ges_meta_container_set_marker_list:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to set
 * @list: The value to set under @meta_item
 *
 * Sets the value of the specified field of the meta container to the
 * given marker list value.
 *
 * Returns: %TRUE if @value was set under @meta_item for @container.
 *
 * Since: 1.18
 */
gboolean
ges_meta_container_set_marker_list (GESMetaContainer * container,
    const gchar * meta_item, const GESMarkerList * list)
{
  gboolean ret;
  GValue v = G_VALUE_INIT;
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  if (list == NULL) {
    GstStructure *structure = _meta_container_get_structure (container);
    gst_structure_remove_field (structure, meta_item);

    g_signal_emit (container, _signals[NOTIFY_SIGNAL], 0, meta_item, list);

    return TRUE;
  }

  g_return_val_if_fail (GES_IS_MARKER_LIST ((gpointer) list), FALSE);

  if (_can_write_value (container, meta_item, GES_TYPE_MARKER_LIST) == FALSE)
    return FALSE;

  g_value_init_from_instance (&v, (gpointer) list);

  ret = _set_value (container, meta_item, &v);

  g_value_unset (&v);

  return ret;
}

/**
 * ges_meta_container_metas_to_string:
 * @container: A #GESMetaContainer
 *
 * Serializes the set metadata fields of the meta container to a string.
 *
 * Returns: (transfer full): A serialized @container.
 */
gchar *
ges_meta_container_metas_to_string (GESMetaContainer * container)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), NULL);

  structure = _meta_container_get_structure (container);

  return gst_structure_to_string (structure);
}

/**
 * ges_meta_container_add_metas_from_string:
 * @container: A #GESMetaContainer
 * @str: A string to deserialize and add to @container
 *
 * Deserializes the given string, and adds and sets the found fields and
 * their values on the container. The string should be the return of
 * ges_meta_container_metas_to_string().
 *
 * Returns: %TRUE if the fields in @str was successfully deserialized
 * and added to @container.
 */
gboolean
ges_meta_container_add_metas_from_string (GESMetaContainer * container,
    const gchar * str)
{
  GstStructure *n_structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);

  n_structure = gst_structure_from_string (str, NULL);
  if (n_structure == NULL) {
    GST_WARNING_OBJECT (container, "Could not add metas: %s", str);
    return FALSE;
  }

  gst_structure_foreach (n_structure, (GstStructureForeachFunc) _append_foreach,
      container);

  gst_structure_free (n_structure);
  return TRUE;
}

/**
 * ges_meta_container_register_static_meta:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @type: The required value type for the registered field
 *
 * Registers a static metadata field on the container to only hold the
 * specified type. After calling this, setting a value under this field
 * can only succeed if its type matches the registered type of the field.
 *
 * Unlike ges_meta_container_register_meta(), no (initial) value is set
 * for this field, which means you can use this method to reserve the
 * space to be _optionally_ set later.
 *
 * Note that if a value has already been set for the field being
 * registered, then its type must match the registering type, and its
 * value will be left in place. If the field has no set value, then
 * you will likely want to include #GES_META_WRITABLE in @flags to allow
 * the value to be set later.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold @type values, with the given @flags.
 *
 * Since: 1.18
 */
gboolean
ges_meta_container_register_static_meta (GESMetaContainer * container,
    GESMetaFlag flags, const gchar * meta_item, GType type)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  /* If the meta is already in use, and is of a different type, then we
   * want to fail since, unlike ges_meta_container_register_meta, we will
   * not be overwriting this value! If we didn't fail, the user could have
   * a false sense that this meta will always be of the reserved type.
   */
  structure = _meta_container_get_structure (container);
  if (gst_structure_has_field (structure, meta_item) &&
      gst_structure_get_field_type (structure, meta_item) != type) {
    gchar *value_string =
        g_strdup_value_contents (gst_structure_get_value (structure,
            meta_item));
    GST_WARNING_OBJECT (container,
        "Meta %s already assigned a value of %s, which is a different type",
        meta_item, value_string);
    g_free (value_string);
    return FALSE;
  }
  return _register_meta (container, flags, meta_item, type);
}

#define CREATE_REGISTER_STATIC(name, value_ctype, value_gtype, setter_name) \
gboolean                                                                      \
ges_meta_container_register_meta_ ## name (GESMetaContainer *container,\
    GESMetaFlag flags, const gchar *meta_item, value_ctype value)             \
{                                                                             \
  gboolean ret;                                                               \
  GValue gval = { 0 };                                                        \
                                                                              \
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);            \
  g_return_val_if_fail (meta_item != NULL, FALSE);                            \
                                                                              \
  if (!_register_meta (container, flags, meta_item, value_gtype))             \
    return FALSE;                                                             \
                                                                              \
  g_value_init (&gval, value_gtype);                                          \
  g_value_set_ ##setter_name (&gval, value);                                  \
                                                                              \
  ret = _set_value  (container, meta_item, &gval);                            \
                                                                              \
  g_value_unset (&gval);                                                      \
  return ret;                                                                 \
}

/**
 * ges_meta_container_register_meta_boolean:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given boolean value, and registers the field to only hold a boolean
 * typed value. After calling this, only boolean values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold boolean typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (boolean, gboolean, G_TYPE_BOOLEAN, boolean);

/**
 * ges_meta_container_register_meta_int:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given int value, and registers the field to only hold an int
 * typed value. After calling this, only int values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold int typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (int, gint, G_TYPE_INT, int);

/**
 * ges_meta_container_register_meta_uint:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given uint value, and registers the field to only hold a uint
 * typed value. After calling this, only uint values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold uint typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (uint, guint, G_TYPE_UINT, uint);

/**
 * ges_meta_container_register_meta_int64:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given int64 value, and registers the field to only hold an int64
 * typed value. After calling this, only int64 values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold int64 typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (int64, gint64, G_TYPE_INT64, int64);

/**
 * ges_meta_container_register_meta_uint64:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given uint64 value, and registers the field to only hold a uint64
 * typed value. After calling this, only uint64 values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold uint64 typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (uint64, guint64, G_TYPE_UINT64, uint64);

/**
 * ges_meta_container_register_meta_float:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given float value, and registers the field to only hold a float
 * typed value. After calling this, only float values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold float typed values, with the given @flags,
 * and the field was successfully set to @value.
*/
CREATE_REGISTER_STATIC (float, float, G_TYPE_FLOAT, float);

/**
 * ges_meta_container_register_meta_double:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given double value, and registers the field to only hold a double
 * typed value. After calling this, only double values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold double typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (double, double, G_TYPE_DOUBLE, double);

/**
 * ges_meta_container_register_meta_date:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given date value, and registers the field to only hold a date
 * typed value. After calling this, only date values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold date typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (date, const GDate *, G_TYPE_DATE, boxed);

/**
 * ges_meta_container_register_meta_date_time:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given date time value, and registers the field to only hold a date time
 * typed value. After calling this, only date time values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold date time typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (date_time, const GstDateTime *, GST_TYPE_DATE_TIME,
    boxed);

/**
 * ges_meta_container_register_meta_string:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given string value, and registers the field to only hold a string
 * typed value. After calling this, only string values can be set for
 * this field. The given flags can be set to make this field only
 * readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold string typed values, with the given @flags,
 * and the field was successfully set to @value.
 */
CREATE_REGISTER_STATIC (string, const gchar *, G_TYPE_STRING, string);

/**
 * ges_meta_container_register_meta:
 * @container: A #GESMetaContainer
 * @flags: Flags to be used for the registered field
 * @meta_item: The key for the @container field to register
 * @value: The value to set for the registered field
 *
 * Sets the value of the specified field of the meta container to the
 * given value, and registers the field to only hold a value of the
 * same type. After calling this, only values of the same type as @value
 * can be set for this field. The given flags can be set to make this
 * field only readable after calling this method.
 *
 * Returns: %TRUE if the @meta_item field was successfully registered on
 * @container to only hold @value types, with the given @flags, and the
 * field was successfully set to @value.
 */
gboolean
ges_meta_container_register_meta (GESMetaContainer * container,
    GESMetaFlag flags, const gchar * meta_item, const GValue * value)
{
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  if (!_register_meta (container, flags, meta_item, G_VALUE_TYPE (value)))
    return FALSE;

  return _set_value (container, meta_item, value);
}

/**
 * ges_meta_container_check_meta_registered:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to check
 * @flags: (out) (optional): A destination to get the registered flags of
 * the field, or %NULL to ignore
 * @type: (out) (optional): A destination to get the registered type of
 * the field, or %NULL to ignore
 *
 * Checks whether the specified field has been registered as static, and
 * gets the registered type and flags of the field, as used in
 * ges_meta_container_register_meta() and
 * ges_meta_container_register_static_meta().
 *
 * Returns: %TRUE if the @meta_item field has been registered on
 * @container.
 */
gboolean
ges_meta_container_check_meta_registered (GESMetaContainer * container,
    const gchar * meta_item, GESMetaFlag * flags, GType * type)
{
  ContainerData *data;
  RegisteredMeta *static_item;

  data = g_object_get_qdata (G_OBJECT (container), ges_meta_key);
  if (!data)
    return FALSE;

  static_item = g_hash_table_lookup (data->static_items, meta_item);
  if (static_item == NULL) {
    GST_WARNING_OBJECT (container, "Static meta %s has not been registered yet",
        meta_item);

    return FALSE;
  }

  if (type)
    *type = static_item->item_type;

  if (flags)
    *flags = static_item->flags;

  return TRUE;
}

/* Copied from gsttaglist.c */
/***** evil macros to get all the *_get_* functions right *****/

#define CREATE_GETTER(name,type)                                         \
gboolean                                                                 \
ges_meta_container_get_ ## name (GESMetaContainer *container,    \
                           const gchar *meta_item, type value)       \
{                                                                        \
  GstStructure *structure;                                                     \
                                                                         \
  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);   \
  g_return_val_if_fail (meta_item != NULL, FALSE);                   \
  g_return_val_if_fail (value != NULL, FALSE);                           \
                                                                         \
  structure = _meta_container_get_structure (container);                    \
                                                                         \
  return gst_structure_get_ ## name (structure, meta_item, value);   \
}

/**
 * ges_meta_container_get_boolean:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current boolean value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the boolean value under @meta_item was copied
 * to @dest.
 */
CREATE_GETTER (boolean, gboolean *);

/**
 * ges_meta_container_get_int:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current int value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the int value under @meta_item was copied
 * to @dest.
 */
CREATE_GETTER (int, gint *);

/**
 * ges_meta_container_get_uint:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current uint value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the uint value under @meta_item was copied
 * to @dest.
 */
CREATE_GETTER (uint, guint *);

/**
 * ges_meta_container_get_double:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current double value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the double value under @meta_item was copied
 * to @dest.
 */
CREATE_GETTER (double, gdouble *);

/**
 * ges_meta_container_get_int64:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current int64 value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the int64 value under @meta_item was copied
 * to @dest.
 */
gboolean
ges_meta_container_get_int64 (GESMetaContainer * container,
    const gchar * meta_item, gint64 * dest)
{
  GstStructure *structure;
  const GValue *value;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  value = gst_structure_get_value (structure, meta_item);
  if (!value || G_VALUE_TYPE (value) != G_TYPE_INT64)
    return FALSE;

  *dest = g_value_get_int64 (value);

  return TRUE;
}

/**
 * ges_meta_container_get_uint64:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current uint64 value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the uint64 value under @meta_item was copied
 * to @dest.
 */
gboolean
ges_meta_container_get_uint64 (GESMetaContainer * container,
    const gchar * meta_item, guint64 * dest)
{
  GstStructure *structure;
  const GValue *value;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  value = gst_structure_get_value (structure, meta_item);
  if (!value || G_VALUE_TYPE (value) != G_TYPE_UINT64)
    return FALSE;

  *dest = g_value_get_uint64 (value);

  return TRUE;
}

/**
 * ges_meta_container_get_float:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current float value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the float value under @meta_item was copied
 * to @dest.
 */
gboolean
ges_meta_container_get_float (GESMetaContainer * container,
    const gchar * meta_item, gfloat * dest)
{
  GstStructure *structure;
  const GValue *value;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  value = gst_structure_get_value (structure, meta_item);
  if (!value || G_VALUE_TYPE (value) != G_TYPE_FLOAT)
    return FALSE;

  *dest = g_value_get_float (value);

  return TRUE;
}

/**
 * ges_meta_container_get_string:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 *
 * Gets the current string value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: (transfer none) (nullable): The string value under @meta_item, or %NULL
 * if it could not be fetched.
 */
const gchar *
ges_meta_container_get_string (GESMetaContainer * container,
    const gchar * meta_item)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (meta_item != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  return gst_structure_get_string (structure, meta_item);
}

/**
 * ges_meta_container_get_meta:
 * @container: A #GESMetaContainer
 * @key: The key for the @container field to get
 *
 * Gets the current value of the specified field of the meta container.
 *
 * Returns: (transfer none) (nullable): The value under @key, or %NULL if @container
 * does not have the field set.
 */
const GValue *
ges_meta_container_get_meta (GESMetaContainer * container, const gchar * key)
{
  GstStructure *structure;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  return gst_structure_get_value (structure, key);
}

/**
 * ges_meta_container_get_marker_list:
 * @container: A #GESMetaContainer
 * @key: The key for the @container field to get
 *
 * Gets the current marker list value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: (transfer full) (nullable): A copy of the marker list value under @key,
 * or %NULL if it could not be fetched.
 * Since: 1.18
 */
GESMarkerList *
ges_meta_container_get_marker_list (GESMetaContainer * container,
    const gchar * key)
{
  GstStructure *structure;
  const GValue *v;

  g_return_val_if_fail (GES_IS_META_CONTAINER (container), FALSE);
  g_return_val_if_fail (key != NULL, FALSE);

  structure = _meta_container_get_structure (container);

  v = gst_structure_get_value (structure, key);

  if (v == NULL) {
    return NULL;
  }

  return GES_MARKER_LIST (g_value_dup_object (v));
}

/**
 * ges_meta_container_get_date:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out) (optional) (transfer full): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current date value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the date value under @meta_item was copied
 * to @dest.
 */
CREATE_GETTER (date, GDate **);

/**
 * ges_meta_container_get_date_time:
 * @container: A #GESMetaContainer
 * @meta_item: The key for the @container field to get
 * @dest: (out) (optional) (transfer full): Destination into which the value under @meta_item
 * should be copied.
 *
 * Gets the current date time value of the specified field of the meta
 * container. If the field does not have a set value, or it is of the
 * wrong type, the method will fail.
 *
 * Returns: %TRUE if the date time value under @meta_item was copied
 * to @dest.
 */
CREATE_GETTER (date_time, GstDateTime **);
