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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstautodetect.h"
#include "gstautoaudiosink.h"
#include "gstautoaudiosrc.h"
#include "gstautovideosink.h"
#include "gstautovideosrc.h"

GST_DEBUG_CATEGORY (autodetect_debug);

GstElement *
gst_auto_create_element_with_pretty_name (GstElement * autodetect,
    GstElementFactory * factory, const gchar * suffix)
{
  GstElement *element;
  gchar *name, *marker;

  marker = g_strdup (GST_OBJECT_NAME (factory));
  if (g_str_has_suffix (marker, suffix))
    marker[strlen (marker) - 4] = '\0';
  if (g_str_has_prefix (marker, "gst"))
    memmove (marker, marker + 3, strlen (marker + 3) + 1);
  name = g_strdup_printf ("%s-actual-%s-%s", GST_OBJECT_NAME (autodetect),
      suffix, marker);
  g_free (marker);

  element = gst_element_factory_create (factory, name);
  g_free (name);

  return element;
}



static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (autodetect_debug, "autodetect", 0,
      "Autodetection audio/video output wrapper elements");

  return gst_element_register (plugin, "autovideosink",
      GST_RANK_NONE, GST_TYPE_AUTO_VIDEO_SINK) &&
      gst_element_register (plugin, "autovideosrc",
      GST_RANK_NONE, GST_TYPE_AUTO_VIDEO_SRC) &&
      gst_element_register (plugin, "autoaudiosink",
      GST_RANK_NONE, GST_TYPE_AUTO_AUDIO_SINK) &&
      gst_element_register (plugin, "autoaudiosrc",
      GST_RANK_NONE, GST_TYPE_AUTO_AUDIO_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    autodetect,
    "Plugin contains auto-detection plugins for video/audio in- and outputs",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
