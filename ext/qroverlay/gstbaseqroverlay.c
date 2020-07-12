/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (c) 2020 Anthony Violo <anthony.violo@ubicast.eu>
 * Copyright (c) 2020 Thibault Saunier <tsaunier@igalia.com>
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
 * plugin-qroverlay
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <json-glib/json-glib.h>

#include <qrencode.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gstbaseqroverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_base_qr_overlay_debug);
#define GST_CAT_DEFAULT gst_base_qr_overlay_debug

enum
{
  PROP_0,
  PROP_X_AXIS,
  PROP_Y_AXIS,
  PROP_PIXEL_SIZE,
  PROP_QRCODE_ERROR_CORRECTION,
};

typedef struct _GstBaseQROverlayPrivate GstBaseQROverlayPrivate;
struct _GstBaseQROverlayPrivate
{
  gfloat qrcode_size;
  guint qrcode_quality;
  guint span_frame;
  QRecLevel level;
  gfloat x_percent;
  gfloat y_percent;
};

#define PRIV(s) gst_base_qr_overlay_get_instance_private (GST_BASE_QR_OVERLAY (s))

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { I420 }, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { I420 }, "
        "framerate = (fraction) [0, MAX], "
        "width = (int) [ 16, MAX ], " "height = (int) [ 16, MAX ]")
    );

#define DEFAULT_PROP_QUALITY    1
#define DEFAULT_PROP_PIXEL_SIZE    3

#define GST_TYPE_QRCODE_QUALITY (gst_qrcode_quality_get_type())
static GType
gst_qrcode_quality_get_type (void)
{
  static GType qrcode_quality_type = 0;

  static const GEnumValue qrcode_quality[] = {
    {0, "Level L", "Approx 7%"},
    {1, "Level M", "Approx 15%"},
    {2, "Level Q", "Approx 25%"},
    {3, "Level H", "Approx 30%"},
    {0, NULL, NULL},
  };

  if (!qrcode_quality_type) {
    qrcode_quality_type =
        g_enum_register_static ("GstQrcodeOverlayCorrection", qrcode_quality);
  }
  return qrcode_quality_type;
}

#define gst_base_qr_overlay_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstBaseQROverlay, gst_base_qr_overlay,
    GST_TYPE_VIDEO_FILTER);

static void gst_base_qr_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_qr_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn
gst_base_qr_overlay_transform_frame_ip (GstVideoFilter * base,
    GstVideoFrame * frame);

/* GObject vmethod implementations */

/* initialize the qroverlay's class */
static void
gst_base_qr_overlay_class_init (GstBaseQROverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_base_qr_overlay_set_property;
  gobject_class->get_property = gst_base_qr_overlay_get_property;

  g_object_class_install_property (gobject_class,
      PROP_X_AXIS, g_param_spec_float ("x",
          "X position (in percent of the width)",
          "X position (in percent of the width)",
          0.0, 100.0, 50.0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_Y_AXIS, g_param_spec_float ("y",
          "Y position (in percent of the height)",
          "Y position (in percent of the height)",
          0.0, 100.0, 50.0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_PIXEL_SIZE, g_param_spec_float ("pixel-size",
          "pixel-size", "Pixel size of each Qrcode pixel",
          1, 100.0, DEFAULT_PROP_PIXEL_SIZE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_QRCODE_ERROR_CORRECTION,
      g_param_spec_enum ("qrcode-error-correction", "qrcode-error-correction",
          "qrcode-error-correction", GST_TYPE_QRCODE_QUALITY,
          DEFAULT_PROP_QUALITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gst_type_mark_as_plugin_api (GST_TYPE_QRCODE_QUALITY, 0);

  GST_VIDEO_FILTER_CLASS (klass)->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_base_qr_overlay_transform_frame_ip);
  gst_type_mark_as_plugin_api (GST_TYPE_QRCODE_QUALITY, 0);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_base_qr_overlay_init (GstBaseQROverlay * filter)
{
  GstBaseQROverlayPrivate *priv = PRIV (filter);

  priv->x_percent = 50.0;
  priv->y_percent = 50.0;
  priv->qrcode_quality = DEFAULT_PROP_QUALITY;
  priv->span_frame = 0;
  priv->qrcode_size = DEFAULT_PROP_PIXEL_SIZE;
}

static void
gst_base_qr_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseQROverlayPrivate *priv = PRIV (object);

  switch (prop_id) {
    case PROP_X_AXIS:
      priv->x_percent = g_value_get_float (value);
      break;
    case PROP_Y_AXIS:
      priv->y_percent = g_value_get_float (value);
      break;
    case PROP_PIXEL_SIZE:
      priv->qrcode_size = g_value_get_float (value);
      break;
    case PROP_QRCODE_ERROR_CORRECTION:
      priv->qrcode_quality = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_qr_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseQROverlayPrivate *priv = PRIV (object);

  switch (prop_id) {
    case PROP_X_AXIS:
      g_value_set_float (value, priv->x_percent);
      break;
    case PROP_Y_AXIS:
      g_value_set_float (value, priv->y_percent);
      break;
    case PROP_PIXEL_SIZE:
      g_value_set_float (value, priv->qrcode_size);
      break;
    case PROP_QRCODE_ERROR_CORRECTION:
      g_value_set_enum (value, priv->qrcode_quality);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
overlay_qr_in_frame (GstBaseQROverlay * filter, QRcode * qrcode,
    GstVideoFrame * frame)
{
  GstBaseQROverlayPrivate *priv = PRIV (filter);
  guchar *source_data;
  gint32 k, y, x, yy, square_size, line = 0;
  int x1, x2, y1, y2;
  guint8 *d;
  gint stride;

  square_size = (qrcode->width + 4 * 2) * priv->qrcode_size;
  /* White bg */
  x1 = (int) (GST_VIDEO_FRAME_WIDTH (frame) -
      square_size) * (priv->x_percent / 100);
  x1 = GST_ROUND_DOWN_2 (x1);
  x2 = x1 + square_size;
  y1 = (int) (GST_VIDEO_FRAME_HEIGHT (frame) -
      square_size) * (priv->y_percent / 100);
  y1 = GST_ROUND_DOWN_4 (y1);
  y2 = y1 + square_size;

  d = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

  /* Start drawing the white luma plane */
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x += square_size)
      memset (&d[y * stride + x], 0xff, square_size);
  }

  /* Draw the black QR code blocks with 4px white space around it
   * on top */
  line += 4 * priv->qrcode_size * stride;
  source_data = qrcode->data;
  for (y = 0; y < qrcode->width; y++) {
    for (x = 0; x < (qrcode->width); x++) {
      for (yy = 0; yy < priv->qrcode_size; yy++) {
        k = ((((line + (4 * priv->qrcode_size))) + stride * yy +
                x * priv->qrcode_size) + x1) + (y1 * stride);
        if (*source_data & 1) {
          memset (d + k, 0, priv->qrcode_size);
        }
      }
      source_data++;
    }
    line += (stride * priv->qrcode_size);
  }

  /* Set Chrominance planes */
  x1 /= 2;
  x2 /= 2;
  y1 /= 2;
  y2 /= 2;
  stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
  for (y = y1; y < y2; y++) {
    for (x = x1; x < x2; x += (x2 - x1)) {
      d = GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
      memset (&d[y * stride + x], 128, (x2 - x1));
      d = GST_VIDEO_FRAME_PLANE_DATA (frame, 2);
      memset (&d[y * stride + x], 128, (x2 - x1));
    }
  }

  QRcode_free (qrcode);
}

/* GstBaseTransform vmethod implementations */
/* this function does the actual processing
 */
static GstFlowReturn
gst_base_qr_overlay_transform_frame_ip (GstVideoFilter * base,
    GstVideoFrame * frame)
{
  GstBaseQROverlayPrivate *priv = PRIV (base);
  QRcode *qrcode;
  gchar *content;
  GstClockTime rtime =
      gst_segment_to_running_time (&GST_BASE_TRANSFORM (base)->segment,
      GST_FORMAT_TIME, GST_BUFFER_PTS (frame->buffer));

  if (GST_CLOCK_TIME_IS_VALID (rtime))
    gst_object_sync_values (GST_OBJECT (base), rtime);

  content =
      GST_BASE_QR_OVERLAY_GET_CLASS (base)->get_content (GST_BASE_QR_OVERLAY
      (base), frame);
  GST_INFO_OBJECT (base, "String will be encoded : %s", content);
  qrcode = QRcode_encodeString (content, 0, priv->qrcode_quality, QR_MODE_8, 0);
  if (qrcode) {
    GST_DEBUG_OBJECT (base, "String encoded");
    overlay_qr_in_frame (GST_BASE_QR_OVERLAY (base), qrcode, frame);
  } else {
    GST_WARNING_OBJECT (base, "Could not encode content: %s", content);
  }
  g_free (content);

  return GST_FLOW_OK;
}
