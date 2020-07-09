/*
 * GStreamer
 * Copyright (C) 2006 Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) 2015 anthony <<user@hostname.org>>
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
 * SECTION:element-qroverlay
 *
 * This element will build a Json string that contains a description of the
 * buffer and will convert the string to a QRcode. The QRcode contains a
 * timestamp, a buffer number, a framerate and some custom extra-data. Each
 * frame will have a Qrcode overlaid in the video stream. Some properties are
 * available to set the position and to define its size. You can add custom data
 * with the properties #qroverlay:extra-data-name and
 * #qroverlay:extra-data-array. You can also define the quality of the Qrcode
 * with #qroverlay:qrcode-error-correction. You can also define interval and
 * span of #qrovlerlay:extra-data-name #qrovlerlay:extra-data-array
 *
 * ## Example launch line
 *
 * ``` bash
 * gst-launch -v -m videotestsrc ! qroverlay ! fakesink silent=TRUE
 * ```
 *
 * Since: 1.20
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include <qrencode.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "gstqroverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_qr_overlay_debug);
#define GST_CAT_DEFAULT gst_qr_overlay_debug

enum
{
  PROP_0,
  PROP_X_AXIS,
  PROP_Y_AXIS,
  PROP_PIXEL_SIZE,
  PROP_DATA_INTERVAL_BUFFERS,
  PROP_DATA_SPAN_BUFFERS,
  PROP_EXTRA_DATA_NAME,
  PROP_EXTRA_DATA_ARRAY,
  PROP_QRCODE_ERROR_CORRECTION,
};


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

#define gst_qr_overlay_parent_class parent_class
G_DEFINE_TYPE (GstQROverlay, gst_qr_overlay, GST_TYPE_BASE_TRANSFORM);

static gboolean
gst_qr_overlay_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out);

static void gst_qr_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_qr_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_qr_overlay_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);

/* GObject vmethod implementations */

/* initialize the qroverlay's class */
static void
gst_qr_overlay_class_init (GstQROverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_qr_overlay_set_property;
  gobject_class->get_property = gst_qr_overlay_get_property;

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
          1, 100.0, 1, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_DATA_INTERVAL_BUFFERS,
      g_param_spec_int64 ("extra-data-interval-buffers",
          "extra-data-interval-buffers",
          "Extra data append into the Qrcode at the first buffer of each "
          " interval", 0, G_MAXINT64, 60, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_DATA_SPAN_BUFFERS, g_param_spec_int64 ("extra-data-span-buffers",
          "extra-data-span-buffers",
          "Numbers of consecutive buffers that the extra data will be inserted "
          " (counting the first buffer)", 0, G_MAXINT64, 1, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_EXTRA_DATA_NAME, g_param_spec_string ("extra-data-name",
          "Extra data name",
          "Json key name for extra append data", NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_EXTRA_DATA_ARRAY, g_param_spec_string ("extra-data-array",
          "Extra data array",
          "List of comma separated values that the extra data value will be "
          " cycled from at each interval, example array structure :"
          " \"240,480,720,960,1200,1440,1680,1920\"", NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_QRCODE_ERROR_CORRECTION,
      g_param_spec_enum ("qrcode-error-correction", "qrcode-error-correction",
          "qrcode-error-correction", GST_TYPE_QRCODE_QUALITY,
          DEFAULT_PROP_QUALITY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (gstelement_class,
      "qroverlay",
      "Qrcode overlay containing buffer information",
      "Overlay Qrcodes over each buffer with buffer information and custom data",
      "Anthony Violo <anthony.violo@ubicast.eu>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_template));

  gst_type_mark_as_plugin_api (GST_TYPE_QRCODE_QUALITY, 0);

  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_qr_overlay_transform_ip);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_qr_overlay_set_caps);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_qr_overlay_init (GstQROverlay * filter)
{
  filter->frame_number = 1;
  filter->x_percent = 50.0;
  filter->y_percent = 50.0;
  filter->qrcode_quality = DEFAULT_PROP_QUALITY;
  filter->level = QR_ECLEVEL_M;
  filter->extra_data_name = 0;
  filter->extra_data_array = 0;
  filter->extra_data_enabled = FALSE;
  filter->array_counter = 0;
  filter->array_size = 1;
  filter->extra_data_interval_buffers = 60;
  filter->extra_data_span_buffers = 1;
  filter->span_frame = 0;
  filter->qrcode_size = 1;
}

static void
gst_qr_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQROverlay *filter = GST_QR_OVERLAY (object);

  switch (prop_id) {
    case PROP_X_AXIS:
      filter->x_percent = g_value_get_float (value);
      break;
    case PROP_Y_AXIS:
      filter->y_percent = g_value_get_float (value);
      break;
    case PROP_PIXEL_SIZE:
      filter->qrcode_size = g_value_get_float (value);
      break;
    case PROP_QRCODE_ERROR_CORRECTION:
      filter->qrcode_quality = g_value_get_enum (value);
      break;
    case PROP_DATA_INTERVAL_BUFFERS:
      filter->extra_data_interval_buffers = g_value_get_int64 (value);
      break;
    case PROP_DATA_SPAN_BUFFERS:
      filter->extra_data_span_buffers = g_value_get_int64 (value);
      break;
    case PROP_EXTRA_DATA_NAME:
      filter->extra_data_name = g_value_dup_string (value);
      break;
    case PROP_EXTRA_DATA_ARRAY:
      filter->extra_data_array = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qr_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQROverlay *filter = GST_QR_OVERLAY (object);

  switch (prop_id) {
    case PROP_X_AXIS:
      g_value_set_float (value, filter->x_percent);
      break;
    case PROP_Y_AXIS:
      g_value_set_float (value, filter->y_percent);
      break;
    case PROP_PIXEL_SIZE:
      g_value_set_float (value, filter->qrcode_size);
      break;
    case PROP_QRCODE_ERROR_CORRECTION:
      g_value_set_enum (value, filter->qrcode_quality);
      break;
    case PROP_DATA_INTERVAL_BUFFERS:
      g_value_set_int64 (value, filter->extra_data_interval_buffers);
      break;
    case PROP_DATA_SPAN_BUFFERS:
      g_value_set_int64 (value, filter->extra_data_span_buffers);
      break;
    case PROP_EXTRA_DATA_NAME:
      g_value_set_string (value, filter->extra_data_name);
      break;
    case PROP_EXTRA_DATA_ARRAY:
      g_value_set_string (value, filter->extra_data_array);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_qr_overlay_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstQROverlay *filter = GST_QR_OVERLAY (trans);
  GstStructure *structure;
  const GValue *framerate_value;
  guint i;

  structure = gst_caps_get_structure (in, 0);
  framerate_value = gst_structure_get_value (structure, "framerate");
  filter->framerate_string = g_strdup_value_contents (framerate_value);
  filter->width =
      atoi (g_strdup_value_contents (gst_structure_get_value (structure,
              "width")));
  filter->height =
      atoi (g_strdup_value_contents (gst_structure_get_value (structure,
              "height")));
  switch (filter->qrcode_quality) {
    case 0:
      filter->level = QR_ECLEVEL_L;
      break;
    case 1:
      filter->level = QR_ECLEVEL_M;
      break;
    case 2:
      filter->level = QR_ECLEVEL_Q;
      break;
    case 3:
      filter->level = QR_ECLEVEL_H;
      break;
    default:
      GST_ERROR_OBJECT (filter, "Invalid level");
      break;
  }
  if (filter->extra_data_array && filter->extra_data_name
      && strlen (filter->extra_data_array) > 3
      && strlen (filter->extra_data_name) > 0) {
    filter->extra_data_enabled = TRUE;
    for (i = 0; filter->extra_data_array[i]; i++)
      if (filter->extra_data_array[i] == ',')
        filter->array_size++;
  }
  return TRUE;
}

static gchar *
parse_data_array (GstQROverlay * filter, gchar * value_in_array)
{
  guint value_size;
  guint i, j, k, count;

  GST_LOG_OBJECT (filter, "Select data in array");
  for (i = 0, count = 0; filter->extra_data_array[i]; i++) {
    if (filter->extra_data_array[i] == ',')
      count++;
    if (count == filter->array_counter)
      break;
  }
  for (j = i, value_size = 0; filter->extra_data_array[j]; j++) {
    value_size++;
    if (j > i && (filter->extra_data_array[j] == ','
            || filter->extra_data_array[j] == '"'))
      break;
  }
  if (!(value_in_array = malloc (value_size + 1 * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to value in array");
  for (j = i, k = 0; filter->extra_data_array[j]; j++) {
    if (j > i && (filter->extra_data_array[j] == ','
            || filter->extra_data_array[j] == '"'))
      break;
    else if (filter->extra_data_array[j] != ','
        && filter->extra_data_array[j] != '"') {
      value_in_array[k] = filter->extra_data_array[j];
      k++;
    }
  }
  value_in_array[k] = '\0';
  return value_in_array;
}

static gchar *
build_string (GstBaseTransform * base, GstBuffer * outbuf,
    gchar * encode_string)
{
  gchar *timestamp;
  gchar *value;
  gchar *buffer_count;
  gchar *framerate;
  gchar *element_name;
  gchar *name;
  gchar *extra_data_value = NULL;
  int size_string;

  GstQROverlay *filter = GST_QR_OVERLAY (base);
  GST_DEBUG_OBJECT (filter, "Build string will be encoded");
  /* Convert timestamp to string */
  if (!(timestamp = malloc (35 * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to timestamp");
  if (!(value = malloc (20 * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to timestamp value");
  sprintf (value, "%llu",
      (long long unsigned int) GST_BUFFER_TIMESTAMP (outbuf));
  strcpy (timestamp, "\"timestamp\":");
  timestamp = strcat (timestamp, value);
  timestamp = strcat (timestamp, ",");
  strcpy (encode_string, "{");
  GST_LOG_OBJECT (filter, "Build timestamp string");
  encode_string = strcat (encode_string, timestamp);
  g_free (value);
  g_free (timestamp);
  /* Convert frame number to string */
  if (!(buffer_count = malloc (23 * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to buffer count");
  if (!(value = malloc (7 * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to buffer counr value");
  strcpy (buffer_count, "\"buffercount\":");
  sprintf (value, "%d", filter->frame_number);
  buffer_count = strcat (buffer_count, value);
  buffer_count = strcat (buffer_count, ",");
  GST_LOG_OBJECT (filter, "Build buffer count string");
  encode_string = strcat (encode_string, buffer_count);
  g_free (value);
  g_free (buffer_count);
  /* Convert framerate to string */
  size_string = strlen (filter->framerate_string);
  if (!(framerate = malloc ((18 + size_string) * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to framerate");
  strcpy (framerate, "\"framerate\":\"");
  framerate = strcat (framerate, filter->framerate_string);
  framerate = strcat (framerate, "\",");
  GST_LOG_OBJECT (filter, "Build framerate string");
  encode_string = strcat (encode_string, framerate);
  g_free (framerate);
  /* element name string */
  name = GST_ELEMENT_NAME (base);
  size_string = strlen (name);
  if (!(element_name = malloc ((14 + size_string) * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to element name");
  strcpy (element_name, "\"name\":\"");
  element_name = strcat (element_name, name);
  element_name = strcat (element_name, "\",");
  GST_LOG_OBJECT (filter, "Build element name string");
  encode_string = strcat (encode_string, element_name);
  g_free (element_name);
  if (filter->extra_data_enabled &&
      (filter->frame_number == 1
          || filter->frame_number % filter->extra_data_interval_buffers == 1
          || (filter->span_frame > 0
              && filter->span_frame < filter->extra_data_span_buffers))) {
    extra_data_value = parse_data_array (filter, extra_data_value);
    GST_LOG_OBJECT (filter, "Build extra data string");
    encode_string = strcat (encode_string, "\"");
    encode_string = strcat (encode_string, filter->extra_data_name);
    encode_string = strcat (encode_string, "\":\"");
    encode_string = strcat (encode_string, extra_data_value);
    encode_string = strcat (encode_string, "\",");
    g_free (extra_data_value);
    filter->span_frame++;
    if (filter->span_frame == filter->extra_data_span_buffers) {
      filter->array_counter++;
      filter->span_frame = 0;
      if (filter->array_counter >= filter->array_size)
        filter->array_counter = 0;
    }
  }
  encode_string = strcat (encode_string, "}");
  GST_LOG_OBJECT (filter, "String built");
  return encode_string;
}

static void
overlay_qr_in_frame (GstQROverlay * filter, QRcode * qrcode, GstBuffer * outbuf)
{
  GstMapInfo current_info;
  guchar *source_data;
  register int32_t k, y, x, yy, realwidth, y_position, x_position, line = 0;
  int img_res, quarter_img_res;

  GST_DEBUG_OBJECT (filter, "Overlay QRcode in frame");
  gst_buffer_map (outbuf, &current_info, GST_MAP_WRITE);
  realwidth = (qrcode->width + 4 * 2) * filter->qrcode_size;
  /* White bg */
  x_position = (int) (filter->width - realwidth) * (filter->x_percent / 100);
  y_position = (int) (filter->height - realwidth) * (filter->y_percent / 100);
  x_position = GST_ROUND_DOWN_2 (x_position);
  y_position = GST_ROUND_DOWN_4 (y_position);
  GST_LOG_OBJECT (filter, "Add white background in frame");
  img_res = filter->width * filter->height;
  quarter_img_res = img_res / 4;
  for (y = y_position; y < realwidth + y_position; y++) {
    memset (current_info.data + (filter->width * y + x_position), 0xff,
        realwidth);
    if (y % 4 == 0) {
      memset (current_info.data + img_res + y / 4 * filter->width +
          x_position / 2, 128, realwidth / 2);
      memset (current_info.data + img_res + y / 4 * filter->width +
          x_position / 2 + quarter_img_res, 128, realwidth / 2);
      if (y < (realwidth + y_position) - 4) {
        memset (current_info.data + img_res + y / 4 * filter->width +
            x_position / 2 + (filter->width / 2), 128, realwidth / 2);
        memset (current_info.data + img_res + y / 4 * filter->width +
            x_position / 2 + quarter_img_res + (filter->width / 2), 128,
            realwidth / 2);
      }
    }
  }
  GST_LOG_OBJECT (filter, "Add data in frame");
  /* data */
  line += 4 * filter->qrcode_size * filter->width;
  source_data = qrcode->data;
  y = (int) (filter->height - realwidth) / 2;
  for (y = 0; y < qrcode->width; y++) {
    for (x = 0; x < (qrcode->width); x++) {
      for (yy = 0; yy < filter->qrcode_size; yy++) {
        k = ((((line + (4 * filter->qrcode_size))) + filter->width * yy +
                x * filter->qrcode_size) + x_position) +
            (y_position * filter->width);
        if (*source_data & 1) {
          memset (current_info.data + k, 0, filter->qrcode_size);
        }
      }
      source_data++;
    }
    line += (filter->width * filter->qrcode_size);
  }
  QRcode_free (qrcode);
  gst_buffer_unmap (outbuf, &current_info);
}

/* GstBaseTransform vmethod implementations */
/* this function does the actual processing
 */
static GstFlowReturn
gst_qr_overlay_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstQROverlay *filter = GST_QR_OVERLAY (base);
  QRcode *qrcode;
  gchar *encode_string;

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (outbuf)))
    gst_object_sync_values (GST_OBJECT (filter), GST_BUFFER_TIMESTAMP (outbuf));
  if (!(encode_string = malloc (150 * sizeof (char *))))
    GST_ERROR_OBJECT (filter, "can't alloc memory to timestamp");
  encode_string = build_string (base, outbuf, encode_string);
  GST_INFO_OBJECT (filter, "String will be encoded : %s", encode_string);
  qrcode =
      QRcode_encodeString ((const char *) encode_string, 0,
      filter->qrcode_quality, QR_MODE_8, 0);
  g_free (encode_string);
  if (qrcode) {
    GST_DEBUG_OBJECT (filter, "String encoded");
    overlay_qr_in_frame (filter, qrcode, outbuf);
    filter->frame_number++;
  } else
    GST_WARNING_OBJECT (filter, "Could not encode the string");
  return GST_FLOW_OK;
}

static gboolean
qroverlay_init (GstPlugin * qroverlay)
{
  GST_DEBUG_CATEGORY_INIT (gst_qr_overlay_debug, "qroverlay", 0,
      "Qrcode overlay element");

  return gst_element_register (qroverlay, "qroverlay", GST_RANK_NONE,
      GST_TYPE_QR_OVERLAY);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qroverlay,
    "libqrencode qroverlay plugin",
    qroverlay_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
