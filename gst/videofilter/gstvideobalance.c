/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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
#ifdef HAVE_LIBOIL
#include <liboil/liboil.h>
#endif
#include <string.h>
#include <math.h>

#include <gst/video/video.h>
#include <gst/interfaces/colorbalance.h>

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

#ifdef WIN32
#define rint(x) (floor((x)+0.5))
#endif

/* GstVideoBalance signals and args */
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

/*
 * look-up tables (LUT).
 */
static void
gst_video_balance_update_tables_planar411 (GstVideoBalance * vb)
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

  /* FIXME this is a bogus transformation for hue, but you get
   * the idea */
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
  videobalance->passthru = gst_video_balance_is_passthrough (videobalance);

  if (!videobalance->passthru) {
    gst_video_balance_update_tables_planar411 (videobalance);
  }
}

#ifndef HAVE_LIBOIL
static void
oil_tablelookup_u8 (guint8 * dest, int dstr, guint8 * src, int sstr,
    guint8 * table, int tstr, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    *dest = table[*src * tstr];
    dest += dstr;
    src += sstr;
  }
}
#endif

/* Useful macros */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))
#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static void
gst_video_balance_planar411_ip (GstVideoBalance * videobalance, guint8 * data,
    gint width, gint height)
{
  int x, y;
  guint8 *ydata;
  guint8 *udata, *vdata;
  gint ystride, ustride, vstride;
  gint width2, height2;

  ydata = data + GST_VIDEO_I420_Y_OFFSET (width, height);
  ystride = GST_VIDEO_I420_Y_ROWSTRIDE (width);

  for (y = 0; y < height; y++) {
    oil_tablelookup_u8 (ydata, 1, ydata, 1, videobalance->tabley, 1, width);
    ydata += ystride;
  }

  width2 = width >> 1;
  height2 = height >> 1;

  udata = data + GST_VIDEO_I420_U_OFFSET (width, height);
  vdata = data + GST_VIDEO_I420_V_OFFSET (width, height);
  ustride = GST_VIDEO_I420_U_ROWSTRIDE (width);
  vstride = GST_VIDEO_I420_V_ROWSTRIDE (width);

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
  GstVideoBalance *this;
  GstStructure *structure;
  gboolean res;

  this = GST_VIDEO_BALANCE (base);

  GST_DEBUG_OBJECT (this,
      "set_caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps, outcaps);

  structure = gst_caps_get_structure (incaps, 0);

  res = gst_structure_get_int (structure, "width", &this->width);
  res &= gst_structure_get_int (structure, "height", &this->height);
  if (!res)
    goto done;

  this->size = GST_VIDEO_I420_SIZE (this->width, this->height);

done:
  return res;
}

static GstFlowReturn
gst_video_balance_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstVideoBalance *videobalance;
  guint8 *data;
  guint size;

  videobalance = GST_VIDEO_BALANCE (base);

  /* if no change is needed, we are done */
  if (videobalance->passthru)
    goto done;

  data = GST_BUFFER_DATA (outbuf);
  size = GST_BUFFER_SIZE (outbuf);

  if (size < videobalance->size)
    goto wrong_size;

  gst_video_balance_planar411_ip (videobalance, data,
      videobalance->width, videobalance->height);

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
}


/****************
 * Boilerplate
 */
static GstVideoFilterClass *parent_class = NULL;

static void gst_video_balance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_balance_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

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
  GstVideoBalance *balance;
  gint i;

  balance = GST_VIDEO_BALANCE (object);

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
gst_video_balance_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  trans_class = GST_BASE_TRANSFORM_CLASS (g_class);

  parent_class = g_type_class_peek_parent (g_class);

  gobject_class->set_property = gst_video_balance_set_property;
  gobject_class->get_property = gst_video_balance_get_property;

  g_object_class_install_property (gobject_class, PROP_CONTRAST,
      g_param_spec_double ("contrast", "Contrast", "contrast",
          0.0, 2.0, DEFAULT_PROP_CONTRAST, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_BRIGHTNESS,
      g_param_spec_double ("brightness", "Brightness", "brightness",
          -1.0, 1.0, DEFAULT_PROP_BRIGHTNESS, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_HUE,
      g_param_spec_double ("hue", "Hue", "hue",
          -1.0, 1.0, DEFAULT_PROP_HUE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_SATURATION,
      g_param_spec_double ("saturation", "Saturation", "saturation",
          0.0, 2.0, DEFAULT_PROP_SATURATION, G_PARAM_READWRITE));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_video_balance_finalize);

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_balance_set_caps);
  trans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_video_balance_transform_ip);

#ifdef HAVE_LIBOIL
  oil_init ();
#endif
}

static void
gst_video_balance_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideoBalance *videobalance = GST_VIDEO_BALANCE (instance);
  const char *channels[4] = { "HUE", "SATURATION",
    "BRIGHTNESS", "CONTRAST"
  };
  gint i;

  GST_DEBUG ("gst_video_balance_init");

  /* do stuff */
  videobalance->contrast = DEFAULT_PROP_CONTRAST;
  videobalance->brightness = DEFAULT_PROP_BRIGHTNESS;
  videobalance->hue = DEFAULT_PROP_HUE;
  videobalance->saturation = DEFAULT_PROP_SATURATION;

  gst_video_balance_update_properties (videobalance);

  videobalance->tabley = g_new (guint8, 256);
  videobalance->tableu = g_new (guint8 *, 256);
  videobalance->tablev = g_new (guint8 *, 256);
  for (i = 0; i < 256; i++) {
    videobalance->tableu[i] = g_new (guint8, 256);
    videobalance->tablev[i] = g_new (guint8, 256);
  }

  /* Generate the channels list */
  for (i = 0; i < (sizeof (channels) / sizeof (char *)); i++) {
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

  g_return_if_fail (vb != NULL);
  g_return_if_fail (GST_IS_VIDEO_BALANCE (vb));
  g_return_if_fail (GST_IS_VIDEO_FILTER (vb));
  g_return_if_fail (channel->label != NULL);

  if (!g_ascii_strcasecmp (channel->label, "HUE")) {
    vb->hue = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
  } else if (!g_ascii_strcasecmp (channel->label, "SATURATION")) {
    vb->saturation = (value + 1000.0) * 2.0 / 2000.0;
  } else if (!g_ascii_strcasecmp (channel->label, "BRIGHTNESS")) {
    vb->brightness = (value + 1000.0) * 2.0 / 2000.0 - 1.0;
  } else if (!g_ascii_strcasecmp (channel->label, "CONTRAST")) {
    vb->contrast = (value + 1000.0) * 2.0 / 2000.0;
  }

  gst_video_balance_update_properties (vb);
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

static void
gst_video_balance_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoBalance *src;

  src = GST_VIDEO_BALANCE (object);

  GST_DEBUG ("gst_video_balance_set_property");
  switch (prop_id) {
    case PROP_CONTRAST:
      src->contrast = g_value_get_double (value);
      break;
    case PROP_BRIGHTNESS:
      src->brightness = g_value_get_double (value);
      break;
    case PROP_HUE:
      src->hue = g_value_get_double (value);
      break;
    case PROP_SATURATION:
      src->saturation = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  gst_video_balance_update_properties (src);
}

static void
gst_video_balance_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoBalance *src;

  src = GST_VIDEO_BALANCE (object);

  switch (prop_id) {
    case PROP_CONTRAST:
      g_value_set_double (value, src->contrast);
      break;
    case PROP_BRIGHTNESS:
      g_value_set_double (value, src->brightness);
      break;
    case PROP_HUE:
      g_value_set_double (value, src->hue);
      break;
    case PROP_SATURATION:
      g_value_set_double (value, src->saturation);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

GType
gst_video_balance_get_type (void)
{
  static GType video_balance_type = 0;

  if (!video_balance_type) {
    static const GTypeInfo video_balance_info = {
      sizeof (GstVideoBalanceClass),
      gst_video_balance_base_init,
      NULL,
      gst_video_balance_class_init,
      NULL,
      NULL,
      sizeof (GstVideoBalance),
      0,
      gst_video_balance_init,
    };

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

    video_balance_type = g_type_register_static (GST_TYPE_VIDEO_FILTER,
        "GstVideoBalance", &video_balance_info, 0);

    g_type_add_interface_static (video_balance_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (video_balance_type, GST_TYPE_COLOR_BALANCE,
        &colorbalance_info);
  }
  return video_balance_type;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "videobalance", GST_RANK_NONE,
      GST_TYPE_VIDEO_BALANCE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videobalance",
    "Changes hue, saturation, brightness etc. on video images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
