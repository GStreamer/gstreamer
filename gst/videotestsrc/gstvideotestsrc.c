/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2002> David A. Schleef <ds@schleef.org>
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
 * SECTION:element-videotestsrc
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v videotestsrc pattern=snow ! ximagesink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "gstvideotestsrc.h"
#include "videotestsrc.h"

#include <string.h>
#include <stdlib.h>
#include <liboil/liboil.h>

#define USE_PEER_BUFFERALLOC

GST_DEBUG_CATEGORY (video_test_src_debug);
#define GST_CAT_DEFAULT video_test_src_debug


static GstElementDetails video_test_src_details =
GST_ELEMENT_DETAILS ("Video test source",
    "Source/Video",
    "Creates a test video stream",
    "David A. Schleef <ds@schleef.org>");


enum
{
  PROP_0,
  PROP_PATTERN,
  PROP_TIMESTAMP_OFFSET,
  PROP_IS_LIVE
      /* FILL ME */
};


GST_BOILERPLATE (GstVideoTestSrc, gst_video_test_src, GstPushSrc,
    GST_TYPE_PUSH_SRC);


static void gst_video_test_src_set_pattern (GstVideoTestSrc * videotestsrc,
    int pattern_type);
static void gst_video_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_video_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_video_test_src_getcaps (GstBaseSrc * bsrc);
static gboolean gst_video_test_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static void gst_video_test_src_src_fixate (GstPad * pad, GstCaps * caps);

static gboolean gst_video_test_src_is_seekable (GstBaseSrc * psrc);
static gboolean gst_video_test_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static gboolean gst_video_test_src_query (GstBaseSrc * bsrc, GstQuery * query);

static void gst_video_test_src_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_video_test_src_create (GstPushSrc * psrc,
    GstBuffer ** buffer);
static gboolean gst_video_test_src_start (GstBaseSrc * basesrc);

#define GST_TYPE_VIDEO_TEST_SRC_PATTERN (gst_video_test_src_pattern_get_type ())
static GType
gst_video_test_src_pattern_get_type (void)
{
  static GType video_test_src_pattern_type = 0;
  static GEnumValue pattern_types[] = {
    {GST_VIDEO_TEST_SRC_SMPTE, "SMPTE 100% color bars", "smpte"},
    {GST_VIDEO_TEST_SRC_SNOW, "Random (television snow)", "snow"},
    {GST_VIDEO_TEST_SRC_BLACK, "100% Black", "black"},
    {0, NULL, NULL},
  };

  if (!video_test_src_pattern_type) {
    video_test_src_pattern_type =
        g_enum_register_static ("GstVideoTestSrcPattern", pattern_types);
  }
  return video_test_src_pattern_type;
}

static void
gst_video_test_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &video_test_src_details);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_video_test_src_getcaps (NULL)));
}

static void
gst_video_test_src_class_init (GstVideoTestSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_video_test_src_set_property;
  gobject_class->get_property = gst_video_test_src_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_PATTERN,
      g_param_spec_enum ("pattern", "Pattern",
          "Type of test pattern to generate", GST_TYPE_VIDEO_TEST_SRC_PATTERN,
          1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int64 ("timestamp-offset",
          "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to act as a live source", FALSE, G_PARAM_READWRITE));

  gstbasesrc_class->get_caps = gst_video_test_src_getcaps;
  gstbasesrc_class->set_caps = gst_video_test_src_setcaps;
  gstbasesrc_class->is_seekable = gst_video_test_src_is_seekable;
  gstbasesrc_class->do_seek = gst_video_test_src_do_seek;
  gstbasesrc_class->query = gst_video_test_src_query;
  gstbasesrc_class->get_times = gst_video_test_src_get_times;
  gstbasesrc_class->start = gst_video_test_src_start;

  gstpushsrc_class->create = gst_video_test_src_create;
}

static void
gst_video_test_src_init (GstVideoTestSrc * src, GstVideoTestSrcClass * g_class)
{
  GstPad *pad = GST_BASE_SRC_PAD (src);

  gst_pad_set_fixatecaps_function (pad, gst_video_test_src_src_fixate);

  gst_video_test_src_set_pattern (src, GST_VIDEO_TEST_SRC_SMPTE);

  src->timestamp_offset = 0;

  /* we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);
}

static void
gst_video_test_src_src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
}

static void
gst_video_test_src_set_pattern (GstVideoTestSrc * videotestsrc,
    int pattern_type)
{
  videotestsrc->pattern_type = pattern_type;

  GST_DEBUG_OBJECT (videotestsrc, "setting pattern to %d", pattern_type);

  switch (pattern_type) {
    case GST_VIDEO_TEST_SRC_SMPTE:
      videotestsrc->make_image = gst_video_test_src_smpte;
      break;
    case GST_VIDEO_TEST_SRC_SNOW:
      videotestsrc->make_image = gst_video_test_src_snow;
      break;
    case GST_VIDEO_TEST_SRC_BLACK:
      videotestsrc->make_image = gst_video_test_src_black;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_video_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoTestSrc *src = GST_VIDEO_TEST_SRC (object);

  switch (prop_id) {
    case PROP_PATTERN:
      gst_video_test_src_set_pattern (src, g_value_get_enum (value));
      break;
    case PROP_TIMESTAMP_OFFSET:
      src->timestamp_offset = g_value_get_int64 (value);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (src), g_value_get_boolean (value));
      break;
    default:
      break;
  }
}

static void
gst_video_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVideoTestSrc *src = GST_VIDEO_TEST_SRC (object);

  switch (prop_id) {
    case PROP_PATTERN:
      g_value_set_enum (value, src->pattern_type);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->timestamp_offset);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (src)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* threadsafe because this gets called as the plugin is loaded */
static GstCaps *
gst_video_test_src_getcaps (GstBaseSrc * unused)
{
  static GstCaps *capslist = NULL;

  if (!capslist) {
    GstCaps *caps;
    GstStructure *structure;
    int i;

    caps = gst_caps_new_empty ();
    for (i = 0; i < n_fourccs; i++) {
      structure = paint_get_structure (fourcc_list + i);
      gst_structure_set (structure,
          "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
      gst_caps_append_structure (caps, structure);
    }

    capslist = caps;
  }

  return gst_caps_copy (capslist);
}

static gboolean
gst_video_test_src_parse_caps (const GstCaps * caps,
    gint * width, gint * height, gint * rate_numerator, gint * rate_denominator,
    struct fourcc_list_struct **fourcc)
{
  const GstStructure *structure;
  GstPadLinkReturn ret;
  const GValue *framerate;

  GST_DEBUG ("parsing caps");

  if (gst_caps_get_size (caps) < 1)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!(*fourcc = paintinfo_find_by_structure (structure)))
    goto unknown_format;

  ret = gst_structure_get_int (structure, "width", width);
  ret &= gst_structure_get_int (structure, "height", height);
  framerate = gst_structure_get_value (structure, "framerate");

  if (framerate) {
    *rate_numerator = gst_value_get_fraction_numerator (framerate);
    *rate_denominator = gst_value_get_fraction_denominator (framerate);
  } else
    goto no_framerate;

  return ret;

  /* ERRORS */
unknown_format:
  {
    GST_DEBUG ("videotestsrc format not found");
    return FALSE;
  }
no_framerate:
  {
    GST_DEBUG ("videotestsrc no framerate given");
    return FALSE;
  }
}

static gboolean
gst_video_test_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  gboolean res;
  gint width, height, rate_denominator, rate_numerator;
  struct fourcc_list_struct *fourcc;
  GstVideoTestSrc *videotestsrc;

  videotestsrc = GST_VIDEO_TEST_SRC (bsrc);

  res = gst_video_test_src_parse_caps (caps, &width, &height,
      &rate_numerator, &rate_denominator, &fourcc);
  if (res) {
    /* looks ok here */
    videotestsrc->fourcc = fourcc;
    videotestsrc->width = width;
    videotestsrc->height = height;
    videotestsrc->rate_numerator = rate_numerator;
    videotestsrc->rate_denominator = rate_denominator;
    videotestsrc->bpp = videotestsrc->fourcc->bitspp;

    GST_DEBUG_OBJECT (videotestsrc, "size %dx%d, %d/%d fps",
        videotestsrc->width, videotestsrc->height,
        videotestsrc->rate_numerator, videotestsrc->rate_denominator);
  }
  return res;
}

static gboolean
gst_video_test_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res;
  GstVideoTestSrc *src;

  src = GST_VIDEO_TEST_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (src_fmt == dest_fmt) {
        dest_val = src_val;
        goto done;
      }

      switch (src_fmt) {
        case GST_FORMAT_DEFAULT:
          switch (dest_fmt) {
            case GST_FORMAT_TIME:
              /* frames to time */
              dest_val = gst_util_uint64_scale (src_val,
                  src->rate_denominator * GST_SECOND, src->rate_numerator);
              break;
            default:
              goto error;
          }
          break;
        case GST_FORMAT_TIME:
          switch (dest_fmt) {
            case GST_FORMAT_DEFAULT:
              /* time to frames */
              dest_val = gst_util_uint64_scale (src_val,
                  src->rate_numerator, src->rate_denominator * GST_SECOND);
              break;
            default:
              goto error;
          }
          break;
        default:
          goto error;
      }
    done:
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
  }
  return res;

  /* ERROR */
error:
  {
    GST_DEBUG_OBJECT (src, "query failed");
    return FALSE;
  }
}

static void
gst_video_test_src_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration)) {
        *end = timestamp + duration;
      }
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static gboolean
gst_video_test_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstClockTime time;
  GstVideoTestSrc *src;

  src = GST_VIDEO_TEST_SRC (bsrc);

  time = segment->time = segment->start;

  /* now move to the time indicated */
  src->n_frames = gst_util_uint64_scale (time,
      src->rate_numerator, src->rate_denominator * GST_SECOND);
  src->running_time = gst_util_uint64_scale (src->n_frames,
      src->rate_denominator * GST_SECOND, src->rate_numerator);

  g_assert (src->running_time <= time);

  return TRUE;
}

static gboolean
gst_video_test_src_is_seekable (GstBaseSrc * psrc)
{
  /* we're seekable... */
  return TRUE;
}

static GstFlowReturn
gst_video_test_src_create (GstPushSrc * psrc, GstBuffer ** buffer)
{
  GstVideoTestSrc *src;
  gulong newsize;
  GstBuffer *outbuf;
  GstFlowReturn res;
  GstClockTime next_time;

  src = GST_VIDEO_TEST_SRC (psrc);

  if (src->fourcc == NULL || src->rate_numerator == 0)
    goto not_negotiated;

  newsize = gst_video_test_src_get_size (src, src->width, src->height);

  g_return_val_if_fail (newsize > 0, GST_FLOW_ERROR);

  GST_LOG_OBJECT (src, "creating buffer of %ld bytes for %dx%d image",
      newsize, src->width, src->height);

#ifdef USE_PEER_BUFFERALLOC
  res =
      gst_pad_alloc_buffer_and_set_caps (GST_BASE_SRC_PAD (psrc),
      GST_BUFFER_OFFSET_NONE, newsize, GST_PAD_CAPS (GST_BASE_SRC_PAD (psrc)),
      &outbuf);
  if (res != GST_FLOW_OK)
    goto no_buffer;
#else
  outbuf = gst_buffer_new_and_alloc (newsize);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (GST_BASE_SRC_PAD (psrc)));
#endif

  src->make_image (src, (void *) GST_BUFFER_DATA (outbuf),
      src->width, src->height);

  GST_BUFFER_TIMESTAMP (outbuf) = src->timestamp_offset + src->running_time;
  GST_BUFFER_OFFSET (outbuf) = src->n_frames;
  src->n_frames++;
  GST_BUFFER_OFFSET_END (outbuf) = src->n_frames;
  next_time = gst_util_uint64_scale_int (src->n_frames * GST_SECOND,
      src->rate_denominator, src->rate_numerator);
  GST_BUFFER_DURATION (outbuf) = next_time - src->running_time;

  src->running_time = next_time;

  *buffer = outbuf;

  return GST_FLOW_OK;

not_negotiated:
  {
    GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before get function"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
no_buffer:
  {
    GST_DEBUG_OBJECT (src, "could not allocate buffer, reason %s",
        gst_flow_get_name (res));
    return res;
  }
}

static gboolean
gst_video_test_src_start (GstBaseSrc * basesrc)
{
  GstVideoTestSrc *src = GST_VIDEO_TEST_SRC (basesrc);

  src->running_time = 0;
  src->n_frames = 0;

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  oil_init ();

  GST_DEBUG_CATEGORY_INIT (video_test_src_debug, "videotestsrc", 0,
      "Video Test Source");

  return gst_element_register (plugin, "videotestsrc", GST_RANK_NONE,
      GST_TYPE_VIDEO_TEST_SRC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videotestsrc",
    "Creates a test video stream",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
