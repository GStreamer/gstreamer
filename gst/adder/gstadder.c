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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstadder.h"
#include <gst/audio/audio.h>
#include <string.h> 		/* strcmp */

#define GST_ADDER_BUFFER_SIZE 4096
#define GST_ADDER_NUM_BUFFERS 8

/* elementfactory information */
static GstElementDetails adder_details = GST_ELEMENT_DETAILS (
  "Adder",
  "Generic/Audio",
  "Add N audio channels together",
  "Thomas <thomas@apestaart.org>"
);

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

static GstStaticPadTemplate gst_adder_src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS(
    GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
    GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS
  )
);

static GstStaticPadTemplate gst_adder_sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_STATIC_CAPS(
    GST_AUDIO_INT_PAD_TEMPLATE_CAPS "; "
    GST_AUDIO_FLOAT_STANDARD_PAD_TEMPLATE_CAPS
  )
);

static void 		gst_adder_class_init		(GstAdderClass *klass);
static void 		gst_adder_init			(GstAdder *adder);

static void 		gst_adder_get_property 		(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

static GstPad* 		gst_adder_request_new_pad 	(GstElement *element, GstPadTemplate *temp,
                                                         const gchar *unused);
static GstElementStateReturn
			gst_adder_change_state 		(GstElement *element);

/* we do need a loop function */
static void 		gst_adder_loop  		(GstElement *element);

static GstElementClass *parent_class = NULL;
/* static guint gst_adder_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_adder_get_type (void) {
  static GType adder_type = 0;

  if (!adder_type) {
    static const GTypeInfo adder_info = {
      sizeof (GstAdderClass), NULL, NULL,
      (GClassInitFunc) gst_adder_class_init, NULL, NULL,
      sizeof (GstAdder), 0,
      (GInstanceInitFunc) gst_adder_init,
    };
    adder_type = g_type_register_static (GST_TYPE_ELEMENT, "GstAdder", 
	                                 &adder_info, 0);
  }
  return adder_type;
}

static gboolean
gst_adder_parse_caps (GstAdder *adder, GstStructure *structure)
{
  const gchar *mimetype;
  GstElement *el = GST_ELEMENT (adder);

  mimetype = gst_structure_get_name (structure);

  if (adder->format == GST_ADDER_FORMAT_UNSET) {
    /* the caps haven't been set yet at all, so we need to go ahead and set all
       the relevant values. */
    if (strcmp (mimetype, "audio/x-raw-int") == 0) {
      GST_DEBUG ("parse_caps sets adder to format int");
      adder->format     = GST_ADDER_FORMAT_INT;
      gst_structure_get_int     (structure, "width",      &adder->width);
      gst_structure_get_int     (structure, "depth",      &adder->depth);
      gst_structure_get_int     (structure, "endianness", &adder->endianness);
      gst_structure_get_boolean (structure, "signed",     &adder->is_signed);
      gst_structure_get_int     (structure, "channels",   &adder->channels);
      gst_structure_get_int     (structure, "rate",	  &adder->rate);
    } else if (strcmp (mimetype, "audio/x-raw-float") == 0) {
      GST_DEBUG ("parse_caps sets adder to format float");
      adder->format     = GST_ADDER_FORMAT_FLOAT;
      gst_structure_get_int     (structure, "width",     &adder->width);
      gst_structure_get_int     (structure, "channels",  &adder->channels);
      gst_structure_get_int     (structure, "rate",      &adder->rate);
    }
  } else {
    /* otherwise, a previously-linked pad has set all the values. we should barf
       if some of the attempted new values don't match. */
    if (strcmp (mimetype, "audio/x-raw-int") == 0) {
      gint width, channels, rate;
      gboolean is_signed;

      gst_structure_get_int     (structure, "width",     &width);
      gst_structure_get_int     (structure, "channels",  &channels);
      gst_structure_get_boolean (structure, "signed",    &is_signed);
      gst_structure_get_int     (structure, "rate",      &rate);

      /* provide an error message if we can't link */
      if (adder->format != GST_ADDER_FORMAT_INT) {
        gst_element_error (el, CORE, NEGOTIATION, NULL, ("can't link a non-int pad to an int adder"));
        return FALSE;
      }
      if (adder->channels != channels) {
        gst_element_error (el, CORE, NEGOTIATION, NULL,
                           ("can't link %d-channel pad with %d-channel adder",
                           channels, adder->channels));
       return FALSE;
      }
      if (adder->rate != rate) {
        gst_element_error (el, CORE, NEGOTIATION, NULL, ("can't link %d Hz pad with %d Hz adder",
                           rate, adder->rate));
       return FALSE;
      }
      if (adder->width != width) {
        gst_element_error (el, CORE, NEGOTIATION, NULL, ("can't link %d-bit pad with %d-bit adder",
                           width, adder->width));
       return FALSE;
      }
      if (adder->is_signed != is_signed) {
        gst_element_error (el, CORE, NEGOTIATION, NULL, ("can't link %ssigned pad with %ssigned adder",
                           adder->is_signed ? "" : "un",
                           is_signed ? "" : "un"));
       return FALSE;
      }
    } else if (strcmp (mimetype, "audio/x-raw-float") == 0) {
      gint channels, rate, width;

      gst_structure_get_int (structure, "width",     &width);
      gst_structure_get_int (structure, "channels",  &channels);
      gst_structure_get_int (structure, "rate",      &rate);

      if (adder->format != GST_ADDER_FORMAT_FLOAT) {
        gst_element_error (el, CORE, NEGOTIATION, NULL, ("can't link a non-float pad to a float adder"));
        return FALSE;
      }
      if (adder->channels != channels) {
        gst_element_error (el, CORE, NEGOTIATION, NULL,
                           ("can't link %d-channel pad with %d-channel adder",
                           channels, adder->channels));
        return FALSE;
      }
      if (adder->rate != rate) {
        gst_element_error (el, CORE, NEGOTIATION, NULL, ("can't link %d Hz pad with %d Hz adder",
                           rate, adder->rate));
        return FALSE;
      }
      if (adder->width != width) {
        gst_element_error (el, CORE, NEGOTIATION, NULL, ("can't link %d bit float pad with %d bit adder",
                           width, adder->width));
        return FALSE;
      }
    }
  }
  return TRUE;
}

static GstPadLinkReturn
gst_adder_link (GstPad *pad, const GstCaps *caps)
{
  GstAdder *adder;
  GstElement *element;
  const GList *sinkpads;
  GList *remove = NULL;
  GSList *channels;
  GstPad *p;
  const GList *pads;
  GstPadLinkReturn ret;

  g_return_val_if_fail (caps != NULL, GST_PAD_LINK_REFUSED);
  g_return_val_if_fail (pad  != NULL, GST_PAD_LINK_REFUSED);

  element = GST_PAD_PARENT (pad);
  adder = GST_ADDER (element);

  pads = gst_element_get_pad_list (element);
  while (pads) {
    GstPad *otherpad = GST_PAD (pads->data);

    if (otherpad != pad) {
      ret = gst_pad_try_set_caps (otherpad, caps);
      if (GST_PAD_LINK_FAILED (ret)) {
        return ret;
      }
    }
    pads = g_list_next (pads);
  }


  if (!gst_adder_parse_caps (adder, gst_caps_get_structure (caps, 0)))
    return GST_PAD_LINK_REFUSED;

  if (pad == adder->srcpad || gst_pad_try_set_caps (adder->srcpad, caps) > 0) {
    sinkpads = gst_element_get_pad_list ((GstElement *) adder);
    while (sinkpads) {
      p = (GstPad *) sinkpads->data;
      if (p != pad && p != adder->srcpad) {
	if (gst_pad_try_set_caps (p, caps) <= 0) {
	  GST_DEBUG ("caps mismatch; unlinking and removing pad %s:%s "
		     "(peer %s:%s)",
		     GST_DEBUG_PAD_NAME (p),
		     GST_DEBUG_PAD_NAME (GST_PAD_PEER (p)));
	  gst_pad_unlink (GST_PAD (GST_PAD_PEER (p)), p);
	  remove = g_list_prepend (remove, p);
	}
      }
      while (remove) {
        gst_element_remove_pad (GST_ELEMENT (adder),
                                GST_PAD (remove->data));
      restart:
        channels = adder->input_channels;
        while (channels) {
          GstAdderInputChannel *channel;
	  channel = (GstAdderInputChannel*) channels->data;
          if (channel->sinkpad == GST_PAD (remove->data)) {
            gst_bytestream_destroy (channel->bytestream);
            adder->input_channels =
              g_slist_remove_link (adder->input_channels, channels);
            adder->numsinkpads--;
            goto restart;
          }
          channels = g_slist_next (channels);
        }
        remove = g_list_next (remove);
      }
      remove = g_list_next (remove);
    }
    return GST_PAD_LINK_OK;
  } else {
    return GST_PAD_LINK_REFUSED;
  }
}

static void
gst_adder_class_init (GstAdderClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_adder_sink_template));
  gst_element_class_set_details (gstelement_class, &adder_details);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_NUM_PADS,
    g_param_spec_int ("num_pads","number of pads","Number Of Pads",
                     0, G_MAXINT, 0, G_PARAM_READABLE));

  gobject_class->get_property = gst_adder_get_property;

  gstelement_class->request_new_pad = gst_adder_request_new_pad;
  gstelement_class->change_state    = gst_adder_change_state;
}

static void 
gst_adder_init (GstAdder *adder) 
{
  adder->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_adder_src_template), "src");
  gst_element_add_pad (GST_ELEMENT (adder), adder->srcpad);
  gst_element_set_loop_function (GST_ELEMENT (adder), gst_adder_loop);
  gst_pad_set_getcaps_function (adder->srcpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function (adder->srcpad, gst_adder_link);

  adder->format = GST_ADDER_FORMAT_UNSET;

  /* keep track of the sinkpads requested */
 
  adder->numsinkpads = 0;
  adder->input_channels = NULL;
}

static GstPad*
gst_adder_request_new_pad (GstElement *element, GstPadTemplate *templ, 
                           const gchar *unused) 
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
    g_warning ("gstadder: could not allocate adder input channel !\n");
    return NULL;
  }
  
  adder = GST_ADDER (element);

  /* fill in input_channel structure */

  name = g_strdup_printf ("sink%d", adder->numsinkpads);
  input->sinkpad = gst_pad_new_from_template (templ, name);
  input->bytestream = gst_bytestream_new (input->sinkpad);

  gst_element_add_pad (GST_ELEMENT (adder), input->sinkpad);
  gst_pad_set_getcaps_function (input->sinkpad, gst_pad_proxy_getcaps);
  gst_pad_set_link_function(input->sinkpad, gst_adder_link);

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

  GSList    *inputs;

  register guint i;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ADDER (element));

  adder = GST_ADDER (element);

  /* get new output buffer */
  /* FIXME the 1024 is arbitrary */
  buf_out = gst_buffer_new_and_alloc (1024);

  if (buf_out == NULL) {
    gst_element_error (adder, CORE, TOO_LAZY, NULL, ("could not get new output buffer"));
    return;
  }

  /* initialize the output data to 0 */
  memset (GST_BUFFER_DATA (buf_out), 0, GST_BUFFER_SIZE (buf_out));

  /* get data from all of the sinks */
  inputs = adder->input_channels;

  GST_DEBUG ("starting to cycle through channels");

  while (inputs) {
    guint32 got_bytes;
    guint8 *raw_in;
    GstAdderInputChannel *input;

    input = (GstAdderInputChannel *) inputs->data;

    inputs = inputs->next;

    GST_DEBUG ("looking into channel %p", input);

    if (!GST_PAD_IS_USABLE (input->sinkpad)) {
      GST_DEBUG ("adder ignoring pad %s:%s",
	         GST_DEBUG_PAD_NAME (input->sinkpad));
      continue;
    }

    /* Get data from the bytestream of each input channel. We need to check for
       events before passing on the data to the output buffer. */
    got_bytes = gst_bytestream_peek_bytes (input->bytestream, &raw_in,
	                                     GST_BUFFER_SIZE (buf_out));

    /* FIXME we should do something with the data if got_bytes > 0 */
    if (got_bytes < GST_BUFFER_SIZE(buf_out)) {
      GstEvent  *event = NULL;
      guint32    waiting;

      /* we need to check for an event. */
      gst_bytestream_get_status (input->bytestream, &waiting, &event);

      if (event) {
	switch (GST_EVENT_TYPE (event)) {
	  case GST_EVENT_EOS:
            /* if we get an EOS event from one of our sink pads, we assume that
               pad's finished handling data. just skip this pad. */
            GST_DEBUG ("got an EOS event");
	    gst_event_unref (event);
            continue;
	  case GST_EVENT_INTERRUPT:
	    gst_event_unref (event);
            GST_DEBUG ("got an interrupt event");
	    /* we have to call interrupt here, the scheduler will switch out
	       this element ASAP or returns TRUE if we need to exit the loop */
	    if (gst_element_interrupt (GST_ELEMENT (adder))) {
	      gst_buffer_unref (buf_out);
	      return;
	    }
	  default:
	    break;
        }
      }
    } else {
      /* here's where the data gets copied. */

      GST_DEBUG ("copying %d bytes from channel %p to output data %p "
	         "in buffer %p",
                 GST_BUFFER_SIZE (buf_out), input,
		 GST_BUFFER_DATA (buf_out), buf_out);

      if (adder->format == GST_ADDER_FORMAT_INT) {
        if (adder->width == 32) {
          gint32 *in  = (gint32 *) raw_in;
          gint32 *out = (gint32 *) GST_BUFFER_DATA (buf_out);
          for (i = 0; i < GST_BUFFER_SIZE (buf_out) / 4; i++)
            out[i] = CLAMP(out[i] + in[i], 0x80000000, 0x7fffffff);
        } else if (adder->width == 16) {
          gint16 *in  = (gint16 *) raw_in;
          gint16 *out = (gint16 *) GST_BUFFER_DATA (buf_out);
          for (i = 0; i < GST_BUFFER_SIZE (buf_out) / 2; i++)
            out[i] = CLAMP(out[i] + in[i], 0x8000, 0x7fff);
        } else if (adder->width == 8) {
          gint8 *in  = (gint8 *) raw_in;
          gint8 *out = (gint8 *) GST_BUFFER_DATA (buf_out);
          for (i = 0; i < GST_BUFFER_SIZE (buf_out); i++)
            out[i] = CLAMP(out[i] + in[i], 0x80, 0x7f);
        } else {
          gst_element_error (adder, STREAM, FORMAT, NULL,
		     ("invalid width (%u) for integer audio in gstadder",
		     adder->width));
	  return;
        }
      } else if (adder->format == GST_ADDER_FORMAT_FLOAT) {
        if (adder->width == 64) {
          gdouble *in  = (gdouble *) raw_in;
          gdouble *out = (gdouble *) GST_BUFFER_DATA (buf_out);
          for (i = 0; i < GST_BUFFER_SIZE (buf_out) / sizeof (gdouble); i++)
            out[i] = CLAMP(out[i] + in[i], -1.0, 1.0);
        } else if (adder->width == 32) {
          gfloat *in  = (gfloat *) raw_in;
          gfloat *out = (gfloat *) GST_BUFFER_DATA (buf_out);
          for (i = 0; i < GST_BUFFER_SIZE (buf_out) / sizeof (gfloat); i++)
            out[i] = CLAMP(out[i] + in[i], -1.0, 1.0);
        } else {
          gst_element_error (adder, STREAM, FORMAT, NULL,
                     ("invalid width (%u) for float audio in gstadder",
                     adder->width));
          return;
        }
      } else {
        gst_element_error (adder, STREAM, FORMAT, NULL,
	           ("invalid audio format (%d) in gstadder",
	           adder->format));
	return;
      }

      gst_bytestream_flush (input->bytestream, GST_BUFFER_SIZE (buf_out));

      GST_DEBUG ("done copying data");
    }
  }

  if (adder->format == GST_ADDER_FORMAT_UNSET) {
    GstCaps *caps = gst_caps_from_string (GST_AUDIO_INT_PAD_TEMPLATE_CAPS);

    if (gst_pad_try_set_caps (adder->srcpad, caps) < 0) {
      gst_element_error (adder, CORE, NEGOTIATION, NULL,
                         ("Couldn't set the default caps, use link_filtered instead"));
      return;
    }

    gst_adder_parse_caps (adder, gst_caps_get_structure(caps, 0));
  }

  GST_BUFFER_TIMESTAMP (buf_out) = adder->timestamp;
  if (adder->format == GST_ADDER_FORMAT_FLOAT)
    adder->offset += GST_BUFFER_SIZE (buf_out) / sizeof (gfloat) / adder->channels;
  else
    adder->offset += GST_BUFFER_SIZE (buf_out) * 8 / adder->width / adder->channels;
  adder->timestamp = adder->offset * GST_SECOND / adder->rate;

  /* send it out */

  GST_DEBUG ("pushing buf_out");
  gst_pad_push (adder->srcpad, GST_DATA (buf_out));
}


static GstElementStateReturn
gst_adder_change_state (GstElement *element)
{
  GstAdder *adder;

  adder = GST_ADDER (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      adder->timestamp = 0;
      adder->offset = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}


static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  if (!gst_element_register (plugin, "adder", GST_RANK_NONE, GST_TYPE_ADDER)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE(
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "adder",
  "Adds multiple streams",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)

