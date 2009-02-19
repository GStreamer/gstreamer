/*
 * This library is licensed under 2 different licenses and you
 * can choose to use it under the terms of either one of them. The
 * two licenses are the MPL 1.1 and the LGPL.
 *
 * MPL:
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * LGPL:
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
 *
 * The Original Code is Fluendo MPEG Demuxer plugin.
 *
 * The Initial Developer of the Original Code is Fluendo, S.L.
 * Portions created by Fluendo, S.L. are Copyright (C) 2005
 * Fluendo, S.L. All Rights Reserved.
 *
 * Contributor(s): Wim Taymans <wim@fluendo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpegdemux.h"
#include "gstmpegtsdemux.h"
#include "mpegtsparse.h"
#include "mpegtspacketizer.h"

GST_DEBUG_CATEGORY_EXTERN (gstflupesfilter_debug);
GST_DEBUG_CATEGORY_EXTERN (gstflusectionfilter_debug);
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gstflupesfilter_debug, "mpegpesfilter", 0,
      "MPEG-TS/PS PES filter output");
  GST_DEBUG_CATEGORY_INIT (gstflusectionfilter_debug, "mpegsectionfilter", 0,
      "MPEG-TS Section filter output");

  if (!gst_flups_demux_plugin_init (plugin))
    return FALSE;
  if (!gst_mpegts_demux_plugin_init (plugin))
    return FALSE;
  if (!gst_mpegtsparse_plugin_init (plugin))
    return FALSE;
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpegdemux2",
    "MPEG demuxers",
    plugin_init, VERSION,
    GST_LICENSE_UNKNOWN, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
