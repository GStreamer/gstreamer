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

#define GST_TYPE_ALPHA \
  (gst_alpha_get_type())
#define GST_ALPHA(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_ALPHA,GstAlpha))
#define GST_ALPHA_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ALPHA,GstAlphaClass))
#define GST_IS_ALPHA(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_ALPHA))
#define GST_IS_ALPHA_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_ALPHA))

typedef struct _GstAlpha GstAlpha;
typedef struct _GstAlphaClass GstAlphaClass;

typedef enum
{
  ALPHA_METHOD_ADD,
  ALPHA_METHOD_GREEN,
  ALPHA_METHOD_BLUE,
}
GstAlphaMethod;

#define DEFAULT_METHOD ALPHA_METHOD_ADD
#define DEFAULT_ALPHA 1.0
#define DEFAULT_TARGET_CR 116
#define DEFAULT_TARGET_CB 116

struct _GstAlpha
{
  GstElement element;

  /* pads */
  GstPad *sinkpad;
  GstPad *srcpad;

  /* caps */
  gint in_width, in_height;
  gint out_width, out_height;

  gdouble alpha;

  guint target_cr, target_cb;

  GstAlphaMethod method;
};

struct _GstAlphaClass
{
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails gst_alpha_details =
GST_ELEMENT_DETAILS ("alpha filter",
    "Filter/Effect/Video",
    "Adds an alpha channel to video",
    "Wim Taymans <wim@fluendo.com>");


/* Alpha signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_METHOD,
  ARG_ALPHA,
  ARG_TARGET_CR,
  ARG_TARGET_CB,
  /* FILL ME */
};

static GstStaticPadTemplate gst_alpha_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV"))
    );

static GstStaticPadTemplate gst_alpha_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );


static void gst_alpha_base_init (gpointer g_class);
static void gst_alpha_class_init (GstAlphaClass * klass);
static void gst_alpha_init (GstAlpha * alpha);

static void gst_alpha_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_alpha_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstPadLinkReturn
gst_alpha_sink_link (GstPad * pad, const GstCaps * caps);
static void gst_alpha_chain (GstPad * pad, GstData * _data);

static GstElementStateReturn gst_alpha_change_state (GstElement * element);


static GstElementClass *parent_class = NULL;

#define GST_TYPE_ALPHA_METHOD (gst_alpha_method_get_type())
static GType
gst_alpha_method_get_type (void)
{
  static GType alpha_method_type = 0;
  static GEnumValue alpha_method[] = {
    {ALPHA_METHOD_ADD, "0", "Add alpha channel"},
    {ALPHA_METHOD_GREEN, "1", "Chroma Key green"},
    {ALPHA_METHOD_BLUE, "2", "Chroma Key blue"},
    {0, NULL, NULL},
  };

  if (!alpha_method_type) {
    alpha_method_type = g_enum_register_static ("GstAlphaMethod", alpha_method);
  }
  return alpha_method_type;
}

/* static guint gst_alpha_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_alpha_get_type (void)
{
  static GType alpha_type = 0;

  if (!alpha_type) {
    static const GTypeInfo alpha_info = {
      sizeof (GstAlphaClass),
      gst_alpha_base_init,
      NULL,
      (GClassInitFunc) gst_alpha_class_init,
      NULL,
      NULL,
      sizeof (GstAlpha),
      0,
      (GInstanceInitFunc) gst_alpha_init,
    };

    alpha_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAlpha", &alpha_info, 0);
  }
  return alpha_type;
}

static void
gst_alpha_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_alpha_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_alpha_src_template));
}
static void
gst_alpha_class_init (GstAlphaClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_METHOD,
      g_param_spec_enum ("method", "Method",
          "How the alpha channels should be created", GST_TYPE_ALPHA_METHOD,
          DEFAULT_METHOD, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "The value for the alpha channel",
          0.0, 1.0, DEFAULT_ALPHA, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TARGET_CR,
      g_param_spec_uint ("target_cr", "Target Red", "The Red Chroma target", 0,
          255, 116, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TARGET_CB,
      g_param_spec_uint ("target_cb", "Target Blue", "The Blue Chroma target",
          0, 255, 116, (GParamFlags) G_PARAM_READWRITE));

  gobject_class->set_property = gst_alpha_set_property;
  gobject_class->get_property = gst_alpha_get_property;

  gstelement_class->change_state = gst_alpha_change_state;
}

static void
gst_alpha_init (GstAlpha * alpha)
{
  /* create the sink and src pads */
  alpha->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_alpha_sink_template), "sink");
  gst_element_add_pad (GST_ELEMENT (alpha), alpha->sinkpad);
  gst_pad_set_chain_function (alpha->sinkpad, gst_alpha_chain);
  gst_pad_set_link_function (alpha->sinkpad, gst_alpha_sink_link);

  alpha->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_alpha_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (alpha), alpha->srcpad);

  alpha->alpha = DEFAULT_ALPHA;
  alpha->method = DEFAULT_METHOD;
  alpha->target_cr = DEFAULT_TARGET_CR;
  alpha->target_cb = DEFAULT_TARGET_CB;

  GST_FLAG_SET (alpha, GST_ELEMENT_EVENT_AWARE);
}

/* do we need this function? */
static void
gst_alpha_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAlpha *alpha;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_ALPHA (object));

  alpha = GST_ALPHA (object);

  switch (prop_id) {
    case ARG_METHOD:
      alpha->method = g_value_get_enum (value);
      break;
    case ARG_ALPHA:
      alpha->alpha = g_value_get_double (value);
      break;
    case ARG_TARGET_CB:
      alpha->target_cb = g_value_get_uint (value);
      break;
    case ARG_TARGET_CR:
      alpha->target_cr = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
static void
gst_alpha_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAlpha *alpha;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_ALPHA (object));

  alpha = GST_ALPHA (object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, alpha->method);
      break;
    case ARG_ALPHA:
      g_value_set_double (value, alpha->alpha);
      break;
    case ARG_TARGET_CR:
      g_value_set_uint (value, alpha->target_cr);
      break;
    case ARG_TARGET_CB:
      g_value_set_uint (value, alpha->target_cb);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstPadLinkReturn
gst_alpha_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstAlpha *alpha;
  GstStructure *structure;
  gboolean ret;

  alpha = GST_ALPHA (gst_pad_get_parent (pad));
  structure = gst_caps_get_structure (caps, 0);

  ret = gst_structure_get_int (structure, "width", &alpha->in_width);
  ret &= gst_structure_get_int (structure, "height", &alpha->in_height);

  return GST_PAD_LINK_OK;
}

/*
static int yuv_colors_Y[] = {  16, 150,  29 };
static int yuv_colors_U[] = { 128,  46, 255 };
static int yuv_colors_V[] = { 128,  21, 107 };
*/

static void
gst_alpha_add (guint8 * src, guint8 * dest, gint width, gint height,
    gdouble alpha)
{
  gint b_alpha = (gint) (alpha * 255);
  guint8 *srcY;
  guint8 *srcU;
  guint8 *srcV;
  gint size;
  gint half_width = width / 2;
  gint i, j;

  size = width * height;

  srcY = src;
  srcU = srcY + size;
  srcV = srcU + size / 4;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width / 2; j++) {
      *dest++ = b_alpha;
      *dest++ = *srcY++;
      *dest++ = *srcU;
      *dest++ = *srcV;
      *dest++ = b_alpha;
      *dest++ = *srcY++;
      *dest++ = *srcU++;
      *dest++ = *srcV++;
    }
    if (i % 2 == 0) {
      srcU -= half_width;
      srcV -= half_width;
    }
  }
}

static void
gst_alpha_chroma_key (gchar * src, gchar * dest, gint width, gint height,
    gboolean soft, gint target_u, gint target_v, gfloat edge_factor,
    gdouble alpha)
{
  gint b_alpha;
  gint f_alpha = (gint) (alpha * 255);
  guint8 *srcY1, *srcY2, *srcU, *srcV;
  guint8 *dest1, *dest2;
  gint i, j;
  gint x, z, u, v;
  gint size;

  size = width * height;

  srcY1 = src;
  srcY2 = src + width;
  srcU = srcY1 + size;
  srcV = srcU + size / 4;

  dest1 = dest;
  dest2 = dest + width * 4;

  for (i = 0; i < height / 2; i++) {
    for (j = 0; j < width / 2; j++) {
      u = *srcU++;
      v = *srcV++;

      x = target_u - u;
      z = target_v - v;

      // only filter if in top left square
      if ((x > 0) && (z > 0)) {
        // only calculate lot of stuff if we'll use soft edges
        if (soft) {
          gint ds = (x > z) ? z : x;

          gfloat df = (gfloat) (ds) / edge_factor;

          if (df > 1.0)
            df = 1.0;

          // suppress foreground
          if (x > z) {
            u += z;
            v += z;
          } else {
            u += x;
            v += x;
          }
          b_alpha = (int) (f_alpha * (1.0 - df));
        } else {
          // kill color and alpha
          b_alpha = 0;
        }
      } else {
        // do nothing;
        b_alpha = f_alpha;
      }

      *dest1++ = b_alpha;
      *dest1++ = *srcY1++;
      *dest1++ = u;
      *dest1++ = v;
      *dest1++ = b_alpha;
      *dest1++ = *srcY1++;
      *dest1++ = u;
      *dest1++ = v;
      *dest2++ = b_alpha;
      *dest2++ = *srcY2++;
      *dest2++ = u;
      *dest2++ = v;
      *dest2++ = b_alpha;
      *dest2++ = *srcY2++;
      *dest2++ = u;
      *dest2++ = v;
    }
    dest1 += width * 4;
    dest2 += width * 4;
    srcY1 += width;
    srcY2 += width;
  }
}

static void
gst_alpha_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buffer;
  GstAlpha *alpha;
  GstBuffer *outbuf;
  gint new_width, new_height;

  alpha = GST_ALPHA (gst_pad_get_parent (pad));

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

  new_width = alpha->in_width;
  new_height = alpha->in_height;

  if (new_width != alpha->out_width ||
      new_height != alpha->out_height || !GST_PAD_CAPS (alpha->srcpad)) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy (gst_pad_get_negotiated_caps (alpha->sinkpad));
    gst_caps_set_simple (newcaps,
        "format", GST_TYPE_FOURCC, GST_STR_FOURCC ("AYUV"),
        "width", G_TYPE_INT, new_width, "height", G_TYPE_INT, new_height, NULL);

    if (!gst_pad_try_set_caps (alpha->srcpad, newcaps)) {
      GST_ELEMENT_ERROR (alpha, CORE, NEGOTIATION, (NULL), (NULL));
      return;
    }

    alpha->out_width = new_width;
    alpha->out_height = new_height;
  }

  outbuf = gst_buffer_new_and_alloc (new_width * new_height * 4);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buffer);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buffer);

  switch (alpha->method) {
    case ALPHA_METHOD_ADD:
      gst_alpha_add (GST_BUFFER_DATA (buffer),
          GST_BUFFER_DATA (outbuf), new_width, new_height, alpha->alpha);
      break;
    case ALPHA_METHOD_GREEN:
      gst_alpha_chroma_key (GST_BUFFER_DATA (buffer),
          GST_BUFFER_DATA (outbuf),
          new_width, new_height,
          TRUE, alpha->target_cr, alpha->target_cb, 1.0, alpha->alpha);
      break;
    case ALPHA_METHOD_BLUE:
      gst_alpha_chroma_key (GST_BUFFER_DATA (buffer),
          GST_BUFFER_DATA (outbuf),
          new_width, new_height, TRUE, 100, 100, 1.0, alpha->alpha);
      break;
  }

  gst_buffer_unref (buffer);

  gst_pad_push (alpha->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_alpha_change_state (GstElement * element)
{
  GstAlpha *alpha;

  alpha = GST_ALPHA (element);

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
  return gst_element_register (plugin, "alpha", GST_RANK_NONE, GST_TYPE_ALPHA);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alpha",
    "resizes a video by adding borders or cropping",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
