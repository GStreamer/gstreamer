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

gboolean
gst_xvid_init (void)
{
  XVID_INIT_PARAM xinit;
  gint ret;
  static gboolean is_init = FALSE;

  /* only init once */
  if (is_init == TRUE) {
    return TRUE;
  }

  /* set up xvid initially (function pointers, CPU flags) */
  memset(&xinit, 0, sizeof(XVID_INIT_PARAM));
  xinit.cpu_flags = 0;
  if ((ret = xvid_init(NULL, 0, &xinit, NULL)) != XVID_ERR_OK) {
    g_warning("Failed to initialize XviD: %s (%d)",
	      gst_xvid_error(ret), ret);
    return FALSE;
  }
  
  if (xinit.api_version != API_VERSION) {
    g_warning("Xvid API version mismatch! %d.%d (that's us) != %d.%d (lib)",
              (API_VERSION >> 8) & 0xff, API_VERSION & 0xff,
              (xinit.api_version >> 8) & 0xff, xinit.api_version & 0xff);
    return FALSE;
  }

  is_init = TRUE;
  return TRUE;
}

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
plugin_init (GstPlugin *plugin)
{
  return (gst_element_register (plugin, "xvidenc",
				GST_RANK_NONE, GST_TYPE_XVIDENC) &&
	  gst_element_register (plugin, "xviddec",
				GST_RANK_NONE, GST_TYPE_XVIDDEC));
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "xvid",
  "XVid plugin library",
  plugin_init,
  VERSION,
  "GPL",
  GST_PACKAGE,
  GST_ORIGIN)
