/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *                    2001 Thomas <thomas@apestaart.org>
 *
 * adder.c: Adder element, N in, one out, samples are added
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

#include "gstadder.h"

//#define DEBUG

GstElementDetails adder_details = {
  "Adder",
  "Filter/Effect",
  "2-to-1 audio adder/mixer",
  VERSION,
  "Thomas <thomas@apestaart.org>",
  "(C) 2001",
};

/* Adder signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_NUM_PADS,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (adder_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "test_src",
    "audio/raw",
      "format",             GST_PROPS_STRING ("int"),
        "law",              GST_PROPS_INT (0),
        "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
        "signed",           GST_PROPS_BOOLEAN (TRUE),
        "width",            GST_PROPS_INT_RANGE (8, 16),
        "depth",            GST_PROPS_INT_RANGE (8, 16),
        "rate",             GST_PROPS_INT_RANGE (4000, 48000), //FIXME
        "channels",         GST_PROPS_INT_RANGE (1, 2)
  )
);  

GST_PADTEMPLATE_FACTORY (adder_sink_template_factory,
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "test_sink",
    "audio/raw",
      "format",             GST_PROPS_STRING ("int"),
        "law",              GST_PROPS_INT (0),
        "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
        "signed",           GST_PROPS_BOOLEAN (TRUE),
        "width",            GST_PROPS_INT_RANGE (8, 16),
        "depth",            GST_PROPS_INT_RANGE (8, 16),
        "rate",             GST_PROPS_INT_RANGE (4000, 48000), //FIXME
        "channels",         GST_PROPS_INT_RANGE (1, 2)
  )
);  

static void 		gst_adder_class_init		(GstAdderClass *klass);
static void 		gst_adder_init			(GstAdder *adder);

static void 		gst_adder_get_property 		(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstPad* 		gst_adder_request_new_pad 	(GstElement *element, GstPadTemplate *temp,
                                                         const gchar *unused);

/* we do need a loop function */
static void 		gst_adder_loop  		(GstElement *element);

static GstElementClass *parent_class = NULL;
//static guint gst_adder_signals[LAST_SIGNAL] = { 0 };

GType
gst_adder_get_type(void) {
  static GType adder_type = 0;

  if (!adder_type) {
    static const GTypeInfo adder_info = {
      sizeof(GstAdderClass),      NULL,
      NULL,
      (GClassInitFunc)gst_adder_class_init,
      NULL,
      NULL,
      sizeof(GstAdder),
      0,
      (GInstanceInitFunc)gst_adder_init,
    };
    adder_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAdder", &adder_info, 0);
  }
  return adder_type;
}

static void
gst_adder_class_init (GstAdderClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_NUM_PADS,
    g_param_spec_int("num_pads","num_pads","num_pads",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); // CHECKME

  gobject_class->get_property = gst_adder_get_property;

  gstelement_class->request_new_pad = gst_adder_request_new_pad;
}

static void 
gst_adder_init (GstAdder *adder) 
{
  adder->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (adder), adder->srcpad);
  gst_element_set_loop_function (GST_ELEMENT (adder), gst_adder_loop);

  /* keep track of the sinkpads requested */
 
  adder->numsinkpads = 0;
  adder->input_channels = NULL;
}

static GstPad*
gst_adder_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *unused) 
{
  gchar *name;
  adder_input_channel_t *input_channel;
  GstAdder *adder;

  g_return_val_if_fail (GST_IS_ADDER (element), NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("gstadder: request new pad that is not a SINK pad\n");
    return NULL;
  }

  adder = GST_ADDER (element);

  /* allocate space for the input_channel */

  input_channel = (adder_input_channel_t *) g_malloc (sizeof (adder_input_channel_t));
  if (input_channel == NULL)
    printf ("Could not allocate memory for adder input channel !\n");
     
  /* fill in input_channel structure */

  name = g_strdup_printf ("sink%d",adder->numsinkpads);
  input_channel->sinkpad = gst_pad_new_from_template (templ, name);
  gst_element_add_pad (GST_ELEMENT (adder), input_channel->sinkpad);
  input_channel->bytes_waiting = 0;
  input_channel->p_input_data = NULL;
  input_channel->input_buffer = NULL;

  /* add the input_channel to the list of input channels */
  
  adder->input_channels = g_slist_append (adder->input_channels, input_channel);
  adder->numsinkpads++;
  
  return input_channel->sinkpad;
}

static void
gst_adder_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstAdder *adder;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_ADDER (object));

  adder = GST_ADDER (object);

  switch (prop_id) {
    case ARG_NUM_PADS:
      g_value_set_int (value, adder->numsinkpads);
      break;
    default:
      break;
  }
}


/* use this loop */
static void
gst_adder_loop (GstElement *element)
{
  /*
   * combine channels by adding sample values
   * basic algorithm :
   * - each input pipe has a bytes_waiting to it
   * - repeat for each input pipe :
   *   - if bytes_waiting is zero, request a new buffer
   *   - check which input pipe has the smalles bytes_waiting
   * - allocate an output buffer of that size
   * - repeat for each input pipe :
   *   - get this much data from each of the input channels
   *   - clear each buffer that has been depleted completely and set it's value
   *     to 0
   *   - add this to the output buffer
   * - push out the output buffer
   */
    
  GstAdder *adder;
  
  GSList *p_input_channel_GSL;
  adder_input_channel_t *p_input_channel;
  
  GstBuffer *buf_in, *buf_out;
  GstPad *sinkpad;
  
  guint i;

  guint pad_number, smallest_pad_size;

  gint16 *data_out, *data_in;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ADDER (element));
  
  adder = GST_ADDER (element);

  do 
  {

#ifdef DEBUG
    printf ("DEBUG : gst_adder_loop iteration\n");
#endif

    /* first, request all buffers that have a zero bytes_waiting value */


#ifdef DEBUG
    printf ("\tDEBUG : gst_adder_loop : getting needed input buffers\n");
#endif
    
    pad_number = 0;
    p_input_channel_GSL = adder->input_channels;
    smallest_pad_size = 0;

    while (p_input_channel_GSL)
    {
      guint16 bw;

      p_input_channel = (adder_input_channel_t *) p_input_channel_GSL->data;
      
      ++pad_number;
	  bw = p_input_channel->bytes_waiting;
	  sinkpad = p_input_channel->sinkpad;
      
#ifdef DEBUG
      printf ("\tDEBUG : gst_adder_loop : input channel %d has %d bytes left\n",
     		  pad_number, bw);
#endif  
      if (bw == 0)
      {
        /* QUESTION : maybe free previous buffer ? where do we do this ? */
        
        /* no more data left; get more */
        buf_in = gst_pad_pull (sinkpad);
        if (buf_in == NULL)
          printf ("ERROR : could not get input buffer !\n");
        bw = GST_BUFFER_SIZE (buf_in);
        p_input_channel->bytes_waiting = bw;
        p_input_channel->input_buffer = buf_in;
        p_input_channel->p_input_data = (guint16 *) GST_BUFFER_DATA (buf_in);

#ifdef DEBUG
        printf ("\tDEBUG : gst_adder_loop : input channel %d got %d new bytes\n",
        		  pad_number, bw);
#endif  
      }
      /* update smallest pad size */
      if (smallest_pad_size == 0)
      {
        /* not set yet, set it ! */
        smallest_pad_size = bw;
      }
      if (bw < smallest_pad_size) smallest_pad_size = bw;
      
      p_input_channel_GSL = g_slist_next (p_input_channel_GSL);
    }

#ifdef DEBUG
    printf ("\tDEBUG : gst_adder_loop : smallest pad size %d\n",
			smallest_pad_size);
#endif  

   /* get new output buffer */

    buf_out = gst_buffer_new ();
    if (buf_out == NULL)
      printf ("ERROR : could not get new output buffer !\n");

    GST_BUFFER_SIZE (buf_out) = smallest_pad_size;
    GST_BUFFER_DATA (buf_out) = g_malloc0 (smallest_pad_size);

    data_out = (guint16 *) GST_BUFFER_DATA (buf_out);
    if (data_out == NULL)
      printf ("ERROR : could not allocate output buffer !\n");

    /* get data from all of the sinks */

    pad_number = 0;
    p_input_channel_GSL = adder->input_channels;

    while (p_input_channel_GSL)
    {
      ++pad_number;
      p_input_channel = (adder_input_channel_t *) p_input_channel_GSL->data;
      data_out = (gint16 *) GST_BUFFER_DATA (buf_out);

      data_in = p_input_channel->p_input_data;

      /* add to the output buffer */
      for (i = 0; i < smallest_pad_size / 2; ++i, ++data_out, ++data_in)
      {
        *data_out += *data_in;
      }
      /* adjust bytes_waiting and data pointer */
      p_input_channel->bytes_waiting -= smallest_pad_size;
      if (p_input_channel->bytes_waiting == 0)
      {
#ifdef DEBUG
        printf ("\tDEBUG : gst_adder_loop : channel %d is empty, unref...\n",
			pad_number);
#endif 
        gst_buffer_unref (p_input_channel->input_buffer);
        p_input_channel->p_input_data = NULL;
      }
      p_input_channel->p_input_data = data_in;
      
      p_input_channel_GSL = g_slist_next (p_input_channel_GSL);
    }

    /* send it out */
    GST_DEBUG (0, "pushing buf_out\n");
    gst_pad_push (adder->srcpad, buf_out);
    GST_DEBUG (0, "pushed buf_out\n");

/* thomas : quick fix try */
//    GST_FLAG_SET (element, GST_ELEMENT_COTHREAD_STOPPING);
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING (element));
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("adder",GST_TYPE_ADDER,
                                   &adder_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (adder_src_template_factory));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (adder_sink_template_factory));
      
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "adder",
  plugin_init
};

