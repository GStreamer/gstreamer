#include <glib-object.h>
#include <gst/gst.h>

#include "ges-meta-container.h"

/**
* SECTION: ges-metadata-container
* @short_description: An interface for storing metadata
*
* Interface that allows reading and writing metadata
*/

static GQuark ges_taglist_key;

typedef struct
{
  GstTagMergeMode mode;
  GstTagList *list;
  GMutex lock;
} GESMetadata;

#define GES_METADATA_LOCK(data) g_mutex_lock(&data->lock)
#define GES_METADATA_UNLOCK(data) g_mutex_unlock(&data->lock)

G_DEFINE_INTERFACE_WITH_CODE (GESMetadataContainer, ges_meta_container,
    G_TYPE_OBJECT, ges_taglist_key =
    g_quark_from_static_string ("ges-metadata-container-data"););

static void
ges_meta_container_default_init (GESMetadataContainerInterface * iface)
{

}

static void
ges_metadata_free (gpointer p)
{
  GESMetadata *data = (GESMetadata *) p;

  if (data->list)
    gst_tag_list_unref (data->list);

  g_mutex_clear (&data->lock);

  g_slice_free (GESMetadata, data);
}

static GESMetadata *
ges_meta_container_get_data (GESMetadataContainer * container)
{
  GESMetadata *data;

  data = g_object_get_qdata (G_OBJECT (container), ges_taglist_key);
  if (!data) {
    /* make sure no other thread is creating a GstTagData at the same time */
    static GMutex create_mutex; /* no initialisation required */
    g_mutex_lock (&create_mutex);

    data = g_object_get_qdata (G_OBJECT (container), ges_taglist_key);
    if (!data) {
      data = g_slice_new (GESMetadata);
      g_mutex_init (&data->lock);
      data->list = gst_tag_list_new_empty ();
      data->mode = GST_TAG_MERGE_KEEP;
      g_object_set_qdata_full (G_OBJECT (container), ges_taglist_key, data,
          ges_metadata_free);
    }

    g_mutex_unlock (&create_mutex);
  }

  return data;
}

typedef struct
{
  GESMetadataForeachFunc func;
  const GESMetadataContainer *container;
  gpointer data;
} MetadataForeachData;

static void
tag_list_foreach (const GstTagList * taglist, const gchar * tag,
    gpointer user_data)
{
  MetadataForeachData *data = (MetadataForeachData *) user_data;

  GValue value = { 0, };

  if (!gst_tag_list_copy_value (&value, taglist, tag))
    return;

  data->func (data->container, tag, &value, data->data);
}

/**
 * ges_metadata_container_foreach:
 * @container: container to iterate over
 * @func: (scope call): function to be called for each tag
 * @user_data: (closure): user specified data
 *
 * Calls the given function for each tag inside the metadata container. Note
 * that if there is no tag, the function won't be called at all.
 */
void
ges_metadata_container_foreach (GESMetadataContainer * container,
    GESMetadataForeachFunc func, gpointer user_data)
{
  GESMetadata *data;
  MetadataForeachData foreach_data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (func != NULL);

  data = ges_meta_container_get_data (container);

  foreach_data.func = func;
  foreach_data.container = container;
  foreach_data.data = user_data;

  gst_tag_list_foreach (data->list, (GstTagForeachFunc) tag_list_foreach,
      &foreach_data);
}

/**
 * ges_meta_container_set_boolean:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_boolean (GESMetadataContainer * container,
    const gchar * metadata_item, gboolean value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_BOOLEAN);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_int:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_int (GESMetadataContainer * container,
    const gchar * metadata_item, gint value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_INT);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_uint:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_uint (GESMetadataContainer * container,
    const gchar * metadata_item, guint value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_UINT);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_int64:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_int64 (GESMetadataContainer * container,
    const gchar * metadata_item, gint64 value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_INT64);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_uint64:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_uint64 (GESMetadataContainer * container,
    const gchar * metadata_item, guint64 value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_UINT64);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_float:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_float (GESMetadataContainer * container,
    const gchar * metadata_item, gfloat value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_FLOAT);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_double:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_double (GESMetadataContainer * container,
    const gchar * metadata_item, gdouble value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_DOUBLE);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_date:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_date (GESMetadataContainer * container,
    const gchar * metadata_item, const GDate * value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_DATE);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_date_time:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_date_time (GESMetadataContainer * container,
    const gchar * metadata_item, const GstDateTime * value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, GST_TYPE_DATE_TIME);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_string:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_string (GESMetadataContainer * container,
    const gchar * metadata_item, const gchar * value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_STRING);
  gst_tag_list_add (data->list, data->mode, metadata_item, value, NULL);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_meta_container_set_value:
 * @container: Target container
 * @metadata_item: Name of the metadata item to set
 * @value: Value to set
 * Sets the value of a given metadata item
 */
void
ges_meta_container_set_meta (GESMetadataContainer * container,
    const gchar * metadata_item, const GValue * value)
{
  GESMetadata *data;

  g_return_if_fail (GES_IS_METADATA_CONTAINER (container));
  g_return_if_fail (metadata_item != NULL);

  data = ges_meta_container_get_data (container);

  GES_METADATA_LOCK (data);
  ges_metadata_register (metadata_item, G_TYPE_STRING);
  gst_tag_list_add_value (data->list, data->mode, metadata_item, value);
  GES_METADATA_UNLOCK (data);
}

/**
 * ges_metadata_container_to_string:
 * @container: a #GESMetadataContainer
 *
 * Serializes a metadata container to a string.
 *
 * Returns: a newly-allocated string, or NULL in case of an error. The
 *    string must be freed with g_free() when no longer needed.
 */
gchar *
ges_metadata_container_to_string (GESMetadataContainer * container)
{
  GESMetadata *data;

  g_return_val_if_fail (GES_IS_METADATA_CONTAINER (container), NULL);

  data = ges_meta_container_get_data (container);

  return gst_tag_list_to_string (data->list);
}

/**
 * ges_metadata_container_new_from_string:
 * @str: a string created with ges_metadata_container_to_string()
 *
 * Deserializes a metadata container.
 *
 * Returns: (transfer full): a new #GESMetadataContainer, or NULL in case of an
 * error.
 */
GESMetadataContainer *
ges_metadata_container_new_from_string (const gchar * str)
{
  return NULL;
}

void
ges_metadata_register (const gchar * name, GType type)
{
  gst_tag_register (name, GST_TAG_FLAG_META, type, name, name, NULL);
}

/* Copied from gsttaglist.c */
/***** evil macros to get all the *_get_* functions right *****/

#define CREATE_GETTER(name,type)                                         \
gboolean                                                                 \
ges_meta_container_get_ ## name (GESMetadataContainer *container,    \
                           const gchar *metadata_item, type value)       \
{                                                                        \
  GESMetadata *data;                                                     \
                                                                         \
  g_return_val_if_fail (GES_IS_METADATA_CONTAINER (container), FALSE);   \
  g_return_val_if_fail (metadata_item != NULL, FALSE);                   \
  g_return_val_if_fail (value != NULL, FALSE);                           \
                                                                         \
  data = ges_meta_container_get_data (container);                    \
                                                                         \
  return gst_tag_list_get_ ## name (data->list, metadata_item, value);   \
}

/**
 * ges_meta_container_get_boolean:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (boolean, gboolean *);
/**
 * ges_meta_container_get_int:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (int, gint *);
/**
 * ges_meta_container_get_uint:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (uint, guint *);
/**
 * ges_meta_container_get_int64:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (int64, gint64 *);
/**
 * ges_meta_container_get_uint64:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (uint64, guint64 *);
/**
 * ges_meta_container_get_float:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (float, gfloat *);
/**
 * ges_meta_container_get_double:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (double, gdouble *);

static inline gchar *
_gst_strdup0 (const gchar * s)
{
  if (s == NULL || *s == '\0')
    return NULL;

  return g_strdup (s);
}

/**
 * ges_meta_container_get_string:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (string, gchar **);

/**
 * ges_meta_container_get_meta:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @value: (out) (transfer full): Destination to which value of metadata item will be copied
 *
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 *
 * Returns: %TRUE if the vale could be optained %FALSE otherwize
 */
gboolean
ges_meta_container_get_meta (GESMetadataContainer * container,
    const gchar * tag, GValue * value)
{
  GESMetadata *data;

  g_return_val_if_fail (GES_IS_METADATA_CONTAINER (container), FALSE);
  g_return_val_if_fail (tag != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  data = ges_meta_container_get_data (container);

  if (!gst_tag_list_copy_value (value, data->list, tag))
    return FALSE;

  return (value != NULL);
}

/**
 * ges_meta_container_get_date:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (date, GDate **);

/**
 * ges_meta_container_get_date_time:
 * @container: Target container
 * @metadata_item: Name of the metadata item to get
 * @dest: (out): Destination to which value of metadata item will be copied
 * Gets the value of a given metadata item, returns NULL if @metadata_item
 * can not be found.
 */
CREATE_GETTER (date_time, GstDateTime **);
