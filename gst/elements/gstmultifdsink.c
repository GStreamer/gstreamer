/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2004 Wim Taymans <wim@fluendo.com>
 *
 * gstmultifdsink.c: 
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

#include "gstmultifdsink.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_multifdsink_debug);
#define GST_CAT_DEFAULT gst_multifdsink_debug

GstElementDetails gst_multifdsink_details =
GST_ELEMENT_DETAILS ("Filedescriptor Sink",
    "Sink/File",
    "Write data to one or more file descriptors",
    "Erik Walthinsen <omega@cse.ogi.edu>");


/* MultiFdSink signals and args */
enum
{
  ADD_SIGNAL,
  REMOVE_SIGNAL,
  CLEAR_SIGNAL,
  /* FILL ME */
  LAST_SIGNAL
};
static guint gst_multifdsink_signals[LAST_SIGNAL] = { 0 };

enum
{
  ARG_0,
  ARG_FDS
};


#define _do_init(bla) \
    GST_DEBUG_CATEGORY_INIT (gst_multifdsink_debug, "multifdsink", 0, "multifdsink element");

GST_BOILERPLATE_FULL (GstMultiFdSink, gst_multifdsink, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void gst_multifdsink_add (GstElement * sink, gint fd);
static void gst_multifdsink_remove (GstElement * sink, gint fd);
static void gst_multifdsink_clear (GstElement * sink);

static void gst_multifdsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_multifdsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_multifdsink_chain (GstPad * pad, GstData * _data);


static void
gst_multifdsink_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (gstelement_class, &gst_multifdsink_details);
}
static void
gst_multifdsink_class_init (GstMultiFdSinkClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FDS,
      g_param_spec_int ("fds", "fds", "A GArray of filedescriptors",
          0, G_MAXINT, 1, G_PARAM_READWRITE));

  gst_multifdsink_signals[ADD_SIGNAL] =
      g_signal_new ("add", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFdSinkClass, add),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 0);
  gst_multifdsink_signals[REMOVE_SIGNAL] =
      g_signal_new ("remove", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFdSinkClass, remove),
      NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 0);
  gst_multifdsink_signals[CLEAR_SIGNAL] =
      g_signal_new ("clear", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstMultiFdSinkClass, clear),
      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  klass->add = gst_multifdsink_add;
  klass->remove = gst_multifdsink_remove;
  klass->clear = gst_multifdsink_clear;

  gobject_class->set_property = gst_multifdsink_set_property;
  gobject_class->get_property = gst_multifdsink_get_property;
}

static void
gst_multifdsink_init (GstMultiFdSink * multifdsink)
{
  multifdsink->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (multifdsink), multifdsink->sinkpad);
  gst_pad_set_chain_function (multifdsink->sinkpad, gst_multifdsink_chain);

  FD_ZERO (&multifdsink->writefds);
}

static void
gst_multifdsink_add (GstElement * element, gint fd)
{
  GstMultiFdSink *sink = GST_MULTIFDSINK (element);

  FD_SET (fd, &sink->writefds);
}

static void
gst_multifdsink_remove (GstElement * element, gint fd)
{
  GstMultiFdSink *sink = GST_MULTIFDSINK (element);

  FD_CLR (fd, &sink->writefds);
}

static void
gst_multifdsink_clear (GstElement * element)
{
  GstMultiFdSink *sink = GST_MULTIFDSINK (element);

  FD_ZERO (&sink->writefds);
}

static void
gst_multifdsink_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf;
  GstMultiFdSink *sink;
  struct timeval timeout;
  struct timeval *timeoutp;
  gint result;
  int fd;

  sink = GST_MULTIFDSINK (gst_pad_get_parent (pad));
  buf = GST_BUFFER (_data);

  /* if the incoming buffer has a duration, we can use that as the timeout
   * value; otherwise, we block */
  GST_LOG_OBJECT (sink, "incoming buffer duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buf))) {
    GST_TIME_TO_TIMEVAL (GST_BUFFER_DURATION (buf), timeout);
    timeoutp = &timeout;
    GST_LOG_OBJECT (sink, "select will be with timeout %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
    GST_LOG_OBJECT (sink, "select will be with timeout %d.%d",
        timeout.tv_sec, timeout.tv_usec);
  } else {
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    timeoutp = NULL;
  }

  result = select (FD_SETSIZE, (fd_set *) 0, &sink->writefds, (fd_set *) 0,
      timeoutp);

  /* Check the writes */
  for (fd = 0; fd < FD_SETSIZE; fd++) {
    if (FD_ISSET (fd, &sink->writefds)) {
      gint writesize = GST_BUFFER_SIZE (buf);
      gint written;

      GST_DEBUG ("writing %d bytes to file descriptor %d", writesize, fd);
      written = write (fd, GST_BUFFER_DATA (buf), writesize);
      if (written < writesize) {
        GST_DEBUG ("wrote only %d bytes, removing filedescriptor %d ", written,
            fd);
        gst_multifdsink_remove (GST_ELEMENT (sink), fd);
      }
    }
  }

  gst_buffer_unref (buf);
}

static void
gst_multifdsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMultiFdSink *multifdsink;

  multifdsink = GST_MULTIFDSINK (object);

  switch (prop_id) {
    case ARG_FDS:
      break;
    default:
      break;
  }
}

static void
gst_multifdsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMultiFdSink *multifdsink;

  multifdsink = GST_MULTIFDSINK (object);

  switch (prop_id) {
    case ARG_FDS:
      break;
    default:
      break;
  }
}
