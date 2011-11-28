/*
 * gstcmmlenc.c - GStreamer CMML encoder
 * Copyright (C) 2005 Alessandro Decina
 * 
 * Authors:
 *   Alessandro Decina <alessandro@nnva.org>
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
 * SECTION:element-cmmlenc
 * @see_also: cmmldec, oggmux
 *
 * Cmmlenc encodes a CMML document into a CMML stream.  <ulink
 * url="http://www.annodex.net/TR/draft-pfeiffer-cmml-02.html">CMML</ulink> is
 * an XML markup language for time-continuous data maintained by the <ulink
 * url="http:/www.annodex.org/">Annodex Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v filesrc location=annotations.cmml ! cmmlenc ! oggmux name=mux ! filesink location=annotated.ogg
 * ]|
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstcmmlenc.h"
#include "gstannodex.h"

GST_DEBUG_CATEGORY_STATIC (cmmlenc);
#define GST_CAT_DEFAULT cmmlenc

#define CMML_IDENT_HEADER_SIZE 29

enum
{
  ARG_0,
  GST_CMML_ENC_GRANULERATE_N,
  GST_CMML_ENC_GRANULERATE_D,
  GST_CMML_ENC_GRANULESHIFT
};

enum
{
  LAST_SIGNAL
};

static GstStaticPadTemplate gst_cmml_enc_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-cmml, encoded = (boolean) true")
    );

static GstStaticPadTemplate gst_cmml_enc_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-cmml, encoded = (boolean) false")
    );

GST_BOILERPLATE (GstCmmlEnc, gst_cmml_enc, GstElement, GST_TYPE_ELEMENT);
static void gst_cmml_enc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec);
static void gst_cmml_enc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_cmml_enc_sink_event (GstPad * pad, GstEvent * event);
static GstStateChangeReturn gst_cmml_enc_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_cmml_enc_chain (GstPad * pad, GstBuffer * buffer);
static void gst_cmml_enc_parse_preamble (GstCmmlEnc * enc,
    guchar * preamble, guchar * processing_instruction);
static void gst_cmml_enc_parse_end_tag (GstCmmlEnc * enc);
static void gst_cmml_enc_parse_tag_head (GstCmmlEnc * enc,
    GstCmmlTagHead * head);
static void gst_cmml_enc_parse_tag_clip (GstCmmlEnc * enc,
    GstCmmlTagClip * tag);
static GstFlowReturn gst_cmml_enc_new_buffer (GstCmmlEnc * enc,
    guchar * data, gint size, GstBuffer ** buffer);
static GstFlowReturn gst_cmml_enc_push_clip (GstCmmlEnc * enc,
    GstCmmlTagClip * clip, GstClockTime prev_clip_time);
static GstFlowReturn gst_cmml_enc_push (GstCmmlEnc * enc, GstBuffer * buffer);

static void gst_cmml_enc_finalize (GObject * object);

static void
gst_cmml_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_cmml_enc_sink_factory);
  gst_element_class_add_static_pad_template (element_class,
      &gst_cmml_enc_src_factory);
  gst_element_class_set_details_simple (element_class, "CMML streams encoder",
      "Codec/Encoder",
      "Encodes CMML streams", "Alessandro Decina <alessandro@nnva.org>");
}

static void
gst_cmml_enc_class_init (GstCmmlEncClass * enc_class)
{
  GObjectClass *klass = G_OBJECT_CLASS (enc_class);

  klass->get_property = gst_cmml_enc_get_property;
  klass->set_property = gst_cmml_enc_set_property;
  klass->finalize = gst_cmml_enc_finalize;

  g_object_class_install_property (klass, GST_CMML_ENC_GRANULERATE_N,
      g_param_spec_int64 ("granule-rate-numerator",
          "Granulerate numerator",
          "Granulerate numerator",
          0, G_MAXINT64, 1000,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_ENC_GRANULERATE_D,
      g_param_spec_int64 ("granule-rate-denominator",
          "Granulerate denominator",
          "Granulerate denominator",
          0, G_MAXINT64, 1,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (klass, GST_CMML_ENC_GRANULESHIFT,
      g_param_spec_uchar ("granule-shift",
          "Granuleshift",
          "The number of lower bits to use for partitioning a granule position",
          0, 64, 32,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  GST_ELEMENT_CLASS (klass)->change_state = gst_cmml_enc_change_state;
}

static void
gst_cmml_enc_init (GstCmmlEnc * enc, GstCmmlEncClass * klass)
{
  enc->sinkpad =
      gst_pad_new_from_static_template (&gst_cmml_enc_sink_factory, "sink");
  gst_pad_set_chain_function (enc->sinkpad, gst_cmml_enc_chain);
  gst_pad_set_event_function (enc->sinkpad, gst_cmml_enc_sink_event);
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);

  enc->srcpad =
      gst_pad_new_from_static_template (&gst_cmml_enc_src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->major = 3;
  enc->minor = 0;
}

static void
gst_cmml_enc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCmmlEnc *enc = GST_CMML_ENC (object);

  switch (property_id) {
    case GST_CMML_ENC_GRANULERATE_N:
      /* XXX: may need to flush clips */
      enc->granulerate_n = g_value_get_int64 (value);
      break;
    case GST_CMML_ENC_GRANULERATE_D:
      enc->granulerate_d = g_value_get_int64 (value);
      break;
    case GST_CMML_ENC_GRANULESHIFT:
      enc->granuleshift = g_value_get_uchar (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_cmml_enc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstCmmlEnc *enc = GST_CMML_ENC (object);

  switch (property_id) {
    case GST_CMML_ENC_GRANULERATE_N:
      g_value_set_int64 (value, enc->granulerate_n);
      break;
    case GST_CMML_ENC_GRANULERATE_D:
      g_value_set_int64 (value, enc->granulerate_d);
      break;
    case GST_CMML_ENC_GRANULESHIFT:
      g_value_set_uchar (value, enc->granuleshift);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
gst_cmml_enc_finalize (GObject * object)
{
  GstCmmlEnc *enc = GST_CMML_ENC (object);

  if (enc->tracks) {
    gst_cmml_track_list_destroy (enc->tracks);
    enc->tracks = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_cmml_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstCmmlEnc *enc = GST_CMML_ENC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      enc->parser = gst_cmml_parser_new (GST_CMML_PARSER_ENCODE);
      enc->parser->user_data = enc;
      enc->parser->preamble_callback =
          (GstCmmlParserPreambleCallback) gst_cmml_enc_parse_preamble;
      enc->parser->head_callback =
          (GstCmmlParserHeadCallback) gst_cmml_enc_parse_tag_head;
      enc->parser->clip_callback =
          (GstCmmlParserClipCallback) gst_cmml_enc_parse_tag_clip;
      enc->parser->cmml_end_callback =
          (GstCmmlParserCmmlEndCallback) gst_cmml_enc_parse_end_tag;
      enc->tracks = gst_cmml_track_list_new ();
      enc->sent_headers = FALSE;
      enc->sent_eos = FALSE;
      enc->flow_return = GST_FLOW_OK;
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      gst_cmml_track_list_destroy (enc->tracks);
      enc->tracks = NULL;
      g_free (enc->preamble);
      enc->preamble = NULL;
      gst_cmml_parser_free (enc->parser);
      break;
    }
    default:
      break;
  }

  return res;
}

static gboolean
gst_cmml_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstCmmlEnc *enc = GST_CMML_ENC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      if (!enc->sent_eos)
        gst_cmml_enc_parse_end_tag (enc);

      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static GstFlowReturn
gst_cmml_enc_new_buffer (GstCmmlEnc * enc,
    guchar * data, gint size, GstBuffer ** buffer)
{
  GstFlowReturn res;

  res = gst_pad_alloc_buffer (enc->srcpad, GST_BUFFER_OFFSET_NONE, size,
      NULL, buffer);
  if (res == GST_FLOW_OK) {
    if (data)
      memcpy (GST_BUFFER_DATA (*buffer), data, size);
  } else {
    GST_WARNING_OBJECT (enc, "alloc function returned error %s",
        gst_flow_get_name (res));
  }

  return res;
}

static GstCaps *
gst_cmml_enc_set_header_on_caps (GstCmmlEnc * enc, GstCaps * caps,
    GstBuffer * ident, GstBuffer * preamble, GstBuffer * head)
{
  GValue array = { 0 };
  GValue value = { 0 };
  GstStructure *structure;
  GstBuffer *buffer;

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);
  g_value_init (&value, GST_TYPE_BUFFER);

  /* Make copies of header buffers to avoid circular references */
  buffer = gst_buffer_copy (ident);
  gst_value_set_buffer (&value, buffer);
  gst_value_array_append_value (&array, &value);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_copy (preamble);
  gst_value_set_buffer (&value, buffer);
  gst_value_array_append_value (&array, &value);
  gst_buffer_unref (buffer);

  buffer = gst_buffer_copy (head);
  gst_value_set_buffer (&value, buffer);
  gst_value_array_append_value (&array, &value);
  gst_buffer_unref (buffer);

  GST_BUFFER_FLAG_SET (ident, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (preamble, GST_BUFFER_FLAG_IN_CAPS);
  GST_BUFFER_FLAG_SET (head, GST_BUFFER_FLAG_IN_CAPS);

  gst_structure_set_value (structure, "streamheader", &array);

  g_value_unset (&value);
  g_value_unset (&array);

  return caps;
}

/* create a CMML ident header buffer
 */
static GstFlowReturn
gst_cmml_enc_new_ident_header (GstCmmlEnc * enc, GstBuffer ** buffer)
{
  guint8 ident_header[CMML_IDENT_HEADER_SIZE];
  guint8 *wptr = ident_header;

  memcpy (wptr, "CMML\0\0\0\0", 8);
  wptr += 8;
  GST_WRITE_UINT16_LE (wptr, enc->major);
  wptr += 2;
  GST_WRITE_UINT16_LE (wptr, enc->minor);
  wptr += 2;
  GST_WRITE_UINT64_LE (wptr, enc->granulerate_n);
  wptr += 8;
  GST_WRITE_UINT64_LE (wptr, enc->granulerate_d);
  wptr += 8;
  *wptr = enc->granuleshift;

  return gst_cmml_enc_new_buffer (enc,
      (guchar *) & ident_header, CMML_IDENT_HEADER_SIZE, buffer);
}

/* parse the CMML preamble */
static void
gst_cmml_enc_parse_preamble (GstCmmlEnc * enc,
    guchar * preamble, guchar * processing_instruction)
{
  GST_INFO_OBJECT (enc, "parsing preamble");

  /* save the preamble: it will be pushed when the head tag is found */
  enc->preamble = (guchar *) g_strconcat ((gchar *) preamble,
      (gchar *) processing_instruction, NULL);
}

/* parse the CMML end tag */
static void
gst_cmml_enc_parse_end_tag (GstCmmlEnc * enc)
{
  GstBuffer *buffer;

  GST_INFO_OBJECT (enc, "parsing end tag");

  /* push an empty buffer to signal EOS */
  enc->flow_return = gst_cmml_enc_new_buffer (enc, NULL, 0, &buffer);
  if (enc->flow_return == GST_FLOW_OK) {
    /* set granulepos 0 on EOS */
    GST_BUFFER_OFFSET_END (buffer) = 0;
    enc->flow_return = gst_cmml_enc_push (enc, buffer);
    enc->sent_eos = TRUE;
  }

  return;
}

/* encode the CMML head tag and push the CMML headers
 */
static void
gst_cmml_enc_parse_tag_head (GstCmmlEnc * enc, GstCmmlTagHead * head)
{
  GList *headers = NULL;
  GList *walk;
  guchar *head_string;
  GstCaps *caps;
  GstBuffer *ident_buf, *preamble_buf, *head_buf;
  GstBuffer *buffer;

  if (enc->preamble == NULL)
    goto flow_unexpected;

  GST_INFO_OBJECT (enc, "parsing head tag");

  enc->flow_return = gst_cmml_enc_new_ident_header (enc, &ident_buf);
  if (enc->flow_return != GST_FLOW_OK)
    goto alloc_error;
  headers = g_list_append (headers, ident_buf);

  enc->flow_return = gst_cmml_enc_new_buffer (enc,
      enc->preamble, strlen ((gchar *) enc->preamble), &preamble_buf);
  if (enc->flow_return != GST_FLOW_OK)
    goto alloc_error;
  headers = g_list_append (headers, preamble_buf);

  head_string = gst_cmml_parser_tag_head_to_string (enc->parser, head);
  enc->flow_return = gst_cmml_enc_new_buffer (enc,
      head_string, strlen ((gchar *) head_string), &head_buf);
  g_free (head_string);
  if (enc->flow_return != GST_FLOW_OK)
    goto alloc_error;
  headers = g_list_append (headers, head_buf);

  caps = gst_pad_get_caps (enc->srcpad);
  caps = gst_cmml_enc_set_header_on_caps (enc, caps,
      ident_buf, preamble_buf, head_buf);

  while (headers) {
    buffer = GST_BUFFER (headers->data);
    /* set granulepos 0 on headers */
    GST_BUFFER_OFFSET_END (buffer) = 0;
    gst_buffer_set_caps (buffer, caps);

    enc->flow_return = gst_cmml_enc_push (enc, buffer);
    headers = g_list_delete_link (headers, headers);

    if (enc->flow_return != GST_FLOW_OK)
      goto push_error;
  }

  gst_caps_unref (caps);

  enc->sent_headers = TRUE;
  return;

flow_unexpected:
  GST_ELEMENT_ERROR (enc, STREAM, ENCODE,
      (NULL), ("got head tag before preamble"));
  enc->flow_return = GST_FLOW_ERROR;
  return;
push_error:
  gst_caps_unref (caps);
  /* fallthrough */
alloc_error:
  for (walk = headers; walk; walk = walk->next)
    gst_buffer_unref (GST_BUFFER (walk->data));
  g_list_free (headers);
  return;
}

/* encode a CMML clip tag
 * remove the start and end attributes (GstCmmlParser does this itself) and
 * push the tag with the timestamp of its start attribute. If the tag has the
 * end attribute, create a new empty clip and encode it.
 */
static void
gst_cmml_enc_parse_tag_clip (GstCmmlEnc * enc, GstCmmlTagClip * clip)
{
  GstCmmlTagClip *prev_clip;
  GstClockTime prev_clip_time = GST_CLOCK_TIME_NONE;

  /* this can happen if there's a programming error (eg user forgets to set
   * the start-time property) or if one of the gst_cmml_clock_time_from_*
   * overflows in GstCmmlParser */
  if (clip->start_time == GST_CLOCK_TIME_NONE) {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE,
        (NULL), ("invalid start time for clip (%s)", clip->id));
    enc->flow_return = GST_FLOW_ERROR;

    return;
  }

  /* get the previous clip's start time to encode the current granulepos */
  prev_clip = gst_cmml_track_list_get_track_last_clip (enc->tracks,
      (gchar *) clip->track);
  if (prev_clip) {
    prev_clip_time = prev_clip->start_time;
    if (prev_clip_time > clip->start_time) {
      GST_ELEMENT_ERROR (enc, STREAM, ENCODE,
          (NULL), ("previous clip start time > current clip (%s) start time",
              clip->id));
      enc->flow_return = GST_FLOW_ERROR;
      return;
    }

    /* we don't need the prev clip anymore */
    gst_cmml_track_list_del_clip (enc->tracks, prev_clip);
  }

  /* add the current clip to the tracklist */
  gst_cmml_track_list_add_clip (enc->tracks, clip);

  enc->flow_return = gst_cmml_enc_push_clip (enc, clip, prev_clip_time);
}

static GstFlowReturn
gst_cmml_enc_push_clip (GstCmmlEnc * enc, GstCmmlTagClip * clip,
    GstClockTime prev_clip_time)
{
  GstFlowReturn res;
  GstBuffer *buffer;
  gchar *clip_string;
  gint64 granulepos;

  /* encode the clip */
  clip_string =
      (gchar *) gst_cmml_parser_tag_clip_to_string (enc->parser, clip);

  res = gst_cmml_enc_new_buffer (enc,
      (guchar *) clip_string, strlen (clip_string), &buffer);
  g_free (clip_string);
  if (res != GST_FLOW_OK)
    goto done;

  GST_INFO_OBJECT (enc, "encoding clip"
      "(start-time: %" GST_TIME_FORMAT " end-time: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (clip->start_time), GST_TIME_ARGS (clip->end_time));

  /* set the granulepos */
  granulepos = gst_cmml_clock_time_to_granule (prev_clip_time, clip->start_time,
      enc->granulerate_n, enc->granulerate_d, enc->granuleshift);
  if (granulepos == -1) {
    gst_buffer_unref (buffer);
    goto granule_overflow;
  }

  GST_BUFFER_OFFSET (buffer) = clip->start_time;
  GST_BUFFER_OFFSET_END (buffer) = granulepos;
  GST_BUFFER_TIMESTAMP (buffer) = clip->start_time;

  res = gst_cmml_enc_push (enc, buffer);
  if (res != GST_FLOW_OK)
    goto done;

  if (clip->end_time != GST_CLOCK_TIME_NONE) {
    /* create a new empty clip for the same cmml track starting at end_time
     */
    GObject *end_clip = g_object_new (GST_TYPE_CMML_TAG_CLIP,
        "start-time", clip->end_time, "track", clip->track, NULL);

    /* encode the empty end clip */
    gst_cmml_enc_push_clip (enc, GST_CMML_TAG_CLIP (end_clip),
        clip->start_time);
    g_object_unref (end_clip);
  }
done:
  return res;

granule_overflow:
  GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL), ("granulepos overflow"));
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_cmml_enc_push (GstCmmlEnc * enc, GstBuffer * buffer)
{
  GstFlowReturn res;

  res = gst_pad_push (enc->srcpad, buffer);
  if (res != GST_FLOW_OK)
    GST_WARNING_OBJECT (enc, "push returned: %s", gst_flow_get_name (res));

  return res;
}

static GstFlowReturn
gst_cmml_enc_chain (GstPad * pad, GstBuffer * buffer)
{
  GError *err = NULL;
  GstCmmlEnc *enc = GST_CMML_ENC (GST_PAD_PARENT (pad));

  /* the CMML handlers registered with enc->parser will override this when
   * encoding/pushing the buffers downstream
   */
  enc->flow_return = GST_FLOW_OK;

  if (!gst_cmml_parser_parse_chunk (enc->parser,
          (gchar *) GST_BUFFER_DATA (buffer), GST_BUFFER_SIZE (buffer), &err)) {
    GST_ELEMENT_ERROR (enc, STREAM, ENCODE, (NULL), ("%s", err->message));
    g_error_free (err);
    enc->flow_return = GST_FLOW_ERROR;
  }

  gst_buffer_unref (buffer);
  return enc->flow_return;
}

gboolean
gst_cmml_enc_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "cmmlenc", GST_RANK_NONE,
          GST_TYPE_CMML_ENC))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (cmmlenc, "cmmlenc", 0,
      "annodex cmml decoding element");

  return TRUE;
}
