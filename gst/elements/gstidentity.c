/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstidentity.c: 
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


#include <stdlib.h>
#include <gstidentity.h>


GstElementDetails gst_identity_details = {
  "Identity",
  "Filter",
  "Pass data without modification",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* Identity signals and args */
enum {
  SIGNAL_HANDOFF,
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOOP_BASED,
  ARG_SLEEP_TIME,
  ARG_DUPLICATE,
  ARG_ERROR_AFTER,
  ARG_DROP_PROBABILITY,
  ARG_SILENT,
  ARG_LAST_MESSAGE,
  ARG_DUMP,
};


static void gst_identity_class_init	(GstIdentityClass *klass);
static void gst_identity_init		(GstIdentity *identity);

static void gst_identity_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_identity_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void gst_identity_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
static guint gst_identity_signals[LAST_SIGNAL] = { 0 };

GType
gst_identity_get_type (void) 
{
  static GType identity_type = 0;

  if (!identity_type) {
    static const GTypeInfo identity_info = {
      sizeof(GstIdentityClass),      NULL,
      NULL,
      (GClassInitFunc)gst_identity_class_init,
      NULL,
      NULL,
      sizeof(GstIdentity),
      0,
      (GInstanceInitFunc)gst_identity_init,
    };
    identity_type = g_type_register_static (GST_TYPE_ELEMENT, "GstIdentity", &identity_info, 0);
  }
  return identity_type;
}

static void 
gst_identity_class_init (GstIdentityClass *klass) 
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOOP_BASED,
    g_param_spec_boolean ("loop_based", "loop_based", "loop_based",
                          TRUE, G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SLEEP_TIME,
    g_param_spec_uint ("sleep_time", "sleep_time", "sleep_time",
                       0, G_MAXUINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DUPLICATE,
    g_param_spec_uint ("duplicate", "Duplicate Buffers", "Push the buffers N times",
                       0, G_MAXUINT, 1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ERROR_AFTER,
    g_param_spec_int ("error_after", "Error After", "Error after N buffers",
                       G_MININT, G_MAXINT, -1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DROP_PROBABILITY,
    g_param_spec_float ("drop_probability", "Drop Probability", "The Probability a buffer is dropped",
                        0.0, 1.0, 0.0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
    g_param_spec_boolean ("silent", "silent", "silent",
                          TRUE,G_PARAM_READWRITE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
    g_param_spec_string ("last-message", "last-message", "last-message",
                         NULL, G_PARAM_READABLE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DUMP,
    g_param_spec_boolean("dump","Dump","Dump buffer contents",
                         FALSE, G_PARAM_READWRITE));

  gst_identity_signals[SIGNAL_HANDOFF] =
    g_signal_new ("handoff", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET (GstIdentityClass, handoff), NULL, NULL,
                   g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1,
                   G_TYPE_POINTER);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_identity_set_property);  
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_identity_get_property);
}

static GstBufferPool*
gst_identity_get_bufferpool (GstPad *pad)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_get_bufferpool (identity->srcpad);
}

static GstCaps*
gst_identity_getcaps (GstPad *pad, GstCaps *caps)
{
  GstIdentity *identity;
  GstPad *otherpad;
  
  identity = GST_IDENTITY (gst_pad_get_parent (pad));
  otherpad = (pad == identity->srcpad ? identity->sinkpad : identity->srcpad);

  return gst_pad_get_allowed_caps (otherpad);
}

static GstPadConnectReturn
gst_identity_connect (GstPad *pad, GstCaps *caps)
{
  GstIdentity *identity;
  GstPad *otherpad;
  
  identity = GST_IDENTITY (gst_pad_get_parent (pad));
  otherpad = (pad == identity->srcpad ? identity->sinkpad : identity->srcpad);

  if (GST_CAPS_IS_FIXED (caps))
    if (gst_pad_try_set_caps (otherpad, caps))
      return GST_PAD_CONNECT_OK;
    else
      return GST_PAD_CONNECT_REFUSED;
  else
    return GST_PAD_CONNECT_DELAYED;
}

static void 
gst_identity_init (GstIdentity *identity) 
{
  identity->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (identity), identity->sinkpad);
  gst_pad_set_chain_function (identity->sinkpad, GST_DEBUG_FUNCPTR (gst_identity_chain));
  gst_pad_set_bufferpool_function (identity->sinkpad, gst_identity_get_bufferpool);
  gst_pad_set_connect_function (identity->sinkpad, gst_identity_connect);
  gst_pad_set_getcaps_function (identity->sinkpad, gst_identity_getcaps);
  
  identity->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (identity), identity->srcpad);
  gst_pad_set_connect_function (identity->srcpad, gst_identity_connect);
  gst_pad_set_getcaps_function (identity->srcpad, gst_identity_getcaps);

  identity->loop_based = FALSE;
  identity->sleep_time = 0;
  identity->duplicate = 1;
  identity->error_after = -1;
  identity->drop_probability = 0.0;
  identity->silent = FALSE;
  identity->dump = FALSE;
}

static void 
gst_identity_chain (GstPad *pad, GstBuffer *buf) 
{
  GstIdentity *identity;
  guint i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  if (identity->error_after >= 0) {
    identity->error_after--;
    if (identity->error_after == 0) {
      gst_buffer_unref (buf);
      gst_element_error (GST_ELEMENT (identity), "errored after iterations as requested");
      return;
    }
  }

  if (identity->drop_probability > 0.0) {
    if ((gfloat)(1.0*rand()/(RAND_MAX)) < identity->drop_probability) {
      identity->last_message = g_strdup_printf ("dropping   ******* (%s:%s)i (%d bytes, %llu)",
	      GST_DEBUG_PAD_NAME (identity->sinkpad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));
      g_object_notify (G_OBJECT (identity), "last-message");
      gst_buffer_unref (buf);
      return;
    }
  }
  if (identity->dump) {
    gst_util_dump_mem (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  }

  for (i = identity->duplicate; i; i--) {
    if (!identity->silent)
      identity->last_message = g_strdup_printf ("chain   ******* (%s:%s)i (%d bytes, %llu)",
	      GST_DEBUG_PAD_NAME (identity->sinkpad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));
    g_object_notify (G_OBJECT (identity), "last-message");
    g_signal_emit (G_OBJECT (identity), gst_identity_signals[SIGNAL_HANDOFF], 0,
	                       buf);

    if (i>1) 
      gst_buffer_ref (buf);

    gst_pad_push (identity->srcpad, buf);

    if (identity->sleep_time)
      usleep (identity->sleep_time);
  }
}

static void 
gst_identity_loop (GstElement *element) 
{
  GstIdentity *identity;
  GstBuffer *buf;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_IDENTITY (element));

  identity = GST_IDENTITY (element);
  
  buf = gst_pad_pull (identity->sinkpad);
  if (GST_IS_EVENT (buf)) {
    gst_pad_event_default (identity->sinkpad, GST_EVENT (buf));
  }

  gst_identity_chain (identity->sinkpad, buf);
}

static void 
gst_identity_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstIdentity *identity;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));
  
  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case ARG_LOOP_BASED:
      identity->loop_based = g_value_get_boolean (value);
      if (identity->loop_based) {
        gst_element_set_loop_function (GST_ELEMENT (identity), gst_identity_loop);
        gst_pad_set_chain_function (identity->sinkpad, NULL);
      }
      else {
        gst_pad_set_chain_function (identity->sinkpad, gst_identity_chain);
        gst_element_set_loop_function (GST_ELEMENT (identity), NULL);
      }
      break;
    case ARG_SLEEP_TIME:
      identity->sleep_time = g_value_get_uint (value);
      break;
    case ARG_SILENT:
      identity->silent = g_value_get_boolean (value);
      break;
    case ARG_DUPLICATE:
      identity->duplicate = g_value_get_uint (value);
      break;
    case ARG_DUMP:
      identity->dump = g_value_get_boolean (value);
      break;
    case ARG_ERROR_AFTER:
      identity->error_after = g_value_get_uint (value);
      break;
    case ARG_DROP_PROBABILITY:
      identity->drop_probability = g_value_get_float (value);
      break;
    default:
      break;
  }
}

static void gst_identity_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) {
  GstIdentity *identity;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));
  
  identity = GST_IDENTITY (object);

  switch (prop_id) {
    case ARG_LOOP_BASED:
      g_value_set_boolean (value, identity->loop_based);
      break;
    case ARG_SLEEP_TIME:
      g_value_set_uint (value, identity->sleep_time);
      break;
    case ARG_DUPLICATE:
      g_value_set_uint (value, identity->duplicate);
      break;
    case ARG_ERROR_AFTER:
      g_value_set_uint (value, identity->error_after);
      break;
    case ARG_DROP_PROBABILITY:
      g_value_set_float (value, identity->drop_probability);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, identity->silent);
      break;
    case ARG_DUMP:
      g_value_set_boolean (value, identity->dump);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, identity->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
