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


/*#define DEBUG_ENABLED */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gstvideoscale.h>
#include <videoscale.h>



/* elementfactory information */
static GstElementDetails videoscale_details = GST_ELEMENT_DETAILS (
  "Video scaler",
  "Filter/Effect/Video",
  "Resizes video",
  "Wim Taymans <wim.taymans@chello.be>"
);

/* GstVideoscale signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METHOD,
  /* FILL ME */
};

#define GST_TYPE_VIDEOSCALE_METHOD (gst_videoscale_method_get_type())
static GType
gst_videoscale_method_get_type (void)
{
  static GType videoscale_method_type = 0;
  static GEnumValue videoscale_methods[] = {
    { GST_VIDEOSCALE_POINT_SAMPLE, "0", "Point Sample" },
    { GST_VIDEOSCALE_NEAREST,      "1", "Nearest" },
    { GST_VIDEOSCALE_BILINEAR,     "2", "Bilinear" },
    { GST_VIDEOSCALE_BICUBIC,      "3", "Bicubic" },
    { 0, NULL, NULL },
  };
  if (!videoscale_method_type) {
    videoscale_method_type = g_enum_register_static ("GstVideoscaleMethod", videoscale_methods);
  }
  return videoscale_method_type;
}

static GstCaps *
gst_videoscale_get_capslist(void)
{
  GstCaps *caps;
  int i;

  caps = gst_caps_new_empty();
  for(i=0;i<videoscale_n_formats;i++){
    gst_caps_append_structure (caps,
	videoscale_get_structure (videoscale_formats + i));
  }

  return caps;
}

static GstPadTemplate *
gst_videoscale_src_template_factory(void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_videoscale_get_capslist());
}

static GstPadTemplate *
gst_videoscale_sink_template_factory(void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_videoscale_get_capslist());
}

static void	gst_videoscale_base_init	(gpointer g_class);
static void	gst_videoscale_class_init	(GstVideoscaleClass *klass);
static void	gst_videoscale_init		(GstVideoscale *videoscale);
static gboolean gst_videoscale_handle_src_event (GstPad *pad, GstEvent *event);

static void	gst_videoscale_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videoscale_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_videoscale_chain		(GstPad *pad, GstData *_data);
static GstCaps * gst_videoscale_get_capslist(void);

static GstElementClass *parent_class = NULL;
/*static guint gst_videoscale_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_videoscale_get_type (void)
{
  static GType videoscale_type = 0;

  if (!videoscale_type) {
    static const GTypeInfo videoscale_info = {
      sizeof(GstVideoscaleClass),
      gst_videoscale_base_init,
      NULL,
      (GClassInitFunc)gst_videoscale_class_init,
      NULL,
      NULL,
      sizeof(GstVideoscale),
      0,
      (GInstanceInitFunc)gst_videoscale_init,
    };
    videoscale_type = g_type_register_static(GST_TYPE_ELEMENT, "GstVideoscale", &videoscale_info, 0);
  }
  return videoscale_type;
}

static void
gst_videoscale_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videoscale_details);

  gst_element_class_add_pad_template (element_class, gst_videoscale_sink_template_factory());
  gst_element_class_add_pad_template (element_class, gst_videoscale_src_template_factory());
}
static void
gst_videoscale_class_init (GstVideoscaleClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_METHOD,
    g_param_spec_enum("method","method","method",
                      GST_TYPE_VIDEOSCALE_METHOD,0,G_PARAM_READWRITE)); /* CHECKME! */

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videoscale_set_property;
  gobject_class->get_property = gst_videoscale_get_property;

}

static GstCaps *
gst_videoscale_getcaps (GstPad *pad)
{
  GstVideoscale *videoscale;
  GstCaps *othercaps;
  GstCaps *caps;
  GstPad *otherpad;
  int i;

  GST_DEBUG ("gst_videoscale_getcaps");
  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));
  
  otherpad = (pad == videoscale->srcpad) ? videoscale->sinkpad :
    videoscale->srcpad;
  othercaps = gst_pad_get_allowed_caps (otherpad);

  GST_DEBUG_CAPS("othercaps are", othercaps);

  caps = gst_caps_copy (othercaps);
  for(i=0;i<gst_caps_get_size(caps);i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    gst_structure_set (structure,
	"width", GST_TYPE_INT_RANGE, 16, G_MAXINT,
	"height", GST_TYPE_INT_RANGE, 16, G_MAXINT,
	NULL);
  }

  GST_DEBUG_CAPS ("returning caps", caps);

  return caps;
}


static GstPadLinkReturn
gst_videoscale_link (GstPad *pad, const GstCaps *caps)
{
  GstVideoscale *videoscale;
  GstPadLinkReturn ret;
  GstPad *otherpad;
  GstStructure *structure;
  struct videoscale_format_struct *format;
  int height, width;

  GST_DEBUG ("gst_videoscale_link %s\n", gst_caps_to_string (caps));
  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  otherpad = (pad == videoscale->srcpad) ? videoscale->sinkpad :
    videoscale->srcpad;

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width", &width);
  ret &= gst_structure_get_int (structure, "height", &height);

  format = videoscale_find_by_structure (structure);

  if(!ret || format==NULL) return GST_PAD_LINK_REFUSED;

  ret = gst_pad_try_set_caps (otherpad, caps);
  if (ret == GST_PAD_LINK_OK) {
    /* cool, we can use passthru */

    videoscale->format = format;
    videoscale->to_width = width;
    videoscale->to_height = height;
    videoscale->from_width = width;
    videoscale->from_height = height;

    gst_videoscale_setup (videoscale);

    return GST_PAD_LINK_OK;
  }

  if (gst_pad_is_negotiated (otherpad)) {
    GstCaps *newcaps = gst_caps_copy (caps);

    if (pad == videoscale->srcpad) {
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, videoscale->from_width,
          "height", G_TYPE_INT, videoscale->from_height,
          NULL);
    } else {
      gst_caps_set_simple (newcaps,
          "width", G_TYPE_INT, videoscale->to_width,
          "height", G_TYPE_INT, videoscale->to_height,
          NULL);
    }
    ret = gst_pad_try_set_caps (otherpad, newcaps);
    if (GST_PAD_LINK_FAILED (ret)) {
      return GST_PAD_LINK_REFUSED;
    }
  }

  videoscale->passthru = FALSE;

  if (pad == videoscale->srcpad) {
    videoscale->to_width = width;
    videoscale->to_height = height;
  } else {
    videoscale->from_width = width;
    videoscale->from_height = height;
  }
  videoscale->format = format;

  if (gst_pad_is_negotiated (otherpad)) {
    gst_videoscale_setup (videoscale);
  }

  return GST_PAD_LINK_OK;
}

static void
gst_videoscale_init (GstVideoscale *videoscale)
{
  GST_DEBUG ("gst_videoscale_init");
  videoscale->sinkpad = gst_pad_new_from_template (
		  gst_videoscale_sink_template_factory(),
		  "sink");
  gst_element_add_pad(GST_ELEMENT(videoscale),videoscale->sinkpad);
  gst_pad_set_chain_function(videoscale->sinkpad,gst_videoscale_chain);
  gst_pad_set_link_function(videoscale->sinkpad,gst_videoscale_link);
  gst_pad_set_getcaps_function(videoscale->sinkpad,gst_videoscale_getcaps);

  videoscale->srcpad = gst_pad_new_from_template (
		  gst_videoscale_src_template_factory(),
		  "src");
  gst_element_add_pad(GST_ELEMENT(videoscale),videoscale->srcpad);
  gst_pad_set_event_function (videoscale->srcpad, gst_videoscale_handle_src_event);
  gst_pad_set_link_function(videoscale->srcpad,gst_videoscale_link);
  gst_pad_set_getcaps_function(videoscale->srcpad,gst_videoscale_getcaps);

  videoscale->inited = FALSE;

  videoscale->method = GST_VIDEOSCALE_NEAREST;
  /*videoscale->method = GST_VIDEOSCALE_BILINEAR; */
  /*videoscale->method = GST_VIDEOSCALE_POINT_SAMPLE; */
}

static gboolean
gst_videoscale_handle_src_event (GstPad *pad, GstEvent *event)
{
  GstVideoscale *videoscale;
  double a;
  GstStructure *structure;
  GstEvent *new_event;

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      structure = gst_structure_copy (event->event_data.structure.structure);
      if (gst_structure_get_double (event->event_data.structure.structure,
          "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a*videoscale->from_width/videoscale->to_width, NULL);
      }
      if (gst_structure_get_double (event->event_data.structure.structure,
          "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a*videoscale->from_height/videoscale->to_height, NULL);
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
gst_videoscale_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstVideoscale *videoscale;
  guchar *data;
  gulong size;
  GstBuffer *outbuf;

  GST_DEBUG ("gst_videoscale_chain");

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  videoscale = GST_VIDEOSCALE (gst_pad_get_parent (pad));
  g_return_if_fail (videoscale->inited);

  data = GST_BUFFER_DATA(buf);
  size = GST_BUFFER_SIZE(buf);

  if(videoscale->passthru){
    gst_pad_push(videoscale->srcpad, GST_DATA (buf));
    return;
  }

  GST_DEBUG ("gst_videoscale_chain: got buffer of %ld bytes in '%s'",size,
		              GST_OBJECT_NAME (videoscale));
 
  GST_DEBUG ("size=%ld from=%dx%d to=%dx%d fromsize=%ld (should be %d) tosize=%d",
	size,
	videoscale->from_width, videoscale->from_height,
	videoscale->to_width, videoscale->to_height,
  	size, videoscale->from_buf_size,
  	videoscale->to_buf_size);

  g_return_if_fail (size == videoscale->from_buf_size);

  outbuf = gst_pad_alloc_buffer (videoscale->srcpad,
                                 GST_BUFFER_OFFSET_NONE, videoscale->to_buf_size);
  
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  g_return_if_fail(videoscale->format);
  GST_DEBUG ("format " GST_FOURCC_FORMAT,GST_FOURCC_ARGS(videoscale->format->fourcc));
  g_return_if_fail(videoscale->format->scale);

  videoscale->format->scale(videoscale, GST_BUFFER_DATA(outbuf), data);

  GST_DEBUG ("gst_videoscale_chain: pushing buffer of %d bytes in '%s'",GST_BUFFER_SIZE(outbuf),
	              GST_OBJECT_NAME (videoscale));

  gst_pad_push(videoscale->srcpad, GST_DATA (outbuf));

  gst_buffer_unref(buf);
}

static void
gst_videoscale_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideoscale *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOSCALE(object));
  src = GST_VIDEOSCALE(object);

  GST_DEBUG ("gst_videoscale_set_property");
  switch (prop_id) {
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
    default:
      break;
  }
}

static void
gst_videoscale_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideoscale *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOSCALE(object));
  src = GST_VIDEOSCALE(object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "videoscale", GST_RANK_NONE, GST_TYPE_VIDEOSCALE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videoscale",
  "Resizes video",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN
)
