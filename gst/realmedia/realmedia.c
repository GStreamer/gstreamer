/* GStreamer
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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
#  include "config.h"
#endif

#include "rmdemux.h"
#include "rademux.h"
#include "rdtdepay.h"
#include "rdtmanager.h"
#include "rtspreal.h"
#include "pnmsrc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_rmdemux_plugin_init (plugin))
    return FALSE;

  if (!gst_rademux_plugin_init (plugin))
    return FALSE;

  if (!gst_rdt_depay_plugin_init (plugin))
    return FALSE;

  if (!gst_rdt_manager_plugin_init (plugin))
    return FALSE;

  if (!gst_rtsp_real_plugin_init (plugin))
    return FALSE;

  if (!gst_pnm_src_plugin_init (plugin))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    realmedia,
    "RealMedia support plugins",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
