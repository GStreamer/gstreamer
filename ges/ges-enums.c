/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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

#include "ges-enums.h"

#define C_ENUM(v) ((guint) v)
static void
register_ges_track_type_select_result (GType * id)
{
  static const GFlagsValue values[] = {
    {C_ENUM (GES_TRACK_TYPE_UNKNOWN), "GES_TRACK_TYPE_UNKNOWN", "unknown"},
    {C_ENUM (GES_TRACK_TYPE_AUDIO), "GES_TRACK_TYPE_AUDIO", "audio"},
    {C_ENUM (GES_TRACK_TYPE_VIDEO), "GES_TRACK_TYPE_VIDEO", "video"},
    {C_ENUM (GES_TRACK_TYPE_TEXT), "GES_TRACK_TYPE_TEXT", "text"},
    {C_ENUM (GES_TRACK_TYPE_CUSTOM), "GES_TRACK_TYPE_CUSTOM", "custom"},
    {0, NULL, NULL}
  };

  *id = g_flags_register_static ("GESTrackType", values);
}

GType
ges_track_type_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_ges_track_type_select_result, &id);
  return id;
}
