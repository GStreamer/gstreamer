/* Real wrapper plugin
 *
 * Copyright (C) Edward Hervey <bilboed@bilboed.com>
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

#include "gstreal.h"
#include "gstrealvideodec.h"
#include "gstrealaudiodec.h"

static gboolean
plugin_init (GstPlugin * p)
{
  if (!gst_element_register (p, "realvideodec", GST_RANK_MARGINAL,
          GST_TYPE_REAL_VIDEO_DEC))
    return FALSE;
  if (!gst_element_register (p, "realaudiodec", GST_RANK_MARGINAL,
          GST_TYPE_REAL_AUDIO_DEC))
    return FALSE;

  gst_plugin_add_dependency_simple (p, NULL, DEFAULT_REAL_CODECS_PATH, NULL,
      GST_PLUGIN_DEPENDENCY_FLAG_NONE);

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, real,
    "Decode REAL streams",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
