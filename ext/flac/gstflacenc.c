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
/**
 * SECTION:element-flacenc
 * @see_also: #GstFlacDec
 *
 * flacenc encodes FLAC streams.
 * <ulink url="http://flac.sourceforge.net/">FLAC</ulink>
 * is a Free Lossless Audio Codec.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch audiotestsrc num-buffers=100 ! flacenc ! filesink location=beep.flac
 * ]|
 * </refsect2>
 */

/* TODO: - We currently don't handle discontinuities in the stream in a useful
 *         way and instead rely on the developer plugging in audiorate if
 *         the stream contains discontinuities.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>

#include <gstflacenc.h>
#include <gst/audio/audio.h>
#include <gst/audio/multichannel.h>
#include <gst/tag/tag.h>
#include <gst/gsttagsetter.h>

/* Taken from http://flac.sourceforge.net/format.html#frame_header */
static const GstAudioChannelPosition channel_positions[8][8] = {
  {GST_AUDIO_CHANNEL_POSITION_FRONT_MONO},
  {GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT},
  /* FIXME: 7/8 channel layouts are not defined in the FLAC specs */
  {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
      GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}, {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_LFE,
        GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}
};

#define FLAC_SINK_CAPS \
  "audio/x-raw-int, "               \
  "endianness = (int) BYTE_ORDER, " \
  "signed = (boolean) TRUE, "       \
  "width = (int) 8, "               \
  "depth = (int) 8, "               \
  "rate = (int) [ 1, 655350 ], "    \
  "channels = (int) [ 1, 8 ]; "     \
  "audio/x-raw-int, "               \
  "endianness = (int) BYTE_ORDER, " \
  "signed = (boolean) TRUE, "       \
  "width = (int) 16, "              \
  "depth = (int) { 12, 16 }, "      \
  "rate = (int) [ 1, 655350 ], "    \
  "channels = (int) [ 1, 8 ]; "     \
  "audio/x-raw-int, "               \
  "endianness = (int) BYTE_ORDER, " \
  "signed = (boolean) TRUE, "       \
  "width = (int) 32, "              \
  "depth = (int) { 20, 24 }, "      \
  "rate = (int) [ 1, 655350 ], "    \
  "channels = (int) [ 1, 8 ]"

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
  PROP_RICE_PARAMETER_SEARCH_DIST,
  PROP_PADDING,
  PROP_SEEKPOINTS
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

GST_BOILERPLATE_FULL (GstFlacEnc, gst_flac_enc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER, _do_init);

static gboolean gst_flac_enc_start (GstAudioEncoder * enc);
static gboolean gst_flac_enc_stop (GstAudioEncoder * enc);
static gboolean gst_flac_enc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_flac_enc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);
static GstCaps *gst_flac_enc_getcaps (GstAudioEncoder * enc);
static gboolean gst_flac_enc_sink_event (GstAudioEncoder * enc,
    GstEvent * event);

static void gst_flac_enc_finalize (GObject * object);

static gboolean gst_flac_enc_update_quality (GstFlacEnc * flacenc,
    gint quality);
static void gst_flac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_flac_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static FLAC__StreamEncoderWriteStatus
gst_flac_enc_write_callback (const FLAC__StreamEncoder * encoder,
    const FLAC__byte buffer[], size_t bytes,
    unsigned samples, unsigned current_frame, void *client_data);
static FLAC__StreamEncoderSeekStatus
gst_flac_enc_seek_callback (const FLAC__StreamEncoder * encoder,
    FLAC__uint64 absolute_byte_offset, void *client_data);
static FLAC__StreamEncoderTellStatus
gst_flac_enc_tell_callback (const FLAC__StreamEncoder * encoder,
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
#define DEFAULT_PADDING 0
#define DEFAULT_SEEKPOINTS 0

#define GST_TYPE_FLAC_ENC_QUALITY (gst_flac_enc_quality_get_type ())
static GType
gst_flac_enc_quality_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {0, "0 - Fastest compression", "0"},
      {1, "1", "1"},
      {2, "2", "2"},
      {3, "3", "3"},
      {4, "4", "4"},
      {5, "5 - Default", "5"},
      {6, "6", "6"},
      {7, "7", "7"},
      {8, "8 - Highest compression", "8"},
      {9, "9 - Insane", "9"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstFlacEncQuality", values);
  }
  return qtype;
}

static void
gst_flac_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  gst_element_class_set_details_simple (element_class, "FLAC audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio with the FLAC lossless audio encoder",
      "Wim Taymans <wim.taymans@chello.be>");

  GST_DEBUG_CATEGORY_INIT (flacenc_debug, "flacenc", 0,
      "Flac encoding element");
}

static void
gst_flac_enc_class_init (GstFlacEncClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioEncoderClass *base_class;

  gobject_class = (GObjectClass *) klass;
  base_class = (GstAudioEncoderClass *) (klass);

  gobject_class->set_property = gst_flac_enc_set_property;
  gobject_class->get_property = gst_flac_enc_get_property;
  gobject_class->finalize = gst_flac_enc_finalize;

  base_class->start = GST_DEBUG_FUNCPTR (gst_flac_enc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_flac_enc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_flac_enc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_flac_enc_handle_frame);
  base_class->getcaps = GST_DEBUG_FUNCPTR (gst_flac_enc_getcaps);
  base_class->event = GST_DEBUG_FUNCPTR (gst_flac_enc_sink_event);

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_QUALITY,
      g_param_spec_enum ("quality",
          "Quality",
          "Speed versus compression tradeoff",
          GST_TYPE_FLAC_ENC_QUALITY, DEFAULT_QUALITY,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_STREAMABLE_SUBSET, g_param_spec_boolean ("streamable-subset",
          "Streamable subset",
          "true to limit encoder to generating a Subset stream, else false",
          TRUE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MID_SIDE_STEREO,
      g_param_spec_boolean ("mid-side-stereo", "Do mid side stereo",
          "Do mid side stereo (only for stereo input)",
          flacenc_params[DEFAULT_QUALITY].mid_side,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_LOOSE_MID_SIDE_STEREO, g_param_spec_boolean ("loose-mid-side-stereo",
          "Loose mid side stereo", "Loose mid side stereo",
          flacenc_params[DEFAULT_QUALITY].loose_mid_side,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCKSIZE,
      g_param_spec_uint ("blocksize", "Blocksize", "Blocksize in samples",
          FLAC__MIN_BLOCK_SIZE, FLAC__MAX_BLOCK_SIZE,
          flacenc_params[DEFAULT_QUALITY].blocksize,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_LPC_ORDER,
      g_param_spec_uint ("max-lpc-order", "Max LPC order",
          "Max LPC order; 0 => use only fixed predictors", 0,
          FLAC__MAX_LPC_ORDER, flacenc_params[DEFAULT_QUALITY].max_lpc_order,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_QLP_COEFF_PRECISION, g_param_spec_uint ("qlp-coeff-precision",
          "QLP coefficients precision",
          "Precision in bits of quantized linear-predictor coefficients; 0 = automatic",
          0, 32, flacenc_params[DEFAULT_QUALITY].qlp_coeff_precision,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_QLP_COEFF_PREC_SEARCH, g_param_spec_boolean ("qlp-coeff-prec-search",
          "Do QLP coefficients precision search",
          "false = use qlp_coeff_precision, "
          "true = search around qlp_coeff_precision, take best",
          flacenc_params[DEFAULT_QUALITY].qlp_coeff_prec_search,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ESCAPE_CODING,
      g_param_spec_boolean ("escape-coding", "Do Escape coding",
          "search for escape codes in the entropy coding stage "
          "for slightly better compression",
          flacenc_params[DEFAULT_QUALITY].escape_coding,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_EXHAUSTIVE_MODEL_SEARCH,
      g_param_spec_boolean ("exhaustive-model-search",
          "Do exhaustive model search",
          "do exhaustive search of LP coefficient quantization (expensive!)",
          flacenc_params[DEFAULT_QUALITY].exhaustive_model_search,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MIN_RESIDUAL_PARTITION_ORDER,
      g_param_spec_uint ("min-residual-partition-order",
          "Min residual partition order",
          "Min residual partition order (above 4 doesn't usually help much)", 0,
          16, flacenc_params[DEFAULT_QUALITY].min_residual_partition_order,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_MAX_RESIDUAL_PARTITION_ORDER,
      g_param_spec_uint ("max-residual-partition-order",
          "Max residual partition order",
          "Max residual partition order (above 4 doesn't usually help much)", 0,
          16, flacenc_params[DEFAULT_QUALITY].max_residual_partition_order,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_RICE_PARAMETER_SEARCH_DIST,
      g_param_spec_uint ("rice-parameter-search-dist",
          "rice_parameter_search_dist",
          "0 = try only calc'd parameter k; else try all [k-dist..k+dist] "
          "parameters, use best", 0, FLAC__MAX_RICE_PARTITION_ORDER,
          flacenc_params[DEFAULT_QUALITY].rice_parameter_search_dist,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstFlacEnc:padding
   *
   * Write a PADDING block with this length in bytes
   *
   * Since: 0.10.16
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_PADDING,
      g_param_spec_uint ("padding",
          "Padding",
          "Write a PADDING block with this length in bytes", 0, G_MAXUINT,
          DEFAULT_PADDING,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /**
   * GstFlacEnc:seekpoints
   *
   * Write a SEEKTABLE block with a specific number of seekpoints
   * or with a specific interval spacing.
   *
   * Since: 0.10.18
   **/
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SEEKPOINTS,
      g_param_spec_int ("seekpoints",
          "Seekpoints",
          "Add SEEKTABLE metadata (if > 0, number of entries, if < 0, interval in sec)",
          -G_MAXINT, G_MAXINT,
          DEFAULT_SEEKPOINTS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
}

static void
gst_flac_enc_init (GstFlacEnc * flacenc, GstFlacEncClass * klass)
{
  GstAudioEncoder *enc = GST_AUDIO_ENCODER (flacenc);

  flacenc->encoder = FLAC__stream_encoder_new ();
  gst_flac_enc_update_quality (flacenc, DEFAULT_QUALITY);

  /* arrange granulepos marking (and required perfect ts) */
  gst_audio_encoder_set_mark_granule (enc, TRUE);
  gst_audio_encoder_set_perfect_timestamp (enc, TRUE);
}

static void
gst_flac_enc_finalize (GObject * object)
{
  GstFlacEnc *flacenc = GST_FLAC_ENC (object);

  FLAC__stream_encoder_delete (flacenc->encoder);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_flac_enc_start (GstAudioEncoder * enc)
{
  GstFlacEnc *flacenc = GST_FLAC_ENC (enc);

  GST_DEBUG_OBJECT (enc, "start");
  flacenc->stopped = TRUE;
  flacenc->got_headers = FALSE;
  flacenc->last_flow = GST_FLOW_OK;
  flacenc->offset = 0;
  flacenc->channels = 0;
  flacenc->depth = 0;
  flacenc->sample_rate = 0;
  flacenc->eos = FALSE;
  flacenc->tags = gst_tag_list_new ();

  return TRUE;
}

static gboolean
gst_flac_enc_stop (GstAudioEncoder * enc)
{
  GstFlacEnc *flacenc = GST_FLAC_ENC (enc);

  GST_DEBUG_OBJECT (enc, "stop");
  gst_tag_list_free (flacenc->tags);
  flacenc->tags = NULL;
  if (FLAC__stream_encoder_get_state (flacenc->encoder) !=
      FLAC__STREAM_ENCODER_UNINITIALIZED) {
    flacenc->stopped = TRUE;
    FLAC__stream_encoder_finish (flacenc->encoder);
  }
  if (flacenc->meta) {
    FLAC__metadata_object_delete (flacenc->meta[0]);

    if (flacenc->meta[1])
      FLAC__metadata_object_delete (flacenc->meta[1]);

    if (flacenc->meta[2])
      FLAC__metadata_object_delete (flacenc->meta[2]);

    g_free (flacenc->meta);
    flacenc->meta = NULL;
  }
  g_list_foreach (flacenc->headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (flacenc->headers);
  flacenc->headers = NULL;

  gst_tag_setter_reset_tags (GST_TAG_SETTER (enc));

  return TRUE;
}

static void
add_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  GList *comments;
  GList *it;
  GstFlacEnc *flacenc = GST_FLAC_ENC (user_data);

  /* IMAGE and PREVIEW_IMAGE tags are already written
   * differently, no need to store them inside the
   * vorbiscomments too */
  if (strcmp (tag, GST_TAG_IMAGE) == 0
      || strcmp (tag, GST_TAG_PREVIEW_IMAGE) == 0)
    return;

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
gst_flac_enc_set_metadata (GstFlacEnc * flacenc, guint64 total_samples)
{
  const GstTagList *user_tags;
  GstTagList *copy;
  gint entries = 1;
  gint n_images, n_preview_images;

  g_return_if_fail (flacenc != NULL);
  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (flacenc));
  if ((flacenc->tags == NULL) && (user_tags == NULL)) {
    return;
  }
  copy = gst_tag_list_merge (user_tags, flacenc->tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (flacenc)));
  n_images = gst_tag_list_get_tag_size (copy, GST_TAG_IMAGE);
  n_preview_images = gst_tag_list_get_tag_size (copy, GST_TAG_PREVIEW_IMAGE);

  flacenc->meta =
      g_new0 (FLAC__StreamMetadata *, 3 + n_images + n_preview_images);

  flacenc->meta[0] =
      FLAC__metadata_object_new (FLAC__METADATA_TYPE_VORBIS_COMMENT);
  gst_tag_list_foreach (copy, add_one_tag, flacenc);

  if (n_images + n_preview_images > 0) {
    GstBuffer *buffer;
    GstCaps *caps;
    GstStructure *structure;
    GstTagImageType image_type = GST_TAG_IMAGE_TYPE_NONE;
    gint i;

    for (i = 0; i < n_images + n_preview_images; i++) {
      if (i < n_images) {
        if (!gst_tag_list_get_buffer_index (copy, GST_TAG_IMAGE, i, &buffer))
          continue;
      } else {
        if (!gst_tag_list_get_buffer_index (copy, GST_TAG_PREVIEW_IMAGE,
                i - n_images, &buffer))
          continue;
      }

      flacenc->meta[entries] =
          FLAC__metadata_object_new (FLAC__METADATA_TYPE_PICTURE);

      caps = gst_buffer_get_caps (buffer);
      structure = gst_caps_get_structure (caps, 0);

      gst_structure_get (structure, "image-type", GST_TYPE_TAG_IMAGE_TYPE,
          &image_type, NULL);
      /* Convert to ID3v2 APIC image type */
      if (image_type == GST_TAG_IMAGE_TYPE_NONE)
        image_type = (i < n_images) ? 0x00 : 0x01;
      else
        image_type = image_type + 2;

      FLAC__metadata_object_picture_set_data (flacenc->meta[entries],
          GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), TRUE);
      /* FIXME: There's no way to set the picture type in libFLAC */
      flacenc->meta[entries]->data.picture.type = image_type;
      FLAC__metadata_object_picture_set_mime_type (flacenc->meta[entries],
          (char *) gst_structure_get_name (structure), TRUE);

      gst_caps_unref (caps);
      gst_buffer_unref (buffer);
      entries++;
    }
  }

  if (flacenc->seekpoints && total_samples != GST_CLOCK_TIME_NONE) {
    gboolean res;
    guint samples;

    flacenc->meta[entries] =
        FLAC__metadata_object_new (FLAC__METADATA_TYPE_SEEKTABLE);
    if (flacenc->seekpoints > 0) {
      res =
          FLAC__metadata_object_seektable_template_append_spaced_points
          (flacenc->meta[entries], flacenc->seekpoints, total_samples);
    } else {
      samples = -flacenc->seekpoints * flacenc->sample_rate;
      res =
          FLAC__metadata_object_seektable_template_append_spaced_points_by_samples
          (flacenc->meta[entries], samples, total_samples);
    }
    if (!res) {
      GST_DEBUG_OBJECT (flacenc, "adding seekpoint template %d failed",
          flacenc->seekpoints);
      FLAC__metadata_object_delete (flacenc->meta[1]);
      flacenc->meta[entries] = NULL;
    } else {
      entries++;
    }
  } else if (flacenc->seekpoints && total_samples == GST_CLOCK_TIME_NONE) {
    GST_WARNING_OBJECT (flacenc, "total time unknown; can not add seekpoints");
  }

  if (flacenc->padding > 0) {
    flacenc->meta[entries] =
        FLAC__metadata_object_new (FLAC__METADATA_TYPE_PADDING);
    flacenc->meta[entries]->length = flacenc->padding;
    entries++;
  }

  if (FLAC__stream_encoder_set_metadata (flacenc->encoder,
          flacenc->meta, entries) != true)
    g_warning ("Dude, i'm already initialized!");

  gst_tag_list_free (copy);
}

static void
gst_flac_enc_caps_append_structure_with_widths (GstCaps * caps,
    GstStructure * s)
{
  GstStructure *tmp;
  GValue list = { 0, };
  GValue depth = { 0, };


  tmp = gst_structure_copy (s);
  gst_structure_set (tmp, "width", G_TYPE_INT, 8, "depth", G_TYPE_INT, 8, NULL);
  gst_caps_append_structure (caps, tmp);

  tmp = gst_structure_copy (s);

  g_value_init (&depth, G_TYPE_INT);
  g_value_init (&list, GST_TYPE_LIST);
  g_value_set_int (&depth, 12);
  gst_value_list_append_value (&list, &depth);
  g_value_set_int (&depth, 16);
  gst_value_list_append_value (&list, &depth);

  gst_structure_set (tmp, "width", G_TYPE_INT, 16, NULL);
  gst_structure_set_value (tmp, "depth", &list);
  gst_caps_append_structure (caps, tmp);

  g_value_reset (&list);

  tmp = s;

  g_value_set_int (&depth, 20);
  gst_value_list_append_value (&list, &depth);
  g_value_set_int (&depth, 24);
  gst_value_list_append_value (&list, &depth);

  gst_structure_set (tmp, "width", G_TYPE_INT, 32, NULL);
  gst_structure_set_value (tmp, "depth", &list);
  gst_caps_append_structure (caps, tmp);

  g_value_unset (&list);
  g_value_unset (&depth);
}

static GstCaps *
gst_flac_enc_getcaps (GstAudioEncoder * enc)
{
  GstCaps *ret = NULL, *caps = NULL;
  GstPad *pad;

  pad = GST_AUDIO_ENCODER_SINK_PAD (enc);

  GST_OBJECT_LOCK (pad);

  if (GST_PAD_CAPS (pad)) {
    ret = gst_caps_ref (GST_PAD_CAPS (pad));
  } else {
    gint i, c;

    ret = gst_caps_new_empty ();

    gst_flac_enc_caps_append_structure_with_widths (ret,
        gst_structure_new ("audio/x-raw-int",
            "endianness", G_TYPE_INT, G_BYTE_ORDER,
            "signed", G_TYPE_BOOLEAN, TRUE,
            "rate", GST_TYPE_INT_RANGE, 1, 655350,
            "channels", GST_TYPE_INT_RANGE, 1, 2, NULL));

    for (i = 3; i <= 8; i++) {
      GValue positions = { 0, };
      GValue pos = { 0, };
      GstStructure *s;

      g_value_init (&positions, GST_TYPE_ARRAY);
      g_value_init (&pos, GST_TYPE_AUDIO_CHANNEL_POSITION);

      for (c = 0; c < i; c++) {
        g_value_set_enum (&pos, channel_positions[i - 1][c]);
        gst_value_array_append_value (&positions, &pos);
      }
      g_value_unset (&pos);

      s = gst_structure_new ("audio/x-raw-int",
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "signed", G_TYPE_BOOLEAN, TRUE,
          "rate", GST_TYPE_INT_RANGE, 1, 655350,
          "channels", G_TYPE_INT, i, NULL);
      gst_structure_set_value (s, "channel-positions", &positions);
      g_value_unset (&positions);

      gst_flac_enc_caps_append_structure_with_widths (ret, s);
    }
  }

  GST_OBJECT_UNLOCK (pad);

  GST_DEBUG_OBJECT (pad, "Return caps %" GST_PTR_FORMAT, ret);

  caps = gst_audio_encoder_proxy_getcaps (enc, ret);
  gst_caps_unref (ret);

  return caps;
}

static guint64
gst_flac_enc_query_peer_total_samples (GstFlacEnc * flacenc, GstPad * pad)
{
  GstFormat fmt = GST_FORMAT_DEFAULT;
  gint64 duration;

  GST_DEBUG_OBJECT (flacenc, "querying peer for DEFAULT format duration");
  if (gst_pad_query_peer_duration (pad, &fmt, &duration)
      && fmt == GST_FORMAT_DEFAULT && duration != GST_CLOCK_TIME_NONE)
    goto done;

  fmt = GST_FORMAT_TIME;
  GST_DEBUG_OBJECT (flacenc, "querying peer for TIME format duration");

  if (gst_pad_query_peer_duration (pad, &fmt, &duration) &&
      fmt == GST_FORMAT_TIME && duration != GST_CLOCK_TIME_NONE) {
    GST_DEBUG_OBJECT (flacenc, "peer reported duration %" GST_TIME_FORMAT,
        GST_TIME_ARGS (duration));
    duration = GST_CLOCK_TIME_TO_FRAMES (duration, flacenc->sample_rate);

    goto done;
  }

  GST_DEBUG_OBJECT (flacenc, "Upstream reported no total samples");
  return GST_CLOCK_TIME_NONE;

done:
  GST_DEBUG_OBJECT (flacenc,
      "Upstream reported %" G_GUINT64_FORMAT " total samples", duration);

  return duration;
}

static gboolean
gst_flac_enc_set_format (GstAudioEncoder * enc, GstAudioInfo * info)
{
  GstFlacEnc *flacenc;
  guint64 total_samples = GST_CLOCK_TIME_NONE;
  FLAC__StreamEncoderInitStatus init_status;
  GstCaps *caps;

  flacenc = GST_FLAC_ENC (enc);

  /* if configured again, means something changed, can't handle that */
  if (FLAC__stream_encoder_get_state (flacenc->encoder) !=
      FLAC__STREAM_ENCODER_UNINITIALIZED)
    goto encoder_already_initialized;

  flacenc->channels = GST_AUDIO_INFO_CHANNELS (info);
  flacenc->width = GST_AUDIO_INFO_WIDTH (info);
  flacenc->depth = GST_AUDIO_INFO_DEPTH (info);
  flacenc->sample_rate = GST_AUDIO_INFO_RATE (info);

  caps = gst_caps_new_simple ("audio/x-flac",
      "channels", G_TYPE_INT, flacenc->channels,
      "rate", G_TYPE_INT, flacenc->sample_rate, NULL);

  if (!gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc), caps))
    goto setting_src_caps_failed;

  gst_caps_unref (caps);

  total_samples = gst_flac_enc_query_peer_total_samples (flacenc,
      GST_AUDIO_ENCODER_SINK_PAD (enc));

  FLAC__stream_encoder_set_bits_per_sample (flacenc->encoder, flacenc->depth);
  FLAC__stream_encoder_set_sample_rate (flacenc->encoder, flacenc->sample_rate);
  FLAC__stream_encoder_set_channels (flacenc->encoder, flacenc->channels);

  if (total_samples != GST_CLOCK_TIME_NONE)
    FLAC__stream_encoder_set_total_samples_estimate (flacenc->encoder,
        MIN (total_samples, G_GUINT64_CONSTANT (0x0FFFFFFFFF)));

  gst_flac_enc_set_metadata (flacenc, total_samples);

  /* callbacks clear to go now;
   * write callbacks receives headers during init */
  flacenc->stopped = FALSE;

  init_status = FLAC__stream_encoder_init_stream (flacenc->encoder,
      gst_flac_enc_write_callback, gst_flac_enc_seek_callback,
      gst_flac_enc_tell_callback, NULL, flacenc);
  if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK)
    goto failed_to_initialize;

  /* no special feedback to base class; should provide all available samples */

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
gst_flac_enc_update_quality (GstFlacEnc * flacenc, gint quality)
{
  flacenc->quality = quality;

#define DO_UPDATE(name, val, str)                                               \
  G_STMT_START {                                                                \
    if (FLAC__stream_encoder_get_##name (flacenc->encoder) !=                   \
        flacenc_params[quality].val) {                                          \
      FLAC__stream_encoder_set_##name (flacenc->encoder,                        \
          flacenc_params[quality].val);                                         \
      g_object_notify (G_OBJECT (flacenc), str);                                \
    }                                                                           \
  } G_STMT_END

  g_object_freeze_notify (G_OBJECT (flacenc));

  if (flacenc->channels == 2 || flacenc->channels == 0) {
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

static FLAC__StreamEncoderSeekStatus
gst_flac_enc_seek_callback (const FLAC__StreamEncoder * encoder,
    FLAC__uint64 absolute_byte_offset, void *client_data)
{
  GstFlacEnc *flacenc;
  GstPad *peerpad;

  flacenc = GST_FLAC_ENC (client_data);

  if (flacenc->stopped)
    return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;

  if ((peerpad = gst_pad_get_peer (GST_AUDIO_ENCODER_SRC_PAD (flacenc)))) {
    GstEvent *event = gst_event_new_new_segment (TRUE, 1.0, GST_FORMAT_BYTES,
        absolute_byte_offset, GST_BUFFER_OFFSET_NONE, 0);
    gboolean ret = gst_pad_send_event (peerpad, event);

    gst_object_unref (peerpad);

    if (ret) {
      GST_DEBUG ("Seek to %" G_GUINT64_FORMAT " %s",
          (guint64) absolute_byte_offset, "succeeded");
    } else {
      GST_DEBUG ("Seek to %" G_GUINT64_FORMAT " %s",
          (guint64) absolute_byte_offset, "failed");
      return FLAC__STREAM_ENCODER_SEEK_STATUS_UNSUPPORTED;
    }
  } else {
    GST_DEBUG ("Seek to %" G_GUINT64_FORMAT " failed (no peer pad)",
        (guint64) absolute_byte_offset);
  }

  flacenc->offset = absolute_byte_offset;
  return FLAC__STREAM_ENCODER_SEEK_STATUS_OK;
}

static void
notgst_value_array_append_buffer (GValue * array_val, GstBuffer * buf)
{
  GValue value = { 0, };

  g_value_init (&value, GST_TYPE_BUFFER);
  /* copy buffer to avoid problems with circular refcounts */
  buf = gst_buffer_copy (buf);
  /* again, for good measure */
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_IN_CAPS);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (array_val, &value);
  g_value_unset (&value);
}

#define HDR_TYPE_STREAMINFO     0
#define HDR_TYPE_VORBISCOMMENT  4

static GstFlowReturn
gst_flac_enc_process_stream_headers (GstFlacEnc * enc)
{
  GstBuffer *vorbiscomment = NULL;
  GstBuffer *streaminfo = NULL;
  GstBuffer *marker = NULL;
  GValue array = { 0, };
  GstCaps *caps;
  GList *l;
  GstFlowReturn ret = GST_FLOW_OK;

  caps = gst_caps_new_simple ("audio/x-flac",
      "channels", G_TYPE_INT, enc->channels,
      "rate", G_TYPE_INT, enc->sample_rate, NULL);

  for (l = enc->headers; l != NULL; l = l->next) {
    const guint8 *data;
    guint size;

    /* mark buffers so oggmux will ignore them if it already muxed the
     * header buffers from the streamheaders field in the caps */
    l->data = gst_buffer_make_metadata_writable (GST_BUFFER (l->data));
    GST_BUFFER_FLAG_SET (GST_BUFFER (l->data), GST_BUFFER_FLAG_IN_CAPS);

    data = GST_BUFFER_DATA (GST_BUFFER_CAST (l->data));
    size = GST_BUFFER_SIZE (GST_BUFFER_CAST (l->data));

    /* find initial 4-byte marker which we need to skip later on */
    if (size == 4 && memcmp (data, "fLaC", 4) == 0) {
      marker = GST_BUFFER_CAST (l->data);
    } else if (size > 1 && (data[0] & 0x7f) == HDR_TYPE_STREAMINFO) {
      streaminfo = GST_BUFFER_CAST (l->data);
    } else if (size > 1 && (data[0] & 0x7f) == HDR_TYPE_VORBISCOMMENT) {
      vorbiscomment = GST_BUFFER_CAST (l->data);
    }
  }

  if (marker == NULL || streaminfo == NULL || vorbiscomment == NULL) {
    GST_WARNING_OBJECT (enc, "missing header %p %p %p, muxing into container "
        "formats may be broken", marker, streaminfo, vorbiscomment);
    goto push_headers;
  }

  g_value_init (&array, GST_TYPE_ARRAY);

  /* add marker including STREAMINFO header */
  {
    GstBuffer *buf;
    guint16 num;

    /* minus one for the marker that is merged with streaminfo here */
    num = g_list_length (enc->headers) - 1;

    buf = gst_buffer_new_and_alloc (13 + GST_BUFFER_SIZE (streaminfo));
    GST_BUFFER_DATA (buf)[0] = 0x7f;
    memcpy (GST_BUFFER_DATA (buf) + 1, "FLAC", 4);
    GST_BUFFER_DATA (buf)[5] = 0x01;    /* mapping version major */
    GST_BUFFER_DATA (buf)[6] = 0x00;    /* mapping version minor */
    GST_BUFFER_DATA (buf)[7] = (num & 0xFF00) >> 8;
    GST_BUFFER_DATA (buf)[8] = (num & 0x00FF) >> 0;
    memcpy (GST_BUFFER_DATA (buf) + 9, "fLaC", 4);
    memcpy (GST_BUFFER_DATA (buf) + 13, GST_BUFFER_DATA (streaminfo),
        GST_BUFFER_SIZE (streaminfo));
    notgst_value_array_append_buffer (&array, buf);
    gst_buffer_unref (buf);
  }

  /* add VORBISCOMMENT header */
  notgst_value_array_append_buffer (&array, vorbiscomment);

  /* add other headers, if there are any */
  for (l = enc->headers; l != NULL; l = l->next) {
    if (GST_BUFFER_CAST (l->data) != marker &&
        GST_BUFFER_CAST (l->data) != streaminfo &&
        GST_BUFFER_CAST (l->data) != vorbiscomment) {
      notgst_value_array_append_buffer (&array, GST_BUFFER_CAST (l->data));
    }
  }

  gst_structure_set_value (gst_caps_get_structure (caps, 0),
      "streamheader", &array);
  g_value_unset (&array);

push_headers:

  /* push header buffers; update caps, so when we push the first buffer the
   * negotiated caps will change to caps that include the streamheader field */
  for (l = enc->headers; l != NULL; l = l->next) {
    GstBuffer *buf;

    buf = GST_BUFFER (l->data);
    gst_buffer_set_caps (buf, caps);
    GST_LOG_OBJECT (enc, "Pushing header buffer, size %u bytes",
        GST_BUFFER_SIZE (buf));
    GST_MEMDUMP_OBJECT (enc, "header buffer", GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    ret = gst_pad_push (GST_AUDIO_ENCODER_SRC_PAD (enc), buf);
    l->data = NULL;
  }
  g_list_free (enc->headers);
  enc->headers = NULL;

  gst_caps_unref (caps);

  return ret;
}

static FLAC__StreamEncoderWriteStatus
gst_flac_enc_write_callback (const FLAC__StreamEncoder * encoder,
    const FLAC__byte buffer[], size_t bytes,
    unsigned samples, unsigned current_frame, void *client_data)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstFlacEnc *flacenc;
  GstBuffer *outbuf;

  flacenc = GST_FLAC_ENC (client_data);

  if (flacenc->stopped)
    return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;

  outbuf = gst_buffer_new_and_alloc (bytes);
  memcpy (GST_BUFFER_DATA (outbuf), buffer, bytes);

  /* we assume libflac passes us stuff neatly framed */
  if (!flacenc->got_headers) {
    if (samples == 0) {
      GST_DEBUG_OBJECT (flacenc, "Got header, queueing (%u bytes)",
          (guint) bytes);
      flacenc->headers = g_list_append (flacenc->headers, outbuf);
      /* note: it's important that we increase our byte offset */
      goto out;
    } else {
      GST_INFO_OBJECT (flacenc, "Non-header packet, we have all headers now");
      ret = gst_flac_enc_process_stream_headers (flacenc);
      flacenc->got_headers = TRUE;
    }
  }

  if (flacenc->got_headers && samples == 0) {
    /* header fixup, push downstream directly */
    GST_DEBUG_OBJECT (flacenc, "Fixing up headers at pos=%" G_GUINT64_FORMAT
        ", size=%u", flacenc->offset, (guint) bytes);
    GST_MEMDUMP_OBJECT (flacenc, "Presumed header fragment",
        GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf));
    gst_buffer_set_caps (outbuf,
        GST_PAD_CAPS (GST_AUDIO_ENCODER_SRC_PAD (flacenc)));
    ret = gst_pad_push (GST_AUDIO_ENCODER_SRC_PAD (flacenc), outbuf);
  } else {
    /* regular frame data, pass to base class */
    GST_LOG ("Pushing buffer: ts=%" GST_TIME_FORMAT ", samples=%u, size=%u, "
        "pos=%" G_GUINT64_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        samples, (guint) bytes, flacenc->offset);
    ret = gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (flacenc),
        outbuf, samples);
  }

  if (ret != GST_FLOW_OK)
    GST_DEBUG_OBJECT (flacenc, "flow: %s", gst_flow_get_name (ret));

  flacenc->last_flow = ret;

out:
  flacenc->offset += bytes;

  if (ret != GST_FLOW_OK)
    return FLAC__STREAM_ENCODER_WRITE_STATUS_FATAL_ERROR;

  return FLAC__STREAM_ENCODER_WRITE_STATUS_OK;
}

static FLAC__StreamEncoderTellStatus
gst_flac_enc_tell_callback (const FLAC__StreamEncoder * encoder,
    FLAC__uint64 * absolute_byte_offset, void *client_data)
{
  GstFlacEnc *flacenc = GST_FLAC_ENC (client_data);

  *absolute_byte_offset = flacenc->offset;

  return FLAC__STREAM_ENCODER_TELL_STATUS_OK;
}

static gboolean
gst_flac_enc_sink_event (GstAudioEncoder * enc, GstEvent * event)
{
  GstFlacEnc *flacenc;
  GstTagList *taglist;
  gboolean ret = FALSE;

  flacenc = GST_FLAC_ENC (enc);

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
        stream_time = -1;
      }

      if (start > 0) {
        if (flacenc->offset > 0)
          GST_DEBUG ("Not handling mid-stream newsegment event");
        else
          GST_DEBUG ("Not handling newsegment event with non-zero start");
      } else {
        GstEvent *e = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
            0, -1, 0);

        ret = gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (enc), e);
      }

      if (stream_time > 0) {
        GST_DEBUG ("Not handling non-zero stream time");
      }

      /* don't push it downstream, we'll generate our own via seek to 0 */
      gst_event_unref (event);
      ret = TRUE;
      break;
    }
    case GST_EVENT_EOS:
      flacenc->eos = TRUE;
      break;
    case GST_EVENT_TAG:
      if (flacenc->tags) {
        gst_event_parse_tag (event, &taglist);
        gst_tag_list_insert (flacenc->tags, taglist,
            gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (flacenc)));
      } else {
        g_assert_not_reached ();
      }
      break;
    default:
      break;
  }

  return ret;
}

static GstFlowReturn
gst_flac_enc_handle_frame (GstAudioEncoder * enc, GstBuffer * buffer)
{
  GstFlacEnc *flacenc;
  FLAC__int32 *data;
  gulong insize;
  gint samples, width;
  gulong i;
  FLAC__bool res;

  flacenc = GST_FLAC_ENC (enc);

  /* base class ensures configuration */
  g_return_val_if_fail (flacenc->depth != 0, GST_FLOW_NOT_NEGOTIATED);

  width = flacenc->width;

  if (G_UNLIKELY (!buffer)) {
    if (flacenc->eos) {
      FLAC__stream_encoder_finish (flacenc->encoder);
    } else {
      /* can't handle intermittent draining/resyncing */
      GST_ELEMENT_WARNING (flacenc, STREAM, FORMAT, (NULL),
          ("Stream discontinuity detected. "
              "The output may have wrong timestamps, "
              "consider using audiorate to handle discontinuities"));
    }
    return flacenc->last_flow;
  }

  insize = GST_BUFFER_SIZE (buffer);
  samples = insize / (width >> 3);

  data = g_malloc (samples * sizeof (FLAC__int32));

  if (width == 8) {
    gint8 *indata = (gint8 *) GST_BUFFER_DATA (buffer);

    for (i = 0; i < samples; i++)
      data[i] = (FLAC__int32) indata[i];
  } else if (width == 16) {
    gint16 *indata = (gint16 *) GST_BUFFER_DATA (buffer);

    for (i = 0; i < samples; i++)
      data[i] = (FLAC__int32) indata[i];
  } else if (width == 32) {
    gint32 *indata = (gint32 *) GST_BUFFER_DATA (buffer);

    for (i = 0; i < samples; i++)
      data[i] = (FLAC__int32) indata[i];
  } else {
    g_assert_not_reached ();
  }

  res = FLAC__stream_encoder_process_interleaved (flacenc->encoder,
      (const FLAC__int32 *) data, samples / flacenc->channels);

  g_free (data);

  if (!res) {
    if (flacenc->last_flow == GST_FLOW_OK)
      return GST_FLOW_ERROR;
    else
      return flacenc->last_flow;
  }

  return GST_FLOW_OK;
}

static void
gst_flac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFlacEnc *this = GST_FLAC_ENC (object);

  GST_OBJECT_LOCK (this);

  switch (prop_id) {
    case PROP_QUALITY:
      gst_flac_enc_update_quality (this, g_value_get_enum (value));
      break;
    case PROP_STREAMABLE_SUBSET:
      FLAC__stream_encoder_set_streamable_subset (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_MID_SIDE_STEREO:
      FLAC__stream_encoder_set_do_mid_side_stereo (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_LOOSE_MID_SIDE_STEREO:
      FLAC__stream_encoder_set_loose_mid_side_stereo (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_BLOCKSIZE:
      FLAC__stream_encoder_set_blocksize (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_MAX_LPC_ORDER:
      FLAC__stream_encoder_set_max_lpc_order (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_QLP_COEFF_PRECISION:
      FLAC__stream_encoder_set_qlp_coeff_precision (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_QLP_COEFF_PREC_SEARCH:
      FLAC__stream_encoder_set_do_qlp_coeff_prec_search (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_ESCAPE_CODING:
      FLAC__stream_encoder_set_do_escape_coding (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_EXHAUSTIVE_MODEL_SEARCH:
      FLAC__stream_encoder_set_do_exhaustive_model_search (this->encoder,
          g_value_get_boolean (value));
      break;
    case PROP_MIN_RESIDUAL_PARTITION_ORDER:
      FLAC__stream_encoder_set_min_residual_partition_order (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_MAX_RESIDUAL_PARTITION_ORDER:
      FLAC__stream_encoder_set_max_residual_partition_order (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_RICE_PARAMETER_SEARCH_DIST:
      FLAC__stream_encoder_set_rice_parameter_search_dist (this->encoder,
          g_value_get_uint (value));
      break;
    case PROP_PADDING:
      this->padding = g_value_get_uint (value);
      break;
    case PROP_SEEKPOINTS:
      this->seekpoints = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (this);
}

static void
gst_flac_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFlacEnc *this = GST_FLAC_ENC (object);

  GST_OBJECT_LOCK (this);

  switch (prop_id) {
    case PROP_QUALITY:
      g_value_set_enum (value, this->quality);
      break;
    case PROP_STREAMABLE_SUBSET:
      g_value_set_boolean (value,
          FLAC__stream_encoder_get_streamable_subset (this->encoder));
      break;
    case PROP_MID_SIDE_STEREO:
      g_value_set_boolean (value,
          FLAC__stream_encoder_get_do_mid_side_stereo (this->encoder));
      break;
    case PROP_LOOSE_MID_SIDE_STEREO:
      g_value_set_boolean (value,
          FLAC__stream_encoder_get_loose_mid_side_stereo (this->encoder));
      break;
    case PROP_BLOCKSIZE:
      g_value_set_uint (value,
          FLAC__stream_encoder_get_blocksize (this->encoder));
      break;
    case PROP_MAX_LPC_ORDER:
      g_value_set_uint (value,
          FLAC__stream_encoder_get_max_lpc_order (this->encoder));
      break;
    case PROP_QLP_COEFF_PRECISION:
      g_value_set_uint (value,
          FLAC__stream_encoder_get_qlp_coeff_precision (this->encoder));
      break;
    case PROP_QLP_COEFF_PREC_SEARCH:
      g_value_set_boolean (value,
          FLAC__stream_encoder_get_do_qlp_coeff_prec_search (this->encoder));
      break;
    case PROP_ESCAPE_CODING:
      g_value_set_boolean (value,
          FLAC__stream_encoder_get_do_escape_coding (this->encoder));
      break;
    case PROP_EXHAUSTIVE_MODEL_SEARCH:
      g_value_set_boolean (value,
          FLAC__stream_encoder_get_do_exhaustive_model_search (this->encoder));
      break;
    case PROP_MIN_RESIDUAL_PARTITION_ORDER:
      g_value_set_uint (value,
          FLAC__stream_encoder_get_min_residual_partition_order
          (this->encoder));
      break;
    case PROP_MAX_RESIDUAL_PARTITION_ORDER:
      g_value_set_uint (value,
          FLAC__stream_encoder_get_max_residual_partition_order
          (this->encoder));
      break;
    case PROP_RICE_PARAMETER_SEARCH_DIST:
      g_value_set_uint (value,
          FLAC__stream_encoder_get_rice_parameter_search_dist (this->encoder));
      break;
    case PROP_PADDING:
      g_value_set_uint (value, this->padding);
      break;
    case PROP_SEEKPOINTS:
      g_value_set_int (value, this->seekpoints);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (this);
}
