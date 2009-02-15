/*
 * GStreamer
 * Copyright 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
 * Copyright 2008 Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
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
 * gst-launch dvdreadsrc ! dvddemux ! dvdsubparse ! kateenc ! oggmux ! filesink location=test.ogg
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/gst.h>
#include <gst/gsttagsetter.h>
#include <gst/tag/tag.h>

#include "gstkate.h"
#include "gstkateutil.h"
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

/* taken off the dvdsubdec element */
static const guint32 gst_kate_enc_default_clut[16] = {
  0xb48080, 0x248080, 0x628080, 0xd78080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080
};

#define GST_KATE_UINT16_BE(ptr) ( ( ((guint16)((ptr)[0])) <<8) | ((ptr)[1]) )

/* taken off the DVD SPU decoder - now is time for today's WTF ???? */
#define GST_KATE_STM_TO_GST(stm) ((GST_MSECOND * 1024 * (stm)) / 90)

#define DEFAULT_KEEPALIVE_MIN_TIME 2.5f
#define DEFAULT_DEFAULT_SPU_DURATION 1.5f

#define GST_KATE_SPU_MIME_TYPE "video/x-dvd-subpicture"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/plain; text/x-pango-markup; " GST_KATE_SPU_MIME_TYPE)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_KATE_MIME_TYPE)
    );

static void gst_kate_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_kate_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_kate_enc_dispose (GObject * object);

static GstFlowReturn gst_kate_enc_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn gst_kate_enc_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_kate_enc_sink_event (GstPad * pad, GstEvent * event);
static const GstQueryType *gst_kate_enc_source_query_type (GstPad * pad);
static gboolean gst_kate_enc_source_query (GstPad * pad, GstQuery * query);
static void gst_kate_enc_add_interfaces (GType kateenc_type);

GST_BOILERPLATE_FULL (GstKateEnc, gst_kate_enc, GstElement,
    GST_TYPE_ELEMENT, gst_kate_enc_add_interfaces);

static void
gst_kate_enc_base_init (gpointer gclass)
{
  static const GstElementDetails element_details =
      GST_ELEMENT_DETAILS ("Kate stream encoder",
      "Codec/Encoder/Subtitle",
      "Encodes Kate streams from text or subpictures",
      "Vincent Penquerc'h <ogg.k.ogg.k@googlemail.com>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the plugin's class */
static void
gst_kate_enc_class_init (GstKateEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_kate_enc_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_kate_enc_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_kate_enc_dispose);

  g_object_class_install_property (gobject_class, ARG_LANGUAGE,
      g_param_spec_string ("language", "Language",
          "Set the language of the stream", "", G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_CATEGORY,
      g_param_spec_string ("category", "Category",
          "Set the category of the stream", "", G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_GRANULE_RATE_NUM,
      g_param_spec_int ("granule-rate-numerator", "Granule rate numerator",
          "Set the numerator of the granule rate",
          1, G_MAXINT, 1, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_GRANULE_RATE_DEN,
      g_param_spec_int ("granule-rate-denominator", "Granule rate denominator",
          "Set the denominator of the granule rate",
          1, G_MAXINT, 1000, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_GRANULE_SHIFT,
      g_param_spec_int ("granule-shift", "Granule shift",
          "Set the granule shift", 0, 64, 32, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_ORIGINAL_CANVAS_WIDTH,
      g_param_spec_int ("original-canvas-width", "Original canvas width",
          "Set the width of the canvas this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_ORIGINAL_CANVAS_HEIGHT,
      g_param_spec_int ("original-canvas-height", "Original canvas height",
          "Set the height of the canvas this stream was authored for (0 is unspecified)",
          0, G_MAXINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_KEEPALIVE_MIN_TIME,
      g_param_spec_float ("keepalive-min-time", "Keepalive mimimum time",
          "Set minimum time to emit keepalive packets (0 disables keepalive packets)",
          0.0f, FLT_MAX, DEFAULT_KEEPALIVE_MIN_TIME, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_DEFAULT_SPU_DURATION,
      g_param_spec_float ("default-spu-duration", "Default SPU duration",
          "Set the assumed max duration (in seconds) of SPUs with no duration specified",
          0.0f, FLT_MAX, DEFAULT_DEFAULT_SPU_DURATION, G_PARAM_READWRITE));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_kate_enc_change_state);
}

static void
gst_kate_enc_add_interfaces (GType kateenc_type)
{
  static const GInterfaceInfo tag_setter_info = { NULL, NULL, NULL };

  g_type_add_interface_static (kateenc_type, GST_TYPE_TAG_SETTER,
      &tag_setter_info);
}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_kate_enc_init (GstKateEnc * ke, GstKateEncClass * gclass)
{
  GST_DEBUG_OBJECT (ke, "gst_kate_enc_init");

  ke->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (ke->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_enc_chain));
  gst_pad_set_event_function (ke->sinkpad,
      GST_DEBUG_FUNCPTR (gst_kate_enc_sink_event));
  gst_element_add_pad (GST_ELEMENT (ke), ke->sinkpad);

  ke->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_type_function (ke->srcpad,
      GST_DEBUG_FUNCPTR (gst_kate_enc_source_query_type));
  gst_pad_set_query_function (ke->srcpad,
      GST_DEBUG_FUNCPTR (gst_kate_enc_source_query));
  gst_element_add_pad (GST_ELEMENT (ke), ke->srcpad);

  ke->initialized = FALSE;
  ke->headers_sent = FALSE;
  ke->last_timestamp = 0;
  ke->latest_end_time = 0;
  ke->language = NULL;
  ke->category = NULL;
  ke->granule_rate_numerator = 1000;
  ke->granule_rate_denominator = 1;
  ke->granule_shift = 32;
  ke->original_canvas_width = 0;
  ke->original_canvas_height = 0;
  ke->keepalive_min_time = DEFAULT_KEEPALIVE_MIN_TIME;
  ke->default_spu_duration = DEFAULT_DEFAULT_SPU_DURATION;
  memcpy (ke->spu_clut, gst_kate_enc_default_clut,
      sizeof (gst_kate_enc_default_clut));
  ke->delayed_spu = FALSE;
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

  buffer = gst_buffer_new_and_alloc (kp->nbytes);
  if (G_UNLIKELY (!buffer)) {
    GST_WARNING_OBJECT (ke, "Failed to allocate buffer for %u bytes",
        kp->nbytes);
    return NULL;
  }

  memcpy (GST_BUFFER_DATA (buffer), kp->data, kp->nbytes);

  /* same system as other Ogg codecs, as per ext/ogg/README:
     OFFSET_END is the granulepos
     OFFSET is its time representation
   */
  GST_BUFFER_OFFSET_END (buffer) = granpos;
  GST_BUFFER_OFFSET (buffer) = timestamp;
  GST_BUFFER_TIMESTAMP (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = duration;

  /* data packets are each on their own page */
//  if (!header)
//    GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);

  return buffer;
}

static GstFlowReturn
gst_kate_enc_push_buffer (GstKateEnc * ke, GstBuffer * buffer)
{
  GstFlowReturn rflow;

  ke->last_timestamp = GST_BUFFER_TIMESTAMP (buffer);
  if (GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer) >
      ke->latest_end_time) {
    ke->latest_end_time =
        GST_BUFFER_TIMESTAMP (buffer) + GST_BUFFER_DURATION (buffer);
  }

  /* Hack to flush each packet on its own page - taken off the CMML encoder element */
  GST_BUFFER_DURATION (buffer) = G_MAXINT64;

  rflow = gst_pad_push (ke->srcpad, buffer);
  if (G_UNLIKELY (rflow != GST_FLOW_OK)) {
    GST_ERROR_OBJECT (ke, "Failed to push buffer: %d", rflow);
  }

  return rflow;
}

static GstFlowReturn
gst_kate_enc_push_and_free_kate_packet (GstKateEnc * ke, kate_packet * kp,
    kate_int64_t granpos, GstClockTime timestamp, GstClockTime duration,
    gboolean header)
{
  GstBuffer *buffer;

  GST_LOG_OBJECT (ke, "Creating buffer, %u bytes", kp->nbytes);
  buffer =
      gst_kate_enc_create_buffer (ke, kp, granpos, timestamp, duration, header);
  if (G_UNLIKELY (!buffer)) {
    GST_WARNING_OBJECT (ke, "Failed to create buffer, %u bytes", kp->nbytes);
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
    gst_tag_list_free (merged_tags);
  }
}

static GstFlowReturn
gst_kate_enc_send_headers (GstKateEnc * ke)
{
  GstFlowReturn rflow = GST_FLOW_OK;
  GstCaps *caps;
  GList *headers = NULL, *item;

  gst_kate_enc_set_metadata (ke);

  /* encode headers and store them in a list */
  while (1) {
    kate_packet kp;
    int ret = kate_encode_headers (&ke->k, &ke->kc, &kp);
    if (ret == 0) {
      GstBuffer *buffer =
          gst_kate_enc_create_buffer (ke, &kp, 0ll, 0ll, 0ll, TRUE);
      if (!buffer) {
        rflow = GST_FLOW_ERROR;
        break;
      }
      kate_packet_clear (&kp);

      headers = g_list_append (headers, buffer);
    } else if (ret > 0) {
      GST_LOG_OBJECT (ke, "Last header encoded");
      break;
    } else {
      GST_LOG_OBJECT (ke, "Error encoding header: %d", ret);
      rflow = GST_FLOW_ERROR;
      break;
    }
  }

  if (rflow == GST_FLOW_OK) {
    caps =
        gst_kate_util_set_header_on_caps (&ke->element,
        gst_pad_get_caps (ke->srcpad), headers);
    if (caps) {
      GST_DEBUG_OBJECT (ke, "here are the caps: %" GST_PTR_FORMAT, caps);
      gst_pad_set_caps (ke->srcpad, caps);

      GST_LOG_OBJECT (ke, "setting caps on headers");
      item = headers;
      while (item) {
        GstBuffer *buffer = item->data;
        GST_LOG_OBJECT (ke, "settings caps on header %p", buffer);
        gst_buffer_set_caps (buffer, caps);
        item = item->next;
      }

      gst_caps_unref (caps);

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
      GST_WARNING_OBJECT (ke, "Failed to flush headers: %d", rflow);
    }
  }
  return rflow;
}

enum SpuCmd
{
  SPU_CMD_FSTA_DSP = 0x00,      /* Forced Display */
  SPU_CMD_DSP = 0x01,           /* Display Start */
  SPU_CMD_STP_DSP = 0x02,       /* Display Off */
  SPU_CMD_SET_COLOR = 0x03,     /* Set the color indexes for the palette */
  SPU_CMD_SET_ALPHA = 0x04,     /* Set the alpha indexes for the palette */
  SPU_CMD_SET_DAREA = 0x05,     /* Set the display area for the SPU */
  SPU_CMD_DSPXA = 0x06,         /* Pixel data addresses */
  SPU_CMD_CHG_COLCON = 0x07,    /* Change Color & Contrast */
  SPU_CMD_END = 0xff
};

static void
gst_kate_enc_decode_colormap (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_colormap[3] = ptr[0] >> 4;
  ke->spu_colormap[2] = ptr[0] & 0x0f;
  ke->spu_colormap[1] = ptr[1] >> 4;
  ke->spu_colormap[0] = ptr[1] & 0x0f;
}

static void
gst_kate_enc_decode_alpha (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_alpha[3] = ptr[0] >> 4;
  ke->spu_alpha[2] = ptr[0] & 0x0f;
  ke->spu_alpha[1] = ptr[1] >> 4;
  ke->spu_alpha[0] = ptr[1] & 0x0f;
}

static void
gst_kate_enc_decode_area (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_left = ((((guint16) ptr[0]) & 0x3f) << 4) | (ptr[1] >> 4);
  ke->spu_top = ((((guint16) ptr[3]) & 0x3f) << 4) | (ptr[4] >> 4);
  ke->spu_right = ((((guint16) ptr[1]) & 0x03) << 8) | ptr[2];
  ke->spu_bottom = ((((guint16) ptr[4]) & 0x03) << 8) | ptr[5];
  GST_DEBUG_OBJECT (ke, "SPU area %u %u -> %u %d", ke->spu_left, ke->spu_top,
      ke->spu_right, ke->spu_bottom);
}

static void
gst_kate_enc_decode_pixaddr (GstKateEnc * ke, const guint8 * ptr)
{
  ke->spu_pix_data[0] = GST_KATE_UINT16_BE (ptr + 0);
  ke->spu_pix_data[1] = GST_KATE_UINT16_BE (ptr + 2);
}

/* heavily inspired from dvdspudec */
static guint16
gst_kate_enc_decode_colcon (GstKateEnc * ke, const guint8 * ptr)
{
  guint16 nbytes = GST_KATE_UINT16_BE (ptr + 0);
  guint16 nbytes_left = nbytes;

  GST_LOG_OBJECT (ke, "Number of bytes in color/contrast change command is %u",
      nbytes);
  if (G_UNLIKELY (nbytes < 2)) {
    GST_WARNING_OBJECT (ke,
        "Number of bytes in color/contrast change command is %u, should be at least 2",
        nbytes);
    return 0;
  }

  ptr += 2;
  nbytes_left -= 2;

  /* we will just skip that data for now */
  while (nbytes_left > 0) {
    guint32 entry, nchanges, sz;
    GST_LOG_OBJECT (ke, "Reading a color/contrast change entry, %u bytes left",
        nbytes_left);
    if (G_UNLIKELY (nbytes_left < 4)) {
      GST_WARNING_OBJECT (ke,
          "Not enough bytes to read a full color/contrast entry header");
      break;
    }
    entry = GST_READ_UINT32_BE (ptr);
    GST_LOG_OBJECT (ke, "Color/contrast change entry header is %08x", entry);
    nchanges = CLAMP ((ptr[2] >> 4), 1, 8);
    ptr += 4;
    nbytes_left -= 4;
    if (entry == 0x0fffffff) {
      GST_LOG_OBJECT (ke,
          "Encountered color/contrast change termination code, breaking, %u bytes left",
          nbytes_left);
      break;
    }
    GST_LOG_OBJECT (ke, "Color/contrast change entry has %u changes", nchanges);
    sz = 6 * nchanges;
    if (G_UNLIKELY (sz > nbytes_left)) {
      GST_WARNING_OBJECT (ke,
          "Not enough bytes to read a full color/contrast entry");
      break;
    }
    ptr += sz;
    nbytes_left -= sz;
  }
  return nbytes - nbytes_left;
}

static inline guint8
gst_kate_enc_get_nybble (const guint8 * nybbles, size_t * nybble_offset)
{
  guint8 ret;

  ret = nybbles[(*nybble_offset) / 2];

  /* If the offset is even, we shift the answer down 4 bits, otherwise not */
  if ((*nybble_offset) & 0x01)
    ret &= 0x0f;
  else
    ret = ret >> 4;

  (*nybble_offset)++;

  return ret;
}

static guint16
gst_kate_enc_get_rle_code (const guint8 * nybbles, size_t * nybble_offset)
{
  guint16 code;

  code = gst_kate_enc_get_nybble (nybbles, nybble_offset);
  if (code < 0x4) {             /* 4 .. f */
    code = (code << 4) | gst_kate_enc_get_nybble (nybbles, nybble_offset);
    if (code < 0x10) {          /* 1x .. 3x */
      code = (code << 4) | gst_kate_enc_get_nybble (nybbles, nybble_offset);
      if (code < 0x40) {        /* 04x .. 0fx */
        code = (code << 4) | gst_kate_enc_get_nybble (nybbles, nybble_offset);
      }
    }
  }
  return code;
}

static void
gst_kate_enc_crop_bitmap (GstKateEnc * ke, kate_bitmap * kb, guint16 * dx,
    guint16 * dy)
{
  int top, bottom, left, right;
  guint8 zero = 0;
  size_t n, x, y, w, h;

#if 0
  /* find the zero */
  zero = kb->pixels[0];
  for (x = 0; x < kb->width; ++x) {
    if (kb->pixels[x] != zero) {
      GST_LOG_OBJECT (ke, "top line at %u is not zero: %u", x, kb->pixels[x]);
      return;
    }
  }
#endif

  /* top */
  for (top = 0; top < kb->height; ++top) {
    int empty = 1;
    for (x = 0; x < kb->width; ++x) {
      if (G_UNLIKELY (kb->pixels[x + top * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }

  /* bottom */
  for (bottom = kb->height - 1; bottom >= top; --bottom) {
    int empty = 1;
    for (x = 0; x < kb->width; ++x) {
      if (G_UNLIKELY (kb->pixels[x + bottom * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }

  /* left */
  for (left = 0; left < kb->width; ++left) {
    int empty = 1;
    for (y = top; y <= bottom; ++y) {
      if (G_UNLIKELY (kb->pixels[left + y * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }

  /* right */
  for (right = kb->width - 1; right >= left; --right) {
    int empty = 1;
    for (y = top; y <= bottom; ++y) {
      if (G_UNLIKELY (kb->pixels[right + y * kb->width] != zero)) {
        empty = 0;
        break;
      }
    }
    if (!empty)
      break;
  }


  w = right - left + 1;
  h = bottom - top + 1;
  GST_LOG_OBJECT (ke, "cropped from %zu %zu to %zu %zu", kb->width, kb->height,
      w, h);
  *dx += left;
  *dy += top;
  n = 0;
  for (y = 0; y < h; ++y) {
    memmove (kb->pixels + n, kb->pixels + kb->width * (y + top) + left, w);
    n += w;
  }
  kb->width = w;
  kb->height = h;
}

#define CHECK(x) do { guint16 _ = (x); if (G_UNLIKELY((_) > sz)) { GST_WARNING_OBJECT (ke, "SPU overflow"); return GST_FLOW_ERROR; } } while (0)
#define ADVANCE(x) do { guint16 _ = (x); ptr += (_); sz -= (_); } while (0)
#define IGNORE(x) do { guint16 __ = (x); CHECK (__); ADVANCE (__); } while (0)

static GstFlowReturn
gst_kate_enc_decode_command_sequence (GstKateEnc * ke, GstBuffer * buf,
    guint16 command_sequence_offset)
{
  guint16 date;
  guint16 next_command_sequence;
  const guint8 *ptr;
  guint16 sz;

  if (command_sequence_offset >= GST_BUFFER_SIZE (buf)) {
    GST_WARNING_OBJECT (ke, "Command sequence offset %u is out of range %u",
        command_sequence_offset, GST_BUFFER_SIZE (buf));
    return GST_FLOW_ERROR;
  }

  ptr = GST_BUFFER_DATA (buf) + command_sequence_offset;
  sz = GST_BUFFER_SIZE (buf) - command_sequence_offset;

  GST_DEBUG_OBJECT (ke, "Decoding command sequence at %u (%u bytes)",
      command_sequence_offset, sz);

  CHECK (2);
  date = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  GST_DEBUG_OBJECT (ke, "date %u", date);

  CHECK (2);
  next_command_sequence = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  GST_DEBUG_OBJECT (ke, "next command sequence at %u", next_command_sequence);

  while (sz) {
    guint8 cmd = *ptr++;
    switch (cmd) {
      case SPU_CMD_FSTA_DSP:   /* 0x00 */
        GST_DEBUG_OBJECT (ke, "[0] DISPLAY");
        break;
      case SPU_CMD_DSP:        /* 0x01 */
        GST_DEBUG_OBJECT (ke, "[1] SHOW");
        ke->show_time = date;
        break;
      case SPU_CMD_STP_DSP:    /* 0x02 */
        GST_DEBUG_OBJECT (ke, "[2] HIDE");
        ke->hide_time = date;
        break;
      case SPU_CMD_SET_COLOR:  /* 0x03 */
        GST_DEBUG_OBJECT (ke, "[3] SET COLOR");
        CHECK (2);
        gst_kate_enc_decode_colormap (ke, ptr);
        ADVANCE (2);
        break;
      case SPU_CMD_SET_ALPHA:  /* 0x04 */
        GST_DEBUG_OBJECT (ke, "[4] SET ALPHA");
        CHECK (2);
        gst_kate_enc_decode_alpha (ke, ptr);
        ADVANCE (2);
        break;
      case SPU_CMD_SET_DAREA:  /* 0x05 */
        GST_DEBUG_OBJECT (ke, "[5] SET DISPLAY AREA");
        CHECK (6);
        gst_kate_enc_decode_area (ke, ptr);
        ADVANCE (6);
        break;
      case SPU_CMD_DSPXA:      /* 0x06 */
        GST_DEBUG_OBJECT (ke, "[6] SET PIXEL ADDRESSES");
        CHECK (4);
        gst_kate_enc_decode_pixaddr (ke, ptr);
        GST_DEBUG_OBJECT (ke, "  -> first pixel address %u",
            ke->spu_pix_data[0]);
        GST_DEBUG_OBJECT (ke, "  -> second pixel address %u",
            ke->spu_pix_data[1]);
        ADVANCE (4);
        break;
      case SPU_CMD_CHG_COLCON: /* 0x07 */
        GST_DEBUG_OBJECT (ke, "[7] CHANGE COLOR/CONTRAST");
        CHECK (2);
        ADVANCE (gst_kate_enc_decode_colcon (ke, ptr));
        break;
      case SPU_CMD_END:        /* 0xff */
        GST_DEBUG_OBJECT (ke, "[0xff] END");
        if (next_command_sequence != command_sequence_offset) {
          GST_DEBUG_OBJECT (ke, "Jumping to next sequence at offset %u",
              next_command_sequence);
          return gst_kate_enc_decode_command_sequence (ke, buf,
              next_command_sequence);
        } else {
          GST_DEBUG_OBJECT (ke, "No more sequences to decode");
          return GST_FLOW_OK;
        }
        break;
      default:
        GST_WARNING_OBJECT (ke, "invalid SPU command: %u", cmd);
        return GST_FLOW_ERROR;
    }
  }
  return GST_FLOW_ERROR;
}

static inline int
gst_kate_enc_clamp (int value)
{
  if (value < 0)
    return 0;
  if (value > 255)
    return 255;
  return value;
}

static void
gst_kate_enc_yuv2rgb (int y, int u, int v, int *r, int *g, int *b)
{
#if 0
  *r = gst_kate_enc_clamp (y + 1.371 * v);
  *g = gst_kate_enc_clamp (y - 0.698 * v - 0.336 * u);
  *b = gst_kate_enc_clamp (y + 1.732 * u);
#elif 0
  *r = gst_kate_enc_clamp (y + u);
  *g = gst_kate_enc_clamp (y - (76 * u - 26 * v) / 256);
  *b = gst_kate_enc_clamp (y + v);
#else
  y = (y - 16) * 255 / 219;
  u -= 128;
  v -= 128;

  *r = gst_kate_enc_clamp (y + 1.402 * 255 / 224 * v);
  *g = gst_kate_enc_clamp (y + 0.34414 * 255 / 224 * v -
      0.71414 * 255 / 224 * u);
  *b = gst_kate_enc_clamp (y + 1.772 * 244 / 224 * u);
#endif
}

static GstFlowReturn
gst_kate_enc_create_spu_palette (GstKateEnc * ke, kate_palette * kp)
{
  size_t n;

  kate_palette_init (kp);
  kp->ncolors = 4;
  kp->colors = (kate_color *) g_malloc (kp->ncolors * sizeof (kate_color));
  if (G_UNLIKELY (!kp->colors))
    return GST_FLOW_ERROR;

#if 1
  for (n = 0; n < kp->ncolors; ++n) {
    int idx = ke->spu_colormap[n];
    guint32 color = ke->spu_clut[idx];
    int y = (color >> 16) & 0xff;
    int v = (color >> 8) & 0xff;
    int u = color & 0xff;
    int r, g, b;
    gst_kate_enc_yuv2rgb (y, u, v, &r, &g, &b);
    kp->colors[n].r = r;
    kp->colors[n].g = g;
    kp->colors[n].b = b;
    kp->colors[n].a = ke->spu_alpha[n] * 17;
  }
#else
  /* just make a ramp from 0 to 255 for those non transparent colors */
  for (n = 0; n < kp->ncolors; ++n)
    if (ke->spu_alpha[n] == 0)
      ++ntrans;

  for (n = 0; n < kp->ncolors; ++n) {
    kp->colors[n].r = luma;
    kp->colors[n].g = luma;
    kp->colors[n].b = luma;
    kp->colors[n].a = ke->spu_alpha[n] * 17;
    if (ke->spu_alpha[n])
      luma /= 2;
  }
#endif

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_kate_enc_decode_spu (GstKateEnc * ke, GstBuffer * buf, kate_region * kr,
    kate_bitmap * kb, kate_palette * kp)
{
  const guint8 *ptr = GST_BUFFER_DATA (buf);
  size_t sz = GST_BUFFER_SIZE (buf);
  guint16 packet_size;
  guint16 x, y;
  size_t n;
  guint8 *pixptr[2];
  size_t nybble_offset[2];
  size_t max_nybbles[2];
  GstFlowReturn rflow;
  guint16 next_command_sequence;
  guint16 code;

  /* before decoding anything, initialize to sensible defaults */
  memset (ke->spu_colormap, 0, sizeof (ke->spu_colormap));
  memset (ke->spu_alpha, 0, sizeof (ke->spu_alpha));
  ke->spu_top = ke->spu_left = 1;
  ke->spu_bottom = ke->spu_right = 0;
  ke->spu_pix_data[0] = ke->spu_pix_data[1] = 0;
  ke->show_time = ke->hide_time = 0;

  /* read sizes and get to the start of the data */
  CHECK (2);
  packet_size = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  GST_DEBUG_OBJECT (ke, "packet size %u (GstBuffer size %u)", packet_size,
      GST_BUFFER_SIZE (buf));

  CHECK (2);
  next_command_sequence = GST_KATE_UINT16_BE (ptr);
  ADVANCE (2);
  ptr = GST_BUFFER_DATA (buf) + next_command_sequence;
  sz = GST_BUFFER_SIZE (buf) - next_command_sequence;
  GST_DEBUG_OBJECT (ke, "next command sequence at %u for %u",
      next_command_sequence, sz);

  rflow = gst_kate_enc_decode_command_sequence (ke, buf, next_command_sequence);
  if (G_UNLIKELY (rflow != GST_FLOW_OK))
    return rflow;

  /* if no addresses or sizes were given, or if they define an empty SPU, nothing more to do */
  if (G_UNLIKELY (ke->spu_right - ke->spu_left < 0
          || ke->spu_bottom - ke->spu_top < 0 || ke->spu_pix_data[0] == 0
          || ke->spu_pix_data[1] == 0)) {
    GST_WARNING_OBJECT (ke, "SPU area is empty, nothing to encode");
    return GST_FLOW_ERROR;
  }

  /* create the palette */
  rflow = gst_kate_enc_create_spu_palette (ke, kp);
  if (G_UNLIKELY (rflow != GST_FLOW_OK))
    return rflow;

  /* create the bitmap */
  kate_bitmap_init (kb);
  kb->width = ke->spu_right - ke->spu_left + 1;
  kb->height = ke->spu_bottom - ke->spu_top + 1;
  kb->bpp = 2;
  kb->type = kate_bitmap_type_paletted;
  kb->pixels = (unsigned char *) g_malloc (kb->width * kb->height);
  if (G_UNLIKELY (!kb->pixels)) {
    GST_WARNING_OBJECT (ke, "Failed to allocate memory for pixel data");
    return GST_FLOW_ERROR;
  }

  n = 0;
  pixptr[0] = GST_BUFFER_DATA (buf) + ke->spu_pix_data[0];
  pixptr[1] = GST_BUFFER_DATA (buf) + ke->spu_pix_data[1];
  nybble_offset[0] = 0;
  nybble_offset[1] = 0;
  max_nybbles[0] = 2 * (packet_size - ke->spu_pix_data[0]);
  max_nybbles[1] = 2 * (packet_size - ke->spu_pix_data[1]);
  for (y = 0; y < kb->height; ++y) {
    nybble_offset[y & 1] = GST_ROUND_UP_2 (nybble_offset[y & 1]);
    for (x = 0; x < kb->width;) {
      if (G_UNLIKELY (nybble_offset[y & 1] >= max_nybbles[y & 1])) {
        GST_DEBUG_OBJECT (ke, "RLE overflow, clearing the remainder");
        memset (kb->pixels + n, 0, kb->width - x);
        n += kb->width - x;
        break;
      }
      code = gst_kate_enc_get_rle_code (pixptr[y & 1], &nybble_offset[y & 1]);
      if (code == 0) {
        memset (kb->pixels + n, 0, kb->width - x);
        n += kb->width - x;
        break;
      } else {
        guint16 npixels = code >> 2;
        guint16 pixel = code & 3;
        if (npixels > kb->width - x) {
          npixels = kb->width - x;
        }
        memset (kb->pixels + n, pixel, npixels);
        n += npixels;
        x += npixels;
      }
    }
  }

  GST_LOG_OBJECT (ke, "%u/%u bytes left in the data packet",
      max_nybbles[0] - nybble_offset[0], max_nybbles[1] - nybble_offset[1]);

  /* some streams seem to have huge uncropped SPUs, fix those up */
  x = ke->spu_left;
  y = ke->spu_top;
  gst_kate_enc_crop_bitmap (ke, kb, &x, &y);

  /* create the region */
  kate_region_init (kr);
  if (ke->original_canvas_width > 0 && ke->original_canvas_height > 0) {
    /* prefer relative sizes in case we're encoding for a different resolution
       that what the SPU was created for */
    kr->metric = kate_millionths;
    kr->x = 1000000 * x / ke->original_canvas_width;
    kr->y = 1000000 * y / ke->original_canvas_height;
    kr->w = 1000000 * kb->width / ke->original_canvas_width;
    kr->h = 1000000 * kb->height / ke->original_canvas_height;
  } else {
    kr->metric = kate_pixel;
    kr->x = x;
    kr->y = y;
    kr->w = kb->width;
    kr->h = kb->height;
  }

  /* some SPUs have no hide time */
  if (ke->hide_time == 0) {
    GST_INFO_OBJECT (ke, "SPU has no hide time");
    /* now, we don't know when the next SPU is scheduled to go, since we probably
       haven't received it yet, so we'll just make it a 1 second delay, which is
       probably going to end before the next one while being readable */
    //ke->hide_time = ke->show_time + (1000*90/1024);
  }

  return GST_FLOW_OK;
}

#undef IGNORE
#undef ADVANCE
#undef CHECK

static GstFlowReturn
gst_kate_enc_chain_push_packet (GstKateEnc * ke, kate_packet * kp,
    GstClockTime start, GstClockTime duration)
{
  kate_int64_t granpos;
  GstFlowReturn rflow;

  granpos = kate_encode_get_granule (&ke->k);
  if (G_UNLIKELY (granpos < 0)) {
    GST_WARNING_OBJECT (ke, "Negative granpos for packet");
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
    GST_WARNING_OBJECT (ke, "Failed to encode keepalive packet: %d", ret);
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
      rflow = GST_FLOW_ERROR;
    } else {
      rflow =
          gst_kate_enc_chain_push_packet (ke, &kp, ke->delayed_start,
          now - ke->delayed_start + 1);
    }

    if (rflow == GST_FLOW_OK) {
      GST_DEBUG_OBJECT (ke, "delayed SPU packet flushed");
    } else {
      GST_WARNING_OBJECT (ke, "Failed to flush delayed SPU packet: %d", rflow);
    }

    /* forget it even if we couldn't flush it */
    ke->delayed_spu = FALSE;

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
  kate_region kregion;
  kate_bitmap kbitmap;
  kate_palette kpalette;
  GstFlowReturn rflow;
  int ret = 0;

  rflow = gst_kate_enc_decode_spu (ke, buf, &kregion, &kbitmap, &kpalette);
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
  } else if (G_UNLIKELY (kbitmap.width == 0 || kbitmap.height == 0)) {
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
    GST_DEBUG_OBJECT (ke, "Encoding %dx%d SPU: (%u bytes) from %f to %f",
        kbitmap.width, kbitmap.height, GST_BUFFER_SIZE (buf), t0, t1);
    ret = kate_encode_set_region (&ke->k, &kregion);
    if (G_UNLIKELY (ret < 0)) {
      GST_WARNING_OBJECT (ke, "Failed to set event region (%d)", ret);
      rflow = GST_FLOW_ERROR;
    } else {
      ret = kate_encode_set_palette (&ke->k, &kpalette);
      if (G_UNLIKELY (ret < 0)) {
        GST_WARNING_OBJECT (ke, "Failed to set event palette (%d)", ret);
        rflow = GST_FLOW_ERROR;
      } else {
        ret = kate_encode_set_bitmap (&ke->k, &kbitmap);
        if (G_UNLIKELY (ret < 0)) {
          GST_WARNING_OBJECT (ke, "Failed to set event bitmap (%d)", ret);
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
            rflow = GST_FLOW_OK;
          } else {
            ret = kate_encode_text (&ke->k, t0, t1, "", 0, &kp);
            if (G_UNLIKELY (ret < 0)) {
              GST_WARNING_OBJECT (ke,
                  "Failed to encode empty text for SPU buffer (%d)", ret);
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
    g_free (kpalette.colors);
    g_free (kbitmap.pixels);
  }

  return rflow;
}

static GstFlowReturn
gst_kate_enc_chain_text (GstKateEnc * ke, GstBuffer * buf,
    const char *mime_type)
{
  kate_packet kp;
  int ret = 0;
  GstFlowReturn rflow;
  GstClockTime start = GST_BUFFER_TIMESTAMP (buf);
  GstClockTime stop = GST_BUFFER_TIMESTAMP (buf) + GST_BUFFER_DURATION (buf);

  if (!strcmp (mime_type, "text/x-pango-markup")) {
    ret = kate_encode_set_markup_type (&ke->k, kate_markup_simple);
  } else {
    ret = kate_encode_set_markup_type (&ke->k, kate_markup_none);
  }

  if (G_UNLIKELY (ret < 0)) {
    GST_WARNING_OBJECT (ke, "Failed to set markup type (%d)", ret);
    rflow = GST_FLOW_ERROR;
  } else {
    const char *text = (const char *) GST_BUFFER_DATA (buf);
    if (text) {
      size_t text_len = GST_BUFFER_SIZE (buf);
      kate_float t0 = start / (double) GST_SECOND;
      kate_float t1 = stop / (double) GST_SECOND;
      GST_LOG_OBJECT (ke, "Encoding text: %*.*s (%u bytes) from %f to %f",
          (int) text_len, (int) text_len, GST_BUFFER_DATA (buf),
          GST_BUFFER_SIZE (buf), t0, t1);
      ret = kate_encode_text (&ke->k, t0, t1, text, text_len, &kp);
      if (G_UNLIKELY (ret < 0)) {
        rflow = GST_FLOW_ERROR;
      } else {
        rflow =
            gst_kate_enc_chain_push_packet (ke, &kp, start, stop - start + 1);
      }
    } else {
      GST_WARNING_OBJECT (ke, "No text in text packet");
      rflow = GST_FLOW_ERROR;
    }
  }

  return rflow;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_kate_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstKateEnc *ke = GST_KATE_ENC (gst_pad_get_parent (pad));
  GstFlowReturn rflow = GST_FLOW_OK;
  GstCaps *caps;
  const gchar *mime_type = NULL;

  GST_DEBUG_OBJECT (ke, "got packet, %u bytes", GST_BUFFER_SIZE (buf));

  /* get the type of the data we're being sent */
  caps = GST_PAD_CAPS (pad);
  if (G_UNLIKELY (caps == NULL)) {
    GST_ERROR_OBJECT (ke, ": Could not get caps of pad");
    rflow = GST_FLOW_ERROR;
  } else {
    const GstStructure *structure = gst_caps_get_structure (caps, 0);
    if (structure)
      mime_type = gst_structure_get_name (structure);

    if (mime_type) {
      GST_LOG_OBJECT (ke, "Packet has MIME type %s", mime_type);

      /* first push headers if we haven't done that yet */
      rflow = gst_kate_enc_flush_headers (ke);

      if (G_LIKELY (rflow == GST_FLOW_OK)) {
        /* flush any packet we had waiting */
        rflow = gst_kate_enc_flush_waiting (ke, GST_BUFFER_TIMESTAMP (buf));

        if (G_LIKELY (rflow == GST_FLOW_OK)) {
          if (!strcmp (mime_type, GST_KATE_SPU_MIME_TYPE)) {
            /* encode a kate_bitmap */
            rflow = gst_kate_enc_chain_spu (ke, buf);
          } else {
            /* encode text */
            rflow = gst_kate_enc_chain_text (ke, buf, mime_type);
          }
        }
      }
    } else {
      GST_WARNING_OBJECT (ke, "Packet has no MIME type, ignored");
    }
  }

  gst_buffer_unref (buf);

  gst_object_unref (ke);

  GST_LOG_OBJECT (ke, "Leaving chain function");

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
      ke->tags = gst_tag_list_new ();
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG_OBJECT (ke, "READY -> PAUSED, initializing kate state");
      ret = kate_info_init (&ke->ki);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke, "failed to initialize kate info structure: %d",
            ret);
        break;
      }
      if (ke->language) {
        ret = kate_info_set_language (&ke->ki, ke->language);
        if (ret < 0) {
          GST_WARNING_OBJECT (ke, "failed to set stream language: %d", ret);
          break;
        }
      }
      if (ke->category) {
        ret = kate_info_set_category (&ke->ki, ke->category);
        if (ret < 0) {
          GST_WARNING_OBJECT (ke, "failed to set stream category: %d", ret);
          break;
        }
      }
      ret =
          kate_info_set_original_canvas_size (&ke->ki,
          ke->original_canvas_width, ke->original_canvas_height);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke, "failed to set original canvas size: %d", ret);
        break;
      }
      ret = kate_comment_init (&ke->kc);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke,
            "failed to initialize kate comment structure: %d", ret);
        break;
      }
      ret = kate_encode_init (&ke->k, &ke->ki);
      if (ret < 0) {
        GST_WARNING_OBJECT (ke, "failed to initialize kate state: %d", ret);
        break;
      }
      ke->headers_sent = FALSE;
      ke->initialized = TRUE;
      ke->last_timestamp = 0;
      ke->latest_end_time = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_tag_list_free (ke->tags);
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

#if 1
static const GstQueryType *
gst_kate_enc_source_query_type (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return types;
}
#endif

static gboolean
gst_kate_enc_source_query (GstPad * pad, GstQuery * query)
{
  GstKateEnc *ke;
  gboolean res = FALSE;

  ke = GST_KATE_ENC (gst_pad_get_parent (pad));

  GST_DEBUG ("source query %d", GST_QUERY_TYPE (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!gst_kate_enc_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val)) {
        return gst_pad_query_default (pad, query);
      }
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      res = TRUE;
    }
      break;
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (ke);

  return res;
}

static gboolean
gst_kate_enc_sink_event (GstPad * pad, GstEvent * event)
{
  GstKateEnc *ke = GST_KATE_ENC (gst_pad_get_parent (pad));
  GstStructure *structure;
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      GST_LOG_OBJECT (ke, "Got newsegment event");
      if (ke->initialized) {
        GST_LOG_OBJECT (ke, "ensuring all headers are in");
        if (gst_kate_enc_flush_headers (ke) != GST_FLOW_OK) {
          GST_WARNING_OBJECT (ke, "Failed to flush headers");
        } else {
          GstFormat format;
          gint64 timestamp;

          gst_event_parse_new_segment (event, NULL, NULL, &format, &timestamp,
              NULL, NULL);
          if (format != GST_FORMAT_TIME || !GST_CLOCK_TIME_IS_VALID (timestamp)) {
            GST_WARNING_OBJECT (ke,
                "No time in newsegment event %p, format %d, timestamp %lld",
                event, (int) format, (long long) timestamp);
            /* to be safe, we'd need to generate a keepalive anyway, but we'd have to guess at the timestamp to use; a
               good guess would be the last known timestamp plus the keepalive time, but if we then get a packet with a
               timestamp less than this, it would fail to encode, which would be Bad. If we don't encode a keepalive, we
               run the risk of stalling the pipeline and hanging, which is Very Bad. Oh dear. We can't exit(-1), can we ? */
          } else {
            float t = timestamp / (double) GST_SECOND;

            if (ke->delayed_spu
                && t - ke->delayed_start / (double) GST_SECOND >=
                ke->default_spu_duration) {
              if (G_UNLIKELY (gst_kate_enc_flush_waiting (ke,
                          timestamp) != GST_FLOW_OK)) {
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
                gst_kate_enc_generate_keepalive (ke, timestamp);
              }
            }
          }
        }
      }
      ret = gst_pad_push_event (ke->srcpad, event);
      break;

    case GST_EVENT_CUSTOM_DOWNSTREAM:
      GST_LOG_OBJECT (ke, "Got custom downstream event");
      /* adapted from the dvdsubdec element */
      structure = event->structure;
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
      ret = gst_pad_event_default (pad, event);
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
            GST_WARNING_OBJECT (ke, "Failed to encode EOS packet: %d", ret);
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
      ret = gst_pad_event_default (pad, event);
      break;

    default:
      GST_LOG_OBJECT (ke, "Got unhandled event");
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (ke);
  return ret;
}
