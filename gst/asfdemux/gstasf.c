/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst.h>
#include <gst/riff/riff-read.h>
#include "gst/gst-i18n-plugin.h"

#include "gstasfdemux.h"
#include "gstrtspwms.h"
#include "gstrtpasfdepay.h"

/* #include "gstasfmux.h" */

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (asfdemux_dbg, "asfdemux", 0, "asf demuxer element");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

  gst_riff_init ();

  if (!gst_element_register (plugin, "asfdemux", GST_RANK_SECONDARY,
          GST_TYPE_ASF_DEMUX)) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "rtspwms", GST_RANK_SECONDARY,
          GST_TYPE_RTSP_WMS)) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "rtpasfdepay", GST_RANK_MARGINAL,
          GST_TYPE_RTP_ASF_DEPAY)) {
    return FALSE;
  }
/*
  if (!gst_element_register (plugin, "asfmux", GST_RANK_NONE, GST_TYPE_ASFMUX))
    return FALSE;
*/

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    asf,
    "Demuxes and muxes audio and video in Microsofts ASF format",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
