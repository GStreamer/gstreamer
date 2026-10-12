/*
 * GStreamer
 * Copyright (C) 2026 Vivia Nikolaidou <vivia@ahiru.eu>
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

#include "gstvipsenumtypes.h"

static GType
gst_vips_interesting_get_type_basic (void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    {GST_VIPS_INTERESTING_NONE, "None", "none"},
    {GST_VIPS_INTERESTING_CENTRE, "Centre", "centre"},
    {GST_VIPS_INTERESTING_ENTROPY, "Entropy", "entropy"},
    {GST_VIPS_INTERESTING_ATTENTION, "Attention", "attention"},
    {GST_VIPS_INTERESTING_LOW, "Low", "low"},
    {GST_VIPS_INTERESTING_HIGH, "High", "high"},
    {0, NULL, NULL},
  };

  if (g_once_init_enter (&type)) {
    GType newtype = g_enum_register_static ("GstVipsInteresting", values);
    g_once_init_leave (&type, newtype);
  }
  return type;
}

GType
gst_vips_interesting_get_type (void)
{
  return gst_vips_interesting_get_type_basic ();
}
