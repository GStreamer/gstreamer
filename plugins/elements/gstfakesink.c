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
  /* FILL ME */
};


static void gst_fakesink_class_init(GstFakeSinkClass *klass);
static void gst_fakesink_init(GstFakeSink *fakesink);

static void gst_fakesink_chain(GstPad *pad,GstBuffer *buf);

static GstSinkClass *parent_class = NULL;
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
    fakesink_type = gtk_type_unique (GST_TYPE_SINK, &fakesink_info);
  }
  return fakesink_type;
}

static void
gst_fakesink_class_init (GstFakeSinkClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstSinkClass *gstsink_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstsink_class = (GstSinkClass*)klass;

  gst_fakesink_signals[SIGNAL_HANDOFF] =
    gtk_signal_new ("handoff", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstFakeSinkClass, handoff),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

  gtk_object_class_add_signals (gtkobject_class, gst_fakesink_signals,
                                    LAST_SIGNAL);

  parent_class = gtk_type_class (GST_TYPE_SINK);
}

static void 
gst_fakesink_init (GstFakeSink *fakesink) 
{
  fakesink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (fakesink), fakesink->sinkpad);
  gst_pad_set_chain_function (fakesink->sinkpad, gst_fakesink_chain);

  // we're ready right away, since we don't have any args...
//  gst_element_set_state(GST_ELEMENT(fakesink),GST_STATE_READY);
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

  fakesink = GST_FAKESINK (pad->parent);
  g_print("(%s:%s)< ",GST_DEBUG_PAD_NAME(pad));
  
  gst_buffer_unref (buf);

  gtk_signal_emit (GTK_OBJECT (fakesink), gst_fakesink_signals[SIGNAL_HANDOFF],
	                      fakesink);

}
