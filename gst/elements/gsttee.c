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

static GstPadTemplate*
tee_src_factory_create (void)
{
  return 
    gst_padtemplate_new (
       	"src%d",
  	GST_PAD_SRC,
  	GST_PAD_REQUEST,
  	NULL			/* no caps */
    );
}


static void 	gst_tee_class_init	(GstTeeClass *klass);
static void 	gst_tee_init		(GstTee *tee);

static GstPad* 	gst_tee_request_new_pad (GstElement *element, GstPadTemplate *temp);

static void 	gst_tee_get_property 	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void  	gst_tee_chain 		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
//static guint gst_tee_signals[LAST_SIGNAL] = { 0 };
static GstPadTemplate *gst_tee_src_template;

GType
gst_tee_get_type(void) {
  static GType tee_type = 0;

  if (!tee_type) {
    static const GTypeInfo tee_info = {
      sizeof(GstTeeClass),      NULL,
      NULL,
      (GClassInitFunc)gst_tee_class_init,
      NULL,
      NULL,
      sizeof(GstTee),
      0,
      (GInstanceInitFunc)gst_tee_init,
    };
    tee_type = g_type_register_static (GST_TYPE_ELEMENT, "GstTee", &tee_info, 0);
  }
  return tee_type;
}

static void
gst_tee_class_init (GstTeeClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUM_PADS,
    g_param_spec_int("num_pads","num_pads","num_pads",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); // CHECKME

  gobject_class->get_property = gst_tee_get_property;

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
gst_tee_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstTee *tee;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_TEE (object));

  tee = GST_TEE (object);

  switch (prop_id) {
    case ARG_NUM_PADS:
      g_value_set_int (value, tee->numsrcpads);
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
  gst_tee_src_template = tee_src_factory_create ();
  gst_elementfactory_add_padtemplate (factory, gst_tee_src_template);

  return TRUE;
}

