/*
 * GStreamer
 * Copyright (C) 2026 Vivia Nikolaidou <vivia@ahiru.eu>
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
 * SECTION:element-vips-smartcrop
 *
 * Smart-crops a video into an area of the requested size by removing the
 * least interesting parts, using libvips.
 *
 * The output width and height are negotiated from the downstream caps,
 * like a capsfilter: when the downstream does not request a specific size,
 * the element negotiates the same size as the input and passes the buffers
 * through unchanged.
 *
 * When cropping, the output buffer is a view into the input buffer: no
 * pixel data is copied, the cropped area is described by the video metadata
 * with the input stride.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvipssmartcrop.h"
#include "gstvipsimage.h"

#include <gst/video/gstvideometa.h>

GST_DEBUG_CATEGORY_STATIC (gst_vips_smartcrop_debug);
#define GST_CAT_DEFAULT gst_vips_smartcrop_debug
#define gst_vips_smartcrop_parent_class parent_class

#define DEFAULT_PROP_INTERESTING GST_VIPS_INTERESTING_ATTENTION

enum
{
  PROP_0,
  PROP_INTERESTING,
  PROP_ATTENTION_X,
  PROP_ATTENTION_Y,
};

static void gst_vips_smartcrop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vips_smartcrop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_vips_smartcrop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_vips_smartcrop_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_vips_smartcrop_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_vips_smartcrop_transform_frame (GstVideoFilter *
    filter, GstVideoFrame * in_frame, GstVideoFrame * out_frame);

static GstFlowReturn gst_vips_smartcrop_prepare_output_buffer (GstBaseTransform
    * trans, GstBuffer * inbuf, GstBuffer ** outbuf);
static GstFlowReturn gst_vips_smartcrop_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_vips_smartcrop_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);

static GstBaseTransformClass *parent_btransform_class;

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIPS_VIDEO_CAPS)
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIPS_VIDEO_CAPS)
    );

G_DEFINE_TYPE (GstVipsSmartcrop, gst_vips_smartcrop, GST_TYPE_VIDEO_FILTER);

GST_ELEMENT_REGISTER_DEFINE (vips_smartcrop, "vips-smartcrop",
    GST_RANK_NONE, gst_vips_smartcrop_get_type ());

/* The maximum value of a width or height field, which may be fixed or a
 * range. */
static gint
gst_vips_smartcrop_get_size_max (const GValue * value)
{
  if (G_VALUE_HOLDS_INT (value))
    return g_value_get_int (value);
  if (GST_VALUE_HOLDS_INT_RANGE (value))
    return gst_value_get_int_range_max (value);

  return G_MAXINT;
}

static GstCaps *
gst_vips_smartcrop_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (trans);
  GstCaps *result;
  guint i;

  GST_LOG_OBJECT (self, "transforming caps %" GST_PTR_FORMAT, caps);

  result = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (result); ++i) {
    GstStructure *structure = gst_caps_get_structure (result, i);

    if (direction == GST_PAD_SINK) {
      /* The input caps are given: the output size may be anything up to
       * the input size. */
      gst_structure_set (structure,
          "width", GST_TYPE_INT_RANGE, 1,
          gst_vips_smartcrop_get_size_max (gst_structure_get_value (structure,
                  "width")),
          "height", GST_TYPE_INT_RANGE, 1,
          gst_vips_smartcrop_get_size_max (gst_structure_get_value (structure,
                  "height")), NULL);
    } else {
      /* The output caps are given: the input size may be anything. */
      gst_structure_set (structure,
          "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    }
  }

  if (filter) {
    GstCaps *intersected =
        gst_caps_intersect_full (filter, result, GST_CAPS_INTERSECT_FIRST);
    gst_caps_replace (&result, intersected);
    gst_caps_unref (intersected);
  }

  GST_LOG_OBJECT (self, "transformed caps to %" GST_PTR_FORMAT, result);

  return result;
}

static GstCaps *
gst_vips_smartcrop_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  (void) direction;

  /* @caps is the (fixed) caps of the other pad. When it fixes the size,
   * fixate to that size so that the element stays in passthrough whenever
   * only one side requests a specific size. */
  if (othercaps && gst_caps_get_size (caps) > 0
      && gst_caps_get_size (othercaps) > 0) {
    GstStructure *caps_structure = gst_caps_get_structure (caps, 0);
    GstStructure *structure = gst_caps_get_structure (othercaps, 0);
    const GValue *width, *height;
    const GValue *other_width, *other_height;

    other_width = gst_structure_get_value (structure, "width");
    other_height = gst_structure_get_value (structure, "height");

    if (!G_VALUE_HOLDS_INT (other_width)) {
      width = gst_structure_get_value (caps_structure, "width");
      if (G_VALUE_HOLDS_INT (width))
        gst_structure_set_value (structure, "width", width);
    }

    if (!G_VALUE_HOLDS_INT (other_height)) {
      height = gst_structure_get_value (caps_structure, "height");
      if (G_VALUE_HOLDS_INT (height))
        gst_structure_set_value (structure, "height", height);
    }
  }

  if (othercaps && !gst_caps_is_fixed (othercaps)) {
    GstCaps *fixated = gst_caps_fixate (othercaps);
    gst_caps_replace (&othercaps, fixated);
  }

  return othercaps;
}

static gboolean
gst_vips_smartcrop_set_info (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (filter);

  if (G_UNLIKELY (GST_VIDEO_INFO_WIDTH (out_info) >
          GST_VIDEO_INFO_WIDTH (in_info) ||
          GST_VIDEO_INFO_HEIGHT (out_info) > GST_VIDEO_INFO_HEIGHT (in_info))) {
    GST_ERROR_OBJECT (self, "output size %ux%u is larger than input size "
        "%ux%u",
        (guint) GST_VIDEO_INFO_WIDTH (out_info),
        (guint) GST_VIDEO_INFO_HEIGHT (out_info),
        (guint) GST_VIDEO_INFO_WIDTH (in_info),
        (guint) GST_VIDEO_INFO_HEIGHT (in_info));
    return FALSE;
  }

  GST_LOG_OBJECT (self, "incaps = %" GST_PTR_FORMAT ", outcaps = %"
      GST_PTR_FORMAT, incaps, outcaps);

  GST_OBJECT_LOCK (self);
  self->attention_x = 0;
  self->attention_y = 0;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}


static GstFlowReturn
gst_vips_smartcrop_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (filter);
  GstVipsInteresting interesting;
  VipsImage *in = NULL, *out = NULL;
  VipsOperation *op;
  gint attention_x = 0, attention_y = 0;

  GError *error = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_OBJECT_LOCK (self);
  interesting = self->interesting;
  GST_OBJECT_UNLOCK (self);

  if (!gst_vips_image_new_from_frame (in_frame, &in, &error)) {
    GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
        ("failed to create vips image: %s", error->message));
    g_clear_error (&error);
    return GST_FLOW_ERROR;
  }

  /* The VIPS_TYPE_SMARTCROP type is not publicly exported by libvips, so
   * the operation is created by name. */
  op = vips_operation_new ("smartcrop");
  if (G_UNLIKELY (op == NULL))
    goto vips_error;

  g_object_set (op,
      "input", in,
      "width", GST_VIDEO_FRAME_WIDTH (out_frame),
      "height", GST_VIDEO_FRAME_HEIGHT (out_frame),
      "interesting", (VipsInteresting) interesting,
      /* TODO: derive from the video info (premultiplied alpha). */
      "premultiplied", FALSE, NULL);

  if (G_UNLIKELY (vips_object_build (VIPS_OBJECT (op))))
    goto vips_error;

  g_object_get (op,
      "out", &out,
      "attention_x", &attention_x, "attention_y", &attention_y, NULL);

  if (G_UNLIKELY (out == NULL))
    goto cleanup;

  if (!gst_vips_image_copy_to_frame (out, out_frame, &error)) {
    GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
        ("failed to copy vips image to output frame: %s", error->message));
    g_clear_error (&error);
    goto cleanup;
  }

  GST_OBJECT_LOCK (self);
  self->attention_x = attention_x;
  self->attention_y = attention_y;
  GST_OBJECT_UNLOCK (self);

  GST_DEBUG_OBJECT (self, "smart cropped to %ux%u, attention at %d,%d",
      (guint) GST_VIDEO_FRAME_WIDTH (out_frame),
      (guint) GST_VIDEO_FRAME_HEIGHT (out_frame), attention_x, attention_y);

  goto cleanup;

  /* ERRORS */
vips_error:
  {
    GST_ELEMENT_ERROR (self, CORE, FAILED, (NULL),
        ("libvips error: %s", vips_error_buffer ()));
    vips_error_clear ();
    ret = GST_FLOW_ERROR;
    goto cleanup;
  }

cleanup:
  {
    if (op) {
      vips_object_unref_outputs (VIPS_OBJECT (op));
      g_object_unref (op);
    }
    if (out)
      g_object_unref (out);
    g_object_unref (in);

    return ret;
  }
}

static gboolean
gst_vips_smartcrop_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (trans);

  /* The zero-copy crop adds a video meta to describe the stride of the
   * input memory; it can only be used when downstream supports it. */
  self->downstream_video_meta =
      gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  GST_DEBUG_OBJECT (self, "downstream %s video meta",
      self->downstream_video_meta ? "supports" : "does not support");

  return parent_btransform_class->decide_allocation (trans, query);
}

static GstFlowReturn
gst_vips_smartcrop_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (trans);
  GstVideoFilter *filter = GST_VIDEO_FILTER (trans);
  const GstVideoInfo *in_info = &filter->in_info;
  const GstVideoInfo *out_info = &filter->out_info;
  const GstVideoFormat format = GST_VIDEO_INFO_FORMAT (out_info);
  GstVipsInteresting interesting;
  guint bands;
  guint in_width, in_height, out_width, out_height;
  gsize in_stride, offset, size;
  guint mem_idx, mem_len;
  gsize mem_skip;
  int left, top;
  int attention_x = 0, attention_y = 0;
  VipsImage *in = NULL;
  VipsOperation *op = NULL;
  GstBuffer *view = NULL;
  GstFlowReturn ret = GST_FLOW_OK;

  self->zero_copy = FALSE;

  /* The zero-copy crop needs a video meta on the output buffer to describe
   * the stride of the input memory, so it also needs the downstream to
   * support video meta. */
  if (G_UNLIKELY (!filter->negotiated || !self->downstream_video_meta
          || gst_base_transform_is_passthrough (trans)))
    goto parent_prepare;

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
      bands = 3;
      break;
    case GST_VIDEO_FORMAT_RGBA:
      bands = 4;
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      bands = 1;
      break;
    default:
      goto parent_prepare;
  }

  GST_OBJECT_LOCK (self);
  interesting = self->interesting;
  GST_OBJECT_UNLOCK (self);

  /* The crop position of the entropy mode is only available together with
   * the generated pixels: it needs the copying transform path. */
  if (G_UNLIKELY (interesting == GST_VIPS_INTERESTING_ENTROPY))
    goto parent_prepare;

  in_width = GST_VIDEO_INFO_WIDTH (in_info);
  in_height = GST_VIDEO_INFO_HEIGHT (in_info);
  out_width = GST_VIDEO_INFO_WIDTH (out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (out_info);
  in_stride = GST_VIDEO_INFO_PLANE_STRIDE (in_info, 0);

  switch (interesting) {
    case GST_VIPS_INTERESTING_NONE:
    case GST_VIPS_INTERESTING_LOW:
      left = 0;
      top = 0;
      break;

    case GST_VIPS_INTERESTING_CENTRE:
      left = (in_width - out_width) / 2;
      top = (in_height - out_height) / 2;
      break;

    case GST_VIPS_INTERESTING_HIGH:
      left = in_width - out_width;
      top = in_height - out_height;
      break;

    case GST_VIPS_INTERESTING_ATTENTION:
    {
      GstVideoFrame frame;
      GError *error = NULL;
      gboolean mapped = FALSE;

      /* Only the attention score is computed: the smartcrop output stays
       * lazy and is never generated. */
      if (G_UNLIKELY (!gst_video_frame_map (&frame, in_info, inbuf,
                  GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF)))
        goto attention_failed;

      mapped = TRUE;

      if (G_UNLIKELY (!gst_vips_image_new_from_frame (&frame, &in, &error))) {
        g_clear_error (&error);
        goto attention_failed;
      }

      op = vips_operation_new ("smartcrop");
      g_object_set (op,
          "input", in,
          "width", out_width,
          "height", out_height, "interesting", (VipsInteresting) interesting,
          /* TODO: derive from the video info (premultiplied alpha). */
          "premultiplied", FALSE, NULL);

      if (G_UNLIKELY (op == NULL || vips_object_build (VIPS_OBJECT (op))))
        goto attention_failed;

      g_object_get (op,
          "attention_x", &attention_x, "attention_y", &attention_y, NULL);

      gst_video_frame_unmap (&frame);
      mapped = FALSE;

      /* Centre the crop over the attention point */
      left = MAX (0, MIN (attention_x - (int) out_width / 2,
              (int) in_width - (int) out_width));
      top = MAX (0, MIN (attention_y - (int) out_height / 2,
              (int) in_height - (int) out_height));

      break;

    attention_failed:
      if (mapped)
        gst_video_frame_unmap (&frame);
      goto parent_prepare;
    }

    default:
      goto parent_prepare;
  }

  offset = (gsize) top *in_stride + (gsize) left *bands;
  size = (gsize) (out_height - 1) * in_stride + (gsize) out_width *bands;

  if (G_UNLIKELY (!gst_buffer_find_memory (inbuf, offset, size, &mem_idx,
              &mem_len, &mem_skip) || mem_len != 1))
    goto parent_prepare;

  view = gst_buffer_copy_region (inbuf, GST_BUFFER_COPY_MEMORY, offset, size);
  if (G_UNLIKELY (view == NULL))
    goto parent_prepare;

  if (G_UNLIKELY (!gst_buffer_copy_into (view, inbuf, GST_BUFFER_COPY_METADATA,
              0, -1))) {
    gst_buffer_unref (view);
    view = NULL;
    goto parent_prepare;
  }

  g_assert (!gst_buffer_get_video_meta (view));

  {
    const gsize meta_offset[4] = { 0, 0, 0, 0 };
    const gint meta_stride[4] = { (gint) in_stride, 0, 0, 0 };

    gst_buffer_add_video_meta_full (view, 0, format, out_width, out_height,
        1, meta_offset, meta_stride);
  }

  GST_OBJECT_LOCK (self);
  self->attention_x = attention_x;
  self->attention_y = attention_y;
  GST_OBJECT_UNLOCK (self);

  self->zero_copy = TRUE;
  *outbuf = view;
  view = NULL;

  GST_DEBUG_OBJECT (self, "cropped %ux%u at %d,%d of %ux%u without copying, "
      "attention %d,%d (interesting %d)",
      out_width, out_height, left, top, in_width, in_height,
      attention_x, attention_y, interesting);

  goto cleanup;

  /* FALLBACK */
parent_prepare:
  ret = parent_btransform_class->prepare_output_buffer (trans, inbuf, outbuf);

cleanup:
  if (op) {
    vips_object_unref_outputs (VIPS_OBJECT (op));
    g_object_unref (op);
  }
  if (in)
    g_object_unref (in);
  if (view)
    gst_buffer_unref (view);

  return ret;
}

static GstFlowReturn
gst_vips_smartcrop_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (trans);

  if (self->zero_copy) {
    /* The output buffer already references the cropped area of the input
     * buffer; there is nothing to transform. */
    return GST_FLOW_OK;
  }

  return parent_btransform_class->transform (trans, inbuf, outbuf);
}

static void
gst_vips_smartcrop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (object);

  switch (prop_id) {
    case PROP_INTERESTING:
      GST_OBJECT_LOCK (self);
      self->interesting = g_value_get_enum (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vips_smartcrop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVipsSmartcrop *self = GST_VIPS_SMARTCROP (object);

  switch (prop_id) {
    case PROP_INTERESTING:
      GST_OBJECT_LOCK (self);
      g_value_set_enum (value, self->interesting);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_ATTENTION_X:
      GST_OBJECT_LOCK (self);
      g_value_set_int (value, self->attention_x);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_ATTENTION_Y:
      GST_OBJECT_LOCK (self);
      g_value_set_int (value, self->attention_y);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vips_smartcrop_class_init (GstVipsSmartcropClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *basetransform_class;
  GstVideoFilterClass *vfilter_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  basetransform_class = (GstBaseTransformClass *) klass;
  vfilter_class = (GstVideoFilterClass *) klass;

  parent_btransform_class = GST_BASE_TRANSFORM_CLASS (parent_class);

  gobject_class->set_property = gst_vips_smartcrop_set_property;
  gobject_class->get_property = gst_vips_smartcrop_get_property;

  g_object_class_install_property (gobject_class, PROP_INTERESTING,
      g_param_spec_enum ("interesting", "Interesting",
          "How to measure interestingness when smart-cropping",
          GST_TYPE_VIPS_INTERESTING, DEFAULT_PROP_INTERESTING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));
  g_object_class_install_property (gobject_class, PROP_ATTENTION_X,
      g_param_spec_int ("attention-x", "Attention X",
          "Horizontal position of the centre of attention of the last frame",
          0, G_MAXINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ATTENTION_Y,
      g_param_spec_int ("attention-y", "Attention Y",
          "Vertical position of the centre of attention of the last frame",
          0, G_MAXINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class, "Vips smart crop",
      "Filter/Effect/Video",
      "Smart-crops video using libvips", "Vivia Nikolaidou <vivia@ahiru.eu>");
  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  basetransform_class->passthrough_on_same_caps = TRUE;
  basetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_vips_smartcrop_transform_caps);
  basetransform_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_vips_smartcrop_fixate_caps);
  basetransform_class->prepare_output_buffer =
      GST_DEBUG_FUNCPTR (gst_vips_smartcrop_prepare_output_buffer);
  basetransform_class->transform =
      GST_DEBUG_FUNCPTR (gst_vips_smartcrop_transform);
  basetransform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_vips_smartcrop_decide_allocation);

  vfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_vips_smartcrop_set_info);
  vfilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_vips_smartcrop_transform_frame);
}

static void
gst_vips_smartcrop_init (GstVipsSmartcrop * self)
{
  GST_DEBUG_CATEGORY_INIT (gst_vips_smartcrop_debug, "vips-smartcrop", 0,
      "vips-smartcrop element");

  self->interesting = DEFAULT_PROP_INTERESTING;
}
