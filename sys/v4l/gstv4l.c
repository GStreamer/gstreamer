/* G-Streamer video4linux plugins
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstv4lelement.h"
#include "gstv4lsrc.h"
#include "gstv4lmjpegsrc.h"
#include "gstv4lmjpegsink.h"

static gboolean
plugin_init (GModule   *module,
	     GstPlugin *plugin)
{
  if (!gst_v4lelement_factory_init (plugin) ||
      !gst_v4lsrc_factory_init (plugin) ||
      !gst_v4lmjpegsrc_factory_init (plugin) ||
      !gst_v4lmjpegsink_factory_init (plugin)) {
    return FALSE;
  }

  return TRUE;

}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "video4linux",
  plugin_init
};
