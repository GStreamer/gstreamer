/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfdsink.c: 
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


static void 	gst_fdsink_class_init	(GstFdSinkClass *klass);
static void 	gst_fdsink_init		(GstFdSink *fdsink);

static void 	gst_fdsink_set_property	(GObject *object, guint prop_id, 
					 const GValue *value, GParamSpec *pspec);
static void 	gst_fdsink_get_property	(GObject *object, guint prop_id, 
					 GValue *value, GParamSpec *pspec);

static void 	gst_fdsink_chain	(GstPad *pad,GstBuffer *buf);

static GstElementClass *parent_class = NULL;
//static guint gst_fdsink_signals[LAST_SIGNAL] = { 0 };

GType
gst_fdsink_get_type (void) 
{
  static GType fdsink_type = 0;

  if (!fdsink_type) {
    static const GTypeInfo fdsink_info = {
      sizeof(GstFdSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_fdsink_class_init,
      NULL,
      NULL,
      sizeof(GstFdSink),
      0,
      (GInstanceInitFunc)gst_fdsink_init,
    };
    fdsink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFdSink", &fdsink_info, 0);
  }
  return fdsink_type;
}

static void
gst_fdsink_class_init (GstFdSinkClass *klass) 
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FD,
    g_param_spec_int("fd","fd","fd",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME

  gobject_class->set_property = gst_fdsink_set_property;
  gobject_class->get_property = gst_fdsink_get_property;
}

static void 
gst_fdsink_init (GstFdSink *fdsink) 
{
  fdsink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (fdsink), fdsink->sinkpad);
  gst_pad_set_chain_function (fdsink->sinkpad, gst_fdsink_chain);

  fdsink->fd = 1;
}

static void 
gst_fdsink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstFdSink *fdsink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  fdsink = GST_FDSINK (gst_pad_get_parent (pad));
  
  g_return_if_fail (fdsink->fd >= 0);
  
  if (GST_BUFFER_DATA (buf)) {
    GST_DEBUG (0,"writing %d bytes to file descriptor %d\n",GST_BUFFER_SIZE (buf), fdsink->fd);
    write (fdsink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }
  
  gst_buffer_unref (buf);
}

static void 
gst_fdsink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstFdSink *fdsink;
   
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSINK (object));
  
  fdsink = GST_FDSINK (object);

  switch (prop_id) {
    case ARG_FD:
      fdsink->fd = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void 
gst_fdsink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstFdSink *fdsink;
   
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FDSINK (object));
  
  fdsink = GST_FDSINK (object);

  switch (prop_id) {
    case ARG_FD:
      g_value_set_int (value, fdsink->fd);
      break;
    default:
      break;
  }
}
