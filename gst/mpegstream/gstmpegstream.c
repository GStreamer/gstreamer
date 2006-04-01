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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstmpegparse.h"
#include "gstmpegdemux.h"
#include "gstdvddemux.h"
#include "gstrfc2250enc.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* short-circuit here; this is potentially dangerous since if the second
   * or third init fails then the whole plug-in will be placed on the register
   * stack again and the first _init will be called more than once
   * and wtay wants to use dlclose at some point in the future */

  if (!gst_mpeg_parse_plugin_init (plugin) || !gst_mpeg_demux_plugin_init (plugin) || !gst_dvd_demux_plugin_init (plugin)       /*||
                                                                                                                                   !gst_rfc2250_enc_plugin_init (plugin) */ )
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpegstream",
    "MPEG system stream parser",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
