/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Jan Schmidt <thaytan@mad.scientist.com>
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
#include <pixbufscale.h>
#include <gst/video/video.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* debug variable definition */
GST_DEBUG_CATEGORY (pixbufscale_debug);
#define GST_CAT_DEFAULT pixbufscale_debug

/* elementfactory information */
static GstElementDetails pixbufscale_details =
GST_ELEMENT_DETAILS ("Gdk Pixbuf scaler",
    "Filter/Effect/Video",
    "Resizes video",
    "Jan Schmidt <thaytan@mad.scientist.com>\nWim Taymans <wim.taymans@chello.be>");

/* GstPixbufScale signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

static GstStaticPadTemplate gst_pixbufscale_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate gst_pixbufscale_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

#define GST_TYPE_PIXBUFSCALE_METHOD (gst_pixbufscale_method_get_type())
static GType
gst_pixbufscale_method_get_type (void)
{
  static GType pixbufscale_method_type = 0;
  static GEnumValue pixbufscale_methods[] = {
    {GST_PIXBUFSCALE_NEAREST, "0", "Nearest Neighbour"},
    {GST_PIXBUFSCALE_TILES, "1", "Tiles"},
    {GST_PIXBUFSCALE_BILINEAR, "2", "Bilinear"},
    {GST_PIXBUFSCALE_HYPER, "3", "Hyper"},
    {0, NULL, NULL},
  };

  if (!pixbufscale_method_type) {
    pixbufscale_method_type =
        g_enum_register_static ("GstPixbufScaleMethod", pixbufscale_methods);
  }
  return pixbufscale_method_type;
}

static void gst_pixbufscale_base_init (gpointer g_class);
static void gst_pixbufscale_class_init (GstPixbufScaleClass * klass);
static void gst_pixbufscale_init (GstPixbufScale * pixbufscale);
static gboolean gst_pixbufscale_handle_src_event (GstPad * pad,
    GstEvent * event);

static void gst_pixbufscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pixbufscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_pixbufscale_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

GType
gst_pixbufscale_get_type (void)
{
  static GType pixbufscale_type = 0;

  if (!pixbufscale_type) {
    static const GTypeInfo pixbufscale_info = {
      sizeof (GstPixbufScaleClass),
      gst_pixbufscale_base_init,
      NULL,
      (GClassInitFunc) gst_pixbufscale_class_init,
      NULL,
      NULL,
      sizeof (GstPixbufScale),
      0,
      (GInstanceInitFunc) gst_pixbufscale_init,
    };

    pixbufscale_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstPixbufScale",
        &pixbufscale_info, 0);
  }
  return pixbufscale_type;
}

static void
gst_pixbufscale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &pixbufscale_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pixbufscale_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pixbufscale_sink_template));
}
static void
gst_pixbufscale_class_init (GstPixbufScaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_PIXBUFSCALE_METHOD, GST_PIXBUFSCALE_BILINEAR,
          G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_pixbufscale_set_property;
  gobject_class->get_property = gst_pixbufscale_get_property;
}

static GstCaps *
gst_pixbufscale_getcaps (GstPad * pad)
{
  GstPixbufScale *pixbufscale;
  GstCaps *othercaps;
  GstCaps *caps;
  GstPad *otherpad;
  int i;

  pixbufscale = GST_PIXBUFSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == pixbufscale->srcpad) ? pixbufscale->sinkpad :
      pixbufscale->srcpad;
  othercaps = gst_pad_get_allowed_caps (otherpad);

  caps = gst_caps_copy (othercaps);
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 16, G_MAXINT, NULL);
  }

  GST_DEBUG ("getcaps are: %" GST_PTR_FORMAT, caps);
  return caps;
}

static GstPadLinkReturn
gst_pixbufscale_link (GstPad * pad, const GstCaps * caps)
{
  GstPixbufScale *pixbufscale;
  GstPadLinkReturn ret;
  GstPad *otherpad;
  GstStructure *structure;
  int height, width;

  GST_DEBUG ("gst_pixbufscale_link %s\n", gst_caps_to_string (caps));
  pixbufscale = GST_PIXBUFSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == pixbufscale->srcpad) ? pixbufscale->sinkpad :
      pixbufscale->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);

  ret = gst_pad_try_set_caps (otherpad, caps);
  if (ret == GST_PAD_LINK_OK) {
    /* cool, we can use passthru */

    pixbufscale->to_width = width;
    pixbufscale->to_height = height;
    pixbufscale->from_width = width;
    pixbufscale->from_height = height;

    pixbufscale->from_buf_size = width * height * 3;
    pixbufscale->to_buf_size = width * height * 3;

    pixbufscale->inited = TRUE;

    return GST_PAD_LINK_OK;
  }

  if (gst_pad_is_negotiated (otherpad)) {
    GstCaps *newcaps = gst_caps_copy (caps);

    if (pad == pixbufscale->srcpad) {
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, pixbufscale->from_width,
          "height", G_TYPE_INT, pixbufscale->from_height, NULL);
    } else {
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, pixbufscale->to_width,
          "height", G_TYPE_INT, pixbufscale->to_height, NULL);
    }
    ret = gst_pad_try_set_caps (otherpad, newcaps);
    if (GST_PAD_LINK_FAILED (ret)) {
      return GST_PAD_LINK_REFUSED;
    }
  }

  pixbufscale->passthru = FALSE;

  if (pad == pixbufscale->srcpad) {
    pixbufscale->to_width = width;
    pixbufscale->to_height = height;
  } else {
    pixbufscale->from_width = width;
    pixbufscale->from_height = height;
  }

  if (gst_pad_is_negotiated (otherpad)) {
    pixbufscale->from_buf_size = pixbufscale->from_width *
        pixbufscale->from_height * 3;
    pixbufscale->to_buf_size = pixbufscale->to_width *
        pixbufscale->to_height * 3;
    pixbufscale->inited = TRUE;
  }

  return GST_PAD_LINK_OK;
}

static void
gst_pixbufscale_init (GstPixbufScale * pixbufscale)
{
  GST_DEBUG_OBJECT (pixbufscale, "_init");
  pixbufscale->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_pixbufscale_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (pixbufscale), pixbufscale->sinkpad);
  gst_pad_set_chain_function (pixbufscale->sinkpad, gst_pixbufscale_chain);
  gst_pad_set_link_function (pixbufscale->sinkpad, gst_pixbufscale_link);
  gst_pad_set_getcaps_function (pixbufscale->sinkpad, gst_pixbufscale_getcaps);

  pixbufscale->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_pixbufscale_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (pixbufscale), pixbufscale->srcpad);
  gst_pad_set_event_function (pixbufscale->srcpad,
      gst_pixbufscale_handle_src_event);
  gst_pad_set_link_function (pixbufscale->srcpad, gst_pixbufscale_link);
  gst_pad_set_getcaps_function (pixbufscale->srcpad, gst_pixbufscale_getcaps);

  pixbufscale->inited = FALSE;

  pixbufscale->method = GST_PIXBUFSCALE_TILES;
  pixbufscale->gdk_method = GDK_INTERP_TILES;
}

static gboolean
gst_pixbufscale_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstPixbufScale *pixbufscale;
  double a;
  GstStructure *structure;
  GstEvent *new_event;

  pixbufscale = GST_PIXBUFSCALE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      structure = gst_structure_copy (event->event_data.structure.structure);
      if (gst_structure_get_double (event->event_data.structure.structure,
              "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * pixbufscale->from_width / pixbufscale->to_width, NULL);
      }
      if (gst_structure_get_double (event->event_data.structure.structure,
              "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * pixbufscale->from_height / pixbufscale->to_height, NULL);
      }
      gst_event_unref (event);
      new_event = gst_event_new (GST_EVENT_NAVIGATION);
      new_event->event_data.structure.structure = structure;
      return gst_pad_event_default (pad, new_event);
      break;
    default:
      return gst_pad_event_default (pad, event);
      break;
  }
}

static void
pixbufscale_scale (GstPixbufScale * scale, unsigned char *dest,
    unsigned char *src)
{
  GdkPixbuf *src_pixbuf, *dest_pixbuf;

  src_pixbuf = gdk_pixbuf_new_from_data
      (src, GDK_COLORSPACE_RGB, FALSE,
      8, scale->from_width, scale->from_height,
      3 * scale->from_width, NULL, NULL);
  dest_pixbuf = gdk_pixbuf_new_from_data
      (dest, GDK_COLORSPACE_RGB, FALSE,
      8, scale->to_width, scale->to_height, 3 * scale->to_width, NULL, NULL);
  gdk_pixbuf_scale (src_pixbuf, dest_pixbuf, 0, 0, scale->to_width,
      scale->to_height, 0, 0,
      (double) scale->to_width / scale->from_width,
      (double) scale->to_height / scale->from_height, scale->gdk_method);

  dest_pixbuf = gdk_pixbuf_scale_simple
      (src_pixbuf, scale->to_width, scale->to_height, scale->gdk_method);

  g_object_unref (src_pixbuf);
  g_object_unref (dest_pixbuf);
}

static void
gst_pixbufscale_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstPixbufScale *pixbufscale;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  pixbufscale = GST_PIXBUFSCALE (gst_pad_get_parent (pad));
  g_return_if_fail (pixbufscale->inited);

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (pixbufscale->passthru) {
    GST_LOG_OBJECT (pixbufscale, "passing through buffer of %ld bytes in '%s'",
        size, GST_OBJECT_NAME (pixbufscale));
    gst_pad_push (pixbufscale->srcpad, GST_DATA (buf));
    return;
  }

  GST_LOG_OBJECT (pixbufscale, "got buffer of %ld bytes in '%s'", size,
      GST_OBJECT_NAME (pixbufscale));
  GST_LOG_OBJECT (pixbufscale,
      "size=%ld from=%dx%d to=%dx%d fromsize=%ld (should be %d) tosize=%d",
      size, pixbufscale->from_width, pixbufscale->from_height,
      pixbufscale->to_width, pixbufscale->to_height, size,
      pixbufscale->from_buf_size, pixbufscale->to_buf_size);

  g_return_if_fail (size == pixbufscale->from_buf_size);

  outbuf = gst_pad_alloc_buffer (pixbufscale->srcpad,
      GST_BUFFER_OFFSET_NONE, pixbufscale->to_buf_size);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

  pixbufscale_scale (pixbufscale, GST_BUFFER_DATA (outbuf), data);

  GST_DEBUG_OBJECT (pixbufscale, "pushing buffer of %d bytes in '%s'",
      GST_BUFFER_SIZE (outbuf), GST_OBJECT_NAME (pixbufscale));

  gst_pad_push (pixbufscale->srcpad, GST_DATA (outbuf));

  gst_buffer_unref (buf);
}

static void
gst_pixbufscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPixbufScale *src;

  g_return_if_fail (GST_IS_PIXBUFSCALE (object));
  src = GST_PIXBUFSCALE (object);

  switch (prop_id) {
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      switch (src->method) {
        case GST_PIXBUFSCALE_NEAREST:
          src->gdk_method = GDK_INTERP_NEAREST;
          break;
        case GST_PIXBUFSCALE_TILES:
          src->gdk_method = GDK_INTERP_TILES;
          break;
        case GST_PIXBUFSCALE_BILINEAR:
          src->gdk_method = GDK_INTERP_BILINEAR;
          break;
        case GST_PIXBUFSCALE_HYPER:
          src->gdk_method = GDK_INTERP_HYPER;
          break;
      }
      break;
    default:
      break;
  }
}

static void
gst_pixbufscale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPixbufScale *src;

  g_return_if_fail (GST_IS_PIXBUFSCALE (object));
  src = GST_PIXBUFSCALE (object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


gboolean
pixbufscale_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "pixbufscale", GST_RANK_NONE,
          GST_TYPE_PIXBUFSCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (pixbufscale_debug, "pixbufscale", 0,
      "pixbufscale element");

  return TRUE;
}
