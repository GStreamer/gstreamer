/* GStreamer
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstogmparse.c: OGM stream header parsing (and data passthrough)
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

#include <string.h>

#include <gst/gst.h>
#include <gst/riff/riff-media.h>

GST_DEBUG_CATEGORY_STATIC (gst_ogm_parse_debug);
#define GST_CAT_DEFAULT gst_ogm_parse_debug

#define GST_TYPE_OGM_VIDEO_PARSE (gst_ogm_video_parse_get_type())
#define GST_IS_OGM_VIDEO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OGM_VIDEO_PARSE))

#define GST_TYPE_OGM_AUDIO_PARSE (gst_ogm_audio_parse_get_type())
#define GST_IS_OGM_AUDIO_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OGM_AUDIO_PARSE))

#define GST_TYPE_OGM_TEXT_PARSE (gst_ogm_text_parse_get_type())
#define GST_IS_OGM_TEXT_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OGM_TEXT_PARSE))

#define GST_TYPE_OGM_PARSE (gst_ogm_parse_get_type())
#define GST_OGM_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OGM_PARSE, GstOgmParse))
#define GST_OGM_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OGM_PARSE, GstOgmParse))
#define GST_IS_OGM_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OGM_PARSE))
#define GST_IS_OGM_PARSE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OGM_PARSE))
#define GST_OGM_PARSE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_OGM_PARSE, GstOgmParseClass))

typedef struct _stream_header_video
{
  gint32 width;
  gint32 height;
} stream_header_video;

typedef struct _stream_header_audio
{
  gint16 channels;
  gint16 blockalign;
  gint32 avgbytespersec;
} stream_header_audio;

typedef struct _stream_header
{
  gchar streamtype[8];
  gchar subtype[4];

  /* size of the structure */
  gint32 size;

  /* in reference time */
  gint64 time_unit;

  gint64 samples_per_unit;

  /* in media time */
  gint32 default_len;

  gint32 buffersize;
  gint32 bits_per_sample;

  union
  {
    stream_header_video video;
    stream_header_audio audio;
    /* text has no additional data */
  } s;
} stream_header;

typedef struct _GstOgmParse
{
  GstElement element;

  /* pads */
  GstPad *srcpad, *sinkpad;
  GstPadTemplate *srcpadtempl;

  /* audio or video */
  stream_header hdr;

  /* expected next granulepos (used for timestamp guessing) */
  guint64 next_granulepos;
} GstOgmParse;

typedef struct _GstOgmParseClass
{
  GstElementClass parent_class;
} GstOgmParseClass;

static GstStaticPadTemplate ogm_video_parse_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ogm-video"));
static GstStaticPadTemplate ogm_audio_parse_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ogm-audio"));
static GstStaticPadTemplate ogm_text_parse_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-ogm-text"));
static GstPadTemplate *video_src_templ, *audio_src_templ, *text_src_templ;

static GType gst_ogm_audio_parse_get_type (void);
static GType gst_ogm_video_parse_get_type (void);
static GType gst_ogm_text_parse_get_type (void);
static GType gst_ogm_parse_get_type (void);

static void gst_ogm_audio_parse_base_init (GstOgmParseClass * klass);
static void gst_ogm_video_parse_base_init (GstOgmParseClass * klass);
static void gst_ogm_text_parse_base_init (GstOgmParseClass * klass);
static void gst_ogm_parse_class_init (GstOgmParseClass * klass);
static void gst_ogm_parse_init (GstOgmParse * ogm);
static void gst_ogm_video_parse_init (GstOgmParse * ogm);
static void gst_ogm_audio_parse_init (GstOgmParse * ogm);
static void gst_ogm_text_parse_init (GstOgmParse * ogm);

#if 0
static const GstFormat *gst_ogm_parse_get_sink_formats (GstPad * pad);
#endif

static const GstQueryType *gst_ogm_parse_get_sink_querytypes (GstPad * pad);
static gboolean gst_ogm_parse_sink_query (GstPad * pad, GstQuery * query);
static gboolean gst_ogm_parse_sink_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);

static GstFlowReturn gst_ogm_parse_chain (GstPad * pad, GstBuffer * buffer);

static GstStateChangeReturn gst_ogm_parse_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

static GType
gst_ogm_parse_get_type (void)
{
  static GType ogm_parse_type = 0;

  if (!ogm_parse_type) {
    static const GTypeInfo ogm_parse_info = {
      sizeof (GstOgmParseClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_ogm_parse_class_init,
      NULL,
      NULL,
      sizeof (GstOgmParse),
      0,
      (GInstanceInitFunc) gst_ogm_parse_init,
    };

    ogm_parse_type =
        g_type_register_static (GST_TYPE_ELEMENT,
        "GstOgmParse", &ogm_parse_info, 0);
  }

  return ogm_parse_type;
}

static GType
gst_ogm_audio_parse_get_type (void)
{
  static GType ogm_audio_parse_type = 0;

  if (!ogm_audio_parse_type) {
    static const GTypeInfo ogm_audio_parse_info = {
      sizeof (GstOgmParseClass),
      (GBaseInitFunc) gst_ogm_audio_parse_base_init,
      NULL,
      NULL,
      NULL,
      NULL,
      sizeof (GstOgmParse),
      0,
      (GInstanceInitFunc) gst_ogm_audio_parse_init,
    };

    ogm_audio_parse_type =
        g_type_register_static (GST_TYPE_OGM_PARSE,
        "GstOgmAudioParse", &ogm_audio_parse_info, 0);
  }

  return ogm_audio_parse_type;
}

GType
gst_ogm_video_parse_get_type (void)
{
  static GType ogm_video_parse_type = 0;

  if (!ogm_video_parse_type) {
    static const GTypeInfo ogm_video_parse_info = {
      sizeof (GstOgmParseClass),
      (GBaseInitFunc) gst_ogm_video_parse_base_init,
      NULL,
      NULL,
      NULL,
      NULL,
      sizeof (GstOgmParse),
      0,
      (GInstanceInitFunc) gst_ogm_video_parse_init,
    };

    ogm_video_parse_type =
        g_type_register_static (GST_TYPE_OGM_PARSE,
        "GstOgmVideoParse", &ogm_video_parse_info, 0);
  }

  return ogm_video_parse_type;
}

GType
gst_ogm_text_parse_get_type (void)
{
  static GType ogm_text_parse_type = 0;

  if (!ogm_text_parse_type) {
    static const GTypeInfo ogm_text_parse_info = {
      sizeof (GstOgmParseClass),
      (GBaseInitFunc) gst_ogm_text_parse_base_init,
      NULL,
      NULL,
      NULL,
      NULL,
      sizeof (GstOgmParse),
      0,
      (GInstanceInitFunc) gst_ogm_text_parse_init,
    };

    ogm_text_parse_type =
        g_type_register_static (GST_TYPE_OGM_PARSE,
        "GstOgmTextParse", &ogm_text_parse_info, 0);
  }

  return ogm_text_parse_type;
}

static void
gst_ogm_audio_parse_base_init (GstOgmParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_ogm_audio_parse_details =
      GST_ELEMENT_DETAILS ("OGM audio stream parser",
      "Codec/Decoder/Audio",
      "parse an OGM audio header and stream",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstCaps *caps = gst_riff_create_audio_template_caps ();

  gst_element_class_set_details (element_class, &gst_ogm_audio_parse_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogm_audio_parse_sink_template_factory));
  audio_src_templ = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_SOMETIMES, caps);
  gst_element_class_add_pad_template (element_class, audio_src_templ);
}

static void
gst_ogm_video_parse_base_init (GstOgmParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_ogm_video_parse_details =
      GST_ELEMENT_DETAILS ("OGM video stream parser",
      "Codec/Decoder/Video",
      "parse an OGM video header and stream",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstCaps *caps = gst_riff_create_video_template_caps ();

  gst_element_class_set_details (element_class, &gst_ogm_video_parse_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogm_video_parse_sink_template_factory));
  video_src_templ = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_SOMETIMES, caps);
  gst_element_class_add_pad_template (element_class, video_src_templ);
}

static void
gst_ogm_text_parse_base_init (GstOgmParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_ogm_text_parse_details =
      GST_ELEMENT_DETAILS ("OGM text stream parser",
      "Codec/Decoder/Subtitle",
      "parse an OGM text header and stream",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  GstCaps *caps = gst_caps_new_simple ("text/plain", NULL, NULL);

  gst_element_class_set_details (element_class, &gst_ogm_text_parse_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&ogm_text_parse_sink_template_factory));
  text_src_templ = gst_pad_template_new ("src",
      GST_PAD_SRC, GST_PAD_SOMETIMES, caps);
  gst_element_class_add_pad_template (element_class, text_src_templ);
}

static void
gst_ogm_parse_class_init (GstOgmParseClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_ogm_parse_change_state;
}

static void
gst_ogm_parse_init (GstOgmParse * ogm)
{
  /* initalize */
  memset (&ogm->hdr, 0, sizeof (ogm->hdr));
  ogm->next_granulepos = 0;
  ogm->srcpad = NULL;
}

static void
gst_ogm_audio_parse_init (GstOgmParse * ogm)
{
  /* create the pads */
  ogm->sinkpad =
      gst_pad_new_from_static_template (&ogm_audio_parse_sink_template_factory,
      "sink");
  gst_pad_set_query_function (ogm->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ogm_parse_sink_query));
  gst_pad_set_chain_function (ogm->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ogm_parse_chain));
  gst_element_add_pad (GST_ELEMENT (ogm), ogm->sinkpad);

#if 0
  ogm->srcpad = gst_pad_new_from_template (audio_src_templ, "src");
  gst_pad_use_explicit_caps (ogm->srcpad);
  gst_element_add_pad (GST_ELEMENT (ogm), ogm->srcpad);
#endif
  ogm->srcpad = NULL;
  ogm->srcpadtempl = audio_src_templ;
}

static void
gst_ogm_video_parse_init (GstOgmParse * ogm)
{
  /* create the pads */
  ogm->sinkpad =
      gst_pad_new_from_static_template (&ogm_video_parse_sink_template_factory,
      "sink");
  gst_pad_set_query_function (ogm->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ogm_parse_sink_query));
  gst_pad_set_chain_function (ogm->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ogm_parse_chain));
  gst_element_add_pad (GST_ELEMENT (ogm), ogm->sinkpad);

#if 0
  ogm->srcpad = gst_pad_new_from_template (video_src_templ, "src");
  gst_pad_use_explicit_caps (ogm->srcpad);
  gst_element_add_pad (GST_ELEMENT (ogm), ogm->srcpad);
#endif
  ogm->srcpad = NULL;
  ogm->srcpadtempl = video_src_templ;
}

static void
gst_ogm_text_parse_init (GstOgmParse * ogm)
{
  /* create the pads */
  ogm->sinkpad =
      gst_pad_new_from_static_template (&ogm_text_parse_sink_template_factory,
      "sink");
  gst_pad_set_query_type_function (ogm->sinkpad,
      gst_ogm_parse_get_sink_querytypes);
  gst_pad_set_query_function (ogm->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ogm_parse_sink_query));
  gst_pad_set_chain_function (ogm->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ogm_parse_chain));
  gst_element_add_pad (GST_ELEMENT (ogm), ogm->sinkpad);

#if 0
  ogm->srcpad = gst_pad_new_from_template (text_src_templ, "src");
  gst_pad_use_explicit_caps (ogm->srcpad);
  gst_element_add_pad (GST_ELEMENT (ogm), ogm->srcpad);
#endif
  ogm->srcpad = NULL;
  ogm->srcpadtempl = text_src_templ;
}

#if 0
static const GstFormat *
gst_ogm_parse_get_sink_formats (GstPad * pad)
{
  static GstFormat formats[] = {
    GST_FORMAT_DEFAULT,
    GST_FORMAT_TIME,
    0
  };

  return formats;
}
#endif

static const GstQueryType *
gst_ogm_parse_get_sink_querytypes (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    0
  };

  return types;
}

static gboolean
gst_ogm_parse_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = FALSE;
  GstOgmParse *ogm = GST_OGM_PARSE (gst_pad_get_parent (pad));

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          switch (ogm->hdr.streamtype[0]) {
            case 'a':
              *dest_value = GST_SECOND * src_value / ogm->hdr.samples_per_unit;
              res = TRUE;
              break;
            case 'v':
            case 't':
              *dest_value = (GST_SECOND / 10000000) *
                  ogm->hdr.time_unit * src_value;
              res = TRUE;
              break;
            default:
              break;
          }
          break;
        default:
          break;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          switch (ogm->hdr.streamtype[0]) {
            case 'a':
              *dest_value = ogm->hdr.samples_per_unit * src_value / GST_SECOND;
              res = TRUE;
              break;
            case 'v':
            case 't':
              *dest_value = src_value /
                  ((GST_SECOND / 10000000) * ogm->hdr.time_unit);
              res = TRUE;
              break;
            default:
              break;
          }
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  gst_object_unref (ogm);
  return res;
}

static gboolean
gst_ogm_parse_sink_query (GstPad * pad, GstQuery * query)
{
  GstOgmParse *ogm = GST_OGM_PARSE (gst_pad_get_parent (pad));
  GstFormat format;
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 val;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_DEFAULT && format != GST_FORMAT_TIME)
        break;

      if ((res = gst_ogm_parse_sink_convert (pad,
                  GST_FORMAT_DEFAULT, ogm->next_granulepos, &format, &val))) {
        /* don't know the total length here.. */
        gst_query_set_position (query, format, val);
      }
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      /* peel off input */
      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if ((res = gst_ogm_parse_sink_convert (pad, src_fmt, src_val,
                  &dest_fmt, &dest_val))) {
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (ogm);
  return res;
}

static GstFlowReturn
gst_ogm_parse_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstOgmParse *ogm = GST_OGM_PARSE (GST_PAD_PARENT (pad));
  GstBuffer *buf = GST_BUFFER (buffer);
  guint8 *data = GST_BUFFER_DATA (buf);
  guint size = GST_BUFFER_SIZE (buf);

  GST_DEBUG_OBJECT (ogm, "New packet with packet start code 0x%02x", data[0]);

  switch (data[0]) {
    case 0x01:{
      GstCaps *caps = NULL;

      /* stream header */
      if (size < sizeof (stream_header) + 1) {
        GST_ELEMENT_ERROR (ogm, STREAM, WRONG_TYPE,
            ("Buffer too small"), (NULL));
        break;
      }

      if (!memcmp (&data[1], "video\000\000\000", 8)) {
        ogm->hdr.s.video.width = GST_READ_UINT32_LE (&data[45]);
        ogm->hdr.s.video.height = GST_READ_UINT32_LE (&data[49]);
      } else if (!memcmp (&data[1], "audio\000\000\000", 8)) {
        ogm->hdr.s.audio.channels = GST_READ_UINT32_LE (&data[45]);
        ogm->hdr.s.audio.blockalign = GST_READ_UINT32_LE (&data[47]);
        ogm->hdr.s.audio.avgbytespersec = GST_READ_UINT32_LE (&data[49]);
      } else if (!memcmp (&data[1], "text\000\000\000\000", 8)) {
        /* nothing here */
      } else {
        GST_ELEMENT_ERROR (ogm, STREAM, WRONG_TYPE,
            ("Unknown stream type"), (NULL));
        break;
      }
      memcpy (ogm->hdr.streamtype, &data[1], 8);
      memcpy (ogm->hdr.subtype, &data[9], 4);
      ogm->hdr.size = GST_READ_UINT32_LE (&data[13]);
      ogm->hdr.time_unit = GST_READ_UINT64_LE (&data[17]);
      ogm->hdr.samples_per_unit = GST_READ_UINT64_LE (&data[25]);
      ogm->hdr.default_len = GST_READ_UINT32_LE (&data[33]);
      ogm->hdr.buffersize = GST_READ_UINT32_LE (&data[37]);
      ogm->hdr.bits_per_sample = GST_READ_UINT32_LE (&data[41]);

      switch (ogm->hdr.streamtype[0]) {
        case 'a':{
          guint codec_id;

          if (!sscanf (ogm->hdr.subtype, "%04x", &codec_id)) {
            caps = NULL;
            break;
          }
          caps = gst_riff_create_audio_caps (codec_id,
              NULL, NULL, NULL, NULL, NULL);
          gst_caps_set_simple (caps,
              "channels", G_TYPE_INT, ogm->hdr.s.audio.channels,
              "rate", G_TYPE_INT, ogm->hdr.samples_per_unit, NULL);
          GST_LOG_OBJECT (ogm, "Type: %s, subtype: 0x%04x, "
              "channels: %d, samplerate: %d, blockalign: %d, bps: %d",
              ogm->hdr.streamtype, codec_id, ogm->hdr.s.audio.channels,
              ogm->hdr.samples_per_unit,
              ogm->hdr.s.audio.blockalign, ogm->hdr.s.audio.avgbytespersec);
          break;
        }
        case 'v':{
          guint32 fcc;

          fcc = GST_MAKE_FOURCC (ogm->hdr.subtype[0],
              ogm->hdr.subtype[1], ogm->hdr.subtype[2], ogm->hdr.subtype[3]);
          GST_LOG_OBJECT (ogm, "Type: %s, subtype: %" GST_FOURCC_FORMAT
              ", size: %dx%d, timeunit: %" G_GINT64_FORMAT
              " (fps: %lf), s/u: %" G_GINT64_FORMAT ", "
              "def.len: %d, bufsize: %d, bps: %d",
              ogm->hdr.streamtype, GST_FOURCC_ARGS (fcc),
              ogm->hdr.s.video.width, ogm->hdr.s.video.height,
              ogm->hdr.time_unit, 10000000. / ogm->hdr.time_unit,
              ogm->hdr.samples_per_unit, ogm->hdr.default_len,
              ogm->hdr.buffersize, ogm->hdr.bits_per_sample);
          caps = gst_riff_create_video_caps (fcc, NULL, NULL, NULL, NULL, NULL);
          gst_caps_set_simple (caps,
              "width", G_TYPE_INT, ogm->hdr.s.video.width,
              "height", G_TYPE_INT, ogm->hdr.s.video.height,
              "framerate", GST_TYPE_FRACTION, 10000000, ogm->hdr.time_unit,
              NULL);
          break;
        }
        case 't':
          GST_LOG_OBJECT (ogm, "Type: %s, s/u: %" G_GINT64_FORMAT
              ", timeunit=%" G_GINT64_FORMAT,
              ogm->hdr.streamtype, ogm->hdr.samples_per_unit,
              ogm->hdr.time_unit);
          caps = gst_caps_new_simple ("text/plain", NULL);
          break;
        default:
          g_assert_not_reached ();
      }

      if (ogm->srcpad) {
        GstCaps *current_caps = GST_PAD_CAPS (ogm->srcpad);

        if (current_caps && !gst_caps_is_equal (current_caps, caps)) {
          GST_WARNING_OBJECT (ogm, "Already an existing pad %s:%s",
              GST_DEBUG_PAD_NAME (ogm->srcpad));
          gst_element_remove_pad (GST_ELEMENT (ogm), ogm->srcpad);
          ogm->srcpad = NULL;
        } else {
          GST_DEBUG_OBJECT (ogm, "Existing pad has the same caps, do nothing");
        }
      }
      if (ogm->srcpad == NULL) {
        if (caps) {
          ogm->srcpad = gst_pad_new ("src", GST_PAD_SRC);
          gst_pad_set_caps (ogm->srcpad, caps);
        } else {
          GST_WARNING_OBJECT (ogm,
              "No fixed caps were found, carrying on with template");
          ogm->srcpad = gst_pad_new_from_template (ogm->srcpadtempl, "src");
        }
        gst_element_add_pad (GST_ELEMENT (ogm), ogm->srcpad);
      }
      break;
    }
    case 0x03:
      /* comment - unused */
      break;
    default:
      if ((data[0] & 0x01) == 0) {
        /* data - push on */
        guint len = ((data[0] & 0xc0) >> 6) | ((data[0] & 0x02) << 1);
        guint xsize = 0, n;
        GstBuffer *sbuf;
        gboolean keyframe = (data[0] & 0x08) >> 3;

        if (size < len + 1) {
          GST_ELEMENT_ERROR (ogm, STREAM, WRONG_TYPE,
              ("Buffer too small"), (NULL));
          break;
        }
        for (n = len; n > 0; n--) {
          xsize = (xsize << 8) | data[n];
        }

        GST_DEBUG_OBJECT (ogm,
            "[0x%02x] samples: %d, hdrbytes: %d, datasize: %d",
            data[0], xsize, len, size - len - 1);
        sbuf = gst_buffer_create_sub (buf, len + 1, size - len - 1);
        if (GST_BUFFER_OFFSET_END_IS_VALID (buf)) {
          ogm->next_granulepos = GST_BUFFER_OFFSET_END (buf);
        }
        switch (ogm->hdr.streamtype[0]) {
          case 't':
          case 'v':{
            gint samples = (ogm->hdr.streamtype[0] == 'v') ? 1 : xsize;

            if (!keyframe)
              GST_BUFFER_FLAG_SET (sbuf, GST_BUFFER_FLAG_DELTA_UNIT);

            GST_BUFFER_TIMESTAMP (sbuf) = (GST_SECOND / 10000000) *
                ogm->next_granulepos * ogm->hdr.time_unit;
            GST_BUFFER_DURATION (sbuf) = (GST_SECOND / 10000000) *
                ogm->hdr.time_unit * samples;
            ogm->next_granulepos += samples;
            break;
          }
          case 'a':
            GST_BUFFER_TIMESTAMP (sbuf) = GST_SECOND *
                ogm->next_granulepos / ogm->hdr.samples_per_unit;
            GST_BUFFER_DURATION (sbuf) = GST_SECOND * xsize /
                ogm->hdr.samples_per_unit;
            ogm->next_granulepos += xsize;
            break;
          default:
            gst_buffer_unref (sbuf);
            sbuf = NULL;
            GST_ELEMENT_ERROR (ogm, RESOURCE, SYNC, (NULL), (NULL));
            break;
        }
        gst_buffer_set_caps (sbuf, GST_PAD_CAPS (ogm->srcpad));
        ret = gst_pad_push (ogm->srcpad, sbuf);
        if (ret != GST_FLOW_OK) {
          GST_DEBUG_OBJECT (ogm, "Flow return: %s", gst_flow_get_name (ret));
        }
      } else {
        GST_ELEMENT_ERROR (ogm, STREAM, WRONG_TYPE,
            ("Wrong packet startcode 0x%02x", data[0]), (NULL));
      }
      break;
  }

  gst_buffer_unref (buf);

  return ret;
}

static GstStateChangeReturn
gst_ogm_parse_change_state (GstElement * element, GstStateChange transition)
{
  GstOgmParse *ogm = GST_OGM_PARSE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (ogm->srcpad) {
        gst_element_remove_pad (element, ogm->srcpad);
        ogm->srcpad = NULL;
      }
      memset (&ogm->hdr, 0, sizeof (ogm->hdr));
      ogm->next_granulepos = 0;
      break;
    default:
      break;
  }

  return parent_class->change_state (element, transition);
}

gboolean
gst_ogm_parse_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_ogm_parse_debug, "ogmparse", 0, "ogm parser");

  return gst_element_register (plugin, "ogmaudioparse", GST_RANK_PRIMARY,
      GST_TYPE_OGM_AUDIO_PARSE) &&
      gst_element_register (plugin, "ogmvideoparse", GST_RANK_PRIMARY,
      GST_TYPE_OGM_VIDEO_PARSE) &&
      gst_element_register (plugin, "ogmtextparse", GST_RANK_PRIMARY,
      GST_TYPE_OGM_TEXT_PARSE);
}
