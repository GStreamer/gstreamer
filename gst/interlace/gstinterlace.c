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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-interlace
 * @title: interlace
 *
 * The interlace element takes a non-interlaced raw video stream as input,
 * creates fields out of each frame, then combines fields into interlaced
 * frames to output as an interlaced video stream. It can also produce
 * telecined streams from progressive input.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc pattern=ball ! interlace ! xvimagesink
 * ]|
 * This pipeline illustrates the combing effects caused by displaying
 * two interlaced fields as one progressive frame.
 * |[
 * gst-launch-1.0 -v filesrc location=/path/to/file ! decodebin ! videorate !
 *   videoscale ! video/x-raw,format=\(string\)I420,width=720,height=480,
 *   framerate=60000/1001,pixel-aspect-ratio=11/10 !
 *   interlace top-field-first=false ! autovideosink
 * ]|
 * This pipeline converts a progressive video stream into an interlaced
 * stream suitable for standard definition NTSC.
 * |[
 * gst-launch-1.0 -v videotestsrc pattern=ball ! video/x-raw,
 *   format=\(string\)I420,width=720,height=480,framerate=24000/1001,
 *   pixel-aspect-ratio=11/10 ! interlace !
 *   autovideosink
 * ]|
 * This pipeline converts a 24 frames per second progressive film stream into a
 * 30000/1001 2:3:2:3... pattern telecined stream suitable for displaying film
 * content on NTSC.
 *
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

  /* properties */
  gboolean top_field_first;
  gint pattern;
  gboolean allow_rff;

  /* state */
  GstVideoInfo info;
  GstVideoInfo out_info;
  int src_fps_n;
  int src_fps_d;

  GMutex lock;
  gint new_pattern;
  GstBuffer *stored_frame;
  guint stored_fields;
  guint phase_index;
  guint field_index;            /* index of the next field to push, 0=top 1=bottom */
  GstClockTime timebase;
  guint fields_since_timebase;
  guint pattern_offset;         /* initial offset into the pattern */
  gboolean passthrough;
  gboolean switch_fields;
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
  GST_INTERLACE_PATTERN_EURO,
  GST_INTERLACE_PATTERN_3_4R3,
  GST_INTERLACE_PATTERN_3R7_4,
  GST_INTERLACE_PATTERN_3_3_4,
  GST_INTERLACE_PATTERN_3_3,
  GST_INTERLACE_PATTERN_3_2R4,
  GST_INTERLACE_PATTERN_1_2R4,
} GstInterlacePattern;

#define GST_INTERLACE_PATTERN (gst_interlace_pattern_get_type ())
static GType
gst_interlace_pattern_get_type (void)
{
  static GType interlace_pattern_type = 0;
  static const GEnumValue pattern_types[] = {
    {GST_INTERLACE_PATTERN_1_1, "1:1 (e.g. 60p -> 60i)", "1:1"},
    {GST_INTERLACE_PATTERN_2_2, "2:2 (e.g. 30p -> 60i)", "2:2"},
    {GST_INTERLACE_PATTERN_2_3, "2:3 (e.g. 24p -> 60i telecine)", "2:3"},
    {GST_INTERLACE_PATTERN_2_3_3_2, "2:3:3:2 (e.g. 24p -> 60i telecine)",
        "2:3:3:2"},
    {GST_INTERLACE_PATTERN_EURO, "Euro 2-11:3 (e.g. 24p -> 50i telecine)",
        "2-11:3"},
    {GST_INTERLACE_PATTERN_3_4R3, "3:4-3 (e.g. 16p -> 60i telecine)", "3:4-3"},
    {GST_INTERLACE_PATTERN_3R7_4, "3-7:4 (e.g. 16p -> 50i telecine)", "3-7:4"},
    {GST_INTERLACE_PATTERN_3_3_4, "3:3:4 (e.g. 18p -> 60i telecine)", "3:3:4"},
    {GST_INTERLACE_PATTERN_3_3, "3:3 (e.g. 20p -> 60i telecine)", "3:3"},
    {GST_INTERLACE_PATTERN_3_2R4, "3:2-4 (e.g. 27.5p -> 60i telecine)",
        "3:2-4"},
    {GST_INTERLACE_PATTERN_1_2R4, "1:2-4 (e.g. 27.5p -> 50i telecine)",
        "1:2-4"},
    {0, NULL, NULL}
  };

  if (!interlace_pattern_type) {
    interlace_pattern_type =
        g_enum_register_static ("GstInterlacePattern", pattern_types);
  }

  return interlace_pattern_type;
}

#define VIDEO_FORMATS "{AYUV,YUY2,UYVY,I420,YV12,Y42B,Y444,NV12,NV21}"

static GstStaticPadTemplate gst_interlace_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS)
        ",interlace-mode={interleaved,mixed} ;"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_FORMAT_INTERLACED,
            VIDEO_FORMATS)
        ",interlace-mode=alternate")
    );

static GstStaticPadTemplate gst_interlace_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE
        ("{AYUV,YUY2,UYVY,I420,YV12,Y42B,Y444,NV12,NV21}")
    )
    );

GType gst_interlace_get_type (void);
static void gst_interlace_finalize (GObject * obj);

static void gst_interlace_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_interlace_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_interlace_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_interlace_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstFlowReturn gst_interlace_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);

static gboolean gst_interlace_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstStateChangeReturn gst_interlace_change_state (GstElement * element,
    GstStateChange transition);

static GstCaps *gst_interlace_caps_double_framerate (GstCaps * caps,
    gboolean half, gboolean skip_progressive);

#define gst_interlace_parent_class parent_class
G_DEFINE_TYPE (GstInterlace, gst_interlace, GST_TYPE_ELEMENT);

static void
gst_interlace_class_init (GstInterlaceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  object_class->set_property = gst_interlace_set_property;
  object_class->get_property = gst_interlace_get_property;
  object_class->finalize = gst_interlace_finalize;

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

  gst_element_class_set_static_metadata (element_class,
      "Interlace filter", "Filter/Video",
      "Creates an interlaced video from progressive frames",
      "David Schleef <ds@schleef.org>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_interlace_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_interlace_src_template);

  element_class->change_state = gst_interlace_change_state;

  gst_type_mark_as_plugin_api (GST_INTERLACE_PATTERN, 0);
}

static void
gst_interlace_finalize (GObject * obj)
{
  GstInterlace *interlace = GST_INTERLACE (obj);
  g_mutex_clear (&interlace->lock);
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_interlace_reset (GstInterlace * interlace)
{
  interlace->phase_index = interlace->pattern_offset;
  interlace->timebase = GST_CLOCK_TIME_NONE;
  interlace->field_index = 0;
  interlace->passthrough = FALSE;
  interlace->switch_fields = FALSE;
  if (interlace->stored_frame) {
    gst_buffer_unref (interlace->stored_frame);
    interlace->stored_frame = NULL;
  }
}

static void
gst_interlace_init (GstInterlace * interlace)
{
  GST_DEBUG ("gst_interlace_init");
  interlace->sinkpad =
      gst_pad_new_from_static_template (&gst_interlace_sink_template, "sink");
  gst_pad_set_chain_function (interlace->sinkpad, gst_interlace_chain);
  gst_pad_set_event_function (interlace->sinkpad, gst_interlace_sink_event);
  gst_pad_set_query_function (interlace->sinkpad, gst_interlace_sink_query);
  gst_element_add_pad (GST_ELEMENT (interlace), interlace->sinkpad);

  interlace->srcpad =
      gst_pad_new_from_static_template (&gst_interlace_src_template, "src");
  gst_pad_set_query_function (interlace->srcpad, gst_interlace_src_query);
  gst_element_add_pad (GST_ELEMENT (interlace), interlace->srcpad);

  interlace->top_field_first = FALSE;
  interlace->allow_rff = FALSE;
  interlace->pattern = GST_INTERLACE_PATTERN_2_3;
  interlace->new_pattern = GST_INTERLACE_PATTERN_2_3;
  interlace->pattern_offset = 0;
  interlace->src_fps_n = 0;
  g_mutex_init (&interlace->lock);
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
  {"2-11:3", 25, 24, {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3,}},
  /* 16p (16000/1001) -> 60i (NTSC 30000/1001) */
  {"3:4-3", 15, 8, {3, 4, 4, 4,}},
  /* 16p -> 50i (PAL) */
  {"3-7:4", 25, 16, {3, 3, 3, 3, 3, 3, 3, 4,}},
  /* 18p to NTSC 60i */
  {"3:3:4", 5, 3, {3, 3, 4,}},
  /* 20p to NTSC 60i */
  {"3:3", 3, 2, {3, 3,}},
  /* 27.5 to NTSC 60i */
  {"3:2-4", 11, 10, {3, 2, 2, 2, 2,}},
  /* 27.5 to PAL 50i */
  {"1:2-4", 9, 10, {1, 2, 2, 2, 2,}},
};

static void
gst_interlace_decorate_buffer_ts (GstInterlace * interlace, GstBuffer * buf,
    int n_fields)
{
  gint src_fps_n, src_fps_d;
  g_mutex_lock (&interlace->lock);
  src_fps_n = interlace->src_fps_n;
  src_fps_d = interlace->src_fps_d;
  g_mutex_unlock (&interlace->lock);
  /* field duration = src_fps_d / (2 * src_fps_n) */
  if (src_fps_n == 0) {
    /* If we don't know the fps, we can't generate timestamps/durations */
    GST_BUFFER_DTS (buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_PTS (buf) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;
  } else {
    GST_BUFFER_DTS (buf) = interlace->timebase +
        gst_util_uint64_scale (GST_SECOND,
        src_fps_d * interlace->fields_since_timebase, src_fps_n * 2);
    GST_BUFFER_PTS (buf) = GST_BUFFER_DTS (buf);
    GST_BUFFER_DURATION (buf) =
        gst_util_uint64_scale (GST_SECOND, src_fps_d * n_fields, src_fps_n * 2);
  }
}

static void
gst_interlace_decorate_buffer (GstInterlace * interlace, GstBuffer * buf,
    int n_fields, gboolean interlaced)
{
  gst_interlace_decorate_buffer_ts (interlace, buf, n_fields);

  if (interlace->field_index == 0) {
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_FLAG_TFF);
  }
  if (n_fields == 3) {
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_FLAG_RFF);
  }
  if (n_fields == 1) {
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_FLAG_ONEFIELD);
  }
  g_mutex_lock (&interlace->lock);
  if (interlace->pattern > GST_INTERLACE_PATTERN_2_2 && n_fields == 2
      && interlaced) {
    GST_BUFFER_FLAG_SET (buf, GST_VIDEO_BUFFER_FLAG_INTERLACED);
  }
  g_mutex_unlock (&interlace->lock);
}

static const gchar *
interlace_mode_from_pattern (GstInterlace * interlace)
{
  GstInterlacePattern pattern;
  g_mutex_lock (&interlace->lock);
  pattern = interlace->pattern;
  g_mutex_unlock (&interlace->lock);
  if (pattern > GST_INTERLACE_PATTERN_2_2)
    return "mixed";
  else
    return "interleaved";
}

static GstCaps *
dup_caps_with_alternate (GstCaps * caps)
{
  GstCaps *with_alternate;
  GstCapsFeatures *features;

  with_alternate = gst_caps_copy (caps);
  features = gst_caps_features_new (GST_CAPS_FEATURE_FORMAT_INTERLACED, NULL);
  gst_caps_set_features_simple (with_alternate, features);

  gst_caps_set_simple (with_alternate, "interlace-mode", G_TYPE_STRING,
      "alternate", NULL);

  return with_alternate;
}

static gboolean
gst_interlace_setcaps (GstInterlace * interlace, GstCaps * caps)
{
  gboolean ret;
  GstVideoInfo info, out_info;
  GstCaps *othercaps, *src_peer_caps;
  const PulldownFormat *pdformat;
  gboolean alternate;
  int i;
  int src_fps_n, src_fps_d;
  GstInterlacePattern pattern;

  if (!gst_video_info_from_caps (&info, caps))
    goto caps_error;

  g_mutex_lock (&interlace->lock);
  interlace->pattern = interlace->new_pattern;
  pattern = interlace->pattern;
  g_mutex_unlock (&interlace->lock);

  /* Check if downstream prefers alternate mode */
  othercaps = gst_caps_copy (caps);
  gst_caps_set_simple (othercaps, "interlace-mode", G_TYPE_STRING,
      interlace_mode_from_pattern (interlace), NULL);
  gst_caps_append (othercaps, dup_caps_with_alternate (othercaps));
  if (pattern == GST_INTERLACE_PATTERN_2_2) {
    for (i = 0; i < gst_caps_get_size (othercaps); ++i) {
      GstStructure *s;

      s = gst_caps_get_structure (othercaps, i);
      gst_structure_remove_field (s, "field-order");
    }
  } else if (pattern == GST_INTERLACE_PATTERN_1_1 &&
      GST_VIDEO_INFO_INTERLACE_MODE (&info) ==
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE) {
    /* interlaced will do passthrough, mixed will fail later in the
     * negotiation */
    othercaps = gst_interlace_caps_double_framerate (othercaps, TRUE, FALSE);
  } else if (pattern > GST_INTERLACE_PATTERN_2_2) {
    GST_FIXME_OBJECT (interlace,
        "Add calculations for telecine framerate conversions");
    for (i = 0; i < gst_caps_get_size (othercaps); ++i) {
      GstStructure *s = gst_caps_get_structure (othercaps, i);

      gst_structure_remove_field (s, "framerate");
    }
  }
  src_peer_caps = gst_pad_peer_query_caps (interlace->srcpad, othercaps);
  gst_caps_unref (othercaps);
  othercaps = gst_caps_fixate (src_peer_caps);
  if (gst_caps_is_empty (othercaps)) {
    gst_caps_unref (othercaps);
    goto caps_error;
  }
  if (!gst_video_info_from_caps (&out_info, othercaps)) {
    gst_caps_unref (othercaps);
    goto caps_error;
  }

  alternate =
      GST_VIDEO_INFO_INTERLACE_MODE (&out_info) ==
      GST_VIDEO_INTERLACE_MODE_ALTERNATE;

  pdformat = &formats[pattern];

  interlace->phase_index = interlace->pattern_offset;

  src_fps_n = info.fps_n * pdformat->ratio_n;
  src_fps_d = info.fps_d * pdformat->ratio_d;
  g_mutex_lock (&interlace->lock);
  interlace->src_fps_n = src_fps_n;
  interlace->src_fps_d = src_fps_d;
  g_mutex_unlock (&interlace->lock);
  GST_DEBUG_OBJECT (interlace, "new framerate %d/%d", src_fps_n, src_fps_d);

  if (alternate) {
    GST_DEBUG_OBJECT (interlace,
        "producing alternate stream as requested downstream");
  }

  interlace->switch_fields = FALSE;
  if (gst_caps_can_intersect (caps, othercaps) &&
      pattern <= GST_INTERLACE_PATTERN_2_2 &&
      GST_VIDEO_INFO_INTERLACE_MODE (&info) != GST_VIDEO_INTERLACE_MODE_MIXED) {
    /* FIXME: field-order is optional in the caps. This means that, if we're
     * in a non-telecine mode and we have TFF upstream and
     * top-field-first=FALSE in interlace (or the other way around), AND
     * field-order isn't mentioned in the caps, we will do passthrough here
     * and end up outptuting wrong data. Must detect missing field-order info
     * and not do passthrough in that case, but instead check the
     * GstVideoBufferFlags at the switch_fields check */
    interlace->passthrough = TRUE;
  } else {
    if (GST_VIDEO_INFO_IS_INTERLACED (&info)) {
      if (pattern == GST_INTERLACE_PATTERN_2_2) {
        /* There is a chance we'd have to switch fields when in fact doing
         * passthrough - see FIXME comment above, basically it would
         * auto-negotiate to passthrough (because field-order is missing from
         * the caps) */
        GstCaps *clonedcaps = gst_caps_copy (othercaps);
        for (i = 0; i < gst_caps_get_size (clonedcaps); ++i) {
          GstStructure *s = gst_caps_get_structure (clonedcaps, i);

          gst_structure_remove_field (s, "field-order");
        }
        if (gst_caps_can_intersect (caps, clonedcaps)) {
          interlace->switch_fields = TRUE;
          gst_caps_unref (clonedcaps);
        } else {
          gst_caps_unref (clonedcaps);
          GST_ERROR_OBJECT (interlace,
              "Caps %" GST_PTR_FORMAT " not compatible with %" GST_PTR_FORMAT,
              caps, othercaps);
          gst_caps_unref (othercaps);
          goto caps_error;
        }
      } else {
        GST_ERROR_OBJECT (interlace,
            "Caps %" GST_PTR_FORMAT " not compatible with %" GST_PTR_FORMAT,
            caps, othercaps);
        gst_caps_unref (othercaps);
        goto caps_error;
      }
    }
    interlace->passthrough = FALSE;
    gst_caps_set_simple (othercaps, "framerate", GST_TYPE_FRACTION, src_fps_n,
        src_fps_d, NULL);
    if (pattern <= GST_INTERLACE_PATTERN_2_2 || alternate) {
      gst_caps_set_simple (othercaps, "field-order", G_TYPE_STRING,
          interlace->top_field_first ? "top-field-first" : "bottom-field-first",
          NULL);
    }
    /* outcaps changed, regenerate out_info */
    gst_video_info_from_caps (&out_info, othercaps);
  }

  GST_DEBUG_OBJECT (interlace->sinkpad, "set caps %" GST_PTR_FORMAT, caps);
  GST_DEBUG_OBJECT (interlace->srcpad, "set caps %" GST_PTR_FORMAT, othercaps);

  ret = gst_pad_set_caps (interlace->srcpad, othercaps);
  gst_caps_unref (othercaps);

  interlace->info = info;
  interlace->out_info = out_info;

  return ret;

caps_error:
  {
    GST_DEBUG_OBJECT (interlace, "error parsing caps");
    return FALSE;
  }
}

static gboolean
gst_interlace_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean ret;
  GstInterlace *interlace;

  interlace = GST_INTERLACE (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (interlace, "handling FLUSH_START");
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
            n_fields, FALSE);

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
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_interlace_setcaps (interlace, caps);
      gst_event_unref (event);
      break;
    }
    default:
      ret = gst_pad_push_event (interlace->srcpad, event);
      break;
  }

  return ret;
}

static gboolean
gst_interlace_fraction_double (gint * n_out, gint * d_out, gboolean half)
{
  gint n, d, gcd;

  n = *n_out;
  d = *d_out;

  if (d == 0)
    return FALSE;

  if (n == 0)
    return TRUE;

  gcd = gst_util_greatest_common_divisor (n, d);
  n /= gcd;
  d /= gcd;

  if (half) {
    if (G_MAXINT / 2 >= ABS (d)) {
      d *= 2;
    } else if (n >= 2 && n != G_MAXINT) {
      n /= 2;
    } else {
      d = G_MAXINT;
    }
  } else {
    if (G_MAXINT / 2 >= ABS (n)) {
      n *= 2;
    } else if (d >= 2 && d != G_MAXINT) {
      d /= 2;
    } else {
      n = G_MAXINT;
    }
  }

  *n_out = n;
  *d_out = d;

  return TRUE;
}

static GstCaps *
gst_interlace_caps_double_framerate (GstCaps * caps, gboolean half,
    gboolean skip_progressive)
{
  guint len;

  for (len = gst_caps_get_size (caps); len > 0; len--) {
    GstStructure *s = gst_caps_get_structure (caps, len - 1);
    const GValue *val;
    const gchar *interlace_mode;

    val = gst_structure_get_value (s, "framerate");
    if (!val)
      continue;

    interlace_mode = gst_structure_get_string (s, "interlace-mode");
    /* Do not double the framerate for interlaced - we will either passthrough
     * or fail to negotiate */
    if (skip_progressive && (g_strcmp0 (interlace_mode, "progressive") != 0))
      continue;

    if (G_VALUE_TYPE (val) == GST_TYPE_FRACTION) {
      gint n, d;

      n = gst_value_get_fraction_numerator (val);
      d = gst_value_get_fraction_denominator (val);

      if (!gst_interlace_fraction_double (&n, &d, half)) {
        gst_caps_remove_structure (caps, len - 1);
        continue;
      }

      gst_structure_set (s, "framerate", GST_TYPE_FRACTION, n, d, NULL);
    } else if (G_VALUE_TYPE (val) == GST_TYPE_FRACTION_RANGE) {
      const GValue *min, *max;
      GValue nrange = { 0, }, nmin = {
      0,}, nmax = {
      0,};
      gint n, d;

      g_value_init (&nrange, GST_TYPE_FRACTION_RANGE);
      g_value_init (&nmin, GST_TYPE_FRACTION);
      g_value_init (&nmax, GST_TYPE_FRACTION);

      min = gst_value_get_fraction_range_min (val);
      max = gst_value_get_fraction_range_max (val);

      n = gst_value_get_fraction_numerator (min);
      d = gst_value_get_fraction_denominator (min);

      if (!gst_interlace_fraction_double (&n, &d, half)) {
        g_value_unset (&nrange);
        g_value_unset (&nmax);
        g_value_unset (&nmin);
        gst_caps_remove_structure (caps, len - 1);
        continue;
      }

      gst_value_set_fraction (&nmin, n, d);

      n = gst_value_get_fraction_numerator (max);
      d = gst_value_get_fraction_denominator (max);

      if (!gst_interlace_fraction_double (&n, &d, half)) {
        g_value_unset (&nrange);
        g_value_unset (&nmax);
        g_value_unset (&nmin);
        gst_caps_remove_structure (caps, len - 1);
        continue;
      }

      gst_value_set_fraction (&nmax, n, d);
      gst_value_set_fraction_range (&nrange, &nmin, &nmax);

      gst_structure_take_value (s, "framerate", &nrange);

      g_value_unset (&nmin);
      g_value_unset (&nmax);
    } else if (G_VALUE_TYPE (val) == GST_TYPE_LIST) {
      const GValue *lval;
      GValue nlist = { 0, };
      GValue nval = { 0, };
      gint i;

      g_value_init (&nlist, GST_TYPE_LIST);
      for (i = gst_value_list_get_size (val); i > 0; i--) {
        gint n, d;

        lval = gst_value_list_get_value (val, i - 1);

        if (G_VALUE_TYPE (lval) != GST_TYPE_FRACTION)
          continue;

        n = gst_value_get_fraction_numerator (lval);
        d = gst_value_get_fraction_denominator (lval);

        /* Double/Half the framerate but if this fails simply
         * skip this value from the list */
        if (!gst_interlace_fraction_double (&n, &d, half)) {
          continue;
        }

        g_value_init (&nval, GST_TYPE_FRACTION);

        gst_value_set_fraction (&nval, n, d);
        gst_value_list_append_and_take_value (&nlist, &nval);
      }
      gst_structure_take_value (s, "framerate", &nlist);
    }
  }

  return caps;
}

static GstCaps *
gst_interlace_getcaps (GstPad * pad, GstInterlace * interlace, GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *othercaps, *tcaps;
  GstCaps *icaps;
  GstCaps *clean_filter = NULL;
  const char *mode;
  guint i;
  gint pattern;

  otherpad =
      (pad == interlace->srcpad) ? interlace->sinkpad : interlace->srcpad;

  g_mutex_lock (&interlace->lock);
  pattern = interlace->new_pattern;
  g_mutex_unlock (&interlace->lock);

  if (filter != NULL) {
    clean_filter = gst_caps_copy (filter);
    if (pattern == GST_INTERLACE_PATTERN_1_1) {
      clean_filter =
          gst_interlace_caps_double_framerate (clean_filter,
          (pad == interlace->sinkpad), TRUE);
    } else if (pattern != GST_INTERLACE_PATTERN_2_2) {
      GST_FIXME_OBJECT (interlace,
          "Add calculations for telecine framerate conversions");
      for (i = 0; i < gst_caps_get_size (clean_filter); ++i) {
        GstStructure *s = gst_caps_get_structure (clean_filter, i);

        gst_structure_remove_field (s, "framerate");
      }
    }

    if (pad == interlace->sinkpad) {
      /* @filter may contain the different formats supported upstream.
       * Those will be used to filter the src pad caps as this element
       * is not supposed to do any video format conversion.
       * Add a variant of the filter with the Interlaced feature as we want
       * to be able to negotiate it if needed.
       */
      gst_caps_append (clean_filter, dup_caps_with_alternate (clean_filter));
    }

    for (i = 0; i < gst_caps_get_size (clean_filter); ++i) {
      GstStructure *s;

      s = gst_caps_get_structure (clean_filter, i);
      gst_structure_remove_field (s, "interlace-mode");
      if (pattern == GST_INTERLACE_PATTERN_2_2 && pad == interlace->sinkpad) {
        gst_structure_remove_field (s, "field-order");
      }
    }
  }

  tcaps = gst_pad_get_pad_template_caps (otherpad);
  othercaps = gst_pad_peer_query_caps (otherpad, clean_filter);
  othercaps = gst_caps_make_writable (othercaps);
  if (othercaps) {
    if (pattern == GST_INTERLACE_PATTERN_2_2) {
      for (i = 0; i < gst_caps_get_size (othercaps); ++i) {
        GstStructure *s = gst_caps_get_structure (othercaps, i);

        if (pad == interlace->srcpad) {
          gst_structure_set (s, "field-order", G_TYPE_STRING,
              interlace->top_field_first ? "top-field-first" :
              "bottom-field-first", NULL);
        } else {
          gst_structure_remove_field (s, "field-order");
        }
      }
    }
    icaps = gst_caps_intersect (othercaps, tcaps);
    gst_caps_unref (othercaps);
    gst_caps_unref (tcaps);
  } else {
    icaps = tcaps;
  }

  if (clean_filter) {
    othercaps = gst_caps_intersect (icaps, clean_filter);
    gst_caps_unref (icaps);
    icaps = othercaps;
  }

  icaps = gst_caps_make_writable (icaps);
  mode = interlace_mode_from_pattern (interlace);

  if (pad == interlace->srcpad) {
    /* Set interlace-mode to what the element will produce, so either
     * mixed/interleaved or alternate if the caps feature is present. */
    gst_caps_set_simple (icaps, "interlace-mode", G_TYPE_STRING, mode, NULL);
    icaps = gst_caps_merge (icaps, dup_caps_with_alternate (icaps));
  } else {
    GstCaps *interlaced, *alternate;

    /* Sink pad is supposed to receive a progressive stream so remove the
     * Interlaced feature and set interlace-mode=progressive */
    for (i = 0; i < gst_caps_get_size (icaps); ++i) {
      GstCapsFeatures *features;

      features = gst_caps_get_features (icaps, i);
      gst_caps_features_remove (features, GST_CAPS_FEATURE_FORMAT_INTERLACED);
    }

    gst_caps_set_simple (icaps, "interlace-mode", G_TYPE_STRING, "progressive",
        NULL);

    /* Now add variants of the same caps with the interlace-mode and Interlaced
     * caps so we can operate in passthrough if needed. */
    interlaced = gst_caps_copy (icaps);
    gst_caps_set_simple (interlaced, "interlace-mode", G_TYPE_STRING, mode,
        NULL);
    alternate = dup_caps_with_alternate (icaps);

    icaps = gst_caps_merge (icaps, interlaced);
    icaps = gst_caps_merge (icaps, alternate);
  }

  if (pattern == GST_INTERLACE_PATTERN_1_1) {
    icaps =
        gst_interlace_caps_double_framerate (icaps, (pad == interlace->srcpad),
        FALSE);
  } else if (pattern != GST_INTERLACE_PATTERN_2_2) {
    GST_FIXME_OBJECT (interlace,
        "Add calculations for telecine framerate conversions");
    for (i = 0; i < gst_caps_get_size (icaps); ++i) {
      GstStructure *s = gst_caps_get_structure (icaps, i);

      gst_structure_remove_field (s, "framerate");
    }
  }

  if (clean_filter)
    gst_caps_unref (clean_filter);

  GST_DEBUG_OBJECT (pad, "caps: %" GST_PTR_FORMAT, icaps);
  return icaps;
}

static gboolean
gst_interlace_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret;
  GstInterlace *interlace;

  interlace = GST_INTERLACE (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_interlace_getcaps (pad, interlace, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }
  return ret;
}

static gboolean
gst_interlace_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret;
  GstInterlace *interlace;

  interlace = GST_INTERLACE (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_interlace_getcaps (pad, interlace, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }
  return ret;
}

static void
copy_fields (GstInterlace * interlace, GstBuffer * dest, GstBuffer * src,
    int field_index)
{
  GstVideoInfo *info = &interlace->info;
  gint i, j, n_planes;
  guint8 *d, *s;
  GstVideoFrame dframe, sframe;

  if (!gst_video_frame_map (&dframe, info, dest, GST_MAP_WRITE))
    goto dest_map_failed;

  if (!gst_video_frame_map (&sframe, info, src, GST_MAP_READ))
    goto src_map_failed;

  n_planes = GST_VIDEO_FRAME_N_PLANES (&dframe);

  for (i = 0; i < n_planes; i++) {
    gint cheight, cwidth;
    gint ss, ds;

    d = GST_VIDEO_FRAME_PLANE_DATA (&dframe, i);
    s = GST_VIDEO_FRAME_PLANE_DATA (&sframe, i);

    ds = GST_VIDEO_FRAME_PLANE_STRIDE (&dframe, i);
    ss = GST_VIDEO_FRAME_PLANE_STRIDE (&sframe, i);

    d += field_index * ds;
    if (!interlace->switch_fields) {
      s += field_index * ss;
    } else {
      s += (field_index ^ 1) * ss;
    }

    cheight = GST_VIDEO_FRAME_COMP_HEIGHT (&dframe, i);
    cwidth = MIN (ABS (ss), ABS (ds));

    for (j = field_index; j < cheight; j += 2) {
      memcpy (d, s, cwidth);
      d += ds * 2;
      s += ss * 2;
    }
  }

  gst_video_frame_unmap (&dframe);
  gst_video_frame_unmap (&sframe);
  return;

dest_map_failed:
  {
    GST_ERROR_OBJECT (interlace, "failed to map dest");
    return;
  }
src_map_failed:
  {
    GST_ERROR_OBJECT (interlace, "failed to map src");
    gst_video_frame_unmap (&dframe);
    return;
  }
}

static GstBuffer *
copy_field (GstInterlace * interlace, GstBuffer * src, int field_index)
{
  gint i, j, n_planes;
  GstVideoFrame dframe, sframe;
  GstBuffer *dest;

  dest =
      gst_buffer_new_allocate (NULL, GST_VIDEO_INFO_SIZE (&interlace->out_info),
      NULL);

  if (!gst_video_frame_map (&dframe, &interlace->out_info, dest, GST_MAP_WRITE))
    goto dest_map_failed;

  if (!gst_video_frame_map (&sframe, &interlace->info, src, GST_MAP_READ))
    goto src_map_failed;

  n_planes = GST_VIDEO_FRAME_N_PLANES (&dframe);

  for (i = 0; i < n_planes; i++) {
    guint8 *d, *s;
    gint cheight, cwidth;
    gint ss, ds;

    d = GST_VIDEO_FRAME_PLANE_DATA (&dframe, i);
    s = GST_VIDEO_FRAME_PLANE_DATA (&sframe, i);

    ds = GST_VIDEO_FRAME_PLANE_STRIDE (&dframe, i);
    ss = GST_VIDEO_FRAME_PLANE_STRIDE (&sframe, i);

    cheight = GST_VIDEO_FRAME_COMP_HEIGHT (&sframe, i);
    cwidth = MIN (ABS (ss), ABS (ds));

    for (j = field_index; j < cheight; j += 2) {
      memcpy (d, s, cwidth);
      d += ds;
      s += ss * 2;
    }
  }

  gst_video_frame_unmap (&dframe);
  gst_video_frame_unmap (&sframe);
  return dest;
dest_map_failed:
  {
    GST_ELEMENT_ERROR (interlace, CORE, FAILED, ("Failed to write map buffer"),
        ("Failed to map dest buffer for field %d", field_index));
    gst_buffer_unref (dest);
    return NULL;
  }
src_map_failed:
  {
    GST_ELEMENT_ERROR (interlace, CORE, FAILED, ("Failed to read map buffer"),
        ("Failed to map source buffer for field %d", field_index));
    gst_buffer_unref (dest);
    gst_video_frame_unmap (&dframe);
    return NULL;
  }
}

static GstFlowReturn
gst_interlace_push_buffer (GstInterlace * interlace, GstBuffer * buffer)
{
  GST_DEBUG_OBJECT (interlace, "output timestamp %" GST_TIME_FORMAT
      " duration %" GST_TIME_FORMAT " flags %04x %s %s %s",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_BUFFER_FLAGS (buffer),
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_FLAG_TFF) ? "tff" :
      "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_FLAG_RFF) ? "rff" :
      "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_FLAG_ONEFIELD) ?
      "onefield" : "");

  return gst_pad_push (interlace->srcpad, buffer);
}

static GstFlowReturn
gst_interlace_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstInterlace *interlace = GST_INTERLACE (parent);
  GstFlowReturn ret = GST_FLOW_OK;
  gint num_fields = 0;
  guint current_fields;
  const PulldownFormat *format;
  GstClockTime timestamp;
  gboolean alternate;

  timestamp = GST_BUFFER_TIMESTAMP (buffer);

  GST_DEBUG ("Received buffer at %" GST_TIME_FORMAT, GST_TIME_ARGS (timestamp));

  GST_DEBUG ("duration %" GST_TIME_FORMAT " flags %04x %s %s %s",
      GST_TIME_ARGS (GST_BUFFER_DURATION (buffer)),
      GST_BUFFER_FLAGS (buffer),
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_FLAG_TFF) ? "tff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_FLAG_RFF) ? "rff" : "",
      (GST_BUFFER_FLAGS (buffer) & GST_VIDEO_BUFFER_FLAG_ONEFIELD) ? "onefield"
      : "");

  if (interlace->passthrough) {
    return gst_pad_push (interlace->srcpad, buffer);
  }

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
    interlace->timebase = timestamp;
  }

  g_mutex_lock (&interlace->lock);
  format = &formats[interlace->pattern];
  g_mutex_unlock (&interlace->lock);

  if (interlace->stored_fields == 0
      && interlace->phase_index == interlace->pattern_offset
      && GST_CLOCK_TIME_IS_VALID (timestamp)) {
    interlace->timebase = timestamp;
    interlace->fields_since_timebase = 0;
  }

  current_fields = format->n_fields[interlace->phase_index];
  /* increment the phase index */
  interlace->phase_index++;
  g_assert (interlace->phase_index < G_N_ELEMENTS (format->n_fields));
  if (!format->n_fields[interlace->phase_index]) {
    interlace->phase_index = 0;
  }
  if (interlace->switch_fields && !interlace->stored_frame) {
    /* When switching fields, we want to skip the very first field of the very
     * first frame, then take one field from the stored frame and one from the
     * current one. This happens in the code when we do not have enough fields
     * available on current_fields, so we decrement the number, which is what
     * would happen if we had used one field. This way, the current frame
     * will be stored and then its other field will be used the next time the
     * chain function is called */
    current_fields--;
  }

  GST_DEBUG ("incoming buffer assigned %d fields", current_fields);

  alternate =
      GST_VIDEO_INFO_INTERLACE_MODE (&interlace->out_info) ==
      GST_VIDEO_INTERLACE_MODE_ALTERNATE;

  num_fields = interlace->stored_fields + current_fields;
  while (num_fields >= 2) {
    GstBuffer *output_buffer, *output_buffer2 = NULL;
    guint n_output_fields;
    gboolean interlaced = FALSE;

    GST_DEBUG ("have %d fields, %d current, %d stored",
        num_fields, current_fields, interlace->stored_fields);

    if (interlace->stored_fields > 0) {
      GST_DEBUG ("1 field from stored, 1 from current");

      if (alternate) {
        /* take the first field from the stored frame */
        output_buffer = copy_field (interlace, interlace->stored_frame,
            interlace->field_index);
        if (!output_buffer)
          return GST_FLOW_ERROR;
        /* take the second field from the incoming buffer */
        output_buffer2 = copy_field (interlace, buffer,
            interlace->field_index ^ 1);
        if (!output_buffer2)
          return GST_FLOW_ERROR;
      } else {
        output_buffer = gst_buffer_new_and_alloc (gst_buffer_get_size (buffer));
        /* take the first field from the stored frame */
        copy_fields (interlace, output_buffer, interlace->stored_frame,
            interlace->field_index);
        /* take the second field from the incoming buffer */
        copy_fields (interlace, output_buffer, buffer,
            interlace->field_index ^ 1);
      }

      interlace->stored_fields--;
      current_fields--;
      n_output_fields = 2;
      interlaced = TRUE;
    } else {
      if (alternate) {
        output_buffer = copy_field (interlace, buffer, interlace->field_index);
        if (!output_buffer)
          return GST_FLOW_ERROR;
        output_buffer2 =
            copy_field (interlace, buffer, interlace->field_index ^ 1);
        if (!output_buffer2)
          return GST_FLOW_ERROR;
      } else {
        output_buffer = gst_buffer_copy (buffer);
      }

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

    if (!alternate) {
      g_assert (!output_buffer2);
      gst_interlace_decorate_buffer (interlace, output_buffer, n_output_fields,
          interlaced);
    } else {
      g_assert (output_buffer2);
      gst_interlace_decorate_buffer_ts (interlace, output_buffer,
          n_output_fields);

      /* Both fields share the same ts */
      GST_BUFFER_PTS (output_buffer2) = GST_BUFFER_PTS (output_buffer);
      GST_BUFFER_DTS (output_buffer2) = GST_BUFFER_DTS (output_buffer);
      GST_BUFFER_DURATION (output_buffer2) =
          GST_BUFFER_DURATION (output_buffer);

      if (interlace->field_index == 0) {
        GST_BUFFER_FLAG_SET (output_buffer, GST_VIDEO_BUFFER_FLAG_TOP_FIELD);
        GST_BUFFER_FLAG_SET (output_buffer2,
            GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD);
      } else {
        GST_BUFFER_FLAG_SET (output_buffer, GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD);
        GST_BUFFER_FLAG_SET (output_buffer2, GST_VIDEO_BUFFER_FLAG_TOP_FIELD);
      }

      GST_BUFFER_FLAG_SET (output_buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
      GST_BUFFER_FLAG_SET (output_buffer2, GST_VIDEO_BUFFER_FLAG_INTERLACED);
    }

    /* Guard against overflows here. If this ever happens, resetting the phase
     * above would never happen because of some bugs */
    g_assert (interlace->fields_since_timebase <= G_MAXUINT - n_output_fields);
    interlace->fields_since_timebase += n_output_fields;
    interlace->field_index ^= (n_output_fields & 1);

    ret = gst_interlace_push_buffer (interlace, output_buffer);
    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (interlace, "Failed to push buffer %p", output_buffer);
      break;
    }

    if (output_buffer2) {
      ret = gst_interlace_push_buffer (interlace, output_buffer2);
      if (ret != GST_FLOW_OK) {
        GST_DEBUG_OBJECT (interlace, "Failed to push buffer %p",
            output_buffer2);
        break;
      }
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
    case PROP_PATTERN:{
      gint pattern = g_value_get_enum (value);
      g_mutex_lock (&interlace->lock);
      interlace->new_pattern = pattern;
      if (pattern == interlace->pattern || interlace->src_fps_n == 0) {
        interlace->pattern = pattern;
        g_mutex_unlock (&interlace->lock);
      } else {
        g_mutex_unlock (&interlace->lock);
        gst_pad_push_event (interlace->srcpad, gst_event_new_reconfigure ());
      }
      break;
    }
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
      g_mutex_lock (&interlace->lock);
      g_value_set_enum (value, interlace->new_pattern);
      g_mutex_unlock (&interlace->lock);
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
  GstInterlace *interlace = GST_INTERLACE (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (&interlace->lock);
      interlace->src_fps_n = 0;
      g_mutex_unlock (&interlace->lock);
      /* why? */
      //gst_interlace_reset (interlace);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
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
    interlace,
    "Create an interlaced video stream",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
