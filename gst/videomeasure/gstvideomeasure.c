/* GStreamer
 * Copyright (C) <2009> Руслан Ижбулатов <lrn1986 _at_ gmail _dot_ com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst-i18n-plugin.h>

#include "gstvideomeasure.h"
#include "gstvideomeasure_ssim.h"
#include "gstvideomeasure_collector.h"

GstEvent *
gst_event_new_measured (guint64 framenumber, GstClockTime timestamp,
    const gchar * metric, const GValue * mean, const GValue * lowest,
    const GValue * highest)
{
  GstStructure *str = gst_structure_new (GST_EVENT_VIDEO_MEASURE,
      "event", G_TYPE_STRING, "frame-measured",
      "offset", G_TYPE_UINT64, framenumber,
      "timestamp", GST_TYPE_CLOCK_TIME, timestamp,
      "metric", G_TYPE_STRING, metric,
      NULL);
  gst_structure_set_value (str, "mean", mean);
  gst_structure_set_value (str, "lowest", lowest);
  gst_structure_set_value (str, "highest", highest);
  return gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, str);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res;

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

  res = gst_element_register (plugin, "ssim", GST_RANK_NONE, GST_TYPE_SSIM);

  res &= gst_element_register (plugin, "measurecollector", GST_RANK_NONE,
      GST_TYPE_MEASURE_COLLECTOR);

  return res;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videomeasure,
    "Various video measurers",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
