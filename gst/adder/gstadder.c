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
#include <string.h> 		/* strcmp */

#define GST_ADDER_BUFFER_SIZE 4096
#define GST_ADDER_NUM_BUFFERS 8

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

GST_PAD_TEMPLATE_FACTORY (gst_adder_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "int_src",
    "audio/raw",
      "format",             GST_PROPS_STRING ("int"),
        "law",              GST_PROPS_INT (0),
        "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
        "signed",           GST_PROPS_BOOLEAN (TRUE),
        "width",            GST_PROPS_LIST (GST_PROPS_INT (8), GST_PROPS_INT (16)),
        "depth",            GST_PROPS_LIST (GST_PROPS_INT (8), GST_PROPS_INT (16)),
        "rate",             GST_PROPS_INT_RANGE (4000, 48000), /* FIXME */
        "channels",         GST_PROPS_INT_RANGE (1, 2)
  ),
  GST_CAPS_NEW (
    "float_src",
    "audio/raw",
      "format",             GST_PROPS_STRING("float"),
        "layout",           GST_PROPS_STRING ("gfloat"),
        "intercept",        GST_PROPS_FLOAT (0.0),
        "slope",            GST_PROPS_FLOAT (1.0),
        "rate",             GST_PROPS_INT_RANGE (4000, 96000),
        "channels",         GST_PROPS_INT_RANGE (1, 2)
  )
);  

GST_PAD_TEMPLATE_FACTORY (gst_adder_sink_template_factory,
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "int_sink",
    "audio/raw",
      "format",             GST_PROPS_STRING ("int"),
        "law",              GST_PROPS_INT (0),
        "endianness",       GST_PROPS_INT (G_BYTE_ORDER),
        "signed",           GST_PROPS_BOOLEAN (TRUE),
        "width",            GST_PROPS_LIST (GST_PROPS_INT (8), GST_PROPS_INT (16)),
        "depth",            GST_PROPS_LIST (GST_PROPS_INT (8), GST_PROPS_INT (16)),
        "rate",             GST_PROPS_INT_RANGE (4000, 48000), /* FIXME */
        "channels",         GST_PROPS_INT_RANGE (1, 2)
  ),
  GST_CAPS_NEW (
    "float_sink",
    "audio/raw",
      "format",             GST_PROPS_STRING("float"),
        "layout",           GST_PROPS_STRING ("gfloat"),
        "intercept",        GST_PROPS_FLOAT (0.0),
        "slope",            GST_PROPS_FLOAT (1.0),
        "rate",             GST_PROPS_INT_RANGE (4000, 96000),
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
/* static guint gst_adder_signals[LAST_SIGNAL] = { 0 }; */

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

static gboolean
gst_adder_parse_caps (GstAdder *adder, GstCaps *caps)
{
  const gchar *format;
  
  gst_caps_get_string (caps, "format", &format);

  if (adder->format == GST_ADDER_FORMAT_UNSET) {
    /* the caps haven't been set yet at all, so we need to go ahead and set all
       the relevant values. */
    if (strcmp (format, "int") == 0) {
      adder->format     = GST_ADDER_FORMAT_INT;
      gst_caps_get_int     (caps, "width",      &adder->width);
      gst_caps_get_int     (caps, "depth",      &adder->depth);
      gst_caps_get_int     (caps, "law",        &adder->law);
      gst_caps_get_int     (caps, "endianness", &adder->endianness);
      gst_caps_get_boolean (caps, "signed",     &adder->is_signed);
      gst_caps_get_int     (caps, "channels",   &adder->channels);
    } else if (strcmp (format, "float") == 0) {
      adder->format     = GST_ADDER_FORMAT_FLOAT;
      gst_caps_get_string  (caps, "layout",    &adder->layout);
      gst_caps_get_float   (caps, "intercept", &adder->intercept);
      gst_caps_get_float   (caps, "slope",     &adder->slope);
      gst_caps_get_int     (caps, "channels",  &adder->channels);
    }
  } else {
    /* otherwise, a previously-connected pad has set all the values. we should
       barf if some of the attempted new values don't match. */
    if (strcmp (format, "int") == 0) {
      gint width, channels;
      gboolean is_signed;

      gst_caps_get_int     (caps, "width",     &width);
      gst_caps_get_int     (caps, "channels",  &channels);
      gst_caps_get_boolean (caps, "signed",    &is_signed);

      if ((adder->format != GST_ADDER_FORMAT_INT) ||
          (adder->width  != width) ||
          (adder->channels != channels) ||
          (adder->is_signed != is_signed)) {
        return FALSE;
      }
    } else if (strcmp (format, "float") == 0) {
      gint channels;

      gst_caps_get_int     (caps, "channels",  &channels);

      if ((adder->format != GST_ADDER_FORMAT_FLOAT) ||
          (adder->channels != channels)) {
        return FALSE;
      }
    } else {
      /* whoa, we don't know what's trying to connect with us ! barf ! */
      return FALSE;
    }
  }
  return TRUE;
}

static GstPadConnectReturn
gst_adder_connect (GstPad *pad, GstCaps *caps)
{
  GstAdder *adder;
  GList *sinkpads, *remove = NULL;
  GSList *channels;
  GstPad *p;
  
  g_return_val_if_fail (caps != NULL, GST_PAD_CONNECT_REFUSED);
  g_return_val_if_fail (pad  != NULL, GST_PAD_CONNECT_REFUSED);

  adder = GST_ADDER (GST_PAD_PARENT (pad));

  if (GST_CAPS_IS_FIXED (caps)) {
    if (!gst_adder_parse_caps (adder, caps))
      return GST_PAD_CONNECT_REFUSED;
  
    if (pad == adder->srcpad || gst_pad_try_set_caps (adder->srcpad, caps)) {
      sinkpads = gst_element_get_pad_list ((GstElement*) adder);
      while (sinkpads) {
        p = (GstPad*) sinkpads->data;
        if (p != pad && p != adder->srcpad) {
          if (!gst_pad_try_set_caps (p, caps)) {
            GST_DEBUG (0, "caps mismatch; disconnecting and removing pad %s:%s (peer %s:%s)",
                       GST_DEBUG_PAD_NAME (p), GST_DEBUG_PAD_NAME (GST_PAD_PEER (p)));
            gst_pad_disconnect (GST_PAD (GST_PAD_PEER (p)), p);
            remove = g_list_prepend (remove, p);
          }
        }
        sinkpads = g_list_next (sinkpads);
      }
      while (remove) {
        gst_element_remove_pad (GST_ELEMENT (adder), GST_PAD_CAST (remove->data));
      restart:
        channels = adder->input_channels;
        while (channels) {
          GstAdderInputChannel *channel = (GstAdderInputChannel*) channels->data;
          if (channel->sinkpad == GST_PAD_CAST (remove->data)) {
            gst_bytestream_destroy (channel->bytestream);
            adder->input_channels = g_slist_remove_link (adder->input_channels, channels);
            adder->numsinkpads--;
            goto restart;
          }
          channels = g_slist_next (channels);
        }
        remove = g_list_next (remove);
      }
      return GST_PAD_CONNECT_OK;
    } else {
      return GST_PAD_CONNECT_REFUSED;
    }
  } else {
    return GST_PAD_CONNECT_DELAYED;
  }
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
                     G_MININT, G_MAXINT, 0, G_PARAM_READABLE));

  gobject_class->get_property = gst_adder_get_property;

  gstelement_class->request_new_pad = gst_adder_request_new_pad;
}

static void 
gst_adder_init (GstAdder *adder) 
{
  adder->srcpad = gst_pad_new_from_template (gst_adder_src_template_factory (), "src");
  gst_element_add_pad (GST_ELEMENT (adder), adder->srcpad);
  gst_element_set_loop_function (GST_ELEMENT (adder), gst_adder_loop);
  gst_pad_set_connect_function (adder->srcpad, gst_adder_connect);

  adder->format = GST_ADDER_FORMAT_UNSET;

  /* keep track of the sinkpads requested */
 
  adder->numsinkpads = 0;
  adder->input_channels = NULL;
}

static GstPad*
gst_adder_request_new_pad (GstElement *element, GstPadTemplate *templ, const gchar *unused) 
{
  gchar                *name;
  GstAdder             *adder;
  GstAdderInputChannel *input;

  g_return_val_if_fail (GST_IS_ADDER (element), NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("gstadder: request new pad that is not a SINK pad\n");
    return NULL;
  }

  /* allocate space for the input_channel */

  input = (GstAdderInputChannel *) g_malloc (sizeof (GstAdderInputChannel));
  if (input == NULL) {
    g_warning ("gstadder: could not allocate memory for adder input channel !\n");
    return NULL;
  }
  
  adder = GST_ADDER (element);

  /* fill in input_channel structure */

  name = g_strdup_printf ("sink%d", adder->numsinkpads);
  input->sinkpad = gst_pad_new_from_template (templ, name);
  input->bytestream = gst_bytestream_new (input->sinkpad);

  gst_element_add_pad (GST_ELEMENT (adder), input->sinkpad);
  gst_pad_set_connect_function(input->sinkpad, gst_adder_connect);

  /* add the input_channel to the list of input channels */
  
  adder->input_channels = g_slist_append (adder->input_channels, input);
  adder->numsinkpads++;
  
  return input->sinkpad;
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
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
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
   * - request an output buffer from the pool
   * - repeat for each input pipe :
   *   - get number of bytes from the channel's bytestream to fill output buffer
   *   - if there's an EOS event, remove the input channel
   *   - otherwise add the gotten bytes to the output buffer
   * - push out the output buffer
   */
    
  GstAdder  *adder;
  GstBuffer *buf_out;
  GstEvent  *event = NULL;

  GSList               *inputs;
  GstAdderInputChannel *input;

  gint8     *raw_in, *zero_out;
  guint32    waiting;
  register guint i;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ADDER (element));
  
  adder = GST_ADDER (element);
  adder->bufpool = gst_pad_get_bufferpool (adder->srcpad);
  if (adder->bufpool == NULL) {
    adder->bufpool = gst_buffer_pool_get_default(GST_ADDER_BUFFER_SIZE, GST_ADDER_NUM_BUFFERS);
  }
  
  do {
    /* get new output buffer */
    buf_out = gst_buffer_new_from_pool (adder->bufpool, 0, 0);
  
    if (buf_out == NULL)
      GST_ERROR (0, "could not get new output buffer !\n");

    /* initialize the output data to 0 */
    zero_out = (gint8 *) GST_BUFFER_DATA (buf_out);      
    for (i = 0; i < GST_BUFFER_SIZE (buf_out); i++)
      zero_out[i] = 0;

    /* get data from all of the sinks */
    inputs = adder->input_channels;

    GST_DEBUG (0, "starting to cycle through channels");

    while (inputs) {
      input = (GstAdderInputChannel *) inputs->data;

      GST_DEBUG (0, "looking into channel %p", input);
      
      /* get data from the bytestream of each input channel. we need to check for
         events before passing on the data to the output buffer. */
      raw_in = gst_bytestream_peek_bytes (input->bytestream, GST_BUFFER_SIZE (buf_out));

      if (raw_in == NULL) {
        /* we need to check for an event. */
        gst_bytestream_get_status (input->bytestream, &waiting, &event);

        if (event) {
          if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
            /* if we get an EOS event from one of our sink pads, we assume that
               pad's finished handling data. delete the bytestream, free up the
               pad, and free up the memory associated with the input channel. */
            GST_DEBUG (0, "got an EOS event");

            gst_bytestream_destroy (input->bytestream);
            /* gst_object_unref (GST_OBJECT (input->sinkpad)); this causes problems */
            g_free (input);

            adder->input_channels = g_slist_delete_link (inputs, inputs);
            inputs = adder->input_channels;

            break;
          }
        }
      } else {
        /* here's where the data gets copied. this is a little nasty looking
           because it's the same code pretty much 3 times, except each time uses
           different data types and clamp limits. */
        GST_DEBUG (0, "copying %d bytes from channel %p to output data %p in buffer %p",
                   GST_BUFFER_SIZE (buf_out), input, GST_BUFFER_DATA (buf_out), buf_out);

        if (adder->format == GST_ADDER_FORMAT_INT) {
          if (adder->width == 16) {
            gint16 *in  = (gint16 *) raw_in;
            gint16 *out = (gint16 *) GST_BUFFER_DATA (buf_out);      
            for (i = 0; i < GST_BUFFER_SIZE (buf_out) / 2; i++)
              out[i] = CLAMP(out[i] + in[i], -32768, 32767);
          } else if (adder->width == 8) {
            gint8 *in  = (gint8 *) raw_in;
            gint8 *out = (gint8 *) GST_BUFFER_DATA (buf_out);      
            for (i = 0; i < GST_BUFFER_SIZE (buf_out); i++)
              out[i] = CLAMP(out[i] + in[i], -128, 127);
          } else {
            GST_ERROR (0, "invalid width (%d) for int format in gstadder\n", adder->width);
          }
        } else if (adder->format == GST_ADDER_FORMAT_FLOAT) {
          gfloat *in  = (gfloat *) raw_in;
          gfloat *out = (gfloat *) GST_BUFFER_DATA (buf_out);
          for (i = 0; i < GST_BUFFER_SIZE (buf_out) / sizeof (gfloat); i++)
            out[i] += in[i];
        } else {
          GST_ERROR (0, "invalid audio format (%d) in gstadder\n", adder->format);
        }

        gst_bytestream_flush (input->bytestream, GST_BUFFER_SIZE (buf_out));

        GST_DEBUG (0, "done copying data");
      }
      
      inputs = g_slist_next (inputs);
    }

    /* send it out */

    GST_DEBUG (0, "pushing buf_out");
    gst_pad_push (adder->srcpad, buf_out);

  } while (TRUE);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new("adder",GST_TYPE_ADDER,
                                   &adder_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  if (! gst_library_load ("gstbytestream")) {
    gst_info ("gstadder: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }
    
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_adder_src_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (gst_adder_sink_template_factory));
      
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "adder",
  plugin_init
};

