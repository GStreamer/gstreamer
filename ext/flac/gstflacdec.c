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

#include <string.h>
#include <sys/soundcard.h>

//#define DEBUG_ENABLED
#include "gstflacdec.h"


extern GstPadTemplate *dec_src_template, *dec_sink_template;

/* elementfactory information */
GstElementDetails flacdec_details = {
  "FLAC decoder",
  "Filter/Audio/Decoder",
  "Decodes FLAC lossless audio streams",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};

/* FlacDec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
};

static void 		gst_flacdec_class_init		(FlacDecClass *klass);
static void 		gst_flacdec_init		(FlacDec *flacdec);

static void 		gst_flacdec_loop		(GstElement *element);

static void 		gst_flacdec_metadata_callback 	(const FLAC__StreamDecoder *decoder, 
							 const FLAC__StreamMetaData *metadata, 
							 void *client_data);
static void 		gst_flacdec_error_callback 	(const FLAC__StreamDecoder *decoder, 
							 FLAC__StreamDecoderErrorStatus status, 
							 void *client_data);

static FLAC__StreamDecoderReadStatus 	gst_flacdec_read 	(const FLAC__StreamDecoder *decoder, 
								 FLAC__byte buffer[], unsigned *bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus 	gst_flacdec_write 	(const FLAC__StreamDecoder *decoder, 
								 const FLAC__Frame *frame, const FLAC__int32 *buffer[], 
								 void *client_data);

static GstElementClass *parent_class = NULL;
//static guint gst_flacdec_signals[LAST_SIGNAL] = { 0 };

GType
flacdec_get_type(void) {
  static GType flacdec_type = 0;

  if (!flacdec_type) {
    static const GTypeInfo flacdec_info = {
      sizeof(FlacDecClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_flacdec_class_init,
      NULL,
      NULL,
      sizeof(FlacDec),
      0,
      (GInstanceInitFunc)gst_flacdec_init,
    };
    flacdec_type = g_type_register_static (GST_TYPE_ELEMENT, "FlacDec", &flacdec_info, 0);
  }
  return flacdec_type;
}

static void
gst_flacdec_class_init (FlacDecClass *klass) 
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}

static void 
gst_flacdec_init (FlacDec *flacdec) 
{
  flacdec->sinkpad = gst_pad_new_from_template (dec_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (flacdec), gst_flacdec_loop);
  flacdec->srcpad = gst_pad_new_from_template (dec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->srcpad);

  flacdec->decoder = FLAC__stream_decoder_new ();
  flacdec->offset_left = 0;
  flacdec->data_left = NULL;

  FLAC__stream_decoder_set_read_callback (flacdec->decoder, gst_flacdec_read);
  FLAC__stream_decoder_set_write_callback (flacdec->decoder, gst_flacdec_write);
  FLAC__stream_decoder_set_metadata_callback (flacdec->decoder, gst_flacdec_metadata_callback);
  FLAC__stream_decoder_set_error_callback (flacdec->decoder, gst_flacdec_error_callback);
  FLAC__stream_decoder_set_client_data (flacdec->decoder, flacdec);

  FLAC__stream_decoder_init (flacdec->decoder);
}

static void 
gst_flacdec_metadata_callback (const FLAC__StreamDecoder *decoder, const FLAC__StreamMetaData *metadata, void *client_data)
{
}

static void 
gst_flacdec_error_callback (const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
}

static FLAC__StreamDecoderReadStatus
gst_flacdec_read (const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], unsigned *bytes, void *client_data)
{
  FlacDec *flacdec;
  GstBuffer *inbuf = NULL;
  gint insize;
  guchar *indata;

  flacdec = GST_FLACDEC (client_data);

  if (flacdec->data_left == NULL) {
    inbuf = gst_pad_pull (flacdec->sinkpad);
    insize = GST_BUFFER_SIZE (inbuf);
    indata = GST_BUFFER_DATA (inbuf);
  }
  else {
    inbuf = flacdec->data_left;
    insize = GST_BUFFER_SIZE (inbuf) - flacdec->offset_left;
    indata = GST_BUFFER_DATA (inbuf) + flacdec->offset_left;
  }

  if (*bytes < insize) {
    // we have more than we can handle
    flacdec->data_left = inbuf;
    flacdec->offset_left += *bytes;
    inbuf = NULL;
  }
  else {
    flacdec->data_left = NULL;
    flacdec->offset_left = 0;
    *bytes = insize;
  }
  memcpy (buffer, indata, *bytes);

  if (inbuf) 
    gst_buffer_unref (inbuf);

  return FLAC__STREAM_DECODER_READ_CONTINUE;
}

static FLAC__StreamDecoderWriteStatus
gst_flacdec_write (const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *buffer[], void *client_data)
{
  FlacDec *flacdec;
  GstBuffer *outbuf;
  guint depth = frame->header.bits_per_sample;
  guint channels = frame->header.channels;
  guint samples = frame->header.blocksize;
  guint j, i;

  flacdec = GST_FLACDEC (client_data);

  if (!GST_PAD_CAPS (flacdec->srcpad)) {
    gst_pad_try_set_caps (flacdec->srcpad,
		    GST_CAPS_NEW (
		      "flac_caps",
		      "audio/raw",
		        "format", 	GST_PROPS_STRING ("int"),
                         "law",         GST_PROPS_INT (0),
                         "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
                         "signed",      GST_PROPS_BOOLEAN (TRUE),
                         "width",       GST_PROPS_INT (depth),
                         "depth",       GST_PROPS_INT (depth),
                         "rate",     	GST_PROPS_INT (frame->header.sample_rate),
                         "channels", 	GST_PROPS_INT (channels)
		    ));
  }

  outbuf = gst_buffer_new ();
  GST_BUFFER_SIZE (outbuf) = samples * channels * ((depth+7)>>3);
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));

  if (depth == 8) {
    guint8 *outbuffer = (guint8 *)GST_BUFFER_DATA (outbuf);

    for (i=0; i<samples; i++) {
      for (j=0; j < channels; j++) {
        *outbuffer++ = (guint8) buffer[j][i];
      }
    }
  }
  else if (depth == 16) {
    guint16 *outbuffer = (guint16 *)GST_BUFFER_DATA (outbuf);

    for (i=0; i<samples; i++) {
      for (j=0; j < channels; j++) {
        *outbuffer++ = (guint16) buffer[j][i];
      }
    }
  }
  else {
    g_warning ("flacdec: invalid depth %d found\n", depth);
    return FLAC__STREAM_DECODER_WRITE_ABORT;
  }

  gst_pad_push (flacdec->srcpad, outbuf);

  return FLAC__STREAM_DECODER_WRITE_CONTINUE;
}

static void 
gst_flacdec_loop (GstElement *element) 
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (element);
  
  if (FLAC__stream_decoder_get_state (flacdec->decoder) == FLAC__STREAM_DECODER_SEARCH_FOR_METADATA) {
    FLAC__stream_decoder_process_metadata (flacdec->decoder);
  }

  FLAC__stream_decoder_process_one_frame (flacdec->decoder);
}

