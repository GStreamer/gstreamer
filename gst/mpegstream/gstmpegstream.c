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


#include "gstmpegparse.h"
#include "gstmpegdemux.h"
#include "gstrfc2250enc.h"

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  /* mpegdemux needs the bytestream package */
  if (!gst_library_load ("gstbytestream")) {
      gst_info ("mpeg_demux:: could not load support library: 'gstbytestream'\n");    return FALSE;
  }

  /* short-circuit here; this is potentially dangerous since if the second
   * or third init fails then the whole plug-in will be placed on the register
   * stack again and the first _init will be called more than once
   * which GType initialization doesn't like */
  if (!gst_mpeg_parse_plugin_init (module, plugin)) return FALSE;
  if (!gst_mpeg_demux_plugin_init (module, plugin)) return FALSE;
  if (!gst_rfc2250_enc_plugin_init (module, plugin)) return FALSE;

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mpegstream",
  plugin_init
};
