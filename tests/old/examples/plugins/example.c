/* Gnome-Streamer
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

#include "example.h"

/* elementfactory information */
static GstElementDetails example_details = {
  "An example plugin",
  "Example",
  "Shows the basic structure of a plugin",
  VERSION,
  "your name <your.name@your.isp>",
  "(C) 2000",
};

/* Example signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_ACTIVE
};

static GstPadFactory sink_factory = {
  "sink",					/* the name of the pads */
  GST_PAD_FACTORY_SINK,				/* type of the pad */
  GST_PAD_FACTORY_ALWAYS,			/* ALWAYS/SOMETIMES */
  GST_PAD_FACTORY_CAPS(
  "example_sink",					/* the name of the caps */
     "unknown/unknown",					/* the mime type of the caps */
     "something",	GST_PROPS_INT (1),		/* a property */
     "foo",		GST_PROPS_BOOLEAN (TRUE)	/* another property */
  ),
  NULL
};

static GstPadFactory src_factory = {
  "src",
  GST_PAD_FACTORY_SRC,
  GST_PAD_FACTORY_ALWAYS,
  GST_PAD_FACTORY_CAPS(
  "example_src",
    "unknown/unknown"
  ),
  NULL
};


static void	gst_example_class_init		(GstExampleClass *klass);
static void	gst_example_init		(GstExample *example);

static void	gst_example_chain		(GstPad *pad, GstBuffer *buf);

static void	gst_example_set_arg		(GtkObject *object,GtkArg *arg,guint id);
static void	gst_example_get_arg		(GtkObject *object,GtkArg *arg,guint id);

GstPadTemplate *src_template, *sink_template;

static GstElementClass *parent_class = NULL;
//static guint gst_example_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_example_get_type(void)
{
  static GtkType example_type = 0;

  if (!example_type) {
    static const GtkTypeInfo example_info = {
      "GstExample",
      sizeof(GstExample),
      sizeof(GstExampleClass),
      (GtkClassInitFunc)gst_example_class_init,
      (GtkObjectInitFunc)gst_example_init,
      (GtkArgSetFunc)gst_example_set_arg,
      (GtkArgGetFunc)gst_example_get_arg,
      (GtkClassInitFunc)NULL,
    };
    example_type = gtk_type_unique(GST_TYPE_ELEMENT,&example_info);
  }
  return example_type;
}

static void
gst_example_class_init (GstExampleClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_ELEMENT);

  gtk_object_add_arg_type("GstExample::active", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_ACTIVE);

  gtkobject_class->set_arg = gst_example_set_arg;
  gtkobject_class->get_arg = gst_example_get_arg;
}

static void
gst_example_init(GstExample *example)
{
  example->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad(GST_ELEMENT(example),example->sinkpad);
  gst_pad_set_chain_function(example->sinkpad,gst_example_chain);

  example->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_element_add_pad(GST_ELEMENT(example),example->srcpad);

  example->active = FALSE;
}

static void
gst_example_chain (GstPad *pad, GstBuffer *buf)
{
  GstExample *example;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
  //g_return_if_fail(GST_IS_BUFFER(buf));

  example = GST_EXAMPLE(gst_pad_get_parent (pad));

  g_return_if_fail(example != NULL);
  g_return_if_fail(GST_IS_EXAMPLE(example));

  if (example->active) {
    /* DO STUFF */
  }

  gst_pad_push(example->srcpad,buf);
}

static void
gst_example_set_arg (GtkObject *object,GtkArg *arg,guint id)
{
  GstExample *example;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_EXAMPLE(object));
  example = GST_EXAMPLE(object);

  switch(id) {
    case ARG_ACTIVE:
      example->active = GTK_VALUE_INT(*arg);
      g_print("example: set active to %d\n",example->active);
      break;
    default:
      break;
  }
}

static void
gst_example_get_arg (GtkObject *object,GtkArg *arg,guint id)
{
  GstExample *example;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_EXAMPLE(object));
  example = GST_EXAMPLE(object);

  switch (id) {
    case ARG_ACTIVE:
      GTK_VALUE_INT(*arg) = example->active;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

GstPlugin*
plugin_init (GModule *module)
{
  GstPlugin *plugin;
  GstElementFactory *factory;

  plugin = gst_plugin_new("example");
  g_return_val_if_fail(plugin != NULL, NULL);

  factory = gst_elementfactory_new("example", GST_TYPE_EXAMPLE, &example_details);
  g_return_val_if_fail(factory != NULL, NULL);

  sink_template = gst_padtemplate_new (&sink_factory);
  gst_elementfactory_add_padtemplate (factory, sink_template);

  src_template = gst_padtemplate_new (&src_factory);
  gst_elementfactory_add_padtemplate (factory, src_template);

  gst_plugin_add_factory (plugin, factory);

  return plugin;
}
