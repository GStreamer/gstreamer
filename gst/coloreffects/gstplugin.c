/* GStreamer
 * Copyright (C) <2010> Filippo Argiolas <filippo.argiolas@gmail.com>
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
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstcoloreffects.h"
#include "gstchromahold.h"

struct _elements_entry
{
  const gchar *name;
    GType (*type) (void);
};

static const struct _elements_entry _elements[] = {
  {"coloreffects", gst_color_effects_get_type},
  {"chromahold", gst_chroma_hold_get_type},
  {NULL, 0},
};

static gboolean
plugin_init (GstPlugin * plugin)
{
  gint i = 0;

  while (_elements[i].name) {
    if (!gst_element_register (plugin, _elements[i].name,
            GST_RANK_NONE, (_elements[i].type) ()))
      return FALSE;
    i++;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    coloreffects,
    "Color Look-up Table filters",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
