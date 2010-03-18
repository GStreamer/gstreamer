/* GStreamer
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstrfc2250enc.h"

#define CLASS(o)        GST_RFC2250_ENC_CLASS (G_OBJECT_GET_CLASS (o))

/* GstRFC2250Enc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BIT_RATE,
  ARG_MPEG2
      /* FILL ME */
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], " "systemstream = (boolean) FALSE")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], " "systemstream = (boolean) FALSE")
    );

static void gst_rfc2250_enc_class_init (GstRFC2250EncClass * klass);
static void gst_rfc2250_enc_base_init (GstRFC2250EncClass * klass);
static void gst_rfc2250_enc_init (GstRFC2250Enc * rfc2250_enc);
static GstStateChangeReturn
gst_rfc2250_enc_change_state (GstElement * element, GstStateChange transition);

static void gst_rfc2250_enc_loop (GstElement * element);

static void gst_rfc2250_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/*static guint gst_rfc2250_enc_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_rfc2250_enc_get_type (void)
{
  static GType rfc2250_enc_type = 0;

  if (!rfc2250_enc_type) {
    static const GTypeInfo rfc2250_enc_info = {
      sizeof (GstRFC2250EncClass),
      (GBaseInitFunc) gst_rfc2250_enc_base_init,
      NULL,
      (GClassInitFunc) gst_rfc2250_enc_class_init,
      NULL,
      NULL,
      sizeof (GstRFC2250Enc),
      0,
      (GInstanceInitFunc) gst_rfc2250_enc_init,
    };

    rfc2250_enc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRFC2250Enc",
        &rfc2250_enc_info, 0);
  }
  return rfc2250_enc_type;
}

static void
gst_rfc2250_enc_base_init (GstRFC2250EncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (element_class,
      "RFC 2250 packet encoder", "Codec/Parser",
      "transforms MPEG1/2 video to an RFC 2250 compliant format",
      "Wim Taymans <wim.taymans@chello.be>");
}

static void
gst_rfc2250_enc_class_init (GstRFC2250EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
      g_param_spec_uint ("bit_rate", "bit_rate", "bit_rate",
          0, G_MAXUINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MPEG2,
      g_param_spec_boolean ("mpeg2", "mpeg2", "is this an mpeg2 stream",
          FALSE, G_PARAM_READABLE));

  gobject_class->get_property = gst_rfc2250_enc_get_property;

  gstelement_class->change_state = gst_rfc2250_enc_change_state;
}

static void
gst_rfc2250_enc_init (GstRFC2250Enc * rfc2250_enc)
{
  rfc2250_enc->sinkpad =
      gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (rfc2250_enc), rfc2250_enc->sinkpad);
  gst_element_set_loop_function (GST_ELEMENT (rfc2250_enc),
      gst_rfc2250_enc_loop);
  rfc2250_enc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (rfc2250_enc), rfc2250_enc->srcpad);

  /* initialize parser state */
  rfc2250_enc->packetize = NULL;
  rfc2250_enc->next_ts = 0;
  rfc2250_enc->packet = 0;

  /* zero counters (should be done at RUNNING?) */
  rfc2250_enc->bit_rate = 0;
  rfc2250_enc->MTU = 3048;
}

static void
gst_rfc2250_enc_new_buffer (GstRFC2250Enc * enc)
{
  if (enc->packet) {
    gst_pad_push (enc->srcpad, GST_DATA (enc->packet));
  }
  enc->packet = gst_buffer_new ();
  enc->flags = 0;
  enc->remaining = enc->MTU;
}

static void
gst_rfc2250_enc_add_slice (GstRFC2250Enc * enc, GstBuffer * buffer)
{
  gint slice_length = GST_BUFFER_SIZE (buffer);

  /* see if the slice fits in the current buffer */
  if (slice_length <= enc->remaining) {
    GstBuffer *newbuf;

    newbuf = gst_buffer_merge (enc->packet, buffer);
    gst_buffer_unref (buffer);
    gst_buffer_unref (enc->packet);
    enc->packet = newbuf;
    enc->remaining -= slice_length;
  }
  /* it doesn't fit */
  else {
    /* do we need to start a new packet? */
    if (slice_length <= enc->MTU) {
      GstBuffer *newbuf;

      gst_rfc2250_enc_new_buffer (enc);
      newbuf = gst_buffer_merge (enc->packet, buffer);
      gst_buffer_unref (buffer);
      gst_buffer_unref (enc->packet);
      enc->packet = newbuf;
      enc->remaining -= slice_length;
    }
    /* else we have to fragment */
    else {
      gint offset = 0;

      while (slice_length > 0) {
        GstBuffer *outbuf;
        GstBuffer *newbuf;

        outbuf =
            gst_buffer_create_sub (buffer, offset, MIN (enc->remaining,
                slice_length));
        newbuf = gst_buffer_merge (enc->packet, outbuf);
        slice_length -= GST_BUFFER_SIZE (outbuf);
        offset += GST_BUFFER_SIZE (outbuf);
        gst_buffer_unref (outbuf);
        gst_buffer_unref (newbuf);
        enc->packet = newbuf;
        gst_rfc2250_enc_new_buffer (enc);
      }
      gst_buffer_unref (buffer);
    }
  }
}

static void
gst_rfc2250_enc_loop (GstElement * element)
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

    GST_DEBUG ("rfc2250enc: have chunk 0x%02X", id);

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
      case USER_START_CODE:
      case SEQUENCE_ERROR_START_CODE:
      case SEQUENCE_END_START_CODE:
        break;
      default:
        /* do this here because of the long range */
        if (id >= SLICE_MIN_START_CODE && id <= SLICE_MAX_START_CODE) {
          enc->flags |= ENC_HAVE_DATA;
          gst_rfc2250_enc_add_slice (enc, buffer);
          buffer = NULL;
          break;
        }
        break;

    }
    if (buffer) {
      gst_buffer_merge (enc->packet, buffer);
      enc->remaining -= GST_BUFFER_SIZE (buffer);
      gst_buffer_unref (buffer);
    }
  } else {
    if (enc->packet) {
      gst_pad_push (enc->srcpad, GST_DATA (enc->packet));
      enc->packet = NULL;
      enc->flags = 0;
      enc->remaining = enc->MTU;
    }
    gst_pad_event_default (enc->sinkpad, GST_EVENT (data));
  }
}

static GstStateChangeReturn
gst_rfc2250_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstRFC2250Enc *rfc2250_enc = GST_RFC2250_ENC (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!rfc2250_enc->packetize) {
        rfc2250_enc->packetize =
            gst_mpeg_packetize_new (rfc2250_enc->sinkpad,
            GST_MPEG_PACKETIZE_VIDEO);
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (rfc2250_enc->packetize) {
        gst_mpeg_packetize_destroy (rfc2250_enc->packetize);
        rfc2250_enc->packetize = NULL;
      }
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_rfc2250_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRFC2250Enc *rfc2250_enc;

  /* it's not null if we got it, but it might not be ours */
  rfc2250_enc = GST_RFC2250_ENC (object);

  switch (prop_id) {
    case ARG_BIT_RATE:
      g_value_set_uint (value, rfc2250_enc->bit_rate);
      break;
    case ARG_MPEG2:
      if (rfc2250_enc->packetize)
        g_value_set_boolean (value,
            GST_MPEG_PACKETIZE_IS_MPEG2 (rfc2250_enc->packetize));
      else
        g_value_set_boolean (value, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


gboolean
gst_rfc2250_enc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rfc2250enc",
      GST_RANK_NONE, GST_TYPE_RFC2250_ENC);
}
