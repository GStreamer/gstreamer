/* GStreamer
 * Copyright (C) 2022 Sebastian Dröge <sebastian@centricular.com>
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

#include "gstrtputils.h"

guint8
gst_rtp_get_extmap_id_for_attribute (const GstStructure * s,
    const gchar * ext_name)
{
  guint i;
  guint8 extmap_id = 0;
  guint n_fields = gst_structure_n_fields (s);

  for (i = 0; i < n_fields; i++) {
    const gchar *field_name = gst_structure_nth_field_name (s, i);
    if (g_str_has_prefix (field_name, "extmap-")) {
      const gchar *str = gst_structure_get_string (s, field_name);
      if (str && g_strcmp0 (str, ext_name) == 0) {
        gint64 id = g_ascii_strtoll (field_name + 7, NULL, 10);
        if (id > 0 && id < 15) {
          extmap_id = id;
          break;
        }
      }
    }
  }
  return extmap_id;
}
