/* Gnonlin
 * Copyright (C) <2001> Wim Taymans <wim.taymans@gmail.com>
 *               <2004-2008> Edward Hervey <bilboed@bilboed.com>
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

#include "gnl.h"

struct _elements_entry
{
  const gchar *name;
    GType (*type) (void);
};

static struct _elements_entry _elements[] = {
  {"gnlsource", gnl_source_get_type},
  {"gnlcomposition", gnl_composition_get_type},
  {"gnloperation", gnl_operation_get_type},
  {"gnlurisource", gnl_urisource_get_type},
  {NULL, 0}
};

static gboolean
plugin_init (GstPlugin * plugin)
{
  gint i = 0;

  for (; _elements[i].name; i++)
    if (!(gst_element_register (plugin,
                _elements[i].name, GST_RANK_NONE, (_elements[i].type) ())))
      return FALSE;

  gnl_init_ghostpad_category ();

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gnonlin,
    "Standard elements for non-linear multimedia editing",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
