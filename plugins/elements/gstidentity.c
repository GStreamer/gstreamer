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
};


static void gst_identity_class_init	(GstIdentityClass *klass);
static void gst_identity_init		(GstIdentity *identity);

static void gst_identity_set_arg	(GtkObject *object, GtkArg *arg, guint id);
static void gst_identity_get_arg	(GtkObject *object, GtkArg *arg, guint id);

static void gst_identity_chain		(GstPad *pad, GstBuffer *buf);

static GstFilterClass *parent_class = NULL;
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
    identity_type = gtk_type_unique (GST_TYPE_FILTER, &identity_info);
  }
  return identity_type;
}

static void 
gst_identity_class_init (GstIdentityClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstFilterClass *gstfilter_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstfilter_class = (GstFilterClass*)klass;

  parent_class = gtk_type_class (GST_TYPE_FILTER);

  gtk_object_add_arg_type ("GstIdentity::loop_based", GTK_TYPE_BOOL,
                           GTK_ARG_READWRITE, ARG_LOOP_BASED);

  gtkobject_class->set_arg = gst_identity_set_arg;  
  gtkobject_class->get_arg = gst_identity_get_arg;
}

static void 
gst_identity_init (GstIdentity *identity) 
{
  identity->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (identity), identity->sinkpad);
  gst_pad_set_chain_function (identity->sinkpad, gst_identity_chain);
  
  identity->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (identity), identity->srcpad);

  identity->loop_based = FALSE;
}

static void 
gst_identity_chain (GstPad *pad, GstBuffer *buf) 
{
  GstIdentity *identity;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  identity = GST_IDENTITY (pad->parent);
  g_print("(%s:%s)i ",GST_DEBUG_PAD_NAME(pad));
  
  gst_pad_push (identity->srcpad, buf);
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
    g_print("(%s:%s)i ",GST_DEBUG_PAD_NAME(identity->sinkpad));

    gst_pad_push (identity->srcpad, buf);

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
    default:
      arg->type = GTK_TYPE_INVALID;
      break;
  }
}
