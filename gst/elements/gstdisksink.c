/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstdisksink.c: 
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
#include "gstdisksink.h"


GstElementDetails gst_disksink_details = {
  "Disk Sink",
  "Sink",
  "Disk hole for data",
  VERSION,
  "Thomas <thomas@apestaart.org>",
  "(C) 2001"
};


/* DiskSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOCATION,
};


static void	gst_disksink_class_init	(GstDiskSinkClass *klass);
static void	gst_disksink_init	(GstDiskSink *disksink);

static void	gst_disksink_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void	gst_disksink_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static gboolean gst_disksink_open_file 	(GstDiskSink *sink);
static void 	gst_disksink_close_file (GstDiskSink *sink);

static void	gst_disksink_chain	(GstPad *pad,GstBuffer *buf);

static GstElementStateReturn gst_disksink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_disksink_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_disksink_get_type (void) 
{
  static GtkType disksink_type = 0;

  if (!disksink_type) {
    static const GtkTypeInfo disksink_info = {
      "GstDiskSink",
      sizeof(GstDiskSink),
      sizeof(GstDiskSinkClass),
      (GtkClassInitFunc)gst_disksink_class_init,
      (GtkObjectInitFunc)gst_disksink_init,
      (GtkArgSetFunc)gst_disksink_set_arg,
      (GtkArgGetFunc)gst_disksink_get_arg,
      (GtkClassInitFunc)NULL,	/* deprecated, do not use ! */
    };
    disksink_type = gtk_type_unique (GST_TYPE_ELEMENT, &disksink_info);
  }
  return disksink_type;
}

static void
gst_disksink_class_init (GstDiskSinkClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstDiskSink::location", GST_TYPE_FILENAME,
                           GTK_ARG_READWRITE, ARG_LOCATION);

  gst_disksink_signals[SIGNAL_HANDOFF] =
    gtk_signal_new ("handoff", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstDiskSinkClass, handoff),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

  gtk_object_class_add_signals (gtkobject_class, gst_disksink_signals,
                                    LAST_SIGNAL);

  gtkobject_class->set_arg = gst_disksink_set_arg;
  gtkobject_class->get_arg = gst_disksink_get_arg;

  gstelement_class->change_state = gst_disksink_change_state;
}

static void 
gst_disksink_init (GstDiskSink *disksink) 
{
  GstPad *pad;
  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (disksink), pad);
  gst_pad_set_chain_function (pad, gst_disksink_chain);

  disksink->filename = NULL;
  disksink->file = NULL;
}

static void
gst_disksink_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstDiskSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_DISKSINK (object);

  switch(id) {
    case ARG_LOCATION:
      /* the element must be stopped or paused in order to do this */
      g_return_if_fail ((GST_STATE (sink) < GST_STATE_PLAYING)
                      || (GST_STATE (sink) == GST_STATE_PAUSED));
      if (sink->filename)
	g_free (sink->filename);
      sink->filename = g_strdup (GTK_VALUE_STRING (*arg));
      if ( (GST_STATE (sink) == GST_STATE_PAUSED) 
        && (sink->filename != NULL))
      {
              gst_disksink_close_file (sink);
              gst_disksink_open_file (sink);   
      }
 
      break;
    default:
      break;
  }
}

static void   
gst_disksink_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstDiskSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DISKSINK (object));
 
  sink = GST_DISKSINK (object);
  
  switch (id) {
    case ARG_LOCATION:
      GTK_VALUE_STRING (*arg) = sink->filename;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}

static gboolean
gst_disksink_open_file (GstDiskSink *sink)
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (sink, GST_DISKSINK_OPEN), FALSE);

  /* open the file */
  sink->file = fopen (sink->filename, "w");
  if (sink->file == NULL) {
    perror ("open");
    gst_element_error (GST_ELEMENT (sink), g_strconcat("opening file \"", sink->filename, "\"", NULL));
    return FALSE;
  } 

  GST_FLAG_SET (sink, GST_DISKSINK_OPEN);

  return TRUE;
}

static void
gst_disksink_close_file (GstDiskSink *sink)
{
  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_DISKSINK_OPEN));

  if (fclose (sink->file) != 0)
  {
    perror ("close");
    gst_element_error (GST_ELEMENT (sink), g_strconcat("closing file \"", sink->filename, "\"", NULL));
  }
  else {
    GST_FLAG_UNSET (sink, GST_DISKSINK_OPEN);
  }
}

/**
 * gst_disksink_chain:
 * @pad: the pad this disksink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and write to file if it's open
 */
static void 
gst_disksink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstDiskSink *disksink;
  gint bytes_written = 0;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  disksink = GST_DISKSINK (gst_pad_get_parent (pad));

  if (GST_FLAG_IS_SET (disksink, GST_DISKSINK_OPEN))
  {
    bytes_written = fwrite (GST_BUFFER_DATA (buf), 1, GST_BUFFER_SIZE (buf), disksink->file);
    if (bytes_written < GST_BUFFER_SIZE (buf))
    {
      printf ("disksink : Warning : %d bytes should be written, only %d bytes written\n",
      		  GST_BUFFER_SIZE (buf), bytes_written);
    }
  }
  gst_buffer_unref (buf);

  gtk_signal_emit (GTK_OBJECT (disksink), gst_disksink_signals[SIGNAL_HANDOFF],
	                      disksink);
}

static GstElementStateReturn
gst_disksink_change_state (GstElement *element)
{
  g_return_val_if_fail (GST_IS_DISKSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_DISKSINK_OPEN))
      gst_disksink_close_file (GST_DISKSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_DISKSINK_OPEN)) {
      if (!gst_disksink_open_file (GST_DISKSINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

