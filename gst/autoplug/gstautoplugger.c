/* GStreamer
 * Copyright (C) 2001 RidgeRun, Inc. (www.ridgerun.com)
 *
 * gstautoplugger.c: Data  for the dynamic autopluggerger
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

GstElementDetails gst_autoplugger_details = {
  "Dynamic autoplugger",
  "Autoplugger",
  "Magic element that converts from any type to any other",
  VERSION,
  "Erik Walthinsen <omega@temple-baptist.com>",
  "(C) 2001 RidgeRun, Inc. (www.ridgerun.com)",
};

#define GST_TYPE_AUTOPLUGGER \
  (gst_autoplugger_get_type())
#define GST_AUTOPLUGGER(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_AUTOPLUGGER,GstAutoplugger))
#define GST_AUTOPLUGGER_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUGGER,GstAutopluggerClass))
#define GST_IS_AUTOPLUGGER(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_AUTOPLUGGER))
#define GST_IS_AUTOPLUGGER_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUGGER))

typedef struct _GstAutoplugger GstAutoplugger;
typedef struct _GstAutopluggerClass GstAutopluggerClass;

struct _GstAutoplugger {
  GstBin bin;

  GstGhostPad *srcghost, *sinkghost;

  GstElement *cache, *typefind;
};

struct _GstAutopluggerClass {
  GstBinClass parent_class;
};


/*  signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
};


static void			gst_autoplugger_class_init		(GstAutopluggerClass *klass);
static void			gst_autoplugger_init		(GstAutoplugger *queue);

static void			gst_autoplugger_set_arg		(GtkObject *object, GtkArg *arg, guint id);
static void			gst_autoplugger_get_arg		(GtkObject *object, GtkArg *arg, guint id);

//static GstElementStateReturn	gst_autoplugger_change_state	(GstElement *element);


static void	gst_autoplugger_external_sink_caps_changed	(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger);
static void	gst_autoplugger_external_src_caps_changed	(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger);

static GstElementClass *parent_class = NULL;
//static guint gst_autoplugger_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_autoplugger_get_type(void) {
  static GtkType autoplugger_type = 0;

  if (!autoplugger_type) {
    static const GtkTypeInfo autoplugger_info = {
      "GstAutoplugger",
      sizeof(GstAutoplugger),
      sizeof(GstAutopluggerClass),
      (GtkClassInitFunc)gst_autoplugger_class_init,
      (GtkObjectInitFunc)gst_autoplugger_init,
      (GtkArgSetFunc)gst_autoplugger_set_arg,
      (GtkArgGetFunc)gst_autoplugger_get_arg,
      (GtkClassInitFunc)NULL,
    };
    autoplugger_type = gtk_type_unique (GST_TYPE_BIN, &autoplugger_info);
  }
  return autoplugger_type;
}

static void
gst_autoplugger_class_init (GstAutopluggerClass *klass)
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

/*
  gst_autoplugger_signals[_EMPTY] =
    gtk_signal_new ("_empty", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstAutopluggerClass, _empty),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);
  gtk_object_class_add_signals (gtkobject_class, gst_autoplugger_signals, LAST_SIGNAL);
*/

/*
  gtk_object_add_arg_type ("GstAutoplugger::buffer_count", GTK_TYPE_INT,
                           GTK_ARG_READABLE, ARG_BUFFER_COUNT);
  gtk_object_add_arg_type ("GstAutoplugger::reset", GTK_TYPE_BOOL,
                           GTK_ARG_WRITABLE, ARG_RESET);
*/

  gtkobject_class->set_arg = gst_autoplugger_set_arg;
  gtkobject_class->get_arg = gst_autoplugger_get_arg;

//  gstelement_class->change_state = gst_autoplugger_change_state;
}

static void
gst_autoplugger_init (GstAutoplugger *autoplugger)
{
  GstPad *srcpad, *sinkpad;

  // create the autoplugger cache, which is the fundamental unit of the autopluggerger
  // FIXME we need to find a way to set element's name before _init
  // FIXME ... so we can name the subelements uniquely
  autoplugger->cache = gst_elementfactory_make("autoplugcache", "unnamed_autoplugcache");
  g_return_if_fail (autoplugger->cache != NULL);

  // add the cache to self
  gst_bin_add (GST_BIN(autoplugger), autoplugger->cache);

  // get the cache's pads so we can attach stuff to them
  sinkpad = gst_element_get_pad (autoplugger->cache, "sink");
  srcpad = gst_element_get_pad (autoplugger->cache, "src");

  // attach handlers to the typefind pads
  gtk_signal_connect (GTK_OBJECT (sinkpad), "caps_changed",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_sink_caps_changed), autoplugger);
  gtk_signal_connect (GTK_OBJECT (srcpad), "caps_changed",
                      GTK_SIGNAL_FUNC (gst_autoplugger_external_src_caps_changed), autoplugger);

  // ghost both of these pads to the outside world
  gst_element_add_ghost_pad (GST_ELEMENT(autoplugger), sinkpad, "sink");
  gst_element_add_ghost_pad (GST_ELEMENT(autoplugger), srcpad, "src");
}


static void
gst_autoplugger_external_sink_caps_changed(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger)
{
  GST_INFO(GST_CAT_AUTOPLUG, "have cache:sink caps of %s\n",gst_caps_get_mime(caps));
}

static void
gst_autoplugger_external_src_caps_changed(GstPad *pad, GstCaps *caps, GstAutoplugger *autoplugger)
{
  GST_INFO(GST_CAT_AUTOPLUG, "have cache:src caps of %s\n",gst_caps_get_mime(caps));
}

static void
gst_autoplugger_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstAutoplugger *autoplugger;

  autoplugger = GST_AUTOPLUGGER (object);

  switch (id) {
    default:
      break;
  }
}

static void
gst_autoplugger_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstAutoplugger *autoplugger;

  autoplugger = GST_AUTOPLUGGER (object);

  switch (id) {
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new ("autoplugger", GST_TYPE_AUTOPLUGGER,
                                    &gst_autoplugger_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_plugin_add_factory (plugin, factory);

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "autoplugger",
  plugin_init
};

