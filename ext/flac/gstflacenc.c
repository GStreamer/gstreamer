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

extern GstPadTemplate *gst_flacenc_src_template, *gst_flacenc_sink_template;

/* elementfactory information */
GstElementDetails flacenc_details = {
  "FLAC encoder",
  "Codec/Audio/Encoder",
  "LGPL",
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
  ARG_QUALITY,
  ARG_STREAMABLE_SUBSET,
  ARG_MID_SIDE_STEREO,
  ARG_LOOSE_MID_SIDE_STEREO,
  ARG_BLOCKSIZE,
  ARG_MAX_LPC_ORDER,
  ARG_QLP_COEFF_PRECISION,
  ARG_QLP_COEFF_PREC_SEARCH,
  ARG_ESCAPE_CODING,
  ARG_EXHAUSTIVE_MODEL_SEARCH,
  ARG_MIN_RESIDUAL_PARTITION_ORDER,
  ARG_MAX_RESIDUAL_PARTITION_ORDER,
  ARG_RICE_PARAMETER_SEARCH_DIST,
};

static void		gst_flacenc_init		(FlacEnc *flacenc);
static void		gst_flacenc_class_init		(FlacEncClass *klass);
static void 		gst_flacenc_dispose 		(GObject *object);

static GstPadConnectReturn
			gst_flacenc_sinkconnect 	(GstPad *pad, GstCaps *caps);
static void		gst_flacenc_chain		(GstPad *pad, GstBuffer *buf);

static gboolean 	gst_flacenc_update_quality 	(FlacEnc *flacenc, gint quality);
static void     	gst_flacenc_set_property        (GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void     	gst_flacenc_get_property        (GObject *object, guint prop_id,
							 GValue *value, GParamSpec *pspec);
static GstElementStateReturn
			gst_flacenc_change_state 	(GstElement *element);

static FLAC__StreamEncoderWriteStatus 
			gst_flacenc_write_callback 	(const FLAC__StreamEncoder *encoder, 
							 const FLAC__byte buffer[], unsigned bytes, 
				    			 unsigned samples, unsigned current_frame, 
							 void *client_data);
static void 		gst_flacenc_metadata_callback 	(const FLAC__StreamEncoder *encoder, 
							 const FLAC__StreamMetadata *metadata, 
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

typedef struct {
  gboolean 	exhaustive_model_search;
  gboolean 	escape_coding;
  gboolean 	mid_side;
  gboolean 	loose_mid_side;
  guint  	qlp_coeff_precision;
  gboolean	qlp_coeff_prec_search;
  guint 	min_residual_partition_order;
  guint 	max_residual_partition_order;
  guint 	rice_parameter_search_dist;
  guint 	max_lpc_order;
  guint		blocksize;
} FlacEncParams;

static const FlacEncParams flacenc_params[] = 
{
  { FALSE, FALSE, FALSE, FALSE, 0, FALSE, 2, 2,  0, 0,  1152 },
  { FALSE, FALSE, TRUE,  TRUE,  0, FALSE, 2, 2,  0, 0,  1152 },
  { FALSE, FALSE, TRUE,  FALSE, 0, FALSE, 0, 3,  0, 0,  1152 },
  { FALSE, FALSE, FALSE, FALSE, 0, FALSE, 3, 3,  0, 6,  4608 },
  { FALSE, FALSE, TRUE,  TRUE,  0, FALSE, 3, 3,  0, 8,  4608 },
  { FALSE, FALSE, TRUE,  FALSE, 0, FALSE, 3, 3,  0, 8,  4608 },
  { FALSE, FALSE, TRUE,  FALSE, 0, FALSE, 0, 4,  0, 8,  4608 },
  { TRUE,  FALSE, TRUE,  FALSE, 0, FALSE, 0, 6,  0, 8,  4608 },
  { TRUE,  FALSE, TRUE,  FALSE, 0, FALSE, 0, 6,  0, 12, 4608 },
  { TRUE,  TRUE,  TRUE,  FALSE, 0, FALSE, 0, 16, 0, 32, 4608 },
};

#define DEFAULT_QUALITY 5

#define GST_TYPE_FLACENC_QUALITY (gst_flacenc_quality_get_type ())
GType
gst_flacenc_quality_get_type (void)
{
  static GType qtype = 0;
  if (qtype == 0) {
    static const GEnumValue values[] = {
      { 0, "0", "0 - Fastest compression" },
      { 1, "1", "1" },
      { 2, "2", "2" },
      { 3, "3", "3" },
      { 4, "4", "4" },
      { 5, "5", "5 - Default" },
      { 6, "6", "6" },
      { 7, "7", "7" },
      { 8, "8", "8 - Highest compression " },
      { 9, "9", "9 - Insane" },
      { 0, NULL, NULL }
    };
    qtype = g_enum_register_static ("FlacEncQuality", values);
  }
  return qtype;
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
  gobject_class->dispose      = gst_flacenc_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
    g_param_spec_enum ("quality", 
	               "Quality", 
		       "Speed versus compression tradeoff",
                       GST_TYPE_FLACENC_QUALITY, DEFAULT_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_STREAMABLE_SUBSET,
    g_param_spec_boolean ("streamable_subset", 
	                  "Streamable subset", 
			  "true to limit encoder to generating a Subset stream, else false",
                          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MID_SIDE_STEREO,
    g_param_spec_boolean ("mid_side_stereo", 
	                  "Do mid side stereo", 
	                  "Do mid side stereo (only for stereo input)", 
                          flacenc_params[DEFAULT_QUALITY].mid_side, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LOOSE_MID_SIDE_STEREO,
    g_param_spec_boolean ("loose_mid_side_stereo", 
	                  "Loose mid side stereo", 
	                  "Loose mid side stereo", 
                          flacenc_params[DEFAULT_QUALITY].loose_mid_side, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
    g_param_spec_uint ("blocksize", 
	               "Blocksize", 
	               "Blocksize in samples",
                       FLAC__MIN_BLOCK_SIZE, FLAC__MAX_BLOCK_SIZE, 
                       flacenc_params[DEFAULT_QUALITY].blocksize, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_LPC_ORDER,
    g_param_spec_uint ("max_lpc_order", 
	               "Max LPC order", 
	               "Max LPC order; 0 => use only fixed predictors",
                       0, FLAC__MAX_LPC_ORDER,
                       flacenc_params[DEFAULT_QUALITY].max_lpc_order, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QLP_COEFF_PRECISION,
    g_param_spec_uint ("qlp_coeff_precision", 
	               "QLP coefficients precision", 
	               "Precision in bits of quantized linear-predictor coefficients; 0 = automatic",
                       0, 32, 
                       flacenc_params[DEFAULT_QUALITY].qlp_coeff_precision, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QLP_COEFF_PREC_SEARCH,
    g_param_spec_boolean ("qlp_coeff_prec_search", 
	                  "Do QLP coefficients precision search", 
	                  "false = use qlp_coeff_precision, "
			    "true = search around qlp_coeff_precision, take best", 
                          flacenc_params[DEFAULT_QUALITY].qlp_coeff_prec_search, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ESCAPE_CODING,
    g_param_spec_boolean ("escape_coding", 
	                  "Do Escape coding", 
	                  "search for escape codes in the entropy coding stage "
			    "for slightly better compression", 
                          flacenc_params[DEFAULT_QUALITY].escape_coding, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_EXHAUSTIVE_MODEL_SEARCH,
    g_param_spec_boolean ("exhaustive_model_search", 
	                  "Do exhaustive model search", 
	                  "do exhaustive search of LP coefficient quantization (expensive!)",
                          flacenc_params[DEFAULT_QUALITY].exhaustive_model_search, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MIN_RESIDUAL_PARTITION_ORDER,
    g_param_spec_uint ("min_residual_partition_order", 
	               "Min residual partition order", 
	               "Min residual partition order (above 4 doesn't usually help much)",
                       0, 16, 
                       flacenc_params[DEFAULT_QUALITY].min_residual_partition_order, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_RESIDUAL_PARTITION_ORDER,
    g_param_spec_uint ("max_residual_partition_order", 
	               "Max residual partition order", 
	               "Max residual partition order (above 4 doesn't usually help much)",
                       0, 16, 
                       flacenc_params[DEFAULT_QUALITY].max_residual_partition_order, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_RICE_PARAMETER_SEARCH_DIST,
    g_param_spec_uint ("rice_parameter_search_dist", 
	               "rice_parameter_search_dist", 
	               "0 = try only calc'd parameter k; else try all [k-dist..k+dist] "
		         "parameters, use best",
                       0, FLAC__MAX_RICE_PARTITION_ORDER, 
                       flacenc_params[DEFAULT_QUALITY].rice_parameter_search_dist, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_flacenc_change_state;
}

static void
gst_flacenc_init (FlacEnc *flacenc)
{
  flacenc->sinkpad = gst_pad_new_from_template (gst_flacenc_sink_template, "sink");
  gst_element_add_pad(GST_ELEMENT(flacenc),flacenc->sinkpad);
  gst_pad_set_chain_function(flacenc->sinkpad,gst_flacenc_chain);
  gst_pad_set_connect_function (flacenc->sinkpad, gst_flacenc_sinkconnect);

  flacenc->srcpad = gst_pad_new_from_template (gst_flacenc_src_template, "src");
  gst_element_add_pad(GST_ELEMENT(flacenc),flacenc->srcpad);

  GST_FLAG_SET (flacenc, GST_ELEMENT_EVENT_AWARE);

  flacenc->encoder = FLAC__stream_encoder_new();

  FLAC__stream_encoder_set_write_callback (flacenc->encoder, 
		                        gst_flacenc_write_callback);
  FLAC__stream_encoder_set_metadata_callback (flacenc->encoder, 
		                        gst_flacenc_metadata_callback);
  FLAC__stream_encoder_set_client_data (flacenc->encoder, 
		                        flacenc);

  flacenc->negotiated = FALSE;
  flacenc->first = TRUE;
  flacenc->first_buf = NULL;
  flacenc->data = NULL;
  gst_flacenc_update_quality (flacenc, DEFAULT_QUALITY);
}

static void
gst_flacenc_dispose (GObject *object)
{
  FlacEnc *flacenc = GST_FLACENC (object);

  FLAC__stream_encoder_delete (flacenc->encoder);
  
  G_OBJECT_CLASS (parent_class)->dispose (object);
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

  FLAC__stream_encoder_set_bits_per_sample (flacenc->encoder, 
		  			    flacenc->depth);
  FLAC__stream_encoder_set_sample_rate (flacenc->encoder, 
		  			flacenc->sample_rate);
  FLAC__stream_encoder_set_channels (flacenc->encoder, 
		  		     flacenc->channels);

  flacenc->negotiated = TRUE;

  return GST_PAD_CONNECT_OK;
}

static gboolean
gst_flacenc_update_quality (FlacEnc *flacenc, gint quality)
{
  flacenc->quality = quality;

#define DO_UPDATE(name, val, str) 	                    	\
G_STMT_START{                                                   \
  if (FLAC__stream_encoder_get_##name (flacenc->encoder) != 	\
      flacenc_params[quality].##val) {        			\
    FLAC__stream_encoder_set_##name (flacenc->encoder, 		\
      flacenc_params[quality].##val);				\
    g_object_notify (G_OBJECT (flacenc), str);                  \
  };                                                            \
} G_STMT_END

  g_object_freeze_notify (G_OBJECT (flacenc));

  DO_UPDATE (do_mid_side_stereo,           mid_side,                     "mid_side_stereo");
  DO_UPDATE (loose_mid_side_stereo,        loose_mid_side,               "loose_mid_side");
  DO_UPDATE (blocksize,                    blocksize,                    "blocksize");
  DO_UPDATE (max_lpc_order,                max_lpc_order,                "max_lpc_order");
  DO_UPDATE (qlp_coeff_precision,          qlp_coeff_precision,          "qlp_coeff_precision");
  DO_UPDATE (do_qlp_coeff_prec_search,     qlp_coeff_prec_search,        "qlp_coeff_prec_search");
  DO_UPDATE (do_escape_coding,             escape_coding,                "escape_coding");
  DO_UPDATE (do_exhaustive_model_search,   exhaustive_model_search,      "exhaustive_model_search");
  DO_UPDATE (min_residual_partition_order, min_residual_partition_order, "min_residual_partition_order");
  DO_UPDATE (max_residual_partition_order, max_residual_partition_order, "max_residual_partition_order");
  DO_UPDATE (rice_parameter_search_dist,   rice_parameter_search_dist,   "rice_parameter_search_dist");

#undef DO_UPDATE

  g_object_thaw_notify (G_OBJECT (flacenc));

  return TRUE;
}

static FLAC__StreamEncoderWriteStatus 
gst_flacenc_write_callback (const FLAC__StreamEncoder *encoder, 
			    const FLAC__byte buffer[], unsigned bytes, 
			    unsigned samples, unsigned current_frame, 
			    void *client_data)
{
  FlacEnc *flacenc;
  GstBuffer *outbuf;

  flacenc = GST_FLACENC (client_data);

  if (flacenc->stopped) 
    return FLAC__STREAM_ENCODER_WRITE_OK;

  outbuf = gst_buffer_new_and_alloc (bytes);

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
gst_flacenc_metadata_callback (const FLAC__StreamEncoder *encoder, 
		               const FLAC__StreamMetadata *metadata, 
			       void *client_data)
{
  GstEvent *event;
  FlacEnc *flacenc;

  flacenc = GST_FLACENC (client_data);

  if (flacenc->stopped) 
    return;

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
  FLAC__int32 *data;
  gulong insize;
  gint samples, depth;
  gulong i;
  FLAC__bool res;

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

  if (!flacenc->negotiated) {
    gst_element_error (GST_ELEMENT (flacenc),
		    "format not negotiated");
    return;
  }

  depth = flacenc->depth;

  insize = GST_BUFFER_SIZE (buf);
  samples = insize / ((depth+7)>>3);

  if (FLAC__stream_encoder_get_state (flacenc->encoder) == 
		  FLAC__STREAM_ENCODER_UNINITIALIZED) 
  {
    FLAC__StreamEncoderState state;

    state = FLAC__stream_encoder_init (flacenc->encoder);
    if (state != FLAC__STREAM_ENCODER_OK) {
      gst_element_error (GST_ELEMENT (flacenc),
		         "could not initialize encoder (wrong parameters?)");
      return;
    }
  }

  /* we keep a pointer in the flacenc struct because we are freeing the data
   * after a push opreration that might never return */
  data = flacenc->data = g_malloc (samples * sizeof (FLAC__int32));
    
  if (depth == 8) {
    gint8 *indata = (gint8 *) GST_BUFFER_DATA (buf);
    
    for (i=0; i<samples; i++) {
      data[i] = (FLAC__int32) *indata++;
    }
  }
  else if (depth == 16) {
    gint16 *indata = (gint16 *) GST_BUFFER_DATA (buf);

    for (i=0; i<samples; i++) {
      data[i] = (FLAC__int32) *indata++;
    }
  }

  gst_buffer_unref(buf);

  res = FLAC__stream_encoder_process_interleaved (flacenc->encoder, 
	                      (const FLAC__int32 *) data, samples / flacenc->channels);

  g_free (flacenc->data);
  flacenc->data = NULL;

  if (!res) {
    gst_element_error (GST_ELEMENT (flacenc),
		         "encoding error");
  }
}

static void
gst_flacenc_set_property (GObject *object, guint prop_id,
		          const GValue *value, GParamSpec *pspec)
{
  FlacEnc *this;
  
  this = (FlacEnc *)object;
  switch (prop_id) {
    case ARG_QUALITY:
      gst_flacenc_update_quality (this, g_value_get_enum (value));
      break;
    case ARG_STREAMABLE_SUBSET:
      FLAC__stream_encoder_set_streamable_subset (this->encoder, 
	                   g_value_get_boolean (value));
      break;
    case ARG_MID_SIDE_STEREO:
      FLAC__stream_encoder_set_do_mid_side_stereo (this->encoder, 
	                   g_value_get_boolean (value));
      break;
    case ARG_LOOSE_MID_SIDE_STEREO:
      FLAC__stream_encoder_set_loose_mid_side_stereo (this->encoder, 
	                   g_value_get_boolean (value));
      break;
    case ARG_BLOCKSIZE:
      FLAC__stream_encoder_set_blocksize (this->encoder, 
	                   g_value_get_uint (value));
      break;
    case ARG_MAX_LPC_ORDER:
      FLAC__stream_encoder_set_max_lpc_order (this->encoder, 
	                   g_value_get_uint (value));
      break;
    case ARG_QLP_COEFF_PRECISION:
      FLAC__stream_encoder_set_qlp_coeff_precision (this->encoder, 
	                   g_value_get_uint (value));
      break;
    case ARG_QLP_COEFF_PREC_SEARCH:
      FLAC__stream_encoder_set_do_qlp_coeff_prec_search (this->encoder, 
	                   g_value_get_boolean (value));
      break;
    case ARG_ESCAPE_CODING:
      FLAC__stream_encoder_set_do_escape_coding (this->encoder, 
	                   g_value_get_boolean (value));
      break;
    case ARG_EXHAUSTIVE_MODEL_SEARCH:
      FLAC__stream_encoder_set_do_exhaustive_model_search (this->encoder, 
	                   g_value_get_boolean (value));
      break;
    case ARG_MIN_RESIDUAL_PARTITION_ORDER:
      FLAC__stream_encoder_set_min_residual_partition_order (this->encoder, 
	                   g_value_get_uint (value));
      break;
    case ARG_MAX_RESIDUAL_PARTITION_ORDER:
      FLAC__stream_encoder_set_max_residual_partition_order (this->encoder, 
	                   g_value_get_uint (value));
      break;
    case ARG_RICE_PARAMETER_SEARCH_DIST:
      FLAC__stream_encoder_set_rice_parameter_search_dist (this->encoder, 
		                                 g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }
}

static void
gst_flacenc_get_property (GObject *object, guint prop_id, 
		          GValue *value, GParamSpec *pspec)
{
  FlacEnc *this;
  
  this = (FlacEnc *)object;
  
  switch (prop_id) {
    case ARG_QUALITY:
      g_value_set_enum (value, this->quality);
      break;
    case ARG_STREAMABLE_SUBSET:
      g_value_set_boolean (value, 
	      FLAC__stream_encoder_get_streamable_subset (this->encoder));
      break;
    case ARG_MID_SIDE_STEREO:
      g_value_set_boolean (value, 
	      FLAC__stream_encoder_get_do_mid_side_stereo (this->encoder));
      break;
    case ARG_LOOSE_MID_SIDE_STEREO:
      g_value_set_boolean (value, 
	      FLAC__stream_encoder_get_loose_mid_side_stereo (this->encoder));
      break;
    case ARG_BLOCKSIZE:
      g_value_set_uint (value, 
	      FLAC__stream_encoder_get_blocksize (this->encoder));
      break;
    case ARG_MAX_LPC_ORDER:
      g_value_set_uint (value, 
	      FLAC__stream_encoder_get_max_lpc_order (this->encoder));
      break;
    case ARG_QLP_COEFF_PRECISION:
      g_value_set_uint (value, 
	      FLAC__stream_encoder_get_qlp_coeff_precision (this->encoder));
      break;
    case ARG_QLP_COEFF_PREC_SEARCH:
      g_value_set_boolean (value, 
	      FLAC__stream_encoder_get_do_qlp_coeff_prec_search (this->encoder));
      break;
    case ARG_ESCAPE_CODING:
      g_value_set_boolean (value, 
	      FLAC__stream_encoder_get_do_escape_coding (this->encoder));
      break;
    case ARG_EXHAUSTIVE_MODEL_SEARCH:
      g_value_set_boolean (value, 
	      FLAC__stream_encoder_get_do_exhaustive_model_search (this->encoder));
      break;
    case ARG_MIN_RESIDUAL_PARTITION_ORDER:
      g_value_set_uint (value, 
	      FLAC__stream_encoder_get_min_residual_partition_order (this->encoder));
      break;
    case ARG_MAX_RESIDUAL_PARTITION_ORDER:
      g_value_set_uint (value, 
	      FLAC__stream_encoder_get_max_residual_partition_order (this->encoder));
      break;
    case ARG_RICE_PARAMETER_SEARCH_DIST:
      g_value_set_uint (value, 
	      FLAC__stream_encoder_get_rice_parameter_search_dist (this->encoder));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_flacenc_change_state (GstElement *element)
{
  FlacEnc *flacenc = GST_FLACENC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
    case GST_STATE_READY_TO_PAUSED:
      flacenc->first = TRUE;
      flacenc->stopped = FALSE;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (FLAC__stream_encoder_get_state (flacenc->encoder) != 
		        FLAC__STREAM_ENCODER_UNINITIALIZED) {
        flacenc->stopped = TRUE;
        FLAC__stream_encoder_finish (flacenc->encoder);
      }
      flacenc->negotiated = FALSE;
      if (flacenc->first_buf)
	gst_buffer_unref (flacenc->first_buf);
      flacenc->first_buf = NULL;
      g_free (flacenc->data);
      flacenc->data = NULL;
      break;
    case GST_STATE_READY_TO_NULL:
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

