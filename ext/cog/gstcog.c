/* GStreamer
 * Copyright (C) 2007 David Schleef <ds@schleef.org>
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

#include <string.h>

#include <gst/gst.h>
#include <cog/cog.h>

#include "gstjpegdec.h"

//GType gst_dither_get_type (void);
GType gst_deblock_get_type (void);
GType gst_cogdownsample_get_type (void);
GType gst_motion_detect_get_type (void);
GType gst_cogcolorspace_get_type (void);
GType gst_cog_scale_get_type (void);
GType gst_colorconvert_get_type (void);
GType gst_logoinsert_get_type (void);
GType gst_mse_get_type (void);
GType gst_decimate_get_type (void);

static gboolean
plugin_init (GstPlugin * plugin)
{
  cog_init ();

  gst_element_register (plugin, "cogjpegdec", GST_RANK_PRIMARY,
      GST_TYPE_JPEG_DEC);
  //gst_element_register (plugin, "dither", GST_RANK_NONE,
  //      gst_dither_get_type());
  gst_element_register (plugin, "deblock", GST_RANK_NONE,
      gst_deblock_get_type ());
  gst_element_register (plugin, "cogdownsample", GST_RANK_NONE,
      gst_cogdownsample_get_type ());
  gst_element_register (plugin, "motiondetect", GST_RANK_NONE,
      gst_motion_detect_get_type ());
  gst_element_register (plugin, "cogcolorspace", GST_RANK_NONE,
      gst_cogcolorspace_get_type ());
  gst_element_register (plugin, "cogscale", GST_RANK_NONE,
      gst_cog_scale_get_type ());
  gst_element_register (plugin, "colorconvert", GST_RANK_NONE,
      gst_colorconvert_get_type ());
  gst_element_register (plugin, "coglogoinsert", GST_RANK_NONE,
      gst_logoinsert_get_type ());
  gst_element_register (plugin, "cogmse", GST_RANK_NONE, gst_mse_get_type ());
  gst_element_register (plugin, "cogdecimate", GST_RANK_NONE,
      gst_decimate_get_type ());

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "cog",
    "Cog plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
