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

#include <gst/gst.h>
#include "gstlevel.h"
#include "math.h"


static GstElementDetails level_details = {
  "Level",
  "Filter/Effect",
  "RMS Level indicator for audio/raw",
  VERSION,
  "Thomas <thomas@apestaart.org>",
  "(C) 2001",
};


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

static GstPadTemplate*
level_src_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_src",
        "audio/raw",
	gst_props_new (
          "channels", GST_PROPS_INT_RANGE (1, 2),
	  NULL)),
      NULL);
  }
  return template;
}

static GstPadTemplate*
level_sink_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "test_src",
        "audio/raw",
	gst_props_new (
          "channels", GST_PROPS_INT_RANGE (1, 2),
	  NULL)),
      NULL);
  }
  return template;
}

static void		gst_level_class_init		(GstLevelClass *klass);
static void		gst_level_init			(GstLevel *filter);

static void		gst_level_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_level_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_level_chain			(GstPad *pad, GstBuffer *buf);
static void inline 	gst_level_fast_16bit_chain 	(gint16* data, gint16** out_data, 
			         				 guint numsamples);
static void inline 	gst_level_fast_8bit_chain		(gint8* data, gint8** out_data,
                                				 guint numsamples);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_level_get_type(void) {
  static GType level_type = 0;

  if (!level_type) {
    static const GTypeInfo level_info = {
      sizeof(GstLevelClass),      NULL,
      NULL,
      (GClassInitFunc)gst_level_class_init,
      NULL,
      NULL,
      sizeof(GstLevel),
      0,
      (GInstanceInitFunc)gst_level_init,
    };
    level_type = g_type_register_static(GST_TYPE_ELEMENT, "GstLevel", &level_info, 0);
  }
  return level_type;
}

static void
gst_level_class_init (GstLevelClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_level_set_property;
  gobject_class->get_property = gst_level_get_property;
}

static void
gst_level_init (GstLevel *filter)
{
  filter->sinkpad = gst_pad_new_from_template(level_sink_factory (),"sink");
  filter->srcpad = gst_pad_new_from_template(level_src_factory (),"src");

  gst_element_add_pad(GST_ELEMENT(filter),filter->sinkpad);
  gst_pad_set_chain_function(filter->sinkpad,gst_level_chain);
  filter->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(filter),filter->srcpad);
}

static void
gst_level_chain (GstPad *pad,GstBuffer *buf)
{
  GstLevel *filter;
  gint16 *in_data;
  gint16 *out_data;
  GstBuffer* outbuf;
  gint width;

  GstCaps *caps;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  filter = GST_LEVEL(GST_OBJECT_PARENT (pad));
  g_return_if_fail(filter != NULL);
  g_return_if_fail(GST_IS_LEVEL(filter));

  caps = NULL;
  caps = GST_PAD_CAPS(pad);
  if (caps == NULL)
  {
    /* FIXME : Please change this to a better warning method ! */
    printf ("WARNING : chain : Could not get caps of pad !\n");
  }

  gst_caps_get_int(caps, "width", &width);

  in_data = (gint16 *)GST_BUFFER_DATA(buf);
  outbuf=gst_buffer_new();
  GST_BUFFER_DATA(outbuf) = (gchar*)g_new(gint16,GST_BUFFER_SIZE(buf)/2);
  GST_BUFFER_SIZE(outbuf) = GST_BUFFER_SIZE(buf);

  out_data = (gint16*)GST_BUFFER_DATA(outbuf);
  
  switch (width) {
    case 16:
	gst_level_fast_16bit_chain(in_data,&out_data,GST_BUFFER_SIZE(buf)/2);
	break;
    case 8:
	gst_level_fast_8bit_chain((gint8*)in_data,(gint8**)&out_data,GST_BUFFER_SIZE(buf));
	break;
  }
  gst_buffer_unref(buf);
  gst_pad_push(filter->srcpad,outbuf);
}

static void inline
gst_level_fast_16bit_chain(gint16* in_data, gint16** out_data, 
			         guint num_samples)
#include "filter.func"

static void inline
gst_level_fast_8bit_chain(gint8* in_data, gint8** out_data,
                                guint num_samples)
#include "filter.func"

static void
gst_level_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstLevel *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_LEVEL(object));
  filter = GST_LEVEL(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_level_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstLevel *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_LEVEL(object));
  filter = GST_LEVEL(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("level",GST_TYPE_LEVEL,
                                   &level_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_element_factory_add_pad_template (factory, level_src_factory ());
  gst_element_factory_add_pad_template (factory, level_sink_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "level",
  plugin_init
};
