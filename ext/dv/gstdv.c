/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2005> Wim Taymans <wim@fluendo.com>
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

#include "gstdvdec.h"
#include "gstdvdemux.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstRank dvdec_rank;

  if (!gst_element_register (plugin, "dvdemux", GST_RANK_PRIMARY,
          gst_dvdemux_get_type ()))
    return FALSE;

  /* libdv does not correctly play back videos on big-endian machines. also it's
     only optimized properly on x86-32 and x86-64. */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  rank = GST_RANK_PRIMARY;
#else
  rank = GST_RANK_MARGINAL;
#endif

  if (!gst_element_register (plugin, "dvdec", rank, gst_dvdec_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dv",
    "DV demuxer and decoder based on libdv (libdv.sf.net)",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
