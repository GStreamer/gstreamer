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


/*#define GST_DEBUG_ENABLED*/
#include "gstrfc2250enc.h"

/* elementfactory information */
static GstElementDetails rfc2250_enc_details = {
  "RFC 2250 packet encoder",
  "Filter/Parser/System",
  "transforms MPEG1/2 video to an RFC 2250 compliant format",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

#define CLASS(o)	GST_RFC2250_ENC_CLASS (G_OBJECT_GET_CLASS (o))

/* GstRFC2250Enc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BIT_RATE,
  ARG_MPEG2,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "rfc2250_enc_sink",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT_RANGE (1, 2),
      "systemstream", GST_PROPS_BOOLEAN (FALSE)
  )
);

GST_PADTEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "rfc2250_enc_src",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT_RANGE (1, 2),
      "systemstream", GST_PROPS_BOOLEAN (FALSE)
  )
);

static void 	gst_rfc2250_enc_class_init	(GstRFC2250EncClass *klass);
static void 	gst_rfc2250_enc_init		(GstRFC2250Enc *rfc2250_enc);
static GstElementStateReturn
		gst_rfc2250_enc_change_state	(GstElement *element);

static gboolean	gst_rfc2250_enc_parse_packhead 	(GstRFC2250Enc *rfc2250_enc, GstBuffer *buffer);
static void 	gst_rfc2250_enc_send_data	(GstRFC2250Enc *rfc2250_enc, GstData *data);

static void 	gst_rfc2250_enc_loop 		(GstElement *element);

static void 	gst_rfc2250_enc_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_rfc2250_enc_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_rfc2250_enc_get_type (void)
{
  static GType rfc2250_enc_type = 0;

  if (!rfc2250_enc_type) {
    static const GTypeInfo rfc2250_enc_info = {
      sizeof(GstRFC2250EncClass),      NULL,
      NULL,
      (GClassInitFunc)gst_rfc2250_enc_class_init,
      NULL,
      NULL,
      sizeof(GstRFC2250Enc),
      0,
      (GInstanceInitFunc)gst_rfc2250_enc_init,
    };
    rfc2250_enc_type = g_type_register_static(GST_TYPE_ELEMENT, "GstRFC2250Enc", &rfc2250_enc_info, 0);
  }
  return rfc2250_enc_type;
}

static void
gst_rfc2250_enc_class_init (GstRFC2250EncClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BIT_RATE,
    g_param_spec_uint("bit_rate","bit_rate","bit_rate",
                      0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MPEG2,
    g_param_spec_boolean ("mpeg2", "mpeg2", "is this an mpeg2 stream",
                          FALSE, G_PARAM_READABLE));

  gobject_class->get_property = gst_rfc2250_enc_get_property;

  gstelement_class->change_state = gst_rfc2250_enc_change_state;
}

static void
gst_rfc2250_enc_init (GstRFC2250Enc *rfc2250_enc)
{
  rfc2250_enc->sinkpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad(GST_ELEMENT(rfc2250_enc),rfc2250_enc->sinkpad);
  gst_element_set_loop_function (GST_ELEMENT (rfc2250_enc), gst_rfc2250_enc_loop);
  rfc2250_enc->srcpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (src_factory), "src");
  gst_element_add_pad(GST_ELEMENT(rfc2250_enc),rfc2250_enc->srcpad);

  /* initialize parser state */
  rfc2250_enc->packetize = NULL;
  rfc2250_enc->next_ts = 0;
  rfc2250_enc->packet = 0;

  /* zero counters (should be done at RUNNING?) */
  rfc2250_enc->bit_rate = 0;
  rfc2250_enc->MTU = 3048;

  GST_FLAG_SET (rfc2250_enc, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_rfc2250_enc_new_buffer (GstRFC2250Enc *enc)
{
  if (enc->packet) {
    gst_pad_push (enc->srcpad, enc->packet);
  }
  enc->packet = gst_buffer_new ();
  enc->flags = 0;
  enc->remaining = enc->MTU;
}

static void
gst_rfc2250_enc_add_slice (GstRFC2250Enc *enc, GstBuffer *buffer)
{
  gint slice_length = GST_BUFFER_SIZE (buffer);

  /* see if the slice fits in the current buffer */
  if (slice_length <= enc->remaining) {
    gst_buffer_append (enc->packet, buffer);
    gst_buffer_unref (buffer);
    enc->remaining -= slice_length;
  }
  /* it doesn't fit */
  else {
    /* do we need to start a new packet? */
    if (slice_length <= enc->MTU) {
      gst_rfc2250_enc_new_buffer (enc);
      gst_buffer_append (enc->packet, buffer);
      gst_buffer_unref (buffer);
      enc->remaining -= slice_length;
    }
    /* else we have to fragment */
    else {
      gint offset = 0;

      while (slice_length > 0) {
	GstBuffer *outbuf;

	outbuf = gst_buffer_create_sub (buffer, offset, MIN (enc->remaining, slice_length));
        gst_buffer_append (enc->packet, outbuf);
	slice_length -= GST_BUFFER_SIZE (outbuf);
	offset += GST_BUFFER_SIZE (outbuf);
	gst_buffer_unref (outbuf);
        gst_rfc2250_enc_new_buffer (enc);
      }
      gst_buffer_unref (buffer);
    }
  }
}

static void
gst_rfc2250_enc_loop (GstElement *element)
{
  GstRFC2250Enc *enc = GST_RFC2250_ENC (element);
  GstData *data;
  guint id;
  gboolean mpeg2;

  data = gst_mpeg_packetize_read (enc->packetize);

  id = GST_MPEG_PACKETIZE_ID (enc->packetize);
  mpeg2 = GST_MPEG_PACKETIZE_IS_MPEG2 (enc->packetize);
    
  if (GST_IS_BUFFER (data)) {
    GstBuffer *buffer = GST_BUFFER (data);

    GST_DEBUG (0, "rfc2250enc: have chunk 0x%02X\n", id);

    switch (id) {
      case SEQUENCE_START_CODE:
	gst_rfc2250_enc_new_buffer (enc);
	enc->flags |= ENC_HAVE_SEQ;
	break;
      case GOP_START_CODE:
	if (enc->flags & ENC_HAVE_DATA) {
	  gst_rfc2250_enc_new_buffer (enc);
	}
	enc->flags |= ENC_HAVE_GOP;
	break;
      case PICTURE_START_CODE:
	if (enc->flags & ENC_HAVE_DATA) {
	  gst_rfc2250_enc_new_buffer (enc);
	}
	enc->flags |= ENC_HAVE_PIC;
	break;
      case EXT_START_CODE:
	break;
      case SLICE_MIN_START_CODE ... SLICE_MAX_START_CODE:
	enc->flags |= ENC_HAVE_DATA;
	gst_rfc2250_enc_add_slice (enc, buffer);
	buffer = NULL;
	break;
      case USER_START_CODE:
      case SEQUENCE_ERROR_START_CODE:
      case SEQUENCE_END_START_CODE:
	break;
      default:
	break;

    }
    if (buffer) {
      gst_buffer_append (enc->packet, buffer);
      enc->remaining -= GST_BUFFER_SIZE (buffer);
      gst_buffer_unref (buffer);
    }
  }
  else {
    if (enc->packet) {
      gst_pad_push (enc->srcpad, enc->packet);
      enc->packet = NULL;
      enc->flags = 0;
      enc->remaining = enc->MTU;
    }
    gst_pad_event_default (enc->sinkpad, GST_EVENT (data));
  }
}

static GstElementStateReturn
gst_rfc2250_enc_change_state (GstElement *element) 
{
  GstRFC2250Enc *rfc2250_enc = GST_RFC2250_ENC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!rfc2250_enc->packetize) {
        rfc2250_enc->packetize = gst_mpeg_packetize_new (rfc2250_enc->sinkpad, GST_MPEG_PACKETIZE_VIDEO);
      }
      break;
    case GST_STATE_READY_TO_NULL:
      if (rfc2250_enc->packetize) {
        gst_mpeg_packetize_destroy (rfc2250_enc->packetize);
        rfc2250_enc->packetize = NULL;
      }
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_rfc2250_enc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstRFC2250Enc *rfc2250_enc;

  /* it's not null if we got it, but it might not be ours */
  rfc2250_enc = GST_RFC2250_ENC(object);

  switch (prop_id) {
    case ARG_BIT_RATE: 
      g_value_set_uint (value, rfc2250_enc->bit_rate); 
      break;
    case ARG_MPEG2:
      if (rfc2250_enc->packetize)
        g_value_set_boolean (value, GST_MPEG_PACKETIZE_IS_MPEG2 (rfc2250_enc->packetize));
      else
        g_value_set_boolean (value, FALSE);
      break;
    default: 
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


gboolean
gst_rfc2250_enc_plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* this filter needs the bytestream package */
  if (!gst_library_load("gstbytestream")) {
    gst_info("rfc2250_enc:: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }

  /* create an elementfactory for the rfc2250_enc element */
  factory = gst_elementfactory_new("rfc2250enc",GST_TYPE_RFC2250_ENC,
                                   &rfc2250_enc_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (src_factory));
  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
