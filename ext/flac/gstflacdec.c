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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

/*#define DEBUG_ENABLED */
#include "gstflacdec.h"
#include <gst/gsttaginterface.h>

#include <gst/tag/tag.h>

#include "flac_compat.h"

static GstPadTemplate *src_template, *sink_template;

/* elementfactory information */
GstElementDetails flacdec_details = {
  "FLAC decoder",
  "Codec/Decoder/Audio",
  "Decodes FLAC lossless audio streams",
  "Wim Taymans <wim.taymans@chello.be>",
};

/* FlacDec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METADATA
};

static void             gst_flacdec_base_init           (gpointer g_class);
static void 		gst_flacdec_class_init		(FlacDecClass *klass);
static void 		gst_flacdec_init		(FlacDec *flacdec);

static void 		gst_flacdec_loop		(GstElement *element);
static GstElementStateReturn
			gst_flacdec_change_state 	(GstElement *element);
static const GstFormat* gst_flacdec_get_src_formats 	(GstPad *pad);
static gboolean 	gst_flacdec_convert_src	 	(GstPad *pad, GstFormat src_format, gint64 src_value,
		        				 GstFormat *dest_format, gint64 *dest_value);
static const GstQueryType* 
			gst_flacdec_get_src_query_types (GstPad *pad);
static gboolean		gst_flacdec_src_query 		(GstPad *pad, GstQueryType type,
	               					 GstFormat *format, gint64 *value);
static const GstEventMask* 
			gst_flacdec_get_src_event_masks (GstPad *pad);
static gboolean 	gst_flacdec_src_event 		(GstPad *pad, GstEvent *event);

static FLAC__SeekableStreamDecoderReadStatus 	
			gst_flacdec_read 		(const FLAC__SeekableStreamDecoder *decoder, 
							 FLAC__byte buffer[], unsigned *bytes, 
							 void *client_data);
static FLAC__SeekableStreamDecoderSeekStatus 	
			gst_flacdec_seek 		(const FLAC__SeekableStreamDecoder *decoder, 
							 FLAC__uint64 position, void *client_data);
static FLAC__SeekableStreamDecoderTellStatus 	
			gst_flacdec_tell 		(const FLAC__SeekableStreamDecoder *decoder, 
							 FLAC__uint64 *position, void *client_data);
static FLAC__SeekableStreamDecoderLengthStatus 	
			gst_flacdec_length 		(const FLAC__SeekableStreamDecoder *decoder, 
							 FLAC__uint64 *length, void *client_data);
static FLAC__bool 	gst_flacdec_eof 		(const FLAC__SeekableStreamDecoder *decoder, 
							 void *client_data);
static FLAC__StreamDecoderWriteStatus 	
			gst_flacdec_write 		(const FLAC__SeekableStreamDecoder *decoder, 
							 const FLAC__Frame *frame, 
							 const FLAC__int32 * const buffer[], 
							 void *client_data);
static void 		gst_flacdec_metadata_callback 	(const FLAC__SeekableStreamDecoder *decoder, 
							 const FLAC__StreamMetadata *metadata, 
							 void *client_data);
static void 		gst_flacdec_error_callback 	(const FLAC__SeekableStreamDecoder *decoder, 
							 FLAC__StreamDecoderErrorStatus status, 
							 void *client_data);

static GstElementClass *parent_class = NULL;
/*static guint gst_flacdec_signals[LAST_SIGNAL] = { 0 }; */

GType
flacdec_get_type(void) {
  static GType flacdec_type = 0;

  if (!flacdec_type) {
    static const GTypeInfo flacdec_info = {
      sizeof(FlacDecClass),
      gst_flacdec_base_init,
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

static GstCaps*
flac_caps_factory (void)
{
  return gst_caps_new_simple ("audio/x-flac", NULL);
  /* "rate",     	GST_PROPS_INT_RANGE (11025, 48000),
   * "channels", 	GST_PROPS_INT_RANGE (1, 2), */
}

static GstCaps*
raw_caps_factory (void)
{
  return gst_caps_new_simple ("audio/x-raw-int",
      "endianness", 	G_TYPE_INT, G_BYTE_ORDER,
      "signed", 	G_TYPE_BOOLEAN, TRUE,
      "width", 		G_TYPE_INT, 16,
      "depth",    	G_TYPE_INT, 16,
      "rate",     	GST_TYPE_INT_RANGE, 11025, 48000,
      "channels", 	GST_TYPE_INT_RANGE, 1, 2,
      NULL);
}

static void
gst_flacdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *flac_caps;

  raw_caps = raw_caps_factory ();
  flac_caps = flac_caps_factory ();

  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, flac_caps);
  src_template = gst_pad_template_new ("src", GST_PAD_SRC, 
      GST_PAD_ALWAYS, raw_caps);
  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_add_pad_template (element_class, src_template);
  gst_element_class_set_details (element_class, &flacdec_details);
}

static void
gst_flacdec_class_init (FlacDecClass *klass) 
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gstelement_class = (GstElementClass*)klass;
  gobject_class = (GObjectClass*) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_flacdec_change_state;
}

static void 
gst_flacdec_init (FlacDec *flacdec) 
{
  flacdec->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->sinkpad);
  gst_pad_set_convert_function (flacdec->sinkpad, NULL);

  gst_element_set_loop_function (GST_ELEMENT (flacdec), gst_flacdec_loop);
  flacdec->srcpad = gst_pad_new_from_template (src_template, "src");
  gst_element_add_pad (GST_ELEMENT (flacdec), flacdec->srcpad);
  gst_pad_set_formats_function (flacdec->srcpad, gst_flacdec_get_src_formats);
  gst_pad_set_convert_function (flacdec->srcpad, gst_flacdec_convert_src);
  gst_pad_set_query_type_function (flacdec->srcpad, gst_flacdec_get_src_query_types);
  gst_pad_set_query_function (flacdec->srcpad, gst_flacdec_src_query);
  gst_pad_set_event_mask_function (flacdec->srcpad, gst_flacdec_get_src_event_masks);
  gst_pad_set_event_function (flacdec->srcpad, gst_flacdec_src_event);
  gst_pad_use_explicit_caps (flacdec->srcpad);

  flacdec->decoder = FLAC__seekable_stream_decoder_new ();
  flacdec->total_samples = 0;
  flacdec->init = TRUE;
  flacdec->eos = FALSE;
  flacdec->seek_pending = FALSE;

  FLAC__seekable_stream_decoder_set_read_callback (flacdec->decoder, gst_flacdec_read);
  FLAC__seekable_stream_decoder_set_seek_callback (flacdec->decoder, gst_flacdec_seek);
  FLAC__seekable_stream_decoder_set_tell_callback (flacdec->decoder, gst_flacdec_tell);
  FLAC__seekable_stream_decoder_set_length_callback (flacdec->decoder, gst_flacdec_length);  
  FLAC__seekable_stream_decoder_set_eof_callback (flacdec->decoder, gst_flacdec_eof);
#if FLAC_VERSION >= 0x010003
  FLAC__seekable_stream_decoder_set_write_callback (flacdec->decoder, gst_flacdec_write);
#else
  FLAC__seekable_stream_decoder_set_write_callback (flacdec->decoder,
	(FLAC__StreamDecoderWriteStatus (*)
		(const FLAC__SeekableStreamDecoder *decoder, 
		 const FLAC__Frame *frame,
		 const FLAC__int32 *buffer[], 
		 void *client_data))
		(gst_flacdec_write));
#endif
  FLAC__seekable_stream_decoder_set_metadata_respond (flacdec->decoder, FLAC__METADATA_TYPE_VORBIS_COMMENT);
  FLAC__seekable_stream_decoder_set_metadata_callback (flacdec->decoder, gst_flacdec_metadata_callback);
  FLAC__seekable_stream_decoder_set_error_callback (flacdec->decoder, gst_flacdec_error_callback);
  FLAC__seekable_stream_decoder_set_client_data (flacdec->decoder, flacdec);
}

static gboolean
gst_flacdec_update_metadata (FlacDec *flacdec, const FLAC__StreamMetadata *metadata)
{
  GstTagList *list;
  guint32 number_of_comments, cursor, str_len;
  gchar *p_value, *value, *name, *str_ptr;

  list = gst_tag_list_new ();
  if (list == NULL) {
    return FALSE;
  }

  number_of_comments = metadata->data.vorbis_comment.num_comments;
  value = NULL;
  GST_DEBUG ("%d tag(s) found",  number_of_comments);
  for (cursor = 0; cursor < number_of_comments; cursor++)
  {			
    str_ptr = metadata->data.vorbis_comment.comments[cursor].entry;
    str_len = metadata->data.vorbis_comment.comments[cursor].length;
    p_value = g_strstr_len ( str_ptr, str_len , "=" );
    if (p_value)
    {			
      name = g_strndup (str_ptr, p_value - str_ptr);
      value = g_strndup (p_value + 1, str_ptr + str_len - p_value - 1);
			
      GST_DEBUG ("%s : %s", name, value);
      gst_vorbis_tag_add (list, name, value);
      g_free (name);
      g_free (value);
    }
  }


  gst_element_found_tags (GST_ELEMENT (flacdec), list);
  if (GST_PAD_IS_USABLE (flacdec->srcpad)) {
    gst_pad_push (flacdec->srcpad, GST_DATA (gst_event_new_tag (list)));
  }
  return TRUE;
}


static void 
gst_flacdec_metadata_callback (const FLAC__SeekableStreamDecoder *decoder,
			       const FLAC__StreamMetadata *metadata, void *client_data)
{
  FlacDec *flacdec;
	
  flacdec = GST_FLACDEC (client_data);

  switch (metadata->type)
  {
    case FLAC__METADATA_TYPE_STREAMINFO:
         flacdec->stream_samples = metadata->data.stream_info.total_samples;
 	 break;
    case FLAC__METADATA_TYPE_VORBIS_COMMENT:
         gst_flacdec_update_metadata (flacdec, metadata);		  
	 break;
    default:
         break;
  }
}

static void 
gst_flacdec_error_callback (const FLAC__SeekableStreamDecoder *decoder, 
			    FLAC__StreamDecoderErrorStatus status, void *client_data)
{
  FlacDec *flacdec;
  gchar *error;

  flacdec = GST_FLACDEC (client_data);

  switch (status) {
    case FLAC__STREAM_DECODER_ERROR_STATUS_LOST_SYNC:
      error = "lost sync";
      break;
    case FLAC__STREAM_DECODER_ERROR_STATUS_BAD_HEADER:
      error = "bad header";
      break;
    case FLAC__STREAM_DECODER_ERROR_STATUS_FRAME_CRC_MISMATCH:
      error = "CRC mismatch";
      break;
    default:
      error = "unkown error";
      break;
  }

  GST_ELEMENT_ERROR (flacdec, STREAM, DECODE, (NULL), (error));
}

static FLAC__SeekableStreamDecoderSeekStatus 	
gst_flacdec_seek (const FLAC__SeekableStreamDecoder *decoder, 
		  FLAC__uint64 position, void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);

  GST_DEBUG ("seek %" G_GINT64_FORMAT, position);
  if (!gst_bytestream_seek (flacdec->bs, position, GST_SEEK_METHOD_SET)) {
    return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_ERROR;
  }
  return FLAC__SEEKABLE_STREAM_DECODER_SEEK_STATUS_OK;
}

static FLAC__SeekableStreamDecoderTellStatus 	
gst_flacdec_tell (const FLAC__SeekableStreamDecoder *decoder, 
		  FLAC__uint64 *position, void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);

  *position = gst_bytestream_tell (flacdec->bs);
  if (*position == -1)
    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR;

  GST_DEBUG ("tell %" G_GINT64_FORMAT, *position);

  return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_OK;
}

static FLAC__SeekableStreamDecoderLengthStatus 	
gst_flacdec_length (const FLAC__SeekableStreamDecoder *decoder, 
		    FLAC__uint64 *length, void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);

  *length = gst_bytestream_length (flacdec->bs);
  if (*length == -1)
    return FLAC__SEEKABLE_STREAM_DECODER_TELL_STATUS_ERROR;

  GST_DEBUG ("length %" G_GINT64_FORMAT, *length);

  return FLAC__SEEKABLE_STREAM_DECODER_LENGTH_STATUS_OK;
}

static FLAC__bool
gst_flacdec_eof (const FLAC__SeekableStreamDecoder *decoder, 
		 void *client_data)
{
  FlacDec *flacdec;

  flacdec = GST_FLACDEC (client_data);
  GST_DEBUG ("eof %d", flacdec->eos);

  return flacdec->eos;
}

static FLAC__SeekableStreamDecoderReadStatus
gst_flacdec_read (const FLAC__SeekableStreamDecoder *decoder, 
		  FLAC__byte buffer[], unsigned *bytes, 
		  void *client_data)
{
  FlacDec *flacdec;
  gint insize = 0;
  guint8 *indata;

  flacdec = GST_FLACDEC (client_data);

  //g_print ("read %u\n", *bytes);
  
  while (insize == 0) {
    insize = gst_bytestream_peek_bytes (flacdec->bs, &indata, *bytes);
    if (insize < *bytes) {
      GstEvent *event;
      guint32 avail;
			    
      gst_bytestream_get_status (flacdec->bs, &avail, &event);

      switch (GST_EVENT_TYPE (event)) {
        case GST_EVENT_EOS:
          GST_DEBUG ("eos");
          flacdec->eos = TRUE; 
	  gst_event_unref (event);
          if (avail == 0) {
            return 0;
          }
          break;
        case GST_EVENT_DISCONTINUOUS:
          GST_DEBUG ("discont");

	  /* we are not yet sending the discont, we'll do that in the next write operation */
	  flacdec->need_discont = TRUE;
	  gst_event_unref (event);
	  break;
        default:
	  gst_pad_event_default (flacdec->sinkpad, event);
          break;
      }
      if (avail > 0)
        insize = gst_bytestream_peek_bytes (flacdec->bs, &indata, avail);
      else
        insize = 0;
    }
  }

  memcpy (buffer, indata, insize);
  *bytes = insize;
  gst_bytestream_flush_fast (flacdec->bs, insize);

  return FLAC__SEEKABLE_STREAM_DECODER_READ_STATUS_OK;
}

static FLAC__StreamDecoderWriteStatus
gst_flacdec_write (const FLAC__SeekableStreamDecoder *decoder, 
		   const FLAC__Frame *frame, 
		   const FLAC__int32 * const buffer[], 
		   void *client_data)
{
  FlacDec *flacdec;
  GstBuffer *outbuf;
  guint depth = frame->header.bits_per_sample;
  guint channels = frame->header.channels;
  guint samples = frame->header.blocksize;
  guint j, i;

  flacdec = GST_FLACDEC (client_data);

  if (flacdec->need_discont) {
    gint64 time = 0, bytes = 0;
    GstFormat format;
    GstEvent *discont;

    flacdec->need_discont = FALSE;
    
    if (!GST_PAD_CAPS (flacdec->srcpad)) {
      if (flacdec->seek_pending) {
        flacdec->total_samples = flacdec->seek_value;
      }

      if (GST_PAD_IS_USABLE (flacdec->srcpad)) {
        GST_DEBUG ("send discont");

        format = GST_FORMAT_TIME;
        gst_pad_convert (flacdec->srcpad, GST_FORMAT_DEFAULT,
                         flacdec->total_samples, &format, &time);
        format = GST_FORMAT_BYTES;
        gst_pad_convert (flacdec->srcpad, GST_FORMAT_DEFAULT,
                         flacdec->total_samples, &format, &bytes);
        discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, time,
	  	                             GST_FORMAT_BYTES, bytes,
		                             GST_FORMAT_DEFAULT, flacdec->total_samples, 
					 NULL);
	  
        gst_pad_push (flacdec->srcpad, GST_DATA (discont));
      }
    }
  }
  
  if (!GST_PAD_CAPS (flacdec->srcpad)) {
    gst_pad_set_explicit_caps (flacdec->srcpad,
        gst_caps_new_simple ("audio/x-raw-int",
          "endianness",  G_TYPE_INT, G_BYTE_ORDER,
          "signed",      G_TYPE_BOOLEAN, TRUE,
          "width",       G_TYPE_INT, depth,
          "depth",       G_TYPE_INT, depth,
          "rate",     	 G_TYPE_INT, frame->header.sample_rate,
          "channels", 	 G_TYPE_INT, channels,
          NULL));

    flacdec->depth = depth;
    flacdec->channels = channels;
    flacdec->frequency = frame->header.sample_rate;
  }

  if (GST_PAD_IS_USABLE (flacdec->srcpad)) {
    outbuf = gst_buffer_new ();
    GST_BUFFER_SIZE (outbuf) = samples * channels * ((depth+7)>>3);
    GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
    GST_BUFFER_TIMESTAMP (outbuf) = flacdec->total_samples * GST_SECOND / frame->header.sample_rate;
  
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
      return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    }
    gst_pad_push (flacdec->srcpad, GST_DATA (outbuf));
  }
  flacdec->total_samples += samples;

  return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static void 
gst_flacdec_loop (GstElement *element) 
{
  FlacDec *flacdec;
  gboolean res;

  flacdec = GST_FLACDEC (element);

  GST_DEBUG ("flacdec: entering loop");
  if (flacdec->init) {    
    FLAC__StreamDecoderState res;
    GST_DEBUG ("flacdec: initializing decoder");    
    res = FLAC__seekable_stream_decoder_init (flacdec->decoder);
    if (res != FLAC__SEEKABLE_STREAM_DECODER_OK) {
      GST_ELEMENT_ERROR (flacdec, LIBRARY, INIT, (NULL),
                         (FLAC__SeekableStreamDecoderStateString[res]));
      return;
    }
    /*    FLAC__seekable_stream_decoder_process_metadata (flacdec->decoder);*/
    flacdec->init = FALSE;
  }

  if (flacdec->seek_pending) {
    GST_DEBUG ("perform seek to sample %" G_GINT64_FORMAT, 
		              flacdec->seek_value);

    if (FLAC__seekable_stream_decoder_seek_absolute (flacdec->decoder, 
			                             flacdec->seek_value)) 
    {
      flacdec->total_samples = flacdec->seek_value;
      GST_DEBUG ("seek done");
    }
    else {
      GST_DEBUG ("seek failed");
    }
    flacdec->seek_pending = FALSE;
  }

  GST_DEBUG ("flacdec: processing single");
  res = FLAC__seekable_stream_decoder_process_single (flacdec->decoder);
  GST_DEBUG ("flacdec: checking for EOS");
  if (FLAC__seekable_stream_decoder_get_state (flacdec->decoder) == 
		  FLAC__SEEKABLE_STREAM_DECODER_END_OF_STREAM) 
  {
    GstEvent *event;

    GST_DEBUG ("flacdec: sending EOS event");
    FLAC__seekable_stream_decoder_reset(flacdec->decoder);

    if (GST_PAD_IS_USABLE (flacdec->srcpad)) {
      event = gst_event_new (GST_EVENT_EOS);
      gst_pad_push (flacdec->srcpad, GST_DATA (event));
    }
    gst_element_set_eos (element);
  }
  GST_DEBUG ("flacdec: _loop end");
}

GST_PAD_FORMATS_FUNCTION (gst_flacdec_get_src_formats,
  GST_FORMAT_DEFAULT,
  GST_FORMAT_BYTES,
  GST_FORMAT_TIME
)

static gboolean
gst_flacdec_convert_src (GstPad *pad, GstFormat src_format, gint64 src_value,
		         GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  FlacDec *flacdec = GST_FLACDEC (gst_pad_get_parent (pad));
  guint scale = 1;
  gint bytes_per_sample;

  bytes_per_sample = flacdec->channels * ((flacdec->depth+7)>>3);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
	{
          gint byterate = bytes_per_sample * flacdec->frequency;

          if (byterate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / byterate;
          break;
	}
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
	  if (flacdec->frequency == 0)
	    return FALSE;
          *dest_value = src_value * GST_SECOND / flacdec->frequency;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
        case GST_FORMAT_DEFAULT:
	  *dest_value = src_value * scale * flacdec->frequency / GST_SECOND;
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  return res;
}

GST_PAD_QUERY_TYPE_FUNCTION (gst_flacdec_get_src_query_types,
  GST_QUERY_TOTAL,
  GST_QUERY_POSITION
)

static gboolean
gst_flacdec_src_query (GstPad *pad, GstQueryType type,
	               GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  FlacDec *flacdec = GST_FLACDEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
    {
      guint64 samples;

      if (flacdec->stream_samples == 0)
        samples = flacdec->total_samples;
      else
        samples = flacdec->stream_samples;

      gst_pad_convert (flacdec->srcpad, 
		       GST_FORMAT_DEFAULT, 
		       samples, 
		       format, value);
      break;
    }
    case GST_QUERY_POSITION:
      gst_pad_convert (flacdec->srcpad, 
		       GST_FORMAT_DEFAULT, 
		       flacdec->total_samples, 
		       format, value);
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}
	  
GST_PAD_EVENT_MASK_FUNCTION (gst_flacdec_get_src_event_masks,
    { GST_EVENT_SEEK, GST_SEEK_FLAG_ACCURATE }
);

static gboolean
gst_flacdec_src_event (GstPad *pad, GstEvent *event)
{ 
  gboolean res = TRUE;
  FlacDec *flacdec = GST_FLACDEC (gst_pad_get_parent (pad));
  GstFormat format;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      format = GST_FORMAT_DEFAULT;

      if (gst_pad_convert (flacdec->srcpad, 
			   GST_EVENT_SEEK_FORMAT (event), 
			   GST_EVENT_SEEK_OFFSET (event), 
			   &format, &flacdec->seek_value))
        flacdec->seek_pending = TRUE;
      else
	res = FALSE;
      break;
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
  return res;
}

static GstElementStateReturn
gst_flacdec_change_state (GstElement *element)
{
  FlacDec *flacdec = GST_FLACDEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      flacdec->bs = gst_bytestream_new (flacdec->sinkpad);
      flacdec->seek_pending = FALSE;
      flacdec->total_samples = 0;
      flacdec->eos = FALSE;
      if (flacdec->init == FALSE) {
	FLAC__seekable_stream_decoder_reset (flacdec->decoder);
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      flacdec->eos = FALSE;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (flacdec->bs);
      break;
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
