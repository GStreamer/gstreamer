/* GStreamer xvid decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#include <string.h>

#include "gstxviddec.h"
#include "gstxvidenc.h"

gchar *
gst_xvid_error (int errorcode)
{
  gchar *error;

  switch (errorcode) {
    case XVID_ERR_FAIL:
      error = "Operation failed";
      break;
    case XVID_ERR_OK:
      error = "No error";
      break;
    case XVID_ERR_MEMORY:
      error = "Memory error";
      break;
    case XVID_ERR_FORMAT:
      error = "Invalid format";
      break;
    default:
      error = "Unknown error";
      break;
  }

  return error;
}

static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
  return (gst_xviddec_plugin_init(module, plugin) &&
          gst_xvidenc_plugin_init(module, plugin));
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "xvid",
  plugin_init
};
