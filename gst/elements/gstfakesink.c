/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfakesink.c: 
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


#include <gstfakesink.h>


GstElementDetails gst_fakesink_details = {
  "Fake Sink",
  "Sink",
  "Black hole for data",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* FakeSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_SOURCES,
  ARG_SILENT,
};


static void	gst_fakesink_class_init	(GstFakeSinkClass *klass);
static void	gst_fakesink_init	(GstFakeSink *fakesink);

static void	gst_fakesink_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void	gst_fakesink_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static void	gst_fakesink_chain	(GstPad *pad,GstBuffer *buf);

static GstElementClass *parent_class = NULL;
static guint gst_fakesink_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fakesink_get_type (void) 
{
  static GtkType fakesink_type = 0;

  if (!fakesink_type) {
    static const GtkTypeInfo fakesink_info = {
      "GstFakeSink",
      sizeof(GstFakeSink),
      sizeof(GstFakeSinkClass),
      (GtkClassInitFunc)gst_fakesink_class_init,
      (GtkObjectInitFunc)gst_fakesink_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    fakesink_type = gtk_type_unique (GST_TYPE_ELEMENT, &fakesink_info);
  }
  return fakesink_type;
}

static void
gst_fakesink_class_init (GstFakeSinkClass *klass) 
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstFakeSink::num_sources", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_NUM_SOURCES);
  gtk_object_add_arg_type ("GstFakeSink::silent", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_SILENT);

  gst_fakesink_signals[SIGNAL_HANDOFF] =
    gtk_signal_new ("handoff", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstFakeSinkClass, handoff),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

  gtk_object_class_add_signals (gtkobject_class, gst_fakesink_signals,
                                    LAST_SIGNAL);

  gtkobject_class->set_arg = gst_fakesink_set_arg;
  gtkobject_class->get_arg = gst_fakesink_get_arg;
}

static void 
gst_fakesink_init (GstFakeSink *fakesink) 
{
  GstPad *pad;
  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (fakesink), pad);
  gst_pad_set_chain_function (pad, gst_fakesink_chain);
  fakesink->sinkpads = g_slist_prepend (NULL, pad);
  fakesink->numsinkpads = 1;
  fakesink->silent = FALSE;

  // we're ready right away, since we don't have any args...
//  gst_element_set_state(GST_ELEMENT(fakesink),GST_STATE_READY);
}

static void
gst_fakesink_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstFakeSink *sink;
  gint new_numsinks;
  GstPad *pad;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_FAKESINK (object);

  switch(id) {
    case ARG_NUM_SOURCES:
      new_numsinks = GTK_VALUE_INT (*arg);
      while (sink->numsinkpads < new_numsinks) {
        pad = gst_pad_new (g_strdup_printf ("sink%d", sink->numsinkpads), GST_PAD_SINK);
        gst_pad_set_chain_function (pad, gst_fakesink_chain);
        gst_element_add_pad (GST_ELEMENT (sink), pad);
        sink->sinkpads = g_slist_append (sink->sinkpads, pad);
        sink->numsinkpads++;
      }
      break;
    case ARG_SILENT:
      sink->silent = GTK_VALUE_BOOL (*arg);
      break;
    default:
      break;
  }
}

static void   
gst_fakesink_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstFakeSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FAKESINK (object));
 
  sink = GST_FAKESINK (object);
  
  switch (id) {
    case ARG_NUM_SOURCES:
      GTK_VALUE_INT (*arg) = sink->numsinkpads;
      break;
    case ARG_SILENT:
      GTK_VALUE_BOOL (*arg) = sink->silent;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

/**
 * gst_fakesink_chain:
 * @pad: the pad this faksink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and unref it without doing
 * anything with it.
 */
static void 
gst_fakesink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstFakeSink *fakesink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  fakesink = GST_FAKESINK (gst_pad_get_parent (pad));
  if (!fakesink->silent)
    g_print("fakesink: ******* (%s:%s)< \n",GST_DEBUG_PAD_NAME(pad));
  
  gst_buffer_unref (buf);

  gtk_signal_emit (GTK_OBJECT (fakesink), gst_fakesink_signals[SIGNAL_HANDOFF],
	                      fakesink);

}
