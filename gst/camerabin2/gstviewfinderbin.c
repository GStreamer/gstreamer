/* GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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
/**
 * SECTION:element-gstviewfinderbin
 *
 * The gstviewfinderbin element is a displaying element for camerabin2.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! viewfinderbin
 * ]|
 * Feeds the viewfinderbin with video test data.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstviewfinderbin.h"

GST_DEBUG_CATEGORY_STATIC (gst_viewfinder_bin_debug);
#define GST_CAT_DEFAULT gst_viewfinder_bin_debug

/* prototypes */


enum
{
  PROP_0,
  PROP_VIDEO_SINK,
};

/* pad templates */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

/* class initialization */

GST_BOILERPLATE (GstViewfinderBin, gst_viewfinder_bin, GstBin, GST_TYPE_BIN);

static void gst_viewfinder_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * spec);
static void gst_viewfinder_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * spec);

static void
gst_viewfinder_bin_set_video_sink (GstViewfinderBin * vfbin, GstElement * sink);


/* Element class functions */
static GstStateChangeReturn
gst_viewfinder_bin_change_state (GstElement * element, GstStateChange trans);

static void
gst_viewfinder_bin_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class, "Viewfinder Bin",
      "Sink/Video", "Viewfinder Bin used in camerabin2",
      "Thiago Santos <thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_viewfinder_bin_class_init (GstViewfinderBinClass * klass)
{
  GObjectClass *gobject_klass;
  GstElementClass *element_class;

  gobject_klass = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_viewfinder_bin_change_state);

  gobject_klass->set_property = gst_viewfinder_bin_set_property;
  gobject_klass->get_property = gst_viewfinder_bin_get_property;

  g_object_class_install_property (gobject_klass, PROP_VIDEO_SINK,
      g_param_spec_object ("video-sink", "Video Sink",
          "the video output element to use (NULL = default)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_viewfinder_bin_init (GstViewfinderBin * viewfinderbin,
    GstViewfinderBinClass * viewfinderbin_class)
{
  viewfinderbin->ghostpad = gst_ghost_pad_new_no_target_from_template ("sink",
      gst_static_pad_template_get (&sink_template));
  gst_element_add_pad (GST_ELEMENT_CAST (viewfinderbin),
      viewfinderbin->ghostpad);
}

static gboolean
gst_viewfinder_bin_create_elements (GstViewfinderBin * vfbin)
{
  GstElement *csp = NULL;
  GstElement *videoscale = NULL;
  GstPad *pad = NULL;
  gboolean added = FALSE;

  GST_DEBUG_OBJECT (vfbin, "Creating internal elements");

  if (!vfbin->elements_created) {
    /* create elements */
    csp = gst_element_factory_make ("ffmpegcolorspace", "vfbin-csp");
    if (!csp)
      goto error;

    videoscale = gst_element_factory_make ("videoscale", "vfbin-videoscale");
    if (!videoscale)
      goto error;

    GST_DEBUG_OBJECT (vfbin, "Internal elements created, proceding to linking");

    /* add and link */
    gst_bin_add_many (GST_BIN_CAST (vfbin), csp, videoscale, NULL);
    added = TRUE;
    if (!gst_element_link (csp, videoscale))
      goto error;

    /* add ghostpad */
    pad = gst_element_get_static_pad (csp, "sink");
    if (!gst_ghost_pad_set_target (GST_GHOST_PAD (vfbin->ghostpad), pad))
      goto error;

    vfbin->elements_created = TRUE;
    GST_DEBUG_OBJECT (vfbin, "Elements succesfully created and linked");
  }

  if (vfbin->video_sink) {
    /* check if we need to replace the current one */
    if (vfbin->user_video_sink && vfbin->video_sink != vfbin->user_video_sink) {
      gst_bin_remove (GST_BIN_CAST (vfbin), vfbin->video_sink);
      gst_object_unref (vfbin->video_sink);
      vfbin->video_sink = NULL;
    }
  }

  if (!vfbin->video_sink) {
    if (vfbin->user_video_sink)
      vfbin->video_sink = gst_object_ref (vfbin->user_video_sink);
    else
      vfbin->video_sink = gst_element_factory_make ("autovideosink",
          "vfbin-sink");

    gst_bin_add (GST_BIN_CAST (vfbin), gst_object_ref (vfbin->video_sink));

    if (!videoscale)
      videoscale = gst_bin_get_by_name (GST_BIN_CAST (vfbin),
          "vfbin-videoscale");

    if (!gst_element_link_pads (videoscale, "src", vfbin->video_sink, "sink")) {
      GST_WARNING_OBJECT (vfbin, "Failed to link the new sink");
    }
  }

  return TRUE;

error:
  GST_WARNING_OBJECT (vfbin, "Creating internal elements failed");
  if (pad)
    gst_object_unref (pad);
  if (!added) {
    if (csp)
      gst_object_unref (csp);
    if (videoscale)
      gst_object_unref (videoscale);
  } else {
    gst_bin_remove_many (GST_BIN_CAST (vfbin), csp, videoscale, NULL);
  }
  return FALSE;
}

static GstStateChangeReturn
gst_viewfinder_bin_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstViewfinderBin *vfbin = GST_VIEWFINDER_BIN_CAST (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_viewfinder_bin_create_elements (vfbin)) {
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, trans);

  switch (trans) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_viewfinder_bin_set_video_sink (GstViewfinderBin * vfbin, GstElement * sink)
{
  GST_INFO_OBJECT (vfbin, "Setting video sink to %" GST_PTR_FORMAT, sink);

  if (vfbin->video_sink != sink) {
    if (sink)
      gst_object_ref_sink (sink);

    if (vfbin->video_sink) {
      gst_bin_remove (GST_BIN_CAST (vfbin), vfbin->video_sink);
      gst_object_unref (vfbin->video_sink);
    }

    vfbin->video_sink = sink;
    if (sink) {
      gst_bin_add (GST_BIN_CAST (vfbin), gst_object_ref (sink));
      if (vfbin->elements_created) {
        GstElement *videoscale = gst_bin_get_by_name (GST_BIN_CAST (vfbin),
            "vfbin-videoscale");

        g_assert (videoscale != NULL);

        if (!gst_element_link_pads (videoscale, "src", sink, "sink")) {
          GST_WARNING_OBJECT (vfbin, "Failed to link the new sink");
        }
      }
    }

  }

  GST_LOG_OBJECT (vfbin, "Video sink is now %" GST_PTR_FORMAT, sink);
}

static void
gst_viewfinder_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstViewfinderBin *vfbin = GST_VIEWFINDER_BIN_CAST (object);

  switch (prop_id) {
    case PROP_VIDEO_SINK:
      gst_viewfinder_bin_set_video_sink (vfbin, g_value_get_object (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_viewfinder_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstViewfinderBin *vfbin = GST_VIEWFINDER_BIN_CAST (object);

  switch (prop_id) {
    case PROP_VIDEO_SINK:
      g_value_set_object (value, vfbin->video_sink);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_viewfinder_bin_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_viewfinder_bin_debug, "viewfinderbin", 0,
      "ViewFinderBin");
  return gst_element_register (plugin, "viewfinderbin", GST_RANK_NONE,
      gst_viewfinder_bin_get_type ());
}
