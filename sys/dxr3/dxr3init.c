/* GStreamer
 * Copyright (C) 2003 Martin Soto <martinsoto@users.sourceforge.net>
 *
 * dxr3init.c: DXR3 plugin initialization.
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

#include "dxr3videosink.h"
#include "dxr3spusink.h"
#include "dxr3audiosink.h"


static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  gboolean ret;

  ret = dxr3videosink_factory_init (plugin);
  g_return_val_if_fail (ret == TRUE, FALSE);

  ret = dxr3spusink_factory_init (plugin);
  g_return_val_if_fail (ret == TRUE, FALSE);

  ret = dxr3audiosink_factory_init (plugin);
  g_return_val_if_fail (ret == TRUE, FALSE);

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "dxr3",
  plugin_init
};

