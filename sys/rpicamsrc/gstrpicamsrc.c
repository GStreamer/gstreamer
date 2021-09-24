/*
 * GStreamer
 * Copyright (C) 2013-2015 Jan Schmidt <jan@centricular.com>
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
 * @title: rpicamsrc
 * @short_description: Raspberry Pi Camera Source
 *
 * Source element for capturing video from the Raspberry Pi camera module.
 *
 * This element works the same way that the `raspivid` command-line utility
 * does and has a similar feature set.
 *
 * The element can output video in form of raw video frames or encoded as
 * (M)JPEG or H.264 video. You can use the element properties to fine-tune
 * the capture, image processing and encoding parameters.
 *
 * ## Example pipelines
 * |[
 * gst-launch-1.0 -v rpicamsrc preview=true ! fakesink
 * ]|
 * should show a preview window on the screen.
 *
 * |[
 * gst-launch-1.0 -e rpicamsrc bitrate=1000000 ! h264parse ! matroskamux ! filesink location=test.mkv
 * ]|
 * should produce a file called test.mkv containing an H.264 video stream. We
 * pass -e to gst-launch-1.0 so that the pipeline is finalised properly when
 * interrupted with Control-C. This makes sure the Matroska file headers are
 * updated when streaming ends.
 *
 * |[
 * gst-launch-1.0 -e rpicamsrc bitrate=5000000 num-buffers=500 keyframe-interval=30 ! h264parse ! splitmuxsink location=video%02d.mov max-size-time=10000000000 max-size-bytes=1000000
 * ]|
 * Records a video stream captured from the Raspberry Pi camera module and
 * muxes it into ISO mp4 files, splitting as needed to limit size/duration to
 * 10 seconds and 1MB maximum size.
 *
 * gst-launch-1.0 -e rpicamsrc ! image/jpeg,framerate=1/1 ! multifilesink location=image-%05d.jpg
 * ]|
 * Captures a stream of JPEG images from the Raspberry Pi camera module at
 * a rate of 1 frame per second and writes each frame into a new file in the
 * current directory, starting with image-00000.jpg, then image-00001.jpg etc.
 *
 * Since: 1.18
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <gst/video/video.h>

#include "gstrpicamsrc.h"
#include "gstrpicam_types.h"
#include "gstrpicam-enum-types.h"
#include "gstrpicamsrcdeviceprovider.h"
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

GST_DEBUG_CATEGORY (gst_rpi_cam_src_debug);

/* comment out to use JPEG codec instead of MJPEG */
// #define USE_JPEG_CODEC

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CAMERA_NUMBER,
  PROP_BITRATE,
  PROP_KEYFRAME_INTERVAL,
  PROP_PREVIEW,
  PROP_PREVIEW_ENCODED,
  PROP_PREVIEW_OPACITY,
  PROP_PREVIEW_X,
  PROP_PREVIEW_Y,
  PROP_PREVIEW_W,
  PROP_PREVIEW_H,
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
  PROP_AWB_GAIN_RED,
  PROP_AWB_GAIN_BLUE,
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
  PROP_QUANTISATION_PARAMETER,
  PROP_INLINE_HEADERS,
  PROP_SHUTTER_SPEED,
  PROP_SENSOR_MODE,
  PROP_DRC,
  PROP_ANNOTATION_MODE,
  PROP_ANNOTATION_TEXT,
  PROP_ANNOTATION_TEXT_SIZE,
  PROP_ANNOTATION_TEXT_COLOUR,
  PROP_ANNOTATION_TEXT_BG_COLOUR,
  PROP_INTRA_REFRESH_TYPE,
  PROP_VIDEO_DIRECTION,
  PROP_JPEG_QUALITY,
  PROP_USE_STC
};

#define CAMERA_DEFAULT 0

#define BITRATE_DEFAULT 17000000        /* 17Mbit/s default for 1080p */
#define BITRATE_HIGHEST 25000000

#define QUANTISATION_DEFAULT 0

#define SHARPNESS_DEFAULT 0
#define CONTRAST_DEFAULT 0
#define BRIGHTNESS_DEFAULT 50
#define SATURATION_DEFAULT 0
#define ISO_DEFAULT 0
#define VIDEO_STABILISATION_DEFAULT FALSE
#define EXPOSURE_COMPENSATION_DEFAULT 0
#define KEYFRAME_INTERVAL_DEFAULT -1

#define EXPOSURE_MODE_DEFAULT GST_RPI_CAM_SRC_EXPOSURE_MODE_AUTO
#define EXPOSURE_METERING_MODE_DEFAULT GST_RPI_CAM_SRC_EXPOSURE_METERING_MODE_AVERAGE

#define DEFAULT_JPEG_QUALITY 50

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

#define JPEG_CAPS \
  "image/jpeg,"                                   \
  "width = " GST_VIDEO_SIZE_RANGE ","             \
  "height = " GST_VIDEO_SIZE_RANGE ","            \
  "framerate = " GST_VIDEO_FPS_RANGE
#define H264_CAPS               \
  "video/x-h264, "                              \
  "width = " GST_VIDEO_SIZE_RANGE ", "          \
  "height = " GST_VIDEO_SIZE_RANGE ", "         \
  "framerate = " GST_VIDEO_FPS_RANGE ", "       \
  "stream-format = (string) byte-stream, "  \
  "alignment = (string) nal, "               \
  "profile = (string) { constrained-baseline, baseline, main, high }"
#define RAW_CAPS \
  GST_VIDEO_CAPS_MAKE ("{ I420, RGB, BGR, RGBA }")      /* FIXME: Map more raw formats */

static GstStaticPadTemplate video_src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (H264_CAPS "; " JPEG_CAPS "; " RAW_CAPS)
    );


static void gst_rpi_cam_src_finalize (GObject * object);

static void gst_rpi_cam_src_colorbalance_init (GstColorBalanceInterface *
    iface);
static void gst_rpi_cam_src_orientation_init (GstVideoOrientationInterface *
    iface);
static void gst_rpi_cam_src_direction_init (GstVideoDirectionInterface * iface);

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
static gboolean gst_rpi_cam_src_event (GstBaseSrc * src, GstEvent * event);
static gboolean gst_rpi_cam_src_send_event (GstElement * element,
    GstEvent * event);

#define gst_rpi_cam_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRpiCamSrc, gst_rpi_cam_src,
    GST_TYPE_PUSH_SRC,
    G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE,
        gst_rpi_cam_src_colorbalance_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_DIRECTION,
        gst_rpi_cam_src_direction_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_ORIENTATION,
        gst_rpi_cam_src_orientation_init));

#define C_ENUM(v) ((gint) v)

#define GST_RPI_CAM_SRC_TYPE_SENSOR_MODE gst_rpi_cam_src_sensor_mode_get_type()
static GType gst_rpi_cam_src_sensor_mode_get_type (void);

static GType
gst_rpi_cam_src_sensor_mode_get_type (void)
{
  static const GEnumValue values[] = {
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_AUTOMATIC), "Automatic", "automatic"},
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_1920x1080), "1920x1080 16:9 1-30fps",
        "1920x1080"},
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_2592x1944_FAST),
          "2592x1944 4:3 1-15fps / 3240x2464 15fps w/ v.2 board",
        "2592x1944-fast"},
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_2592x1944_SLOW),
          "2592x1944 4:3 0.1666-1fps / 3240x2464 15fps w/ v.2 board",
        "2592x1944-slow"},
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_1296x972), "1296x972 4:3 1-42fps",
        "1296x972"},
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_1296x730), "1296x730 16:9 1-49fps",
        "1296x730"},
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_640x480_SLOW),
        "640x480 4:3 42.1-60fps", "640x480-slow"},
    {C_ENUM (GST_RPI_CAM_SRC_SENSOR_MODE_640x480_FAST),
        "640x480 4:3 60.1-90fps", "640x480-fast"},
    {0, NULL, NULL}
  };
  static GType id = 0;
  if (g_once_init_enter ((gsize *) & id)) {
    GType _id;
    _id = g_enum_register_static ("GstRpiCamSrcSensorMode", values);
    g_once_init_leave ((gsize *) & id, _id);
  }

  return id;
}


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

  gobject_class->finalize = gst_rpi_cam_src_finalize;
  gobject_class->set_property = gst_rpi_cam_src_set_property;
  gobject_class->get_property = gst_rpi_cam_src_get_property;
  g_object_class_install_property (gobject_class, PROP_CAMERA_NUMBER,
      g_param_spec_int ("camera-number", "Camera Number",
          "Which camera to use on a multi-camera system - 0 or 1", 0,
          1, CAMERA_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate",
          "Bitrate for encoding. 0 for VBR using quantisation-parameter", 0,
          BITRATE_HIGHEST, BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#ifdef USE_JPEG_CODEC
  g_object_class_install_property (gobject_class, PROP_JPEG_QUALITY,
      g_param_spec_int ("jpeg-quality", "JPEG Quality",
          "Quality setting for JPEG encode", 1, 100, DEFAULT_JPEG_QUALITY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
  g_object_class_install_property (gobject_class, PROP_KEYFRAME_INTERVAL,
      g_param_spec_int ("keyframe-interval", "Keyframe Interface",
          "Interval (in frames) between I frames. -1 = automatic, 0 = single-keyframe",
          -1, G_MAXINT, KEYFRAME_INTERVAL_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PREVIEW,
      g_param_spec_boolean ("preview", "Preview Window",
          "Display preview window overlay", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FULLSCREEN,
      g_param_spec_boolean ("fullscreen", "Fullscreen Preview",
          "Display preview window full screen", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PREVIEW_ENCODED,
      g_param_spec_boolean ("preview-encoded", "Preview Encoded",
          "Display encoder output in the preview", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PREVIEW_OPACITY,
      g_param_spec_int ("preview-opacity", "Preview Opacity",
          "Opacity to use for the preview window", 0, 255, 255,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PREVIEW_X,
      g_param_spec_int ("preview-x", "Preview window X position",
          "Start X coordinate of the preview window (in pixels)", 0, 2048, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PREVIEW_Y,
      g_param_spec_int ("preview-y", "Preview window Y position",
          "Start Y coordinate of the preview window (in pixels)", 0, 2048, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PREVIEW_W,
      g_param_spec_int ("preview-w", "Preview window width",
          "Width of the preview window (in pixels)", 0, 2048, 1024,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PREVIEW_H,
      g_param_spec_int ("preview-h", "Preview window height",
          "Height of the preview window (in pixels)", 0, 2048, 768,
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
          GST_RPI_CAM_SRC_TYPE_EXPOSURE_MODE, EXPOSURE_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_EXPOSURE_METERING_MODE,
      g_param_spec_enum ("metering-mode", "Exposure Metering Mode",
          "Camera exposure metering mode to use",
          GST_RPI_CAM_SRC_TYPE_EXPOSURE_METERING_MODE,
          EXPOSURE_METERING_MODE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DRC,
      g_param_spec_enum ("drc", "DRC level", "Dynamic Range Control level",
          GST_RPI_CAM_SRC_TYPE_DRC_LEVEL, GST_RPI_CAM_SRC_DRC_LEVEL_OFF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AWB_MODE,
      g_param_spec_enum ("awb-mode", "Automatic White Balance Mode",
          "White Balance mode", GST_RPI_CAM_SRC_TYPE_AWB_MODE,
          GST_RPI_CAM_SRC_AWB_MODE_AUTO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AWB_GAIN_RED,
      g_param_spec_float ("awb-gain-red", "AWB Red Gain",
          "Manual AWB Gain for red channel when awb-mode=off", 0, 8.0, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_AWB_GAIN_BLUE,
      g_param_spec_float ("awb-gain-blue", "AWB Blue Gain",
          "Manual AWB Gain for blue channel when awb-mode=off", 0, 8.0, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IMAGE_EFFECT,
      g_param_spec_enum ("image-effect", "Image effect",
          "Visual FX to apply to the image",
          GST_RPI_CAM_SRC_TYPE_IMAGE_EFFECT,
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
      g_param_spec_boolean ("vflip", "Vertical Flip",
          "Flip capture vertically", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
  g_object_class_install_property (gobject_class,
      PROP_QUANTISATION_PARAMETER,
      g_param_spec_int ("quantisation-parameter",
          "Quantisation Parameter",
          "Set a Quantisation Parameter approx 10-40 with bitrate=0 for VBR encoding. 0 = off",
          0, G_MAXINT, QUANTISATION_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INLINE_HEADERS,
      g_param_spec_boolean ("inline-headers", "Inline Headers",
          "Set to TRUE to insert SPS/PPS before each IDR packet", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SHUTTER_SPEED,
      g_param_spec_int ("shutter-speed", "Shutter Speed",
          "Set a fixed shutter speed, in microseconds. (0 = Auto)", 0,
          6000000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SENSOR_MODE,
      g_param_spec_enum ("sensor-mode", "Camera Sensor Mode",
          "Manually set the camera sensor mode",
          gst_rpi_cam_src_sensor_mode_get_type (),
          GST_RPI_CAM_SRC_SENSOR_MODE_AUTOMATIC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ANNOTATION_MODE,
      g_param_spec_flags ("annotation-mode", "Annotation Mode",
          "Flags to control annotation of the output video",
          GST_RPI_CAM_SRC_TYPE_ANNOTATION_MODE, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ANNOTATION_TEXT,
      g_param_spec_string ("annotation-text", "Annotation Text",
          "Text string to annotate onto video when annotation-mode flags include 'custom-text'",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INTRA_REFRESH_TYPE,
      g_param_spec_enum ("intra-refresh-type", "Intra Refresh Type",
          "Type of Intra Refresh to use, -1 to disable intra refresh",
          GST_RPI_CAM_SRC_TYPE_INTRA_REFRESH_TYPE,
          GST_RPI_CAM_SRC_INTRA_REFRESH_TYPE_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ANNOTATION_TEXT_SIZE,
      g_param_spec_int ("annotation-text-size", "Annotation text size",
          "Set the size of annotation text (in pixels) (0 = Auto)", 0,
          G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ANNOTATION_TEXT_COLOUR,
      g_param_spec_int ("annotation-text-colour",
          "Annotation text colour (VUY)",
          "Set the annotation text colour, as the integer corresponding to a VUY value eg 0x8080FF = 8421631, -1 for default",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_ANNOTATION_TEXT_BG_COLOUR,
      g_param_spec_int ("annotation-text-bg-colour",
          "Annotation text background colour (VUY)",
          "Set the annotation text background colour, as the integer corresponding to a VUY value eg 0x8080FF = 8421631, -1 for default",
          -1, G_MAXINT, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_override_property (gobject_class, PROP_VIDEO_DIRECTION,
      "video-direction");
  g_object_class_install_property (gobject_class, PROP_USE_STC,
      g_param_spec_boolean ("use-stc", "Use System Time Clock",
          "Use the camera STC for timestamping buffers", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "Raspberry Pi Camera Source", "Source/Video",
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
  basesrc_class->event = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_event);
  gstelement_class->send_event = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_send_event);
  pushsrc_class->create = GST_DEBUG_FUNCPTR (gst_rpi_cam_src_create);
  raspicapture_init ();

  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_SENSOR_MODE, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_EXPOSURE_MODE, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_EXPOSURE_METERING_MODE, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_AWB_MODE, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_IMAGE_EFFECT, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_FLICKER_AVOIDANCE, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_DRC_LEVEL, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_ANNOTATION_MODE, 0);
  gst_type_mark_as_plugin_api (GST_RPI_CAM_SRC_TYPE_INTRA_REFRESH_TYPE, 0);
}

static void
gst_rpi_cam_src_init (GstRpiCamSrc * src)
{
  GstColorBalanceChannel *channel;

  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
  raspicapture_default_config (&src->capture_config);
  src->capture_config.intraperiod = KEYFRAME_INTERVAL_DEFAULT;
  src->capture_config.verbose = 1;
  src->capture_config.useSTC = TRUE;

  g_mutex_init (&src->config_lock);

  /* basesrc will generate timestamps if use-stc = false */
  gst_base_src_set_do_timestamp (GST_BASE_SRC (src), TRUE);

  /* Generate the channels list */
  channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
  channel->label = g_strdup ("CONTRAST");
  channel->min_value = -100;
  channel->max_value = 100;
  src->channels = g_list_append (src->channels, channel);

  channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
  channel->label = g_strdup ("BRIGHTNESS");
  channel->min_value = 0;
  channel->max_value = 100;
  src->channels = g_list_append (src->channels, channel);

  channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
  channel->label = g_strdup ("SATURATION");
  channel->min_value = -100;
  channel->max_value = 100;
  src->channels = g_list_append (src->channels, channel);
}

static void
gst_rpi_cam_src_finalize (GObject * object)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (object);
  GList *channels = NULL;
  g_mutex_clear (&src->config_lock);

  channels = src->channels;
  while (channels) {
    GstColorBalanceChannel *channel = channels->data;

    g_object_unref (channel);
    channels->data = NULL;
    channels = g_list_next (channels);
  }

  if (src->channels)
    g_list_free (src->channels);

  G_OBJECT_CLASS (gst_rpi_cam_src_parent_class)->finalize (object);
}

static const GList *
gst_rpi_cam_src_colorbalance_list_channels (GstColorBalance * balance)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (balance);

  g_return_val_if_fail (src != NULL, NULL);
  g_return_val_if_fail (GST_IS_RPICAMSRC (src), NULL);

  return src->channels;
}

static void
gst_rpi_cam_src_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (balance);
  gboolean changed = FALSE;

  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_RPICAMSRC (src));
  g_return_if_fail (channel->label != NULL);

  g_mutex_lock (&src->config_lock);
  if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    changed = value != src->capture_config.camera_parameters.saturation;
    src->capture_config.camera_parameters.saturation = value;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    changed = value != src->capture_config.camera_parameters.brightness;
    src->capture_config.camera_parameters.brightness = value;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    changed = value != src->capture_config.camera_parameters.contrast;
    src->capture_config.camera_parameters.contrast = value;
  }

  if (changed)
    src->capture_config.change_flags |= PROP_CHANGE_COLOURBALANCE;

  g_mutex_unlock (&src->config_lock);

  if (changed) {
    gst_color_balance_value_changed (balance, channel,
        gst_color_balance_get_value (balance, channel));
  }
}

static gint
gst_rpi_cam_src_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (balance);
  gint value = 0;

  g_return_val_if_fail (src != NULL, 0);
  g_return_val_if_fail (GST_IS_RPICAMSRC (src), 0);
  g_return_val_if_fail (channel->label != NULL, 0);

  g_mutex_lock (&src->config_lock);

  if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    value = src->capture_config.camera_parameters.saturation;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    value = src->capture_config.camera_parameters.brightness;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    value = src->capture_config.camera_parameters.contrast;
  }

  g_mutex_unlock (&src->config_lock);

  return value;
}

static GstColorBalanceType
gst_rpi_cam_src_colorbalance_get_balance_type (GstColorBalance * balance)
{
  return GST_COLOR_BALANCE_HARDWARE;
}

static void
gst_rpi_cam_src_reset_custom_orientation (GstRpiCamSrc * src)
{
  src->orientation = GST_VIDEO_ORIENTATION_CUSTOM;
}

static void
gst_rpi_cam_src_set_orientation (GstRpiCamSrc * src,
    GstVideoOrientationMethod orientation)
{
  switch (orientation) {
    case GST_VIDEO_ORIENTATION_IDENTITY:
      src->capture_config.camera_parameters.rotation = 0;
      src->capture_config.camera_parameters.hflip = FALSE;
      src->capture_config.camera_parameters.vflip = FALSE;
      GST_DEBUG_OBJECT (src, "set orientation identity");
      break;
    case GST_VIDEO_ORIENTATION_90R:
      src->capture_config.camera_parameters.rotation = 90;
      src->capture_config.camera_parameters.hflip = FALSE;
      src->capture_config.camera_parameters.vflip = FALSE;
      GST_DEBUG_OBJECT (src, "set orientation 90R");
      break;
    case GST_VIDEO_ORIENTATION_180:
      src->capture_config.camera_parameters.rotation = 180;
      src->capture_config.camera_parameters.hflip = FALSE;
      src->capture_config.camera_parameters.vflip = FALSE;
      GST_DEBUG_OBJECT (src, "set orientation 180");
      break;
    case GST_VIDEO_ORIENTATION_90L:
      src->capture_config.camera_parameters.rotation = 270;
      src->capture_config.camera_parameters.hflip = FALSE;
      src->capture_config.camera_parameters.vflip = FALSE;
      GST_DEBUG_OBJECT (src, "set orientation 90L");
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      src->capture_config.camera_parameters.rotation = 0;
      src->capture_config.camera_parameters.hflip = TRUE;
      src->capture_config.camera_parameters.vflip = FALSE;
      GST_DEBUG_OBJECT (src, "set orientation hflip");
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      src->capture_config.camera_parameters.rotation = 0;
      src->capture_config.camera_parameters.hflip = FALSE;
      src->capture_config.camera_parameters.vflip = TRUE;
      GST_DEBUG_OBJECT (src, "set orientation vflip");
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      src->capture_config.camera_parameters.rotation = 90;
      src->capture_config.camera_parameters.hflip = FALSE;
      src->capture_config.camera_parameters.vflip = TRUE;
      GST_DEBUG_OBJECT (src, "set orientation trans");
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      src->capture_config.camera_parameters.rotation = 270;
      src->capture_config.camera_parameters.hflip = FALSE;
      src->capture_config.camera_parameters.vflip = TRUE;
      GST_DEBUG_OBJECT (src, "set orientation trans");
      break;
    case GST_VIDEO_ORIENTATION_CUSTOM:
      break;
    default:
      GST_WARNING_OBJECT (src, "unsupported orientation %d", orientation);
      break;
  }
  src->orientation =
      orientation >= GST_VIDEO_ORIENTATION_IDENTITY &&
      orientation <= GST_VIDEO_ORIENTATION_CUSTOM ?
      orientation : GST_VIDEO_ORIENTATION_CUSTOM;
  src->capture_config.change_flags |= PROP_CHANGE_ORIENTATION;
}

static void
gst_rpi_cam_src_direction_init (GstVideoDirectionInterface * iface)
{
  /* We implement the video-direction property */
}

static void
gst_rpi_cam_src_colorbalance_init (GstColorBalanceInterface * iface)
{
  iface->list_channels = gst_rpi_cam_src_colorbalance_list_channels;
  iface->set_value = gst_rpi_cam_src_colorbalance_set_value;
  iface->get_value = gst_rpi_cam_src_colorbalance_get_value;
  iface->get_balance_type = gst_rpi_cam_src_colorbalance_get_balance_type;
}

static gboolean
gst_rpi_cam_src_orientation_get_hflip (GstVideoOrientation * orientation,
    gboolean * flip)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (orientation);

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_RPICAMSRC (src), FALSE);

  g_mutex_lock (&src->config_lock);
  *flip = src->capture_config.camera_parameters.hflip;
  g_mutex_unlock (&src->config_lock);

  return TRUE;
}

static gboolean
gst_rpi_cam_src_orientation_get_vflip (GstVideoOrientation * orientation,
    gboolean * flip)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (orientation);

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_RPICAMSRC (src), FALSE);

  g_mutex_lock (&src->config_lock);
  *flip = src->capture_config.camera_parameters.vflip;
  g_mutex_unlock (&src->config_lock);

  return TRUE;
}

static gboolean
gst_rpi_cam_src_orientation_set_hflip (GstVideoOrientation * orientation,
    gboolean flip)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (orientation);

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_RPICAMSRC (src), FALSE);

  g_mutex_lock (&src->config_lock);
  gst_rpi_cam_src_reset_custom_orientation (src);
  src->capture_config.camera_parameters.hflip = flip;
  src->capture_config.change_flags |= PROP_CHANGE_ORIENTATION;
  g_mutex_unlock (&src->config_lock);

  return TRUE;
}

static gboolean
gst_rpi_cam_src_orientation_set_vflip (GstVideoOrientation * orientation,
    gboolean flip)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (orientation);

  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (GST_IS_RPICAMSRC (src), FALSE);

  g_mutex_lock (&src->config_lock);
  gst_rpi_cam_src_reset_custom_orientation (src);
  src->capture_config.camera_parameters.vflip = flip;
  src->capture_config.change_flags |= PROP_CHANGE_ORIENTATION;
  g_mutex_unlock (&src->config_lock);

  return TRUE;
}

static void
gst_rpi_cam_src_orientation_init (GstVideoOrientationInterface * iface)
{
  iface->get_hflip = gst_rpi_cam_src_orientation_get_hflip;
  iface->set_hflip = gst_rpi_cam_src_orientation_set_hflip;
  iface->get_vflip = gst_rpi_cam_src_orientation_get_vflip;
  iface->set_vflip = gst_rpi_cam_src_orientation_set_vflip;

  /* TODO: hcenter / vcenter support */
}

static void
gst_rpi_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (object);

  g_mutex_lock (&src->config_lock);

  switch (prop_id) {
    case PROP_CAMERA_NUMBER:
      src->capture_config.cameraNum = g_value_get_int (value);
      break;
    case PROP_BITRATE:
      src->capture_config.bitrate = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_ENCODING;
      break;
    case PROP_JPEG_QUALITY:
      src->capture_config.jpegQuality = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_ENCODING;
      break;
    case PROP_KEYFRAME_INTERVAL:
      src->capture_config.intraperiod = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_ENCODING;
      break;
    case PROP_PREVIEW:
      src->capture_config.preview_parameters.wantPreview =
          g_value_get_boolean (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_PREVIEW_ENCODED:
      src->capture_config.immutableInput = g_value_get_boolean (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_FULLSCREEN:
      src->capture_config.preview_parameters.wantFullScreenPreview =
          g_value_get_boolean (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_PREVIEW_OPACITY:
      src->capture_config.preview_parameters.opacity = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_PREVIEW_X:
      src->capture_config.preview_parameters.previewWindow.x =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_PREVIEW_Y:
      src->capture_config.preview_parameters.previewWindow.y =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_PREVIEW_W:
      src->capture_config.preview_parameters.previewWindow.width =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_PREVIEW_H:
      src->capture_config.preview_parameters.previewWindow.height =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_PREVIEW;
      break;
    case PROP_SHARPNESS:
      src->capture_config.camera_parameters.sharpness = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_COLOURBALANCE;
      break;
    case PROP_CONTRAST:
      src->capture_config.camera_parameters.contrast = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_COLOURBALANCE;
      break;
    case PROP_BRIGHTNESS:
      src->capture_config.camera_parameters.brightness =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_COLOURBALANCE;
      break;
    case PROP_SATURATION:
      src->capture_config.camera_parameters.saturation =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_COLOURBALANCE;
      break;
    case PROP_ISO:
      src->capture_config.camera_parameters.ISO = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_SENSOR_SETTINGS;
      break;
    case PROP_VIDEO_STABILISATION:
      src->capture_config.camera_parameters.videoStabilisation =
          g_value_get_boolean (value);
      src->capture_config.change_flags |= PROP_CHANGE_VIDEO_STABILISATION;
      break;
    case PROP_EXPOSURE_COMPENSATION:
      src->capture_config.camera_parameters.exposureCompensation =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_SENSOR_SETTINGS;
      break;
    case PROP_EXPOSURE_MODE:
      src->capture_config.camera_parameters.exposureMode =
          g_value_get_enum (value);
      src->capture_config.change_flags |= PROP_CHANGE_SENSOR_SETTINGS;
      break;
    case PROP_EXPOSURE_METERING_MODE:
      src->capture_config.camera_parameters.exposureMeterMode =
          g_value_get_enum (value);
      src->capture_config.change_flags |= PROP_CHANGE_SENSOR_SETTINGS;
      break;
    case PROP_ROTATION:
      gst_rpi_cam_src_reset_custom_orientation (src);
      src->capture_config.camera_parameters.rotation = g_value_get_int (value);
      break;
    case PROP_AWB_MODE:
      src->capture_config.camera_parameters.awbMode = g_value_get_enum (value);
      src->capture_config.change_flags |= PROP_CHANGE_AWB;
      break;
    case PROP_AWB_GAIN_RED:
      src->capture_config.camera_parameters.awb_gains_r =
          g_value_get_float (value);
      src->capture_config.change_flags |= PROP_CHANGE_AWB;
      break;
    case PROP_AWB_GAIN_BLUE:
      src->capture_config.camera_parameters.awb_gains_b =
          g_value_get_float (value);
      src->capture_config.change_flags |= PROP_CHANGE_AWB;
      break;
    case PROP_IMAGE_EFFECT:
      src->capture_config.camera_parameters.imageEffect =
          g_value_get_enum (value);
      src->capture_config.change_flags |= PROP_CHANGE_IMAGE_COLOUR_EFFECT;
      break;
    case PROP_HFLIP:
      gst_rpi_cam_src_reset_custom_orientation (src);
      src->capture_config.camera_parameters.hflip = g_value_get_boolean (value);
      src->capture_config.change_flags |= PROP_CHANGE_ORIENTATION;
      break;
    case PROP_VFLIP:
      gst_rpi_cam_src_reset_custom_orientation (src);
      src->capture_config.camera_parameters.vflip = g_value_get_boolean (value);
      src->capture_config.change_flags |= PROP_CHANGE_ORIENTATION;
      break;
    case PROP_ROI_X:
      src->capture_config.camera_parameters.roi.x = g_value_get_float (value);
      src->capture_config.change_flags |= PROP_CHANGE_ROI;
      break;
    case PROP_ROI_Y:
      src->capture_config.camera_parameters.roi.y = g_value_get_float (value);
      src->capture_config.change_flags |= PROP_CHANGE_ROI;
      break;
    case PROP_ROI_W:
      src->capture_config.camera_parameters.roi.w = g_value_get_float (value);
      src->capture_config.change_flags |= PROP_CHANGE_ROI;
      break;
    case PROP_ROI_H:
      src->capture_config.camera_parameters.roi.h = g_value_get_float (value);
      src->capture_config.change_flags |= PROP_CHANGE_ROI;
      break;
    case PROP_QUANTISATION_PARAMETER:
      src->capture_config.quantisationParameter = g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_ENCODING;
      break;
    case PROP_INLINE_HEADERS:
      src->capture_config.bInlineHeaders = g_value_get_boolean (value);
      break;
    case PROP_SHUTTER_SPEED:
      src->capture_config.camera_parameters.shutter_speed =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_SENSOR_SETTINGS;
      break;
    case PROP_DRC:
      src->capture_config.camera_parameters.drc_level =
          g_value_get_enum (value);
      src->capture_config.change_flags |= PROP_CHANGE_SENSOR_SETTINGS;
      break;
    case PROP_SENSOR_MODE:
      src->capture_config.sensor_mode = g_value_get_enum (value);
      src->capture_config.change_flags |= PROP_CHANGE_SENSOR_SETTINGS;
      break;
    case PROP_ANNOTATION_MODE:
      src->capture_config.camera_parameters.enable_annotate =
          g_value_get_flags (value);
      src->capture_config.change_flags |= PROP_CHANGE_ANNOTATION;
      break;
    case PROP_ANNOTATION_TEXT:
      strncpy (src->capture_config.camera_parameters.annotate_string,
          g_value_get_string (value), MMAL_CAMERA_ANNOTATE_MAX_TEXT_LEN_V2);
      src->capture_config.
          camera_parameters.annotate_string[MMAL_CAMERA_ANNOTATE_MAX_TEXT_LEN_V2
          - 1] = '\0';
      src->capture_config.change_flags |= PROP_CHANGE_ANNOTATION;
      break;
    case PROP_ANNOTATION_TEXT_SIZE:
      src->capture_config.camera_parameters.annotate_text_size =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_ANNOTATION;
      break;
    case PROP_ANNOTATION_TEXT_COLOUR:
      src->capture_config.camera_parameters.annotate_text_colour =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_ANNOTATION;
      break;
    case PROP_ANNOTATION_TEXT_BG_COLOUR:
      src->capture_config.camera_parameters.annotate_bg_colour =
          g_value_get_int (value);
      src->capture_config.change_flags |= PROP_CHANGE_ANNOTATION;
      break;
    case PROP_INTRA_REFRESH_TYPE:
      src->capture_config.intra_refresh_type = g_value_get_enum (value);
      src->capture_config.change_flags |= PROP_CHANGE_ENCODING;
      break;
    case PROP_VIDEO_DIRECTION:
      gst_rpi_cam_src_set_orientation (src, g_value_get_enum (value));
      break;
    case PROP_USE_STC:
      src->capture_config.useSTC = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  g_mutex_unlock (&src->config_lock);
}

static void
gst_rpi_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (object);

  g_mutex_lock (&src->config_lock);
  switch (prop_id) {
    case PROP_CAMERA_NUMBER:
      g_value_set_int (value, src->capture_config.cameraNum);
      break;
    case PROP_BITRATE:
      g_value_set_int (value, src->capture_config.bitrate);
      break;
    case PROP_JPEG_QUALITY:
      g_value_set_int (value, src->capture_config.jpegQuality);
      break;
    case PROP_KEYFRAME_INTERVAL:
      g_value_set_int (value, src->capture_config.intraperiod);
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
    case PROP_PREVIEW_X:
      g_value_set_int (value,
          src->capture_config.preview_parameters.previewWindow.x);
      break;
    case PROP_PREVIEW_Y:
      g_value_set_int (value,
          src->capture_config.preview_parameters.previewWindow.y);
      break;
    case PROP_PREVIEW_W:
      g_value_set_int (value,
          src->capture_config.preview_parameters.previewWindow.width);
      break;
    case PROP_PREVIEW_H:
      g_value_set_int (value,
          src->capture_config.preview_parameters.previewWindow.height);
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
          src->capture_config.camera_parameters.videoStabilisation != 0);
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
    case PROP_AWB_GAIN_RED:
      g_value_set_float (value,
          src->capture_config.camera_parameters.awb_gains_r);
      break;
    case PROP_AWB_GAIN_BLUE:
      g_value_set_float (value,
          src->capture_config.camera_parameters.awb_gains_b);
      break;
    case PROP_IMAGE_EFFECT:
      g_value_set_enum (value,
          src->capture_config.camera_parameters.imageEffect);
      break;
    case PROP_HFLIP:
      g_value_set_boolean (value,
          src->capture_config.camera_parameters.hflip != 0);
      break;
    case PROP_VFLIP:
      g_value_set_boolean (value,
          src->capture_config.camera_parameters.vflip != 0);
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
    case PROP_QUANTISATION_PARAMETER:
      g_value_set_int (value, src->capture_config.quantisationParameter);
      break;
    case PROP_INLINE_HEADERS:
      g_value_set_boolean (value, src->capture_config.bInlineHeaders);
      break;
    case PROP_SHUTTER_SPEED:
      g_value_set_int (value,
          src->capture_config.camera_parameters.shutter_speed);
      break;
    case PROP_DRC:
      g_value_set_enum (value, src->capture_config.camera_parameters.drc_level);
      break;
    case PROP_SENSOR_MODE:
      g_value_set_enum (value, src->capture_config.sensor_mode);
      break;
    case PROP_ANNOTATION_MODE:
      g_value_set_flags (value,
          src->capture_config.camera_parameters.enable_annotate);
      break;
    case PROP_ANNOTATION_TEXT:
      g_value_set_string (value,
          src->capture_config.camera_parameters.annotate_string);
      break;
    case PROP_ANNOTATION_TEXT_SIZE:
      g_value_set_int (value,
          src->capture_config.camera_parameters.annotate_text_size);
      break;
    case PROP_ANNOTATION_TEXT_COLOUR:
      g_value_set_int (value,
          src->capture_config.camera_parameters.annotate_text_colour);
      break;
    case PROP_ANNOTATION_TEXT_BG_COLOUR:
      g_value_set_int (value,
          src->capture_config.camera_parameters.annotate_bg_colour);
      break;
    case PROP_INTRA_REFRESH_TYPE:
      g_value_set_enum (value, src->capture_config.intra_refresh_type);
      break;
    case PROP_VIDEO_DIRECTION:
      g_value_set_enum (value, src->orientation);
      break;
    case PROP_USE_STC:
      g_value_set_boolean (value, src->capture_config.useSTC);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&src->config_lock);
}

static gboolean
gst_rpi_cam_src_start (GstBaseSrc * parent)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (parent);
  GST_LOG_OBJECT (src, "In src_start()");
  /* Ensure basesrc timestamping is off is use-stc is on */
  if (src->capture_config.useSTC)
    gst_base_src_set_do_timestamp (GST_BASE_SRC (src), FALSE);
  g_mutex_lock (&src->config_lock);
  src->capture_state = raspi_capture_setup (&src->capture_config);
  /* Clear all capture flags */
  src->capture_config.change_flags = 0;
  g_mutex_unlock (&src->config_lock);
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
  src->started = FALSE;
  return TRUE;
}

static gboolean
gst_rpi_cam_src_send_event (GstElement * parent, GstEvent * event)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (parent);
  gboolean ret;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_UPSTREAM:
      if (gst_video_event_is_force_key_unit (event)) {
        if (src->started) {
          ret = raspi_capture_request_i_frame (src->capture_state);
        } else {
          ret = FALSE;
        }
        gst_event_unref (event);
      } else {
        ret = GST_ELEMENT_CLASS (parent_class)->send_event (parent, event);
      }
      break;
    default:
      ret = GST_ELEMENT_CLASS (parent_class)->send_event (parent, event);
      break;
  }
  return ret;
}

static gboolean
gst_rpi_cam_src_event (GstBaseSrc * parent, GstEvent * event)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (parent);
  gboolean ret;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_UPSTREAM:
      if (gst_video_event_is_force_key_unit (event)) {
        if (src->started) {
          ret = raspi_capture_request_i_frame (src->capture_state);
        } else {
          ret = FALSE;
        }
      } else {
        ret = GST_BASE_SRC_CLASS (parent_class)->event (parent, event);
      }
      break;
    default:
      ret = GST_BASE_SRC_CLASS (parent_class)->event (parent, event);
      break;
  }
  return ret;
}

static GstCaps *
gst_rpi_cam_src_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (bsrc);
  GstCaps *caps;
  gint i;

  caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (bsrc));
  if (src->capture_state == NULL)
    goto done;
  /* FIXME: Retrieve limiting parameters from the camera module, max width/height fps-range */
  caps = gst_caps_make_writable (caps);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    if (gst_structure_has_name (s, "video/x-h264")) {
      gst_caps_set_simple (caps, "width", GST_TYPE_INT_RANGE, 1, 1920, "height",
          GST_TYPE_INT_RANGE, 1, 1080, "framerate", GST_TYPE_FRACTION_RANGE, 0,
          1, RPICAMSRC_MAX_FPS, 1, NULL);
    } else {
      gst_caps_set_simple (caps, "width", GST_TYPE_INT_RANGE, 1, 3240, "height",
          GST_TYPE_INT_RANGE, 1, 2464, "framerate", GST_TYPE_FRACTION_RANGE, 0,
          1, RPICAMSRC_MAX_FPS, 1, NULL);
    }
  }
done:
  GST_DEBUG_OBJECT (src, "get_caps returning %" GST_PTR_FORMAT, caps);
  return caps;
}

static gboolean
gst_rpi_cam_src_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (bsrc);
  GstVideoInfo info;
  GstStructure *structure;
  const gchar *profile_str = NULL;

  GST_DEBUG_OBJECT (src, "In set_caps %" GST_PTR_FORMAT, caps);
  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "video/x-h264")) {
    src->capture_config.encoding = MMAL_ENCODING_H264;
    profile_str = gst_structure_get_string (structure, "profile");
    if (profile_str) {
      if (g_str_equal (profile_str, "baseline"))
        src->capture_config.profile = MMAL_VIDEO_PROFILE_H264_BASELINE;
      else if (g_str_equal (profile_str, "constrained-baseline"))
        src->capture_config.profile =
            MMAL_VIDEO_PROFILE_H264_CONSTRAINED_BASELINE;
      else if (g_str_equal (profile_str, "main"))
        src->capture_config.profile = MMAL_VIDEO_PROFILE_H264_MAIN;
      else if (g_str_equal (profile_str, "high"))
        src->capture_config.profile = MMAL_VIDEO_PROFILE_H264_HIGH;
      else
        g_warning ("Unknown profile string in rpicamsrc caps: %s", profile_str);
    }
  } else if (gst_structure_has_name (structure, "image/jpeg")) {
#ifdef USE_JPEG_CODEC
    src->capture_config.encoding = MMAL_ENCODING_JPEG;
#else
    src->capture_config.encoding = MMAL_ENCODING_MJPEG;
#endif
  } else {
    /* Raw caps */
    switch (GST_VIDEO_INFO_FORMAT (&info)) {
      case GST_VIDEO_FORMAT_I420:
        src->capture_config.encoding = MMAL_ENCODING_I420;
        break;
      case GST_VIDEO_FORMAT_RGB:
        src->capture_config.encoding = MMAL_ENCODING_RGB24;
        break;
      case GST_VIDEO_FORMAT_BGR:
        src->capture_config.encoding = MMAL_ENCODING_BGR24;
        break;
      case GST_VIDEO_FORMAT_RGBA:
        src->capture_config.encoding = MMAL_ENCODING_RGBA;
        break;
      default:
        return FALSE;
    }
  }

  src->capture_config.width = info.width;
  src->capture_config.height = info.height;
  src->capture_config.fps_n = info.fps_n;
  src->capture_config.fps_d = info.fps_d;

  if (info.fps_n != 0 && info.fps_d != 0)
    src->duration = gst_util_uint64_scale_int (GST_SECOND, info.fps_d,
        info.fps_n);
  else
    src->duration = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static gboolean
gst_rpi_cam_src_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GST_LOG_OBJECT (bsrc, "In decide_allocation");
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
  GstClock *clock = NULL;
  GstClockTime base_time;

  if (!src->started) {
    g_mutex_lock (&src->config_lock);
    raspi_capture_update_config (src->capture_state, &src->capture_config,
        FALSE);
    src->capture_config.change_flags = 0;
    g_mutex_unlock (&src->config_lock);

    if (!raspi_capture_start (src->capture_state))
      return GST_FLOW_ERROR;
    src->started = TRUE;
  }

  GST_OBJECT_LOCK (src);
  if ((clock = GST_ELEMENT_CLOCK (src)) != NULL)
    gst_object_ref (clock);
  base_time = GST_ELEMENT_CAST (src)->base_time;
  GST_OBJECT_UNLOCK (src);

  g_mutex_lock (&src->config_lock);
  if (src->capture_config.change_flags) {
    raspi_capture_update_config (src->capture_state, &src->capture_config,
        TRUE);
    src->capture_config.change_flags = 0;
  }
  g_mutex_unlock (&src->config_lock);

  *buf = NULL;

  do {
    GstBuffer *cbuf = NULL;

    /* FIXME: Use custom allocator */
    ret =
        raspi_capture_fill_buffer (src->capture_state, &cbuf, clock, base_time);

    if (cbuf != NULL) {
      GST_LOG_OBJECT (src, "Made buffer of size %" G_GSIZE_FORMAT,
          gst_buffer_get_size (cbuf));

      if (*buf == NULL) {
        /* Only set the duration when we have a PTS update from the rpi encoder.
         * not every buffer is a frame */
        if (GST_BUFFER_PTS_IS_VALID (cbuf))
          GST_BUFFER_DURATION (cbuf) = src->duration;

        *buf = cbuf;
      } else {
        *buf = gst_buffer_append (*buf, cbuf);
      }
      cbuf = NULL;
    }
  } while (ret == GST_FLOW_KEEP_ACCUMULATING);

  if (ret == GST_FLOW_ERROR_TIMEOUT) {
    GST_ELEMENT_ERROR (src, STREAM, FAILED,
        ("Camera capture timed out."),
        ("Waiting for a buffer from the camera took too long."));
    ret = GST_FLOW_ERROR;
  }

  if (ret != GST_FLOW_OK)
    gst_clear_buffer (buf);

  if (clock)
    gst_object_unref (clock);
  return ret;
}

/**
 * plugin-rpicamsrc:
 *
 * Since: 1.18
 */
static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  GST_DEBUG_CATEGORY_INIT (gst_rpi_cam_src_debug, "rpicamsrc",
      0, "rpicamsrc debug");

  ret = gst_element_register (plugin, "rpicamsrc", GST_RANK_NONE,
      GST_TYPE_RPICAMSRC);

  ret &= gst_device_provider_register (plugin, "rpicamsrcdeviceprovider",
      GST_RANK_PRIMARY, GST_TYPE_RPICAMSRC_DEVICE_PROVIDER);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rpicamsrc,
    "Raspberry Pi Camera Source",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
