/* GStreamer
 * Copyright 2010 ST-Ericsson SA
 *  @author: Benjamin Gaignard <benjamin.gaignard@stericsson.com>
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
/*
 * test autovideoconvert:
 * if rgb2bayer is present
 * gst-launch-1.0 videotestsrc num-buffers=2 ! "video/x-raw,width=100,height=100,framerate=10/1" ! autovideoconvert ! "video/x-bayer,width=100,height=100,format=bggr,framerate=10/1" ! fakesink -v
 * if bayer2rgb is present
 * gst-launch-1.0 videotestsrc num-buffers=2 ! "video/x-bayer,width=100,height=100,format=bggr,framerate=10/1" ! autovideoconvert ! "video/x-raw,width=100,height=100,framerate=10/1" ! fakesink -v
 * test with videoconvert
 * gst-launch-1.0 videotestsrc num-buffers=2 ! "video/x-raw,format=RGBx,width=100,height=100,framerate=10/1" ! autovideoconvert ! "video/x-raw,format=RGB16,width=100,height=100,framerate=10/1" ! fakesink -v
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstautovideoconvert.h"

GST_DEBUG_CATEGORY (autovideoconvert_debug);
#define GST_CAT_DEFAULT (autovideoconvert_debug)

struct _GstAutoVideoConvert
{
  GstAutoConvert parent;
};

G_DEFINE_TYPE (GstAutoVideoConvert, gst_auto_video_convert,
    GST_TYPE_AUTO_CONVERT);

GST_ELEMENT_REGISTER_DEFINE (autovideoconvert, "autovideoconvert",
    GST_RANK_NONE, gst_auto_video_convert_get_type ());

static gboolean
gst_auto_video_convert_element_filter (GstPluginFeature * feature,
    GstAutoVideoConvert * autovideoconvert)
{
  const gchar *klass;

  /* we only care about element factories */
  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  if (!g_strcmp0 (GST_OBJECT_NAME (feature), "autovideoconvert"))
    return FALSE;

  klass = gst_element_factory_get_metadata (GST_ELEMENT_FACTORY_CAST (feature),
      GST_ELEMENT_METADATA_KLASS);
  /* only select color space converter */
  if (strstr (klass, "Colorspace") &&
      strstr (klass, "Converter") &&
      strstr (klass, "Video")) {
    GST_DEBUG_OBJECT (autovideoconvert,
        "gst_auto_video_convert_element_filter found %s",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE_CAST (feature)));
    return TRUE;
  }
  return FALSE;
}


static GList *
gst_auto_video_convert_create_factory_list (GstAutoConvert * autoconvert)
{
  GList *result = NULL;

  /* get the feature list using the filter */
  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) gst_auto_video_convert_element_filter,
      FALSE, autoconvert);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);

  return result;
}

static void
gst_auto_video_convert_class_init (GstAutoVideoConvertClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  ((GstAutoConvertClass *) klass)->load_factories =
      gst_auto_video_convert_create_factory_list;
  GST_DEBUG_CATEGORY_INIT (autovideoconvert_debug, "autovideoconvert", 0,
      "Auto color space converter");

  gst_element_class_set_static_metadata (gstelement_class,
      "Select color space converter and scalers based on caps",
      "Bin/Colorspace/Scale/Video/Converter",
      "Selects the right color space converter based on the caps",
      "Benjamin Gaignard <benjamin.gaignard@stericsson.com>");
}

static void
gst_auto_video_convert_init (GstAutoVideoConvert * autovideoconvert)
{
}
