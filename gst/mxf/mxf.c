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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>

#include "mxfquark.h"
#include "mxfdemux.h"
#include "mxfmux.h"
#include "mxfdms1.h"
#include "mxfaes-bwf.h"
#include "mxfalaw.h"
#include "mxfd10.h"
#include "mxfdv-dif.h"
#include "mxfjpeg2000.h"
#include "mxfmpeg.h"
#include "mxfup.h"
#include "mxfvc3.h"

GST_DEBUG_CATEGORY (mxf_debug);
#define GST_CAT_DEFAULT mxf_debug

static void
mxf_init (void)
{
  gst_tag_register (GST_TAG_MXF_UMID, GST_TAG_FLAG_META,
      G_TYPE_STRING, "UMID", "Unique Material Identifier", NULL);
  gst_tag_register (GST_TAG_MXF_STRUCTURE, GST_TAG_FLAG_META,
      GST_TYPE_STRUCTURE, "Structure", "Structural metadata of "
      "the MXF file", NULL);
  gst_tag_register (GST_TAG_MXF_DESCRIPTIVE_METADATA_FRAMEWORK,
      GST_TAG_FLAG_META, GST_TYPE_STRUCTURE, "DM Framework",
      "Descriptive metadata framework", NULL);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (mxf_debug, "mxf", 0, "MXF");

  mxf_init ();
  mxf_quark_initialize ();
  mxf_metadata_init_types ();
  mxf_dms1_initialize ();
  mxf_aes_bwf_init ();
  mxf_alaw_init ();
  mxf_d10_init ();
  mxf_dv_dif_init ();
  mxf_jpeg2000_init ();
  mxf_mpeg_init ();
  mxf_up_init ();
  mxf_vc3_init ();

  /* mxfmux is disabled for now - it compiles but is completely untested */
  if (!gst_element_register (plugin, "mxfdemux", GST_RANK_PRIMARY,
          GST_TYPE_MXF_DEMUX))
    /* || !gst_element_register (plugin, "mxfmux", GST_RANK_PRIMARY, GST_TYPE_MXF_MUX)) */
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mxf,
    "MXF plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
