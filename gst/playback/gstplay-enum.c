/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#include "gstplay-enum.h"

#define C_ENUM(v) ((gint) v)

static void
register_gst_autoplug_select_result (GType * id)
{
  static const GEnumValue values[] = {
    {C_ENUM (GST_AUTOPLUG_SELECT_TRY), "GST_AUTOPLUG_SELECT_TRY", "try"},
    {C_ENUM (GST_AUTOPLUG_SELECT_EXPOSE), "GST_AUTOPLUG_SELECT_EXPOSE",
          "expose"},
    {C_ENUM (GST_AUTOPLUG_SELECT_SKIP), "GST_AUTOPLUG_SELECT_SKIP", "skip"},
    {0, NULL, NULL}
  };
  *id = g_enum_register_static ("GstAutoplugSelectResult", values);
}

GType
gst_autoplug_select_result_get_type (void)
{
  static GType id;
  static GOnce once = G_ONCE_INIT;

  g_once (&once, (GThreadFunc) register_gst_autoplug_select_result, &id);
  return id;
}
