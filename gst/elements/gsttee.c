/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gsttee.c: Tee element, one in N out
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

#include "gsttee.h"


GstElementDetails gst_tee_details = {
  "Tee pipe fitting",
  "Tee",
  "1-to-N pipe fitting",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 1999, 2000",
};

/* Tee signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_PADS,
  /* FILL ME */
};

static GstPadFactory tee_src_factory = {
  "src%d",
  GST_PAD_FACTORY_SRC,
  GST_PAD_FACTORY_REQUEST,
  NULL,			/* no caps */
  NULL,
};


static void 	gst_tee_class_init	(GstTeeClass *klass);
static void 	gst_tee_init		(GstTee *tee);

static GstPad* 	gst_tee_request_new_pad (GstElement *element, GstPadTemplate *temp);

static void 	gst_tee_get_arg 	(GtkObject *object, GtkArg *arg, guint id);

static void  	gst_tee_chain 		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
//static guint gst_tee_signals[LAST_SIGNAL] = { 0 };
static GstPadTemplate *gst_tee_src_template;

GtkType
gst_tee_get_type(void) {
  static GtkType tee_type = 0;

  if (!tee_type) {
    static const GtkTypeInfo tee_info = {
      "GstTee",
      sizeof(GstTee),
      sizeof(GstTeeClass),
      (GtkClassInitFunc)gst_tee_class_init,
      (GtkObjectInitFunc)gst_tee_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    tee_type = gtk_type_unique (GST_TYPE_ELEMENT, &tee_info);
  }
  return tee_type;
}

static void
gst_tee_class_init (GstTeeClass *klass) 
{
  GtkObjectClass *gtkobject_class;
  GstElementClass *gstelement_class;

  gtkobject_class = (GtkObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = gtk_type_class(GST_TYPE_ELEMENT);

  gtk_object_add_arg_type ("GstTee::num_pads", GTK_TYPE_INT,
                            GTK_ARG_READABLE, ARG_NUM_PADS);

  gtkobject_class->get_arg = gst_tee_get_arg;

  gstelement_class->request_new_pad = gst_tee_request_new_pad;
}

static void 
gst_tee_init (GstTee *tee) 
{
  tee->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (tee), tee->sinkpad);
  gst_pad_set_chain_function (tee->sinkpad, gst_tee_chain);

  tee->numsrcpads = 0;
  tee->srcpads = NULL;
}

static GstPad*
gst_tee_request_new_pad (GstElement *element, GstPadTemplate *templ) 
{
  gchar *name;
  GstPad *srcpad;
  GstTee *tee;

  g_return_val_if_fail (GST_IS_TEE (element), NULL);

  if (templ->direction != GST_PAD_SRC) {
    g_warning ("gsttee: request new pad that is not a SRC pad\n");
    return NULL;
  }

  tee = GST_TEE (element);

  name = g_strdup_printf ("src%d",tee->numsrcpads);
  
  srcpad = gst_pad_new_from_template (templ, name);
  gst_element_add_pad (GST_ELEMENT (tee), srcpad);
  
  tee->srcpads = g_slist_prepend (tee->srcpads, srcpad);
  tee->numsrcpads++;
  
  return srcpad;
}

static void
gst_tee_get_arg (GtkObject *object, GtkArg *arg, guint id)
{
  GstTee *tee;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEE (object));

  tee = GST_TEE (object);

  switch(id) {
    case ARG_NUM_PADS:
      GTK_VALUE_INT (*arg) = tee->numsrcpads;
      break;
    default:
      break;
  }
}

/**
 * gst_tee_chain:
 * @pad: the pad to follow
 * @buf: the buffer to pass
 *
 * Chain a buffer on a pad.
 */
static void 
gst_tee_chain (GstPad *pad, GstBuffer *buf) 
{
  GstTee *tee;
  GSList *srcpads;
  int i;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  tee = GST_TEE (gst_pad_get_parent (pad));
  gst_trace_add_entry (NULL, 0, buf, "tee buffer");

  for (i=0; i<tee->numsrcpads-1; i++)
    gst_buffer_ref (buf);
  
  srcpads = tee->srcpads;
  while (srcpads) {
    gst_pad_push (GST_PAD (srcpads->data), buf);
    srcpads = g_slist_next (srcpads);
  }
}

gboolean
gst_tee_factory_init (GstElementFactory *factory)
{
  gst_tee_src_template = gst_padtemplate_new (&tee_src_factory);
  gst_elementfactory_add_padtemplate (factory, gst_tee_src_template);

  return TRUE;
}

