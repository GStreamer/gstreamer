/* Schrodinger
 * Copyright (C) 2006 David Schleef <ds@schleef.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/video/gstvideoutils.h>
#include <string.h>

#include <math.h>
#include <schroedinger/schro.h>
#include <schroedinger/schrobitstream.h>
#include <schroedinger/schrovirtframe.h>
#include "gstschroutils.h"

GST_DEBUG_CATEGORY_EXTERN (schro_debug);
#define GST_CAT_DEFAULT schro_debug

#define GST_TYPE_SCHRO_ENC \
  (gst_schro_enc_get_type())
#define GST_SCHRO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SCHRO_ENC,GstSchroEnc))
#define GST_SCHRO_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SCHRO_ENC,GstSchroEncClass))
#define GST_IS_SCHRO_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SCHRO_ENC))
#define GST_IS_SCHRO_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SCHRO_ENC))

typedef struct _GstSchroEnc GstSchroEnc;
typedef struct _GstSchroEncClass GstSchroEncClass;

struct _GstSchroEnc
{
  GstVideoEncoder base_encoder;

  GstPad *sinkpad;
  GstPad *srcpad;

  /* state */
  SchroEncoder *encoder;
  SchroVideoFormat *video_format;

  guint64 last_granulepos;
  guint64 granule_offset;

  GstVideoCodecState *input_state;
};

struct _GstSchroEncClass
{
  GstVideoEncoderClass parent_class;
};

GType gst_schro_enc_get_type (void);



enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static void gst_schro_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_schro_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_schro_enc_process (GstSchroEnc * schro_enc);

static gboolean gst_schro_enc_set_format (GstVideoEncoder *
    base_video_encoder, GstVideoCodecState * state);
static gboolean gst_schro_enc_start (GstVideoEncoder * base_video_encoder);
static gboolean gst_schro_enc_stop (GstVideoEncoder * base_video_encoder);
static GstFlowReturn gst_schro_enc_finish (GstVideoEncoder *
    base_video_encoder);
static GstFlowReturn gst_schro_enc_handle_frame (GstVideoEncoder *
    base_video_encoder, GstVideoCodecFrame * frame);
static GstFlowReturn gst_schro_enc_pre_push (GstVideoEncoder *
    base_video_encoder, GstVideoCodecFrame * frame);
static void gst_schro_enc_finalize (GObject * object);
static gboolean gst_schro_enc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);

static GstStaticPadTemplate gst_schro_enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_SCHRO_YUV_LIST))
    );

static GstStaticPadTemplate gst_schro_enc_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-dirac;video/x-qt-part;video/x-mp4-part")
    );

#define parent_class gst_schro_enc_parent_class
G_DEFINE_TYPE (GstSchroEnc, gst_schro_enc, GST_TYPE_VIDEO_ENCODER);

static GType
register_enum_list (const SchroEncoderSetting * setting)
{
  GType type;
  static GEnumValue *enumtypes;
  int n;
  char *typename;
  int i;

  n = setting->max + 1;

  enumtypes = g_malloc0 ((n + 1) * sizeof (GEnumValue));
  for (i = 0; i < n; i++) {
    gchar *nick;

    enumtypes[i].value = i;
    nick = g_strdelimit (g_strdup (setting->enum_list[i]), "_", '-');
    enumtypes[i].value_name = g_intern_static_string (nick);
    enumtypes[i].value_nick = enumtypes[i].value_name;
  }

  typename = g_strdup_printf ("SchroEncoderSettingEnum_%s", setting->name);
  type = g_enum_register_static (typename, enumtypes);
  g_free (typename);

  return type;
}

static void
gst_schro_enc_class_init (GstSchroEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *basevideocoder_class;
  int i;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basevideocoder_class = GST_VIDEO_ENCODER_CLASS (klass);

  gobject_class->set_property = gst_schro_enc_set_property;
  gobject_class->get_property = gst_schro_enc_get_property;
  gobject_class->finalize = gst_schro_enc_finalize;

  for (i = 0; i < schro_encoder_get_n_settings (); i++) {
    const SchroEncoderSetting *setting;

    setting = schro_encoder_get_setting_info (i);

    /* we do this by checking downstream caps, and the profile/level selected
     * should be read from the output caps and not from properties */
    if (strcmp (setting->name, "force_profile") == 0
        || strcmp (setting->name, "profile") == 0
        || strcmp (setting->name, "level") == 0)
      continue;

    /* we configure this based on the input caps */
    if (strcmp (setting->name, "interlaced_coding") == 0)
      continue;

    switch (setting->type) {
      case SCHRO_ENCODER_SETTING_TYPE_BOOLEAN:
        g_object_class_install_property (gobject_class, i + 1,
            g_param_spec_boolean (setting->name, setting->name, setting->name,
                setting->default_value, G_PARAM_READWRITE));
        break;
      case SCHRO_ENCODER_SETTING_TYPE_INT:
        g_object_class_install_property (gobject_class, i + 1,
            g_param_spec_int (setting->name, setting->name, setting->name,
                setting->min, setting->max, setting->default_value,
                G_PARAM_READWRITE));
        break;
      case SCHRO_ENCODER_SETTING_TYPE_ENUM:
        g_object_class_install_property (gobject_class, i + 1,
            g_param_spec_enum (setting->name, setting->name, setting->name,
                register_enum_list (setting), setting->default_value,
                G_PARAM_READWRITE));
        break;
      case SCHRO_ENCODER_SETTING_TYPE_DOUBLE:
        g_object_class_install_property (gobject_class, i + 1,
            g_param_spec_double (setting->name, setting->name, setting->name,
                setting->min, setting->max, setting->default_value,
                G_PARAM_READWRITE));
        break;
      default:
        break;
    }
  }

  gst_element_class_add_static_pad_template (element_class,
      &gst_schro_enc_src_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_schro_enc_sink_template);

  gst_element_class_set_static_metadata (element_class, "Dirac Encoder",
      "Codec/Encoder/Video",
      "Encode raw video into Dirac stream", "David Schleef <ds@schleef.org>");

  basevideocoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_schro_enc_set_format);
  basevideocoder_class->start = GST_DEBUG_FUNCPTR (gst_schro_enc_start);
  basevideocoder_class->stop = GST_DEBUG_FUNCPTR (gst_schro_enc_stop);
  basevideocoder_class->finish = GST_DEBUG_FUNCPTR (gst_schro_enc_finish);
  basevideocoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_schro_enc_handle_frame);
  basevideocoder_class->pre_push = GST_DEBUG_FUNCPTR (gst_schro_enc_pre_push);
  basevideocoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_schro_enc_propose_allocation);
}

static void
gst_schro_enc_init (GstSchroEnc * schro_enc)
{
  GST_DEBUG ("gst_schro_enc_init");

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (schro_enc));

  /* Normally, we'd create the encoder in ->start(), but we use the
   * encoder to store object properties.  So it needs to be created
   * here. */
  schro_enc->encoder = schro_encoder_new ();
  schro_encoder_set_packet_assembly (schro_enc->encoder, TRUE);
  schro_enc->video_format = schro_encoder_get_video_format (schro_enc->encoder);
}

static void
gst_schro_enc_finalize (GObject * object)
{
  GstSchroEnc *schro_enc = GST_SCHRO_ENC (object);

  if (schro_enc->encoder) {
    schro_encoder_free (schro_enc->encoder);
    schro_enc->encoder = NULL;
  }
  if (schro_enc->video_format) {
    g_free (schro_enc->video_format);
    schro_enc->video_format = NULL;
  }
  if (schro_enc->input_state)
    gst_video_codec_state_unref (schro_enc->input_state);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const gchar *
get_profile_name (int profile)
{
  switch (profile) {
    case 0:
      return "vc2-low-delay";
    case 1:
      return "vc2-simple";
    case 2:
      return "vc2-main";
    case 8:
      return "main";
    default:
      break;
  }
  return "unknown";
}

static const gchar *
get_level_name (int level)
{
  switch (level) {
    case 0:
      return "0";
    case 1:
      return "1";
    case 128:
      return "128";
    default:
      break;
  }
  /* need to add it to template caps, so return 0 for now */
  GST_WARNING ("unhandled dirac level %u", level);
  return "0";
}

static void
gst_schro_enc_negotiate_profile (GstSchroEnc * enc)
{
  GstStructure *s;
  const gchar *profile;
  const gchar *level;
  GstCaps *allowed_caps;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (enc));

  GST_DEBUG_OBJECT (enc, "allowed caps: %" GST_PTR_FORMAT, allowed_caps);

  if (allowed_caps == NULL)
    return;

  if (gst_caps_is_empty (allowed_caps) || gst_caps_is_any (allowed_caps))
    goto out;

  allowed_caps = gst_caps_make_writable (allowed_caps);
  allowed_caps = gst_caps_fixate (allowed_caps);
  s = gst_caps_get_structure (allowed_caps, 0);

  profile = gst_structure_get_string (s, "profile");
  if (profile) {
    if (!strcmp (profile, "vc2-low-delay")) {
      schro_encoder_setting_set_double (enc->encoder, "force_profile", 1);
    } else if (!strcmp (profile, "vc2-simple")) {
      schro_encoder_setting_set_double (enc->encoder, "force_profile", 2);
    } else if (!strcmp (profile, "vc2-main")) {
      schro_encoder_setting_set_double (enc->encoder, "force_profile", 3);
    } else if (!strcmp (profile, "main")) {
      schro_encoder_setting_set_double (enc->encoder, "force_profile", 4);
    } else {
      GST_WARNING_OBJECT (enc, "ignoring unknown profile '%s'", profile);
    }
  }

  level = gst_structure_get_string (s, "level");
  if (level != NULL && strcmp (level, "0") != 0) {
    GST_FIXME_OBJECT (enc, "level setting not implemented");
  }

out:

  gst_caps_unref (allowed_caps);
}

static gboolean
gst_schro_enc_set_format (GstVideoEncoder * base_video_encoder,
    GstVideoCodecState * state)
{
  GstSchroEnc *schro_enc = GST_SCHRO_ENC (base_video_encoder);
  GstBuffer *seq_header_buffer;
  GstVideoInfo *info = &state->info;
  GstVideoCodecState *output_state;
  GstClockTime latency;
  GstCaps *out_caps;
  int level, profile;

  GST_DEBUG ("set_output_caps");

  schro_video_format_set_std_video_format (schro_enc->video_format,
      SCHRO_VIDEO_FORMAT_CUSTOM);

  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
#if SCHRO_CHECK_VERSION(1,0,11)
    case GST_VIDEO_FORMAT_Y42B:
#endif
      schro_enc->video_format->chroma_format = SCHRO_CHROMA_420;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
#if SCHRO_CHECK_VERSION(1,0,11)
    case GST_VIDEO_FORMAT_v216:
    case GST_VIDEO_FORMAT_v210:
#endif
      schro_enc->video_format->chroma_format = SCHRO_CHROMA_422;
      break;
    case GST_VIDEO_FORMAT_AYUV:
#if SCHRO_CHECK_VERSION(1,0,12)
    case GST_VIDEO_FORMAT_ARGB:
#endif
#if SCHRO_CHECK_VERSION(1,0,11)
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_AYUV64:
#endif
      schro_enc->video_format->chroma_format = SCHRO_CHROMA_444;
      break;
    default:
      g_assert_not_reached ();
  }

  schro_enc->video_format->frame_rate_numerator = GST_VIDEO_INFO_FPS_N (info);
  schro_enc->video_format->frame_rate_denominator = GST_VIDEO_INFO_FPS_D (info);

  /* Seems that schroenc doesn't like unknown framerates, so let's pick
   * the random value 30 FPS if the framerate is unknown.
   */
  if (schro_enc->video_format->frame_rate_denominator == 0 ||
      schro_enc->video_format->frame_rate_numerator == 0) {
    schro_enc->video_format->frame_rate_numerator = 30;
    schro_enc->video_format->frame_rate_denominator = 1;
  }

  schro_enc->video_format->width = GST_VIDEO_INFO_WIDTH (info);
  schro_enc->video_format->height = GST_VIDEO_INFO_HEIGHT (info);
  schro_enc->video_format->clean_width = GST_VIDEO_INFO_WIDTH (info);
  schro_enc->video_format->clean_height = GST_VIDEO_INFO_HEIGHT (info);
  schro_enc->video_format->left_offset = 0;
  schro_enc->video_format->top_offset = 0;

  schro_enc->video_format->aspect_ratio_numerator = GST_VIDEO_INFO_PAR_N (info);
  schro_enc->video_format->aspect_ratio_denominator =
      GST_VIDEO_INFO_PAR_D (info);

  switch (GST_VIDEO_INFO_FORMAT (&state->info)) {
    default:
      schro_video_format_set_std_signal_range (schro_enc->video_format,
          SCHRO_SIGNAL_RANGE_8BIT_VIDEO);
      break;
#if SCHRO_CHECK_VERSION(1,0,11)
    case GST_VIDEO_FORMAT_v210:
      schro_video_format_set_std_signal_range (schro_enc->video_format,
          SCHRO_SIGNAL_RANGE_10BIT_VIDEO);
      break;
    case GST_VIDEO_FORMAT_v216:
    case GST_VIDEO_FORMAT_AYUV64:
      schro_enc->video_format->luma_offset = 64 << 8;
      schro_enc->video_format->luma_excursion = 219 << 8;
      schro_enc->video_format->chroma_offset = 128 << 8;
      schro_enc->video_format->chroma_excursion = 224 << 8;
      break;
#endif
#if SCHRO_CHECK_VERSION(1,0,12)
    case GST_VIDEO_FORMAT_ARGB:
      schro_enc->video_format->luma_offset = 256;
      schro_enc->video_format->luma_excursion = 511;
      schro_enc->video_format->chroma_offset = 256;
      schro_enc->video_format->chroma_excursion = 511;
      break;
#endif
  }

  if (GST_VIDEO_INFO_IS_INTERLACED (&state->info)) {
    schro_enc->video_format->interlaced_coding = 1;
  }

  /* See if downstream caps specify profile/level */
  gst_schro_enc_negotiate_profile (schro_enc);

  /* Finally set latency */
  latency = gst_util_uint64_scale (GST_SECOND,
      schro_enc->video_format->frame_rate_denominator *
      (int) schro_encoder_setting_get_double (schro_enc->encoder,
          "queue_depth"), schro_enc->video_format->frame_rate_numerator);
  gst_video_encoder_set_latency (base_video_encoder, latency, latency);

  schro_video_format_set_std_colour_spec (schro_enc->video_format,
      SCHRO_COLOUR_SPEC_HDTV);

  schro_encoder_set_video_format (schro_enc->encoder, schro_enc->video_format);
  schro_encoder_start (schro_enc->encoder);

  seq_header_buffer =
      gst_schro_wrap_schro_buffer (schro_encoder_encode_sequence_header
      (schro_enc->encoder));

  schro_enc->granule_offset = ~0;

  profile = schro_encoder_setting_get_double (schro_enc->encoder, "profile");
  level = schro_encoder_setting_get_double (schro_enc->encoder, "level");

  out_caps = gst_caps_new_simple ("video/x-dirac",
      "profile", G_TYPE_STRING, get_profile_name (profile),
      "level", G_TYPE_STRING, get_level_name (level), NULL);

  output_state =
      gst_video_encoder_set_output_state (base_video_encoder, out_caps, state);

  GST_BUFFER_FLAG_SET (seq_header_buffer, GST_BUFFER_FLAG_HEADER);
  {
    GValue array = { 0 };
    GValue value = { 0 };
    guint8 *outdata;
    GstBuffer *buf;
    GstMemory *seq_header_memory, *extra_header;
    gsize size;

    g_value_init (&array, GST_TYPE_ARRAY);
    g_value_init (&value, GST_TYPE_BUFFER);

    buf = gst_buffer_new ();
    /* Add the sequence header */
    seq_header_memory = gst_buffer_get_memory (seq_header_buffer, 0);
    gst_buffer_append_memory (buf, seq_header_memory);

    size = gst_buffer_get_size (buf) + SCHRO_PARSE_HEADER_SIZE;
    outdata = g_malloc0 (SCHRO_PARSE_HEADER_SIZE);

    GST_WRITE_UINT32_BE (outdata, 0x42424344);
    GST_WRITE_UINT8 (outdata + 4, SCHRO_PARSE_CODE_END_OF_SEQUENCE);
    GST_WRITE_UINT32_BE (outdata + 5, 0);
    GST_WRITE_UINT32_BE (outdata + 9, size);

    extra_header = gst_memory_new_wrapped (0, outdata, SCHRO_PARSE_HEADER_SIZE,
        0, SCHRO_PARSE_HEADER_SIZE, outdata, g_free);
    gst_buffer_append_memory (buf, extra_header);

    /* ogg(mux) expects the header buffers to have 0 timestamps -
       set OFFSET and OFFSET_END accordingly */
    GST_BUFFER_OFFSET (buf) = 0;
    GST_BUFFER_OFFSET_END (buf) = 0;
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_HEADER);

    gst_value_set_buffer (&value, buf);
    gst_buffer_unref (buf);
    gst_value_array_append_value (&array, &value);
    gst_structure_set_value (gst_caps_get_structure (output_state->caps, 0),
        "streamheader", &array);
    g_value_unset (&value);
    g_value_unset (&array);
  }
  gst_buffer_unref (seq_header_buffer);

  gst_video_codec_state_unref (output_state);

  /* And save the input state for later use */
  if (schro_enc->input_state)
    gst_video_codec_state_unref (schro_enc->input_state);
  schro_enc->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static void
gst_schro_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSchroEnc *src;

  src = GST_SCHRO_ENC (object);

  GST_DEBUG ("%s", pspec->name);

  if (prop_id >= 1) {
    const SchroEncoderSetting *setting;
    setting = schro_encoder_get_setting_info (prop_id - 1);
    switch (G_VALUE_TYPE (value)) {
      case G_TYPE_DOUBLE:
        schro_encoder_setting_set_double (src->encoder, setting->name,
            g_value_get_double (value));
        break;
      case G_TYPE_INT:
        schro_encoder_setting_set_double (src->encoder, setting->name,
            g_value_get_int (value));
        break;
      case G_TYPE_BOOLEAN:
        schro_encoder_setting_set_double (src->encoder, setting->name,
            g_value_get_boolean (value));
        break;
      default:
        schro_encoder_setting_set_double (src->encoder, setting->name,
            g_value_get_enum (value));
        break;
    }
  }
}

static void
gst_schro_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSchroEnc *src;

  src = GST_SCHRO_ENC (object);

  if (prop_id >= 1) {
    const SchroEncoderSetting *setting;
    setting = schro_encoder_get_setting_info (prop_id - 1);
    switch (G_VALUE_TYPE (value)) {
      case G_TYPE_DOUBLE:
        g_value_set_double (value,
            schro_encoder_setting_get_double (src->encoder, setting->name));
        break;
      case G_TYPE_INT:
        g_value_set_int (value,
            schro_encoder_setting_get_double (src->encoder, setting->name));
        break;
      case G_TYPE_BOOLEAN:
        g_value_set_boolean (value,
            schro_encoder_setting_get_double (src->encoder, setting->name));
        break;
      default:
        /* it's an enum */
        g_value_set_enum (value,
            schro_encoder_setting_get_double (src->encoder, setting->name));
        break;
    }
  }
}

static gboolean
gst_schro_enc_start (GstVideoEncoder * base_video_encoder)
{
  return TRUE;
}

static gboolean
gst_schro_enc_stop (GstVideoEncoder * base_video_encoder)
{
  return TRUE;
}

static GstFlowReturn
gst_schro_enc_finish (GstVideoEncoder * base_video_encoder)
{
  GstSchroEnc *schro_enc = GST_SCHRO_ENC (base_video_encoder);

  GST_DEBUG ("finish");

  schro_encoder_end_of_stream (schro_enc->encoder);
  gst_schro_enc_process (schro_enc);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_schro_enc_handle_frame (GstVideoEncoder * base_video_encoder,
    GstVideoCodecFrame * frame)
{
  GstSchroEnc *schro_enc = GST_SCHRO_ENC (base_video_encoder);
  SchroFrame *schro_frame;
  GstFlowReturn ret;
  GstVideoInfo *info = &schro_enc->input_state->info;

  if (schro_enc->granule_offset == ~0LL) {
    schro_enc->granule_offset =
        gst_util_uint64_scale (frame->pts, 2 * GST_VIDEO_INFO_FPS_N (info),
        GST_SECOND * GST_VIDEO_INFO_FPS_D (info));
    GST_DEBUG ("granule offset %" G_GINT64_FORMAT, schro_enc->granule_offset);
  }

  schro_frame = gst_schro_buffer_wrap (frame->input_buffer, FALSE, info);

  GST_DEBUG ("pushing frame %p", frame);
  schro_encoder_push_frame_full (schro_enc->encoder, schro_frame, frame);

  ret = gst_schro_enc_process (schro_enc);

  return ret;
}

static GstFlowReturn
gst_schro_enc_pre_push (GstVideoEncoder * base_video_encoder,
    GstVideoCodecFrame * frame)
{
  GstSchroEnc *schro_enc;
  int delay;
  int dist;
  int pt;
  int dt;
  guint64 granulepos_hi;
  guint64 granulepos_low;
  GstBuffer *buf = frame->output_buffer;

  schro_enc = GST_SCHRO_ENC (base_video_encoder);

  pt = frame->presentation_frame_number * 2 + schro_enc->granule_offset;
  dt = frame->decode_frame_number * 2 + schro_enc->granule_offset;
  delay = pt - dt;
  dist = frame->distance_from_sync;

  GST_DEBUG ("sys %d dpn %d pt %d dt %d delay %d dist %d",
      (int) frame->system_frame_number,
      (int) frame->decode_frame_number, pt, dt, delay, dist);

  granulepos_hi = (((guint64) pt - delay) << 9) | ((dist >> 8));
  granulepos_low = (delay << 9) | (dist & 0xff);
  GST_DEBUG ("granulepos %" G_GINT64_FORMAT ":%" G_GINT64_FORMAT, granulepos_hi,
      granulepos_low);

#if 0
  if (frame->is_eos) {
    GST_BUFFER_OFFSET_END (buf) = schro_enc->last_granulepos;
  } else {
#endif
    schro_enc->last_granulepos = (granulepos_hi << 22) | (granulepos_low);
    GST_BUFFER_OFFSET_END (buf) = schro_enc->last_granulepos;
#if 0
  }
#endif

  GST_BUFFER_OFFSET (buf) = gst_util_uint64_scale (schro_enc->last_granulepos,
      GST_SECOND * schro_enc->video_format->frame_rate_denominator,
      schro_enc->video_format->frame_rate_numerator);

  return GST_FLOW_OK;
}

static gboolean
gst_schro_enc_propose_allocation (GstVideoEncoder * encoder, GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}


static GstFlowReturn
gst_schro_enc_process (GstSchroEnc * schro_enc)
{
  SchroBuffer *encoded_buffer;
  GstVideoCodecFrame *frame;
  GstFlowReturn ret;
  int presentation_frame;
  void *voidptr;
  GstVideoEncoder *base_video_encoder = GST_VIDEO_ENCODER (schro_enc);

  GST_DEBUG ("process");

  while (1) {
    switch (schro_encoder_wait (schro_enc->encoder)) {
      case SCHRO_STATE_NEED_FRAME:
        return GST_FLOW_OK;
      case SCHRO_STATE_END_OF_STREAM:
        GST_DEBUG ("EOS");
        return GST_FLOW_OK;
      case SCHRO_STATE_HAVE_BUFFER:
        voidptr = NULL;
        encoded_buffer = schro_encoder_pull_full (schro_enc->encoder,
            &presentation_frame, &voidptr);
        frame = voidptr;
        if (encoded_buffer == NULL) {
          GST_DEBUG ("encoder_pull returned NULL");
          /* FIXME This shouldn't happen */
          return GST_FLOW_ERROR;
        }
#if SCHRO_CHECK_VERSION (1, 0, 9)
        {
          GstMessage *message;
          GstStructure *structure;
          GstBuffer *buf;
          gpointer data;

          data = g_malloc (sizeof (double) * 21);
          schro_encoder_get_frame_stats (schro_enc->encoder,
              (double *) data, 21);
          buf = gst_buffer_new_wrapped (data, sizeof (double) * 21);
          structure = gst_structure_new ("GstSchroEnc",
              "frame-stats", GST_TYPE_BUFFER, buf, NULL);
          gst_buffer_unref (buf);
          message = gst_message_new_element (GST_OBJECT (schro_enc), structure);
          gst_element_post_message (GST_ELEMENT (schro_enc), message);
        }
#endif

        if (voidptr == NULL) {
          GST_DEBUG ("got eos");
          //frame = schro_enc->eos_frame;
          frame = NULL;
          schro_buffer_unref (encoded_buffer);
        }

        /* FIXME: Get the frame from somewhere somehow... */
        if (frame) {
          if (SCHRO_PARSE_CODE_IS_SEQ_HEADER (encoded_buffer->data[4])) {
            GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
          }

          frame->output_buffer = gst_schro_wrap_schro_buffer (encoded_buffer);

          ret = gst_video_encoder_finish_frame (base_video_encoder, frame);

          if (ret != GST_FLOW_OK) {
            GST_DEBUG ("pad_push returned %d", ret);
            return ret;
          }
        }
        break;
      case SCHRO_STATE_AGAIN:
        break;
    }
  }
  return GST_FLOW_OK;
}
