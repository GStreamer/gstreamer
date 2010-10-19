/*
 * GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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
 * SECTION:element-gltestsrc
 *
 * <refsect2>
 * <para>
 * The gltestsrc element is used to produce test video texture.
 * The video test produced can be controlled with the "pattern"
 * property.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v gltestsrc pattern=smpte ! glimagesink
 * </programlisting>
 * Shows original SMPTE color bars in a window.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgltestsrc.h"
#include "gltestsrc.h"
#include <gst/gst-i18n-plugin.h>

#define USE_PEER_BUFFERALLOC

GST_DEBUG_CATEGORY_STATIC (gl_test_src_debug);
#define GST_CAT_DEFAULT gl_test_src_debug

enum
{
  PROP_0,
  PROP_PATTERN,
  PROP_TIMESTAMP_OFFSET,
  PROP_IS_LIVE
      /* FILL ME */
};

GST_BOILERPLATE (GstGLTestSrc, gst_gl_test_src, GstPushSrc, GST_TYPE_PUSH_SRC);

static void gst_gl_test_src_set_pattern (GstGLTestSrc * gltestsrc,
    int pattern_type);
static void gst_gl_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_test_src_src_query (GstPad * pad, GstQuery * query);

static gboolean gst_gl_test_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static void gst_gl_test_src_src_fixate (GstPad * pad, GstCaps * caps);

static gboolean gst_gl_test_src_is_seekable (GstBaseSrc * psrc);
static gboolean gst_gl_test_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static gboolean gst_gl_test_src_query (GstBaseSrc * bsrc, GstQuery * query);

static void gst_gl_test_src_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_gl_test_src_create (GstPushSrc * psrc,
    GstBuffer ** buffer);
static gboolean gst_gl_test_src_start (GstBaseSrc * basesrc);
static gboolean gst_gl_test_src_stop (GstBaseSrc * basesrc);

static void gst_gl_test_src_callback (gint width, gint height, guint texture,
    gpointer stuff);

#define GST_TYPE_GL_TEST_SRC_PATTERN (gst_gl_test_src_pattern_get_type ())
static GType
gst_gl_test_src_pattern_get_type (void)
{
  static GType gl_test_src_pattern_type = 0;
  static const GEnumValue pattern_types[] = {
    {GST_GL_TEST_SRC_SMPTE, "SMPTE 100% color bars", "smpte"},
    {GST_GL_TEST_SRC_SNOW, "Random (television snow)", "snow"},
    {GST_GL_TEST_SRC_BLACK, "100% Black", "black"},
    {GST_GL_TEST_SRC_WHITE, "100% White", "white"},
    {GST_GL_TEST_SRC_RED, "Red", "red"},
    {GST_GL_TEST_SRC_GREEN, "Green", "green"},
    {GST_GL_TEST_SRC_BLUE, "Blue", "blue"},
    {GST_GL_TEST_SRC_CHECKERS1, "Checkers 1px", "checkers-1"},
    {GST_GL_TEST_SRC_CHECKERS2, "Checkers 2px", "checkers-2"},
    {GST_GL_TEST_SRC_CHECKERS4, "Checkers 4px", "checkers-4"},
    {GST_GL_TEST_SRC_CHECKERS8, "Checkers 8px", "checkers-8"},
    {GST_GL_TEST_SRC_CIRCULAR, "Circular", "circular"},
    {GST_GL_TEST_SRC_BLINK, "Blink", "blink"},
    {0, NULL, NULL}
  };

  if (!gl_test_src_pattern_type) {
    gl_test_src_pattern_type =
        g_enum_register_static ("GstGLTestSrcPattern", pattern_types);
  }
  return gl_test_src_pattern_type;
}

static void
gst_gl_test_src_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video test source",
      "Source/Video", "Creates a test video stream",
      "David A. Schleef <ds@schleef.org>");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_from_string (GST_GL_VIDEO_CAPS)));
}

static void
gst_gl_test_src_class_init (GstGLTestSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  GST_DEBUG_CATEGORY_INIT (gl_test_src_debug, "gltestsrc", 0,
      "Video Test Source");

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->set_property = gst_gl_test_src_set_property;
  gobject_class->get_property = gst_gl_test_src_get_property;

  g_object_class_install_property (gobject_class, PROP_PATTERN,
      g_param_spec_enum ("pattern", "Pattern",
          "Type of test pattern to generate", GST_TYPE_GL_TEST_SRC_PATTERN,
          GST_GL_TEST_SRC_SMPTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_TIMESTAMP_OFFSET, g_param_spec_int64 ("timestamp-offset",
          "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to act as a live source", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbasesrc_class->set_caps = gst_gl_test_src_setcaps;
  gstbasesrc_class->is_seekable = gst_gl_test_src_is_seekable;
  gstbasesrc_class->do_seek = gst_gl_test_src_do_seek;
  gstbasesrc_class->query = gst_gl_test_src_query;
  gstbasesrc_class->get_times = gst_gl_test_src_get_times;
  gstbasesrc_class->start = gst_gl_test_src_start;
  gstbasesrc_class->stop = gst_gl_test_src_stop;

  gstpushsrc_class->create = gst_gl_test_src_create;
}

static void
gst_gl_test_src_init (GstGLTestSrc * src, GstGLTestSrcClass * g_class)
{
  GstPad *pad = GST_BASE_SRC_PAD (src);

  gst_pad_set_fixatecaps_function (pad, gst_gl_test_src_src_fixate);

  gst_gl_test_src_set_pattern (src, GST_GL_TEST_SRC_SMPTE);

  src->timestamp_offset = 0;

  /* we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);

  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR (gst_gl_test_src_src_query));
}

static void
gst_gl_test_src_src_fixate (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;

  GST_DEBUG ("fixate");

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);
}

static void
gst_gl_test_src_set_pattern (GstGLTestSrc * gltestsrc, gint pattern_type)
{
  gltestsrc->pattern_type = pattern_type;

  GST_DEBUG_OBJECT (gltestsrc, "setting pattern to %d", pattern_type);

  switch (pattern_type) {
    case GST_GL_TEST_SRC_SMPTE:
      gltestsrc->make_image = gst_gl_test_src_smpte;
      break;
    case GST_GL_TEST_SRC_SNOW:
      gltestsrc->make_image = gst_gl_test_src_snow;
      break;
    case GST_GL_TEST_SRC_BLACK:
      gltestsrc->make_image = gst_gl_test_src_black;
      break;
    case GST_GL_TEST_SRC_WHITE:
      gltestsrc->make_image = gst_gl_test_src_white;
      break;
    case GST_GL_TEST_SRC_RED:
      gltestsrc->make_image = gst_gl_test_src_red;
      break;
    case GST_GL_TEST_SRC_GREEN:
      gltestsrc->make_image = gst_gl_test_src_green;
      break;
    case GST_GL_TEST_SRC_BLUE:
      gltestsrc->make_image = gst_gl_test_src_blue;
      break;
    case GST_GL_TEST_SRC_CHECKERS1:
      gltestsrc->make_image = gst_gl_test_src_checkers1;
      break;
    case GST_GL_TEST_SRC_CHECKERS2:
      gltestsrc->make_image = gst_gl_test_src_checkers2;
      break;
    case GST_GL_TEST_SRC_CHECKERS4:
      gltestsrc->make_image = gst_gl_test_src_checkers4;
      break;
    case GST_GL_TEST_SRC_CHECKERS8:
      gltestsrc->make_image = gst_gl_test_src_checkers8;
      break;
    case GST_GL_TEST_SRC_CIRCULAR:
      gltestsrc->make_image = gst_gl_test_src_circular;
      break;
    case GST_GL_TEST_SRC_BLINK:
      gltestsrc->make_image = gst_gl_test_src_black;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_gl_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (object);

  switch (prop_id) {
    case PROP_PATTERN:
      gst_gl_test_src_set_pattern (src, g_value_get_enum (value));
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
gst_gl_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (object);

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

static gboolean
gst_gl_test_src_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstElement *parent = GST_ELEMENT (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = gst_query_get_structure (query);
      res =
          g_strcmp0 (gst_element_get_name (parent),
          gst_structure_get_name (structure)) == 0;
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  return res;
}

static gboolean
gst_gl_test_src_parse_caps (const GstCaps * caps,
    gint * width, gint * height, gint * rate_numerator, gint * rate_denominator)
{
  const GstStructure *structure;
  GstPadLinkReturn ret;
  const GValue *framerate;

  GST_DEBUG ("parsing caps");

  if (gst_caps_get_size (caps) < 1)
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

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
no_framerate:
  {
    GST_DEBUG ("gltestsrc no framerate given");
    return FALSE;
  }
}

static gboolean
gst_gl_test_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  gboolean res;
  gint width, height, rate_denominator, rate_numerator;
  GstGLTestSrc *gltestsrc;

  gltestsrc = GST_GL_TEST_SRC (bsrc);

  GST_DEBUG ("setcaps");

  res = gst_gl_test_src_parse_caps (caps, &width, &height,
      &rate_numerator, &rate_denominator);
  if (res) {
    /* looks ok here */
    gltestsrc->width = width;
    gltestsrc->height = height;
    gltestsrc->rate_numerator = rate_numerator;
    gltestsrc->rate_denominator = rate_denominator;
    gltestsrc->negotiated = TRUE;

    GST_DEBUG_OBJECT (gltestsrc, "size %dx%d, %d/%d fps",
        gltestsrc->width, gltestsrc->height,
        gltestsrc->rate_numerator, gltestsrc->rate_denominator);


    gst_gl_display_gen_fbo (gltestsrc->display, gltestsrc->width,
        gltestsrc->height, &gltestsrc->fbo, &gltestsrc->depthbuffer);
  }
  return res;
}

static gboolean
gst_gl_test_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res;
  GstGLTestSrc *src;

  src = GST_GL_TEST_SRC (bsrc);

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
              if (src->rate_numerator) {
                dest_val = gst_util_uint64_scale (src_val,
                    src->rate_denominator * GST_SECOND, src->rate_numerator);
              } else
                dest_val = 0;
              break;
            default:
              goto error;
          }
          break;
        case GST_FORMAT_TIME:
          switch (dest_fmt) {
            case GST_FORMAT_DEFAULT:
              /* time to frames */
              if (src->rate_numerator) {
                dest_val = gst_util_uint64_scale (src_val,
                    src->rate_numerator, src->rate_denominator * GST_SECOND);
              } else
                dest_val = 0;
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
gst_gl_test_src_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration))
        *end = timestamp + duration;
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static gboolean
gst_gl_test_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstClockTime time;
  GstGLTestSrc *src;

  src = GST_GL_TEST_SRC (bsrc);

  segment->time = segment->start;
  time = segment->last_stop;

  /* now move to the time indicated */
  if (src->rate_numerator) {
    src->n_frames = gst_util_uint64_scale (time,
        src->rate_numerator, src->rate_denominator * GST_SECOND);
  } else
    src->n_frames = 0;

  if (src->rate_numerator) {
    src->running_time = gst_util_uint64_scale (src->n_frames,
        src->rate_denominator * GST_SECOND, src->rate_numerator);
  } else {
    /* FIXME : Not sure what to set here */
    src->running_time = 0;
  }

  g_assert (src->running_time <= time);

  return TRUE;
}

static gboolean
gst_gl_test_src_is_seekable (GstBaseSrc * psrc)
{
  /* we're seekable... */
  return TRUE;
}

static GstFlowReturn
gst_gl_test_src_create (GstPushSrc * psrc, GstBuffer ** buffer)
{
  GstGLTestSrc *src;
  GstGLBuffer *outbuf;

  //GstFlowReturn res;
  GstClockTime next_time;

  src = GST_GL_TEST_SRC (psrc);

  if (G_UNLIKELY (!src->negotiated))
    goto not_negotiated;

  /* 0 framerate and we are at the second frame, eos */
  if (G_UNLIKELY (src->rate_numerator == 0 && src->n_frames == 1))
    goto eos;

  GST_LOG_OBJECT (src, "creating buffer %dx%d image for frame %d",
      src->width, src->height, (gint) src->n_frames);

  outbuf = gst_gl_buffer_new (src->display, src->width, src->height);

  if (!outbuf->texture) {
    gst_buffer_unref (GST_BUFFER_CAST (outbuf));
    goto eos;
  }

  gst_buffer_set_caps (GST_BUFFER (outbuf),
      GST_PAD_CAPS (GST_BASE_SRC_PAD (psrc)));

  if (src->pattern_type == GST_GL_TEST_SRC_BLINK) {
    if (src->n_frames & 0x1)
      src->make_image = gst_gl_test_src_white;
    else
      src->make_image = gst_gl_test_src_black;
  }

  src->buffer = outbuf;

  //blocking call, generate a FBO
  if (!gst_gl_display_use_fbo (src->display, src->width, src->height, src->fbo, src->depthbuffer, outbuf->texture, gst_gl_test_src_callback, 0, 0, 0,   //no input texture
          0, src->width, 0, src->height,
          GST_GL_DISPLAY_PROJECTION_ORTHO2D, (gpointer) src)) {
    gst_buffer_unref (GST_BUFFER_CAST (outbuf));
    goto eos;
  }

  GST_BUFFER_TIMESTAMP (GST_BUFFER (outbuf)) =
      src->timestamp_offset + src->running_time;
  GST_BUFFER_OFFSET (GST_BUFFER (outbuf)) = src->n_frames;
  src->n_frames++;
  GST_BUFFER_OFFSET_END (GST_BUFFER (outbuf)) = src->n_frames;
  if (src->rate_numerator) {
    next_time = gst_util_uint64_scale_int (src->n_frames * GST_SECOND,
        src->rate_denominator, src->rate_numerator);
    GST_BUFFER_DURATION (GST_BUFFER (outbuf)) = next_time - src->running_time;
  } else {
    next_time = src->timestamp_offset;
    /* NONE means forever */
    GST_BUFFER_DURATION (GST_BUFFER (outbuf)) = GST_CLOCK_TIME_NONE;
  }

  src->running_time = next_time;

  *buffer = GST_BUFFER (outbuf);

  return GST_FLOW_OK;

not_negotiated:
  {
    GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL),
        (_("format wasn't negotiated before get function")));
    return GST_FLOW_NOT_NEGOTIATED;
  }
eos:
  {
    GST_DEBUG_OBJECT (src, "eos: 0 framerate, frame %d", (gint) src->n_frames);
    return GST_FLOW_UNEXPECTED;
  }
#if 0
no_buffer:
  {
    GST_DEBUG_OBJECT (src, "could not allocate buffer, reason %s",
        gst_flow_get_name (res));
    return res;
  }
#endif
}

static gboolean
gst_gl_test_src_start (GstBaseSrc * basesrc)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (basesrc);
  GstElement *parent = GST_ELEMENT (gst_element_get_parent (src));
  GstStructure *structure = NULL;
  GstQuery *query = NULL;
  gboolean isPerformed = FALSE;

  if (!parent) {
    GST_ELEMENT_ERROR (src, CORE, STATE_CHANGE, (NULL),
        ("A parent bin is required"));
    return FALSE;
  }

  structure = gst_structure_new (gst_element_get_name (src), NULL);
  query = gst_query_new_application (GST_QUERY_CUSTOM, structure);

  isPerformed = gst_element_query (parent, query);

  if (isPerformed) {
    const GValue *id_value =
        gst_structure_get_value (structure, "gstgldisplay");
    if (G_VALUE_HOLDS_POINTER (id_value))
      /* at least one gl element is before in our gl chain */
      src->display =
          g_object_ref (GST_GL_DISPLAY (g_value_get_pointer (id_value)));
    else {
      /* this gl filter is a sink in terms of the gl chain */
      src->display = gst_gl_display_new ();
      gst_gl_display_create_context (src->display, 0);
    }
  }

  gst_query_unref (query);
  gst_object_unref (GST_OBJECT (parent));

  src->running_time = 0;
  src->n_frames = 0;
  src->negotiated = FALSE;

  return isPerformed;
}

static gboolean
gst_gl_test_src_stop (GstBaseSrc * basesrc)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (basesrc);

  if (src->display) {
    //blocking call, delete the FBO
    gst_gl_display_del_fbo (src->display, src->fbo, src->depthbuffer);
    g_object_unref (src->display);
    src->display = NULL;
  }

  return TRUE;
}

//opengl scene
static void
gst_gl_test_src_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (stuff);

  src->make_image (src, src->buffer, src->width, src->height);
}
