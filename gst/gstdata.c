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

/* this file makes too much noise for most debugging sessions */
#define GST_DEBUG_FORCE_DISABLE
#include "gst_private.h"

#include "gstdata.h"
#include "gstdata_private.h"
#include "gstlog.h"

void
_gst_data_init (GstData *data, GType type, guint16 flags, GstDataFreeFunction free, GstDataCopyFunction copy)
{
  _GST_DATA_INIT (data, type, flags, free, copy);
}

void
_gst_data_free (GstData *data)
{
  _GST_DATA_DISPOSE (data);
  g_free (data);
}

/**
 * gst_data_copy:
 * @data: a #GstData to copy
 *
 * Copies the given #GstData
 *
 * Returns: a copy of the data or NULL if the data cannot be copied.
 */
GstData*
gst_data_copy (const GstData *data) 
{
  if (data->copy)
    return data->copy (data); 

  return NULL;
}

/**
 * gst_data_copy_on_write:
 * @data: a #GstData to copy
 *
 * Copies the given #GstData if the refcount is greater than 1 so that the
 * #GstData object can be written to safely.
 *
 * Returns: a copy of the data if the refcount is > 1, data if the refcount == 1
 * or NULL if the data could not be copied.
 */
GstData*
gst_data_copy_on_write (const GstData *data) 
{
  gint refcount;

  GST_ATOMIC_INT_READ (&data->refcount, &refcount);

  if (refcount == 1)
    return GST_DATA (data);
	
  if (data->copy)
    return data->copy (data); 

  return NULL;
}

/**
 * gst_data_free:
 * @data: a #GstData to free
 *
 * Frees the given #GstData 
 */
void
gst_data_free (GstData *data) 
{
  if (data->free)
    data->free (data); 
}

/**
 * gst_data_ref:
 * @data: a #GstData to reference
 *
 * Increments the reference count of this data.
 *
 * Returns: the data
 */
GstData* 
gst_data_ref (GstData *data) 
{
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (GST_DATA_REFCOUNT_VALUE(data) > 0, NULL);

  GST_ATOMIC_INT_INC (&data->refcount);

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
GstData* 
gst_data_ref_by_count (GstData *data, gint count)
{
  g_return_val_if_fail (data != NULL, NULL);
  g_return_val_if_fail (count >= 0, NULL);
  g_return_val_if_fail (GST_DATA_REFCOUNT_VALUE(data) > 0, NULL);

  GST_ATOMIC_INT_ADD (&data->refcount, count);

  return data;
}

/**
 * gst_data_unref:
 * @data: a #GstData to unreference
 *
 * Decrements the refcount of this data. If the refcount is
 * zero, the data will be freeed.
 */
void 
gst_data_unref (GstData *data) 
{
  gint zero;

  g_return_if_fail (data != NULL);

  GST_INFO (GST_CAT_BUFFER, "unref data %p, current count is %d", data,GST_DATA_REFCOUNT_VALUE(data));
  g_return_if_fail (GST_DATA_REFCOUNT_VALUE(data) > 0);

  GST_ATOMIC_INT_DEC_AND_TEST (&data->refcount, &zero);

  /* if we ended up with the refcount at zero, free the data */
  if (zero) {
    if (data->free) 
      data->free (data); 
  }
}

