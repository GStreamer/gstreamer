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

#include <string.h>
#include <gst/gst.h>

#define GST_TYPE_EFFECTV \
  (gst_effectv_get_type())
#define GST_EFFECTV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EFFECTV,GstEffecTV))
#define GST_EFFECTV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstEffecTV))
#define GST_IS_EFFECTV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EFFECTV))
#define GST_IS_EFFECTV_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EFFECTV))

typedef struct _GstEffecTV GstEffecTV;
typedef struct _GstEffecTVClass GstEffecTVClass;

struct _GstEffecTV
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gint width, height;
  gint map_width, map_height;
  guint32 *map;
  gint video_width_margin;
};

struct _GstEffecTVClass
{
  GstElementClass parent_class;
};

static GstElementDetails effectv_details = {
  "EffecTV",
  "Filter/Effect",
  "Aply edge detect on video",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2002",
};


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

GST_PAD_TEMPLATE_FACTORY (effectv_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "effectv_src",
    "video/raw",
      "format",         GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")),
      "bpp",            GST_PROPS_INT (32),
      "depth",          GST_PROPS_INT (32),
      "endianness",     GST_PROPS_INT (G_BYTE_ORDER),
      "red_mask",       GST_PROPS_INT (0xff0000),
      "green_mask",     GST_PROPS_INT (0xff00),
      "blue_mask",      GST_PROPS_INT (0xff),
      "width",          GST_PROPS_INT_RANGE (16, 4096),
      "height",         GST_PROPS_INT_RANGE (16, 4096)
  )
)

GST_PAD_TEMPLATE_FACTORY (effectv_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "effectv_src",
    "video/raw",
      "format",         GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")),
      "bpp",            GST_PROPS_INT (32),
      "depth",          GST_PROPS_INT (32),
      "endianness",     GST_PROPS_INT (G_BYTE_ORDER),
      "red_mask",       GST_PROPS_INT (0xff0000),
      "green_mask",     GST_PROPS_INT (0xff00),
      "blue_mask",      GST_PROPS_INT (0xff),
      "width",          GST_PROPS_INT_RANGE (16, 4096),
      "height",         GST_PROPS_INT_RANGE (16, 4096)
  )
)

static GType gst_effectv_get_type (void);

static void gst_effectv_class_init (GstEffecTVClass * klass);
static void gst_effectv_init (GstEffecTV * filter);

static void gst_effectv_set_property (GObject * object, guint prop_id,
					   const GValue * value, GParamSpec * pspec);
static void gst_effectv_get_property (GObject * object, guint prop_id,
					   GValue * value, GParamSpec * pspec);

static void gst_effectv_chain (GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

     static GType gst_effectv_get_type (void)
{
  static GType effectv_type = 0;

  if (!effectv_type) {
    static const GTypeInfo effectv_info = {
      sizeof (GstEffecTVClass), NULL,
      NULL,
      (GClassInitFunc) gst_effectv_class_init,
      NULL,
      NULL,
      sizeof (GstEffecTV),
      0,
      (GInstanceInitFunc) gst_effectv_init,
    };

    effectv_type = g_type_register_static (GST_TYPE_ELEMENT, "GstEffecTV", &effectv_info, 0);
  }
  return effectv_type;
}

static void
gst_effectv_class_init (GstEffecTVClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_effectv_set_property;
  gobject_class->get_property = gst_effectv_get_property;
}

static GstPadConnectReturn
gst_effectv_sinkconnect (GstPad * pad, GstCaps * caps)
{
  GstEffecTV *filter;

  filter = GST_EFFECTV (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "width", &filter->width);
  gst_caps_get_int (caps, "height", &filter->height);

  filter->map_width = filter->width / 4;
  filter->map_height = filter->height / 4;
  filter->video_width_margin = filter->width - filter->map_width * 4;

  g_free (filter->map);
  filter->map = (guint32 *)g_malloc (filter->map_width * filter->map_height * sizeof(guint32) * 2);
  bzero(filter->map, filter->map_width * filter->map_height * sizeof(guint32) * 2);

  if (gst_pad_try_set_caps (filter->srcpad, caps)) {
    return GST_PAD_CONNECT_OK;
  }

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_effectv_init (GstEffecTV * filter)
{
  filter->sinkpad = gst_pad_new_from_template (effectv_sink_factory (), "sink");
  gst_pad_set_chain_function (filter->sinkpad, gst_effectv_chain);
  gst_pad_set_connect_function (filter->sinkpad, gst_effectv_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template (effectv_src_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->map = NULL;
}

static void
gst_effectv_chain (GstPad * pad, GstBuffer * buf)
{
  GstEffecTV *filter;
  int x, y;
  int r, g, b;
  guint32 *src, *dest;
  guint32 p, q;
  guint32 v0, v1, v2, v3;
  GstBuffer *outbuf;

  filter = GST_EFFECTV (gst_pad_get_parent (pad));

  src = (guint32 *) GST_BUFFER_DATA (buf);

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = (filter->width * filter->height * 4);
  dest = (guint32 *) GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  
  src += filter->width * 4 + 4;
  dest += filter->width * 4 + 4;
  
  for (y = 1; y < filter->map_height - 1; y++) {
    for (x = 1; x < filter->map_width - 1; x++) {

      p = *src;
      q = *(src - 4);

/* difference between the current pixel and right neighbor. */
      r = ((p & 0xff0000) - (q & 0xff0000)) >> 16;
      g = ((p & 0xff00) - (q & 0xff00)) >> 8;
      b = (p & 0xff) - (q & 0xff);
      r *= r;
      g *= g;
      b *= b;
      r = r >> 5;		/* To lack the lower bit for saturated addition,  */
      g = g >> 5;		/* devide the value with 32, instead of 16. It is */
      b = b >> 4;		/* same as `v2 &= 0xfefeff' */
      if (r > 127)
	r = 127;
      if (g > 127)
	g = 127;
      if (b > 255)
	b = 255;
      v2 = (r << 17) | (g << 9) | b;

/* difference between the current pixel and upper neighbor. */
      q = *(src - filter->width * 4);
      r = ((p & 0xff0000) - (q & 0xff0000)) >> 16;
      g = ((p & 0xff00) - (q & 0xff00)) >> 8;
      b = (p & 0xff) - (q & 0xff);
      r *= r;
      g *= g;
      b *= b;
      r = r >> 5;
      g = g >> 5;
      b = b >> 4;
      if (r > 127)
	r = 127;
      if (g > 127)
	g = 127;
      if (b > 255)
	b = 255;
      v3 = (r << 17) | (g << 9) | b;

      v0 = filter->map[(y - 1) * filter->map_width * 2 + x * 2];
      v1 = filter->map[y * filter->map_width * 2 + (x - 1) * 2 + 1];
      filter->map[y * filter->map_width * 2 + x * 2] = v2;
      filter->map[y * filter->map_width * 2 + x * 2 + 1] = v3;
      r = v0 + v1;
      g = r & 0x01010100;
      dest[0] = r | (g - (g >> 8));
      r = v0 + v3;
      g = r & 0x01010100;
      dest[1] = r | (g - (g >> 8));
      dest[2] = v3;
      dest[3] = v3;
      r = v2 + v1;
      g = r & 0x01010100;
      dest[filter->width] = r | (g - (g >> 8));
      r = v2 + v3;
      g = r & 0x01010100;
      dest[filter->width + 1] = r | (g - (g >> 8));
      dest[filter->width + 2] = v3;
      dest[filter->width + 3] = v3;
      dest[filter->width * 2] = v2;
      dest[filter->width * 2 + 1] = v2;
      dest[filter->width * 3] = v2;
      dest[filter->width * 3 + 1] = v2;

      src += 4;
      dest += 4;
    }
    src += filter->width * 3 + 8 + filter->video_width_margin;
    dest += filter->width * 3 + 8 + filter->video_width_margin;
  }
  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, outbuf);
}

static void
gst_effectv_set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstEffecTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_EFFECTV (object));

  filter = GST_EFFECTV (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_effectv_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstEffecTV *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_EFFECTV (object));

  filter = GST_EFFECTV (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new ("edgeTV", GST_TYPE_EFFECTV, &effectv_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (effectv_src_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (effectv_sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "effectv",
  plugin_init
};
