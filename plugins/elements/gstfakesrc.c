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


static void gst_fakesrc_class_init(GstFakeSrcClass *klass);
static void gst_fakesrc_init(GstFakeSrc *fakesrc);


static GstSrcClass *parent_class = NULL;
static guint gst_fakesrc_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fakesrc_get_type(void) {
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
    fakesrc_type = gtk_type_unique(GST_TYPE_SRC,&fakesrc_info);
  }
  return fakesrc_type;
}

static void
gst_fakesrc_class_init(GstFakeSrcClass *klass) {
  GstSrcClass *gstsrc_class;

  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SRC);

  gstsrc_class->push = gst_fakesrc_push;
  gstsrc_class->push_region = NULL;
}

static void gst_fakesrc_init(GstFakeSrc *fakesrc) {
  fakesrc->srcpad = gst_pad_new("src",GST_PAD_SRC);
  gst_element_add_pad(GST_ELEMENT(fakesrc),fakesrc->srcpad);

  // we're already complete, since we don't have any args...
  gst_element_set_state(GST_ELEMENT(fakesrc),GST_STATE_COMPLETE);
}

GstElement *gst_fakesrc_new(gchar *name) {
  GstElement *fakesrc = GST_ELEMENT(gtk_type_new(GST_TYPE_FAKESRC));
  gst_element_set_name(GST_ELEMENT(fakesrc),name);
  return fakesrc;
}

void gst_fakesrc_push(GstSrc *src) {
  GstFakeSrc *fakesrc;
  GstBuffer *buf;

  g_return_if_fail(src != NULL);
  g_return_if_fail(GST_IS_FAKESRC(src));
  fakesrc = GST_FAKESRC(src);

//  g_print("gst_fakesrc_push(): pushing fake buffer from '%s'\n",
//          gst_element_get_name(GST_ELEMENT(fakesrc)));
  g_print(">");
  buf = gst_buffer_new();
  gst_pad_push(fakesrc->srcpad,buf);
}
