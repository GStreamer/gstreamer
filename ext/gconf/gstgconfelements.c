/* GStreamer
 * (c) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

#include "gstgconfelements.h"

#include "gstgconfaudiosink.h"
#include "gstgconfaudiosrc.h"
#include "gstgconfvideosink.h"
#include "gstgconfvideosrc.h"

GST_DEBUG_CATEGORY (gconf_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gconf_debug, "gconf", 0,
      "GConf/GStreamer audio/video output wrapper elements");

  if (!gst_element_register (plugin, "gconfvideosink",
          GST_RANK_NONE, GST_TYPE_GCONF_VIDEO_SINK) ||
      !gst_element_register (plugin, "gconfvideosrc",
          GST_RANK_NONE, GST_TYPE_GCONF_VIDEO_SRC) ||
      !gst_element_register (plugin, "gconfaudiosink",
          GST_RANK_NONE, GST_TYPE_GCONF_AUDIO_SINK) ||
      !gst_element_register (plugin, "gconfaudiosrc",
          GST_RANK_NONE, GST_TYPE_GCONF_AUDIO_SRC)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gconfelements",
    "elements wrapping the GStreamer/GConf audio/video output settings",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
