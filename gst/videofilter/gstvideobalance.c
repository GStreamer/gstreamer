/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2010> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * This file was (probably) generated from gstvideobalance.c,
 * gstvideobalance.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

/**
 * SECTION:element-videobalance
 *
 * Adjusts brightness, contrast, hue, saturation on a video stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! videobalance saturation=0.0 ! ffmpegcolorspace ! ximagesink
 * ]| This pipeline converts the image to black and white by setting the
 * saturation to 0.0.
 * </refsect2>
 *
 * Last reviewed on 2006-03-03 (0.10.3)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideobalance.h"
#include <string.h>
#include <math.h>

#include <gst/controller/gstcontroller.h>
#include <gst/interfaces/colorbalance.h>

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

#ifdef WIN32
#define rint(x) (floor((x)+0.5))
#endif

/* GstVideoBalance properties */
#define DEFAULT_PROP_CONTRAST		1.0
#define DEFAULT_PROP_BRIGHTNESS		0.0
#define DEFAULT_PROP_HUE		0.0
#define DEFAULT_PROP_SATURATION		1.0

enum
{
  PROP_0,
  PROP_CONTRAST,
  PROP_BRIGHTNESS,
  PROP_HUE,
  PROP_SATURATION
};

static GstStaticPadTemplate gst_video_balance_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }"))
    );

static GstStaticPadTemplate gst_video_balance_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }"))
    );


static void gst_video_balance_colorbalance_init (GstColorBalanceClass * iface);
static void gst_video_balance_interface_init (GstImplementsInterfaceClass *
    klass);

static void gst_video_balance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_balance_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
_do_init (GType video_balance_type)
{
  static const GInterfaceInfo iface_info = {
    (GInterfaceInitFunc) gst_video_balance_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo colorbalance_info = {
    (GInterfaceInitFunc) gst_video_balance_colorbalance_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (video_balance_type,
      GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
  g_type_add_interface_static (video_balance_type, GST_TYPE_COLOR_BALANCE,
      &colorbalance_info);
}

GST_BOILERPLATE_FULL (GstVideoBalance, gst_video_balance, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER, _do_init);

/*
 * look-up tables (LUT).
 */
static void
gst_video_balance_update_tables (GstVideoBalance * vb)
{
  gint i, j;
  gdouble y, u, v, hue_cos, hue_sin;

  /* Y */
  for (i = 0; i < 256; i++) {
    y = 16 + ((i - 16) * vb->contrast + vb->brightness * 255);
    if (y < 0)
      y = 0;
    else if (y > 255)
      y = 255;
    vb->tabley[i] = rint (y);
  }

  hue_cos = cos (M_PI * vb->hue);
  hue_sin = sin (M_PI * vb->hue);

  /* U/V lookup tables are 2D, since we need both U/V for each table
   * separately. */
  for (i = -128; i < 128; i++) {
    for (j = -128; j < 128; j++) {
      u = 128 + ((i * hue_cos + j * hue_sin) * vb->saturation);
      v = 128 + ((-i * hue_sin + j * hue_cos) * vb->saturation);
      if (u < 0)
        u = 0;
      else if (u > 255)
        u = 255;
      if (v < 0)
        v = 0;
      else if (v > 255)
        v = 255;
      vb->tableu[i + 128][j + 128] = rint (u);
      vb->tablev[i + 128][j + 128] = rint (v);
    }
  }
}

static gboolean
gst_video_balance_is_passthrough (GstVideoBalance * videobalance)
{
  return videobalance->contrast == 1.0 &&
      videobalance->brightness == 0.0 &&
      videobalance->hue == 0.0 && videobalance->saturation == 1.0;
}

static void
gst_video_balance_update_properties (GstVideoBalance * videobalance)
{
  gboolean passthrough = gst_video_balance_is_passthrough (videobalance);

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (videobalance),
      passthrough);

  if (!passthrough)
    gst_video_balance_update_tables (videobalance);
}

static void
gst_video_balance_planar_yuv (GstVideoBalance * videobalance, guint8 * data)
{
  gint x, y;
  guint8 *ydata;
  guint8 *udata, *vdata;
  gint ystride, ustride, vstride;
  GstVideoFormat format;
  gint width, height;
  gint width2, height2;

  format = videobalance->format;
  width = videobalance->width;
  height = videobalance->height;

  ydata =
      data + gst_video_format_get_component_offset (format, 0, width, height);
  ystride = gst_video_format_get_row_stride (format, 0, width);

  for (y = 0; y < height; y++) {
    guint8 *yptr;

    yptr = ydata + y * ystride;
    for (x = 0; x < width; x++) {
      *ydata = videobalance->tabley[*ydata];
      ydata++;
    }
  }

  width2 = gst_video_format_get_component_width (format, 1, width);
  height2 = gst_video_format_get_component_height (format, 1, height);

  udata =
      data + gst_video_format_get_component_offset (format, 1, width, height);
  vdata =
      data + gst_video_format_get_component_offset (format, 2, width, height);
  ustride = gst_video_format_get_row_stride (format, 1, width);
  vstride = gst_video_format_get_row_stride (format, 1, width);

  for (y = 0; y < height2; y++) {
    guint8 *uptr, *vptr;
    guint8 u1, v1;

    uptr = udata + y * ustride;
    vptr = vdata + y * vstride;

    for (x = 0; x < width2; x++) {
      u1 = *uptr;
      v1 = *vptr;

      *uptr++ = videobalance->tableu[u1][v1];
      *vptr++ = videobalance->tablev[u1][v1];
    }
  }
}

/* get notified of caps and plug in the correct process function */
static gboolean
gst_video_balance_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoBalance *videobalance = GST_VIDEO_BALANCE (base);

  GST_DEBUG_OBJECT (videobalance,
      "in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  videobalance->process = NULL;

  if (!gst_video_format_parse_caps (incaps, &videobalance->format,
          &videobalance->width, &videobalance->height))
    goto invalid_caps;

  videobalance->size =
      gst_video_format_get_size (videobalance->format, videobalance->width,
      videobalance->height);

  switch (videobalance->format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      videobalance->process = gst_video_balance_planar_yuv;
      break;
    default:
      break;
  }

  return videobalance->process != NULL;

invalid_caps:
  GST_ERROR_OBJECT (videobalance, "Invalid caps: %" GST_PTR_FORMAT, incaps);
  return FALSE;
}

static void
gst_video_balance_before_transform (GstBaseTransform * base, GstBuffer * buf)
{
  GstVideoBalance *balance = GST_VIDEO_BALANCE (base);
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (buf);
  stream_time =
      gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (balance, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (balance), stream_time);
}

static GstFlowReturn
gst_video_balance_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstVideoBalance *videobalance = GST_VIDEO_BALANCE (base);
  guint8 *data;
  guint size;

  if (!videobalance->process)
    goto not_negotiated;

  /* if no change is needed, we are done */
  if (base->passthrough)
    goto done;

  data = GST_BUFFER_DATA (outbuf);
  size = GST_BUFFER_SIZE (outbuf);

  if (size != videobalance->size)
    goto wrong_size;

  videobalance->process (videobalance, data);

done:
  return GST_FLOW_OK;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_ERROR (videobalance, STREAM, FORMAT,
        (NULL), ("Invalid buffer size %d, expected %d", size,
            videobalance->size));
    return GST_FLOW_ERROR;
  }
not_negotiated:
  GST_ERROR_OBJECT (videobalance, "Not negotiated yet");
  return GST_FLOW_NOT_NEGOTIATED;
}

static void
gst_video_balance_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video balance",
      "Filter/Effect/Video",
      "Adjusts brightness, contrast, hue, saturation on a video stream",
      "David Schleef <ds@schleef.org>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_balance_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_video_balance_src_template));
}

static void
gst_video_balance_finalize (GObject * object)
{
  GList *channels = NULL;
  GstVideoBalance *balance = GST_VIDEO_BALANCE (object);
  gint i;

  if (balance->tableu) {
    for (i = 0; i < 256; i++)
      g_free (balance->tableu[i]);
    g_free (balance->tableu);
    balance->tableu = NULL;
  }

  if (balance->tablev) {
    for (i = 0; i < 256; i++)
      g_free (balance->tablev[i]);
    g_free (balance->tablev);
    balance->tablev = NULL;
  }

  if (balance->tabley) {
    g_free (balance->tabley);
    balance->tabley = NULL;
  }

  channels = balance->channels;
  while (channels) {
    GstColorBalanceChannel *channel = channels->data;

    g_object_unref (channel);
    channels->data = NULL;
    channels = g_list_next (channels);
  }

  if (balance->channels)
    g_list_free (balance->channels);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_video_balance_class_init (GstVideoBalanceClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_video_balance_finalize);
  gobject_class->set_property = gst_video_balance_set_property;
  gobject_class->get_property = gst_video_balance_get_property;

  g_object_class_install_property (gobject_class, PROP_CONTRAST,
      g_param_spec_double ("contrast", "Contrast", "contrast",
          0.0, 2.0, DEFAULT_PROP_CONTRAST,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BRIGHTNESS,
      g_param_spec_double ("brightness", "Brightness", "brightness", -1.0, 1.0,
          DEFAULT_PROP_BRIGHTNESS,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_HUE,
      g_param_spec_double ("hue", "Hue", "hue", -1.0, 1.0, DEFAULT_PROP_HUE,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SATURATION,
      g_param_spec_double ("saturation", "Saturation", "saturation", 0.0, 2.0,
          DEFAULT_PROP_SATURATION,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_balance_set_caps);
  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_video_balance_transform_ip);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_video_balance_before_transform);
}

static void
gst_video_balance_init (GstVideoBalance * videobalance,
    GstVideoBalanceClass * klass)
{
  const gchar *channels[4] = { "HUE", "SATURATION",
    "BRIGHTNESS", "CONTRAST"
  };
  gint i;

  /* Initialize propertiews */
  videobalance->contrast = DEFAULT_PROP_CONTRAST;
  videobalance->brightness = DEFAULT_PROP_BRIGHTNESS;
  videobalance->hue = DEFAULT_PROP_HUE;
  videobalance->saturation = DEFAULT_PROP_SATURATION;

  videobalance->tabley = g_new (guint8, 256);
  videobalance->tableu = g_new (guint8 *, 256);
  videobalance->tablev = g_new (guint8 *, 256);
  for (i = 0; i < 256; i++) {
    videobalance->tableu[i] = g_new (guint8, 256);
    videobalance->tablev[i] = g_new (guint8, 256);
  }

  gst_video_balance_update_properties (videobalance);

  /* Generate the channels list */
  for (i = 0; i < G_N_ELEMENTS (channels); i++) {
    GstColorBalanceChannel *channel;

    channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
    channel->label = g_strdup (channels[i]);
    channel->min_value = -1000;
    channel->max_value = 1000;

    videobalance->channels = g_list_append (videobalance->channels, channel);
  }
}

static gboolean
gst_video_balance_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  g_assert (type == GST_TYPE_COLOR_BALANCE);
  return TRUE;
}

static void
gst_video_balance_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_video_balance_interface_supported;
}

static const GList *
gst_video_balance_colorbalance_list_channels (GstColorBalance * balance)
{
  GstVideoBalance *videobalance = GST_VIDEO_BALANCE (balance);

  g_return_val_if_fail (videobalance != NULL, NULL);
  g_return_val_if_fail (GST_IS_VIDEO_BALANCE (videobalance), NULL);

  return videobalance->channels;
}

static void
gst_video_balance_colorbalance_set_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel, gint value)
{
  GstVideoBalance *vb = GST_VIDEO_BALANCE (balance);
  gdouble new_val;
  gboolean changed;

  g_return_if_fail (vb != NULL);
  g_return_if_fail (GST_IS_VIDEO_BALANCE (vb));
  g_return_if_fail (GST_IS_VIDEO_FILTER (vb));
  g_return_if_fail (channel->label != NULL);

  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
    changed = new_val != vb->hue;
    vb->hue = new_val;
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0;
    changed = new_val != vb->saturation;
    vb->saturation = new_val;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
    changed = new_val != vb->brightness;
    vb->brightness = new_val;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    new_val = (value + 1000.0) * 2.0 / 2000.0;
    changed = new_val != vb->contrast;
    vb->contrast = new_val;
  }

  gst_video_balance_update_properties (vb);

  gst_color_balance_value_changed (balance, channel,
      gst_color_balance_get_value (balance, channel));
}

static gint
gst_video_balance_colorbalance_get_value (GstColorBalance * balance,
    GstColorBalanceChannel * channel)
{
  GstVideoBalance *vb = GST_VIDEO_BALANCE (balance);
  gint value = 0;

  g_return_val_if_fail (vb != NULL, 0);
  g_return_val_if_fail (GST_IS_VIDEO_BALANCE (vb), 0);
  g_return_val_if_fail (channel->label != NULL, 0);

  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    value = (vb->hue + 1) * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    value = vb->saturation * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    value = (vb->brightness + 1) * 2000.0 / 2.0 - 1000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    value = vb->contrast * 2000.0 / 2.0 - 1000.0;
  }

  return value;
}

static void
gst_video_balance_colorbalance_init (GstColorBalanceClass * iface)
{
  GST_COLOR_BALANCE_TYPE (iface) = GST_COLOR_BALANCE_SOFTWARE;
  iface->list_channels = gst_video_balance_colorbalance_list_channels;
  iface->set_value = gst_video_balance_colorbalance_set_value;
  iface->get_value = gst_video_balance_colorbalance_get_value;
}

static GstColorBalanceChannel *
gst_video_balance_find_channel (GstVideoBalance * balance, const gchar * label)
{
  GList *l;

  for (l = balance->channels; l; l = l->next) {
    GstColorBalanceChannel *channel = l->data;

    if (g_ascii_strcasecmp (channel->label, label) == 0)
      return channel;
  }
  return NULL;
}

static void
gst_video_balance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoBalance *balance = GST_VIDEO_BALANCE (object);
  gdouble d;
  const gchar *label = NULL;

  switch (prop_id) {
    case PROP_CONTRAST:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing contrast from %lf to %lf",
          balance->contrast, d);
      if (d != balance->contrast)
        label = "CONTRAST";
      balance->contrast = d;
      break;
    case PROP_BRIGHTNESS:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing brightness from %lf to %lf",
          balance->brightness, d);
      if (d != balance->brightness)
        label = "BRIGHTNESS";
      balance->brightness = d;
      break;
    case PROP_HUE:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing hue from %lf to %lf", balance->hue,
          d);
      if (d != balance->hue)
        label = "HUE";
      balance->hue = d;
      break;
    case PROP_SATURATION:
      d = g_value_get_double (value);
      GST_DEBUG_OBJECT (balance, "Changing saturation from %lf to %lf",
          balance->saturation, d);
      if (d != balance->saturation)
        label = "SATURATION";
      balance->saturation = d;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_video_balance_update_properties (balance);

  if (label) {
    GstColorBalanceChannel *channel =
        gst_video_balance_find_channel (balance, label);
    gst_color_balance_value_changed (GST_COLOR_BALANCE (balance), channel,
        gst_color_balance_get_value (GST_COLOR_BALANCE (balance), channel));
  }
}

static void
gst_video_balance_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoBalance *balance = GST_VIDEO_BALANCE (object);

  switch (prop_id) {
    case PROP_CONTRAST:
      g_value_set_double (value, balance->contrast);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_double (value, balance->brightness);
      break;
    case PROP_HUE:
      g_value_set_double (value, balance->hue);
      break;
    case PROP_SATURATION:
      g_value_set_double (value, balance->saturation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
