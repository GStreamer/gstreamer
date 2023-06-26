/*
 * GStreamer gstreamer-tensorid
 * Copyright (C) 2023 Collabora Ltd
 *
 * gsttensorid.c
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

#include <gst/gst.h>
#include "gsttensorid.h"

/* Structure to encapsulate a string and its associated GQuark */
struct TensorQuark
{
  const char *string;
  GQuark quark_id;
};

class TensorId
{
public:
  TensorId (void):tensor_quarks_array (g_array_new (FALSE, FALSE,
          sizeof (TensorQuark)))
  {
  }
  ~TensorId (void)
  {
    if (tensor_quarks_array) {
      for (guint i = 0; i < tensor_quarks_array->len; i++) {
        TensorQuark *quark =
            &g_array_index (tensor_quarks_array, TensorQuark, i);
        g_free ((gpointer) quark->string);      // free the duplicated string
      }
      g_array_free (tensor_quarks_array, TRUE);
    }
  }
  GQuark get_quark (const char *str)
  {
    for (guint i = 0; i < tensor_quarks_array->len; i++) {
      TensorQuark *quark = &g_array_index (tensor_quarks_array, TensorQuark, i);
      if (g_strcmp0 (quark->string, str) == 0) {
        return quark->quark_id; // already registered
      }
    }

    // Register the new quark and append to the GArray
    TensorQuark new_quark;
    new_quark.string = g_strdup (str);  // create a copy of the string
    new_quark.quark_id = g_quark_from_string (new_quark.string);
    g_array_append_val (tensor_quarks_array, new_quark);

    return new_quark.quark_id;
  }
private:
  GArray * tensor_quarks_array;
};

static TensorId tensorId;

G_BEGIN_DECLS

GQuark
gst_tensorid_get_quark (const char *tensor_id)
{
  return tensorId.get_quark (tensor_id);
}

G_END_DECLS
