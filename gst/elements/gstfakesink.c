/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfakesink.c: 
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


#include <gstfakesink.h>


GstElementDetails gst_fakesink_details = {
  "Fake Sink",
  "Sink",
  "Black hole for data",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* FakeSink signals and args */
enum {
  /* FILL ME */
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_SOURCES,
  ARG_SILENT,
};


static void	gst_fakesink_class_init	(GstFakeSinkClass *klass);
static void	gst_fakesink_init	(GstFakeSink *fakesink);

static void	gst_fakesink_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_fakesink_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void	gst_fakesink_chain	(GstPad *pad,GstBuffer *buf);

static GstElementClass *parent_class = NULL;
static guint gst_fakesink_signals[LAST_SIGNAL] = { 0 };

GType
gst_fakesink_get_type (void) 
{
  static GType fakesink_type = 0;

  if (!fakesink_type) {
    static const GTypeInfo fakesink_info = {
      sizeof(GstFakeSinkClass),      NULL,
      NULL,
      (GClassInitFunc)gst_fakesink_class_init,
      NULL,
      NULL,
      sizeof(GstFakeSink),
      0,
      (GInstanceInitFunc)gst_fakesink_init,
    };
    fakesink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstFakeSink", &fakesink_info, 0);
  }
  return fakesink_type;
}

static void
gst_fakesink_class_init (GstFakeSinkClass *klass) 
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUM_SOURCES,
    g_param_spec_int("num_sources","num_sources","num_sources",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SILENT,
    g_param_spec_boolean("silent","silent","silent",
                         TRUE,G_PARAM_READWRITE)); // CHECKME

  gst_fakesink_signals[SIGNAL_HANDOFF] =
    g_signal_newc ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                    G_STRUCT_OFFSET (GstFakeSinkClass, handoff), NULL, NULL,
                    g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                    G_TYPE_POINTER);

  gobject_class->set_property = gst_fakesink_set_property;
  gobject_class->get_property = gst_fakesink_get_property;
}

static void 
gst_fakesink_init (GstFakeSink *fakesink) 
{
  GstPad *pad;
  pad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (fakesink), pad);
  gst_pad_set_chain_function (pad, gst_fakesink_chain);
  fakesink->sinkpads = g_slist_prepend (NULL, pad);
  fakesink->numsinkpads = 1;
  fakesink->silent = FALSE;

  // we're ready right away, since we don't have any args...
//  gst_element_set_state(GST_ELEMENT(fakesink),GST_STATE_READY);
}

static void
gst_fakesink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstFakeSink *sink;
  gint new_numsinks;
  GstPad *pad;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_FAKESINK (object);

  switch (prop_id) {
    case ARG_NUM_SOURCES:
      new_numsinks = g_value_get_int (value);
      while (sink->numsinkpads < new_numsinks) {
        pad = gst_pad_new (g_strdup_printf ("sink%d", sink->numsinkpads), GST_PAD_SINK);
        gst_pad_set_chain_function (pad, gst_fakesink_chain);
        gst_element_add_pad (GST_ELEMENT (sink), pad);
        sink->sinkpads = g_slist_append (sink->sinkpads, pad);
        sink->numsinkpads++;
      }
      break;
    case ARG_SILENT:
      sink->silent = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void   
gst_fakesink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstFakeSink *sink;
 
  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_FAKESINK (object));
 
  sink = GST_FAKESINK (object);
  
  switch (prop_id) {
    case ARG_NUM_SOURCES:
      g_value_set_int (value, sink->numsinkpads);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, sink->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * gst_fakesink_chain:
 * @pad: the pad this faksink is connected to
 * @buf: the buffer that has to be absorbed
 *
 * take the buffer from the pad and unref it without doing
 * anything with it.
 */
static void 
gst_fakesink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstFakeSink *fakesink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  fakesink = GST_FAKESINK (gst_pad_get_parent (pad));
  if (!fakesink->silent)
    g_print("fakesink: ******* (%s:%s)< (%d bytes) \n",GST_DEBUG_PAD_NAME(pad),GST_BUFFER_SIZE(buf));
  
  g_signal_emit (G_OBJECT (fakesink), gst_fakesink_signals[SIGNAL_HANDOFF], 0,
                   buf);

  gst_buffer_unref (buf);
}
