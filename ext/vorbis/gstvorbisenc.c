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
 * SECTION:element-vorbisenc
 * @see_also: vorbisdec, oggmux
 *
 * This element encodes raw float audio into a Vorbis stream.
 * <ulink url="http://www.vorbis.com/">Vorbis</ulink> is a royalty-free
 * audio codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc wave=sine num-buffers=100 ! audioconvert ! vorbisenc ! oggmux ! filesink location=sine.ogg
 * ]| Encode a test sine signal to Ogg/Vorbis.  Note that the resulting file
 * will be really small because a sine signal compresses very well.
 * |[
 * gst-launch -v alsasrc ! audioconvert ! vorbisenc ! oggmux ! filesink location=alsasrc.ogg
 * ]| Record from a sound card using ALSA and encode to Ogg/Vorbis.
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vorbis/vorbisenc.h>

#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include <gst/audio/multichannel.h>
#include <gst/audio/audio.h>
#include "gstvorbisenc.h"

#include "gstvorbiscommon.h"

GST_DEBUG_CATEGORY_EXTERN (vorbisenc_debug);
#define GST_CAT_DEFAULT vorbisenc_debug

static GstStaticPadTemplate vorbis_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 1, 200000 ], "
        "channels = (int) [ 1, 256 ], " "endianness = (int) BYTE_ORDER, "
        "width = (int) 32")
    );

static GstStaticPadTemplate vorbis_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

enum
{
  ARG_0,
  ARG_MAX_BITRATE,
  ARG_BITRATE,
  ARG_MIN_BITRATE,
  ARG_QUALITY,
  ARG_MANAGED,
  ARG_LAST_MESSAGE
};

static GstFlowReturn gst_vorbis_enc_output_buffers (GstVorbisEnc * vorbisenc);

/* this function takes into account the granulepos_offset and the subgranule
 * time offset */
static GstClockTime
granulepos_to_timestamp_offset (GstVorbisEnc * vorbisenc,
    ogg_int64_t granulepos)
{
  if (granulepos >= 0)
    return gst_util_uint64_scale ((guint64) granulepos
        + vorbisenc->granulepos_offset, GST_SECOND, vorbisenc->frequency)
        + vorbisenc->subgranule_offset;
  return GST_CLOCK_TIME_NONE;
}

/* this function does a straight granulepos -> timestamp conversion */
static GstClockTime
granulepos_to_timestamp (GstVorbisEnc * vorbisenc, ogg_int64_t granulepos)
{
  if (granulepos >= 0)
    return gst_util_uint64_scale ((guint64) granulepos,
        GST_SECOND, vorbisenc->frequency);
  return GST_CLOCK_TIME_NONE;
}

#define MAX_BITRATE_DEFAULT     -1
#define BITRATE_DEFAULT         -1
#define MIN_BITRATE_DEFAULT     -1
#define QUALITY_DEFAULT         0.3
#define LOWEST_BITRATE          6000    /* lowest allowed for a 8 kHz stream */
#define HIGHEST_BITRATE         250001  /* highest allowed for a 44 kHz stream */

static gboolean gst_vorbis_enc_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_vorbis_enc_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_vorbis_enc_setup (GstVorbisEnc * vorbisenc);

static void gst_vorbis_enc_dispose (GObject * object);
static void gst_vorbis_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vorbis_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_vorbis_enc_change_state (GstElement * element,
    GstStateChange transition);
static void gst_vorbis_enc_add_interfaces (GType vorbisenc_type);

GST_BOILERPLATE_FULL (GstVorbisEnc, gst_vorbis_enc, GstElement,
    GST_TYPE_ELEMENT, gst_vorbis_enc_add_interfaces);

static void
gst_vorbis_enc_add_interfaces (GType vorbisenc_type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };
  static const GInterfaceInfo preset_info = { NULL, NULL, NULL };

  g_type_add_interface_static (vorbisenc_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);
  g_type_add_interface_static (vorbisenc_type, GST_TYPE_PRESET, &preset_info);
}

static void
gst_vorbis_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *src_template, *sink_template;


  src_template = gst_static_pad_template_get (&vorbis_enc_src_factory);
  gst_element_class_add_pad_template (element_class, src_template);

  sink_template = gst_static_pad_template_get (&vorbis_enc_sink_factory);
  gst_element_class_add_pad_template (element_class, sink_template);
  gst_element_class_set_details_simple (element_class,
      "Vorbis audio encoder", "Codec/Encoder/Audio",
      "Encodes audio in Vorbis format",
      "Monty <monty@xiph.org>, " "Wim Taymans <wim@fluendo.com>");
}

static void
gst_vorbis_enc_class_init (GstVorbisEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_vorbis_enc_set_property;
  gobject_class->get_property = gst_vorbis_enc_get_property;
  gobject_class->dispose = gst_vorbis_enc_dispose;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_BITRATE,
      g_param_spec_int ("max-bitrate", "Maximum Bitrate",
          "Specify a maximum bitrate (in bps). Useful for streaming "
          "applications. (-1 == disabled)",
          -1, HIGHEST_BITRATE, MAX_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Target Bitrate",
          "Attempt to encode at a bitrate averaging this (in bps). "
          "This uses the bitrate management engine, and is not recommended for most users. "
          "Quality is a better alternative. (-1 == disabled)", -1,
          HIGHEST_BITRATE, BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MIN_BITRATE,
      g_param_spec_int ("min-bitrate", "Minimum Bitrate",
          "Specify a minimum bitrate (in bps). Useful for encoding for a "
          "fixed-size channel. (-1 == disabled)", -1, HIGHEST_BITRATE,
          MIN_BITRATE_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_float ("quality", "Quality",
          "Specify quality instead of specifying a particular bitrate.", -0.1,
          1.0, QUALITY_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MANAGED,
      g_param_spec_boolean ("managed", "Managed",
          "Enable bitrate management engine", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_LAST_MESSAGE,
      g_param_spec_string ("last-message", "last-message",
          "The last status message", NULL,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_change_state);
}

static void
gst_vorbis_enc_dispose (GObject * object)
{
  GstVorbisEnc *vorbisenc = GST_VORBISENC (object);

  if (vorbisenc->sinkcaps) {
    gst_caps_unref (vorbisenc->sinkcaps);
    vorbisenc->sinkcaps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_vorbis_enc_generate_sink_caps (void)
{
  GstCaps *caps = gst_caps_new_empty ();
  int i, c;

  gst_caps_append_structure (caps, gst_structure_new ("audio/x-raw-float",
          "rate", GST_TYPE_INT_RANGE, 1, 200000,
          "channels", G_TYPE_INT, 1,
          "endianness", G_TYPE_INT, G_BYTE_ORDER, "width", G_TYPE_INT, 32,
          NULL));

  gst_caps_append_structure (caps, gst_structure_new ("audio/x-raw-float",
          "rate", GST_TYPE_INT_RANGE, 1, 200000,
          "channels", G_TYPE_INT, 2,
          "endianness", G_TYPE_INT, G_BYTE_ORDER, "width", G_TYPE_INT, 32,
          NULL));

  for (i = 3; i <= 8; i++) {
    GValue chanpos = { 0 };
    GValue pos = { 0 };
    GstStructure *structure;

    g_value_init (&chanpos, GST_TYPE_ARRAY);
    g_value_init (&pos, GST_TYPE_AUDIO_CHANNEL_POSITION);

    for (c = 0; c < i; c++) {
      g_value_set_enum (&pos, gst_vorbis_channel_positions[i - 1][c]);
      gst_value_array_append_value (&chanpos, &pos);
    }
    g_value_unset (&pos);

    structure = gst_structure_new ("audio/x-raw-float",
        "rate", GST_TYPE_INT_RANGE, 1, 200000,
        "channels", G_TYPE_INT, i,
        "endianness", G_TYPE_INT, G_BYTE_ORDER, "width", G_TYPE_INT, 32, NULL);
    gst_structure_set_value (structure, "channel-positions", &chanpos);
    g_value_unset (&chanpos);

    gst_caps_append_structure (caps, structure);
  }

  gst_caps_append_structure (caps, gst_structure_new ("audio/x-raw-float",
          "rate", GST_TYPE_INT_RANGE, 1, 200000,
          "channels", GST_TYPE_INT_RANGE, 9, 256,
          "endianness", G_TYPE_INT, G_BYTE_ORDER, "width", G_TYPE_INT, 32,
          NULL));

  return caps;
}

static GstCaps *
gst_vorbis_enc_sink_getcaps (GstPad * pad)
{
  GstVorbisEnc *vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));

  if (vorbisenc->sinkcaps == NULL)
    vorbisenc->sinkcaps = gst_vorbis_enc_generate_sink_caps ();

  return gst_caps_ref (vorbisenc->sinkcaps);
}

static gboolean
gst_vorbis_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstVorbisEnc *vorbisenc;
  GstStructure *structure;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));
  vorbisenc->setup = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &vorbisenc->channels);
  gst_structure_get_int (structure, "rate", &vorbisenc->frequency);

  gst_vorbis_enc_setup (vorbisenc);

  if (vorbisenc->setup)
    return TRUE;

  return FALSE;
}

static gboolean
gst_vorbis_enc_convert_src (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstVorbisEnc *vorbisenc;
  gint64 avg;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  if (vorbisenc->samples_in == 0 ||
      vorbisenc->bytes_out == 0 || vorbisenc->frequency == 0) {
    gst_object_unref (vorbisenc);
    return FALSE;
  }

  avg = (vorbisenc->bytes_out * vorbisenc->frequency) / (vorbisenc->samples_in);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND, avg);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale_int (src_value, avg, GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  gst_object_unref (vorbisenc);
  return res;
}

static gboolean
gst_vorbis_enc_convert_sink (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  GstVorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  bytes_per_sample = vorbisenc->channels * 2;

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
          gint byterate = bytes_per_sample * vorbisenc->frequency;

          if (byterate == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, byterate);
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
          if (vorbisenc->frequency == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND,
              vorbisenc->frequency);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (src_value,
              scale * vorbisenc->frequency, GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
  gst_object_unref (vorbisenc);
  return res;
}

static gint64
gst_vorbis_enc_get_latency (GstVorbisEnc * vorbisenc)
{
  /* FIXME, this probably depends on the bitrate and other setting but for now
   * we return this value, which was obtained by totally unscientific
   * measurements */
  return 58 * GST_MSECOND;
}

static const GstQueryType *
gst_vorbis_enc_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_vorbis_enc_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return gst_vorbis_enc_src_query_types;
}

static gboolean
gst_vorbis_enc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstVorbisEnc *vorbisenc;
  GstPad *peerpad;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));
  peerpad = gst_pad_get_peer (GST_PAD (vorbisenc->sinkpad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat fmt, req_fmt;
      gint64 pos, val;

      gst_query_parse_position (query, &req_fmt, NULL);
      if ((res = gst_pad_query_position (peerpad, &req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_position (peerpad, &fmt, &pos)))
        break;

      if ((res = gst_pad_query_convert (peerpad, fmt, pos, &req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat fmt, req_fmt;
      gint64 dur, val;

      gst_query_parse_duration (query, &req_fmt, NULL);
      if ((res = gst_pad_query_duration (peerpad, &req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_duration (peerpad, &fmt, &dur)))
        break;

      if ((res = gst_pad_query_convert (peerpad, fmt, dur, &req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_vorbis_enc_convert_src (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min_latency, max_latency;
      gint64 latency;

      if ((res = gst_pad_query (peerpad, query))) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);

        latency = gst_vorbis_enc_get_latency (vorbisenc);

        /* add our latency */
        min_latency += latency;
        if (max_latency != -1)
          max_latency += latency;

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_query (peerpad, query);
      break;
  }

error:
  gst_object_unref (peerpad);
  gst_object_unref (vorbisenc);
  return res;
}

static gboolean
gst_vorbis_enc_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_vorbis_enc_convert_sink (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

error:
  return res;
}

static void
gst_vorbis_enc_init (GstVorbisEnc * vorbisenc, GstVorbisEncClass * klass)
{
  vorbisenc->sinkpad =
      gst_pad_new_from_static_template (&vorbis_enc_sink_factory, "sink");
  gst_pad_set_event_function (vorbisenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_sink_event));
  gst_pad_set_chain_function (vorbisenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_chain));
  gst_pad_set_setcaps_function (vorbisenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_sink_setcaps));
  gst_pad_set_getcaps_function (vorbisenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_sink_getcaps));
  gst_pad_set_query_function (vorbisenc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_sink_query));
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->sinkpad);

  vorbisenc->srcpad =
      gst_pad_new_from_static_template (&vorbis_enc_src_factory, "src");
  gst_pad_set_query_function (vorbisenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_src_query));
  gst_pad_set_query_type_function (vorbisenc->srcpad,
      GST_DEBUG_FUNCPTR (gst_vorbis_enc_get_query_types));
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->srcpad);

  vorbisenc->channels = -1;
  vorbisenc->frequency = -1;

  vorbisenc->managed = FALSE;
  vorbisenc->max_bitrate = MAX_BITRATE_DEFAULT;
  vorbisenc->bitrate = BITRATE_DEFAULT;
  vorbisenc->min_bitrate = MIN_BITRATE_DEFAULT;
  vorbisenc->quality = QUALITY_DEFAULT;
  vorbisenc->quality_set = FALSE;
  vorbisenc->last_message = NULL;
}

static void
gst_vorbis_enc_metadata_set1 (const GstTagList * list, const gchar * tag,
    gpointer vorbisenc)
{
  GstVorbisEnc *enc = GST_VORBISENC (vorbisenc);
  GList *vc_list, *l;

  vc_list = gst_tag_to_vorbis_comments (list, tag);

  for (l = vc_list; l != NULL; l = l->next) {
    const gchar *vc_string = (const gchar *) l->data;
    gchar *key = NULL, *val = NULL;

    GST_LOG_OBJECT (vorbisenc, "vorbis comment: %s", vc_string);
    if (gst_tag_parse_extended_comment (vc_string, &key, NULL, &val, TRUE)) {
      vorbis_comment_add_tag (&enc->vc, key, val);
      g_free (key);
      g_free (val);
    }
  }

  g_list_foreach (vc_list, (GFunc) g_free, NULL);
  g_list_free (vc_list);
}

static void
gst_vorbis_enc_set_metadata (GstVorbisEnc * enc)
{
  GstTagList *merged_tags;
  const GstTagList *user_tags;

  vorbis_comment_init (&enc->vc);

  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (enc));

  GST_DEBUG_OBJECT (enc, "upstream tags = %" GST_PTR_FORMAT, enc->tags);
  GST_DEBUG_OBJECT (enc, "user-set tags = %" GST_PTR_FORMAT, user_tags);

  /* gst_tag_list_merge() will handle NULL for either or both lists fine */
  merged_tags = gst_tag_list_merge (user_tags, enc->tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (enc)));

  if (merged_tags) {
    GST_DEBUG_OBJECT (enc, "merged   tags = %" GST_PTR_FORMAT, merged_tags);
    gst_tag_list_foreach (merged_tags, gst_vorbis_enc_metadata_set1, enc);
    gst_tag_list_free (merged_tags);
  }
}

static gchar *
get_constraints_string (GstVorbisEnc * vorbisenc)
{
  gint min = vorbisenc->min_bitrate;
  gint max = vorbisenc->max_bitrate;
  gchar *result;

  if (min > 0 && max > 0)
    result = g_strdup_printf ("(min %d bps, max %d bps)", min, max);
  else if (min > 0)
    result = g_strdup_printf ("(min %d bps, no max)", min);
  else if (max > 0)
    result = g_strdup_printf ("(no min, max %d bps)", max);
  else
    result = g_strdup_printf ("(no min or max)");

  return result;
}

static void
update_start_message (GstVorbisEnc * vorbisenc)
{
  gchar *constraints;

  g_free (vorbisenc->last_message);

  if (vorbisenc->bitrate > 0) {
    if (vorbisenc->managed) {
      constraints = get_constraints_string (vorbisenc);
      vorbisenc->last_message =
          g_strdup_printf ("encoding at average bitrate %d bps %s",
          vorbisenc->bitrate, constraints);
      g_free (constraints);
    } else {
      vorbisenc->last_message =
          g_strdup_printf
          ("encoding at approximate bitrate %d bps (VBR encoding enabled)",
          vorbisenc->bitrate);
    }
  } else {
    if (vorbisenc->quality_set) {
      if (vorbisenc->managed) {
        constraints = get_constraints_string (vorbisenc);
        vorbisenc->last_message =
            g_strdup_printf
            ("encoding at quality level %2.2f using constrained VBR %s",
            vorbisenc->quality, constraints);
        g_free (constraints);
      } else {
        vorbisenc->last_message =
            g_strdup_printf ("encoding at quality level %2.2f",
            vorbisenc->quality);
      }
    } else {
      constraints = get_constraints_string (vorbisenc);
      vorbisenc->last_message =
          g_strdup_printf ("encoding using bitrate management %s", constraints);
      g_free (constraints);
    }
  }

  g_object_notify (G_OBJECT (vorbisenc), "last_message");
}

static gboolean
gst_vorbis_enc_setup (GstVorbisEnc * vorbisenc)
{
  vorbisenc->setup = FALSE;

  if (vorbisenc->bitrate < 0 && vorbisenc->min_bitrate < 0
      && vorbisenc->max_bitrate < 0) {
    vorbisenc->quality_set = TRUE;
  }

  update_start_message (vorbisenc);

  /* choose an encoding mode */
  /* (mode 0: 44kHz stereo uncoupled, roughly 128kbps VBR) */
  vorbis_info_init (&vorbisenc->vi);

  if (vorbisenc->quality_set) {
    if (vorbis_encode_setup_vbr (&vorbisenc->vi,
            vorbisenc->channels, vorbisenc->frequency,
            vorbisenc->quality) != 0) {
      GST_ERROR_OBJECT (vorbisenc,
          "vorbisenc: initialisation failed: invalid parameters for quality");
      vorbis_info_clear (&vorbisenc->vi);
      return FALSE;
    }

    /* do we have optional hard quality restrictions? */
    if (vorbisenc->max_bitrate > 0 || vorbisenc->min_bitrate > 0) {
      struct ovectl_ratemanage_arg ai;

      vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_GET, &ai);

      ai.bitrate_hard_min = vorbisenc->min_bitrate;
      ai.bitrate_hard_max = vorbisenc->max_bitrate;
      ai.management_active = 1;

      vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_SET, &ai);
    }
  } else {
    long min_bitrate, max_bitrate;

    min_bitrate = vorbisenc->min_bitrate > 0 ? vorbisenc->min_bitrate : -1;
    max_bitrate = vorbisenc->max_bitrate > 0 ? vorbisenc->max_bitrate : -1;

    if (vorbis_encode_setup_managed (&vorbisenc->vi,
            vorbisenc->channels,
            vorbisenc->frequency,
            max_bitrate, vorbisenc->bitrate, min_bitrate) != 0) {
      GST_ERROR_OBJECT (vorbisenc,
          "vorbis_encode_setup_managed "
          "(c %d, rate %d, max br %ld, br %d, min br %ld) failed",
          vorbisenc->channels, vorbisenc->frequency, max_bitrate,
          vorbisenc->bitrate, min_bitrate);
      vorbis_info_clear (&vorbisenc->vi);
      return FALSE;
    }
  }

  if (vorbisenc->managed && vorbisenc->bitrate < 0) {
    vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_AVG, NULL);
  } else if (!vorbisenc->managed) {
    /* Turn off management entirely (if it was turned on). */
    vorbis_encode_ctl (&vorbisenc->vi, OV_ECTL_RATEMANAGE_SET, NULL);
  }
  vorbis_encode_setup_init (&vorbisenc->vi);

  /* set up the analysis state and auxiliary encoding storage */
  vorbis_analysis_init (&vorbisenc->vd, &vorbisenc->vi);
  vorbis_block_init (&vorbisenc->vd, &vorbisenc->vb);

  vorbisenc->next_ts = 0;

  vorbisenc->setup = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_vorbis_enc_clear (GstVorbisEnc * vorbisenc)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (vorbisenc->setup) {
    vorbis_analysis_wrote (&vorbisenc->vd, 0);
    ret = gst_vorbis_enc_output_buffers (vorbisenc);

    vorbisenc->setup = FALSE;
  }

  /* clean up and exit.  vorbis_info_clear() must be called last */
  vorbis_block_clear (&vorbisenc->vb);
  vorbis_dsp_clear (&vorbisenc->vd);
  vorbis_info_clear (&vorbisenc->vi);

  vorbisenc->header_sent = FALSE;

  return ret;
}

/* prepare a buffer for transmission by passing data through libvorbis */
static GstBuffer *
gst_vorbis_enc_buffer_from_packet (GstVorbisEnc * vorbisenc,
    ogg_packet * packet)
{
  GstBuffer *outbuf;

  outbuf = gst_buffer_new_and_alloc (packet->bytes);
  memcpy (GST_BUFFER_DATA (outbuf), packet->packet, packet->bytes);
  /* see ext/ogg/README; OFFSET_END takes "our" granulepos, OFFSET its
   * time representation */
  GST_BUFFER_OFFSET_END (outbuf) = packet->granulepos +
      vorbisenc->granulepos_offset;
  GST_BUFFER_OFFSET (outbuf) = granulepos_to_timestamp (vorbisenc,
      GST_BUFFER_OFFSET_END (outbuf));
  GST_BUFFER_TIMESTAMP (outbuf) = vorbisenc->next_ts;

  /* update the next timestamp, taking granulepos_offset and subgranule offset
   * into account */
  vorbisenc->next_ts =
      granulepos_to_timestamp_offset (vorbisenc, packet->granulepos) +
      vorbisenc->initial_ts;
  GST_BUFFER_DURATION (outbuf) =
      vorbisenc->next_ts - GST_BUFFER_TIMESTAMP (outbuf);

  if (vorbisenc->next_discont) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    vorbisenc->next_discont = FALSE;
  }

  gst_buffer_set_caps (outbuf, vorbisenc->srccaps);

  GST_LOG_OBJECT (vorbisenc, "encoded buffer of %d bytes",
      GST_BUFFER_SIZE (outbuf));
  return outbuf;
}

/* the same as above, but different logic for setting timestamp and granulepos
 * */
static GstBuffer *
gst_vorbis_enc_buffer_from_header_packet (GstVorbisEnc * vorbisenc,
    ogg_packet * packet)
{
  GstBuffer *outbuf;

  outbuf = gst_buffer_new_and_alloc (packet->bytes);
  memcpy (GST_BUFFER_DATA (outbuf), packet->packet, packet->bytes);
  GST_BUFFER_OFFSET (outbuf) = vorbisenc->bytes_out;
  GST_BUFFER_OFFSET_END (outbuf) = 0;
  GST_BUFFER_TIMESTAMP (outbuf) = GST_CLOCK_TIME_NONE;
  GST_BUFFER_DURATION (outbuf) = GST_CLOCK_TIME_NONE;

  gst_buffer_set_caps (outbuf, vorbisenc->srccaps);

  GST_DEBUG ("created header packet buffer, %d bytes",
      GST_BUFFER_SIZE (outbuf));
  return outbuf;
}

/* push out the buffer and do internal bookkeeping */
static GstFlowReturn
gst_vorbis_enc_push_buffer (GstVorbisEnc * vorbisenc, GstBuffer * buffer)
{
  vorbisenc->bytes_out += GST_BUFFER_SIZE (buffer);

  GST_DEBUG_OBJECT (vorbisenc,
      "Pushing buffer with GP %" G_GINT64_FORMAT ", ts %" GST_TIME_FORMAT,
      GST_BUFFER_OFFSET_END (buffer),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)));
  return gst_pad_push (vorbisenc->srcpad, buffer);
}

static GstFlowReturn
gst_vorbis_enc_push_packet (GstVorbisEnc * vorbisenc, ogg_packet * packet)
{
  GstBuffer *outbuf;

  outbuf = gst_vorbis_enc_buffer_from_packet (vorbisenc, packet);
  return gst_vorbis_enc_push_buffer (vorbisenc, outbuf);
}

/* Set a copy of these buffers as 'streamheader' on the caps.
 * We need a copy to avoid these buffers ending up with (indirect) refs on
 * themselves
 */
static GstCaps *
gst_vorbis_enc_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2, GstBuffer * buf3)
{
  GstBuffer *buf;
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf3, GST_BUFFER_FLAG_IN_CAPS);

  /* put buffers in a fixed list */
  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buf1);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buf2);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);
  g_value_init (&value, GST_TYPE_BUFFER);
  buf = gst_buffer_copy (buf3);
  gst_value_set_buffer (&value, buf);
  gst_buffer_unref (buf);
  gst_value_array_append_value (&array, &value);
  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&value);
  g_value_unset (&array);

  return caps;
}

static gboolean
gst_vorbis_enc_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstVorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* Tell the library we're at end of stream so that it can handle
       * the last frame and mark end of stream in the output properly */
      GST_DEBUG_OBJECT (vorbisenc, "EOS, clearing state and sending event on");
      gst_vorbis_enc_clear (vorbisenc);

      res = gst_pad_push_event (vorbisenc->srcpad, event);
      break;
    case GST_EVENT_TAG:
      if (vorbisenc->tags) {
        GstTagList *list;

        gst_event_parse_tag (event, &list);
        gst_tag_list_insert (vorbisenc->tags, list,
            gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (vorbisenc)));
      } else {
        g_assert_not_reached ();
      }
      res = gst_pad_push_event (vorbisenc->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      gdouble rate, applied_rate;
      GstFormat format;
      gint64 start, stop, position;

      gst_event_parse_new_segment_full (event, &update, &rate, &applied_rate,
          &format, &start, &stop, &position);
      if (format == GST_FORMAT_TIME) {
        gst_segment_set_newsegment (&vorbisenc->segment, update, rate, format,
            start, stop, position);
        if (vorbisenc->initial_ts == GST_CLOCK_TIME_NONE) {
          GST_DEBUG_OBJECT (vorbisenc, "Initial segment %" GST_SEGMENT_FORMAT,
              &vorbisenc->segment);
          vorbisenc->initial_ts = start;
        }
      }
    }
      /* fall through */
    default:
      res = gst_pad_push_event (vorbisenc->srcpad, event);
      break;
  }
  return res;
}

static gboolean
gst_vorbis_enc_buffer_check_discontinuous (GstVorbisEnc * vorbisenc,
    GstClockTime timestamp, GstClockTime duration)
{
  gboolean ret = FALSE;

  if (timestamp != GST_CLOCK_TIME_NONE &&
      vorbisenc->expected_ts != GST_CLOCK_TIME_NONE &&
      timestamp + duration != vorbisenc->expected_ts) {
    /* It turns out that a lot of elements don't generate perfect streams due
     * to rounding errors. So, we permit small errors (< 1/2 a sample) without
     * causing a discont.
     */
    int halfsample = GST_SECOND / vorbisenc->frequency / 2;

    if ((GstClockTimeDiff) (timestamp - vorbisenc->expected_ts) > halfsample) {
      GST_DEBUG_OBJECT (vorbisenc, "Expected TS %" GST_TIME_FORMAT
          ", buffer TS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (vorbisenc->expected_ts), GST_TIME_ARGS (timestamp));
      ret = TRUE;
    }
  }

  if (timestamp != GST_CLOCK_TIME_NONE && duration != GST_CLOCK_TIME_NONE) {
    vorbisenc->expected_ts = timestamp + duration;
  } else
    vorbisenc->expected_ts = GST_CLOCK_TIME_NONE;

  return ret;
}

static GstFlowReturn
gst_vorbis_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVorbisEnc *vorbisenc;
  GstFlowReturn ret = GST_FLOW_OK;
  gfloat *data;
  gulong size;
  gulong i, j;
  float **vorbis_buffer;
  GstBuffer *buf1, *buf2, *buf3;
  gboolean first = FALSE;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstClockTime running_time = GST_CLOCK_TIME_NONE;

  vorbisenc = GST_VORBISENC (GST_PAD_PARENT (pad));

  if (!vorbisenc->setup)
    goto not_setup;

  buffer = gst_audio_buffer_clip (buffer, &vorbisenc->segment,
      vorbisenc->frequency, 4 * vorbisenc->channels);
  if (buffer == NULL) {
    GST_DEBUG_OBJECT (vorbisenc, "Dropping buffer, out of segment");
    return GST_FLOW_OK;
  }
  running_time =
      gst_segment_to_running_time (&vorbisenc->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (buffer));
  timestamp = running_time + vorbisenc->initial_ts;
  GST_DEBUG_OBJECT (vorbisenc, "Initial ts is %" GST_TIME_FORMAT,
      GST_TIME_ARGS (vorbisenc->initial_ts));
  if (!vorbisenc->header_sent) {
    /* Vorbis streams begin with three headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.  The
       third header holds the bitstream codebook.  We merely need to
       make the headers, then pass them to libvorbis one at a time;
       libvorbis handles the additional Ogg bitstream constraints */
    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;
    GstCaps *caps;

    /* first, make sure header buffers get timestamp == 0 */
    vorbisenc->next_ts = 0;
    vorbisenc->granulepos_offset = 0;
    vorbisenc->subgranule_offset = 0;

    GST_DEBUG_OBJECT (vorbisenc, "creating and sending header packets");
    gst_vorbis_enc_set_metadata (vorbisenc);
    vorbis_analysis_headerout (&vorbisenc->vd, &vorbisenc->vc, &header,
        &header_comm, &header_code);
    vorbis_comment_clear (&vorbisenc->vc);

    /* create header buffers */
    buf1 = gst_vorbis_enc_buffer_from_header_packet (vorbisenc, &header);
    buf2 = gst_vorbis_enc_buffer_from_header_packet (vorbisenc, &header_comm);
    buf3 = gst_vorbis_enc_buffer_from_header_packet (vorbisenc, &header_code);

    /* mark and put on caps */
    vorbisenc->srccaps = gst_caps_new_simple ("audio/x-vorbis", NULL);
    caps = vorbisenc->srccaps;
    caps = gst_vorbis_enc_set_header_on_caps (caps, buf1, buf2, buf3);

    /* negotiate with these caps */
    GST_DEBUG ("here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (vorbisenc->srcpad, caps);

    gst_buffer_set_caps (buf1, caps);
    gst_buffer_set_caps (buf2, caps);
    gst_buffer_set_caps (buf3, caps);

    /* push out buffers */
    /* push_buffer takes the reference even for failure */
    if ((ret = gst_vorbis_enc_push_buffer (vorbisenc, buf1)) != GST_FLOW_OK)
      goto failed_header_push;
    if ((ret = gst_vorbis_enc_push_buffer (vorbisenc, buf2)) != GST_FLOW_OK) {
      buf2 = NULL;
      goto failed_header_push;
    }
    if ((ret = gst_vorbis_enc_push_buffer (vorbisenc, buf3)) != GST_FLOW_OK) {
      buf3 = NULL;
      goto failed_header_push;
    }

    /* now adjust starting granulepos accordingly if the buffer's timestamp is
       nonzero */
    vorbisenc->next_ts = timestamp;
    vorbisenc->expected_ts = timestamp;
    vorbisenc->granulepos_offset = gst_util_uint64_scale
        (running_time, vorbisenc->frequency, GST_SECOND);
    vorbisenc->subgranule_offset = 0;
    vorbisenc->subgranule_offset =
        (vorbisenc->next_ts - vorbisenc->initial_ts) -
        granulepos_to_timestamp_offset (vorbisenc, 0);

    vorbisenc->header_sent = TRUE;
    first = TRUE;
  }

  if (vorbisenc->expected_ts != GST_CLOCK_TIME_NONE &&
      timestamp < vorbisenc->expected_ts) {
    guint64 diff = vorbisenc->expected_ts - timestamp;
    guint64 diff_bytes;

    GST_WARNING_OBJECT (vorbisenc, "Buffer is older than previous "
        "timestamp + duration (%" GST_TIME_FORMAT "< %" GST_TIME_FORMAT
        "), cannot handle. Clipping buffer.",
        GST_TIME_ARGS (timestamp), GST_TIME_ARGS (vorbisenc->expected_ts));

    diff_bytes =
        GST_CLOCK_TIME_TO_FRAMES (diff,
        vorbisenc->frequency) * vorbisenc->channels * sizeof (gfloat);
    if (diff_bytes >= GST_BUFFER_SIZE (buffer)) {
      gst_buffer_unref (buffer);
      return GST_FLOW_OK;
    }
    buffer = gst_buffer_make_metadata_writable (buffer);
    GST_BUFFER_DATA (buffer) += diff_bytes;
    GST_BUFFER_SIZE (buffer) -= diff_bytes;

    GST_BUFFER_TIMESTAMP (buffer) += diff;
    if (GST_BUFFER_DURATION_IS_VALID (buffer))
      GST_BUFFER_DURATION (buffer) -= diff;
  }

  if (gst_vorbis_enc_buffer_check_discontinuous (vorbisenc, timestamp,
          GST_BUFFER_DURATION (buffer)) && !first) {
    GST_WARNING_OBJECT (vorbisenc,
        "Buffer is discontinuous, flushing encoder "
        "and restarting (Discont from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
        ")", GST_TIME_ARGS (vorbisenc->next_ts), GST_TIME_ARGS (timestamp));
    /* Re-initialise encoder (there's unfortunately no API to flush it) */
    if ((ret = gst_vorbis_enc_clear (vorbisenc)) != GST_FLOW_OK)
      return ret;
    if (!gst_vorbis_enc_setup (vorbisenc))
      return GST_FLOW_ERROR;    /* Should be impossible, we can only get here if
                                   we successfully initialised earlier */

    /* Now, set our granulepos offset appropriately. */
    vorbisenc->next_ts = timestamp;
    /* We need to round to the nearest whole number of samples, not just do
     * a truncating division here */
    vorbisenc->granulepos_offset = gst_util_uint64_scale
        (running_time + GST_SECOND / vorbisenc->frequency / 2
        - vorbisenc->subgranule_offset, vorbisenc->frequency, GST_SECOND);

    vorbisenc->header_sent = TRUE;

    /* And our next output buffer must have DISCONT set on it */
    vorbisenc->next_discont = TRUE;
  }

  /* Sending zero samples to libvorbis marks EOS, so we mustn't do that */
  if (GST_BUFFER_SIZE (buffer) == 0) {
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  /* data to encode */
  data = (gfloat *) GST_BUFFER_DATA (buffer);
  size = GST_BUFFER_SIZE (buffer) / (vorbisenc->channels * sizeof (float));

  /* expose the buffer to submit data */
  vorbis_buffer = vorbis_analysis_buffer (&vorbisenc->vd, size);

  /* deinterleave samples, write the buffer data */
  for (i = 0; i < size; i++) {
    for (j = 0; j < vorbisenc->channels; j++) {
      vorbis_buffer[j][i] = *data++;
    }
  }

  /* tell the library how much we actually submitted */
  vorbis_analysis_wrote (&vorbisenc->vd, size);

  GST_LOG_OBJECT (vorbisenc, "wrote %lu samples to vorbis", size);

  vorbisenc->samples_in += size;

  gst_buffer_unref (buffer);

  ret = gst_vorbis_enc_output_buffers (vorbisenc);

  return ret;

  /* error cases */
not_setup:
  {
    gst_buffer_unref (buffer);
    GST_ELEMENT_ERROR (vorbisenc, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized (input is not audio?)"));
    return GST_FLOW_UNEXPECTED;
  }
failed_header_push:
  {
    GST_WARNING_OBJECT (vorbisenc, "Failed to push headers");
    /* buf1 is always already unreffed */
    if (buf2)
      gst_buffer_unref (buf2);
    if (buf3)
      gst_buffer_unref (buf3);
    gst_buffer_unref (buffer);
    return ret;
  }
}

static GstFlowReturn
gst_vorbis_enc_output_buffers (GstVorbisEnc * vorbisenc)
{
  GstFlowReturn ret;

  /* vorbis does some data preanalysis, then divides up blocks for
     more involved (potentially parallel) processing.  Get a single
     block for encoding now */
  while (vorbis_analysis_blockout (&vorbisenc->vd, &vorbisenc->vb) == 1) {
    ogg_packet op;

    GST_LOG_OBJECT (vorbisenc, "analysed to a block");

    /* analysis */
    vorbis_analysis (&vorbisenc->vb, NULL);
    vorbis_bitrate_addblock (&vorbisenc->vb);

    while (vorbis_bitrate_flushpacket (&vorbisenc->vd, &op)) {
      GST_LOG_OBJECT (vorbisenc, "pushing out a data packet");
      ret = gst_vorbis_enc_push_packet (vorbisenc, &op);

      if (ret != GST_FLOW_OK)
        return ret;
    }
  }

  return GST_FLOW_OK;
}

static void
gst_vorbis_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVorbisEnc *vorbisenc;

  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_MAX_BITRATE:
      g_value_set_int (value, vorbisenc->max_bitrate);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, vorbisenc->bitrate);
      break;
    case ARG_MIN_BITRATE:
      g_value_set_int (value, vorbisenc->min_bitrate);
      break;
    case ARG_QUALITY:
      g_value_set_float (value, vorbisenc->quality);
      break;
    case ARG_MANAGED:
      g_value_set_boolean (value, vorbisenc->managed);
      break;
    case ARG_LAST_MESSAGE:
      g_value_set_string (value, vorbisenc->last_message);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vorbis_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVorbisEnc *vorbisenc;

  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_MAX_BITRATE:
    {
      gboolean old_value = vorbisenc->managed;

      vorbisenc->max_bitrate = g_value_get_int (value);
      if (vorbisenc->max_bitrate >= 0
          && vorbisenc->max_bitrate < LOWEST_BITRATE) {
        g_warning ("Lowest allowed bitrate is %d", LOWEST_BITRATE);
        vorbisenc->max_bitrate = LOWEST_BITRATE;
      }
      if (vorbisenc->min_bitrate > 0 && vorbisenc->max_bitrate > 0)
        vorbisenc->managed = TRUE;
      else
        vorbisenc->managed = FALSE;

      if (old_value != vorbisenc->managed)
        g_object_notify (object, "managed");
      break;
    }
    case ARG_BITRATE:
      vorbisenc->bitrate = g_value_get_int (value);
      if (vorbisenc->bitrate >= 0 && vorbisenc->bitrate < LOWEST_BITRATE) {
        g_warning ("Lowest allowed bitrate is %d", LOWEST_BITRATE);
        vorbisenc->bitrate = LOWEST_BITRATE;
      }
      break;
    case ARG_MIN_BITRATE:
    {
      gboolean old_value = vorbisenc->managed;

      vorbisenc->min_bitrate = g_value_get_int (value);
      if (vorbisenc->min_bitrate >= 0
          && vorbisenc->min_bitrate < LOWEST_BITRATE) {
        g_warning ("Lowest allowed bitrate is %d", LOWEST_BITRATE);
        vorbisenc->min_bitrate = LOWEST_BITRATE;
      }
      if (vorbisenc->min_bitrate > 0 && vorbisenc->max_bitrate > 0)
        vorbisenc->managed = TRUE;
      else
        vorbisenc->managed = FALSE;

      if (old_value != vorbisenc->managed)
        g_object_notify (object, "managed");
      break;
    }
    case ARG_QUALITY:
      vorbisenc->quality = g_value_get_float (value);
      if (vorbisenc->quality >= 0.0)
        vorbisenc->quality_set = TRUE;
      else
        vorbisenc->quality_set = FALSE;
      break;
    case ARG_MANAGED:
      vorbisenc->managed = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_vorbis_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstVorbisEnc *vorbisenc = GST_VORBISENC (element);
  GstStateChangeReturn res;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      vorbisenc->tags = gst_tag_list_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      vorbisenc->setup = FALSE;
      vorbisenc->next_discont = FALSE;
      vorbisenc->header_sent = FALSE;
      gst_segment_init (&vorbisenc->segment, GST_FORMAT_TIME);
      vorbisenc->initial_ts = GST_CLOCK_TIME_NONE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      vorbis_block_clear (&vorbisenc->vb);
      vorbis_dsp_clear (&vorbisenc->vd);
      vorbis_info_clear (&vorbisenc->vi);
      g_free (vorbisenc->last_message);
      vorbisenc->last_message = NULL;
      if (vorbisenc->srccaps) {
        gst_caps_unref (vorbisenc->srccaps);
        vorbisenc->srccaps = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tag_list_free (vorbisenc->tags);
      vorbisenc->tags = NULL;
    default:
      break;
  }

  return res;
}
