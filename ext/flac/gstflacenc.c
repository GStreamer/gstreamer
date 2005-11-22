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
#include <stdlib.h>
#include <string.h>

#include <gstflacenc.h>
#include <gst/audio/audio.h>
#include <gst/tag/tag.h>
#include <gst/gsttagsetter.h>
#include "flac_compat.h"


GstElementDetails flacenc_details = {
  "FLAC encoder",
  "Codec/Encoder/Audio",
  "Encodes audio with the FLAC lossless audio encoder",
  "Wim Taymans <wim.taymans@chello.be>",
};

#define FLAC_SINK_CAPS \
  "audio/x-raw-int, "               \
  "endianness = (int) BYTE_ORDER, " \
  "signed = (boolean) TRUE, "       \
  "width = (int) 16, "              \
  "depth = (int) 16, "              \
  "rate = (int) [ 11025, 48000 ], " \
  "channels = (int) [ 1, 2 ]"

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-flac")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (FLAC_SINK_CAPS)
    );

enum
{
  PROP_0,
  PROP_QUALITY,
  PROP_STREAMABLE_SUBSET,
  PROP_MID_SIDE_STEREO,
  PROP_LOOSE_MID_SIDE_STEREO,
  PROP_BLOCKSIZE,
  PROP_MAX_LPC_ORDER,
  PROP_QLP_COEFF_PRECISION,
  PROP_QLP_COEFF_PREC_SEARCH,
  PROP_ESCAPE_CODING,
  PROP_EXHAUSTIVE_MODEL_SEARCH,
  PROP_MIN_RESIDUAL_PARTITION_ORDER,
  PROP_MAX_RESIDUAL_PARTITION_ORDER,
  PROP_RICE_PARAMETER_SEARCH_DIST
};

GST_DEBUG_CATEGORY_STATIC (flacenc_debug);
#define GST_CAT_DEFAULT flacenc_debug


#define _do_init(type)                                                          \
  G_STMT_START{                                                                 \
    static const GInterfaceInfo tag_setter_info = {                             \
      NULL,                                                                     \
      NULL,                                                                     \
      NULL                                                                      \
    };                                                                          \
    g_type_add_interface_static (type, GST_TYPE_TAG_SETTER,                     \
                                 &tag_setter_info);                             \
  }G_STMT_END

GST_BOILERPLATE_FULL (GstFlacEnc, gst_flacenc, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void gst_flacenc_finalize (GObject * object);

static gboolean gst_flacenc_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_flacenc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_flacenc_chain (GstPad * pad, GstBuffer * buffer);

static gboolean gst_flacenc_update_quality (GstFlacEnc * flacenc, gint quality);
static void gst_flacenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_flacenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_flacenc_change_state (GstElement * element,
    GstStateChange transition);

static FLAC__StreamEncoderWriteStatus
gst_flacenc_write_callback (const FLAC__SeekableStreamEncoder * encoder,
    const FLAC__byte buffer[], unsigned bytes,
    unsigned samples, unsigned current_frame, void *client_data);
static FLAC__SeekableStreamEncoderSeekStatus
gst_flacenc_seek_callback (const FLAC__SeekableStreamEncoder * encoder,
    FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__SeekableStreamEncoderTellStatus
gst_flacenc_tell_callback (const FLAC__SeekableStreamEncoder * encoder,
    FLAC__uint64 * absolute_byte_offset, void *client_data);

typedef struct
{
  gboolean exhaustive_model_search;
  gboolean escape_coding;
  gboolean mid_side;
  gboolean loose_mid_side;
  guint qlp_coeff_precision;
  gboolean qlp_coeff_prec_search;
  guint min_residual_partition_order;
  guint max_residual_partition_order;
  guint rice_parameter_search_dist;
  guint max_lpc_order;
  guint blocksize;
}
GstFlacEncParams;

static const GstFlacEncParams flacenc_params[] = {
  {FALSE, FALSE, FALSE, FALSE, 0, FALSE, 2, 2, 0, 0, 1152},
  {FALSE, FALSE, TRUE, TRUE, 0, FALSE, 2, 2, 0, 0, 1152},
  {FALSE, FALSE, TRUE, FALSE, 0, FALSE, 0, 3, 0, 0, 1152},
  {FALSE, FALSE, FALSE, FALSE, 0, FALSE, 3, 3, 0, 6, 4608},
  {FALSE, FALSE, TRUE, TRUE, 0, FALSE, 3, 3, 0, 8, 4608},
  {FALSE, FALSE, TRUE, FALSE, 0, FALSE, 3, 3, 0, 8, 4608},
  {FALSE, FALSE, TRUE, FALSE, 0, FALSE, 0, 4, 0, 8, 4608},
  {TRUE, FALSE, TRUE, FALSE, 0, FALSE, 0, 6, 0, 8, 4608},
  {TRUE, FALSE, TRUE, FALSE, 0, FALSE, 0, 6, 0, 12, 4608},
  {TRUE, TRUE, TRUE, FALSE, 0, FALSE, 0, 16, 0, 32, 4608},
};

#define DEFAULT_QUALITY 5

#define GST_TYPE_FLACENC_QUALITY (gst_flacenc_quality_get_type ())
GType
gst_flacenc_quality_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {0, "0", "0 - Fastest compression"},
      {1, "1", "1"},
      {2, "2", "2"},
      {3, "3", "3"},
      {4, "4", "4"},
      {5, "5", "5 - Default"},
      {6, "6", "6"},
      {7, "7", "7"},
      {8, "8", "8 - Highest compression "},
      {9, "9", "9 - Insane"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstFlacEncQuality", values);
  }
  return qtype;
}

static void
gst_flacenc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_details (element_class, &flacenc_details);

  GST_DEBUG_CATEGORY_INIT (flacenc_debug, "flacenc", 0,
      "Flac encoding element");
}

static void
gst_flacenc_class_init (GstFlacEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_flacenc_set_property;
  gobject_class->get_property = gst_flacenc_get_property;
  gobject_class->finalize = gst_flacenc_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QUALITY,
      g_param_spec_enum ("quality",
          "Quality",
          "Speed versus compression tradeoff",
          GST_TYPE_FLACENC_QUALITY, DEFAULT_QUALITY, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_STREAMABLE_SUBSET, g_param_spec_boolean ("streamable_subset",
          "Streamable subset",
          "true to limit encoder to generating a Subset stream, else false",
          TRUE, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MID_SIDE_STEREO,
      g_param_spec_boolean ("mid_side_stereo", "Do mid side stereo",
          "Do mid side stereo (only for stereo input)",
          flacenc_params[DEFAULT_QUALITY].mid_side, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_LOOSE_MID_SIDE_STEREO, g_param_spec_boolean ("loose_mid_side_stereo",
          "Loose mid side stereo", "Loose mid side stereo",
          flacenc_params[DEFAULT_QUALITY].loose_mid_side, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCKSIZE,
      g_param_spec_uint ("blocksize", "Blocksize", "Blocksize in samples",
          FLAC__MIN_BLOCK_SIZE, FLAC__MAX_BLOCK_SIZE,
          flacenc_params[DEFAULT_QUALITY].blocksize, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_LPC_ORDER,
      g_param_spec_uint ("max_lpc_order", "Max LPC order",
          "Max LPC order; 0 => use only fixed predictors", 0,
          FLAC__MAX_LPC_ORDER, flacenc_params[DEFAULT_QUALITY].max_lpc_order,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_QLP_COEFF_PRECISION, g_param_spec_uint ("qlp_coeff_precision",
          "QLP coefficients precision",
          "Precision in bits of quantized linear-predictor coefficients; 0 = automatic",
          0, 32, flacenc_params[DEFAULT_QUALITY].qlp_coeff_precision,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_QLP_COEFF_PREC_SEARCH, g_param_spec_boolean ("qlp_coeff_prec_search",
          "Do QLP coefficients precision search",
          "false = use qlp_coeff_precision, "
          "true = search around qlp_coeff_precision, take best",
          flacenc_params[DEFAULT_QUALITY].qlp_coeff_prec_search,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ESCAPE_CODING,
      g_param_spec_boolean ("escape_coding", "Do Escape coding",
          "search for escape codes in the entropy coding stage "
          "for slightly better compression",
          flacenc_params[DEFAULT_QUALITY].escape_coding, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_EXHAUSTIVE_MODEL_SEARCH,
      g_param_spec_boolean ("exhaustive_model_search",
          "Do exhaustive model search",
          "do exhaustive search of LP coefficient quantization (expensive!)",
          flacenc_params[DEFAULT_QUALITY].exhaustive_model_search,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MIN_RESIDUAL_PARTITION_ORDER,
      g_param_spec_uint ("min_residual_partition_order",
          "Min residual partition order",
          "Min residual partition order (above 4 doesn't usually help much)", 0,
          16, flacenc_params[DEFAULT_QUALITY].min_residual_partition_order,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MAX_RESIDUAL_PARTITION_ORDER,
      g_param_spec_uint ("max_residual_partition_order",
          "Max residual partition order",
          "Max residual partition order (above 4 doesn't usually help much)", 0,
          16, flacenc_params[DEFAULT_QUALITY].max_residual_partition_order,
          G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_RICE_PARAMETER_SEARCH_DIST,
      g_param_spec_uint ("rice_parameter_search_dist",
          "rice_parameter_search_dist",
          "0 = try only calc'd parameter k; else try all [k-dist..k+dist] "
          "parameters, use best", 0, FLAC__MAX_RICE_PARTITION_ORDER,
          flacenc_params[DEFAULT_QUALITY].rice_parameter_search_dist,
          G_PARAM_READWRITE));

  gstelement_class->change_state = gst_flacenc_change_state;
}

static void
gst_flacenc_init (GstFlacEnc * flacenc, GstFlacEncClass * klass)
{
  GstElementClass *eclass = GST_ELEMENT_CLASS (klass);

  flacenc->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (eclass,
          "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (flacenc), flacenc->sinkpad);
  gst_pad_set_chain_function (flacenc->sinkpad, gst_flacenc_chain);
  gst_pad_set_event_function (flacenc->sinkpad, gst_flacenc_sink_event);
  gst_pad_set_setcaps_function (flacenc->sinkpad, gst_flacenc_sink_setcaps);

  flacenc->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (eclass,
          "src"), "src");
  gst_pad_use_fixed_caps (flacenc->srcpad);
  gst_element_add_pad (GST_ELEMENT (flacenc), flacenc->srcpad);

  flacenc->encoder = FLAC__seekable_stream_encoder_new ();

  flacenc->offset = 0;
  flacenc->samples_written = 0;
  gst_flacenc_update_quality (flacenc, DEFAULT_QUALITY);
  flacenc->tags = gst_tag_list_new ();
}

static void
gst_flacenc_finalize (GObject * object)
{
  GstFlacEnc *flacenc = GST_FLACENC (object);

  FLAC__seekable_stream_encoder_delete (flacenc->encoder);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
add_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  GList *comments;
  GList *it;
  GstFlacEnc *flacenc = GST_FLACENC (user_data);

  comments = gst_tag_to_vorbis_comments (list, tag);
  for (it = comments; it != NULL; it = it->next) {
    FLAC__StreamMetadata_VorbisComment_Entry commment_entry;

    commment_entry.length = strlen (it->data);
    commment_entry.entry = it->data;
    FLAC__metadata_object_vorbiscomment_insert_comment (flacenc->meta[0],
        flacenc->meta[0]->data.vorbis_comment.num_comments,
        commment_entry, TRUE);
    g_free (it->data);
  }
  g_list_free (comments);
}

static void
gst_flacenc_set_metadata (GstFlacEnc * flacenc)
{
  const GstTagList *user_tags;
  GstTagList *copy;

  g_return_if_fail (flacenc != NULL);
  user_tags = gst_tag_setter_get_list (GST_TAG_SETTER (flacenc));
  if ((flacenc->tags == NULL) && (user_tags == NULL)) {
    return;
  }
  copy = gst_tag_list_merge (user_tags, flacenc->tags,
      gst_tag_setter_get_merge_mode (GST_TAG_SETTER (flacenc)));
  flacenc->meta = g_malloc (sizeof (FLAC__StreamMetadata **));

  flacenc->meta[0] =
      FLAC__metadata_object_new (FLAC__METADATA_TYPE_VORBIS_COMMENT);
  gst_tag_list_foreach (copy, add_one_tag, flacenc);

  if (FLAC__seekable_stream_encoder_set_metadata (flacenc->encoder,
          flacenc->meta, 1) != true)
    g_warning ("Dude, i'm already initialized!");
  gst_tag_list_free (copy);
}

static gboolean
gst_flacenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstFlacEnc *flacenc;
  GstStructure *structure;
  FLAC__SeekableStreamEncoderState state;

  /* takes a ref on flacenc */
  flacenc = GST_FLACENC (gst_pad_get_parent (pad));

  if (FLAC__seekable_stream_encoder_get_state (flacenc->encoder) !=
      FLAC__SEEKABLE_STREAM_ENCODER_UNINITIALIZED)
    goto encoder_already_initialized;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "channels", &flacenc->channels)
      || !gst_structure_get_int (structure, "depth", &flacenc->depth)
      || !gst_structure_get_int (structure, "rate", &flacenc->sample_rate))
    /* we got caps incompatible with the template? */
    g_return_val_if_reached (FALSE);

  caps = gst_caps_new_simple ("audio/x-flac",
      "channels", G_TYPE_INT, flacenc->channels,
      "rate", G_TYPE_INT, flacenc->sample_rate, NULL);

  if (!gst_pad_set_caps (flacenc->srcpad, caps))
    goto setting_src_caps_failed;

  gst_caps_unref (caps);

  FLAC__seekable_stream_encoder_set_bits_per_sample (flacenc->encoder,
      flacenc->depth);
  FLAC__seekable_stream_encoder_set_sample_rate (flacenc->encoder,
      flacenc->sample_rate);
  FLAC__seekable_stream_encoder_set_channels (flacenc->encoder,
      flacenc->channels);

  FLAC__seekable_stream_encoder_set_write_callback (flacenc->encoder,
      gst_flacenc_write_callback);
  FLAC__seekable_stream_encoder_set_seek_callback (flacenc->encoder,
      gst_flacenc_seek_callback);
  FLAC__seekable_stream_encoder_set_tell_callback (flacenc->encoder,
      gst_flacenc_tell_callback);

  FLAC__seekable_stream_encoder_set_client_data (flacenc->encoder, flacenc);

  gst_flacenc_set_metadata (flacenc);

  state = FLAC__seekable_stream_encoder_init (flacenc->encoder);
  if (state != FLAC__STREAM_ENCODER_OK)
    goto failed_to_initialize;

  gst_object_unref (flacenc);

  return TRUE;

encoder_already_initialized:
  {
    g_warning ("flac already initialized -- fixme allow this");
    gst_object_unref (flacenc);
    return FALSE;
  }
setting_src_caps_failed:
  {
    GST_DEBUG_OBJECT (flacenc,
        "Couldn't set caps on source pad: %" GST_PTR_FORMAT, caps);
    gst_caps_unref (caps);
    gst_object_unref (flacenc);
    return FALSE;
  }
failed_to_initialize:
  {
    GST_ELEMENT_ERROR (flacenc, LIBRARY, INIT, (NULL),
        ("could not initialize encoder (wrong parameters?)"));
    gst_object_unref (flacenc);
    return FALSE;
  }
}

static gboolean
gst_flacenc_update_quality (GstFlacEnc * flacenc, gint quality)
{
  flacenc->quality = quality;

#define DO_UPDATE(name, val, str)                                               \
  G_STMT_START {                                                                \
    if (FLAC__seekable_stream_encoder_get_##name (flacenc->encoder) !=          \
        flacenc_params[quality].val) {                                          \
      FLAC__seekable_stream_encoder_set_##name (flacenc->encoder,               \
          flacenc_params[quality].val);                                         \
      g_object_notify (G_OBJECT (flacenc), str);                                \
    }                                                                           \
  } G_STMT_END

  g_object_freeze_notify (G_OBJECT (flacenc));

  if (flacenc->channels == 2) {
    DO_UPDATE (do_mid_side_stereo, mid_side, "mid_side_stereo");
    DO_UPDATE (loose_mid_side_stereo, loose_mid_side, "loose_mid_side");
  }

  DO_UPDATE (blocksize, blocksize, "blocksize");
  DO_UPDATE (max_lpc_order, max_lpc_order, "max_lpc_order");
  DO_UPDATE (qlp_coeff_precision, qlp_coeff_precision, "qlp_coeff_precision");
  DO_UPDATE (do_qlp_coeff_prec_search, qlp_coeff_prec_search,
      "qlp_coeff_prec_search");
  DO_UPDATE (do_escape_coding, escape_coding, "escape_coding");
  DO_UPDATE (do_exhaustive_model_search, exhaustive_model_search,
      "exhaustive_model_search");
  DO_UPDATE (min_residual_partition_order, min_residual_partition_order,
      "min_residual_partition_order");
  DO_UPDATE (max_residual_partition_order, max_residual_partition_order,
      "max_residual_partition_order");
  DO_UPDATE (rice_parameter_search_dist, rice_parameter_search_dist,
      "rice_parameter_search_dist");

#undef DO_UPDATE

  g_object_thaw_notify (G_OBJECT (flacenc));

  return TRUE;
}

static FLAC__SeekableStreamEncoderSeekStatus
gst_flacenc_seek_callback (const FLAC__SeekableStreamEncoder * encoder,
    FLAC__uint64 absolute_byte_offset, void *client_data)
{
  GstFlacEnc *flacenc;
  GstEvent *event;
  GstPad *peerpad;

  flacenc = GST_FLACENC (client_data);

  if (flacenc->stopped)
    return FLAC__STREAM_ENCODER_OK;

  event = gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_BYTES,
      absolute_byte_offset, GST_BUFFER_OFFSET_NONE, 0);

  if ((peerpad = gst_pad_get_peer (flacenc->srcpad))) {
    gboolean ret = gst_pad_send_event (peerpad, event);

    gst_object_unref (peerpad);

    GST_DEBUG ("Seek to %" G_GUINT64_FORMAT " %s", absolute_byte_offset,
        (ret) ? "succeeded" : "failed");
  } else {
    GST_DEBUG ("Seek to %" G_GUINT64_FORMAT " failed (no peer pad)",
        absolute_byte_offset);
  }

  flacenc->offset = absolute_byte_offset;

  return FLAC__STREAM_ENCODER_OK;
}

static FLAC__StreamEncoderWriteStatus
gst_flacenc_write_callback (const FLAC__SeekableStreamEncoder * encoder,
    const FLAC__byte buffer[], unsigned bytes,
    unsigned samples, unsigned current_frame, void *client_data)
{
  GstFlowReturn ret;
  GstFlacEnc *flacenc;
  GstBuffer *outbuf;

  flacenc = GST_FLACENC (client_data);

  if (flacenc->stopped)
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;

  if (gst_pad_alloc_buffer (flacenc->srcpad, flacenc->offset, bytes,
          GST_PAD_CAPS (flacenc->srcpad), &outbuf) != GST_FLOW_OK) {
    return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;
  }

  memcpy (GST_BUFFER_DATA (outbuf), buffer, bytes);

  if (samples > 0 && flacenc->samples_written != (guint64) - 1) {
    GST_BUFFER_TIMESTAMP (outbuf) =
        GST_FRAMES_TO_CLOCK_TIME (flacenc->samples_written,
        flacenc->sample_rate);
    GST_BUFFER_DURATION (outbuf) =
        GST_FRAMES_TO_CLOCK_TIME (samples, flacenc->sample_rate);
    /* offset_end = granulepos for ogg muxer */
    GST_BUFFER_OFFSET_END (outbuf) = flacenc->samples_written + samples;
  } else {
    GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG ("Pushing buffer: ts=%" GST_TIME_FORMAT ", samples=%u, size=%u, "
      "pos=%" G_GUINT64_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      samples, bytes, flacenc->offset);

  ret = gst_pad_push (flacenc->srcpad, outbuf);

  flacenc->offset += bytes;
  flacenc->samples_written += samples;

  if (ret != GST_FLOW_OK && GST_FLOW_IS_FATAL (ret))
    return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;

  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__SeekableStreamEncoderTellStatus
gst_flacenc_tell_callback (const FLAC__SeekableStreamEncoder * encoder,
    FLAC__uint64 * absolute_byte_offset, void *client_data)
{
  GstFlacEnc *flacenc = GST_FLACENC (client_data);

  *absolute_byte_offset = flacenc->offset;

  return FLAC__STREAM_ENCODER_OK;
}

static gboolean
gst_flacenc_sink_event (GstPad * pad, GstEvent * event)
{
  GstFlacEnc *flacenc;
  GstTagList *taglist;
  gboolean ret = TRUE;

  flacenc = GST_FLACENC (gst_pad_get_parent (pad));

  GST_DEBUG ("Received %s event on sinkpad", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat format;
      gint64 start, stream_time;

      if (flacenc->offset == 0) {
        gst_event_parse_new_segment (event, NULL, NULL, &format, &start, NULL,
            &stream_time);
      } else {
        start = -1;
      }
      if (start != 0) {
        if (flacenc->offset > 0)
          GST_DEBUG ("Not handling mid-stream newsegment event");
        else
          GST_DEBUG ("Not handling newsegment event with non-zero start");
      } else {
        GstEvent *e = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
            0, -1, 0);

        ret = gst_pad_push_event (flacenc->srcpad, e);
      }
      if (stream_time != 0) {
        GST_DEBUG ("Not handling non-zero stream time");
      }
      gst_event_unref (event);
      /* don't push it downstream, we'll generate our own via seek to 0 */
      break;
    }
    case GST_EVENT_EOS:
      FLAC__seekable_stream_encoder_finish (flacenc->encoder);
      ret = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_TAG:
      if (flacenc->tags) {
        gst_event_parse_tag (event, &taglist);
        gst_tag_list_insert (flacenc->tags, taglist, GST_TAG_MERGE_REPLACE);
      } else {
        g_assert_not_reached ();
      }
      ret = gst_pad_event_default (pad, event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (flacenc);

  return ret;
}

static GstFlowReturn
gst_flacenc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlacEnc *flacenc;
  FLAC__int32 *data;
  gulong insize;
  gint samples, depth;
  gulong i;
  FLAC__bool res;

  flacenc = GST_FLACENC (gst_pad_get_parent (pad));

  depth = flacenc->depth;

  insize = GST_BUFFER_SIZE (buffer);
  samples = insize / ((depth + 7) >> 3);

  data = g_malloc (samples * sizeof (FLAC__int32));

  if (depth == 8) {
    gint8 *indata = (gint8 *) GST_BUFFER_DATA (buffer);

    for (i = 0; i < samples; i++)
      data[i] = (FLAC__int32) indata[i];
  } else if (depth == 16) {
    gint16 *indata = (gint16 *) GST_BUFFER_DATA (buffer);

    for (i = 0; i < samples; i++)
      data[i] = (FLAC__int32) indata[i];
  } else {
    g_assert_not_reached ();
  }

  gst_buffer_unref (buffer);

  res = FLAC__seekable_stream_encoder_process_interleaved (flacenc->encoder,
      (const FLAC__int32 *) data, samples / flacenc->channels);

  g_free (data);

  gst_object_unref (flacenc);

  if (res)
    return GST_FLOW_OK;
  else
    return GST_FLOW_ERROR;
}

static void
gst_flacenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFlacEnc *this = GST_FLACENC (object);

  GST_OBJECT_LOCK (this);

  switch (prop_id) {
    case PROP_QUALITY:
      gst_flacenc_update_quality (this, g_value_get_enum (value));
      break;
    case PROP_STREAMABLE_SUBSET:
      FLAC__seekable_stream_encoder_set_streamable_subset (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_MID_SIDE_STEREO:
      FLAC__seekable_stream_encoder_set_do_mid_side_stereo (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_LOOSE_MID_SIDE_STEREO:
      FLAC__seekable_stream_encoder_set_loose_mid_side_stereo (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_BLOCKSIZE:
      FLAC__seekable_stream_encoder_set_blocksize (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_MAX_LPC_ORDER:
      FLAC__seekable_stream_encoder_set_max_lpc_order (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_QLP_COEFF_PRECISION:
      FLAC__seekable_stream_encoder_set_qlp_coeff_precision (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_QLP_COEFF_PREC_SEARCH:
      FLAC__seekable_stream_encoder_set_do_qlp_coeff_prec_search (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_ESCAPE_CODING:
      FLAC__seekable_stream_encoder_set_do_escape_coding (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_EXHAUSTIVE_MODEL_SEARCH:
      FLAC__seekable_stream_encoder_set_do_exhaustive_model_search (this->
          encoder, g_value_get_boolean (value));
      break;
    case PROP_MIN_RESIDUAL_PARTITION_ORDER:
      FLAC__seekable_stream_encoder_set_min_residual_partition_order (this->
          encoder, g_value_get_uint (value));
      break;
    case PROP_MAX_RESIDUAL_PARTITION_ORDER:
      FLAC__seekable_stream_encoder_set_max_residual_partition_order (this->
          encoder, g_value_get_uint (value));
      break;
    case PROP_RICE_PARAMETER_SEARCH_DIST:
      FLAC__seekable_stream_encoder_set_rice_parameter_search_dist (this->
          encoder, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (this);
}

static void
gst_flacenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFlacEnc *this = GST_FLACENC (object);

  GST_OBJECT_LOCK (this);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_enum (value, this->quality);
      break;
    case PROP_STREAMABLE_SUBSET:
      g_value_set_boolean (value,
          FLAC__seekable_stream_encoder_get_streamable_subset (this->encoder));
      break;
    case PROP_MID_SIDE_STEREO:
      g_value_set_boolean (value,
          FLAC__seekable_stream_encoder_get_do_mid_side_stereo (this->encoder));
      break;
    case PROP_LOOSE_MID_SIDE_STEREO:
      g_value_set_boolean (value,
          FLAC__seekable_stream_encoder_get_loose_mid_side_stereo (this->
              encoder));
      break;
    case PROP_BLOCKSIZE:
      g_value_set_uint (value,
          FLAC__seekable_stream_encoder_get_blocksize (this->encoder));
      break;
    case PROP_MAX_LPC_ORDER:
      g_value_set_uint (value,
          FLAC__seekable_stream_encoder_get_max_lpc_order (this->encoder));
      break;
    case PROP_QLP_COEFF_PRECISION:
      g_value_set_uint (value,
          FLAC__seekable_stream_encoder_get_qlp_coeff_precision (this->
              encoder));
      break;
    case PROP_QLP_COEFF_PREC_SEARCH:
      g_value_set_boolean (value,
          FLAC__seekable_stream_encoder_get_do_qlp_coeff_prec_search (this->
              encoder));
      break;
    case PROP_ESCAPE_CODING:
      g_value_set_boolean (value,
          FLAC__seekable_stream_encoder_get_do_escape_coding (this->encoder));
      break;
    case PROP_EXHAUSTIVE_MODEL_SEARCH:
      g_value_set_boolean (value,
          FLAC__seekable_stream_encoder_get_do_exhaustive_model_search (this->
              encoder));
      break;
    case PROP_MIN_RESIDUAL_PARTITION_ORDER:
      g_value_set_uint (value,
          FLAC__seekable_stream_encoder_get_min_residual_partition_order (this->
              encoder));
      break;
    case PROP_MAX_RESIDUAL_PARTITION_ORDER:
      g_value_set_uint (value,
          FLAC__seekable_stream_encoder_get_max_residual_partition_order (this->
              encoder));
      break;
    case PROP_RICE_PARAMETER_SEARCH_DIST:
      g_value_set_uint (value,
          FLAC__seekable_stream_encoder_get_rice_parameter_search_dist (this->
              encoder));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (this);
}

static GstStateChangeReturn
gst_flacenc_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFlacEnc *flacenc = GST_FLACENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      flacenc->stopped = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (FLAC__seekable_stream_encoder_get_state (flacenc->encoder) !=
          FLAC__STREAM_ENCODER_UNINITIALIZED) {
        flacenc->stopped = TRUE;
        FLAC__seekable_stream_encoder_finish (flacenc->encoder);
      }
      flacenc->offset = 0;
      flacenc->samples_written = 0;
      if (flacenc->meta) {
        FLAC__metadata_object_delete (flacenc->meta[0]);
        g_free (flacenc->meta);
        flacenc->meta = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}
