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


#include <gstrtjpegenc.h>
#include <gstrtjpegdec.h>

extern GstElementDetails gst_rtjpegenc_details;
extern GstElementDetails gst_rtjpegdec_details;

GstTypeDefinition rtjpegdefinition = {
  "rtjpeg_video/rtjpeg",
  "video/rtjpeg",
  ".rtj",
  NULL,
};

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *enc, *dec;

  gst_plugin_set_longname(plugin,"Justin Schoeman's RTjpeg codec and \
conversion utilities");

  /* create an elementfactory for the rtjpegenc element */
  enc = gst_element_factory_new("rtjpegenc",GST_TYPE_RTJPEGENC,
                               &gst_rtjpegenc_details);
  g_return_val_if_fail(enc != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (enc));

  /* create an elementfactory for the rtjpegdec element */
  dec = gst_element_factory_new("rtjpegdec",GST_TYPE_RTJPEGDEC,
                               &gst_rtjpegdec_details);
  g_return_val_if_fail(dec != NULL, FALSE);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (dec));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "rtjpeg",
  plugin_init
};
