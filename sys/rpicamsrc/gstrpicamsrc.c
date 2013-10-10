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
};

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

static GstStaticPadTemplate video_src_template =
  GST_STATIC_PAD_TEMPLATE ("vidsrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RAW_AND_JPEG_CAPS "; " H264_CAPS)
    );
static GstStaticPadTemplate viewfind_src_template =
  GST_STATIC_PAD_TEMPLATE ("vfsrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RAW_AND_JPEG_CAPS "; " H264_CAPS)
    );
static GstStaticPadTemplate image_src_template =
  GST_STATIC_PAD_TEMPLATE ("imgsrc",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (RAW_AND_JPEG_CAPS)
    );

#define gst_rpi_cam_src_parent_class parent_class
G_DEFINE_TYPE (GstRpiCamSrc, gst_rpi_cam_src, GST_TYPE_BASE_CAMERA_SRC);

static void gst_rpi_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rpi_cam_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean
gst_rpi_cam_src_setup_pipeline (GstBaseCameraSrc *parent)
{
  GstRpiCamSrc *self = GST_RPICAMSRC(parent);
  g_print ("In setup_pipeline\n");
}

static void
gst_rpi_cam_src_class_init (GstRpiCamSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseCameraSrcClass *basecamsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  basecamsrc_class = (GstBaseCameraSrcClass *) klass;

  gobject_class->set_property = gst_rpi_cam_src_set_property;
  gobject_class->get_property = gst_rpi_cam_src_get_property;

  gst_element_class_set_static_metadata (gstelement_class,
      "Raspberry Pi Camera Source",
      "Source/Video",
      "Raspberry Pi camera module source",
    "Jan Schmidt <jan@centricular.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&viewfind_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&image_src_template));

  basecamsrc_class->setup_pipeline = gst_rpi_cam_src_setup_pipeline;
}

static void
gst_rpi_cam_src_init (GstRpiCamSrc *src)
{
}

static void
gst_rpi_cam_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRpiCamSrc *src = GST_RPICAMSRC (object);

  switch (prop_id) {
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
rpicamsrc_init (GstPlugin * rpicamsrc)
{
  GST_DEBUG_CATEGORY_INIT (gst_rpi_cam_src_debug, "rpicamsrc",
      0, "rpicamsrc debug");

  return gst_element_register (rpicamsrc, "rpicamsrc", GST_RANK_NONE,
      GST_TYPE_RPICAMSRC);
}

#ifndef PACKAGE
#define PACKAGE "gstrpicamsrc"
#endif

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rpicamsrc,
    "Raspberry Pi Camera Source",
    rpicamsrc_init,
    VERSION,
    "LGPL",
    "GStreamer",
    "http://gstreamer.net/"
)
