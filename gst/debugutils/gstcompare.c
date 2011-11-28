/* GStreamer Element
 *
 * Copyright 2011 Collabora Ltd.
 *  @author: Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright 2011 Nokia Corp.
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
#include <gst/base/gstcollectpads.h>
#include <gst/video/video.h>

#include "gstcompare.h"

GST_DEBUG_CATEGORY_STATIC (compare_debug);
#define GST_CAT_DEFAULT   compare_debug


static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate check_sink_factory =
GST_STATIC_PAD_TEMPLATE ("check",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

enum GstCompareMethod
{
  GST_COMPARE_METHOD_MEM,
  GST_COMPARE_METHOD_MAX,
  GST_COMPARE_METHOD_SSIM
};

#define GST_COMPARE_METHOD_TYPE (gst_compare_method_get_type())
static GType
gst_compare_method_get_type (void)
{
  static GType method_type = 0;

  static const GEnumValue method_types[] = {
    {GST_COMPARE_METHOD_MEM, "Memory", "mem"},
    {GST_COMPARE_METHOD_MAX, "Maximum metric", "max"},
    {GST_COMPARE_METHOD_SSIM, "SSIM (raw video)", "ssim"},
    {0, NULL, NULL}
  };

  if (!method_type) {
    method_type = g_enum_register_static ("GstCompareMethod", method_types);
  }
  return method_type;
}

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_META,
  PROP_OFFSET_TS,
  PROP_METHOD,
  PROP_THRESHOLD,
  PROP_UPPER,
  PROP_LAST
};

#define DEFAULT_META             GST_BUFFER_COPY_ALL
#define DEFAULT_OFFSET_TS        FALSE
#define DEFAULT_METHOD           GST_COMPARE_METHOD_MEM
#define DEFAULT_THRESHOLD        0
#define DEFAULT_UPPER            TRUE

static void gst_compare_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_compare_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_compare_reset (GstCompare * overlay);

static GstCaps *gst_compare_getcaps (GstPad * pad);
static GstFlowReturn gst_compare_collect_pads (GstCollectPads * cpads,
    GstCompare * comp);

static GstStateChangeReturn gst_compare_change_state (GstElement * element,
    GstStateChange transition);

GST_BOILERPLATE (GstCompare, gst_compare, GstElement, GST_TYPE_ELEMENT);


static void
gst_compare_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class,
      &check_sink_factory);
  gst_element_class_set_details_simple (element_class, "Compare buffers",
      "Filter/Debug", "Compares incoming buffers",
      "Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>");
}

static void
gst_compare_finalize (GObject * object)
{
  GstCompare *comp = GST_COMPARE (object);

  gst_object_unref (comp->cpads);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_compare_class_init (GstCompareClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG_CATEGORY_INIT (compare_debug, "compare", 0, "Compare buffers");

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_compare_change_state);

  gobject_class->set_property = gst_compare_set_property;
  gobject_class->get_property = gst_compare_get_property;
  gobject_class->finalize = gst_compare_finalize;

  g_object_class_install_property (gobject_class, PROP_META,
      g_param_spec_flags ("meta", "Compare Meta",
          "Indicates which metadata should be compared",
          gst_buffer_copy_flags_get_type (), DEFAULT_META,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OFFSET_TS,
      g_param_spec_boolean ("offset-ts", "Offsets Timestamps",
          "Consider OFFSET and OFFSET_END part of timestamp metadata",
          DEFAULT_OFFSET_TS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "Content Compare Method",
          "Method to compare buffer content",
          GST_COMPARE_METHOD_TYPE, DEFAULT_METHOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_double ("threshold", "Content Threshold",
          "Threshold beyond which to consider content different as determined by content-method",
          0, G_MAXDOUBLE, DEFAULT_THRESHOLD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_UPPER,
      g_param_spec_boolean ("upper", "Threshold Upper Bound",
          "Whether threshold value is upper bound or lower bound for difference measure",
          DEFAULT_UPPER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_compare_init (GstCompare * comp, GstCompareClass * klass)
{
  comp->cpads = gst_collect_pads_new ();
  gst_collect_pads_set_function (comp->cpads,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_compare_collect_pads),
      comp);

  comp->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_getcaps_function (comp->sinkpad, gst_compare_getcaps);
  gst_element_add_pad (GST_ELEMENT (comp), comp->sinkpad);

  comp->checkpad =
      gst_pad_new_from_static_template (&check_sink_factory, "check");
  gst_pad_set_getcaps_function (comp->checkpad, gst_compare_getcaps);
  gst_element_add_pad (GST_ELEMENT (comp), comp->checkpad);

  gst_collect_pads_add_pad_full (comp->cpads, comp->sinkpad,
      sizeof (GstCollectData), NULL);
  gst_collect_pads_add_pad_full (comp->cpads, comp->checkpad,
      sizeof (GstCollectData), NULL);

  comp->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (comp->srcpad, gst_compare_getcaps);
  gst_element_add_pad (GST_ELEMENT (comp), comp->srcpad);

  /* init properties */
  comp->meta = DEFAULT_META;
  comp->offset_ts = DEFAULT_OFFSET_TS;
  comp->method = DEFAULT_METHOD;
  comp->threshold = DEFAULT_THRESHOLD;
  comp->upper = DEFAULT_UPPER;

  gst_compare_reset (comp);
}

static void
gst_compare_reset (GstCompare * comp)
{
}

static GstCaps *
gst_compare_getcaps (GstPad * pad)
{
  GstCompare *comp;
  GstPad *otherpad;
  GstCaps *result;

  comp = GST_COMPARE (gst_pad_get_parent (pad));
  if (G_UNLIKELY (comp == NULL))
    return gst_caps_new_any ();

  otherpad = (pad == comp->srcpad ? comp->sinkpad : comp->srcpad);
  result = gst_pad_peer_get_caps (otherpad);
  if (result == NULL)
    result = gst_caps_new_any ();

  gst_object_unref (comp);

  return result;
}

static void
gst_compare_meta (GstCompare * comp, GstBuffer * buf1, GstBuffer * buf2)
{
  gint flags = 0;

  if (comp->meta & GST_BUFFER_COPY_FLAGS) {
    if (GST_BUFFER_FLAGS (buf1) != GST_BUFFER_FLAGS (buf2)) {
      flags |= GST_BUFFER_COPY_FLAGS;
      GST_DEBUG_OBJECT (comp, "flags %d != flags %d", GST_BUFFER_FLAGS (buf1),
          GST_BUFFER_FLAGS (buf2));
    }
  }
  if (comp->meta & GST_BUFFER_COPY_TIMESTAMPS) {
    if (GST_BUFFER_TIMESTAMP (buf1) != GST_BUFFER_TIMESTAMP (buf2)) {
      flags |= GST_BUFFER_COPY_TIMESTAMPS;
      GST_DEBUG_OBJECT (comp,
          "ts %" GST_TIME_FORMAT " != ts %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf1)),
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf2)));
    }
    if (GST_BUFFER_DURATION (buf1) != GST_BUFFER_DURATION (buf2)) {
      flags |= GST_BUFFER_COPY_TIMESTAMPS;
      GST_DEBUG_OBJECT (comp,
          "dur %" GST_TIME_FORMAT " != dur %" GST_TIME_FORMAT,
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf1)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (buf2)));
    }
    if (comp->offset_ts) {
      if (GST_BUFFER_OFFSET (buf1) != GST_BUFFER_OFFSET (buf2)) {
        flags |= GST_BUFFER_COPY_TIMESTAMPS;
        GST_DEBUG_OBJECT (comp,
            "offset %" G_GINT64_FORMAT " != offset %" G_GINT64_FORMAT,
            GST_BUFFER_OFFSET (buf1), GST_BUFFER_OFFSET (buf2));
      }
      if (GST_BUFFER_OFFSET_END (buf1) != GST_BUFFER_OFFSET_END (buf2)) {
        flags |= GST_BUFFER_COPY_TIMESTAMPS;
        GST_DEBUG_OBJECT (comp,
            "offset_end %" G_GINT64_FORMAT " != offset_end %" G_GINT64_FORMAT,
            GST_BUFFER_OFFSET_END (buf1), GST_BUFFER_OFFSET_END (buf2));
      }
    }
  }
  if (comp->meta & GST_BUFFER_COPY_CAPS) {
    if (!gst_caps_is_equal (GST_BUFFER_CAPS (buf1), GST_BUFFER_CAPS (buf2))) {
      flags |= GST_BUFFER_COPY_CAPS;
      GST_DEBUG_OBJECT (comp,
          "caps %" GST_PTR_FORMAT " != caps %" GST_PTR_FORMAT,
          GST_BUFFER_CAPS (buf1), GST_BUFFER_CAPS (buf2));
    }
  }

  /* signal mismatch by debug and message */
  if (flags) {
    GST_WARNING_OBJECT (comp, "buffers %p and %p failed metadata match %d",
        buf1, buf2, flags);

    gst_element_post_message (GST_ELEMENT (comp),
        gst_message_new_element (GST_OBJECT (comp),
            gst_structure_new ("delta", "meta", G_TYPE_INT, flags, NULL)));
  }
}

/* when comparing contents, it is already ensured sizes are equal */

static gint
gst_compare_mem (GstCompare * comp, GstBuffer * buf1, GstBuffer * buf2)
{
  return memcmp (GST_BUFFER_DATA (buf1), GST_BUFFER_DATA (buf2),
      GST_BUFFER_SIZE (buf1)) ? 1 : 0;
}

static gint
gst_compare_max (GstCompare * comp, GstBuffer * buf1, GstBuffer * buf2)
{
  gint i, delta = 0;
  gint8 *data1, *data2;

  data1 = (gint8 *) GST_BUFFER_DATA (buf1);
  data2 = (gint8 *) GST_BUFFER_DATA (buf2);

  /* primitive loop */
  for (i = 0; i < GST_BUFFER_SIZE (buf1); i++) {
    gint diff = ABS (*data1 - *data2);
    if (diff > 0)
      GST_LOG_OBJECT (comp, "diff at %d = %d", i, diff);
    delta = MAX (delta, ABS (*data1 - *data2));
    data1++;
    data2++;
  }

  return delta;
}

static double
gst_compare_ssim_window (GstCompare * comp, guint8 * data1, guint8 * data2,
    gint width, gint height, gint step, gint stride)
{
  gint count = 0, i, j;
  gint sum1 = 0, sum2 = 0, ssum1 = 0, ssum2 = 0, acov = 0;
  gdouble avg1, avg2, var1, var2, cov;

  const gdouble k1 = 0.01;
  const gdouble k2 = 0.03;
  const gdouble L = 255.0;
  const gdouble c1 = (k1 * L) * (k1 * L);
  const gdouble c2 = (k2 * L) * (k2 * L);

  /* plain and simple; no fancy optimizations */
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      sum1 += *data1;
      sum2 += *data2;
      ssum1 += *data1 * *data1;
      ssum2 += *data2 * *data2;
      acov += *data1 * *data2;
      count++;
      data1 += step;
      data2 += step;
    }
    data1 -= j * step;
    data2 -= j * step;
    data1 += stride;
    data2 += stride;
  }

  avg1 = sum1 / count;
  avg2 = sum2 / count;
  var1 = ssum1 / count - avg1 * avg1;
  var2 = ssum2 / count - avg2 * avg2;
  cov = acov / count - avg1 * avg2;

  return (2 * avg1 * avg2 + c1) * (2 * cov + c2) /
      ((avg1 * avg1 + avg2 * avg2 + c1) * (var1 + var2 + c2));
}

/* @width etc are for the particular component */
static gdouble
gst_compare_ssim_component (GstCompare * comp, guint8 * data1, guint8 * data2,
    gint width, gint height, gint step, gint stride)
{
  const gint window = 16;
  gdouble ssim_sum = 0;
  gint count = 0, i, j;

  for (j = 0; j + (window / 2) < height; j += (window / 2)) {
    for (i = 0; i + (window / 2) < width; i += (window / 2)) {
      gdouble ssim;

      ssim = gst_compare_ssim_window (comp, data1 + step * i + j * stride,
          data2 + step * i + j * stride,
          MIN (window, width - i), MIN (window, height - j), step, stride);
      GST_LOG_OBJECT (comp, "ssim for %dx%d at (%d, %d) = %f", window, window,
          i, j, ssim);
      ssim_sum += ssim;
      count++;
    }
  }

  return (ssim_sum / count);
}

static gdouble
gst_compare_ssim (GstCompare * comp, GstBuffer * buf1, GstBuffer * buf2)
{
  GstCaps *caps;
  GstVideoFormat format, f;
  gint width, height, w, h, i, comps;
  gdouble cssim[4], ssim, c[4] = { 1.0, 0.0, 0.0, 0.0 };
  guint8 *data1, *data2;

  caps = GST_BUFFER_CAPS (buf1);
  if (!caps)
    goto invalid_input;

  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    goto invalid_input;

  caps = GST_BUFFER_CAPS (buf2);
  if (!caps)
    goto invalid_input;

  if (!gst_video_format_parse_caps (caps, &f, &w, &h))
    goto invalid_input;

  if (f != format || w != width || h != height)
    return comp->threshold + 1;

  comps = gst_video_format_is_gray (format) ? 1 : 3;
  if (gst_video_format_has_alpha (format))
    comps += 1;

  /* note that some are reported both yuv and gray */
  for (i = 0; i < comps; ++i)
    c[i] = 1.0;
  /* increase luma weight if yuv */
  if (gst_video_format_is_yuv (format) && (comps > 1))
    c[0] = comps - 1;
  for (i = 0; i < comps; ++i)
    c[i] /= (gst_video_format_is_yuv (format) && (comps > 1)) ?
        2 * (comps - 1) : comps;

  data1 = GST_BUFFER_DATA (buf1);
  data2 = GST_BUFFER_DATA (buf2);
  for (i = 0; i < comps; i++) {
    gint offset, cw, ch, step, stride;

    /* only support most common formats */
    if (gst_video_format_get_component_depth (format, i) != 8)
      goto unsupported_input;
    offset = gst_video_format_get_component_offset (format, i, width, height);
    cw = gst_video_format_get_component_width (format, i, width);
    ch = gst_video_format_get_component_height (format, i, height);
    step = gst_video_format_get_pixel_stride (format, i);
    stride = gst_video_format_get_row_stride (format, i, width);

    GST_LOG_OBJECT (comp, "component %d", i);
    cssim[i] = gst_compare_ssim_component (comp, data1 + offset, data2 + offset,
        cw, ch, step, stride);
    GST_LOG_OBJECT (comp, "ssim[%d] = %f", i, cssim[i]);
  }

#ifndef GST_DISABLE_GST_DEBUG
  for (i = 0; i < 4; i++) {
    GST_DEBUG_OBJECT (comp, "ssim[%d] = %f, c[%d] = %f", i, cssim[i], i, c[i]);
  }
#endif

  ssim = cssim[0] * c[0] + cssim[1] * c[1] + cssim[2] * c[2] + cssim[3] * c[3];

  return ssim;

  /* ERRORS */
invalid_input:
  {
    GST_ERROR_OBJECT (comp, "ssim method needs raw video input");
    return 0;
  }
unsupported_input:
  {
    GST_ERROR_OBJECT (comp, "raw video format not supported %" GST_PTR_FORMAT,
        caps);
    return 0;
  }
}

static void
gst_compare_buffers (GstCompare * comp, GstBuffer * buf1, GstBuffer * buf2)
{
  gdouble delta = 0;

  /* first check metadata */
  gst_compare_meta (comp, buf1, buf2);

  /* check content according to method */
  /* but at least size should match */
  if (GST_BUFFER_SIZE (buf1) != GST_BUFFER_SIZE (buf2)) {
    delta = comp->threshold + 1;
  } else {
    GST_MEMDUMP_OBJECT (comp, "buffer 1", GST_BUFFER_DATA (buf1),
        GST_BUFFER_SIZE (buf1));
    GST_MEMDUMP_OBJECT (comp, "buffer 2", GST_BUFFER_DATA (buf2),
        GST_BUFFER_SIZE (buf2));
    switch (comp->method) {
      case GST_COMPARE_METHOD_MEM:
        delta = gst_compare_mem (comp, buf1, buf2);
        break;
      case GST_COMPARE_METHOD_MAX:
        delta = gst_compare_max (comp, buf1, buf2);
        break;
      case GST_COMPARE_METHOD_SSIM:
        delta = gst_compare_ssim (comp, buf1, buf2);
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  if ((comp->upper && delta > comp->threshold) ||
      (!comp->upper && delta < comp->threshold)) {
    GST_WARNING_OBJECT (comp, "buffers %p and %p failed content match %f",
        buf1, buf2, delta);

    gst_element_post_message (GST_ELEMENT (comp),
        gst_message_new_element (GST_OBJECT (comp),
            gst_structure_new ("delta", "content", G_TYPE_DOUBLE, delta,
                NULL)));
  }
}

static GstFlowReturn
gst_compare_collect_pads (GstCollectPads * cpads, GstCompare * comp)
{
  GstBuffer *buf1, *buf2;

  buf1 = gst_collect_pads_pop (comp->cpads,
      gst_pad_get_element_private (comp->sinkpad));

  buf2 = gst_collect_pads_pop (comp->cpads,
      gst_pad_get_element_private (comp->checkpad));

  if (!buf1 && !buf2) {
    gst_pad_push_event (comp->srcpad, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  } else if (buf1 && buf2) {
    gst_compare_buffers (comp, buf1, buf2);
  } else {
    GST_WARNING_OBJECT (comp, "buffer %p != NULL", buf1 ? buf1 : buf2);

    comp->count++;
    gst_element_post_message (GST_ELEMENT (comp),
        gst_message_new_element (GST_OBJECT (comp),
            gst_structure_new ("delta", "count", G_TYPE_INT, comp->count,
                NULL)));
  }

  if (buf1)
    gst_pad_push (comp->srcpad, buf1);

  if (buf2)
    gst_buffer_unref (buf2);

  return GST_FLOW_OK;
}

static void
gst_compare_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCompare *comp = GST_COMPARE (object);

  switch (prop_id) {
    case PROP_META:
      comp->meta = g_value_get_flags (value);
      break;
    case PROP_OFFSET_TS:
      comp->offset_ts = g_value_get_boolean (value);
      break;
    case PROP_METHOD:
      comp->method = g_value_get_enum (value);
      break;
    case PROP_THRESHOLD:
      comp->threshold = g_value_get_double (value);
      break;
    case PROP_UPPER:
      comp->upper = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_compare_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCompare *comp = GST_COMPARE (object);

  switch (prop_id) {
    case PROP_META:
      g_value_set_flags (value, comp->meta);
      break;
    case PROP_OFFSET_TS:
      g_value_set_boolean (value, comp->offset_ts);
      break;
    case PROP_METHOD:
      g_value_set_enum (value, comp->method);
      break;
    case PROP_THRESHOLD:
      g_value_set_double (value, comp->threshold);
      break;
    case PROP_UPPER:
      g_value_set_boolean (value, comp->upper);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_compare_change_state (GstElement * element, GstStateChange transition)
{
  GstCompare *comp = GST_COMPARE (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (comp->cpads);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (comp->cpads);
      break;
    default:
      break;
  }

  ret = GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element, transition), GST_STATE_CHANGE_SUCCESS);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_compare_reset (comp);
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}
