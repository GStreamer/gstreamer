/*
 * GStreamer gstreamer-tensormeta
 * Copyright (C) 2023 Collabora Ltd
 *
 * gsttensormeta.c
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gsttensormeta.h"

static gboolean
gst_tensor_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstTensorMeta *tmeta = (GstTensorMeta *) meta;

  tmeta->num_tensors = 0;
  tmeta->tensors = NULL;

  return TRUE;
}

static void
gst_tensor_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstTensorMeta *tmeta = (GstTensorMeta *) meta;

  for (int i = 0; i < tmeta->num_tensors; i++) {
    gst_tensor_free (tmeta->tensors[i]);
  }
  g_free (tmeta->tensors);
}

/**
 * gst_tensor_meta_api_get_type: (skip)
 *
 * Since: 1.26
 */
GType
gst_tensor_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstTensorMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}


/**
 * gst_tensor_meta_get_info: (skip)
 *
 * Since: 1.26
 */
const GstMetaInfo *
gst_tensor_meta_get_info (void)
{
  static const GstMetaInfo *tmeta_info = NULL;

  if (g_once_init_enter (&tmeta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_tensor_meta_api_get_type (),
        "GstTensorMeta",
        sizeof (GstTensorMeta),
        gst_tensor_meta_init,
        gst_tensor_meta_free,
        NULL);                  /* tensor_meta_transform not implemented */
    g_once_init_leave (&tmeta_info, meta);
  }
  return tmeta_info;
}

/**
 * gst_buffer_add_tensor_meta:
 * @buffer: A writable #GstBuffer
 *
 * Adds a #GstTensorMeta to a buffer or returns the existing one
 *
 * Returns: (transfer none): The new #GstTensorMeta
 *
 * Since: 1.26
 */

GstTensorMeta *
gst_buffer_add_tensor_meta (GstBuffer * buffer)
{
  GstTensorMeta *tmeta;

  tmeta = gst_buffer_get_tensor_meta (buffer);
  if (tmeta)
    return tmeta;

  return (GstTensorMeta *) gst_buffer_add_meta (buffer,
      gst_tensor_meta_get_info (), NULL);
}

/**
 * gst_buffer_get_tensor_meta:
 * @buffer: A #GstBuffer
 *
 * Gets the #GstTensorMeta from a buffer
 *
 * Returns: (nullable)(transfer none): The #GstTensorMeta if there is wone
 *
 * Since: 1.26
 */

GstTensorMeta *
gst_buffer_get_tensor_meta (GstBuffer * buffer)
{
  return (GstTensorMeta *) gst_buffer_get_meta (buffer,
      GST_TENSOR_META_API_TYPE);
}

/**
 * gst_tensor_meta_set:
 * @tmeta: a #GstTensorMeta
 * @num_tensors: The number of tensors in the @tensors array
 * @tensors: (in) (array length=num_tensors) (transfer full): An array of poiners to #GstTensor
 *
 * Sets tensors into the #GstTensorMeta
 *
 * Since: 1.26
 */
void
gst_tensor_meta_set (GstTensorMeta * tmeta, guint num_tensors,
    GstTensor ** tensors)
{
  guint i;

  for (i = 0; i < tmeta->num_tensors; i++) {
    gst_tensor_free (tmeta->tensors[i]);
  }
  g_free (tmeta->tensors);

  tmeta->num_tensors = num_tensors;
  tmeta->tensors = tensors;
}

/**
 * gst_tensor_meta_get:
 * @tmeta: A #GstTensorMeta
 * @index: The number of the tensor to get
 *
 * Retrieves a tensor from the #GstTensorMeta, the index must be
 * smaller than #GstTensorMeta.num_tensors
 *
 * Return: (transfer none): a GstTensor
 *
 * Since: 1.26
 */
const GstTensor *
gst_tensor_meta_get (GstTensorMeta * tmeta, gsize index)
{
  g_return_val_if_fail (tmeta->tensors, NULL);
  g_return_val_if_fail (index < tmeta->num_tensors, NULL);

  return tmeta->tensors[index];
}

/**
 * gst_tensor_meta_get_index_from_id:
 * @meta: a #GstTensorMeta
 * @id: The tensor id to look for
 *
 * Finds the first tensor with the requsted ID in the meta
 *
 * Return: The index of the tensor inthe meta, or -1 if
 *  its not found.
 *
 * Since: 1.26
 */
gint
gst_tensor_meta_get_index_from_id (GstTensorMeta * meta, GQuark id)
{
  for (int i = 0; i < meta->num_tensors; ++i) {
    if (meta->tensors[i]->id == id)
      return i;
  }

  return -1;
}
