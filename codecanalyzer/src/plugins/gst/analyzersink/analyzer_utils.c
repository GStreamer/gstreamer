/*
 * Copyright (c) 2013, Intel Corporation.
 * Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include "analyzer_utils.h"
#include "gstanalyzersink.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret = FALSE;

  ret |= gst_element_register (plugin, "analyzersink",
      GST_RANK_PRIMARY + 1, GST_TYPE_ANALYZER_SINK);

  return ret;
}

gboolean
analyzer_sink_register_static ()
{
  return gst_plugin_register_static (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "analzsersink",
      "sink element to dump parsed information to the xml",
      plugin_init, VERSION, "LGPL", "codecanalyzer", PACKAGE_NAME, PACKAGE_URL);
}
