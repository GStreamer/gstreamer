/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include <gst/gst.h>
#include <gst/video/video.h>

#include <string.h>

#define GST_TYPE_ALPHA_COLOR \
  (gst_alpha_color_get_type())
#define GST_ALPHA_COLOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ALPHA_COLOR,GstAlphaColor))
#define GST_ALPHA_COLOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ALPHA_COLOR,GstAlphaColorClass))
#define GST_IS_ALPHA_COLOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ALPHA_COLOR))
#define GST_IS_ALPHA_COLOR_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ALPHA_COLOR))

typedef struct _GstAlphaColor GstAlphaColor;
typedef struct _GstAlphaColorClass GstAlphaColorClass;

struct _GstAlphaColor
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* caps */
  gint in_width, in_height;
  gint out_width, out_height;
};

struct _GstAlphaColorClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_alpha_color_details =
GST_ELEMENT_DETAILS ("alpha color filter",
    "Filter/Effect/Video",
    "converts rgb to yuv with alpha",
    "Wim Taymans <wim@fluendo.com>");


/* AlphaColor signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static GstStaticPadTemplate gst_alpha_color_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA)
    );

static GstStaticPadTemplate gst_alpha_color_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV"))
    );


static void gst_alpha_color_base_init (gpointer g_class);
static void gst_alpha_color_class_init (GstAlphaColorClass * klass);
static void gst_alpha_color_init (GstAlphaColor * alpha);

static void gst_alpha_color_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_alpha_color_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstPadLinkReturn
gst_alpha_color_sink_link (GstPad * pad, const GstCaps * caps);
static void gst_alpha_color_chain (GstPad * pad, GstData * _data);

static GstElementStateReturn gst_alpha_color_change_state (GstElement *
    element);


static GstElementClass *parent_class = NULL;

/* static guint gst_alpha_color_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_alpha_color_get_type (void)
{
  static GType alpha_type = 0;

  if (!alpha_type) {
    static const GTypeInfo alpha_info = {
      sizeof (GstAlphaColorClass),
      gst_alpha_color_base_init,
      NULL,
      (GClassInitFunc) gst_alpha_color_class_init,
      NULL,
      NULL,
      sizeof (GstAlphaColor),
      0,
      (GInstanceInitFunc) gst_alpha_color_init,
    };

    alpha_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAlphaColor", &alpha_info,
        0);
  }
  return alpha_type;
}

static void
gst_alpha_color_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_alpha_color_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_color_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_color_src_template));
}
static void
gst_alpha_color_class_init (GstAlphaColorClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_alpha_color_set_property;
  gobject_class->get_property = gst_alpha_color_get_property;

  gstelement_class->change_state = gst_alpha_color_change_state;
}

static void
gst_alpha_color_init (GstAlphaColor * alpha)
{
  /* create the sink and src pads */
  alpha->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_alpha_color_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (alpha), alpha->sinkpad);
  gst_pad_set_chain_function (alpha->sinkpad, gst_alpha_color_chain);
  gst_pad_set_link_function (alpha->sinkpad, gst_alpha_color_sink_link);

  alpha->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_alpha_color_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (alpha), alpha->srcpad);

  GST_FLAG_SET (alpha, GST_ELEMENT_EVENT_AWARE);
}

/* do we need this function? */
static void
gst_alpha_color_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAlphaColor *alpha;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_ALPHA_COLOR (object));

  alpha = GST_ALPHA_COLOR (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
gst_alpha_color_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAlphaColor *alpha;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_ALPHA_COLOR (object));

  alpha = GST_ALPHA_COLOR (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPadLinkReturn
gst_alpha_color_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstAlphaColor *alpha;
  GstStructure *structure;
  gboolean ret;
  gdouble fps;

  alpha = GST_ALPHA_COLOR (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "width", &alpha->in_width);
  ret &= gst_structure_get_int (structure, "height", &alpha->in_height);
  ret &= gst_structure_get_double (structure, "framerate", &fps);

  if (!ret)
    return GST_PAD_LINK_REFUSED;

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("AYUV"),
      "framerate", G_TYPE_DOUBLE, fps,
      "width", G_TYPE_INT, alpha->in_width,
      "height", G_TYPE_INT, alpha->in_height, NULL);

  return gst_pad_try_set_caps (alpha->srcpad, caps);
}

static void
transform (guint8 * data, gint size)
{
  guint8 y, u, v;

  while (size > 0) {
    y = data[0] * 0.299 + data[1] * 0.587 + data[2] * 0.114 + 0;
    u = data[0] * -0.169 + data[1] * -0.332 + data[2] * 0.500 + 128;
    v = data[0] * 0.500 + data[1] * -0.419 + data[2] * -0.0813 + 128;

    data[0] = data[3];
    data[1] = y;
    data[2] = u;
    data[3] = v;

    data += 4;
    size -= 4;
  }
}

static void
gst_alpha_color_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buffer;
  GstAlphaColor *alpha;
  GstBuffer *outbuf;
  gint width, height;

  alpha = GST_ALPHA_COLOR (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (_data)) {
    GstEvent *event = GST_EVENT (_data);

    switch (GST_EVENT_TYPE (event)) {
      default:
        gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

  buffer = GST_BUFFER (_data);

  width = alpha->in_width;
  height = alpha->in_height;

  outbuf = gst_buffer_copy_on_write (buffer);

  transform (GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));

  gst_pad_push (alpha->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_alpha_color_change_state (GstElement * element)
{
  GstAlphaColor *alpha;

  alpha = GST_ALPHA_COLOR (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "alphacolor", GST_RANK_NONE,
      GST_TYPE_ALPHA_COLOR);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alphacolor",
    "colorspace conversion preserving the alpha channels",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
