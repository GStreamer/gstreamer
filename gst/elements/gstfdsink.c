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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstfdsink.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_fdsink_debug);
#define GST_CAT_DEFAULT gst_fdsink_debug

GstElementDetails gst_fdsink_details =
GST_ELEMENT_DETAILS ("Filedescriptor Sink",
    "Sink/File",
    "Write data to a file descriptor",
    "Erik Walthinsen <omega@cse.ogi.edu>");


/* FdSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_FD
};


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_fdsink_debug, "fdsink", 0, "fdsink element");

GST_BOILERPLATE_FULL (GstFdSink, gst_fdsink, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void gst_fdsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fdsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_fdsink_chain (GstPad * pad, GstData * _data);


static void
gst_fdsink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_fdsink_details);
}
static void
gst_fdsink_class_init (GstFdSinkClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);


  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FD,
      g_param_spec_int ("fd", "fd", "An open file descriptor to write to",
          0, G_MAXINT, 1, G_PARAM_READWRITE));

  gobject_class->set_property = gst_fdsink_set_property;
  gobject_class->get_property = gst_fdsink_get_property;
}

static void
gst_fdsink_init (GstFdSink * fdsink)
{
  fdsink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (fdsink), fdsink->sinkpad);
  gst_pad_set_chain_function (fdsink->sinkpad, gst_fdsink_chain);

  fdsink->fd = 1;
}

static void
gst_fdsink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstFdSink *fdsink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  fdsink = GST_FDSINK (gst_pad_get_parent (pad));

  g_return_if_fail (fdsink->fd >= 0);

  if (GST_BUFFER_DATA (buf)) {
    GST_DEBUG ("writing %d bytes to file descriptor %d", GST_BUFFER_SIZE (buf),
        fdsink->fd);
    write (fdsink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

  gst_buffer_unref (buf);
}

static void
gst_fdsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
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
gst_fdsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
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
