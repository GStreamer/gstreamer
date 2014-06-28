/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
 * Copyright 2008, 2009 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-kateenc
 * @see_also: oggmux
 *
 * <refsect2>
 * <para>
 * This element encodes Kate streams
 * <ulink url="http://libkate.googlecode.com/">Kate</ulink> is a free codec
 * for text based data, such as subtitles. Any number of kate streams can be
 * embedded in an Ogg stream.
 * </para>
 * <para>
 * libkate (see above url) is needed to build this plugin.
 * </para>
 * <title>Example pipeline</title>
 * <para>
 * This encodes a DVD SPU track to a Kate stream:
 * <programlisting>
 * gst-launch dvdreadsrc ! dvddemux ! dvdsubparse ! kateenc category=spu-subtitles ! oggmux ! filesink location=test.ogg
 * </programlisting>
 * </para>
 * </refsect2>
 */

/* FIXME:
 *  - should we automatically pick up the language code from the
 *    upstream event tags if none was set via the property?
 *  - turn category property into an enum (freestyle text property in
 *    combination with supposedly strictly defined known values that
 *    aren't even particularly human-readable is just not very nice)? */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>

#include "gstkate.h"
#include "gstkateutil.h"
#include "gstkatespu.h"
#include "gstkateenc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_kateenc_debug);
#define GST_CAT_DEFAULT gst_kateenc_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_LANGUAGE,
  ARG_CATEGORY,
  ARG_GRANULE_RATE_NUM,
  ARG_GRANULE_RATE_DEN,
  ARG_GRANULE_SHIFT,
  ARG_KEEPALIVE_MIN_TIME,
  ARG_ORIGINAL_CANVAS_WIDTH,
  ARG_ORIGINAL_CANVAS_HEIGHT,
  ARG_DEFAULT_SPU_DURATION,
};

#define DEFAULT_KEEPALIVE_MIN_TIME 2.5f
#define DEFAULT_DEFAULT_SPU_DURATION 1.5f

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-raw, format={ pango-markup, utf8 }; "
        GST_KATE_SPU_MIME_TYPE)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subtitle/x-kate; application/x-kate")
    );

static void gst_kate_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_kate_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_kate_enc_dispose (GObject * object);

static gboolean gst_kate_enc_setcaps (GstKateEnc * ke, GstCaps * caps);
static GstFlowReturn gst_kate_enc_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static GstStateChangeReturn gst_kate_enc_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_kate_enc_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_kate_enc_source_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

#define gst_kate_enc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstKateEnc, gst_kate_enc, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL));

/* initialize the plugin's class */
static void
gst_kate_enc_class_init (GstKateEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_kate_enc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_kate_enc_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_kate_enc_dispose);

  g_object_class_install_property (gobject_class, ARG_LANGUAGE,
      g_param_spec_string ("language", "Language",
          "The language of the stream (e.g. \"fr\" or \"fr_FR\" for French)",
          "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_CATEGORY,
      g_param_spec_string ("category", "Category",
          "The category of the stream", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_GRANULE_RATE_NUM,
      g_param_spec_int ("granule-rate-numerator", "Granule rate numerator",
          "The numerator of the granule rate",
          1, G_MAXINT, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_GRANULE_RATE_DEN,
      g_param_spec_int ("granule-rate-denominator", "Granule rate denominator",
          "The denominator of the granule rate",
          1, G_MAXINT, 1000, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_GRANULE_SHIFT,
      g_param_spec_int ("granule-shift", "Granule shift",
          "The granule shift", 0, 64, 32,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_ORIGINAL_CANVAS_WIDTH,
      g_param_spec_int ("original-canvas-width", "Original canvas width",
          "The width of the canvas this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_ORIGINAL_CANVAS_HEIGHT,
      g_param_spec_int ("original-canvas-height", "Original canvas height",
          "The height of the canvas this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_KEEPALIVE_MIN_TIME,
      g_param_spec_float ("keepalive-min-time", "Keepalive mimimum time",
          "Minimum time to emit keepalive packets (0 disables keepalive packets)",
          0.0f, FLT_MAX, DEFAULT_KEEPALIVE_MIN_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_SPU_DURATION,
      g_param_spec_float ("default-spu-duration", "Default SPU duration",
          "The assumed max duration (in seconds) of SPUs with no duration specified",
          0.0f, FLT_MAX, DEFAULT_DEFAULT_SPU_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_kate_enc_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "Kate stream encoder", "Codec/Encoder/Subtitle",
      "Encodes Kate streams from text or subpictures",
      "Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>");
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_kate_enc_init (GstKateEnc * ke)
{
  GST_DEBUG_OBJECT (ke, "gst_kate_enc_init");

  ke->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (ke->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_enc_chain));
  gst_pad_set_event_function (ke->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_enc_sink_event));
  gst_element_add_pad (GST_ELEMENT (ke), ke->sinkpad);

  ke->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_function (ke->srcpad,
      GST_DEBUG_FUNCPTR (gst_kate_enc_source_query));
  gst_element_add_pad (GST_ELEMENT (ke), ke->srcpad);

  ke->initialized = FALSE;
  ke->headers_sent = FALSE;
  ke->last_timestamp = 0;
  ke->latest_end_time = 0;
  ke->language = NULL;
  ke->category = NULL;
  ke->format = GST_KATE_FORMAT_UNDEFINED;
  ke->granule_rate_numerator = 1000;
  ke->granule_rate_denominator = 1;
  ke->granule_shift = 32;
  ke->original_canvas_width = 0;
  ke->original_canvas_height = 0;
  ke->keepalive_min_time = DEFAULT_KEEPALIVE_MIN_TIME;
  ke->default_spu_duration = DEFAULT_DEFAULT_SPU_DURATION;
  memcpy (ke->spu_clut, gst_kate_spu_default_clut,
      sizeof (gst_kate_spu_default_clut));
  ke->delayed_spu = FALSE;
  ke->delayed_bitmap = NULL;
  ke->delayed_palette = NULL;
  ke->delayed_region = NULL;
}

static void
gst_kate_enc_dispose (GObject * object)
{
  GstKateEnc *ke = GST_KATE_ENC (object);

  GST_LOG_OBJECT (ke, "disposing");

  if (ke->language) {
    g_free (ke->language);
    ke->language = NULL;
  }
  if (ke->category) {
    g_free (ke->category);
    ke->category = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_kate_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKateEnc *ke = GST_KATE_ENC (object);
  const char *str;

  switch (prop_id) {
    case ARG_LANGUAGE:
      if (ke->language) {
        g_free (ke->language);
        ke->language = NULL;
      }
      str = g_value_get_string (value);
      if (str)
        ke->language = g_strdup (str);
      break;
    case ARG_CATEGORY:
      if (ke->category) {
        g_free (ke->category);
        ke->category = NULL;
      }
      str = g_value_get_string (value);
      if (str)
        ke->category = g_strdup (str);
      break;
    case ARG_GRANULE_RATE_NUM:
      ke->granule_rate_numerator = g_value_get_int (value);
      break;
    case ARG_GRANULE_RATE_DEN:
      ke->granule_rate_denominator = g_value_get_int (value);
      break;
    case ARG_GRANULE_SHIFT:
      ke->granule_rate_denominator = g_value_get_int (value);
      break;
    case ARG_KEEPALIVE_MIN_TIME:
      ke->keepalive_min_time = g_value_get_float (value);
      break;
    case ARG_ORIGINAL_CANVAS_WIDTH:
      ke->original_canvas_width = g_value_get_int (value);
      break;
    case ARG_ORIGINAL_CANVAS_HEIGHT:
      ke->original_canvas_height = g_value_get_int (value);
      break;
    case ARG_DEFAULT_SPU_DURATION:
      ke->default_spu_duration = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kate_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKateEnc *ke = GST_KATE_ENC (object);

  switch (prop_id) {
    case ARG_LANGUAGE:
      g_value_set_string (value, ke->language ? ke->language : "");
      break;
    case ARG_CATEGORY:
      g_value_set_string (value, ke->category ? ke->category : "");
      break;
    case ARG_GRANULE_RATE_NUM:
      g_value_set_int (value, ke->granule_rate_numerator);
      break;
    case ARG_GRANULE_RATE_DEN:
      g_value_set_int (value, ke->granule_rate_denominator);
      break;
    case ARG_GRANULE_SHIFT:
      g_value_set_int (value, ke->granule_shift);
      break;
    case ARG_KEEPALIVE_MIN_TIME:
      g_value_set_float (value, ke->keepalive_min_time);
      break;
    case ARG_ORIGINAL_CANVAS_WIDTH:
      g_value_set_int (value, ke->original_canvas_width);
      break;
    case ARG_ORIGINAL_CANVAS_HEIGHT:
      g_value_set_int (value, ke->original_canvas_height);
      break;
    case ARG_DEFAULT_SPU_DURATION:
      g_value_set_float (value, ke->default_spu_duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

static GstBuffer *
gst_kate_enc_create_buffer (GstKateEnc * ke, kate_packet * kp,
    kate_int64_t granpos, GstClockTime timestamp, GstClockTime duration,
    gboolean header)
{
  GstBuffer *buffer;

  g_return_val_if_fail (kp != NULL, NULL);
  g_return_val_if_fail (kp->data != NULL, NULL);

  buffer = gst_buffer_new_allocate (NULL, kp->nbytes, NULL);
  if (G_UNLIKELY (!buffer)) {
    GST_WARNING_OBJECT (ke, "Failed to allocate buffer for %u bytes",
        (guint) kp->nbytes);
    return NULL;
  }

  gst_buffer_fill (buffer, 0, kp->data, kp->nbytes);

  /* same system as other Ogg codecs, as per ext/ogg/README:
     OFFSET_END is the granulepos
     OFFSET is its time representation
   */
  GST_BUFFER_OFFSET_END (buffer) = granpos;
  GST_BUFFER_OFFSET (buffer) = timestamp;
  GST_BUFFER_TIMESTAMP (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = duration;

  return buffer;
}

static GstFlowReturn
gst_kate_enc_push_buffer (GstKateEnc * ke, GstBuffer * buffer)
{
  GstFlowReturn flow;

  ke->last_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer) >
      ke->latest_end_time) {
    ke->latest_end_time =
        GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
  }

  flow = gst_pad_push (ke->srcpad, buffer);
  if (G_UNLIKELY (flow != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (ke->srcpad, "push flow: %s", gst_flow_get_name (flow));
  }

  return flow;
}

static GstFlowReturn
gst_kate_enc_push_and_free_kate_packet (GstKateEnc * ke, kate_packet * kp,
    kate_int64_t granpos, GstClockTime timestamp, GstClockTime duration,
    gboolean header)
{
  GstBuffer *buffer;

  GST_LOG_OBJECT (ke, "Creating buffer, %u bytes", (guint) kp->nbytes);
  buffer =
      gst_kate_enc_create_buffer (ke, kp, granpos, timestamp, duration, header);
  if (G_UNLIKELY (!buffer)) {
    GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
        ("Failed to create buffer, %u bytes", (guint) kp->nbytes));
    kate_packet_clear (kp);
    return GST_FLOW_ERROR;
  }

  kate_packet_clear (kp);

  return gst_kate_enc_push_buffer (ke, buffer);
}

static void
gst_kate_enc_metadata_set1 (const GstTagList * list, const gchar * tag,
    gpointer kateenc)
{
  GstKateEnc *ke = GST_KATE_ENC (kateenc);
  GList *vc_list, *l;

  vc_list = gst_tag_to_vorbis_comments (list, tag);

  for (l = vc_list; l != NULL; l = l->next) {
    const gchar *vc_string = (const gchar *) l->data;
    gchar *key = NULL, *val = NULL;

    GST_LOG_OBJECT (ke, "Kate comment: %s", vc_string);
    if (gst_tag_parse_extended_comment (vc_string, &key, NULL, &val, TRUE)) {
      kate_comment_add_tag (&ke->kc, key, val);
      g_free (key);
      g_free (val);
    }
  }

  g_list_foreach (vc_list, (GFunc) g_free, NULL);
  g_list_free (vc_list);
}

static void
gst_kate_enc_set_metadata (GstKateEnc * ke)
{
  GstTagList *merged_tags;
  const GstTagList *user_tags;

  user_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (ke));

  GST_DEBUG_OBJECT (ke, "upstream tags = %" GST_PTR_FORMAT, ke->tags);
  GST_DEBUG_OBJECT (ke, "user-set tags = %" GST_PTR_FORMAT, user_tags);

  /* gst_tag_list_merge() will handle NULL for either or both lists fine */
  merged_tags = gst_tag_list_merge (user_tags, ke->tags,
      gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (ke)));

  if (merged_tags) {
    GST_DEBUG_OBJECT (ke, "merged   tags = %" GST_PTR_FORMAT, merged_tags);
    gst_tag_list_foreach (merged_tags, gst_kate_enc_metadata_set1, ke);
    gst_tag_list_unref (merged_tags);
  }
}

static gboolean
gst_kate_enc_setcaps (GstKateEnc * ke, GstCaps * caps)
{
  GST_LOG_OBJECT (ke, "input caps: %" GST_PTR_FORMAT, caps);

  /* One day we could try to automatically set the category based on the
   * input format, assuming that the input is subtitles. Currently that
   * doesn't work yet though, because we send the header packets already from
   * the sink event handler when receiving the newsegment event, so before
   * the first buffer (might be tricky to change too, given that there could
   * be no data at the beginning for a long time). So for now we just try to
   * make sure people didn't set the category to something obviously wrong. */
  if (ke->category != NULL) {
    GstStructure *s = gst_caps_get_structure (caps, 0);

    if (gst_structure_has_name (s, "text/x-raw")) {
      const gchar *format;

      format = gst_structure_get_string (s, "format");
      if (strcmp (format, "utf8") == 0) {
        ke->format = GST_KATE_FORMAT_TEXT_UTF8;
      } else if (strcmp (format, "pango-markup") == 0) {
        ke->format = GST_KATE_FORMAT_TEXT_PANGO_MARKUP;
      }

      if (strcmp (ke->category, "K-SPU") == 0 ||
          strcmp (ke->category, "spu-subtitles") == 0) {
        GST_ELEMENT_WARNING (ke, LIBRARY, SETTINGS, (NULL),
            ("Category set to '%s', but input is text-based.", ke->category));
      }
    } else if (gst_structure_has_name (s, "subpicture/x-dvd")) {
      ke->format = GST_KATE_FORMAT_SPU;
      if (strcmp (ke->category, "SUB") == 0 ||
          strcmp (ke->category, "subtitles") == 0) {
        GST_ELEMENT_WARNING (ke, LIBRARY, SETTINGS, (NULL),
            ("Category set to '%s', but input is subpictures.", ke->category));
      }
    } else {
      GST_ERROR_OBJECT (ke, "unexpected input caps %" GST_PTR_FORMAT, caps);
      return FALSE;
    }
  }

  return TRUE;
}

static gboolean
gst_kate_enc_is_simple_subtitle_category (GstKateEnc * ke, const char *category)
{
  static const char *const simple[] = {
    "subtitles",
    "SUB",
    "spu-subtitles",
    "K-SPU",
  };
  int n;

  if (!category)
    return FALSE;
  for (n = 0; n < G_N_ELEMENTS (simple); ++n) {
    if (!strcmp (category, simple[n]))
      return TRUE;
  }
  return FALSE;
}

static GstFlowReturn
gst_kate_enc_send_headers (GstKateEnc * ke)
{
  GstFlowReturn rflow = GST_FLOW_OK;
  GstCaps *caps;
  GList *headers = NULL, *item;

  if (G_UNLIKELY (ke->category == NULL || *ke->category == '\0')) {
    /* The error code is a bit of a lie, but seems most appropriate. */
    GST_ELEMENT_ERROR (ke, LIBRARY, SETTINGS, (NULL),
        ("The 'category' property must be set. For subtitles, set it to "
            "either 'SUB' (text subtitles) or 'K-SPU' (dvd-style subtitles)"));
    return GST_FLOW_ERROR;
  }

  gst_kate_enc_set_metadata (ke);

  /* encode headers and store them in a list */
  while (1) {
    kate_packet kp;
    int ret = kate_encode_headers (&ke->k, &ke->kc, &kp);
    if (ret == 0) {
      GstBuffer *buffer;

      buffer = gst_kate_enc_create_buffer (ke, &kp, 0, 0, 0, TRUE);
      if (!buffer) {
        GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
            ("Failed to create buffer, %u bytes", (guint) kp.nbytes));
        rflow = GST_FLOW_ERROR;
        break;
      }
      kate_packet_clear (&kp);

      GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_HEADER);
      headers = g_list_append (headers, buffer);
    } else if (ret > 0) {
      GST_LOG_OBJECT (ke, "Last header encoded");
      break;
    } else {
      GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
          ("Failed encoding headers: %s",
              gst_kate_util_get_error_message (ret)));
      rflow = GST_FLOW_ERROR;
      break;
    }
  }

  if (rflow == GST_FLOW_OK) {
    if (gst_kate_enc_is_simple_subtitle_category (ke, ke->category)) {
      caps = gst_kate_util_set_header_on_caps (&ke->element,
          gst_caps_from_string ("subtitle/x-kate"), headers);
    } else {
      caps = gst_kate_util_set_header_on_caps (&ke->element,
          gst_caps_from_string ("application/x-kate"), headers);
    }
    if (caps) {
      GST_DEBUG_OBJECT (ke, "here are the caps: %" GST_PTR_FORMAT, caps);
      gst_pad_set_caps (ke->srcpad, caps);
      gst_caps_unref (caps);

      if (ke->pending_segment)
        gst_pad_push_event (ke->srcpad, ke->pending_segment);
      ke->pending_segment = NULL;

      GST_LOG_OBJECT (ke, "pushing headers");
      item = headers;
      while (item) {
        GstBuffer *buffer = item->data;
        GST_LOG_OBJECT (ke, "pushing header %p", buffer);
        gst_kate_enc_push_buffer (ke, buffer);
        item = item->next;
      }
    } else {
      GST_ERROR_OBJECT (ke, "Failed to set headers on caps");
    }
  }

  g_list_free (headers);

  return rflow;
}

static GstFlowReturn
gst_kate_enc_flush_headers (GstKateEnc * ke)
{
  GstFlowReturn rflow = GST_FLOW_OK;
  if (!ke->headers_sent) {
    GST_INFO_OBJECT (ke, "headers not yet sent, flushing");
    rflow = gst_kate_enc_send_headers (ke);
    if (rflow == GST_FLOW_OK) {
      ke->headers_sent = TRUE;
      GST_INFO_OBJECT (ke, "headers flushed");
    } else {
      GST_WARNING_OBJECT (ke, "Failed to flush headers: %s",
          gst_flow_get_name (rflow));
    }
  }
  return rflow;
}

static GstFlowReturn
gst_kate_enc_chain_push_packet (GstKateEnc * ke, kate_packet * kp,
    GstClockTime start, GstClockTime duration)
{
  kate_int64_t granpos;
  GstFlowReturn rflow;

  granpos = kate_encode_get_granule (&ke->k);
  if (G_UNLIKELY (granpos < 0)) {
    GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
        ("Negative granpos for packet"));
    kate_packet_clear (kp);
    return GST_FLOW_ERROR;
  }
  rflow =
      gst_kate_enc_push_and_free_kate_packet (ke, kp, granpos, start, duration,
      FALSE);
  if (G_UNLIKELY (rflow != GST_FLOW_OK)) {
    GST_WARNING_OBJECT (ke, "Failed to push Kate packet");
  }
  return rflow;
}

static void
gst_kate_enc_generate_keepalive (GstKateEnc * ke, GstClockTime timestamp)
{
  kate_packet kp;
  int ret;
  kate_float t = timestamp / (double) GST_SECOND;
  GST_DEBUG_OBJECT (ke, "keepalive at %f", t);
  ret = kate_encode_keepalive (&ke->k, t, &kp);
  if (ret < 0) {
    GST_WARNING_OBJECT (ke, "Failed to encode keepalive packet: %s",
        gst_kate_util_get_error_message (ret));
  } else {
    kate_int64_t granpos = kate_encode_get_granule (&ke->k);
    GST_LOG_OBJECT (ke, "Keepalive packet encoded");
    if (gst_kate_enc_push_and_free_kate_packet (ke, &kp, granpos, timestamp, 0,
            FALSE)) {
      GST_WARNING_OBJECT (ke, "Failed to push keepalive packet");
    }
  }
}

static GstFlowReturn
gst_kate_enc_flush_waiting (GstKateEnc * ke, GstClockTime now)
{
  GstFlowReturn rflow = GST_FLOW_OK;
  if (ke->delayed_spu) {
    int ret;
    kate_packet kp;
    GstClockTime keepalive_time;

    kate_float t0 = ke->delayed_start / (double) GST_SECOND;
    kate_float t1 = now / (double) GST_SECOND;

    GST_INFO_OBJECT (ke,
        "We had a delayed SPU packet starting at %f, flushing at %f (assumed duration %f)",
        t0, t1, t1 - t0);

    ret = kate_encode_text (&ke->k, t0, t1, "", 0, &kp);
    if (G_UNLIKELY (ret < 0)) {
      GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
          ("Failed to encode text packet: %s",
              gst_kate_util_get_error_message (ret)));
      rflow = GST_FLOW_ERROR;
    } else {
      rflow =
          gst_kate_enc_chain_push_packet (ke, &kp, ke->delayed_start,
          now - ke->delayed_start + 1);
    }

    if (rflow == GST_FLOW_OK) {
      GST_DEBUG_OBJECT (ke, "delayed SPU packet flushed");
    } else {
      GST_WARNING_OBJECT (ke, "Failed to flush delayed SPU packet: %s",
          gst_flow_get_name (rflow));
    }

    /* forget it even if we couldn't flush it */
    ke->delayed_spu = FALSE;

    /* free the delayed data */
    g_free (ke->delayed_bitmap->pixels);
    g_free (ke->delayed_bitmap);
    ke->delayed_bitmap = NULL;
    g_free (ke->delayed_palette->colors);
    g_free (ke->delayed_palette);
    ke->delayed_palette = NULL;
    g_free (ke->delayed_region);
    ke->delayed_region = NULL;

    /* now that we've flushed the packet, we want to insert keepalives as requested */
    if (ke->keepalive_min_time > 0.0f && t1 > t0) {
      GST_INFO_OBJECT (ke, "generating keepalives at %f from %f to %f",
          ke->keepalive_min_time, t0, t1);
      for (keepalive_time = ke->delayed_start;
          (keepalive_time += ke->keepalive_min_time * GST_SECOND) < now;) {
        GST_INFO_OBJECT (ke, "generating keepalive at %f",
            keepalive_time / (double) GST_SECOND);
        gst_kate_enc_generate_keepalive (ke, keepalive_time);
      }
    }
  }
  return rflow;
}

static GstFlowReturn
gst_kate_enc_chain_spu (GstKateEnc * ke, GstBuffer * buf)
{
  kate_packet kp;
  kate_region *kregion;
  kate_bitmap *kbitmap;
  kate_palette *kpalette;
  GstFlowReturn rflow;
  int ret = 0;

  /* allocate region, bitmap, and palette, in case we have to delay encoding them */
  kregion = (kate_region *) g_malloc (sizeof (kate_region));
  kbitmap = (kate_bitmap *) g_malloc (sizeof (kate_bitmap));
  kpalette = (kate_palette *) g_malloc (sizeof (kate_palette));
  if (!kregion || !kpalette || !kbitmap) {
    if (kregion)
      g_free (kregion);
    if (kbitmap)
      g_free (kbitmap);
    if (kpalette)
      g_free (kpalette);
    GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL), ("Out of memory"));
    return GST_FLOW_ERROR;
  }

  rflow = gst_kate_spu_decode_spu (ke, buf, kregion, kbitmap, kpalette);
  if (G_UNLIKELY (rflow != GST_FLOW_OK)) {
    GST_ERROR_OBJECT (ke, "Failed to decode incoming SPU");
#if 0
    {
      static int spu_count = 0;
      FILE *f;
      char name[32];
      snprintf (name, sizeof (name), "/tmp/bad_spu_%04d", spu_count++);
      name[sizeof (name) - 1] = 0;
      f = fopen (name, "w");
      if (f) {
        fwrite (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), 1, f);
        fclose (f);
      }
    }
#endif
  } else if (G_UNLIKELY (kbitmap->width == 0 || kbitmap->height == 0)) {
    /* there are some DVDs (well, at least one) where some dimwits put in a wholly transparent full screen 720x576 SPU !!!!?! */
    GST_WARNING_OBJECT (ke, "SPU is totally invisible - dimwits");
    rflow = GST_FLOW_OK;
  } else {
    /* timestamp offsets are hidden in the SPU packets */
    GstClockTime start =
        GST_BUFFER_TIMESTAMP (buf) + GST_KATE_STM_TO_GST (ke->show_time);
    GstClockTime stop =
        GST_BUFFER_TIMESTAMP (buf) + GST_KATE_STM_TO_GST (ke->hide_time);
    kate_float t0 = start / (double) GST_SECOND;
    kate_float t1 = stop / (double) GST_SECOND;
    GST_DEBUG_OBJECT (ke, "buf ts %f, start/show %hu/%hu",
        GST_BUFFER_TIMESTAMP (buf) / (double) GST_SECOND, ke->show_time,
        ke->hide_time);

#if 0
    {
      static int spu_count = 0;
      FILE *f;
      char name[32];
      snprintf (name, sizeof (name), "/tmp/spu_%04d", spu_count++);
      name[sizeof (name) - 1] = 0;
      f = fopen (name, "w");
      if (f) {
        fwrite (GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf), 1, f);
        fclose (f);
      }
    }
#endif
    GST_DEBUG_OBJECT (ke, "Encoding %" G_GSIZE_FORMAT "x%" G_GSIZE_FORMAT
        " SPU: (%" G_GSIZE_FORMAT " bytes) from %f to %f",
        kbitmap->width, kbitmap->height, gst_buffer_get_size (buf), t0, t1);

    ret = kate_encode_set_region (&ke->k, kregion);
    if (G_UNLIKELY (ret < 0)) {
      GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
          ("Failed to set region: %s", gst_kate_util_get_error_message (ret)));
      rflow = GST_FLOW_ERROR;
    } else {
      ret = kate_encode_set_palette (&ke->k, kpalette);
      if (G_UNLIKELY (ret < 0)) {
        GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
            ("Failed to set palette: %s",
                gst_kate_util_get_error_message (ret)));
        rflow = GST_FLOW_ERROR;
      } else {
        ret = kate_encode_set_bitmap (&ke->k, kbitmap);
        if (G_UNLIKELY (ret < 0)) {
          GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
              ("Failed to set bitmap: %s",
                  gst_kate_util_get_error_message (ret)));
          rflow = GST_FLOW_ERROR;
        } else {
          /* Some SPUs have no hide time - so I'm going to delay the encoding of the packet
             till either a suitable event happens, and the time of this event will be used
             as the end time of this SPU, which will then be encoded and sent off. Suitable
             events are the arrival of a subsequent SPU (eg, this SPU will replace the one
             with no end), EOS, a new segment event, or a time threshold being reached */
          if (ke->hide_time <= ke->show_time) {
            GST_INFO_OBJECT (ke,
                "Cannot encode SPU packet now, hide time is now known (starting at %f) - delaying",
                t0);
            ke->delayed_spu = TRUE;
            ke->delayed_start = start;
            ke->delayed_bitmap = kbitmap;
            ke->delayed_palette = kpalette;
            ke->delayed_region = kregion;
            rflow = GST_FLOW_OK;
          } else {
            ret = kate_encode_text (&ke->k, t0, t1, "", 0, &kp);
            if (G_UNLIKELY (ret < 0)) {
              GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
                  ("Failed to encode empty text for SPU buffer: %s",
                      gst_kate_util_get_error_message (ret)));
              rflow = GST_FLOW_ERROR;
            } else {
              rflow =
                  gst_kate_enc_chain_push_packet (ke, &kp, start,
                  stop - start + 1);
            }
          }
        }
      }
    }

    if (!ke->delayed_spu) {
      g_free (kpalette->colors);
      g_free (kpalette);
      g_free (kbitmap->pixels);
      g_free (kbitmap);
      g_free (kregion);
    }
  }

  return rflow;
}

static GstFlowReturn
gst_kate_enc_chain_text (GstKateEnc * ke, GstBuffer * buf)
{
  kate_packet kp = { 0 };
  int ret = 0;
  GstFlowReturn rflow;
  GstClockTime start = GST_BUFFER_TIMESTAMP (buf);
  GstClockTime stop = GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);

  if (ke->format == GST_KATE_FORMAT_TEXT_PANGO_MARKUP) {
    ret = kate_encode_set_markup_type (&ke->k, kate_markup_simple);
  } else if (ke->format == GST_KATE_FORMAT_TEXT_UTF8) {
    ret = kate_encode_set_markup_type (&ke->k, kate_markup_none);
  } else {
    return GST_FLOW_ERROR;
  }

  if (G_UNLIKELY (ret < 0)) {
    GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
        ("Failed to set markup type: %s",
            gst_kate_util_get_error_message (ret)));
    rflow = GST_FLOW_ERROR;
  } else {
    GstMapInfo info;
    gboolean need_unmap = TRUE;
    kate_float t0 = start / (double) GST_SECOND;
    kate_float t1 = stop / (double) GST_SECOND;

    if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
      info.data = NULL;
      info.size = 0;
      need_unmap = FALSE;
      GST_WARNING_OBJECT (buf, "Failed to map buffer");
    }

    GST_LOG_OBJECT (ke, "Encoding text: %*.*s (%u bytes) from %f to %f",
        (int) info.size, (int) info.size, info.data, (int) info.size, t0, t1);
    ret = kate_encode_text (&ke->k, t0, t1, (const char *) info.data, info.size,
        &kp);
    if (G_UNLIKELY (ret < 0)) {
      GST_ELEMENT_ERROR (ke, STREAM, ENCODE, (NULL),
          ("Failed to encode text: %s", gst_kate_util_get_error_message (ret)));
      rflow = GST_FLOW_ERROR;
    } else {
      rflow = gst_kate_enc_chain_push_packet (ke, &kp, start, stop - start + 1);
    }
    if (need_unmap)
      gst_buffer_unmap (buf, &info);
  }

  return rflow;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_kate_enc_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstKateEnc *ke = GST_KATE_ENC (parent);
  GstFlowReturn rflow;

  GST_DEBUG_OBJECT (ke, "got packet, %" G_GSIZE_FORMAT " bytes",
      gst_buffer_get_size (buf));

  /* first push headers if we haven't done that yet */
  rflow = gst_kate_enc_flush_headers (ke);

  if (G_LIKELY (rflow == GST_FLOW_OK)) {
    /* flush any packet we had waiting */
    rflow = gst_kate_enc_flush_waiting (ke, GST_BUFFER_TIMESTAMP (buf));

    if (G_LIKELY (rflow == GST_FLOW_OK)) {
      if (ke->format == GST_KATE_FORMAT_SPU) {
        /* encode a kate_bitmap */
        rflow = gst_kate_enc_chain_spu (ke, buf);
      } else {
        /* encode text */
        rflow = gst_kate_enc_chain_text (ke, buf);
      }
    }
  }

  gst_buffer_unref (buf);

  return rflow;
}

static GstStateChangeReturn
gst_kate_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstKateEnc *ke = GST_KATE_ENC (element);
  GstStateChangeReturn res;
  int ret;

  GST_INFO_OBJECT (ke, "gst_kate_enc_change_state");

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ke->tags = gst_tag_list_new_empty ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (ke, "READY -> PAUSED, initializing kate state");
      ret = kate_info_init (&ke->ki);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke, "failed to initialize kate info structure: %s",
            gst_kate_util_get_error_message (ret));
        break;
      }
      if (ke->language) {
        ret = kate_info_set_language (&ke->ki, ke->language);
        if (ret < 0) {
          GST_WARNING_OBJECT (ke, "failed to set stream language: %s",
              gst_kate_util_get_error_message (ret));
          break;
        }
      }
      if (ke->category) {
        ret = kate_info_set_category (&ke->ki, ke->category);
        if (ret < 0) {
          GST_WARNING_OBJECT (ke, "failed to set stream category: %s",
              gst_kate_util_get_error_message (ret));
          break;
        }
      }
      ret =
          kate_info_set_original_canvas_size (&ke->ki,
          ke->original_canvas_width, ke->original_canvas_height);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke, "failed to set original canvas size: %s",
            gst_kate_util_get_error_message (ret));
        break;
      }
      ret = kate_comment_init (&ke->kc);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke,
            "failed to initialize kate comment structure: %s",
            gst_kate_util_get_error_message (ret));
        break;
      }
      ret = kate_encode_init (&ke->k, &ke->ki);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke, "failed to initialize kate state: %s",
            gst_kate_util_get_error_message (ret));
        break;
      }
      ke->headers_sent = FALSE;
      ke->initialized = TRUE;
      ke->last_timestamp = 0;
      ke->latest_end_time = 0;
      ke->format = GST_KATE_FORMAT_UNDEFINED;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tag_list_unref (ke->tags);
      ke->tags = NULL;
      break;
    default:
      break;
  }

  res = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (res == GST_STATE_CHANGE_FAILURE) {
    GST_WARNING_OBJECT (ke, "Parent failed to change state");
    return res;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (ke, "PAUSED -> READY, clearing kate state");
      if (ke->initialized) {
        kate_clear (&ke->k);
        kate_info_clear (&ke->ki);
        kate_comment_clear (&ke->kc);
        ke->initialized = FALSE;
        ke->last_timestamp = 0;
        ke->latest_end_time = 0;
      }
      gst_event_replace (&ke->pending_segment, NULL);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  GST_DEBUG_OBJECT (ke, "State change done");

  return res;
}

static GstClockTime
gst_kate_enc_granule_time (kate_state * k, gint64 granulepos)
{
  float t;

  if (granulepos == -1)
    return -1;

  t = kate_granule_time (k->ki, granulepos);
  return t * GST_SECOND;
}

/*
conversions on the sink:
  - nothing
conversions on the source:
  - default is granules at num/den rate
  - default -> time is possible
  - bytes do not mean anything, packets can be any number of bytes, and we
    have no way to know the number of bytes emitted without decoding
*/

static gboolean
gst_kate_enc_convert (GstPad * pad, GstFormat src_fmt, gint64 src_val,
    GstFormat * dest_fmt, gint64 * dest_val)
{
  GstKateEnc *ke;
  gboolean res = FALSE;

  if (src_fmt == *dest_fmt) {
    *dest_val = src_val;
    return TRUE;
  }

  ke = GST_KATE_ENC (gst_pad_get_parent (pad));

  if (!ke->initialized) {
    GST_WARNING_OBJECT (ke, "not initialized yet");
    gst_object_unref (ke);
    return FALSE;
  }

  if (src_fmt == GST_FORMAT_BYTES || *dest_fmt == GST_FORMAT_BYTES) {
    GST_WARNING_OBJECT (ke, "unsupported format");
    gst_object_unref (ke);
    return FALSE;
  }

  switch (src_fmt) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_fmt) {
        case GST_FORMAT_TIME:
          *dest_val = gst_kate_enc_granule_time (&ke->k, src_val);
          res = TRUE;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  if (!res) {
    GST_WARNING_OBJECT (ke, "unsupported format");
  }

  gst_object_unref (ke);
  return res;
}

static gboolean
gst_kate_enc_source_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;

  GST_DEBUG ("source query %d", GST_QUERY_TYPE (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!gst_kate_enc_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val)) {
        return gst_pad_query_default (pad, parent, query);
      }
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      res = TRUE;
    }
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static gboolean
gst_kate_enc_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstKateEnc *ke = GST_KATE_ENC (parent);
  const GstStructure *structure;
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_kate_enc_setcaps (ke, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:{
      GstSegment seg;

      GST_LOG_OBJECT (ke, "Got newsegment event");

      gst_event_copy_segment (event, &seg);

      if (!ke->headers_sent) {
        if (ke->pending_segment)
          gst_event_unref (ke->pending_segment);
        ke->pending_segment = event;
        event = NULL;
      }

      if (ke->initialized) {
        GST_LOG_OBJECT (ke, "ensuring all headers are in");
        if (gst_kate_enc_flush_headers (ke) != GST_FLOW_OK) {
          GST_WARNING_OBJECT (ke, "Failed to flush headers");
        } else {
          if (seg.format != GST_FORMAT_TIME
              || !GST_CLOCK_TIME_IS_VALID (seg.start)) {
            GST_WARNING_OBJECT (ke,
                "No time in newsegment event %p, format %d, timestamp %"
                G_GINT64_FORMAT, event, (int) seg.format, seg.start);
            /* to be safe, we'd need to generate a keepalive anyway, but we'd have to guess at the timestamp to use; a
               good guess would be the last known timestamp plus the keepalive time, but if we then get a packet with a
               timestamp less than this, it would fail to encode, which would be Bad. If we don't encode a keepalive, we
               run the risk of stalling the pipeline and hanging, which is Very Bad. Oh dear. We can't exit(-1), can we ? */
          } else {
            float t = seg.start / (double) GST_SECOND;

            if (ke->delayed_spu
                && t - ke->delayed_start / (double) GST_SECOND >=
                ke->default_spu_duration) {
              if (G_UNLIKELY (gst_kate_enc_flush_waiting (ke,
                          seg.start) != GST_FLOW_OK)) {
                GST_WARNING_OBJECT (ke, "Failed to encode delayed packet");
                /* continue with new segment handling anyway */
              }
            }

            GST_LOG_OBJECT (ke, "ts %f, last %f (min %f)", t,
                ke->last_timestamp / (double) GST_SECOND,
                ke->keepalive_min_time);
            if (ke->keepalive_min_time > 0.0f
                && t - ke->last_timestamp / (double) GST_SECOND >=
                ke->keepalive_min_time) {
              /* we only generate a keepalive if there is no SPU waiting, as it would
                 mean out of sequence start times - and granulepos */
              if (!ke->delayed_spu) {
                gst_kate_enc_generate_keepalive (ke, seg.start);
              }
            }
          }
        }
      }
      if (event)
        ret = gst_pad_push_event (ke->srcpad, event);
      else
        ret = TRUE;
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      GST_LOG_OBJECT (ke, "Got custom downstream event");
      /* adapted from the dvdsubdec element */
      structure = gst_event_get_structure (event);
      if (structure != NULL
          && gst_structure_has_name (structure, "application/x-gst-dvd")) {
        if (ke->initialized) {
          GST_LOG_OBJECT (ke, "ensuring all headers are in");
          if (gst_kate_enc_flush_headers (ke) != GST_FLOW_OK) {
            GST_WARNING_OBJECT (ke, "Failed to flush headers");
          } else {
            const gchar *event_name =
                gst_structure_get_string (structure, "event");
            if (event_name) {
              if (!strcmp (event_name, "dvd-spu-clut-change")) {
                gchar name[16];
                int idx;
                gboolean found;
                gint value;
                GST_INFO_OBJECT (ke, "New CLUT received");
                for (idx = 0; idx < 16; ++idx) {
                  g_snprintf (name, sizeof (name), "clut%02d", idx);
                  found = gst_structure_get_int (structure, name, &value);
                  if (found) {
                    ke->spu_clut[idx] = value;
                  } else {
                    GST_WARNING_OBJECT (ke,
                        "DVD CLUT event did not contain %s field", name);
                  }
                }
              } else if (!strcmp (event_name, "dvd-lang-codes")) {
                /* we can't know which stream corresponds to us */
              }
            } else {
              GST_WARNING_OBJECT (ke, "custom downstream event with no name");
            }
          }
        }
      }
      ret = gst_pad_push_event (ke->srcpad, event);
      break;

    case GST_EVENT_TAG:
      GST_LOG_OBJECT (ke, "Got tag event");
      if (ke->tags) {
        GstTagList *list;

        gst_event_parse_tag (event, &list);
        gst_tag_list_insert (ke->tags, list,
            gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (ke)));
      } else {
        g_assert_not_reached ();
      }
      ret = gst_pad_event_default (pad, parent, event);
      break;

    case GST_EVENT_EOS:
      GST_INFO_OBJECT (ke, "Got EOS event");
      if (ke->initialized) {
        GST_LOG_OBJECT (ke, "ensuring all headers are in");
        if (gst_kate_enc_flush_headers (ke) != GST_FLOW_OK) {
          GST_WARNING_OBJECT (ke, "Failed to flush headers");
        } else {
          kate_packet kp;
          int ret;
          GstClockTime delayed_end =
              ke->delayed_start + ke->default_spu_duration * GST_SECOND;

          if (G_UNLIKELY (gst_kate_enc_flush_waiting (ke,
                      delayed_end) != GST_FLOW_OK)) {
            GST_WARNING_OBJECT (ke, "Failed to encode delayed packet");
            /* continue with EOS handling anyway */
          }

          ret = kate_encode_finish (&ke->k, -1, &kp);
          if (ret < 0) {
            GST_WARNING_OBJECT (ke, "Failed to encode EOS packet: %s",
                gst_kate_util_get_error_message (ret));
          } else {
            kate_int64_t granpos = kate_encode_get_granule (&ke->k);
            GST_LOG_OBJECT (ke, "EOS packet encoded");
            if (gst_kate_enc_push_and_free_kate_packet (ke, &kp, granpos,
                    ke->latest_end_time, 0, FALSE)) {
              GST_WARNING_OBJECT (ke, "Failed to push EOS packet");
            }
          }
        }
      }
      ret = gst_pad_event_default (pad, parent, event);
      break;

    default:
      GST_LOG_OBJECT (ke, "Got unhandled event");
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }

  return ret;
}
