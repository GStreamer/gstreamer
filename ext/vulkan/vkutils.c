/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vkutils.h"

gboolean
_check_for_all_layers (uint32_t check_count, const char **check_names,
    uint32_t layer_count, VkLayerProperties * layers)
{
  uint32_t i, j;

  for (i = 0; i < check_count; i++) {
    gboolean found = FALSE;
    for (j = 0; j < layer_count; j++) {
      if (g_strcmp0 (check_names[i], layers[j].layerName) == 0) {
        found = TRUE;
      }
    }
    if (!found) {
      GST_ERROR ("Cannot find layer: %s", check_names[i]);
      return FALSE;
    }
  }
  return TRUE;
}
