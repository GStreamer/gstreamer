/* GStreamer
 * Copyright (C) 2010 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2010 Robert Swain <robert.swain@collabora.co.uk>
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
 * SECTION:element-interlace
 *
 * The interlace element takes a non-interlaced raw video stream as input,
 * creates fields out of each frame, then combines fields into interlaced
 * frames to output as an interlaced video stream. It can also produce
 * telecined streams from progressive input.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc pattern=ball ! interlace ! xvimagesink
 * ]|
 * This pipeline illustrates the combing effects caused by displaying
 * two interlaced fields as one progressive frame.
 * |[
 * gst-launch -v filesrc location=/path/to/file ! decodebin ! videorate !
 *   videoscale ! video/x-raw-yuv,format=\(fourcc\)I420,width=720,height=480,
 *   framerate=60000/1001,pixel-aspect-ratio=11/10 ! 
 *   interlace top-field-first=false ! ...
 * ]|
 * This pipeline converts a progressive video stream into an interlaced
 * stream suitable for standard definition NTSC.
 * |[
 * gst-launch -v videotestsrc pattern=ball ! video/x-raw-yuv,
 *   format=\(fourcc\)I420,width=720,height=480,framerate=24000/1001,
 *   pixel-aspect-ratio=11/10 ! interlace mode=telecine telecine-pattern=2:3 !
 *   ...
 * ]|
 * This pipeline converts a 24 frames per second progressive film stream into a
 * 30000/1001 2:3:2:3... pattern telecined stream suitable for displaying film
 * content on NTSC.
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY (gst_interlace_debug);
#define GST_CAT_DEFAULT gst_interlace_debug

#define GST_TYPE_INTERLACE \
  (gst_interlace_get_type())
#define GST_INTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_INTERLACE,GstInterlace))
#define GST_INTERLACE_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_INTERLACE,GstInterlaceClass))
#define GST_IS_GST_INTERLACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_INTERLACE))
#define GST_IS_GST_INTERLACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_INTERLACE))

typedef struct _GstInterlace GstInterlace;
typedef struct _GstInterlaceClass GstInterlaceClass;

struct _GstInterlace
{
  GstElement element;

  GstPad *srcpad;
  GstPad *sinkpad;
  GstCaps *srccaps;

  /* properties */
  gboolean top_field_first;
  gint mode;

  /* state */
  int width;
  int height;
  GstVideoFormat format;
  GstBuffer *stored_frame;
  gint stored_fields_pushed;
  gint telecine_pattern;
  gint phase_index;
  GstClockTime buffer_ts;
};

struct _GstInterlaceClass
{
  GstElementClass element_class;
};

enum
{
  PROP_0,
  PROP_TOP_FIELD_FIRST,
  PROP_MODE,
  PROP_TELECINE_PATTERN
};

typedef enum
{
  GST_INTERLACE_MODE_INTERLACE,
  GST_INTERLACE_MODE_TELECINE
} GstInterlaceMode;

#define GST_INTERLACE_MODE (gst_interlace_mode_get_type ())
static GType
gst_interlace_mode_get_type (void)
{
  static GType interlace_mode_type = 0;
  static const GEnumValue mode_types[] = {
    {GST_INTERLACE_MODE_INTERLACE, "Interlace", "interlace"},
    {GST_INTERLACE_MODE_TELECINE, "Telecine", "telecine"},
    {0, NULL, NULL}
  };

  if (!interlace_mode_type) {
    interlace_mode_type =
        g_enum_register_static ("GstInterlaceMode", mode_types);
  }

  return interlace_mode_type;
}

typedef enum
{
  GST_INTERLACE_PATTERN_2_3,
  GST_INTERLACE_PATTERN_2_3_3_2,
  GST_INTERLACE_PATTERN_EURO
} GstInterlacePattern;

#define GST_INTERLACE_PATTERN (gst_interlace_pattern_get_type ())
static GType
gst_interlace_pattern_get_type (void)
{
  static GType interlace_pattern_type = 0;
  static const GEnumValue pattern_types[] = {
    {GST_INTERLACE_PATTERN_2_3, "2:3", "2:3"},
    {GST_INTERLACE_PATTERN_2_3_3_2, "2:3:3:2", "2:3:3:2"},
    {GST_INTERLACE_PATTERN_EURO, "Euro 2-11:3", "2-11:3"},
    {0, NULL, NULL}
  };

  if (!interlace_pattern_type) {
    interlace_pattern_type =
        g_enum_register_static ("GstInterlacePattern", pattern_types);
  }

  return interlace_pattern_type;
}

static GstStaticPadTemplate gst_interlace_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{AYUV,YUY2,UYVY,I420,YV12,Y42B,Y444,NV12,NV21}")
        ",interlaced=TRUE")
    );

static GstStaticPadTemplate gst_interlace_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV
        ("{AYUV,YUY2,UYVY,I420,YV12,Y42B,Y444,NV12,NV21}")
        ",interlaced=FALSE")
    );

static void gst_interlace_base_init (gpointer g_class);
static void gst_interlace_class_init (GstInterlaceClass * klass);
static void gst_interlace_init (GstInterlace * interlace);
static gboolean gst_interlace_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_interlace_chain (GstPad * pad, GstBuffer * buffer);

static void gst_interlace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_interlace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_interlace_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_interlace_getcaps (GstPad * pad);
static GstStateChangeReturn gst_interlace_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;


static GType
gst_interlace_get_type (void)
{
  static GType interlace_type = 0;

  if (!interlace_type) {
    static const GTypeInfo interlace_info = {
      sizeof (GstInterlaceClass),
      gst_interlace_base_init,
      NULL,
      (GClassInitFunc) gst_interlace_class_init,
      NULL,
      NULL,
      sizeof (GstInterlace),
      0,
      (GInstanceInitFunc) gst_interlace_init,
    };

    interlace_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstInterlace", &interlace_info, 0);
  }

  return interlace_type;
}

static void
gst_interlace_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class,
      "Interlace filter", "Filter/Video",
      "Creates an interlaced video from progressive frames",
      "Entropy Wave <ds@entropywave.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_interlace_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_interlace_src_template));
}

static void
gst_interlace_class_init (GstInterlaceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_interlace_set_property;
  object_class->get_property = gst_interlace_get_property;

  element_class->change_state = gst_interlace_change_state;

  g_object_class_install_property (object_class, PROP_TOP_FIELD_FIRST,
      g_param_spec_boolean ("top-field-first", "top field first",
          "Interlaced stream should be top field first", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_MODE,
      g_param_spec_enum ("mode", "Mode", "Type of interlacing to be performed",
          GST_INTERLACE_MODE, GST_INTERLACE_MODE_INTERLACE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_TELECINE_PATTERN,
      g_param_spec_enum ("telecine-pattern", "Telecine pattern",
          "Pattern of fields to be used for telecine", GST_INTERLACE_PATTERN,
          GST_INTERLACE_PATTERN_2_3,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_interlace_reset (GstInterlace * interlace)
{
  interlace->phase_index = 0;
  interlace->buffer_ts = GST_CLOCK_TIME_NONE;
}

static void
gst_interlace_init (GstInterlace * interlace)
{
  GST_DEBUG ("gst_interlace_init");
  interlace->sinkpad =
      gst_pad_new_from_static_template (&gst_interlace_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (interlace), interlace->sinkpad);
  gst_pad_set_chain_function (interlace->sinkpad, gst_interlace_chain);
  gst_pad_set_setcaps_function (interlace->sinkpad, gst_interlace_setcaps);
  gst_pad_set_getcaps_function (interlace->sinkpad, gst_interlace_getcaps);
  gst_pad_set_event_function (interlace->sinkpad, gst_interlace_sink_event);

  interlace->srcpad =
      gst_pad_new_from_static_template (&gst_interlace_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (interlace), interlace->srcpad);
  gst_pad_set_setcaps_function (interlace->srcpad, gst_interlace_setcaps);
  gst_pad_set_getcaps_function (interlace->srcpad, gst_interlace_getcaps);

  interlace->top_field_first = FALSE;
  interlace->mode = GST_INTERLACE_MODE_INTERLACE;
  interlace->telecine_pattern = GST_INTERLACE_PATTERN_2_3;
  gst_interlace_reset (interlace);
}

typedef struct _PulldownFormat PulldownFormat;
struct _PulldownFormat
{
  const gchar *name;
  int n_fields[13];
  int dur_n, dur_d;
};

static const PulldownFormat formats[] = {
  /* 24p -> 30fps telecine */
  {"2:3", {2, 3,}, 1001, 30000},
  {"2:3:3:2", {2, 3, 3, 2,}, 1001, 30000},
  /* 24p -> 50i Euro pulldown */
  {"2-11:3", {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,}, 1, 25}
};

static void
gst_interlace_telecine_decorate_buffer (GstInterlace * interlace,
    GstBuffer * buf, const PulldownFormat * format)
{
  GST_BUFFER_TIMESTAMP (buf) = interlace->buffer_ts;
  GST_BUFFER_DURATION (buf) =
      gst_util_uint64_scale (GST_SECOND, format->dur_n, format->dur_d);
  /* increment the buffer timestamp by duration for the next buffer */
  interlace->buffer_ts +=
      gst_util_uint64_scale (GST_SECOND, format->dur_n, format->dur_d);
  gst_buffer_set_caps (buf, interlace->srccaps);

  if (interlace->top_field_first)
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_TFF);
}

static gboolean
gst_interlace_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstInterlace *interlace;

  interlace = GST_INTERLACE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (interlace, "handling FLUSH_START");
      if (interlace->stored_frame) {
        gst_buffer_unref (interlace->stored_frame);
        interlace->stored_frame = NULL;
      }
      ret = gst_pad_push_event (interlace->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (interlace, "handling FLUSH_STOP");
      gst_interlace_reset (interlace);
      ret = gst_pad_push_event (interlace->srcpad, event);
      break;
    case GST_EVENT_EOS:
      if (interlace->mode == GST_INTERLACE_MODE_TELECINE) {
        gint num_fields;
        const PulldownFormat *format = &formats[interlace->telecine_pattern];

        num_fields =
            format->n_fields[interlace->phase_index] -
            interlace->stored_fields_pushed;
        interlace->stored_fields_pushed = 0;

        /* on EOS we want to push as many sane frames as are left */
        while (num_fields > 1) {
          GstBuffer *output_buffer;

          /* make metadata writable before editing it */
          interlace->stored_frame =
              gst_buffer_make_metadata_writable (interlace->stored_frame);
          num_fields -= 2;

          gst_interlace_telecine_decorate_buffer (interlace,
              interlace->stored_frame, format);

          /* ref output_buffer/stored frame because we want to keep it for now
           * and pushing gives away a ref */
          output_buffer = gst_buffer_ref (interlace->stored_frame);
          if (gst_pad_push (interlace->srcpad, output_buffer)) {
            GST_DEBUG_OBJECT (interlace, "Failed to push buffer %p",
                output_buffer);
            return FALSE;
          }
          output_buffer = NULL;

          if (num_fields <= 1) {
            gst_buffer_unref (interlace->stored_frame);
            interlace->stored_frame = NULL;
            break;
          }
        }

        /* increment the phase index */
        interlace->phase_index++;
        if (!format->n_fields[interlace->phase_index]) {
          interlace->phase_index = 0;
        }
      }

      ret = gst_pad_push_event (interlace->srcpad, event);
      break;
    default:
      ret = gst_pad_push_event (interlace->srcpad, event);
      break;
  }

  g_object_unref (interlace);

  return ret;
}

static GstCaps *
gst_interlace_getcaps (GstPad * pad)
{
  GstInterlace *interlace;
  GstPad *otherpad;
  GstCaps *othercaps;
  GstCaps *icaps;

  interlace = GST_INTERLACE (gst_pad_get_parent (pad));

  otherpad =
      (pad == interlace->srcpad) ? interlace->sinkpad : interlace->srcpad;

  othercaps = gst_pad_peer_get_caps (otherpad);
  if (othercaps == NULL) {
    icaps = gst_caps_copy (gst_pad_get_pad_template_caps (otherpad));
  } else {
    icaps = gst_caps_intersect (othercaps,
        gst_pad_get_pad_template_caps (otherpad));
  }

  gst_caps_set_simple (icaps, "interlaced", G_TYPE_BOOLEAN,
      pad == interlace->srcpad ? TRUE : FALSE, NULL);

  return icaps;
}

static gboolean
gst_interlace_setcaps (GstPad * pad, GstCaps * caps)
{
  GstInterlace *interlace;
  gboolean ret;
  int width, height;
  GstVideoFormat format;
  gboolean interlaced = TRUE;
  int fps_n, fps_d;
  GstPad *otherpad;
  GstCaps *othercaps;
  GString *method = NULL;
  const PulldownFormat *pdformat;

  interlace = GST_INTERLACE (gst_pad_get_parent (pad));

  otherpad =
      (pad == interlace->srcpad) ? interlace->sinkpad : interlace->srcpad;

  ret = gst_video_format_parse_caps (caps, &format, &width, &height);
  gst_video_format_parse_caps_interlaced (caps, &interlaced);
  ret &= gst_video_parse_caps_framerate (caps, &fps_n, &fps_d);

  if (!ret)
    goto error;

  othercaps = gst_caps_copy (caps);

  if (interlace->mode == GST_INTERLACE_MODE_TELECINE) {
    pdformat = &formats[interlace->telecine_pattern];
    method = g_string_new ("telecine-");
    g_string_append (method, pdformat->name);
  }

  if (pad == interlace->srcpad) {
    gst_caps_set_simple (othercaps, "interlaced", G_TYPE_BOOLEAN, FALSE, NULL);
    if (interlace->mode == GST_INTERLACE_MODE_INTERLACE) {
      gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION, fps_n * 2,
          fps_d, NULL);
    } else {
      gst_caps_set_simple (caps, "framerate", GST_TYPE_FRACTION,
          pdformat->dur_d, pdformat->dur_n, "interlacing-method", G_TYPE_STRING,
          method->str, NULL);
    }
  } else {
    gst_caps_set_simple (othercaps, "interlaced", G_TYPE_BOOLEAN, TRUE, NULL);
    if (interlace->mode == GST_INTERLACE_MODE_INTERLACE) {
      gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION, fps_n,
          fps_d * 2, NULL);
    } else {
      gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION,
          pdformat->dur_d, pdformat->dur_n, "interlacing-method", G_TYPE_STRING,
          method->str, NULL);
    }
  }

  ret = gst_pad_set_caps (otherpad, othercaps);
  if (!ret)
    goto error;

  interlace->format = format;
  interlace->width = width;
  interlace->height = height;

  if (pad == interlace->sinkpad) {
    interlace->srccaps = gst_caps_ref (othercaps);
  } else {
    interlace->srccaps = gst_caps_ref (caps);
  }

error:
  g_object_unref (interlace);

  return ret;
}

static void
copy_field (GstInterlace * interlace, GstBuffer * d, GstBuffer * s,
    int field_index)
{
  int j;
  guint8 *dest;
  guint8 *src;
  int width = interlace->width;
  int height = interlace->height;

  if (interlace->format == GST_VIDEO_FORMAT_I420 ||
      interlace->format == GST_VIDEO_FORMAT_YV12) {
    /* planar 4:2:0 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width;
      src = GST_BUFFER_DATA (s) + j * width;
      memcpy (dest, src, width);
    }
    for (j = field_index; j < height / 2; j += 2) {
      dest = GST_BUFFER_DATA (d) + width * height + j * width / 2;
      src = GST_BUFFER_DATA (s) + width * height + j * width / 2;
      memcpy (dest, src, width / 2);
    }
    for (j = field_index; j < height / 2; j += 2) {
      dest =
          GST_BUFFER_DATA (d) + width * height + width / 2 * height / 2 +
          j * width / 2;
      src =
          GST_BUFFER_DATA (s) + width * height + width / 2 * height / 2 +
          j * width / 2;
      memcpy (dest, src, width / 2);
    }
  } else if (interlace->format == GST_VIDEO_FORMAT_UYVY ||
      interlace->format == GST_VIDEO_FORMAT_YUY2) {
    /* packed 4:2:2 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width * 2;
      src = GST_BUFFER_DATA (s) + j * width * 2;
      memcpy (dest, src, width * 2);
    }
  } else if (interlace->format == GST_VIDEO_FORMAT_AYUV) {
    /* packed 4:4:4 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width * 4;
      src = GST_BUFFER_DATA (s) + j * width * 4;
      memcpy (dest, src, width * 4);
    }
  } else if (interlace->format == GST_VIDEO_FORMAT_Y42B) {
    /* planar 4:2:2 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width;
      src = GST_BUFFER_DATA (s) + j * width;
      memcpy (dest, src, width);
    }
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + width * height + j * width / 2;
      src = GST_BUFFER_DATA (s) + width * height + j * width / 2;
      memcpy (dest, src, width / 2);
    }
    for (j = field_index; j < height; j += 2) {
      dest =
          GST_BUFFER_DATA (d) + width * height + width / 2 * height +
          j * width / 2;
      src =
          GST_BUFFER_DATA (s) + width * height + width / 2 * height +
          j * width / 2;
      memcpy (dest, src, width / 2);
    }
  } else if (interlace->format == GST_VIDEO_FORMAT_Y444) {
    /* planar 4:4:4 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width;
      src = GST_BUFFER_DATA (s) + j * width;
      memcpy (dest, src, width);
    }
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + width * height + j * width;
      src = GST_BUFFER_DATA (s) + width * height + j * width;
      memcpy (dest, src, width);
    }
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + width * height + width * height + j * width;
      src = GST_BUFFER_DATA (s) + width * height + width * height + j * width;
      memcpy (dest, src, width);
    }
  } else if (interlace->format == GST_VIDEO_FORMAT_NV12 ||
      interlace->format == GST_VIDEO_FORMAT_NV21) {
    /* planar/packed 4:2:0 */
    for (j = field_index; j < height; j += 2) {
      dest = GST_BUFFER_DATA (d) + j * width;
      src = GST_BUFFER_DATA (s) + j * width;
      memcpy (dest, src, width);
    }
    for (j = field_index; j < height / 2; j += 2) {
      dest = GST_BUFFER_DATA (d) + width * height + j * width;
      src = GST_BUFFER_DATA (s) + width * height + j * width;
      memcpy (dest, src, width);
    }
  } else {
    g_assert_not_reached ();
  }
}


static GstFlowReturn
gst_interlace_chain (GstPad * pad, GstBuffer * buffer)
{
  GstInterlace *interlace = GST_INTERLACE (gst_pad_get_parent (pad));
  GstFlowReturn ret;
  gint num_fields = 0;

  GST_DEBUG ("Received buffer at %u:%02u:%02u:%09u",
      (guint) (GST_BUFFER_TIMESTAMP (buffer) / (GST_SECOND * 60 * 60)),
      (guint) ((GST_BUFFER_TIMESTAMP (buffer) / (GST_SECOND * 60)) % 60),
      (guint) ((GST_BUFFER_TIMESTAMP (buffer) / GST_SECOND) % 60),
      (guint) (GST_BUFFER_TIMESTAMP (buffer) % GST_SECOND));

  GST_DEBUG ("duration %" GST_TIME_FORMAT " flags %04x %s %s %s",
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_BUFFER_FLAGS (buffer),
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_TFF) ? "tff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_RFF) ? "rff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_ONEFIELD) ? "onefield" :
      "");

  if (GST_BUFFER_FLAGS (buffer) & GST_BUFFER_FLAG_DISCONT) {
    GST_DEBUG ("discont");
  }

  if (interlace->buffer_ts == GST_CLOCK_TIME_NONE) {
    /* get the initial ts */
    interlace->buffer_ts = GST_BUFFER_TIMESTAMP (buffer);
  }

  if (interlace->stored_frame == NULL) {
    interlace->stored_frame = buffer;
    ret = GST_FLOW_OK;
  } else {
    switch (interlace->mode) {
      case GST_INTERLACE_MODE_INTERLACE:
      {
        GstBuffer *output_buffer;

        output_buffer = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buffer));

        copy_field (interlace, output_buffer, interlace->stored_frame,
            !interlace->top_field_first);
        copy_field (interlace, output_buffer, buffer,
            interlace->top_field_first);

        GST_BUFFER_TIMESTAMP (output_buffer) =
            GST_BUFFER_TIMESTAMP (interlace->stored_frame);
        GST_BUFFER_DURATION (output_buffer) =
            GST_BUFFER_DURATION (interlace->stored_frame) +
            GST_BUFFER_DURATION (buffer);
        gst_buffer_set_caps (output_buffer, interlace->srccaps);

        if (interlace->top_field_first) {
          GST_BUFFER_FLAG_SET (output_buffer, GST_VIDEO_BUFFER_TFF);
        }

        ret = gst_pad_push (interlace->srcpad, output_buffer);

        gst_buffer_unref (buffer);
        gst_buffer_unref (interlace->stored_frame);
        interlace->stored_frame = NULL;
        break;
      }
      case GST_INTERLACE_MODE_TELECINE:
      {
        const PulldownFormat *format = &formats[interlace->telecine_pattern];

        num_fields =
            format->n_fields[interlace->phase_index] -
            interlace->stored_fields_pushed;
        interlace->stored_fields_pushed = 0;

        while (num_fields > 0) {
          GstBuffer *output_buffer;

          if (num_fields > 1) {
            /* make metadata writable before editing it */
            interlace->stored_frame =
                gst_buffer_make_metadata_writable (interlace->stored_frame);
            num_fields -= 2;
            gst_interlace_telecine_decorate_buffer (interlace,
                interlace->stored_frame, format);
            /* ref output_buffer/stored frame because we want to keep it for now
             * and pushing gives away a ref */
            output_buffer = gst_buffer_ref (interlace->stored_frame);
          } else {
            output_buffer = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buffer));

            /* take the top field from the stored frame */
            copy_field (interlace, output_buffer, interlace->stored_frame, 0);
            /* take the bottom field from the incoming buffer */
            copy_field (interlace, output_buffer, buffer, 1);
            /* one field pushed from the stored frame, one from the incoming
             * buffer that will become stored */
            num_fields--;
            interlace->stored_fields_pushed = 1;
            gst_interlace_telecine_decorate_buffer (interlace, output_buffer,
                format);
          }

          if ((ret = gst_pad_push (interlace->srcpad, output_buffer))) {
            GST_DEBUG_OBJECT (interlace, "Failed to push buffer %p",
                output_buffer);
            return ret;
          }
          output_buffer = NULL;

          if (!num_fields) {
            gst_buffer_unref (interlace->stored_frame);
            /* if we still need one field from the incoming buffer, store it */
            interlace->stored_frame = buffer;
            break;
          }
        }

        /* increment the phase index */
        interlace->phase_index++;
        if (!format->n_fields[interlace->phase_index]) {
          interlace->phase_index = 0;
        }
        break;
      }
      default:
        break;
    }
  }

  gst_object_unref (interlace);

  return ret;
}

static void
gst_interlace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstInterlace *interlace = GST_INTERLACE (object);

  switch (prop_id) {
    case PROP_TOP_FIELD_FIRST:
      interlace->top_field_first = g_value_get_boolean (value);
      break;
    case PROP_MODE:
      interlace->mode = g_value_get_enum (value);
      break;
    case PROP_TELECINE_PATTERN:
      interlace->telecine_pattern = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_interlace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstInterlace *interlace = GST_INTERLACE (object);

  switch (prop_id) {
    case PROP_TOP_FIELD_FIRST:
      g_value_set_boolean (value, interlace->top_field_first);
      break;
    case PROP_MODE:
      g_value_set_enum (value, interlace->mode);
      break;
    case PROP_TELECINE_PATTERN:
      g_value_set_enum (value, interlace->telecine_pattern);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_interlace_change_state (GstElement * element, GstStateChange transition)
{
  //GstInterlace *interlace = GST_INTERLACE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      //gst_interlace_reset (interlace);
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_interlace_debug, "interlace", 0,
      "interlace element");

  return gst_element_register (plugin, "interlace", GST_RANK_NONE,
      GST_TYPE_INTERLACE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "interlace",
    "Create an interlaced video stream",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
