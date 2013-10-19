/*
 * GStreamer
 * Copyright (C) 2013 Jan Schmidt <jan@centricular.com>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-rpicamsrc
 *
 * Source element for capturing from the Raspberry Pi camera module
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m rpicamsrc ! fakesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstrpicamsrc.h"
#include "gstrpicam_types.h"
#include "gstrpicam-enum-types.h"
#include "RaspiCapture.h"

#include "bcm_host.h"
#include "interface/vcos/vcos.h"

#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_connection.h"

GST_DEBUG_CATEGORY_STATIC (gst_rpi_cam_src_debug);
#define GST_CAT_DEFAULT gst_rpi_cam_src_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_PREVIEW,
  PROP_PREVIEW_ENCODED,
  PROP_PREVIEW_OPACITY,
  PROP_FULLSCREEN,
  PROP_SHARPNESS,
  PROP_CONTRAST,
  PROP_BRIGHTNESS,
  PROP_SATURATION,
  PROP_ISO,
  PROP_VIDEO_STABILISATION,
  PROP_EXPOSURE_COMPENSATION,
  PROP_EXPOSURE_MODE,
  PROP_EXPOSURE_METERING_MODE,
  PROP_AWB_MODE,
  PROP_IMAGE_EFFECT,
  PROP_IMAGE_EFFECT_PARAMS,
  PROP_COLOUR_EFFECTS,
  PROP_ROTATION,
  PROP_HFLIP,
  PROP_VFLIP,
  PROP_ROI_X,
  PROP_ROI_Y,
  PROP_ROI_W,
  PROP_ROI_H,
};

#define BITRATE_DEFAULT 17000000        /* 17Mbit/s default for 1080p */
#define BITRATE_HIGHEST 25000000

#define SHARPNESS_DEFAULT 0
#define CONTRAST_DEFAULT 0
#define BRIGHTNESS_DEFAULT 50
#define SATURATION_DEFAULT 0
#define ISO_DEFAULT 0
#define VIDEO_STABILISATION_DEFAULT FALSE
#define EXPOSURE_COMPENSATION_DEFAULT 0

#define EXPOSURE_MODE_DEFAULT GST_RPI_CAM_SRC_EXPOSURE_MODE_AUTO
#define EXPOSURE_METERING_MODE_DEFAULT GST_RPI_CAM_SRC_EXPOSURE_METERING_MODE_AVERAGE

/*
   params->exposureMode = MMAL_PARAM_EXPOSUREMODE_AUTO;
   params->exposureMeterMode = MMAL_PARAM_EXPOSUREMETERINGMODE_AVERAGE;
   params->awbMode = MMAL_PARAM_AWBMODE_AUTO;
   params->imageEffect = MMAL_PARAM_IMAGEFX_NONE;
   params->colourEffects.enable = 0;
   params->colourEffects.u = 128;
   params->colourEffects.v = 128;
   params->rotation = 0;
   params->hflip = params->vflip = 0;
   params->roi.x = params->roi.y = 0.0;
   params->roi.w = params->roi.h = 1.0;
*/

#define RAW_AND_JPEG_CAPS \
  GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ";" \
  "image/jpeg,"                                   \
  "width = " GST_VIDEO_SIZE_RANGE ","             \
  "height = " GST_VIDEO_SIZE_RANGE ","            \
  "framerate = " GST_VIDEO_FPS_RANGE
#define H264_CAPS 				\
  "video/x-h264, "                              \
  "width = " GST_VIDEO_SIZE_RANGE ", "          \
  "height = " GST_VIDEO_SIZE_RANGE ", "         \
  "framerate = " GST_VIDEO_FPS_RANGE ", "       \
  "stream-format = (string) { byte-stream }, "  \
  "alignment = (string) { au }, "               \
  "profile = (string) { baseline, main, high }"

static GstStaticPadTemplate video_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ( /*RAW_AND_JPEG_CAPS "; " */ H264_CAPS)
    );

#define gst_rpi_cam_src_parent_class parent_class
G_DEFINE_TYPE (GstRpiCamSrc, gst_rpi_cam_src, GST_TYPE_PUSH_SRC);

static void gst_rpi_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rpi_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_rpi_cam_src_start (GstBaseSrc * parent);
static gboolean gst_rpi_cam_src_stop (GstBaseSrc * parent);
static gboolean gst_rpi_cam_src_decide_allocation (GstBaseSrc * src,
    GstQuery * query);
static GstFlowReturn gst_rpi_cam_src_create (GstPushSrc * parent,
    GstBuffer ** buf);
static GstCaps *gst_rpi_cam_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_rpi_cam_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_rpi_cam_src_fixate (GstBaseSrc * basesrc, GstCaps * caps);

static void
gst_rpi_cam_src_class_init (GstRpiCamSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *basesrc_class;
  GstPushSrcClass *pushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;
  pushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_rpi_cam_src_set_property;
  gobject_class->get_property = gst_rpi_cam_src_get_property;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate", "Bitrate for encoding",
          1, BITRATE_HIGHEST, BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PREVIEW,
      g_param_spec_boolean ("preview",
          "Preview Window", "Display preview window overlay",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen",
          "Fullscreen Preview", "Display preview window full screen",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PREVIEW_ENCODED,
      g_param_spec_boolean ("preview-encoded",
          "Preview Encoded", "Display encoder output in the preview",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PREVIEW_OPACITY,
      g_param_spec_int ("preview-opacity", "Preview Opacity",
          "Opacity to use for the preview window", 0, 255, 255,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SHARPNESS,
      g_param_spec_int ("sharpness", "Sharpness", "Image capture sharpness",
          -100, 100, SHARPNESS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONTRAST,
      g_param_spec_int ("contrast", "Contrast", "Image capture contrast", -100,
          100, CONTRAST_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BRIGHTNESS,
      g_param_spec_int ("brightness", "Brightness", "Image capture brightness",
          0, 100, BRIGHTNESS_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SATURATION,
      g_param_spec_int ("saturation", "Saturation", "Image capture saturation",
          -100, 100, SATURATION_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ISO,
      g_param_spec_int ("iso", "ISO", "ISO value to use (0 = Auto)", 0, 3200, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VIDEO_STABILISATION,
      g_param_spec_boolean ("video-stabilisation", "Video Stabilisation",
          "Enable or disable video stabilisation", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXPOSURE_COMPENSATION,
      g_param_spec_int ("exposure-compensation", "EV compensation",
          "Exposure Value compensation", -10, 10, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXPOSURE_MODE,
      g_param_spec_enum ("exposure-mode", "Exposure Mode",
          "Camera exposure mode to use",
          GST_RPI_CAM_TYPE_RPI_CAM_SRC_EXPOSURE_MODE, EXPOSURE_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXPOSURE_METERING_MODE,
      g_param_spec_enum ("metering-mode", "Exposure Metering Mode",
          "Camera exposure metering mode to use",
          GST_RPI_CAM_TYPE_RPI_CAM_SRC_EXPOSURE_METERING_MODE,
          EXPOSURE_METERING_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AWB_MODE,
      g_param_spec_enum ("awb-mode", "Automatic White Balance Mode",
          "White Balance mode", GST_RPI_CAM_TYPE_RPI_CAM_SRC_AWB_MODE,
          GST_RPI_CAM_SRC_AWB_MODE_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IMAGE_EFFECT,
      g_param_spec_enum ("image-effect", "Image effect",
          "Visual FX to apply to the image",
          GST_RPI_CAM_TYPE_RPI_CAM_SRC_IMAGE_EFFECT,
          GST_RPI_CAM_SRC_IMAGEFX_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#if 0
  PROP_IMAGE_EFFECT_PARAMS, PROP_COLOUR_EFFECTS,
#endif
      g_object_class_install_property (gobject_class, PROP_ROTATION,
      g_param_spec_int ("rotation", "Rotation",
          "Rotate captured image (0, 90, 180, 270 degrees)", 0, 270, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HFLIP,
      g_param_spec_boolean ("hflip", "Horizontal Flip",
          "Flip capture horizontally", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_VFLIP,
      g_param_spec_boolean ("vflip", "Vertical Flip", "Flip capture vertically",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ROI_X,
      g_param_spec_float ("roi-x", "ROI X",
          "Normalised region-of-interest X coord", 0, 1.0, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ROI_Y,
      g_param_spec_float ("roi-y", "ROI Y",
          "Normalised region-of-interest Y coord", 0, 1.0, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ROI_W,
      g_param_spec_float ("roi-w", "ROI W",
          "Normalised region-of-interest W coord", 0, 1.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ROI_H,
      g_param_spec_float ("roi-h", "ROI H",
          "Normalised region-of-interest H coord", 0, 1.0, 1.0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Raspberry Pi Camera Source",
      "Source/Video",
      "Raspberry Pi camera module source", "Jan Schmidt <jan@centricular.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_src_template));

  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_stop);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_rpi_cam_src_decide_allocation);
  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_get_caps);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_set_caps);
  basesrc_class->fixate = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_fixate);
  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_create);

  raspicapture_init ();
}

static void
gst_rpi_cam_src_init (GstRpiCamSrc * src)
{
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
  raspicapture_default_config (&src->capture_config);

  src->capture_config.verbose = 1;
  /* do-timestamping by default for now. FIXME: Implement proper timestamping */
  gst_base_src_set_do_timestamp (GST_BASE_SRC (src), TRUE);
}

static void
gst_rpi_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      src->capture_config.bitrate = g_value_get_int (value);
      break;
    case PROP_PREVIEW:
      src->capture_config.preview_parameters.wantPreview =
          g_value_get_boolean (value);
      break;
    case PROP_PREVIEW_ENCODED:
      src->capture_config.immutableInput = g_value_get_boolean (value);
      break;
    case PROP_FULLSCREEN:
      src->capture_config.preview_parameters.wantFullScreenPreview =
          g_value_get_boolean (value);
      break;
    case PROP_PREVIEW_OPACITY:
      src->capture_config.preview_parameters.opacity = g_value_get_int (value);
      break;
    case PROP_SHARPNESS:
      src->capture_config.camera_parameters.sharpness = g_value_get_int (value);
      break;
    case PROP_CONTRAST:
      src->capture_config.camera_parameters.contrast = g_value_get_int (value);
      break;
    case PROP_BRIGHTNESS:
      src->capture_config.camera_parameters.brightness =
          g_value_get_int (value);
      break;
    case PROP_SATURATION:
      src->capture_config.camera_parameters.saturation =
          g_value_get_int (value);
      break;
    case PROP_ISO:
      src->capture_config.camera_parameters.ISO = g_value_get_int (value);
      break;
    case PROP_VIDEO_STABILISATION:
      src->capture_config.camera_parameters.videoStabilisation =
          g_value_get_boolean (value);
      break;
    case PROP_EXPOSURE_COMPENSATION:
      src->capture_config.camera_parameters.exposureCompensation =
          g_value_get_int (value);
      break;
    case PROP_EXPOSURE_MODE:
      src->capture_config.camera_parameters.exposureMode =
          g_value_get_enum (value);
      break;
    case PROP_EXPOSURE_METERING_MODE:
      src->capture_config.camera_parameters.exposureMeterMode =
          g_value_get_enum (value);
      break;
    case PROP_ROTATION:
      src->capture_config.camera_parameters.rotation = g_value_get_int (value);
      break;
    case PROP_AWB_MODE:
      src->capture_config.camera_parameters.awbMode = g_value_get_enum (value);
      break;
    case PROP_IMAGE_EFFECT:
      src->capture_config.camera_parameters.imageEffect = g_value_get_enum (value);
      break;
    case PROP_HFLIP:
      src->capture_config.camera_parameters.hflip = g_value_get_boolean (value);
      break;
    case PROP_VFLIP:
      src->capture_config.camera_parameters.vflip = g_value_get_boolean (value);
      break;
    case PROP_ROI_X:
      src->capture_config.camera_parameters.roi.x = g_value_get_float (value);
      break;
    case PROP_ROI_Y:
      src->capture_config.camera_parameters.roi.y = g_value_get_float (value);
      break;
    case PROP_ROI_W:
      src->capture_config.camera_parameters.roi.w = g_value_get_float (value);
      break;
    case PROP_ROI_H:
      src->capture_config.camera_parameters.roi.h = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rpi_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_int (value, src->capture_config.bitrate);
      break;
    case PROP_PREVIEW:
      g_value_set_boolean (value,
          src->capture_config.preview_parameters.wantPreview);
      break;
    case PROP_PREVIEW_ENCODED:
      g_value_set_boolean (value, src->capture_config.immutableInput);
      break;
    case PROP_FULLSCREEN:
      g_value_set_boolean (value,
          src->capture_config.preview_parameters.wantFullScreenPreview);
      break;
    case PROP_PREVIEW_OPACITY:
      g_value_set_int (value, src->capture_config.preview_parameters.opacity);
      break;
    case PROP_SHARPNESS:
      g_value_set_int (value, src->capture_config.camera_parameters.sharpness);
      break;
    case PROP_CONTRAST:
      g_value_set_int (value, src->capture_config.camera_parameters.contrast);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_int (value, src->capture_config.camera_parameters.brightness);
      break;
    case PROP_SATURATION:
      g_value_set_int (value, src->capture_config.camera_parameters.saturation);
      break;
    case PROP_ISO:
      g_value_set_int (value, src->capture_config.camera_parameters.ISO);
      break;
    case PROP_VIDEO_STABILISATION:
      g_value_set_boolean (value,
          ! !(src->capture_config.camera_parameters.videoStabilisation));
      break;
    case PROP_EXPOSURE_COMPENSATION:
      g_value_set_int (value,
          src->capture_config.camera_parameters.exposureCompensation);
      break;
    case PROP_EXPOSURE_MODE:
      g_value_set_enum (value,
          src->capture_config.camera_parameters.exposureMode);
      break;
    case PROP_EXPOSURE_METERING_MODE:
      g_value_set_enum (value,
          src->capture_config.camera_parameters.exposureMeterMode);
      break;
    case PROP_ROTATION:
      g_value_set_int (value, src->capture_config.camera_parameters.rotation);
      break;
    case PROP_AWB_MODE:
      g_value_set_enum (value, src->capture_config.camera_parameters.awbMode);
      break;
    case PROP_IMAGE_EFFECT:
      g_value_set_enum (value, src->capture_config.camera_parameters.imageEffect);
      break;
    case PROP_HFLIP:
      g_value_set_boolean (value,
          ! !(src->capture_config.camera_parameters.hflip));
      break;
    case PROP_VFLIP:
      g_value_set_boolean (value,
          ! !(src->capture_config.camera_parameters.vflip));
      break;
    case PROP_ROI_X:
      g_value_set_float (value, src->capture_config.camera_parameters.roi.x);
      break;
    case PROP_ROI_Y:
      g_value_set_float (value, src->capture_config.camera_parameters.roi.y);
      break;
    case PROP_ROI_W:
      g_value_set_float (value, src->capture_config.camera_parameters.roi.w);
      break;
    case PROP_ROI_H:
      g_value_set_float (value, src->capture_config.camera_parameters.roi.h);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_rpi_cam_src_start (GstBaseSrc * parent)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (parent);
  g_print ("In src_start()\n");
  src->capture_state = raspi_capture_setup (&src->capture_config);
  if (src->capture_state == NULL)
    return FALSE;

  return TRUE;
}

static gboolean
gst_rpi_cam_src_stop (GstBaseSrc * parent)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (parent);
  if (src->started)
    raspi_capture_stop (src->capture_state);
  raspi_capture_free (src->capture_state);
  src->capture_state = NULL;
  return TRUE;
}

static GstCaps *
gst_rpi_cam_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (bsrc);
  GstCaps *caps;

  caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  if (src->capture_state == NULL)
    goto done;

  /* FIXME: Retrieve limiting parameters from the camera module, max width/height fps-range */
  caps = gst_caps_make_writable (caps);
  gst_caps_set_simple (caps,
      "width", GST_TYPE_INT_RANGE, 1, 1920,
      "height", GST_TYPE_INT_RANGE, 1, 1080,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, 90, 1, NULL);

done:
  GST_DEBUG_OBJECT (src, "get_caps returning %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_rpi_cam_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (bsrc);
  GstVideoInfo info;

  GST_DEBUG_OBJECT (src, "In set_caps %" GST_PTR_FORMAT, caps);
  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  src->capture_config.width = info.width;
  src->capture_config.height = info.height;
  src->capture_config.fps_n = info.fps_n;
  src->capture_config.fps_d = info.fps_d;

  return TRUE;
}

static gboolean
gst_rpi_cam_src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  g_print ("In decide_allocation\n");
  return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
}

static GstCaps *
gst_rpi_cam_src_fixate (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstStructure *structure;
  gint i;

  GST_DEBUG_OBJECT (basesrc, "fixating caps %" GST_PTR_FORMAT, caps);

  caps = gst_caps_make_writable (caps);

  for (i = 0; i < gst_caps_get_size (caps); ++i) {
    structure = gst_caps_get_structure (caps, i);

    /* Fixate to 1920x1080 resolution if possible */
    gst_structure_fixate_field_nearest_int (structure, "width", 1920);
    gst_structure_fixate_field_nearest_int (structure, "height", 1080);
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
    gst_structure_fixate_field (structure, "format");
  }

  GST_DEBUG_OBJECT (basesrc, "fixated caps %" GST_PTR_FORMAT, caps);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (basesrc, caps);

  return caps;
}

static GstFlowReturn
gst_rpi_cam_src_create (GstPushSrc * parent, GstBuffer ** buf)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (parent);
  GstFlowReturn ret;

  if (!src->started) {
    if (!raspi_capture_start (src->capture_state))
      return GST_FLOW_ERROR;
    src->started = TRUE;
  }

  /* FIXME: Use custom allocator */
  ret = raspi_capture_fill_buffer (src->capture_state, buf);
  if (*buf)
    GST_LOG_OBJECT (src, "Made buffer of size %" G_GSIZE_FORMAT "\n",
        gst_buffer_get_size (*buf));

  return ret;
}

static gboolean
plugin_init (GstPlugin * rpicamsrc)
{
  GST_DEBUG_CATEGORY_INIT (gst_rpi_cam_src_debug, "rpicamsrc",
      0, "rpicamsrc debug");

  return gst_element_register (rpicamsrc, "rpicamsrc", GST_RANK_NONE,
      GST_TYPE_RPICAMSRC);
}

#ifndef PACKAGE
#define PACKAGE "gstrpicamsrc"
#endif

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rpicamsrc,
    "Raspberry Pi Camera Source",
    plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
