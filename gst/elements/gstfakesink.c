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
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_fakesink_class_init(GstFakeSinkClass *klass);
static void gst_fakesink_init(GstFakeSink *fakesink);

GstElement *gst_fakesink_new(gchar *name);
void gst_fakesink_chain(GstPad *pad,GstBuffer *buf);

static GstSinkClass *parent_class = NULL;
//static guint gst_fakesink_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fakesink_get_type(void) {
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
    fakesink_type = gtk_type_unique(GST_TYPE_SINK,&fakesink_info);
  }
  return fakesink_type;
}

static void
gst_fakesink_class_init(GstFakeSinkClass *klass) {
  GstSinkClass *gstsink_class;

  gstsink_class = (GstSinkClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SINK);
}

static void gst_fakesink_init(GstFakeSink *fakesink) {
  fakesink->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(fakesink),fakesink->sinkpad);
  gst_pad_set_chain_function(fakesink->sinkpad,gst_fakesink_chain);

  // we're already complete, since we don't have any args...
  gst_element_set_state(GST_ELEMENT(fakesink),GST_STATE_COMPLETE);
}

/**
 * gst_fakesink_new:
 * @name: the name of the new fakesrc
 *
 * create a new fakesink
 *
 * Returns: the new fakesink
 */
GstElement *gst_fakesink_new(gchar *name) {
  GstElement *fakesink = GST_ELEMENT(gtk_type_new(GST_TYPE_FAKESINK));
  gst_element_set_name(GST_ELEMENT(fakesink),name);
  return fakesink;
}

/**
 * gst_fakesink_chain:
 * @pad: the pad this faksink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and unref it without doing
 * anything with it.
 */
void gst_fakesink_chain(GstPad *pad,GstBuffer *buf) {
  GstFakeSink *fakesink;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  fakesink = GST_FAKESINK(pad->parent);
//  g_print("gst_fakesink_chain: got buffer of %d bytes in '%s'\n",
//          buf->datasize,gst_element_get_name(GST_ELEMENT(fakesink)));
  g_print("<");
  gst_buffer_unref(buf);
}
