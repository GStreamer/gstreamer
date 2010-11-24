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
 * The gstviewfinderbin element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! viewfinderbin
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstviewfinderbin.h"

/* prototypes */


enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

/* class initialization */

GST_BOILERPLATE (GstViewfinderBin, gst_viewfinder_bin, GstBin, GST_TYPE_BIN);

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
  GstElementClass *element_class;

  element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_viewfinder_bin_change_state);
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
  GstElement *csp;
  GstElement *videoscale;
  GstElement *sink;
  GstPad *pad = NULL;

  if (vfbin->elements_created)
    return TRUE;

  /* create elements */
  csp = gst_element_factory_make ("ffmpegcolorspace", "vfbin-csp");
  if (!csp)
    goto error;

  videoscale = gst_element_factory_make ("videoscale", "vfbin-videoscale");
  if (!videoscale)
    goto error;

  sink = gst_element_factory_make ("autovideosink", "vfbin-sink");
  if (!sink)
    goto error;

  /* add and link */
  gst_bin_add_many (GST_BIN_CAST (vfbin), csp, videoscale, sink, NULL);
  if (!gst_element_link_many (csp, videoscale, sink, NULL))
    goto error;

  /* add ghostpad */
  pad = gst_element_get_static_pad (csp, "sink");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (vfbin->ghostpad), pad))
    goto error;

  vfbin->elements_created = TRUE;
  return TRUE;

error:
  if (pad)
    gst_object_unref (pad);
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

gboolean
gst_viewfinder_bin_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "viewfinderbin", GST_RANK_NONE,
      gst_viewfinder_bin_get_type ());
}
