/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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
 * SECTION:element-katedec
 * @title: katedec
 * @see_also: oggdemux
 *
 * This element decodes Kate streams
 * <ulink url="http://libkate.googlecode.com/">Kate</ulink> is a free codec
 * for text based data, such as subtitles. Any number of kate streams can be
 * embedded in an Ogg stream.
 *
 * libkate (see above url) is needed to build this plugin.
 *
 * ## Example pipeline
 *
 * This explicitely decodes a Kate stream:
 * |[
 * gst-launch-1.0 filesrc location=test.ogg ! oggdemux ! katedec ! fakesink silent=TRUE
 * ]|
 *
 * This will automatically detect and use any Kate streams multiplexed
 * in an Ogg stream:
 * |[
 * gst-launch-1.0 playbin uri=file:///tmp/test.ogg
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>

#include "gstkate.h"
#include "gstkatespu.h"
#include "gstkatedec.h"

GST_DEBUG_CATEGORY_EXTERN (gst_katedec_debug);
#define GST_CAT_DEFAULT gst_katedec_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_REMOVE_MARKUP = DECODER_BASE_ARG_COUNT
};

/* We don't accept application/x-kate here on purpose for now, since we're
 * only really interested in subtitle-like things for playback purposes, not
 * cracktastic complex overlays or presentation images etc. - those should be
 * fed into a tiger overlay plugin directly */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subtitle/x-kate")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-raw, format = { pango-markup, utf8 }; "
        GST_KATE_SPU_MIME_TYPE)
    );

#define gst_kate_dec_parent_class parent_class
G_DEFINE_TYPE (GstKateDec, gst_kate_dec, GST_TYPE_ELEMENT);

static void gst_kate_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_kate_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_kate_dec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static GstStateChangeReturn gst_kate_dec_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_kate_dec_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_kate_dec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_kate_dec_sink_handle_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_kate_dec_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

/* initialize the plugin's class */
static void
gst_kate_dec_class_init (GstKateDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_kate_dec_set_property;
  gobject_class->get_property = gst_kate_dec_get_property;

  gst_kate_util_install_decoder_base_properties (gobject_class);

  g_object_class_install_property (gobject_class, ARG_REMOVE_MARKUP,
      g_param_spec_boolean ("remove-markup", "Remove markup",
          "Remove markup from decoded text ?", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_kate_dec_change_state);

  gst_element_class_add_static_pad_template (gstelement_class, &src_factory);
  gst_element_class_add_static_pad_template (gstelement_class, &sink_factory);

  gst_element_class_set_static_metadata (gstelement_class,
      "Kate stream text decoder", "Codec/Decoder/Subtitle",
      "Decodes Kate text streams",
      "Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>");
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_kate_dec_init (GstKateDec * dec)
{
  GST_DEBUG_OBJECT (dec, "gst_kate_dec_init");

  dec->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_dec_chain));
  gst_pad_set_query_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_dec_sink_query));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_dec_sink_event));
  gst_pad_use_fixed_caps (dec->sinkpad);
  gst_pad_set_caps (dec->sinkpad,
      gst_static_pad_template_get_caps (&sink_factory));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (gst_kate_dec_src_query));
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  gst_kate_util_decode_base_init (&dec->decoder, TRUE);

  dec->src_caps = NULL;
  dec->output_format = GST_KATE_FORMAT_UNDEFINED;
  dec->remove_markup = FALSE;
}

static void
gst_kate_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_kate_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstKateDec *kd = GST_KATE_DEC (object);

  switch (prop_id) {
    case ARG_REMOVE_MARKUP:
      g_value_set_boolean (value, kd->remove_markup);
      break;
    default:
      if (!gst_kate_util_decoder_base_get_property (&kd->decoder, object,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static GstFlowReturn
gst_kate_dec_handle_kate_event (GstKateDec * kd, const kate_event * ev)
{
  GstFlowReturn rflow = GST_FLOW_OK;
  GstKateFormat format = GST_KATE_FORMAT_UNDEFINED;
  gchar *escaped;
  GstBuffer *buffer;
  size_t len;
  gboolean plain = TRUE;

  if (kd->remove_markup && ev->text_markup_type != kate_markup_none) {
    size_t len0 = ev->len + 1;
    escaped = g_strdup (ev->text);
    if (escaped) {
      kate_text_remove_markup (ev->text_encoding, escaped, &len0);
    }
    plain = TRUE;
  } else if (ev->text_markup_type == kate_markup_none) {
    /* no pango markup yet, escape text */
    /* TODO: actually do the pango thing */
    escaped = g_strdup (ev->text);
    plain = TRUE;
  } else {
    escaped = g_strdup (ev->text);
    plain = FALSE;
  }

  if (G_LIKELY (escaped)) {
    len = strlen (escaped);
    if (len > 0) {
      GST_DEBUG_OBJECT (kd, "kate event: %s, escaped %s", ev->text, escaped);
      buffer = gst_buffer_new_and_alloc (len + 1);
      if (G_LIKELY (buffer)) {
        GstCaps *caps;
        if (plain)
          format = GST_KATE_FORMAT_TEXT_UTF8;
        else
          format = GST_KATE_FORMAT_TEXT_PANGO_MARKUP;
        if (format != kd->output_format) {
          caps = gst_caps_new_simple ("text/x-raw", "format", G_TYPE_STRING,
              (format == GST_KATE_FORMAT_TEXT_UTF8) ? "utf8" : "pango-markup",
              NULL);
          gst_pad_push_event (kd->srcpad, gst_event_new_caps (caps));
          gst_caps_unref (caps);
          kd->output_format = format;
        }
        /* allocate and copy the NULs, but don't include them in passed size */
        gst_buffer_fill (buffer, 0, escaped, len + 1);
        gst_buffer_resize (buffer, 0, len);
        GST_BUFFER_TIMESTAMP (buffer) = ev->start_time * GST_SECOND;
        GST_BUFFER_DURATION (buffer) =
            (ev->end_time - ev->start_time) * GST_SECOND;
        rflow = gst_pad_push (kd->srcpad, buffer);
        if (rflow == GST_FLOW_NOT_LINKED) {
          GST_DEBUG_OBJECT (kd, "source pad not linked, ignored");
        } else if (rflow != GST_FLOW_OK) {
          GST_WARNING_OBJECT (kd, "failed to push buffer: %s",
              gst_flow_get_name (rflow));
        }
      } else {
        GST_ELEMENT_ERROR (kd, STREAM, DECODE, (NULL),
            ("Failed to create buffer"));
        rflow = GST_FLOW_ERROR;
      }
    } else {
      GST_WARNING_OBJECT (kd, "Empty string, nothing to do");
      rflow = GST_FLOW_OK;
    }
    g_free (escaped);
  } else {
    GST_ELEMENT_ERROR (kd, STREAM, DECODE, (NULL),
        ("Failed to allocate string"));
    rflow = GST_FLOW_ERROR;
  }

  /* if there's a background paletted bitmap, construct a DVD SPU for it */
  if (ev->bitmap && ev->palette) {
    GstBuffer *buffer = gst_kate_spu_encode_spu (kd, ev);
    if (buffer) {
      GstCaps *caps;

      GST_BUFFER_TIMESTAMP (buffer) = ev->start_time * GST_SECOND;
      GST_BUFFER_DURATION (buffer) =
          (ev->end_time - ev->start_time) * GST_SECOND;

      if (kd->output_format != GST_KATE_FORMAT_SPU) {
        caps = gst_caps_new_empty_simple (GST_KATE_SPU_MIME_TYPE);
        gst_pad_push_event (kd->srcpad, gst_event_new_caps (caps));
        gst_caps_unref (caps);
        kd->output_format = GST_KATE_FORMAT_SPU;
      }

      rflow = gst_pad_push (kd->srcpad, buffer);
      if (rflow == GST_FLOW_NOT_LINKED) {
        GST_DEBUG_OBJECT (kd, "source pad not linked, ignored");
      } else if (rflow != GST_FLOW_OK) {
        GST_WARNING_OBJECT (kd, "failed to push buffer: %s",
            gst_flow_get_name (rflow));
      }
    } else {
      GST_ELEMENT_ERROR (kd, STREAM, DECODE, (NULL),
          ("failed to create SPU from paletted bitmap"));
      rflow = GST_FLOW_ERROR;
    }
  }
  return rflow;
}

/* GstElement vmethod implementations */

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_kate_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstKateDec *kd = GST_KATE_DEC (parent);
  const kate_event *ev = NULL;
  GstFlowReturn rflow = GST_FLOW_OK;

  if (!gst_kate_util_decoder_base_update_segment (&kd->decoder,
          GST_ELEMENT_CAST (kd), buf)) {
    GST_WARNING_OBJECT (kd, "Out of segment!");
    goto not_in_seg;
  }

  rflow =
      gst_kate_util_decoder_base_chain_kate_packet (&kd->decoder,
      GST_ELEMENT_CAST (kd), pad, buf, kd->srcpad, kd->srcpad, &kd->src_caps,
      &ev);
  if (G_UNLIKELY (rflow != GST_FLOW_OK)) {
    gst_buffer_unref (buf);
    return rflow;
  }

  if (ev) {
    rflow = gst_kate_dec_handle_kate_event (kd, ev);
  }

not_in_seg:
  gst_buffer_unref (buf);
  return rflow;
}

static GstStateChangeReturn
gst_kate_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstKateDec *kd = GST_KATE_DEC (element);

  ret = gst_kate_decoder_base_change_state (&kd->decoder, element,
      parent_class, transition);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    gst_caps_replace (&kd->src_caps, NULL);
  }

  return ret;
}

gboolean
gst_kate_dec_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstKateDec *kd = GST_KATE_DEC (parent);
  gboolean res =
      gst_kate_decoder_base_sink_query (&kd->decoder, GST_ELEMENT_CAST (kd),
      pad, parent, query);
  return res;
}

static gboolean
gst_kate_dec_set_caps (GstKateDec * kd, GstCaps * caps)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  GstFlowReturn rflow = GST_FLOW_OK;

  if (gst_structure_has_field (structure, "streamheader")) {
    const GValue *value;
    GstBuffer *buf;
    const kate_event *ev;

    value = gst_structure_get_value (structure, "streamheader");

    if (GST_VALUE_HOLDS_BUFFER (value)) {
      buf = gst_value_get_buffer (value);

      gst_kate_util_decoder_base_chain_kate_packet (&kd->decoder,
          GST_ELEMENT_CAST (kd), kd->sinkpad, buf, kd->srcpad, kd->srcpad,
          &kd->src_caps, &ev);

      if (ev) {
        rflow = gst_kate_dec_handle_kate_event (kd, ev);
      }
    } else if (GST_VALUE_HOLDS_ARRAY (value)) {
      gint i, size = gst_value_array_get_size (value);

      for (i = 0; i < size; i++) {
        const GValue *v = gst_value_array_get_value (value, i);

        buf = gst_value_get_buffer (v);
        gst_kate_util_decoder_base_chain_kate_packet (&kd->decoder,
            GST_ELEMENT_CAST (kd), kd->sinkpad, buf, kd->srcpad, kd->srcpad,
            &kd->src_caps, &ev);

        if (ev) {
          rflow = gst_kate_dec_handle_kate_event (kd, ev);
          if (rflow != GST_FLOW_OK && rflow != GST_FLOW_NOT_LINKED)
            break;
        }
      }
    } else {
      GST_WARNING_OBJECT (kd, "Unhandled streamheader type: %s",
          G_VALUE_TYPE_NAME (value));
    }
  }

  return rflow == GST_FLOW_OK || rflow == GST_FLOW_NOT_LINKED;
}

static gboolean
gst_kate_dec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstKateDec *kd = GST_KATE_DEC (parent);
  gboolean res = TRUE;

  GST_LOG_OBJECT (pad, "Event on sink pad: %" GST_PTR_FORMAT, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      gst_kate_dec_set_caps (kd, caps);
      break;
    }
    default:
      break;
  }

  /* Delay events till we've set caps */
  if (gst_kate_util_decoder_base_queue_event (&kd->decoder, event,
          &gst_kate_dec_sink_handle_event, parent, pad)) {
    return TRUE;
  }

  res = gst_kate_dec_sink_handle_event (pad, parent, event);

  return res;
}

static gboolean
gst_kate_dec_sink_handle_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstKateDec *kd = GST_KATE_DEC (parent);

  GST_LOG_OBJECT (pad, "Handling event on sink pad: %s",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
      break;

    case GST_EVENT_FLUSH_START:
      gst_kate_util_decoder_base_set_flushing (&kd->decoder, TRUE);
      break;

    case GST_EVENT_FLUSH_STOP:
      gst_kate_util_decoder_base_set_flushing (&kd->decoder, FALSE);
      break;

    case GST_EVENT_TAG:{
      GstTagList *tags;
      gst_event_parse_tag (event, &tags);
      gst_kate_util_decoder_base_add_tags (&kd->decoder, tags, FALSE);
      gst_event_unref (event);
      event = gst_kate_util_decoder_base_get_tag_event (&kd->decoder);
      break;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

static gboolean
gst_kate_dec_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstKateDec *kd = GST_KATE_DEC (parent);
  gboolean res = TRUE;

  GST_LOG_OBJECT (pad, "Handling query on src pad: %s",
      GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *caps;

      if (kd->src_caps) {
        GST_DEBUG_OBJECT (kd, "We have src caps %" GST_PTR_FORMAT,
            kd->src_caps);
        caps = gst_caps_copy (kd->src_caps);
      } else {
        GST_DEBUG_OBJECT (kd, "We have no src caps, using template caps");
        caps = gst_static_pad_template_get_caps (&src_factory);
      }

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}
