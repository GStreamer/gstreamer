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
  ARG_CLOSED/*,
  ARG_SILENT,
  ARG_BYTESPERWRITE,
  ARG_NUM_SOURCES,
  */
};


static void	gst_disksink_class_init	(GstDiskSinkClass *klass);
static void	gst_disksink_init	(GstDiskSink *disksink);

static void	gst_disksink_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void	gst_disksink_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static void	gst_disksink_chain	(GstPad *pad,GstBuffer *buf);

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
      (GtkClassInitFunc)NULL,		/* QUESTION : why null ? otherwise coredump */
    };
    disksink_type = gtk_type_unique (GST_TYPE_ELEMENT, &disksink_info);
  }
  return disksink_type;
}

static void
gst_disksink_class_init (GstDiskSinkClass *klass) 
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);


  gtk_object_add_arg_type ("GstDiskSink::location", GST_TYPE_FILENAME,
                           GTK_ARG_READWRITE, ARG_LOCATION);
/*
  gtk_object_add_arg_type ("GstDiskSink::silent", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_SILENT);
*/
  gtk_object_add_arg_type ("GstDiskSink::closed", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_CLOSED);


  gst_disksink_signals[SIGNAL_HANDOFF] =
    gtk_signal_new ("handoff", GTK_RUN_LAST, gtkobject_class->type,
                    GTK_SIGNAL_OFFSET (GstDiskSinkClass, handoff),
                    gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

  gtk_object_class_add_signals (gtkobject_class, gst_disksink_signals,
                                    LAST_SIGNAL);


  gtkobject_class->set_arg = gst_disksink_set_arg;
  gtkobject_class->get_arg = gst_disksink_get_arg;
}

static void 
gst_disksink_init (GstDiskSink *disksink) 
{
  GstPad *pad;
  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (disksink), pad);
  gst_pad_set_chain_function (pad, gst_disksink_chain);
  disksink->opened = FALSE;
  disksink->filename = NULL;
  disksink->file = NULL;
  
//  disksink->silent = FALSE;  ? what's this ? it's for output !
}

static void
gst_disksink_set_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstDiskSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_DISKSINK (object);

  switch(id) {
    case ARG_LOCATION:
     /* the element must be stopped in order to do this */
      g_return_if_fail (GST_STATE (sink) < GST_STATE_PLAYING);  

      if (sink->filename) g_free (sink->filename);
      
      sink->filename = g_strdup (GTK_VALUE_STRING (*arg));
      sink->file = fopen (GTK_VALUE_STRING (*arg), "w");
      if (sink->file == NULL)
      {
        g_error ("Cannot open %s for writing !\n", GTK_VALUE_STRING (*arg));
		//exit (-2);
      }
      else sink->opened = TRUE;
      gst_element_set_state(GST_ELEMENT(sink),GST_STATE_READY);
      break;
      /*
    case ARG_SILENT:
      sink->silent = GTK_VALUE_BOOL (*arg);
      break;
      */
    case ARG_CLOSED:
      if (GTK_VALUE_BOOL (*arg) == TRUE)
      {
        /* close the file descriptor */
        sink->opened = FALSE;
        if (! (fclose (sink->file)))
        {
          g_warning ("Cannot close file !\n");
        }
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
  /*
    case ARG_SILENT:
      GTK_VALUE_BOOL (*arg) = sink->silent;
      break;
      */
    case ARG_CLOSED:
      GTK_VALUE_BOOL (*arg) = !sink->opened;
      break;
    case ARG_LOCATION:
      GTK_VALUE_STRING (*arg) = sink->filename;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
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
  guint16 bytes_written = 0;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  disksink = GST_DISKSINK (gst_pad_get_parent (pad));
/*
  if (!disksink->silent)
    g_print("disksink: ******* (%s:%s)< \n",GST_DEBUG_PAD_NAME(pad));
*/
  if (disksink->opened)
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
