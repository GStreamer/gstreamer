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


/*#define DEBUG_ENABLED */
#include <gstvideoflip.h>


/* elementfactory information */
static GstElementDetails videoflip_details = {
  "Video Filter Template",
  "Filter/Video",
  "LGPL",
  "Template for a video filter",
  VERSION,
  "David Schleef <ds@schleef.org>",
  "(C) 2003",
};

/* GstVideoflip signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void	gst_videoflip_class_init	(GstVideoflipClass *klass);
static void	gst_videoflip_init		(GstVideoflip *videoflip);

static void	gst_videoflip_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_videoflip_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_videoflip_planar411(GstVideofilter *videofilter, void *dest, void *src);
static void gst_videoflip_setup(GstVideofilter *videofilter);

static GstVideoflipClass *this_class = NULL;
static GstVideofilterClass *parent_class = NULL;
static GstElementClass *element_class = NULL;

GType
gst_videoflip_get_type (void)
{
  static GType videoflip_type = 0;

  if (!videoflip_type) {
    static const GTypeInfo videoflip_info = {
      sizeof(GstVideoflipClass),      NULL,
      NULL,
      (GClassInitFunc)gst_videoflip_class_init,
      NULL,
      NULL,
      sizeof(GstVideoflip),
      0,
      (GInstanceInitFunc)gst_videoflip_init,
    };
    videoflip_type = g_type_register_static(GST_TYPE_VIDEOFILTER, "GstVideoflip", &videoflip_info, 0);
  }
  return videoflip_type;
}

static GstVideofilterFormat gst_videoflip_formats[] = {
  { "I420", 12, gst_videoflip_planar411, },
};

static void
gst_videoflip_class_init (GstVideoflipClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstVideofilterClass *gstvideofilter_class;
  int i;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;
  gstvideofilter_class = (GstVideofilterClass *)klass;

#if 0
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_METHOD,
      g_param_spec_enum("method","method","method",
      GST_TYPE_VIDEOFLIP_METHOD, GST_VIDEOFLIP_METHOD_90R,
      G_PARAM_READWRITE));
#endif

  this_class = klass;
  parent_class = g_type_class_ref(GST_TYPE_VIDEOFILTER);
  element_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_videoflip_set_property;
  gobject_class->get_property = gst_videoflip_get_property;

  gstvideofilter_class->setup = gst_videoflip_setup;

  for(i=0;i<G_N_ELEMENTS(gst_videoflip_formats);i++){
    gst_videofilter_class_add_format(parent_class, gst_videoflip_formats + i);
  }
}

static GstCaps *gst_videoflip_get_capslist(void)
{
  GstVideofilterClass *klass;

  klass = g_type_class_ref(GST_TYPE_VIDEOFILTER);

  return gst_videofilter_class_get_capslist(klass);
}

static GstPadTemplate *
gst_videoflip_src_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps = GST_CAPS_NEW("src","video/raw",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT));

    caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static GstPadTemplate *
gst_videoflip_sink_template_factory(void)
{
  static GstPadTemplate *templ = NULL;

  if(!templ){
    GstCaps *caps = GST_CAPS_NEW("sink","video/raw",
		"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
		"height", GST_PROPS_INT_RANGE (0, G_MAXINT));

    caps = gst_caps_intersect(caps, gst_videoflip_get_capslist ());

    templ = GST_PAD_TEMPLATE_NEW("src", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  }
  return templ;
}

static void
gst_videoflip_init (GstVideoflip *videoflip)
{
  GstVideofilter *videofilter;

  GST_DEBUG("gst_videoflip_init");

  videofilter = GST_VIDEOFILTER(videoflip);

  videofilter->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoflip_sink_template_factory),
		  "sink");

  videofilter->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (gst_videoflip_src_template_factory),
		  "src");

  gst_videofilter_postinit(GST_VIDEOFILTER(videoflip));
}

static void
gst_videoflip_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstVideoflip *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  src = GST_VIDEOFLIP(object);

  GST_DEBUG("gst_videoflip_set_property");
  switch (prop_id) {
#if 0
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      break;
#endif
    default:
      break;
  }
}

static void
gst_videoflip_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstVideoflip *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_VIDEOFLIP(object));
  src = GST_VIDEOFLIP(object);

  switch (prop_id) {
#if 0
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  if(!gst_library_load("gstvideofilter"))
    return FALSE;

  /* create an elementfactory for the videoflip element */
  factory = gst_element_factory_new("videoflip",GST_TYPE_VIDEOFLIP,
                                   &videoflip_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoflip_sink_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_videoflip_src_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "videoflip",
  plugin_init
};

static void gst_videoflip_setup(GstVideofilter *videofilter)
{
  GstVideoflip *videoflip;

  g_return_if_fail(GST_IS_VIDEOFLIP(videofilter));
  videoflip = GST_VIDEOFLIP(videofilter);

  /* if any setup needs to be done, do it here */

}

static void gst_videoflip_planar411(GstVideofilter *videofilter,
    void *dest, void *src)
{
  GstVideoflip *videoflip;

  g_return_if_fail(GST_IS_VIDEOFLIP(videofilter));
  videoflip = GST_VIDEOFLIP(videofilter);

  /* do something interesting here */
}

