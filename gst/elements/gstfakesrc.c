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


#include <gstfakesrc.h>


GstElementDetails gst_fakesrc_details = {
  "Fake Source",
  "Source",
  "Push empty (no data) buffers around",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* FakeSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};


static void gst_fakesrc_class_init	(GstFakeSrcClass *klass);
static void gst_fakesrc_init		(GstFakeSrc *fakesrc);

static void gst_fakesrc_get		(GstPad *pad);

static GstSrcClass *parent_class = NULL;
//static guint gst_fakesrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fakesrc_get_type (void) 
{
  static GtkType fakesrc_type = 0;

  if (!fakesrc_type) {
    static const GtkTypeInfo fakesrc_info = {
      "GstFakeSrc",
      sizeof(GstFakeSrc),
      sizeof(GstFakeSrcClass),
      (GtkClassInitFunc)gst_fakesrc_class_init,
      (GtkObjectInitFunc)gst_fakesrc_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    fakesrc_type = gtk_type_unique (GST_TYPE_SRC, &fakesrc_info);
  }
  return fakesrc_type;
}

static void
gst_fakesrc_class_init (GstFakeSrcClass *klass) 
{
  GstSrcClass *gstsrc_class;

  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_SRC);
}

static void gst_fakesrc_init(GstFakeSrc *fakesrc) {
  // create our output pad
  fakesrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_pad_set_get_function(fakesrc->srcpad,gst_fakesrc_get);
  gst_element_add_pad(GST_ELEMENT(fakesrc),fakesrc->srcpad);

  // we're ready right away, since we don't have any args...
//  gst_element_set_state(GST_ELEMENT(fakesrc),GST_STATE_READY);
}

/**
 * gst_fakesrc_new:
 * @name: then name of the fakse source
 * 
 * create a new fakesrc
 *
 * Returns: The new element.
 */
GstElement *gst_fakesrc_new(gchar *name) {
  GstElement *fakesrc = GST_ELEMENT(gtk_type_new(GST_TYPE_FAKESRC));
  gst_element_set_name(GST_ELEMENT(fakesrc),name);
  return fakesrc;
}

/**
 * gst_fakesrc_get:
 * @src: the faksesrc to get
 * 
 * generate an empty buffer and push it to the next element.
 */
void gst_fakesrc_get(GstPad *pad) {
  GstFakeSrc *src;
  GstBuffer *buf;

  g_return_if_fail(pad != NULL);
  src = GST_FAKESRC(gst_pad_get_parent(pad));
  g_return_if_fail(GST_IS_FAKESRC(src));

  g_print("(%s:%s)> ",GST_DEBUG_PAD_NAME(pad));
  buf = gst_buffer_new();
  gst_pad_push(pad,buf);
}
