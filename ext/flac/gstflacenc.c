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


#include <stdlib.h>
#include <string.h>

#include <gstflacenc.h>

extern GstPadTemplate *enc_src_template, *enc_sink_template;

/* elementfactory information */
GstElementDetails flacenc_details = {
  "FLAC encoder",
  "Codec/Audio/Encoder",
  "Encodes audio with the FLAC lossless audio encoder",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

/* FlacEnc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
};

static void	gst_flacenc_init		(FlacEnc *flacenc);
static void	gst_flacenc_class_init		(FlacEncClass *klass);

static void	gst_flacenc_chain		(GstPad *pad, GstBuffer *buf);

static void     gst_flacenc_set_property        (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void     gst_flacenc_get_property        (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static FLAC__StreamEncoderWriteStatus 
		gst_flacenc_write_callback 	(const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], unsigned bytes, 
			    			 unsigned samples, unsigned current_frame, void *client_data);
static void 	gst_flacenc_metadata_callback 	(const FLAC__StreamEncoder *encoder, const FLAC__StreamMetaData *metadata, 
						 void *client_data);

static GstElementClass *parent_class = NULL;
/*static guint gst_flacenc_signals[LAST_SIGNAL] = { 0 }; */

GType
flacenc_get_type (void)
{
  static GType flacenc_type = 0;

  if (!flacenc_type) {
    static const GTypeInfo flacenc_info = {
      sizeof(FlacEncClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_flacenc_class_init,
      NULL,
      NULL,
      sizeof(FlacEnc),
      0,
      (GInstanceInitFunc)gst_flacenc_init,
    };
    flacenc_type = g_type_register_static (GST_TYPE_ELEMENT, "FlacEnc", &flacenc_info, 0);
  }
  return flacenc_type;
}

static void
gst_flacenc_class_init (FlacEncClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
  
  /* we have no properties atm so this is a bit silly */
  gobject_class->set_property = gst_flacenc_set_property;
  gobject_class->get_property = gst_flacenc_get_property;
}

static GstPadConnectReturn
gst_flacenc_sinkconnect (GstPad *pad, GstCaps *caps)
{
  FlacEnc *flacenc;

  flacenc = GST_FLACENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "channels", &flacenc->channels);
  gst_caps_get_int (caps, "depth", &flacenc->depth);
  gst_caps_get_int (caps, "rate", &flacenc->sample_rate);

  FLAC__stream_encoder_set_bits_per_sample (flacenc->encoder, flacenc->depth);
  FLAC__stream_encoder_set_sample_rate (flacenc->encoder, flacenc->sample_rate);
  FLAC__stream_encoder_set_channels (flacenc->encoder, flacenc->channels);

  return GST_PAD_CONNECT_OK;
}

static void
gst_flacenc_init (FlacEnc *flacenc)
{
  flacenc->sinkpad = gst_pad_new_from_template (enc_sink_template, "sink");
  gst_element_add_pad(GST_ELEMENT(flacenc),flacenc->sinkpad);
  gst_pad_set_chain_function(flacenc->sinkpad,gst_flacenc_chain);
  gst_pad_set_connect_function (flacenc->sinkpad, gst_flacenc_sinkconnect);

  flacenc->srcpad = gst_pad_new_from_template (enc_src_template, "src");
  gst_element_add_pad(GST_ELEMENT(flacenc),flacenc->srcpad);

  flacenc->first = TRUE;
  flacenc->first_buf = NULL;
  flacenc->encoder = FLAC__stream_encoder_new();

  FLAC__stream_encoder_set_write_callback (flacenc->encoder, gst_flacenc_write_callback);
  FLAC__stream_encoder_set_metadata_callback (flacenc->encoder, gst_flacenc_metadata_callback);
  FLAC__stream_encoder_set_client_data (flacenc->encoder, flacenc);

  GST_FLAG_SET (flacenc, GST_ELEMENT_EVENT_AWARE);
}

static FLAC__StreamEncoderWriteStatus 
gst_flacenc_write_callback (const FLAC__StreamEncoder *encoder, const FLAC__byte buffer[], unsigned bytes, 
			    unsigned samples, unsigned current_frame, void *client_data)
{
  FlacEnc *flacenc;
  GstBuffer *outbuf;

  flacenc = GST_FLACENC (client_data);

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = bytes;
  GST_BUFFER_DATA (outbuf) = g_malloc (bytes);

  memcpy (GST_BUFFER_DATA (outbuf), buffer, bytes);

  if (flacenc->first) {
    flacenc->first_buf = outbuf;
    gst_buffer_ref (outbuf);
    flacenc->first = FALSE;
  }

  gst_pad_push (flacenc->srcpad, outbuf);

  return FLAC__STREAM_ENCODER_WRITE_OK;
}

static void 
gst_flacenc_metadata_callback (const FLAC__StreamEncoder *encoder, const FLAC__StreamMetaData *metadata, void *client_data)
{
  GstEvent *event;
  FlacEnc *flacenc;

  flacenc = GST_FLACENC (client_data);

  event = gst_event_new_discontinuous (FALSE, GST_FORMAT_BYTES, 0, NULL);
  gst_pad_push (flacenc->srcpad, GST_BUFFER (event));

  if (flacenc->first_buf) {
    const FLAC__uint64 samples = metadata->data.stream_info.total_samples;
    const unsigned min_framesize = metadata->data.stream_info.min_framesize;
    const unsigned max_framesize = metadata->data.stream_info.max_framesize;

    guint8 *data = GST_BUFFER_DATA (flacenc->first_buf);
    GstBuffer *outbuf = flacenc->first_buf;

    /* this looks evil but is actually how one is supposed to write
     * the stream stats according to the FLAC examples */

    memcpy (&data[26], metadata->data.stream_info.md5sum, 16);
    
    data[21] = (data[21] & 0xf0) | 
	       (FLAC__byte)((samples >> 32) & 0x0f);
    data[22] = (FLAC__byte)((samples >> 24) & 0xff);
    data[23] = (FLAC__byte)((samples >> 16) & 0xff);
    data[24] = (FLAC__byte)((samples >> 8 ) & 0xff);
    data[25] = (FLAC__byte)((samples      ) & 0xff);

    data[12] = (FLAC__byte)((min_framesize >> 16) & 0xFF);
    data[13] = (FLAC__byte)((min_framesize >> 8 ) & 0xFF);
    data[14] = (FLAC__byte)((min_framesize      ) & 0xFF);

    data[15] = (FLAC__byte)((max_framesize >> 16) & 0xFF);
    data[16] = (FLAC__byte)((max_framesize >> 8 ) & 0xFF);
    data[17] = (FLAC__byte)((max_framesize      ) & 0xFF);

    flacenc->first_buf = NULL;
    gst_pad_push (flacenc->srcpad, outbuf);
  }
}

static void
gst_flacenc_chain (GstPad *pad, GstBuffer *buf)
{
  FlacEnc *flacenc;
  gint32 *data[FLAC__MAX_CHANNELS];
  gulong insize;
  gint samples, channels, depth;
  gulong i, j;
  gboolean res;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  flacenc = GST_FLACENC (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_EOS:
	FLAC__stream_encoder_finish(flacenc->encoder);
      default:
	gst_pad_event_default (pad, event);
        break;
    }
    return;
  }

  channels = flacenc->channels;
  depth = flacenc->depth;

  insize = GST_BUFFER_SIZE (buf);
  samples = insize / channels / ((depth+7)>>3);

  if (FLAC__stream_encoder_get_state (flacenc->encoder) == FLAC__STREAM_ENCODER_UNINITIALIZED) {
    FLAC__StreamEncoderState state;

    state = FLAC__stream_encoder_init (flacenc->encoder);
    
    g_assert (state == FLAC__STREAM_ENCODER_OK);
  }

  for (i=0; i<channels; i++) {
    data[i] = g_malloc (samples * sizeof (gint32));
  }
    
  if (depth == 8) {
    gint8 *indata = (gint8 *) GST_BUFFER_DATA (buf);
    for (j=0; j<samples; j++) {
      for (i=0; i<channels; i++) {
        data[i][j] = (gint32) *indata++;
      }
    }
  }
  else if (depth == 16) {
    gint16 *indata = (gint16 *) GST_BUFFER_DATA (buf);
    for (j=0; j<samples; j++) {
      for (i=0; i<channels; i++) {
        data[i][j] = (gint32) *indata++;
      }
    }
  }

  res = FLAC__stream_encoder_process (flacenc->encoder, (const FLAC__int32 **)data, samples);

  for (i=0; i<channels; i++) {
    g_free (data[i]);
  }

  gst_buffer_unref(buf);
}

static void
gst_flacenc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  FlacEnc *this;
  
  this = (FlacEnc *)object;
  switch (prop_id) {
  default:
    GST_DEBUG(0, "Unknown arg %d", prop_id);
    return;
  }
}

static void
gst_flacenc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  FlacEnc *this;
  
  this = (FlacEnc *)object;
  
  switch (prop_id) {
  default:
    GST_DEBUG(0, "Unknown arg %d", prop_id);
    break;
  }
}

