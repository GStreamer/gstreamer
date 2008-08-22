/* GStreamer Celt Encoder
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * Based on the speexenc element
 */

/**
 * SECTION:element-celtenc
 * @see_also: celtdec, oggmux
 *
 * This element raw audio to CELT.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc wave=sine num-buffers=100 ! audioconvert ! celtenc ! oggmux ! filesink location=sine.ogg
 * ]| Encode a test sine signal to Ogg/CELT.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <celt/celt.h>
#include <celt/celt_header.h>

#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>
#include "gstceltenc.h"

GST_DEBUG_CATEGORY_STATIC (celtenc_debug);
#define GST_CAT_DEFAULT celtenc_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 32000, 64000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-celt, "
        "rate = (int) [ 32000, 64000 ], " "channels = (int) [ 1, 2 ]")
    );

static const GstElementDetails celtenc_details =
GST_ELEMENT_DETAILS ("Celt audio encoder",
    "Codec/Encoder/Audio",
    "Encodes audio in Celt format",
    "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

#define DEFAULT_BITRATE         128
#define DEFAULT_FRAMESIZE       256

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_FRAMESIZE
};

static void gst_celt_enc_finalize (GObject * object);

static gboolean gst_celt_enc_sinkevent (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_celt_enc_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_celt_enc_setup (GstCeltEnc * enc);

static void gst_celt_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_celt_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_celt_enc_change_state (GstElement * element,
    GstStateChange transition);

static void
gst_celt_enc_setup_interfaces (GType celtenc_type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };

  g_type_add_interface_static (celtenc_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);

  GST_DEBUG_CATEGORY_INIT (celtenc_debug, "celtenc", 0, "Celt encoder");
}

GST_BOILERPLATE_FULL (GstCeltEnc, gst_celt_enc, GstElement, GST_TYPE_ELEMENT,
    gst_celt_enc_setup_interfaces);

static void
gst_celt_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details_simple (element_class, "Celt audio encoder",
      "Codec/Encoder/Audio",
      "Encodes audio in Celt format",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_celt_enc_class_init (GstCeltEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_celt_enc_set_property;
  gobject_class->get_property = gst_celt_enc_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BITRATE,
      g_param_spec_int ("bitrate", "Encoding Bit-rate",
          "Specify an encoding bit-rate (in bps). (0 = automatic)",
          0, 150, DEFAULT_BITRATE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAMESIZE,
      g_param_spec_int ("framesize", "Frame Size",
          "The number of samples per frame", 64, 256, DEFAULT_FRAMESIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_celt_enc_finalize);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_celt_enc_change_state);
}

static void
gst_celt_enc_finalize (GObject * object)
{
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (object);

  g_object_unref (enc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_celt_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstCeltEnc *enc;
  GstStructure *structure;

  enc = GST_CELT_ENC (GST_PAD_PARENT (pad));
  enc->setup = FALSE;

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &enc->channels);
  gst_structure_get_int (structure, "rate", &enc->rate);

  gst_celt_enc_setup (enc);

  return enc->setup;
}


static GstCaps *
gst_celt_enc_sink_getcaps (GstPad * pad)
{
  GstCaps *caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  GstCaps *peercaps = NULL;
  GstCeltEnc *enc = GST_CELT_ENC (gst_pad_get_parent_element (pad));

  peercaps = gst_pad_peer_get_caps (enc->srcpad);

  if (peercaps) {
    if (!gst_caps_is_empty (peercaps) && !gst_caps_is_any (peercaps)) {
      GstStructure *ps = gst_caps_get_structure (peercaps, 0);
      GstStructure *s = gst_caps_get_structure (caps, 0);
      gint rate, channels;

      if (gst_structure_get_int (ps, "rate", &rate)) {
        gst_structure_fixate_field_nearest_int (s, "rate", rate);
      }

      if (gst_structure_get_int (ps, "channels", &channels)) {
        gst_structure_fixate_field_nearest_int (s, "channels", channels);
      }
    }
    gst_caps_unref (peercaps);
  }

  gst_object_unref (enc);

  return caps;
}


static gboolean
gst_celt_enc_convert_src (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstCeltEnc *enc;
  gint64 avg;

  enc = GST_CELT_ENC (GST_PAD_PARENT (pad));

  if (enc->samples_in == 0 || enc->bytes_out == 0 || enc->rate == 0)
    return FALSE;

  avg = (enc->bytes_out * enc->rate) / (enc->samples_in);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / avg;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * avg / GST_SECOND;
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

static gboolean
gst_celt_enc_convert_sink (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  gint bytes_per_sample;
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (GST_PAD_PARENT (pad));

  bytes_per_sample = enc->channels * 2;

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
          gint byterate = bytes_per_sample * enc->rate;

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
          if (enc->rate == 0)
            return FALSE;
          *dest_value = src_value * GST_SECOND / enc->rate;
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
          *dest_value = src_value * scale * enc->rate / GST_SECOND;
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

static gint64
gst_celt_enc_get_latency (GstCeltEnc * enc)
{
  return gst_util_uint64_scale (enc->frame_size, GST_SECOND, enc->rate);
}

static const GstQueryType *
gst_celt_enc_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_celt_enc_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_LATENCY,
    0
  };

  return gst_celt_enc_src_query_types;
}

static gboolean
gst_celt_enc_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat fmt, req_fmt;
      gint64 pos, val;

      gst_query_parse_position (query, &req_fmt, NULL);
      if ((res = gst_pad_query_peer_position (enc->sinkpad, &req_fmt, &val))) {
        gst_query_set_position (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_peer_position (enc->sinkpad, &fmt, &pos)))
        break;

      if ((res =
              gst_pad_query_peer_convert (enc->sinkpad, fmt, pos, &req_fmt,
                  &val)))
        gst_query_set_position (query, req_fmt, val);

      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat fmt, req_fmt;
      gint64 dur, val;

      gst_query_parse_duration (query, &req_fmt, NULL);
      if ((res = gst_pad_query_peer_duration (enc->sinkpad, &req_fmt, &val))) {
        gst_query_set_duration (query, req_fmt, val);
        break;
      }

      fmt = GST_FORMAT_TIME;
      if (!(res = gst_pad_query_peer_duration (enc->sinkpad, &fmt, &dur)))
        break;

      if ((res =
              gst_pad_query_peer_convert (enc->sinkpad, fmt, dur, &req_fmt,
                  &val))) {
        gst_query_set_duration (query, req_fmt, val);
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res = gst_celt_enc_convert_src (pad, src_fmt, src_val, &dest_fmt,
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

      if ((res = gst_pad_peer_query (pad, query))) {
        gst_query_parse_latency (query, &live, &min_latency, &max_latency);

        latency = gst_celt_enc_get_latency (enc);

        /* add our latency */
        min_latency += latency;
        if (max_latency != -1)
          max_latency += latency;

        gst_query_set_latency (query, live, min_latency, max_latency);
      }
      break;
    }
    default:
      res = gst_pad_peer_query (pad, query);
      break;
  }

error:

  gst_object_unref (enc);

  return res;
}

static gboolean
gst_celt_enc_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              gst_celt_enc_convert_sink (pad, src_fmt, src_val, &dest_fmt,
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
gst_celt_enc_init (GstCeltEnc * enc, GstCeltEncClass * klass)
{
  enc->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_celt_enc_sinkevent));
  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_celt_enc_chain));
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_celt_enc_sink_setcaps));
  gst_pad_set_getcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_celt_enc_sink_getcaps));
  gst_pad_set_query_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_celt_enc_sink_query));

  enc->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_celt_enc_src_query));
  gst_pad_set_query_type_function (enc->srcpad,
      GST_DEBUG_FUNCPTR (gst_celt_enc_get_query_types));
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->channels = -1;
  enc->rate = -1;

  enc->bitrate = DEFAULT_BITRATE;
  enc->frame_size = DEFAULT_FRAMESIZE;

  enc->setup = FALSE;
  enc->header_sent = FALSE;

  enc->adapter = gst_adapter_new ();
}

static GstBuffer *
gst_celt_enc_create_metadata_buffer (GstCeltEnc * enc)
{
  const GstTagList *user_tags;
  GstTagList *merged_tags;
  GstBuffer *comments = NULL;

  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (enc));

  GST_DEBUG_OBJECT (enc, "upstream tags = %" GST_PTR_FORMAT, enc->tags);
  GST_DEBUG_OBJECT (enc, "user-set tags = %" GST_PTR_FORMAT, user_tags);

  /* gst_tag_list_merge() will handle NULL for either or both lists fine */
  merged_tags = gst_tag_list_merge (user_tags, enc->tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (enc)));

  if (merged_tags == NULL)
    merged_tags = gst_tag_list_new ();

  GST_DEBUG_OBJECT (enc, "merged   tags = %" GST_PTR_FORMAT, merged_tags);
  comments = gst_tag_list_to_vorbiscomment_buffer (merged_tags, NULL,
      0, "Encoded with GStreamer Celtenc");
  gst_tag_list_free (merged_tags);

  GST_BUFFER_OFFSET (comments) = enc->bytes_out;
  GST_BUFFER_OFFSET_END (comments) = 0;

  return comments;
}

static gboolean
gst_celt_enc_setup (GstCeltEnc * enc)
{
  gint error = CELT_OK;
  gint bytes_per_packet;

  enc->setup = FALSE;

  /* Fix bitrate */
  if (enc->channels == 1) {
    if (enc->bitrate <= 0)
      enc->bitrate = 64;
    else if (enc->bitrate < 32)
      enc->bitrate = 32;
    else if (enc->bitrate > 110)
      enc->bitrate = 110;
  } else {
    if (enc->bitrate <= 0)
      enc->bitrate = 128;
    else if (enc->bitrate < 64)
      enc->bitrate = 64;
    else if (enc->bitrate > 150)
      enc->bitrate = 150;
  }

  enc->mode =
      celt_mode_create (enc->rate, enc->channels, enc->frame_size, &error);
  if (!enc->mode)
    goto mode_initialization_failed;

  celt_mode_info (enc->mode, CELT_GET_FRAME_SIZE, &enc->frame_size);

  bytes_per_packet =
      (enc->bitrate * 1000 * enc->frame_size / enc->rate + 4) / 8;

  celt_header_init (&enc->header, enc->mode);
  enc->header.nb_channels = enc->channels;

  enc->state = celt_encoder_create (enc->mode);
  if (!enc->state)
    goto encoder_creation_failed;

  GST_LOG_OBJECT (enc, "we have frame size %d", enc->frame_size);

  enc->setup = TRUE;

  return TRUE;

mode_initialization_failed:
  GST_ERROR_OBJECT (enc, "Mode initialization failed: %d", error);
  return FALSE;

encoder_creation_failed:
  GST_ERROR_OBJECT (enc, "Encoder creation failed");
  return FALSE;
}

/* prepare a buffer for transmission */
static GstBuffer *
gst_celt_enc_buffer_from_data (GstCeltEnc * enc, guchar * data,
    gint data_len, guint64 granulepos)
{
  GstBuffer *outbuf;

  outbuf = gst_buffer_new_and_alloc (data_len);
  memcpy (GST_BUFFER_DATA (outbuf), data, data_len);
  GST_BUFFER_OFFSET (outbuf) = enc->bytes_out;
  GST_BUFFER_OFFSET_END (outbuf) = granulepos;

  GST_LOG_OBJECT (enc, "encoded buffer of %d bytes", GST_BUFFER_SIZE (outbuf));
  return outbuf;
}


/* push out the buffer and do internal bookkeeping */
static GstFlowReturn
gst_celt_enc_push_buffer (GstCeltEnc * enc, GstBuffer * buffer)
{
  guint size;

  size = GST_BUFFER_SIZE (buffer);

  enc->bytes_out += size;

  GST_DEBUG_OBJECT (enc, "pushing output buffer of size %u", size);

  return gst_pad_push (enc->srcpad, buffer);
}

static GstCaps *
gst_celt_enc_set_header_on_caps (GstCaps * caps, GstBuffer * buf1,
    GstBuffer * buf2)
{
  GstStructure *structure = NULL;
  GstBuffer *buf;
  GValue array = { 0 };
  GValue value = { 0 };

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_assert (gst_buffer_is_metadata_writable (buf1));
  g_assert (gst_buffer_is_metadata_writable (buf2));

  /* mark buffers */
  GST_BUFFER_FLAG_SET (buf1, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (buf2, GST_BUFFER_FLAG_IN_CAPS);

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
  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&value);
  g_value_unset (&array);

  return caps;
}


static gboolean
gst_celt_enc_sinkevent (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      enc->eos = TRUE;
      res = gst_pad_event_default (pad, event);
      break;
    case GST_EVENT_TAG:
    {
      GstTagList *list;

      gst_event_parse_tag (event, &list);
      if (enc->tags) {
        gst_tag_list_insert (enc->tags, list,
            gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (enc)));
      }
      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (enc);

  return res;
}


static GstFlowReturn
gst_celt_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstCeltEnc *enc;
  GstFlowReturn ret = GST_FLOW_OK;

  enc = GST_CELT_ENC (GST_PAD_PARENT (pad));

  if (!enc->setup)
    goto not_setup;

  if (!enc->header_sent) {
    /* Celt streams begin with two headers; the initial header (with
       most of the codec setup parameters) which is mandated by the Ogg
       bitstream spec.  The second header holds any comment fields.
       We merely need to make the headers, then pass them to libcelt 
       one at a time; libcelt handles the additional Ogg bitstream 
       constraints */
    GstBuffer *buf1, *buf2;
    GstCaps *caps;
    guchar data[100];

    /* create header buffer */
    celt_header_to_packet (&enc->header, data, 100);
    buf1 = gst_celt_enc_buffer_from_data (enc, data, 100, 0);

    /* create comment buffer */
    buf2 = gst_celt_enc_create_metadata_buffer (enc);

    /* mark and put on caps */
    caps = gst_pad_get_caps (enc->srcpad);
    caps = gst_celt_enc_set_header_on_caps (caps, buf1, buf2);

    gst_caps_set_simple (caps,
        "rate", G_TYPE_INT, enc->rate,
        "channels", G_TYPE_INT, enc->channels, NULL);

    /* negotiate with these caps */
    GST_DEBUG_OBJECT (enc, "here are the caps: %" GST_PTR_FORMAT, caps);
    gst_pad_set_caps (enc->srcpad, caps);

    gst_buffer_set_caps (buf1, caps);
    gst_buffer_set_caps (buf2, caps);
    gst_caps_unref (caps);

    /* push out buffers */
    ret = gst_celt_enc_push_buffer (enc, buf1);

    if (ret != GST_FLOW_OK) {
      gst_buffer_unref (buf2);
      goto done;
    }

    ret = gst_celt_enc_push_buffer (enc, buf2);

    if (ret != GST_FLOW_OK)
      goto done;

    enc->header_sent = TRUE;
  }

  {
    gint frame_size = enc->frame_size;
    gint bytes = frame_size * 2 * enc->channels;
    gint bytes_per_packet =
        (enc->bitrate * 1000 * enc->frame_size / enc->rate + 4) / 8;

    GST_DEBUG_OBJECT (enc, "received buffer of %u bytes",
        GST_BUFFER_SIZE (buf));

    /* push buffer to adapter */
    gst_adapter_push (enc->adapter, buf);
    buf = NULL;

    while (gst_adapter_available (enc->adapter) >= bytes) {
      gint16 *data;
      gint outsize;
      GstBuffer *outbuf;

      ret = gst_pad_alloc_buffer_and_set_caps (enc->srcpad,
          GST_BUFFER_OFFSET_NONE, bytes_per_packet, GST_PAD_CAPS (enc->srcpad),
          &outbuf);

      if (GST_FLOW_OK != ret)
        goto done;

      data = (gint16 *) gst_adapter_take (enc->adapter, bytes);
      enc->samples_in += frame_size;

      GST_DEBUG_OBJECT (enc, "encoding %d samples (%d bytes)", frame_size,
          bytes);

      outsize =
          celt_encode (enc->state, data, GST_BUFFER_DATA (outbuf),
          bytes_per_packet);

      g_free (data);

      if (outsize < 0) {
        GST_ERROR_OBJECT (enc, "Encoding failed: %d", outsize);
        ret = GST_FLOW_ERROR;
        goto done;
      }

      enc->frameno++;

      GST_BUFFER_TIMESTAMP (outbuf) =
          gst_util_uint64_scale_int (enc->frameno * frame_size, GST_SECOND,
          enc->rate);
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale_int (frame_size, GST_SECOND, enc->rate);
      /* set gp time and granulepos; see gst-plugins-base/ext/ogg/README */
      GST_BUFFER_OFFSET_END (outbuf) = ((enc->frameno + 1) * frame_size);
      GST_BUFFER_OFFSET (outbuf) =
          gst_util_uint64_scale_int (GST_BUFFER_OFFSET_END (outbuf), GST_SECOND,
          enc->rate);

      ret = gst_celt_enc_push_buffer (enc, outbuf);

      if ((GST_FLOW_OK != ret) && (GST_FLOW_NOT_LINKED != ret))
        goto done;
    }
  }

done:

  if (buf)
    gst_buffer_unref (buf);

  return ret;

  /* ERRORS */
not_setup:
  {
    GST_ELEMENT_ERROR (enc, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized (input is not audio?)"));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

}


static void
gst_celt_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_int (value, enc->bitrate);
      break;
    case PROP_FRAMESIZE:
      g_value_set_int (value, enc->frame_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_celt_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCeltEnc *enc;

  enc = GST_CELT_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      enc->bitrate = g_value_get_int (value);
      break;
    case PROP_FRAMESIZE:
      enc->frame_size = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_celt_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstCeltEnc *enc = GST_CELT_ENC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      enc->tags = gst_tag_list_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      enc->frameno = 0;
      enc->samples_in = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* fall through */
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (res == GST_STATE_CHANGE_FAILURE)
    return res;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      enc->setup = FALSE;
      enc->header_sent = FALSE;
      if (enc->state) {
        celt_encoder_destroy (enc->state);
        enc->state = NULL;
      }
      if (enc->mode) {
        celt_mode_destroy (enc->mode);
        enc->mode = NULL;
      }
      memset (&enc->header, 0, sizeof (enc->header));
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tag_list_free (enc->tags);
      enc->tags = NULL;
    default:
      break;
  }

  return res;
}
