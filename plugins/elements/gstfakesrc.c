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
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_SOURCES,
};


static void		gst_fakesrc_class_init	(GstFakeSrcClass *klass);
static void		gst_fakesrc_init	(GstFakeSrc *fakesrc);

static void		gst_fakesrc_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void		gst_fakesrc_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static GstBuffer *	gst_fakesrc_get		(GstPad *pad);

static GstSrcClass *parent_class = NULL;
static guint gst_fakesrc_signals[LAST_SIGNAL] = { 0 };

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
  GtkObjectClass *gtkobject_class;
  GstSrcClass *gstsrc_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstsrc_class = (GstSrcClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_SRC);

  gtk_object_add_arg_type ("GstFakeSrc::num_sources", GTK_TYPE_INT,
                           GTK_ARG_READWRITE, ARG_NUM_SOURCES);

  gtkobject_class->set_arg = gst_fakesrc_set_arg;
  gtkobject_class->get_arg = gst_fakesrc_get_arg;

  gst_fakesrc_signals[SIGNAL_HANDOFF] =
    gtk_signal_new ("handoff", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstFakeSrcClass, handoff),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

  gtk_object_class_add_signals (gtkobject_class, gst_fakesrc_signals,
                                LAST_SIGNAL);

}

static void gst_fakesrc_init(GstFakeSrc *fakesrc) {
  GstPad *pad;

  // set the default number of 
  fakesrc->numsrcpads = 1;

  // create our first output pad
  pad = gst_pad_new("src",GST_PAD_SRC);
  gst_pad_set_get_function(pad,gst_fakesrc_get);
  gst_element_add_pad(GST_ELEMENT(fakesrc),pad);
  fakesrc->srcpads = g_slist_append(NULL,pad);

  // we're ready right away, since we don't have any args...
//  gst_element_set_state(GST_ELEMENT(fakesrc),GST_STATE_READY);
}

static void
gst_fakesrc_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstFakeSrc *src;
  gint new_numsrcs;
  GstPad *pad;

  /* it's not null if we got it, but it might not be ours */
  src = GST_FAKESRC (object);
   
  switch(id) {
    case ARG_NUM_SOURCES:
      new_numsrcs = GTK_VALUE_INT (*arg);
      if (new_numsrcs > src->numsrcpads) {
        while (src->numsrcpads != new_numsrcs) {
          pad = gst_pad_new(g_strdup_printf("src%d",src->numsrcpads),GST_PAD_SRC);
          gst_pad_set_get_function(pad,gst_fakesrc_get);
          gst_element_add_pad(GST_ELEMENT(src),pad);
          src->srcpads = g_slist_append(src->srcpads,pad);
          src->numsrcpads++;
        }
      }
      break;
    default:
      break;
  }
}

static void 
gst_fakesrc_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstFakeSrc *src;
   
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FAKESRC (object));
  
  src = GST_FAKESRC (object);
   
  switch (id) {
    case ARG_NUM_SOURCES:
      GTK_VALUE_INT (*arg) = src->numsrcpads;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}


/**
 * gst_fakesrc_get:
 * @src: the faksesrc to get
 * 
 * generate an empty buffer and push it to the next element.
 */
static GstBuffer *
gst_fakesrc_get(GstPad *pad)
{
  GstFakeSrc *src;
  GstBuffer *buf;

  g_return_val_if_fail(pad != NULL, NULL);
  src = GST_FAKESRC(gst_pad_get_parent(pad));
  g_return_val_if_fail(GST_IS_FAKESRC(src), NULL);

  g_print("(%s:%s)> ",GST_DEBUG_PAD_NAME(pad));
  buf = gst_buffer_new();

  gtk_signal_emit (GTK_OBJECT (src), gst_fakesrc_signals[SIGNAL_HANDOFF],
                                  src);

  return buf;
}
