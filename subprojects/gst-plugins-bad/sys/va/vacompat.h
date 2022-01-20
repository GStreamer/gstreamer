/* GStreamer
 *  Copyright (C) 2022 Intel Corporation
 *     Author: He Junyan <junyan.he@intel.com>
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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION(2, 60, 0)
#define g_queue_clear_full queue_clear_full
static inline void
queue_clear_full (GQueue * queue, GDestroyNotify free_func)
{
  gpointer data;

  while ((data = g_queue_pop_head (queue)) != NULL)
    free_func (data);
}
#endif

#if !GLIB_CHECK_VERSION(2, 62, 0)
#define g_array_copy array_copy
static inline GArray *
array_copy (GArray *array)
{
  GArray *new_array;
  guint elt_size = g_array_get_element_size (array);

  new_array = g_array_sized_new (FALSE, FALSE, elt_size, array->len);

  g_array_set_size (new_array, array->len);
  if (array->len > 0)
    memcpy (new_array->data, array->data, array->len * elt_size);

  return new_array;
}
#endif

G_END_DECLS
