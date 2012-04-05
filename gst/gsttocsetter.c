/* GStreamer
 * Copyright (C) 2010, 2012 Alexander Saprykin <xelfium@gmail.com>
 *
 * gsttocsetter.c: interface for TOC setting on elements
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
 * SECTION:gsttocsetter
 * @short_description: Element interface that allows setting and retrieval
 *                     of the TOC
 *
 * Element interface that allows setting of the TOC.
 *
 * Elements that support some kind of chapters or editions (or tracks like in
 * the FLAC cue sheet) will implement this interface.
 * 
 * If you just want to retrieve the TOC in your application then all you
 * need to do is watch for TOC messages on your pipeline's bus (or you can
 * perform TOC query). This interface is only for setting TOC data, not for
 * extracting it. To set TOC from the application, find proper tocsetter element
 * and set TOC using gst_toc_setter_set_toc().
 * 
 * Elements implementing the #GstTocSetter interface can extend existing TOC
 * by getting extend UID for that (you can use gst_toc_find_entry() to retrieve it)
 * with any TOC entries received from downstream.
 */


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst_private.h"
#include "gsttocsetter.h"
#include <gobject/gvaluecollector.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_toc_interface_debug);
#define GST_CAT_DEFAULT tag_toc_interface_debug

static GQuark gst_toc_key;

typedef struct
{
  GstToc *toc;
  GMutex lock;
} GstTocData;

GType
gst_toc_setter_get_type (void)
{
  static volatile gsize toc_setter_type = 0;

  if (g_once_init_enter (&toc_setter_type)) {
    GType _type;
    static const GTypeInfo toc_setter_info = {
      sizeof (GstTocSetterIFace),       /* class_size */
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      NULL,
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      0,
      0,
      NULL
    };

    GST_DEBUG_CATEGORY_INIT (gst_toc_interface_debug, "GstTocInterface", 0,
        "interfaces for the TOC");

    _type = g_type_register_static (G_TYPE_INTERFACE, "GstTocSetter",
        &toc_setter_info, 0);

    g_type_interface_add_prerequisite (_type, GST_TYPE_ELEMENT);

    gst_toc_key = g_quark_from_static_string ("GST_TOC_SETTER");
    g_once_init_leave (&toc_setter_type, _type);
  }

  return toc_setter_type;
}

static void
gst_toc_data_free (gpointer p)
{
  GstTocData *data = (GstTocData *) p;

  if (data->toc)
    gst_toc_free (data->toc);

  g_mutex_clear (&data->lock);

  g_slice_free (GstTocData, data);
}

static GstTocData *
gst_toc_setter_get_data (GstTocSetter * setter)
{
  GstTocData *data;

  data = g_object_get_qdata (G_OBJECT (setter), gst_toc_key);
  if (!data) {
    static GMutex create_mutex;

    /* make sure no other thread is creating a GstTocData at the same time */
    g_mutex_lock (&create_mutex);
    data = g_object_get_qdata (G_OBJECT (setter), gst_toc_key);
    if (!data) {
      data = g_slice_new (GstTocData);
      g_mutex_init (&data->lock);
      data->toc = NULL;
      g_object_set_qdata_full (G_OBJECT (setter), gst_toc_key, data,
          gst_toc_data_free);
    }
    g_mutex_unlock (&create_mutex);
  }

  return data;
}

/**
 * gst_toc_setter_reset_toc:
 * @setter: a #GstTocSetter.
 *
 * Reset the internal TOC. Elements should call this from within the
 * state-change handler.
 *
 * Since: 0.10.37
 */
void
gst_toc_setter_reset_toc (GstTocSetter * setter)
{
  GstTocData *data;

  g_return_if_fail (GST_IS_TOC_SETTER (setter));

  data = gst_toc_setter_get_data (setter);

  g_mutex_lock (&data->lock);
  if (data->toc) {
    gst_toc_free (data->toc);
    data->toc = NULL;
  }
  g_mutex_unlock (&data->lock);
}

/**
 * gst_toc_setter_get_toc:
 * @setter: a #GstTocSetter.
 *
 * Return current TOC the setter uses. The TOC should not be
 * modified or freed.
 *
 * This function is not thread-safe. Use gst_toc_setter_get_toc_copy() instead.
 *
 * Returns: a current snapshot of the TOC used in the setter
 *          or NULL if none is used.
 *
 * Since: 0.10.37
 */
const GstToc *
gst_toc_setter_get_toc (GstTocSetter * setter)
{
  g_return_val_if_fail (GST_IS_TOC_SETTER (setter), NULL);

  return gst_toc_setter_get_data (setter)->toc;
}

/**
 * gst_toc_setter_get_toc_copy:
 * @setter: a #GstTocSetter.
 *
 * Return current TOC the setter uses. The difference between this
 * function and gst_toc_setter_get_toc() is that this function returns deep
 * copy of the TOC, so you can modify it in any way. This function is thread-safe.
 * Free it when done with gst_toc_free().
 *
 * Returns: a copy of the current snapshot of the TOC used in the setter
 *          or NULL if none is used.
 *
 * Since: 0.10.37
 */
GstToc *
gst_toc_setter_get_toc_copy (GstTocSetter * setter)
{
  GstTocData *data;
  GstToc *ret = NULL;

  g_return_val_if_fail (GST_IS_TOC_SETTER (setter), NULL);

  data = gst_toc_setter_get_data (setter);
  g_mutex_lock (&data->lock);

  if (data->toc != NULL)
    ret = gst_toc_copy (data->toc);

  g_mutex_unlock (&data->lock);

  return ret;
}

/**
 * gst_toc_setter_set_toc:
 * @setter: a #GstTocSetter.
 * @toc: a #GstToc to set.
 *
 * Set the given TOC on the setter. Previously setted TOC will be
 * freed before setting a new one.
 *
 * Since: 0.10.37
 */
void
gst_toc_setter_set_toc (GstTocSetter * setter, const GstToc * toc)
{
  GstTocData *data;

  g_return_if_fail (GST_IS_TOC_SETTER (setter));

  data = gst_toc_setter_get_data (setter);

  g_mutex_lock (&data->lock);
  if (data->toc)
    gst_toc_free (data->toc);

  data->toc = gst_toc_copy (toc);

  g_mutex_unlock (&data->lock);
}

/**
 * gst_toc_setter_get_toc_entry:
 * @setter: a #GstTocSetter.
 * @uid: UID to find entry with.
 *
 * Return #GstTocEntry (if any) with given @uid. Returned entry should
 * not be modified or freed.
 *
 * This function is not thread-safe. Use gst_toc_setter_get_toc_entry_copy() instead.
 *
 * Returns: a TOC entry with given @uid from the TOC in the setter
 *          or NULL if none entry with such @uid was found.
 *
 * Since: 0.10.37
 */
const GstTocEntry *
gst_toc_setter_get_toc_entry (GstTocSetter * setter, const gchar * uid)
{
  GstTocData *data;
  const GstTocEntry *ret;

  g_return_val_if_fail (GST_IS_TOC_SETTER (setter), NULL);
  g_return_val_if_fail (uid != NULL, NULL);

  data = gst_toc_setter_get_data (setter);

  g_mutex_lock (&data->lock);

  ret = gst_toc_find_entry (data->toc, uid);

  g_mutex_unlock (&data->lock);

  return ret;
}

/**
 * gst_toc_setter_get_toc_entry_copy:
 * @setter: a #GstTocSetter.
 * @uid: UID to find entry with.
 *
 * Return #GstTocEntry (if any) with given @uid. It perform a deep copying,
 * so you can modify returned value. Free it when done with gst_toc_entry_free().
 * This function is thread-safe.
 *
 * Returns: a TOC entry with given @uid from the TOC in the setter
 *          or NULL if none entry with such @uid was found.
 *
 * Since: 0.10.37
 */
GstTocEntry *
gst_toc_setter_get_toc_entry_copy (GstTocSetter * setter, const gchar * uid)
{
  GstTocData *data;
  GstTocEntry *ret = NULL;
  const GstTocEntry *search;

  g_return_val_if_fail (GST_IS_TOC_SETTER (setter), NULL);
  g_return_val_if_fail (uid != NULL, NULL);

  data = gst_toc_setter_get_data (setter);

  g_mutex_lock (&data->lock);

  search = gst_toc_find_entry (data->toc, uid);
  if (search != NULL)
    ret = gst_toc_entry_copy (search);

  g_mutex_unlock (&data->lock);

  return ret;
}

/**
 * gst_toc_setter_add_toc_entry:
 * @setter: a #GstTocSetter.
 * @parent_uid: UID of the parent entry to append given @entry. Use 0 for the TOC root level.
 * @entry: #GstTocEntry to append.
 *
 * Try to find entry with given @parent_uid and append an @entry to that #GstTocEntry.
 *
 * Returns: TRUE if entry with @parent_uid was found, FALSE otherwise.
 *
 * Since: 0.10.37
 */
gboolean
gst_toc_setter_add_toc_entry (GstTocSetter * setter, const gchar * parent_uid,
    const GstTocEntry * entry)
{
  GstTocData *data;
  GstTocEntry *parent;
  GstTocEntry *copy_entry;
  gboolean ret = FALSE;

  g_return_val_if_fail (GST_IS_TOC_SETTER (setter), FALSE);
  g_return_val_if_fail (parent_uid != NULL, FALSE);
  g_return_val_if_fail (entry != NULL, FALSE);

  data = gst_toc_setter_get_data (setter);

  g_mutex_lock (&data->lock);

  copy_entry = gst_toc_entry_copy (entry);

  if (g_strcmp0 (parent_uid, "0") == 0)
    data->toc->entries = g_list_append (data->toc->entries, copy_entry);
  else {
    parent = gst_toc_find_entry (data->toc, parent_uid);

    if (parent != NULL) {
      parent->subentries = g_list_append (parent->subentries, copy_entry);
      ret = TRUE;
    }
  }

  g_mutex_unlock (&data->lock);

  return ret;
}
