/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstdata.c: Data operations
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

#include "gst_private.h"

#include "gstatomic_impl.h"
#include "gstdata.h"
#include "gstdata_private.h"
#include "gstinfo.h"

GType
gst_data_get_type (void)
{
  static GType type = 0;

  if (!type)
    type = g_boxed_type_register_static ("GstData",
        (GBoxedCopyFunc) gst_data_copy, (GBoxedFreeFunc) gst_data_unref);
  return type;
}

/**
 * gst_data_init:
 * @data: a #GstData to initialize
 * @type: the type of this data
 * @flags: flags for this data
 * @free: a free function 
 * @copy: a copy function 
 *
 * Initialize the given data structure with the given parameters. The free and copy 
 * function will be called when this data is freed or copied respectively.
 */
void
gst_data_init (GstData * data, GType type, guint16 flags,
    GstDataFreeFunction free, GstDataCopyFunction copy)
{
  g_return_if_fail (data != NULL);

  _GST_DATA_INIT (data, type, flags, free, copy);
}

/**
 * gst_data_copy_into:
 * @data: a #GstData to copy
 * @target: the target #GstData to copy into
 *
 * Copy the GstData into the specified target GstData structure.
 * Thos method is mainly used by subclasses when they want to copy
 * the relevant GstData info.
 */
void
gst_data_copy_into (const GstData * data, GstData * target)
{
  g_return_if_fail (data != NULL);
}

/**
 * gst_data_dispose:
 * @data: a #GstData to dispose
 *
 * Free all the resources allocated in the gst_data_init() function, 
 * mainly used by subclass implementors.
 */
void
gst_data_dispose (GstData * data)
{
  g_return_if_fail (data != NULL);

  _GST_DATA_DISPOSE (data);
}

/**
 * gst_data_copy:
 * @data: a #GstData to copy
 *
 * Copies the given #GstData. This function will call the custom subclass
 * copy function or return NULL if no function was provided by the subclass.
 *
 * Returns: a copy of the data or NULL if the data cannot be copied. The refcount
 * of the original buffer is not changed so you should unref it when you don't
 * need it anymore.
 */
GstData *
gst_data_copy (const GstData * data)
{
  g_return_val_if_fail (data != NULL, NULL);

  if (data->copy)
    return data->copy (data);

  return NULL;
}

/**
 * gst_data_is_writable:
 * @data: a #GstData to copy
 *
 * Query if the gstdata needs to be copied before it can safely be modified.
 *
 * Returns: FALSE if the given #GstData is potentially shared and needs to
 * be copied before it can be modified safely.
 */
gboolean
gst_data_is_writable (GstData * data)
{
  gint refcount;

  g_return_val_if_fail (data != NULL, FALSE);

  refcount = gst_atomic_int_read (&data->refcount);

  if (refcount == 1 && !GST_DATA_FLAG_IS_SET (data, GST_DATA_READONLY))
    return FALSE;

  return TRUE;
}

/**
 * gst_data_copy_on_write:
 * @data: a #GstData to copy
 *
 * Copies the given #GstData if the refcount is greater than 1 so that the
 * #GstData object can be written to safely.
 *
 * Returns: a copy of the data if the refcount is > 1 or the buffer is 
 * marked READONLY, data if the refcount == 1,
 * or NULL if the data could not be copied. The refcount of the original buffer
 * is decreased when a copy is made, so you are not supposed to use it after a
 * call to this function.
 */
GstData *
gst_data_copy_on_write (GstData * data)
{
  gint refcount;

  g_return_val_if_fail (data != NULL, NULL);

  refcount = gst_atomic_int_read (&data->refcount);

  if (refcount == 1 && !GST_DATA_FLAG_IS_SET (data, GST_DATA_READONLY))
    return GST_DATA (data);

  if (data->copy) {
    GstData *copy = data->copy (data);

    gst_data_unref (data);
    return copy;
  }

  return NULL;
}

/**
 * gst_data_ref:
 * @data: a #GstData to reference
 *
 * Increments the reference count of this data.
 *
 * Returns: the data
 */
GstData *
gst_data_ref (GstData * data)
{
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (GST_DATA_REFCOUNT_VALUE (data) > 0, NULL);

  GST_CAT_LOG (GST_CAT_BUFFER, "%p %d->%d", data,
      GST_DATA_REFCOUNT_VALUE (data), GST_DATA_REFCOUNT_VALUE (data) + 1);

  gst_atomic_int_inc (&data->refcount);

  return data;
}

/**
 * gst_data_ref_by_count:
 * @data: a #GstData to reference
 * @count: the number to increment the reference count by
 *
 * Increments the reference count of this data by the given number.
 *
 * Returns: the data
 */
GstData *
gst_data_ref_by_count (GstData * data, gint count)
{
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (count >= 0, NULL);
  g_return_val_if_fail (GST_DATA_REFCOUNT_VALUE (data) > 0, NULL);

  GST_CAT_LOG (GST_CAT_BUFFER, "%p %d->%d", data,
      GST_DATA_REFCOUNT_VALUE (data), GST_DATA_REFCOUNT_VALUE (data) + count);

  gst_atomic_int_add (&data->refcount, count);

  return data;
}

/**
 * gst_data_unref:
 * @data: a #GstData to unreference
 *
 * Decrements the refcount of this data. If the refcount is
 * zero, the data will be freed.
 *
 * When you add data to a pipeline, the pipeline takes ownership of the
 * data.  When the data has been used by some plugin, it must unref()s it.
 * Applications usually don't need to unref() anything.
 */
void
gst_data_unref (GstData * data)
{
  gint zero;

  g_return_if_fail (data != NULL);

  GST_CAT_LOG (GST_CAT_BUFFER, "%p %d->%d", data,
      GST_DATA_REFCOUNT_VALUE (data), GST_DATA_REFCOUNT_VALUE (data) - 1);
  g_return_if_fail (GST_DATA_REFCOUNT_VALUE (data) > 0);

  zero = gst_atomic_int_dec_and_test (&data->refcount);

  /* if we ended up with the refcount at zero, free the data */
  if (zero) {
    if (data->free)
      data->free (data);
  }
}
