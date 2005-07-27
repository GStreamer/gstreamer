/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/video/video.h>

#include "gstvideoscale.h"
#include "vs_image.h"


/* debug variable definition */
GST_DEBUG_CATEGORY (videoscale_debug);

/* elementfactory information */
static GstElementDetails videoscale_details =
GST_ELEMENT_DETAILS ("Video scaler",
    "Filter/Effect/Video",
    "Resizes video",
    "Wim Taymans <wim.taymans@chello.be>");

enum
{
  PROP_0,
  PROP_METHOD
      /* FILL ME */
};

static GstStaticCaps gst_videoscale_format_caps[] = {
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_xBGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_BGR),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YVYU")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("UYVY")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("Y800")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YV12")),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_16),
  GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_15)
};

enum
{
  GST_VIDEOSCALE_RGBx = 0,
  GST_VIDEOSCALE_xRGB,
  GST_VIDEOSCALE_BGRx,
  GST_VIDEOSCALE_xBGR,
  GST_VIDEOSCALE_RGB,
  GST_VIDEOSCALE_BGR,
  GST_VIDEOSCALE_AYUV,
  GST_VIDEOSCALE_YUY2,
  GST_VIDEOSCALE_YVYU,
  GST_VIDEOSCALE_UYVY,
  GST_VIDEOSCALE_Y,
  GST_VIDEOSCALE_I420,
  GST_VIDEOSCALE_YV12,
  GST_VIDEOSCALE_RGB565,
  GST_VIDEOSCALE_RGB555
};

#define GST_TYPE_VIDEOSCALE_METHOD (gst_videoscale_method_get_type())
static GType
gst_videoscale_method_get_type (void)
{
  static GType videoscale_method_type = 0;
  static GEnumValue videoscale_methods[] = {
    {GST_VIDEOSCALE_POINT_SAMPLE, "0", "Point Sample (not implemented)"},
    {GST_VIDEOSCALE_NEAREST, "1", "Nearest"},
    {GST_VIDEOSCALE_BILINEAR, "2", "Bilinear"},
    {GST_VIDEOSCALE_BICUBIC, "3", "Bicubic (not implemented)"},
    {0, NULL, NULL},
  };

  if (!videoscale_method_type) {
    videoscale_method_type =
        g_enum_register_static ("GstVideoscaleMethod", videoscale_methods);
  }
  return videoscale_method_type;
}

static GstCaps *
gst_videoscale_get_capslist (void)
{
  static GstCaps *caps;

  if (caps == NULL) {
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; i < G_N_ELEMENTS (gst_videoscale_format_caps); i++)
      gst_caps_append (caps,
          gst_caps_make_writable
          (gst_static_caps_get (&gst_videoscale_format_caps[i])));
  }

  return caps;
}

static GstPadTemplate *
gst_videoscale_src_template_factory (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_caps_ref (gst_videoscale_get_capslist ()));
}

static GstPadTemplate *
gst_videoscale_sink_template_factory (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_caps_ref (gst_videoscale_get_capslist ()));
}


static void gst_videoscale_base_init (gpointer g_class);
static void gst_videoscale_class_init (GstVideoscaleClass * klass);
static void gst_videoscale_init (GstVideoscale * videoscale);
static gboolean gst_videoscale_handle_src_event (GstPad * pad,
    GstEvent * event);

/* base transform */
static GstCaps *gst_videoscale_transform_caps (GstBaseTransform * trans,
    GstPad * pad, GstCaps * caps);
static gboolean gst_videoscale_set_caps (GstBaseTransform * trans,
    GstCaps * in, GstCaps * out);
static guint gst_videoscale_get_size (GstBaseTransform * trans);
static GstFlowReturn gst_videoscale_transform (GstBaseTransform * trans,
    GstBuffer * in, GstBuffer * out);

static void gst_videoscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_videoscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;


GType
gst_videoscale_get_type (void)
{
  static GType videoscale_type = 0;

  if (!videoscale_type) {
    static const GTypeInfo videoscale_info = {
      sizeof (GstVideoscaleClass),
      gst_videoscale_base_init,
      NULL,
      (GClassInitFunc) gst_videoscale_class_init,
      NULL,
      NULL,
      sizeof (GstVideoscale),
      0,
      (GInstanceInitFunc) gst_videoscale_init,
    };

    videoscale_type =
        g_type_register_static (GST_TYPE_BASE_TRANSFORM, "GstVideoscale",
        &videoscale_info, 0);
  }
  return videoscale_type;
}

static void
gst_videoscale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videoscale_details);

  gst_element_class_add_pad_template (element_class,
      gst_videoscale_sink_template_factory ());
  gst_element_class_add_pad_template (element_class,
      gst_videoscale_src_template_factory ());
}

static void
gst_videoscale_class_init (GstVideoscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_videoscale_set_property;
  gobject_class->get_property = gst_videoscale_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_VIDEOSCALE_METHOD, 0, G_PARAM_READWRITE));

  trans_class->transform_caps = gst_videoscale_transform_caps;
  trans_class->set_caps = gst_videoscale_set_caps;
  trans_class->get_size = gst_videoscale_get_size;
  trans_class->transform = gst_videoscale_transform;

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_videoscale_init (GstVideoscale * videoscale)
{
  GstBaseTransform *trans = GST_BASE_TRANSFORM (videoscale);

  gst_pad_set_event_function (trans->srcpad, gst_videoscale_handle_src_event);

  videoscale->method = GST_VIDEOSCALE_NEAREST;
  /*videoscale->method = GST_VIDEOSCALE_BILINEAR; */
  /*videoscale->method = GST_VIDEOSCALE_POINT_SAMPLE; */
}


static void
gst_videoscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoscale *src = GST_VIDEOSCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      src->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videoscale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoscale *src = GST_VIDEOSCALE (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_videoscale_transform_caps (GstBaseTransform * trans, GstPad * pad,
    GstCaps * caps)
{
  GstVideoscale *videoscale;
  GstCaps *ret;
  int i;

  videoscale = GST_VIDEOSCALE (trans);

  ret = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *structure = gst_caps_get_structure (ret, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
    gst_structure_remove_field (structure, "pixel-aspect-ratio");
  }

  GST_DEBUG_OBJECT (pad, "returning caps: %" GST_PTR_FORMAT, ret);
  return ret;
}

static int
gst_videoscale_get_format (GstCaps * caps)
{
  int i;
  GstCaps *icaps, *scaps;

  for (i = 0; i < G_N_ELEMENTS (gst_videoscale_format_caps); i++) {
    scaps = gst_static_caps_get (&gst_videoscale_format_caps[i]);
    icaps = gst_caps_intersect (caps, scaps);
    if (!gst_caps_is_empty (icaps)) {
      gst_caps_unref (icaps);
      return i;
    }
    gst_caps_unref (icaps);
  }

  return -1;
}

static gboolean
gst_videoscale_set_caps (GstBaseTransform * trans, GstCaps * in, GstCaps * out)
{
  GstVideoscale *videoscale;
  gboolean ret;
  GstStructure *structure;

  videoscale = GST_VIDEOSCALE (trans);

  structure = gst_caps_get_structure (in, 0);
  ret = gst_structure_get_int (structure, "width", &videoscale->from_width);
  ret &= gst_structure_get_int (structure, "height", &videoscale->from_height);

  structure = gst_caps_get_structure (out, 0);
  ret &= gst_structure_get_int (structure, "width", &videoscale->to_width);
  ret &= gst_structure_get_int (structure, "height", &videoscale->to_height);

  /* fixme: par */
  GST_DEBUG_OBJECT (videoscale, "from=%dx%d to=%dx%d",
      videoscale->from_width, videoscale->from_height,
      videoscale->to_width, videoscale->to_height);

  videoscale->format = gst_videoscale_get_format (in);

  return ret;
}

#define ROUND_UP_2(x)  (((x)+1)&~1)
#define ROUND_UP_4(x)  (((x)+3)&~3)
#define ROUND_UP_8(x)  (((x)+7)&~7)

/* returns size of output buffer */
static gulong
gst_videoscale_prepare_sizes (GstVideoscale * videoscale, VSImage * src,
    VSImage * dest, gboolean reset_source)
{
  gulong size = 0;

  if (reset_source) {
    src->width = videoscale->from_width;
    src->height = videoscale->from_height;
  }

  dest->width = videoscale->to_width;
  dest->height = videoscale->to_height;

  switch (videoscale->format) {
    case GST_VIDEOSCALE_RGBx:
    case GST_VIDEOSCALE_xRGB:
    case GST_VIDEOSCALE_BGRx:
    case GST_VIDEOSCALE_xBGR:
    case GST_VIDEOSCALE_AYUV:
      src->stride = src->width * 4;
      dest->stride = dest->width * 4;
      size = dest->stride * dest->height;
      break;
    case GST_VIDEOSCALE_RGB:
    case GST_VIDEOSCALE_BGR:
      src->stride = ROUND_UP_4 (src->width * 3);
      dest->stride = ROUND_UP_4 (dest->width * 3);
      size = dest->stride * dest->height;
      break;
    case GST_VIDEOSCALE_YUY2:
    case GST_VIDEOSCALE_YVYU:
    case GST_VIDEOSCALE_UYVY:
      src->stride = ROUND_UP_4 (src->width * 2);
      dest->stride = ROUND_UP_4 (dest->width * 2);
      size = dest->stride * dest->height;
      break;
    case GST_VIDEOSCALE_Y:
      src->stride = ROUND_UP_4 (src->width);
      dest->stride = ROUND_UP_4 (dest->width);
      size = dest->stride * dest->height;
      break;
    case GST_VIDEOSCALE_I420:
    case GST_VIDEOSCALE_YV12:
    {
      gulong dest_u_stride;
      gulong dest_u_height;

      src->stride = ROUND_UP_4 (src->width);
      dest->stride = ROUND_UP_4 (dest->width);

      dest_u_height = ROUND_UP_2 (dest->height) / 2;
      dest_u_stride = ROUND_UP_4 (dest->stride / 2);

      size = dest->stride * ROUND_UP_2 (dest->height) +
          2 * dest_u_stride * dest_u_height;
      break;
    }
    case GST_VIDEOSCALE_RGB565:
      src->stride = ROUND_UP_4 (src->width * 2);
      dest->stride = ROUND_UP_4 (dest->width * 2);
      size = dest->stride * dest->height;
      break;
    case GST_VIDEOSCALE_RGB555:
      src->stride = ROUND_UP_4 (src->width * 2);
      dest->stride = ROUND_UP_4 (dest->width * 2);
      size = dest->stride * dest->height;
      break;
    default:
      g_warning ("don't know how to scale");
      break;
  }

  return size;
}

static guint
gst_videoscale_get_size (GstBaseTransform * trans)
{
  GstVideoscale *videoscale;
  VSImage dest;
  VSImage src;

  videoscale = GST_VIDEOSCALE (trans);

  return (guint) gst_videoscale_prepare_sizes (videoscale, &src, &dest, TRUE);
}

static void
gst_videoscale_prepare_images (GstVideoscale * videoscale, GstBuffer * in,
    GstBuffer * out, VSImage * src, VSImage * src_u, VSImage * src_v,
    VSImage * dest, VSImage * dest_u, VSImage * dest_v)
{
  src->pixels = GST_BUFFER_DATA (in);
  dest->pixels = GST_BUFFER_DATA (out);

  switch (videoscale->format) {
    case GST_VIDEOSCALE_I420:
    case GST_VIDEOSCALE_YV12:
      src_u->pixels = src->pixels + ROUND_UP_2 (src->height) * src->stride;
      src_u->height = ROUND_UP_2 (src->height) / 2;
      src_u->width = ROUND_UP_2 (src->width) / 2;
      src_u->stride = ROUND_UP_4 (src->stride / 2);
      memcpy (src_v, src_u, sizeof (*src_v));
      src_v->pixels = src_u->pixels + src_u->height * src_u->stride;

      dest_u->pixels = dest->pixels + ROUND_UP_2 (dest->height) * dest->stride;
      dest_u->height = ROUND_UP_2 (dest->height) / 2;
      dest_u->width = ROUND_UP_2 (dest->width) / 2;
      dest_u->stride = ROUND_UP_4 (dest->stride / 2);
      memcpy (dest_v, dest_u, sizeof (*dest_v));
      dest_v->pixels = dest_u->pixels + dest_u->height * dest_u->stride;
      break;
    default:
      break;
  }
}

static GstFlowReturn
gst_videoscale_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoscale *videoscale;
  GstFlowReturn ret;
  gulong size;
  VSImage dest;
  VSImage src;
  VSImage dest_u;
  VSImage src_u;
  VSImage dest_v;
  VSImage src_v;
  guint8 *tmpbuf;

  videoscale = GST_VIDEOSCALE (trans);

  /* FIXME: I can't figure out how passthru would work in 0.9 */

  size = gst_videoscale_prepare_sizes (videoscale, &src, &dest, TRUE);
  if (!size) {
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }

  gst_buffer_stamp (out, in);

  gst_videoscale_prepare_images (videoscale, in, out, &src, &src_u, &src_v,
      &dest, &dest_u, &dest_v);

  tmpbuf = g_malloc (dest.stride * 2);

  switch (videoscale->method) {
    case GST_VIDEOSCALE_NEAREST:
      switch (videoscale->format) {
        case GST_VIDEOSCALE_RGBx:
        case GST_VIDEOSCALE_xRGB:
        case GST_VIDEOSCALE_BGRx:
        case GST_VIDEOSCALE_xBGR:
        case GST_VIDEOSCALE_AYUV:
          vs_image_scale_nearest_RGBA (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_RGB:
        case GST_VIDEOSCALE_BGR:
          vs_image_scale_nearest_RGB (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_YUY2:
        case GST_VIDEOSCALE_YVYU:
          vs_image_scale_nearest_YUYV (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_UYVY:
          vs_image_scale_nearest_UYVY (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_Y:
          vs_image_scale_nearest_Y (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_I420:
        case GST_VIDEOSCALE_YV12:
          vs_image_scale_nearest_Y (&dest, &src, tmpbuf);
          vs_image_scale_nearest_Y (&dest_u, &src_u, tmpbuf);
          vs_image_scale_nearest_Y (&dest_v, &src_v, tmpbuf);
          break;
        case GST_VIDEOSCALE_RGB565:
          vs_image_scale_nearest_RGB565 (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_RGB555:
          vs_image_scale_nearest_RGB555 (&dest, &src, tmpbuf);
          break;
        default:
          g_warning ("don't know how to scale");
      }
      break;
    case GST_VIDEOSCALE_BILINEAR:
    case GST_VIDEOSCALE_BICUBIC:
      switch (videoscale->format) {
        case GST_VIDEOSCALE_RGBx:
        case GST_VIDEOSCALE_xRGB:
        case GST_VIDEOSCALE_BGRx:
        case GST_VIDEOSCALE_xBGR:
        case GST_VIDEOSCALE_AYUV:
          vs_image_scale_linear_RGBA (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_RGB:
        case GST_VIDEOSCALE_BGR:
          vs_image_scale_linear_RGB (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_YUY2:
        case GST_VIDEOSCALE_YVYU:
          vs_image_scale_linear_YUYV (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_UYVY:
          vs_image_scale_linear_UYVY (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_Y:
          vs_image_scale_linear_Y (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_I420:
        case GST_VIDEOSCALE_YV12:
          vs_image_scale_linear_Y (&dest, &src, tmpbuf);
          //memset (dest_u.pixels, 128, dest_u.stride * dest_u.height);
          //memset (dest_v.pixels, 128, dest_v.stride * dest_v.height);
          vs_image_scale_linear_Y (&dest_u, &src_u, tmpbuf);
          vs_image_scale_linear_Y (&dest_v, &src_v, tmpbuf);
          break;
        case GST_VIDEOSCALE_RGB565:
          vs_image_scale_linear_RGB565 (&dest, &src, tmpbuf);
          break;
        case GST_VIDEOSCALE_RGB555:
          vs_image_scale_linear_RGB555 (&dest, &src, tmpbuf);
          break;
        default:
          g_warning ("don't know how to scale");
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  g_free (tmpbuf);

  GST_LOG_OBJECT (videoscale, "pushing buffer of %d bytes",
      GST_BUFFER_SIZE (out));

done:
  return GST_FLOW_OK;
}

static gboolean
gst_videoscale_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoscale *videoscale;
  double a;
  GstStructure *structure;

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * videoscale->from_width / videoscale->to_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * videoscale->from_height / videoscale->to_height, NULL);
      }
      return gst_pad_event_default (pad, event);
    default:
      GST_DEBUG_OBJECT (videoscale, "passing on non-NAVIGATION event %p",
          event);
      return gst_pad_event_default (pad, event);
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "videoscale", GST_RANK_NONE,
          GST_TYPE_VIDEOSCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (videoscale_debug, "videoscale", 0,
      "videoscale element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoscale",
    "Resizes video", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
