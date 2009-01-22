/* GStreamer
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
#include <config.h>
#endif

#include <gst/gst.h>

#include "mxfdemux.h"
#include "mxfaes-bwf.h"
#include "mxfmpeg.h"
#include "mxfdv-dif.h"
#include "mxfalaw.h"
#include "mxfjpeg2000.h"
#include "mxfd10.h"
#include "mxfup.h"
#include "mxfvc3.h"
#include "mxfdms1.h"

GST_DEBUG_CATEGORY (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  mxf_metadata_init_types ();
  mxf_aes_bwf_init ();
  mxf_mpeg_init ();
  mxf_dv_dif_init ();
  mxf_alaw_init ();
  mxf_jpeg2000_init ();
  mxf_d10_init ();
  mxf_up_init ();
  mxf_vc3_init ();
  mxf_dms1_initialize ();

  if (!gst_element_register (plugin, "mxfdemux", GST_RANK_PRIMARY,
          GST_TYPE_MXF_DEMUX))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (mxf_debug, "mxf", 0, "MXF");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mxf",
    "MXF plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
