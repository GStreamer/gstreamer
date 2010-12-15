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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/*
 * test autocolorspace:
 * if rgb2bayer is present
 * gst-launch videotestsrc num-buffers=2 ! "video/x-raw-rgb,width=100,height=100,framerate=10/1" ! autocolorspace ! "video/x-raw-bayer,width=100,height=100,format=bggr,framerate=10/1" ! fakesink -v
 * if bayer2rgb is present
 * gst-launch videotestsrc num-buffers=2 ! "video/x-raw-bayer,width=100,height=100,format=bggr,framerate=10/1" ! autocolorspace ! "video/x-raw-rgb,width=100,height=100,framerate=10/1" ! fakesink -v
 * test with ffmpegcolorspace
 * gst-launch videotestsrc num-buffers=2 ! "video/x-raw-rgb,bpp=32,width=100,height=100,framerate=10/1" ! autocolorspace ! "video/x-raw-rgb,bpp=16,width=100,height=100,framerate=10/1" ! fakesink -v
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstautocolorspace.h"

GST_DEBUG_CATEGORY (autocolorspace_debug);
#define GST_CAT_DEFAULT (autocolorspace_debug)

GStaticMutex factories_mutex = G_STATIC_MUTEX_INIT;
guint32 factories_cookie = 0;   /* Cookie from last time when factories was updated */
GList *factories = NULL;        /* factories we can use for selecting elements */

/* element factory information */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


static GstStateChangeReturn gst_auto_color_space_change_state (GstElement *
    element, GstStateChange transition);

void gst_auto_color_space_update_factory_list (GstAutoColorSpace *
    autocolorspace);

static gboolean
gst_auto_color_space_element_filter (GstPluginFeature * feature,
    GstAutoColorSpace * autocolorspace)
{
  const gchar *klass;

  /* we only care about element factories */
  if (G_UNLIKELY (!GST_IS_ELEMENT_FACTORY (feature)))
    return FALSE;

  klass = gst_element_factory_get_klass (GST_ELEMENT_FACTORY_CAST (feature));
  /* only select color space converter */
  if (strstr (klass, "Filter") &&
      strstr (klass, "Converter") && strstr (klass, "Video")) {
    GST_DEBUG_OBJECT (autocolorspace,
        "gst_auto_color_space_element_filter found %s\n",
        gst_plugin_feature_get_name (GST_PLUGIN_FEATURE_CAST (feature)));
    return TRUE;
  }
  return FALSE;
}


static GList *
gst_auto_color_space_create_factory_list (GstAutoColorSpace * autocolorspace)
{
  GList *result = NULL;

  /* get the feature list using the filter */
  result = gst_default_registry_feature_filter ((GstPluginFeatureFilter)
      gst_auto_color_space_element_filter, FALSE, autocolorspace);

  /* sort on rank and name */
  result = g_list_sort (result, gst_plugin_feature_rank_compare_func);

  return result;
}

void
gst_auto_color_space_update_factory_list (GstAutoColorSpace * autocolorspace)
{
  /* use a static mutex to protect factories list and factories cookie */
  g_static_mutex_lock (&factories_mutex);

  /* test if a factories list already exist or not */
  if (!factories) {
    /* no factories list create it */
    factories_cookie = gst_default_registry_get_feature_list_cookie ();
    factories = gst_auto_color_space_create_factory_list (autocolorspace);
  } else {
    /* a factories list exist but is it up to date? */
    if (factories_cookie != gst_default_registry_get_feature_list_cookie ()) {
      /* we need to update the factories list */
      /* first free the old one */
      gst_plugin_feature_list_free (factories);
      /* then create an updated one */
      factories_cookie = gst_default_registry_get_feature_list_cookie ();
      factories = gst_auto_color_space_create_factory_list (autocolorspace);
    }
  }

  g_static_mutex_unlock (&factories_mutex);
}

GST_BOILERPLATE (GstAutoColorSpace, gst_auto_color_space, GstBin, GST_TYPE_BIN);

static void
gst_auto_color_space_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details_simple (element_class,
      "Select color space convertor based on caps", "Generic/Bin",
      "Selects the right color space convertor based on the caps",
      "Benjamin Gaignard <benjamin.gaignard@stericsson.com>");
}

static void
gst_auto_color_space_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_auto_color_space_class_init (GstAutoColorSpaceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_auto_color_space_dispose);

  GST_DEBUG_CATEGORY_INIT (autocolorspace_debug, "autocolorspace", 0,
      "Auto color space converter");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_auto_color_space_change_state);

}

static void
gst_auto_color_space_add_autoconvert (GstAutoColorSpace * autocolorspace)
{
  GstPad *pad;

  autocolorspace->autoconvert =
      gst_element_factory_make ("autoconvert", "autoconvertchild");

  /* first add autoconvert in bin */
  gst_bin_add (GST_BIN (autocolorspace), autocolorspace->autoconvert);

  /* get sinkpad and link it to ghost sink pad */
  pad = gst_element_get_static_pad (autocolorspace->autoconvert, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (autocolorspace->sinkpad), pad);
  gst_object_unref (pad);

  /* get srcpad and link it to ghost src pad */
  pad = gst_element_get_static_pad (autocolorspace->autoconvert, "src");
  gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (autocolorspace->srcpad), pad);
  gst_object_unref (pad);
}

static void
gst_auto_color_space_init (GstAutoColorSpace * autocolorspace,
    GstAutoColorSpaceClass * klass)
{
  GstPadTemplate *pad_tmpl;

  /* get sink pad template */
  pad_tmpl = gst_static_pad_template_get (&sinktemplate);
  autocolorspace->sinkpad =
      gst_ghost_pad_new_no_target_from_template ("sink", pad_tmpl);
  /* add sink ghost pad */
  gst_element_add_pad (GST_ELEMENT (autocolorspace), autocolorspace->sinkpad);

  /* get src pad template */
  pad_tmpl = gst_static_pad_template_get (&srctemplate);
  autocolorspace->srcpad =
      gst_ghost_pad_new_no_target_from_template ("src", pad_tmpl);
  /* add src ghost pad */
  gst_element_add_pad (GST_ELEMENT (autocolorspace), autocolorspace->srcpad);

  return;
}

static GstStateChangeReturn
gst_auto_color_space_change_state (GstElement * element,
    GstStateChange transition)
{
  GstAutoColorSpace *autocolorspace = GST_AUTO_COLOR_SPACE (element);
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    {
      /* create and add autoconvert in bin */
      gst_auto_color_space_add_autoconvert (autocolorspace);
      /* get an updated list of factories */
      gst_auto_color_space_update_factory_list (autocolorspace);
      GST_DEBUG_OBJECT (autocolorspace, "set factories list");
      /* give factory list to autoconvert */
      g_object_set (GST_ELEMENT (autocolorspace->autoconvert), "factories",
          factories, NULL);
      /* synchronize autoconvert state with parent state */
      gst_element_sync_state_with_parent (autocolorspace->autoconvert);
      break;
    }
    default:
      break;
  }

  return ret;
}
