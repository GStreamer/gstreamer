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


static void	gst_disksink_class_init		(GstDiskSinkClass *klass);
static void	gst_disksink_init		(GstDiskSink *disksink);

static void	gst_disksink_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_disksink_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static gboolean gst_disksink_open_file 		(GstDiskSink *sink);
static void 	gst_disksink_close_file 	(GstDiskSink *sink);

static void	gst_disksink_chain		(GstPad *pad,GstBuffer *buf);

static GstElementStateReturn gst_disksink_change_state (GstElement *element);

static GstElementClass *parent_class = NULL;
static guint gst_disksink_signals[LAST_SIGNAL] = { 0 };

GType
gst_disksink_get_type (void) 
{
  static GType disksink_type = 0;

  if (!disksink_type) {
    static const GTypeInfo disksink_info = {
      sizeof(GstDiskSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_disksink_class_init,
      NULL,
      NULL,
      sizeof(GstDiskSink),
      0,
      (GInstanceInitFunc)gst_disksink_init,
    };
    disksink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstDiskSink", &disksink_info, 0);
  }
  return disksink_type;
}

static void
gst_disksink_class_init (GstDiskSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_LOCATION,
    g_param_spec_string("location","location","location",
                        NULL,G_PARAM_READWRITE)); // CHECKME!

  gst_disksink_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstDiskSinkClass, handoff), NULL, NULL,
                    g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  gobject_class->set_property = gst_disksink_set_property;
  gobject_class->get_property = gst_disksink_get_property;

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
gst_disksink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDiskSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_DISKSINK (object);

  switch (prop_id) {
    case ARG_LOCATION:
      /* the element must be stopped or paused in order to do this */
      g_return_if_fail ((GST_STATE (sink) < GST_STATE_PLAYING)
                      || (GST_STATE (sink) == GST_STATE_PAUSED));
      if (sink->filename)
	g_free (sink->filename);
      sink->filename = g_strdup (g_value_get_string (value));
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
gst_disksink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDiskSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DISKSINK (object));
 
  sink = GST_DISKSINK (object);
  
  switch (prop_id) {
    case ARG_LOCATION:
      g_value_set_string (value, sink->filename);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
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

  g_signal_emit (G_OBJECT (disksink), gst_disksink_signals[SIGNAL_HANDOFF], 0,
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

