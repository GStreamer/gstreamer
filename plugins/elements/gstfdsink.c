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

#include <gstfdsink.h>
#include <unistd.h>


GstElementDetails gst_fdsink_details = {
  "Filedescriptor Sink",
  "Sink",
  "Write data to a file descriptor",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* FdSink signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FD
};


static void gst_fdsink_class_init(GstFdSinkClass *klass);
static void gst_fdsink_init(GstFdSink *fdsink);
static void gst_fdsink_set_arg(GtkObject *object,GtkArg *arg,guint id);
static void gst_fdsink_get_arg(GtkObject *object,GtkArg *arg,guint id);

void gst_fdsink_chain(GstPad *pad,GstBuffer *buf);

static GstSinkClass *parent_class = NULL;
//static guint gst_fdsink_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_fdsink_get_type(void) {
  static GtkType fdsink_type = 0;

  if (!fdsink_type) {
    static const GtkTypeInfo fdsink_info = {
      "GstFdSink",
      sizeof(GstFdSink),
      sizeof(GstFdSinkClass),
      (GtkClassInitFunc)gst_fdsink_class_init,
      (GtkObjectInitFunc)gst_fdsink_init,
      (GtkArgSetFunc)gst_fdsink_set_arg,
      (GtkArgGetFunc)gst_fdsink_get_arg,
      (GtkClassInitFunc)NULL,
    };
    fdsink_type = gtk_type_unique(GST_TYPE_SINK,&fdsink_info);
  }
  return fdsink_type;
}

static void
gst_fdsink_class_init(GstFdSinkClass *klass) {
  GtkObjectClass *gtkobject_class;
  GstSinkClass *gstsink_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstsink_class = (GstSinkClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_SINK);

  gtk_object_add_arg_type("GstFdSink::fd", GTK_TYPE_INT,
                          GTK_ARG_READWRITE, ARG_FD);

  gtkobject_class->set_arg = gst_fdsink_set_arg;
  gtkobject_class->get_arg = gst_fdsink_get_arg;
}

static void gst_fdsink_init(GstFdSink *fdsink) {
  fdsink->sinkpad = gst_pad_new("sink",GST_PAD_SINK);
  gst_element_add_pad(GST_ELEMENT(fdsink),fdsink->sinkpad);
  gst_pad_set_chain_function(fdsink->sinkpad,gst_fdsink_chain);

  fdsink->fd = 1;
}

GstElement *gst_fdsink_new(gchar *name) {
  GstElement *fdsink = GST_ELEMENT(gtk_type_new(GST_TYPE_FDSINK));
  gst_element_set_name(GST_ELEMENT(fdsink),name);
  return fdsink;
}

GstElement *gst_fdsink_new_with_fd(gchar *name,gint fd) {
  GstElement *fdsink = gst_fdsink_new(name);
  gtk_object_set(GTK_OBJECT(fdsink),"fd",fd,NULL);
  return fdsink;
}

void gst_fdsink_chain(GstPad *pad,GstBuffer *buf) {
  GstFdSink *fdsink;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  fdsink = GST_FDSINK(pad->parent);
  g_return_if_fail(fdsink->fd >= 0);
  if (GST_BUFFER_DATA(buf))
    write(fdsink->fd,GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
  gst_buffer_unref(buf);
}

static void gst_fdsink_set_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstFdSink *fdsink;
   
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_FDSINK(object));
  fdsink = GST_FDSINK(object);

  switch(id) {
    case ARG_FD:
      fdsink->fd = GTK_VALUE_INT(*arg);
      break;
    default:
      break;
  }
}

static void gst_fdsink_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstFdSink *fdsink;
   
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_FDSINK(object));
  fdsink = GST_FDSINK(object);

  switch(id) {
    case ARG_FD:
      GTK_VALUE_INT(*arg) = fdsink->fd;
      break;
    default:
      break;
  }
}
