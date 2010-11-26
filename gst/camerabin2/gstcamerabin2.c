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
 * SECTION:element-gstcamerabin
 *
 * The gstcamerabin element does FIXME stuff.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstcamerabin2.h"

/* prototypes */


enum
{
  PROP_0
};

/********************************
 * Standard GObject boilerplate *
 ********************************/

static GstPipelineClass *parent_class;
static void gst_camera_bin_class_init (GstCameraBinClass * klass);
static void gst_camera_bin_base_init (gpointer klass);
static void gst_camera_bin_init (GstCameraBin * camera);
static void gst_camera_bin_dispose (GObject * object);
static void gst_camera_bin_finalize (GObject * object);

GType
gst_camera_bin_get_type (void)
{
  static GType gst_camera_bin_type = 0;

  if (!gst_camera_bin_type) {
    static const GTypeInfo gst_camera_bin_info = {
      sizeof (GstCameraBinClass),
      (GBaseInitFunc) gst_camera_bin_base_init,
      NULL,
      (GClassInitFunc) gst_camera_bin_class_init,
      NULL,
      NULL,
      sizeof (GstCameraBin),
      0,
      (GInstanceInitFunc) gst_camera_bin_init,
      NULL
    };

    gst_camera_bin_type =
        g_type_register_static (GST_TYPE_PIPELINE, "GstCameraBin2",
        &gst_camera_bin_info, 0);
  }

  return gst_camera_bin_type;
}

/* Element class functions */
static GstStateChangeReturn
gst_camera_bin_change_state (GstElement * element, GstStateChange trans);

static void
gst_camera_bin_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_camera_bin_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_camera_bin_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "CameraBin2",
      "Generic/Bin/Camera", "CameraBin2",
      "Thiago Santos <thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_camera_bin_class_init (GstCameraBinClass * klass)
{
  GObjectClass *object_class;
  GstElementClass *element_class;

  parent_class = g_type_class_peek_parent (klass);
  object_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  object_class->dispose = gst_camera_bin_dispose;
  object_class->finalize = gst_camera_bin_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_camera_bin_change_state);
}

static void
gst_camera_bin_init (GstCameraBin * camerabin)
{
}

/**
 * gst_camera_bin_create_elements:
 * @param camera: the #GstCameraBin
 *
 * Creates all elements inside #GstCameraBin
 *
 * Each of the pads on the camera source is linked as follows:
 * .pad ! queue ! capsfilter ! correspondingbin
 *
 * Where 'correspondingbin' is the bin appropriate for
 * the camera source pad.
 */
static gboolean
gst_camera_bin_create_elements (GstCameraBin * camera)
{
  GstElement *src;
  GstElement *vid;
  GstElement *img;
  GstElement *vf;
  GstElement *vid_queue;
  GstElement *img_queue;
  GstElement *vf_queue;
  GstElement *vid_capsfilter;
  GstElement *img_capsfilter;
  GstElement *vf_capsfilter;

  if (camera->elements_created)
    return TRUE;

  src = gst_element_factory_make ("v4l2camerasrc", "camerasrc");
  vid = gst_element_factory_make ("videorecordingbin", "video-rec-bin");
  img = gst_element_factory_make ("imagecapturebin", "image-cap-bin");
  vf = gst_element_factory_make ("viewfinderbin", "vf-bin");

  vid_queue = gst_element_factory_make ("queue", "video-queue");
  img_queue = gst_element_factory_make ("queue", "image-queue");
  vf_queue = gst_element_factory_make ("queue", "vf-queue");

  vid_capsfilter = gst_element_factory_make ("capsfilter", "video-capsfilter");
  img_capsfilter = gst_element_factory_make ("capsfilter", "image-capsfilter");
  vf_capsfilter = gst_element_factory_make ("capsfilter", "vf-capsfilter");

  gst_bin_add_many (GST_BIN_CAST (camera), src, vid, img, vf, vid_queue,
      img_queue, vf_queue, vid_capsfilter, img_capsfilter, vf_capsfilter, NULL);

  /* Linking can be optimized TODO */
  gst_element_link_many (vid_queue, vid_capsfilter, vid, NULL);
  gst_element_link_many (img_queue, img_capsfilter, img, NULL);
  gst_element_link_many (vf_queue, vf_capsfilter, vf, NULL);
  gst_element_link_pads (src, "vfsrc", vf_queue, "sink");
  gst_element_link_pads (src, "imgsrc", img_queue, "sink");
  gst_element_link_pads (src, "vidsrc", vid_queue, "sink");

  camera->elements_created = TRUE;
  return TRUE;
}

static GstStateChangeReturn
gst_camera_bin_change_state (GstElement * element, GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstCameraBin *camera = GST_CAMERA_BIN_CAST (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_camera_bin_create_elements (camera)) {
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
gst_camera_bin_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "camerabin2", GST_RANK_NONE,
      gst_camera_bin_get_type ());
}
