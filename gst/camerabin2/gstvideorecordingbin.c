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
#include "camerabingeneral.h"

/* prototypes */


enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_VIDEO_ENCODER,
  PROP_MUXER
};

#define DEFAULT_LOCATION "vidcap"
#define DEFAULT_COLORSPACE "ffmpegcolorspace"
#define DEFAULT_VIDEO_ENCODER "theoraenc"
#define DEFAULT_MUXER "oggmux"
#define DEFAULT_SINK "filesink"

/* pad templates */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv; video/x-raw-rgb")
    );

/* class initialization */

GST_BOILERPLATE (GstVideoRecordingBin, gst_video_recording_bin, GstBin,
    GST_TYPE_BIN);

/* GObject callbacks */
static void gst_video_recording_bin_dispose (GObject * object);
static void gst_video_recording_bin_finalize (GObject * object);

/* Element class functions */
static GstStateChangeReturn
gst_video_recording_bin_change_state (GstElement * element,
    GstStateChange trans);

static void
gst_video_recording_bin_set_video_encoder (GstVideoRecordingBin * videobin,
    GstElement * encoder)
{
  GST_DEBUG_OBJECT (GST_OBJECT (videobin),
      "Setting video encoder %" GST_PTR_FORMAT, encoder);

  if (videobin->user_video_encoder)
    g_object_unref (videobin->user_video_encoder);

  if (encoder)
    g_object_ref (encoder);

  videobin->user_video_encoder = encoder;
}

static void
gst_video_recording_bin_set_muxer (GstVideoRecordingBin * videobin,
    GstElement * muxer)
{
  GST_DEBUG_OBJECT (GST_OBJECT (videobin),
      "Setting video muxer %" GST_PTR_FORMAT, muxer);

  if (videobin->user_muxer)
    g_object_unref (videobin->user_muxer);

  if (muxer)
    g_object_ref (muxer);

  videobin->user_muxer = muxer;
}

static void
gst_video_recording_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoRecordingBin *videobin = GST_VIDEO_RECORDING_BIN_CAST (object);

  switch (prop_id) {
    case PROP_LOCATION:
      if (videobin->location)
        g_free (videobin->location);

      videobin->location = g_value_dup_string (value);
      if (videobin->sink) {
        g_object_set (videobin->sink, "location", videobin->location, NULL);
      }
      break;
    case PROP_VIDEO_ENCODER:
      gst_video_recording_bin_set_video_encoder (videobin,
          g_value_get_object (value));
      break;
    case PROP_MUXER:
      gst_video_recording_bin_set_muxer (videobin, g_value_get_object (value));
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
    case PROP_VIDEO_ENCODER:
      g_value_set_object (value, videobin->video_encoder);
      break;
    case PROP_MUXER:
      g_value_set_object (value, videobin->muxer);
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

  gobject_class->dispose = gst_video_recording_bin_dispose;
  gobject_class->finalize = gst_video_recording_bin_finalize;

  gobject_class->set_property = gst_video_recording_bin_set_property;
  gobject_class->get_property = gst_video_recording_bin_get_property;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_video_recording_bin_change_state);

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location to save the captured files.",
          DEFAULT_LOCATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VIDEO_ENCODER,
      g_param_spec_object ("video-encoder", "Video encoder",
          "Video encoder GstElement (default is theoraenc).",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MUXER,
      g_param_spec_object ("video-muxer", "Video muxer",
          "Video muxer GstElement (default is oggmux).",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
  videobin->video_encoder = NULL;
  videobin->user_video_encoder = NULL;
  videobin->muxer = NULL;
  videobin->user_muxer = NULL;
}

static void
gst_video_recording_bin_dispose (GObject * object)
{
  GstVideoRecordingBin *videobin = GST_VIDEO_RECORDING_BIN_CAST (object);

  if (videobin->user_video_encoder) {
    gst_object_unref (videobin->user_video_encoder);
    videobin->user_video_encoder = NULL;
  }

  if (videobin->user_muxer) {
    gst_object_unref (videobin->user_muxer);
    videobin->user_muxer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose ((GObject *) videobin);
}

static void
gst_video_recording_bin_finalize (GObject * object)
{
  GstVideoRecordingBin *videobin = GST_VIDEO_RECORDING_BIN_CAST (object);

  g_free (videobin->location);
  videobin->location = NULL;

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) videobin);
}


static gboolean
gst_video_recording_bin_create_elements (GstVideoRecordingBin * videobin)
{
  GstElement *colorspace;
  GstPad *pad = NULL;

  if (videobin->elements_created)
    return TRUE;

  /* create elements */
  colorspace =
      gst_camerabin_create_and_add_element (GST_BIN (videobin),
      DEFAULT_COLORSPACE);
  if (!colorspace)
    goto error;

  if (videobin->user_video_encoder) {
    videobin->video_encoder = videobin->user_video_encoder;
    if (!gst_camerabin_add_element (GST_BIN (videobin),
            videobin->video_encoder)) {
      goto error;
    }
  } else {
    videobin->video_encoder =
        gst_camerabin_create_and_add_element (GST_BIN (videobin),
        DEFAULT_VIDEO_ENCODER);
    if (!videobin->video_encoder)
      goto error;
  }

  if (videobin->user_muxer) {
    videobin->muxer = videobin->user_muxer;
    if (!gst_camerabin_add_element (GST_BIN (videobin), videobin->muxer)) {
      goto error;
    }
  } else {
    videobin->muxer =
        gst_camerabin_create_and_add_element (GST_BIN (videobin),
        DEFAULT_MUXER);
    if (!videobin->muxer)
      goto error;
  }

  videobin->sink = gst_camerabin_create_and_add_element (GST_BIN (videobin),
      DEFAULT_SINK);
  if (!videobin->sink)
    goto error;

  g_object_set (videobin->sink, "location", videobin->location, "async", FALSE,
      NULL);

  /* add ghostpad */
  pad = gst_element_get_static_pad (colorspace, "sink");
  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (videobin->ghostpad), pad))
    goto error;

  videobin->elements_created = TRUE;
  return TRUE;

error:
  GST_DEBUG_OBJECT (videobin, "Create elements failed");
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
