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
 *   pixel-aspect-ratio=11/10 ! interlace pattern=2:3 !
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
  gint pattern;
  gboolean allow_rff;

  /* state */
  int width;
  int height;
  GstVideoFormat format;
  int src_fps_n;
  int src_fps_d;

  GstBuffer *stored_frame;
  gint stored_fields;
  gint phase_index;
  int field_index;              /* index of the next field to push, 0=top 1=bottom */
  GstClockTime timebase;
  int fields_since_timebase;
  guint pattern_offset;         /* initial offset into the pattern */
};

struct _GstInterlaceClass
{
  GstElementClass element_class;
};

enum
{
  PROP_0,
  PROP_TOP_FIELD_FIRST,
  PROP_PATTERN,
  PROP_PATTERN_OFFSET,
  PROP_ALLOW_RFF
};

typedef enum
{
  GST_INTERLACE_PATTERN_1_1,
  GST_INTERLACE_PATTERN_2_2,
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
    {GST_INTERLACE_PATTERN_1_1, "1:1", "1:1"},
    {GST_INTERLACE_PATTERN_2_2, "2:2", "2:2"},
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
static void gst_interlace_finalize (GObject * obj);

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
      "David Schleef <ds@schleef.org>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_interlace_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_interlace_src_template);
}

static void
gst_interlace_class_init (GstInterlaceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_interlace_set_property;
  object_class->get_property = gst_interlace_get_property;
  object_class->finalize = gst_interlace_finalize;

  element_class->change_state = gst_interlace_change_state;

  g_object_class_install_property (object_class, PROP_TOP_FIELD_FIRST,
      g_param_spec_boolean ("top-field-first", "top field first",
          "Interlaced stream should be top field first", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PATTERN,
      g_param_spec_enum ("field-pattern", "Field pattern",
          "The output field pattern", GST_INTERLACE_PATTERN,
          GST_INTERLACE_PATTERN_2_3,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_PATTERN_OFFSET,
      g_param_spec_uint ("pattern-offset", "Pattern offset",
          "The initial field pattern offset. Counts from 0.",
          0, 12, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_ALLOW_RFF,
      g_param_spec_boolean ("allow-rff", "Allow Repeat-First-Field flags",
          "Allow generation of buffers with RFF flag set, i.e., duration of 3 fields",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_interlace_finalize (GObject * obj)
{
  GstInterlace *interlace = GST_INTERLACE (obj);

  gst_caps_replace (&interlace->srccaps, NULL);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_interlace_reset (GstInterlace * interlace)
{
  interlace->phase_index = interlace->pattern_offset;
  interlace->timebase = GST_CLOCK_TIME_NONE;
  interlace->field_index = 0;
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
  interlace->allow_rff = FALSE;
  interlace->pattern = GST_INTERLACE_PATTERN_2_3;
  interlace->pattern_offset = 0;
  gst_interlace_reset (interlace);
}

typedef struct _PulldownFormat PulldownFormat;
struct _PulldownFormat
{
  const gchar *name;
  /* ratio between outgoing field rate / 2 and incoming frame rate.
   * I.e., 24p -> 60i is 1.25  */
  int ratio_n, ratio_d;
  int n_fields[13];
};

static const PulldownFormat formats[] = {
  /* 60p -> 60i or 50p -> 50i */
  {"1:1", 1, 2, {1}},
  /* 30p -> 60i or 25p -> 50i */
  {"2:2", 1, 1, {2}},
  /* 24p -> 60i telecine */
  {"2:3", 5, 4, {2, 3,}},
  {"2:3:3:2", 5, 4, {2, 3, 3, 2,}},
  /* 24p -> 50i Euro pulldown */
  {"2-11:3", 25, 24, {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,}}
};

static void
gst_interlace_decorate_buffer (GstInterlace * interlace, GstBuffer * buf,
    int n_fields)
{
  /* field duration = src_fps_d / (2 * src_fps_n) */
  if (interlace->src_fps_n == 0) {
    /* If we don't know the fps, we can't generate timestamps/durations */
    GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;
  } else {
    GST_BUFFER_TIMESTAMP (buf) = interlace->timebase +
        gst_util_uint64_scale (GST_SECOND,
        interlace->src_fps_d * interlace->fields_since_timebase,
        interlace->src_fps_n * 2);
    GST_BUFFER_DURATION (buf) =
        gst_util_uint64_scale (GST_SECOND, interlace->src_fps_d * n_fields,
        interlace->src_fps_n * 2);
  }
  /* increment the buffer timestamp by duration for the next buffer */
  gst_buffer_set_caps (buf, interlace->srccaps);

  if (interlace->field_index == 0) {
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_TFF);
  }
  if (n_fields == 3) {
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_RFF);
  }
  if (n_fields == 1) {
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_ONEFIELD);
  }
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
#if 0
      /* FIXME revive this when we output ONEFIELD and RFF buffers */
    {
      gint num_fields;
      const PulldownFormat *format = &formats[interlace->pattern];

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

        gst_interlace_decorate_buffer (interlace, interlace->stored_frame,
            n_fields);

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
#endif

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
    gst_caps_unref (othercaps);
  }

  gst_caps_set_simple (icaps, "interlaced", G_TYPE_BOOLEAN,
      pad == interlace->srcpad ? TRUE : FALSE, NULL);

  gst_object_unref (interlace);

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
  GstCaps *othercaps = NULL;
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
  pdformat = &formats[interlace->pattern];

  if (pad == interlace->srcpad) {
    gst_caps_set_simple (othercaps, "interlaced", G_TYPE_BOOLEAN, FALSE, NULL);
    gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION,
        fps_n * pdformat->ratio_d, fps_d * pdformat->ratio_n, NULL);
  } else {
    gst_caps_set_simple (othercaps, "interlaced", G_TYPE_BOOLEAN, TRUE, NULL);
    gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION,
        fps_n * pdformat->ratio_n, fps_d * pdformat->ratio_d, NULL);
  }

  ret = gst_pad_set_caps (otherpad, othercaps);
  if (!ret)
    goto error;

  interlace->format = format;
  interlace->width = width;
  interlace->height = height;

  interlace->phase_index = interlace->pattern_offset;

  if (pad == interlace->sinkpad) {
    gst_caps_replace (&interlace->srccaps, othercaps);
    interlace->src_fps_n = fps_n * pdformat->ratio_n;
    interlace->src_fps_d = fps_d * pdformat->ratio_d;
  } else {
    gst_caps_replace (&interlace->srccaps, caps);
    interlace->src_fps_n = fps_n;
    interlace->src_fps_d = fps_d;
  }

error:
  if (othercaps)
    gst_caps_unref (othercaps);
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
  GstFlowReturn ret = GST_FLOW_OK;
  gint num_fields = 0;
  int current_fields;
  const PulldownFormat *format;

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

    if (interlace->stored_frame) {
      gst_buffer_unref (interlace->stored_frame);
    }
    interlace->stored_frame = NULL;
    interlace->stored_fields = 0;

    if (interlace->top_field_first) {
      interlace->field_index = 0;
    } else {
      interlace->field_index = 1;
    }
  }

  if (interlace->timebase == GST_CLOCK_TIME_NONE) {
    /* get the initial ts */
    interlace->timebase = GST_BUFFER_TIMESTAMP (buffer);
  }

  format = &formats[interlace->pattern];

  if (interlace->stored_fields == 0
      && interlace->phase_index == interlace->pattern_offset
      && GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer))) {
    interlace->timebase = GST_BUFFER_TIMESTAMP (buffer);
    interlace->fields_since_timebase = 0;
  }

  if (!format->n_fields[interlace->phase_index]) {
    interlace->phase_index = 0;
  }

  current_fields = format->n_fields[interlace->phase_index];
  /* increment the phase index */
  interlace->phase_index++;
  GST_DEBUG ("incoming buffer assigned %d fields", current_fields);

  num_fields = interlace->stored_fields + current_fields;
  while (num_fields >= 2) {
    GstBuffer *output_buffer;
    int n_output_fields;

    GST_DEBUG ("have %d fields, %d current, %d stored",
        num_fields, current_fields, interlace->stored_fields);

    if (interlace->stored_fields > 0) {
      GST_DEBUG ("1 field from stored, 1 from current");

      output_buffer = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buffer));
      /* take the first field from the stored frame */
      copy_field (interlace, output_buffer, interlace->stored_frame,
          interlace->field_index);
      interlace->stored_fields--;
      /* take the second field from the incoming buffer */
      copy_field (interlace, output_buffer, buffer, interlace->field_index ^ 1);
      current_fields--;
      n_output_fields = 2;
    } else {
      output_buffer =
          gst_buffer_make_metadata_writable (gst_buffer_ref (buffer));
      if (num_fields >= 3 && interlace->allow_rff) {
        GST_DEBUG ("3 fields from current");
        /* take both fields from incoming buffer */
        current_fields -= 3;
        n_output_fields = 3;
      } else {
        GST_DEBUG ("2 fields from current");
        /* take both buffers from incoming buffer */
        current_fields -= 2;
        n_output_fields = 2;
      }
    }
    num_fields -= n_output_fields;

    gst_interlace_decorate_buffer (interlace, output_buffer, n_output_fields);
    interlace->fields_since_timebase += n_output_fields;
    interlace->field_index ^= (n_output_fields & 1);

    GST_DEBUG_OBJECT (interlace, "output timestamp %" GST_TIME_FORMAT
        " duration %" GST_TIME_FORMAT " flags %04x %s %s %s",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (output_buffer)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (output_buffer)),
        GST_BUFFER_FLAGS (output_buffer),
        (GST_BUFFER_FLAGS (output_buffer) & GST_VIDEO_BUFFER_TFF) ? "tff" : "",
        (GST_BUFFER_FLAGS (output_buffer) & GST_VIDEO_BUFFER_RFF) ? "rff" : "",
        (GST_BUFFER_FLAGS (output_buffer) & GST_VIDEO_BUFFER_ONEFIELD) ?
        "onefield" : "");

    ret = gst_pad_push (interlace->srcpad, output_buffer);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (interlace, "Failed to push buffer %p", output_buffer);
      break;
    }
  }

  GST_DEBUG ("done.  %d fields remaining", current_fields);

  if (interlace->stored_frame) {
    gst_buffer_unref (interlace->stored_frame);
    interlace->stored_frame = NULL;
    interlace->stored_fields = 0;
  }

  if (current_fields > 0) {
    interlace->stored_frame = buffer;
    interlace->stored_fields = current_fields;
  } else {
    gst_buffer_unref (buffer);
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
    case PROP_PATTERN:
      interlace->pattern = g_value_get_enum (value);
      break;
    case PROP_PATTERN_OFFSET:
      interlace->pattern_offset = g_value_get_uint (value);
      break;
    case PROP_ALLOW_RFF:
      interlace->allow_rff = g_value_get_boolean (value);
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
    case PROP_PATTERN:
      g_value_set_enum (value, interlace->pattern);
      break;
    case PROP_PATTERN_OFFSET:
      g_value_set_uint (value, interlace->pattern_offset);
      break;
    case PROP_ALLOW_RFF:
      g_value_set_boolean (value, interlace->allow_rff);
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
