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
 * SECTION:element-gstvideorecordingbin
 *
 * The gstvideorecordingbin element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc num-buffers=3 ! videorecordingbin
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideorecordingbin.h"

/* prototypes */


enum
{
  PROP_0,
  PROP_LOCATION
};

#define DEFAULT_LOCATION "vidcap"

/* pad templates */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

/* class initialization */

GST_BOILERPLATE (GstVideoRecordingBin, gst_video_recording_bin, GstBin,
    GST_TYPE_BIN);

/* Element class functions */
static GstStateChangeReturn
gst_video_recording_bin_change_state (GstElement * element,
    GstStateChange trans);

static void
gst_video_recording_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoRecordingBin *videobin = GST_VIDEO_RECORDING_BIN_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      videobin->location = g_value_dup_string (value);
      if (videobin->sink) {
        g_object_set (videobin->sink, "location", videobin->location, NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_recording_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoRecordingBin *videobin = GST_VIDEO_RECORDING_BIN_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, videobin->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_recording_bin_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  gst_element_class_set_details_simple (element_class, "Video Recording Bin",
      "Sink/Video", "Video Recording Bin used in camerabin2",
      "Thiago Santos <thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_video_recording_bin_class_init (GstVideoRecordingBinClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_video_recording_bin_set_property;
  gobject_class->get_property = gst_video_recording_bin_get_property;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_video_recording_bin_change_state);

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to save the captured files.",
          DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_video_recording_bin_init (GstVideoRecordingBin * videobin,
    GstVideoRecordingBinClass * videobin_class)
{
  videobin->ghostpad =
      gst_ghost_pad_new_no_target_from_template ("sink",
      gst_static_pad_template_get (&sink_template));
  gst_element_add_pad (GST_ELEMENT_CAST (videobin), videobin->ghostpad);

  videobin->location = g_strdup (DEFAULT_LOCATION);
}

static gboolean
gst_video_recording_bin_create_elements (GstVideoRecordingBin * videobin)
{
  GstElement *colorspace;
  GstElement *encoder;
  GstElement *muxer;
  GstElement *sink;
  GstPad *pad = NULL;

  if (videobin->elements_created)
    return TRUE;

  /* create elements */
  colorspace =
      gst_element_factory_make ("ffmpegcolorspace", "videobin-colorspace");
  if (!colorspace)
    goto error;

  encoder = gst_element_factory_make ("theoraenc", "videobin-encoder");
  if (!encoder)
    goto error;

  muxer = gst_element_factory_make ("oggmux", "videobin->muxer");
  if (!muxer)
    goto error;

  sink = gst_element_factory_make ("filesink", "videobin-sink");
  if (!sink)
    goto error;

  videobin->sink = gst_object_ref (sink);
  g_object_set (sink, "location", videobin->location, "async", FALSE, NULL);

  /* add and link */
  gst_bin_add_many (GST_BIN_CAST (videobin), colorspace, encoder, muxer, sink,
      NULL);
  if (!gst_element_link_many (colorspace, encoder, muxer, sink, NULL))
    goto error;

  /* add ghostpad */
  pad = gst_element_get_static_pad (colorspace, "sink");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (videobin->ghostpad), pad))
    goto error;

  videobin->elements_created = TRUE;
  return TRUE;

error:
  if (pad)
    gst_object_unref (pad);
  return FALSE;
}

static GstStateChangeReturn
gst_video_recording_bin_change_state (GstElement * element,
    GstStateChange trans)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstVideoRecordingBin *videobin = GST_VIDEO_RECORDING_BIN_CAST (element);

  switch (trans) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_video_recording_bin_create_elements (videobin)) {
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
gst_video_recording_bin_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videorecordingbin", GST_RANK_NONE,
      gst_video_recording_bin_get_type ());
}
