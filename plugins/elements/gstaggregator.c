/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstaggregator.c: Aggregator element, one in N out
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

#include "gstaggregator.h"


GstElementDetails gst_aggregator_details = {
  "Aggregator pipe fitting",
  "Aggregator",
  "N-to-1 pipe fitting",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

/* Aggregator signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_PADS,
  ARG_SILENT,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (aggregator_src_factory,
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  NULL			/* no caps */
);

static void 	gst_aggregator_class_init	(GstAggregatorClass *klass);
static void 	gst_aggregator_init		(GstAggregator *aggregator);

static GstPad* 	gst_aggregator_request_new_pad	(GstElement *element, GstPadTemplate *temp);

static void 	gst_aggregator_set_property 	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void 	gst_aggregator_get_property 	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void  	gst_aggregator_chain 		(GstPad *pad, GstBuffer *buf);

static GstElementClass *parent_class = NULL;
//static guint gst_aggregator_signals[LAST_SIGNAL] = { 0 };

GType
gst_aggregator_get_type (void) 
{
  static GType aggregator_type = 0;

  if (!aggregator_type) {
    static const GTypeInfo aggregator_info = {
      sizeof(GstAggregatorClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_aggregator_class_init,
      NULL,
      NULL,
      sizeof(GstAggregator),
      0,
      (GInstanceInitFunc)gst_aggregator_init,
    };
    aggregator_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAggregator", &aggregator_info, 0);
  }
  return aggregator_type;
}

static void
gst_aggregator_class_init (GstAggregatorClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_PADS,
    g_param_spec_int ("num_pads", "num_pads", "num_pads",
                      0, G_MAXINT, 0, G_PARAM_READABLE)); 
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_SILENT,
    g_param_spec_boolean ("silent", "silent", "silent",
                      FALSE, G_PARAM_READWRITE)); 

  gobject_class->set_property = GST_DEBUG_FUNCPTR(gst_aggregator_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR(gst_aggregator_get_property);

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR(gst_aggregator_request_new_pad);
}

static void 
gst_aggregator_init (GstAggregator *aggregator) 
{
  aggregator->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (aggregator), aggregator->srcpad);

  aggregator->numsinkpads = 0;
  aggregator->sinkpads = NULL;
  aggregator->silent = FALSE;
}

static GstPad*
gst_aggregator_request_new_pad (GstElement *element, GstPadTemplate *templ) 
{
  gchar *name;
  GstPad *sinkpad;
  GstAggregator *aggregator;

  g_return_val_if_fail (GST_IS_AGGREGATOR (element), NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("gstaggregator: request new pad that is not a SRC pad\n");
    return NULL;
  }

  aggregator = GST_AGGREGATOR (element);

  name = g_strdup_printf ("sink%d",aggregator->numsinkpads);
  
  sinkpad = gst_pad_new_from_template (templ, name);
  gst_pad_set_chain_function (sinkpad, gst_aggregator_chain);
  gst_element_add_pad (GST_ELEMENT (aggregator), sinkpad);
  
  aggregator->sinkpads = g_slist_prepend (aggregator->sinkpads, sinkpad);
  aggregator->numsinkpads++;
  
  return sinkpad;
}

static void
gst_aggregator_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstAggregator *aggregator;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGGREGATOR (object));

  aggregator = GST_AGGREGATOR (object);

  switch (prop_id) {
    case ARG_SILENT:
      aggregator->silent = g_value_get_boolean (value);
      break;
    default:
      break;
  }
}

static void
gst_aggregator_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAggregator *aggregator;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AGGREGATOR (object));

  aggregator = GST_AGGREGATOR (object);

  switch (prop_id) {
    case ARG_NUM_PADS:
      g_value_set_int (value, aggregator->numsinkpads);
      break;
    case ARG_SILENT:
      g_value_set_boolean (value, aggregator->silent);
      break;
    default:
      break;
  }
}

/**
 * gst_aggregator_chain:
 * @pad: the pad to follow
 * @buf: the buffer to pass
 *
 * Chain a buffer on a pad.
 */
static void 
gst_aggregator_chain (GstPad *pad, GstBuffer *buf) 
{
  GstAggregator *aggregator;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  aggregator = GST_AGGREGATOR (gst_pad_get_parent (pad));
  gst_trace_add_entry (NULL, 0, buf, "aggregator buffer");

  if (!aggregator->silent)
    g_print("aggregator: chain ******* (%s:%s)a (%d bytes, %llu) \n",
               GST_DEBUG_PAD_NAME (pad), GST_BUFFER_SIZE (buf), GST_BUFFER_TIMESTAMP (buf));

  gst_pad_push (aggregator->srcpad, buf);
}

gboolean
gst_aggregator_factory_init (GstElementFactory *factory)
{
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (aggregator_src_factory));

  return TRUE;
}

