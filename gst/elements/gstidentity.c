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
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_LOOP_BASED,
  ARG_SLEEP_TIME,
  ARG_SILENT,
};


static void gst_identity_class_init	(GstIdentityClass *klass);
static void gst_identity_init		(GstIdentity *identity);

static void gst_identity_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void gst_identity_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static void gst_identity_chain		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
//static guint gst_identity_signals[LAST_SIGNAL] = { 0 };

GtkType
gst_identity_get_type (void) 
{
  static GtkType identity_type = 0;

  if (!identity_type) {
    static const GtkTypeInfo identity_info = {
      "GstIdentity",
      sizeof(GstIdentity),
      sizeof(GstIdentityClass),
      (GtkClassInitFunc)gst_identity_class_init,
      (GtkObjectInitFunc)gst_identity_init,
      (GtkArgSetFunc)gst_identity_set_arg,
      (GtkArgGetFunc)gst_identity_get_arg,
      (GtkClassInitFunc)NULL,
    };
    identity_type = gtk_type_unique (GST_TYPE_ELEMENT, &identity_info);
  }
  return identity_type;
}

static void 
gst_identity_class_init (GstIdentityClass *klass) 
{
  GtkObjectClass *gtkobject_class;

  gtkobject_class = (GtkObjectClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstIdentity::loop_based", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_LOOP_BASED);
  gtk_object_add_arg_type ("GstIdentity::sleep_time", GTK_TYPE_UINT,
                           GTK_ARG_READWRITE, ARG_SLEEP_TIME);
  gtk_object_add_arg_type ("GstIdentity::silent", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_SILENT);

  gtkobject_class->set_arg = gst_identity_set_arg;  
  gtkobject_class->get_arg = gst_identity_get_arg;
}

static GstBufferPool*
gst_identity_get_bufferpool (GstPad *pad)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_get_bufferpool (identity->srcpad);
}

static GstPadNegotiateReturn
gst_identity_negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_negotiate_proxy (pad, identity->sinkpad, caps);
}

static GstPadNegotiateReturn
gst_identity_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstIdentity *identity;

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  return gst_pad_negotiate_proxy (pad, identity->srcpad, caps);
}

static void 
gst_identity_init (GstIdentity *identity) 
{
  identity->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (identity), identity->sinkpad);
  gst_pad_set_chain_function (identity->sinkpad, gst_identity_chain);
  gst_pad_set_bufferpool_function (identity->sinkpad, gst_identity_get_bufferpool);
  gst_pad_set_negotiate_function (identity->sinkpad, gst_identity_negotiate_sink);
  
  identity->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (identity), identity->srcpad);
  gst_pad_set_negotiate_function (identity->srcpad, gst_identity_negotiate_src);

  identity->loop_based = FALSE;
  identity->sleep_time = 0;
  identity->silent = FALSE;
}

static void 
gst_identity_chain (GstPad *pad, GstBuffer *buf) 
{
  GstIdentity *identity;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  identity = GST_IDENTITY (gst_pad_get_parent (pad));

  if (!identity->silent)
    g_print("identity: ******* (%s:%s)i \n",GST_DEBUG_PAD_NAME(pad));
  
  gst_pad_push (identity->srcpad, buf);

  if (identity->sleep_time)
    usleep (identity->sleep_time);
}

static void 
gst_identity_loop (GstElement *element) 
{
  GstIdentity *identity;
  GstBuffer *buf;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_IDENTITY (element));

  identity = GST_IDENTITY (element);
  
  do {
    buf = gst_pad_pull (identity->sinkpad);
    g_print("identity: ******* (%s:%s)i \n",GST_DEBUG_PAD_NAME(identity->sinkpad));

    gst_pad_push (identity->srcpad, buf);

    if (identity->sleep_time)
      usleep (identity->sleep_time);

  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
}

static void 
gst_identity_set_arg (GtkObject *object, GtkArg *arg, guint id) 
{
  GstIdentity *identity;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));
  
  identity = GST_IDENTITY (object);

  switch(id) {
    case ARG_LOOP_BASED:
      identity->loop_based = GTK_VALUE_BOOL (*arg);
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
      identity->sleep_time = GTK_VALUE_UINT (*arg);
      break;
    case ARG_SILENT:
      identity->silent = GTK_VALUE_BOOL (*arg);
      break;
    default:
      break;
  }
}

static void gst_identity_get_arg(GtkObject *object,GtkArg *arg,guint id) {
  GstIdentity *identity;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_IDENTITY (object));
  
  identity = GST_IDENTITY (object);

  switch (id) {
    case ARG_LOOP_BASED:
      GTK_VALUE_BOOL (*arg) = identity->loop_based;
      break;
    case ARG_SLEEP_TIME:
      GTK_VALUE_UINT (*arg) = identity->sleep_time;
      break;
    case ARG_SILENT:
      GTK_VALUE_BOOL (*arg) = identity->silent;
      break;
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}
