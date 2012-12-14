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
 * gst-launch videotestsrc num-buffers=2 ! "video/x-raw,width=100,height=100,framerate=10/1" ! autovideoconvert ! "video/x-raw-bayer,width=100,height=100,format=bggr,framerate=10/1" ! fakesink -v
 * if bayer2rgb is present
 * gst-launch videotestsrc num-buffers=2 ! "video/x-raw-bayer,width=100,height=100,format=bggr,framerate=10/1" ! autovideoconvert ! "video/x-raw,width=100,height=100,framerate=10/1" ! fakesink -v
 * test with videoconvert
 * gst-launch videotestsrc num-buffers=2 ! "video/x-raw,format=RGBx,width=100,height=100,framerate=10/1" ! autovideoconvert ! "video/x-raw,format=RGB16,width=100,height=100,framerate=10/1" ! fakesink -v
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstautovideoconvert.h"

GST_DEBUG_CATEGORY (autovideoconvert_debug);
#define GST_CAT_DEFAULT (autovideoconvert_debug)

static GMutex factories_mutex;
static guint32 factories_cookie = 0;    /* Cookie from last time when factories was updated */
static GList *factories = NULL; /* factories we can use for selecting elements */

/* element factory information */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


static GstStateChangeReturn gst_auto_video_convert_change_state (GstElement *
    element, GstStateChange transition);

void gst_auto_video_convert_update_factory_list (GstAutoVideoConvert *
    autovideoconvert);

static gboolean
gst_auto_video_convert_element_filter (GstPluginFeature * feature,
    GstAutoVideoConvert * autovideoconvert)
{
  const gchar *klass;

  /* we only care about element factories */
  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  klass = gst_element_factory_get_metadata (GST_ELEMENT_FACTORY_CAST (feature),
      GST_ELEMENT_METADATA_KLASS);
  /* only select color space converter */
  if (strstr (klass, "Filter") &&
      strstr (klass, "Converter") && strstr (klass, "Video")) {
    GST_DEBUG_OBJECT (autovideoconvert,
        "gst_auto_video_convert_element_filter found %s\n",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE_CAST (feature)));
    return TRUE;
  }
  return FALSE;
}


static GList *
gst_auto_video_convert_create_factory_list (GstAutoVideoConvert *
    autovideoconvert)
{
  GList *result = NULL;

  /* get the feature list using the filter */
  result = gst_registry_feature_filter (gst_registry_get (),
      (GstPluginFeatureFilter) gst_auto_video_convert_element_filter,
      FALSE, autovideoconvert);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);

  return result;
}

void
gst_auto_video_convert_update_factory_list (GstAutoVideoConvert *
    autovideoconvert)
{
  /* use a static mutex to protect factories list and factories cookie */
  g_mutex_lock (&factories_mutex);

  /* test if a factories list already exist or not */
  if (!factories) {
    /* no factories list create it */
    factories_cookie =
        gst_registry_get_feature_list_cookie (gst_registry_get ());
    factories = gst_auto_video_convert_create_factory_list (autovideoconvert);
  } else {
    /* a factories list exist but is it up to date? */
    if (factories_cookie !=
        gst_registry_get_feature_list_cookie (gst_registry_get ())) {
      /* we need to update the factories list */
      /* first free the old one */
      gst_plugin_feature_list_free (factories);
      /* then create an updated one */
      factories_cookie =
          gst_registry_get_feature_list_cookie (gst_registry_get ());
      factories = gst_auto_video_convert_create_factory_list (autovideoconvert);
    }
  }

  g_mutex_unlock (&factories_mutex);
}

G_DEFINE_TYPE (GstAutoVideoConvert, gst_auto_video_convert, GST_TYPE_BIN);

static void
gst_auto_video_convert_class_init (GstAutoVideoConvertClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (autovideoconvert_debug, "autovideoconvert", 0,
      "Auto color space converter");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "Select color space convertor based on caps", "Generic/Bin",
      "Selects the right color space convertor based on the caps",
      "Benjamin Gaignard <benjamin.gaignard@stericsson.com>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_auto_video_convert_change_state);

}

static gboolean
gst_auto_video_convert_add_autoconvert (GstAutoVideoConvert * autovideoconvert)
{
  GstPad *pad;

  if (autovideoconvert->autoconvert)
    return TRUE;

  autovideoconvert->autoconvert =
      gst_element_factory_make ("autoconvert", "autoconvertchild");
  if (!autovideoconvert->autoconvert) {
    GST_ERROR_OBJECT (autovideoconvert,
        "Could not create autoconvert instance");
    return FALSE;
  }

  /* first add autoconvert in bin */
  gst_bin_add (GST_BIN (autovideoconvert),
      gst_object_ref (autovideoconvert->autoconvert));

  /* get sinkpad and link it to ghost sink pad */
  pad = gst_element_get_static_pad (autovideoconvert->autoconvert, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (autovideoconvert->sinkpad),
      pad);
  gst_object_unref (pad);

  /* get srcpad and link it to ghost src pad */
  pad = gst_element_get_static_pad (autovideoconvert->autoconvert, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (autovideoconvert->srcpad), pad);
  gst_object_unref (pad);

  return TRUE;
}

static void
gst_auto_video_convert_remove_autoconvert (GstAutoVideoConvert *
    autovideoconvert)
{
  if (!autovideoconvert->autoconvert)
    return;

  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (autovideoconvert->srcpad),
      NULL);
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (autovideoconvert->sinkpad),
      NULL);

  gst_bin_remove (GST_BIN (autovideoconvert), autovideoconvert->autoconvert);
  gst_object_unref (autovideoconvert->autoconvert);
  autovideoconvert->autoconvert = NULL;
}

static void
gst_auto_video_convert_init (GstAutoVideoConvert * autovideoconvert)
{
  GstPadTemplate *pad_tmpl;

  /* get sink pad template */
  pad_tmpl = gst_static_pad_template_get (&sinktemplate);
  autovideoconvert->sinkpad =
      gst_ghost_pad_new_no_target_from_template ("sink", pad_tmpl);
  /* add sink ghost pad */
  gst_element_add_pad (GST_ELEMENT (autovideoconvert),
      autovideoconvert->sinkpad);
  gst_object_unref (pad_tmpl);

  /* get src pad template */
  pad_tmpl = gst_static_pad_template_get (&srctemplate);
  autovideoconvert->srcpad =
      gst_ghost_pad_new_no_target_from_template ("src", pad_tmpl);
  /* add src ghost pad */
  gst_element_add_pad (GST_ELEMENT (autovideoconvert),
      autovideoconvert->srcpad);
  gst_object_unref (pad_tmpl);

  return;
}

static GstStateChangeReturn
gst_auto_video_convert_change_state (GstElement * element,
    GstStateChange transition)
{
  GstAutoVideoConvert *autovideoconvert = GST_AUTO_VIDEO_CONVERT (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      /* create and add autoconvert in bin */
      if (!gst_auto_video_convert_add_autoconvert (autovideoconvert)) {
        ret = GST_STATE_CHANGE_FAILURE;
        return ret;
      }
      /* get an updated list of factories */
      gst_auto_video_convert_update_factory_list (autovideoconvert);
      GST_DEBUG_OBJECT (autovideoconvert, "set factories list");
      /* give factory list to autoconvert */
      g_object_set (GST_ELEMENT (autovideoconvert->autoconvert), "factories",
          factories, NULL);
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_auto_video_convert_parent_class)->change_state
      (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
    {
      gst_auto_video_convert_remove_autoconvert (autovideoconvert);
      break;
    }
    default:
      break;
  }

  return ret;
}
