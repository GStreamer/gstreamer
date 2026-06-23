/* GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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
 * SECTION:element-cudacompositor
 * @title: cudacompositor
 *
 * A CUDA based video compositing element.
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 cudacompositor name=c ! cudadownload ! autovideosink \
 *     videotestsrc ! video/x-raw,width=320,height=240 ! cudaupload ! c. \
 *     videotestsrc pattern=ball ! video/x-raw,width=100,height=100 ! cudaupload ! c.
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/cuda/gstcuda-private.h>
#include "gstcudacompositor.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_compositor_debug);
#define GST_CAT_DEFAULT gst_cuda_compositor_debug

enum GstCudaCompositorOperator
{
  GST_CUDA_COMPOSITOR_OPERATOR_SOURCE,
  GST_CUDA_COMPOSITOR_OPERATOR_OVER,
};

/**
 * GstCudaCompositorOperator:
 *
 * Since: 1.26
 */
#define GST_TYPE_CUDA_COMPOSITOR_OPERATOR (gst_cuda_compositor_operator_get_type())
static GType
gst_cuda_compositor_operator_get_type (void)
{
  static GType compositor_operator_type = 0;
  static const GEnumValue compositor_operator[] = {
    {GST_CUDA_COMPOSITOR_OPERATOR_SOURCE, "Source", "source"},
    {GST_CUDA_COMPOSITOR_OPERATOR_OVER, "Over", "over"},
    {0, nullptr, nullptr},
  };

  GST_CUDA_CALL_ONCE_BEGIN {
    compositor_operator_type =
        g_enum_register_static ("GstCudaCompositorOperator",
        compositor_operator);
  } GST_CUDA_CALL_ONCE_END;

  return compositor_operator_type;
}

enum GstCudaCompositorSizingPolicy
{
  GST_CUDA_COMPOSITOR_SIZING_POLICY_NONE,
  GST_CUDA_COMPOSITOR_SIZING_POLICY_KEEP_ASPECT_RATIO,
};

/**
 * GstCudaCompositorSizingPolicy:
 *
 * Since: 1.26
 */
#define GST_TYPE_CUDA_COMPOSITOR_SIZING_POLICY (gst_cuda_compositor_sizing_policy_get_type())
static GType
gst_cuda_compositor_sizing_policy_get_type (void)
{
  static GType sizing_policy_type = 0;

  static const GEnumValue sizing_polices[] = {
    {GST_CUDA_COMPOSITOR_SIZING_POLICY_NONE,
        "None: Image is scaled to fill configured destination rectangle without "
          "padding or keeping the aspect ratio", "none"},
    {GST_CUDA_COMPOSITOR_SIZING_POLICY_KEEP_ASPECT_RATIO,
          "Keep Aspect Ratio: Image is scaled to fit destination rectangle "
          "specified by GstD3D12CompositorPad:{xpos, ypos, width, height} "
          "with preserved aspect ratio. Resulting image will be centered in "
          "the destination rectangle with padding if necessary",
        "keep-aspect-ratio"},
    {0, nullptr, nullptr},
  };

  GST_CUDA_CALL_ONCE_BEGIN {
    sizing_policy_type =
        g_enum_register_static ("GstCudaCompositorSizingPolicy",
        sizing_polices);
  } GST_CUDA_CALL_ONCE_END;

  return sizing_policy_type;
}

enum
{
  PROP_PAD_0,
  PROP_PAD_XPOS,
  PROP_PAD_YPOS,
  PROP_PAD_WIDTH,
  PROP_PAD_HEIGHT,
  PROP_PAD_ALPHA,
  PROP_PAD_OPERATOR,
  PROP_PAD_SIZING_POLICY,
};

#define DEFAULT_PAD_XPOS   0
#define DEFAULT_PAD_YPOS   0
#define DEFAULT_PAD_WIDTH  0
#define DEFAULT_PAD_HEIGHT 0
#define DEFAULT_PAD_ALPHA  1.0
#define DEFAULT_PAD_OPERATOR GST_CUDA_COMPOSITOR_OPERATOR_OVER
#define DEFAULT_PAD_SIZING_POLICY GST_CUDA_COMPOSITOR_SIZING_POLICY_NONE

enum
{
  PROP_0,
  PROP_IGNORE_INACTIVE_PADS,
};

/* *INDENT-OFF* */
struct GstCudaCompositorPadPrivate
{
  ~GstCudaCompositorPadPrivate ()
  {
    gst_clear_object (&conv);
  }

  GstCudaConverter *conv = nullptr;

  gboolean config_updated = FALSE;

  std::recursive_mutex lock;

  /* properties */
  gint xpos = DEFAULT_PAD_XPOS;
  gint ypos = DEFAULT_PAD_YPOS;
  gint width = DEFAULT_PAD_WIDTH;
  gint height = DEFAULT_PAD_HEIGHT;
  gdouble alpha = DEFAULT_PAD_ALPHA;
  GstCudaCompositorOperator op = DEFAULT_PAD_OPERATOR;
  GstCudaCompositorSizingPolicy sizing_policy = DEFAULT_PAD_SIZING_POLICY;
};
/* *INDENT-ON* */

/**
 * GstCudaCompositorPad:
 *
 * Since: 1.26
 */
struct _GstCudaCompositorPad
{
  GstCudaAggregatorPad parent;

  GstCudaCompositorPadPrivate *priv;
};

struct _GstCudaCompositor
{
  GstCudaAggregator parent;
};

static void gst_cuda_compositor_pad_finalize (GObject * object);
static void gst_cuda_compositor_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cuda_compositor_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean
gst_cuda_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame);

#define gst_cuda_compositor_pad_parent_class parent_pad_class
G_DEFINE_TYPE (GstCudaCompositorPad, gst_cuda_compositor_pad,
    GST_TYPE_CUDA_AGGREGATOR_PAD);

static void
gst_cuda_compositor_pad_class_init (GstCudaCompositorPadClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto vagg_pad_class = GST_VIDEO_AGGREGATOR_PAD_CLASS (klass);
  auto param_flags = (GParamFlags)
      (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  object_class->finalize = gst_cuda_compositor_pad_finalize;
  object_class->set_property = gst_cuda_compositor_pad_set_property;
  object_class->get_property = gst_cuda_compositor_pad_get_property;

  g_object_class_install_property (object_class, PROP_PAD_XPOS,
      g_param_spec_int ("xpos", "X Position", "X position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_XPOS, param_flags));
  g_object_class_install_property (object_class, PROP_PAD_YPOS,
      g_param_spec_int ("ypos", "Y Position", "Y position of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_YPOS, param_flags));
  g_object_class_install_property (object_class, PROP_PAD_WIDTH,
      g_param_spec_int ("width", "Width", "Width of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_WIDTH, param_flags));
  g_object_class_install_property (object_class, PROP_PAD_HEIGHT,
      g_param_spec_int ("height", "Height", "Height of the picture",
          G_MININT, G_MAXINT, DEFAULT_PAD_HEIGHT, param_flags));
  g_object_class_install_property (object_class, PROP_PAD_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Alpha of the picture", 0.0, 1.0,
          DEFAULT_PAD_ALPHA, param_flags));
  g_object_class_install_property (object_class, PROP_PAD_OPERATOR,
      g_param_spec_enum ("operator", "Operator",
          "Blending operator to use for blending this pad over the previous ones",
          GST_TYPE_CUDA_COMPOSITOR_OPERATOR, DEFAULT_PAD_OPERATOR,
          param_flags));
  g_object_class_install_property (object_class, PROP_PAD_SIZING_POLICY,
      g_param_spec_enum ("sizing-policy", "Sizing policy",
          "Sizing policy to use for image scaling",
          GST_TYPE_CUDA_COMPOSITOR_SIZING_POLICY, DEFAULT_PAD_SIZING_POLICY,
          param_flags));

  vagg_pad_class->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_cuda_compositor_pad_prepare_frame);

  gst_type_mark_as_plugin_api (GST_TYPE_CUDA_COMPOSITOR_OPERATOR,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_CUDA_COMPOSITOR_SIZING_POLICY,
      (GstPluginAPIFlags) 0);
}

static void
gst_cuda_compositor_pad_init (GstCudaCompositorPad * self)
{
  self->priv = new GstCudaCompositorPadPrivate ();
}

static void
gst_cuda_compositor_pad_finalize (GObject * object)
{
  auto self = GST_CUDA_COMPOSITOR_PAD (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_pad_class)->finalize (object);
}

static void
pad_update_position (GstCudaCompositorPad * self,
    gint * old, const GValue * value)
{
  auto priv = self->priv;
  auto tmp = g_value_get_int (value);

  if (*old != tmp) {
    *old = tmp;
    priv->config_updated = TRUE;
  }
}

static void
gst_cuda_compositor_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_CUDA_COMPOSITOR_PAD (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_PAD_XPOS:
      pad_update_position (self, &priv->xpos, value);
      break;
    case PROP_PAD_YPOS:
      pad_update_position (self, &priv->ypos, value);
      break;
    case PROP_PAD_WIDTH:
      pad_update_position (self, &priv->width, value);
      break;
    case PROP_PAD_HEIGHT:
      pad_update_position (self, &priv->height, value);
      break;
    case PROP_PAD_ALPHA:
    {
      gdouble alpha = g_value_get_double (value);
      if (priv->alpha != alpha) {
        priv->config_updated = TRUE;
        priv->alpha = alpha;
      }
      break;
    }
    case PROP_PAD_OPERATOR:
    {
      auto op = (GstCudaCompositorOperator) g_value_get_enum (value);
      if (op != priv->op) {
        priv->op = op;
        priv->config_updated = TRUE;
      }
      break;
    }
    case PROP_PAD_SIZING_POLICY:
    {
      auto policy = (GstCudaCompositorSizingPolicy) g_value_get_enum (value);
      if (priv->sizing_policy != policy) {
        priv->sizing_policy = policy;
        priv->config_updated = TRUE;
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_compositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_CUDA_COMPOSITOR_PAD (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_PAD_XPOS:
      g_value_set_int (value, priv->xpos);
      break;
    case PROP_PAD_YPOS:
      g_value_set_int (value, priv->ypos);
      break;
    case PROP_PAD_WIDTH:
      g_value_set_int (value, priv->width);
      break;
    case PROP_PAD_HEIGHT:
      g_value_set_int (value, priv->height);
      break;
    case PROP_PAD_ALPHA:
      g_value_set_double (value, priv->alpha);
      break;
    case PROP_PAD_OPERATOR:
      g_value_set_enum (value, priv->op);
      break;
    case PROP_PAD_SIZING_POLICY:
      g_value_set_enum (value, priv->sizing_policy);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_compositor_pad_get_output_size (GstCudaCompositorPad * self,
    gint out_par_n, gint out_par_d, gint * width, gint * height,
    gint * x_offset, gint * y_offset)
{
  auto vagg_pad = GST_VIDEO_AGGREGATOR_PAD (self);
  auto priv = self->priv;
  gint pad_width, pad_height;
  guint dar_n, dar_d;

  *x_offset = 0;
  *y_offset = 0;
  *width = 0;
  *height = 0;

  if (!vagg_pad->info.finfo
      || vagg_pad->info.finfo->format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_DEBUG_OBJECT (self, "Have no caps yet");
    return;
  }

  pad_width = priv->width <= 0 ?
      GST_VIDEO_INFO_WIDTH (&vagg_pad->info) : priv->width;
  pad_height = priv->height <= 0 ?
      GST_VIDEO_INFO_HEIGHT (&vagg_pad->info) : priv->height;

  if (pad_width == 0 || pad_height == 0)
    return;

  if (!gst_video_calculate_display_ratio (&dar_n, &dar_d, pad_width, pad_height,
          GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
          GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d)) {
    GST_WARNING_OBJECT (self, "Cannot calculate display aspect ratio");
    return;
  }

  GST_TRACE_OBJECT (self, "scaling %ux%u by %u/%u (%u/%u / %u/%u)",
      pad_width, pad_height, dar_n, dar_d,
      GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
      GST_VIDEO_INFO_PAR_D (&vagg_pad->info), out_par_n, out_par_d);

  switch (priv->sizing_policy) {
    case GST_CUDA_COMPOSITOR_SIZING_POLICY_NONE:
      /* Pick either height or width, whichever is an integer multiple of the
       * display aspect ratio. However, prefer preserving the height to account
       * for interlaced video. */
      if (pad_height % dar_n == 0) {
        pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
      } else if (pad_width % dar_d == 0) {
        pad_height = gst_util_uint64_scale_int (pad_width, dar_d, dar_n);
      } else {
        pad_width = gst_util_uint64_scale_int (pad_height, dar_n, dar_d);
      }
      break;
    case GST_CUDA_COMPOSITOR_SIZING_POLICY_KEEP_ASPECT_RATIO:
    {
      gint from_dar_n, from_dar_d, to_dar_n, to_dar_d, num, den;

      /* Calculate DAR again with actual video size */
      if (!gst_util_fraction_multiply (GST_VIDEO_INFO_WIDTH (&vagg_pad->info),
              GST_VIDEO_INFO_HEIGHT (&vagg_pad->info),
              GST_VIDEO_INFO_PAR_N (&vagg_pad->info),
              GST_VIDEO_INFO_PAR_D (&vagg_pad->info), &from_dar_n,
              &from_dar_d)) {
        from_dar_n = from_dar_d = -1;
      }

      if (!gst_util_fraction_multiply (pad_width, pad_height,
              out_par_n, out_par_d, &to_dar_n, &to_dar_d)) {
        to_dar_n = to_dar_d = -1;
      }

      if (from_dar_n != to_dar_n || from_dar_d != to_dar_d) {
        /* Calculate new output resolution */
        if (from_dar_n != -1 && from_dar_d != -1
            && gst_util_fraction_multiply (from_dar_n, from_dar_d,
                out_par_d, out_par_n, &num, &den)) {
          GstVideoRectangle src_rect, dst_rect, rst_rect;

          src_rect.h = gst_util_uint64_scale_int (pad_width, den, num);
          if (src_rect.h == 0) {
            pad_width = 0;
            pad_height = 0;
            break;
          }

          src_rect.x = src_rect.y = 0;
          src_rect.w = pad_width;

          dst_rect.x = dst_rect.y = 0;
          dst_rect.w = pad_width;
          dst_rect.h = pad_height;

          /* Scale rect to be centered in destination rect */
          gst_video_center_rect (&src_rect, &dst_rect, &rst_rect, TRUE);

          GST_LOG_OBJECT (self,
              "Re-calculated size %dx%d -> %dx%d (x-offset %d, y-offset %d)",
              pad_width, pad_height, rst_rect.w, rst_rect.h, rst_rect.x,
              rst_rect.h);

          *x_offset = rst_rect.x;
          *y_offset = rst_rect.y;
          pad_width = rst_rect.w;
          pad_height = rst_rect.h;
        } else {
          GST_WARNING_OBJECT (self, "Failed to calculate output size");

          *x_offset = 0;
          *y_offset = 0;
          pad_width = 0;
          pad_height = 0;
        }
      }
      break;
    }
  }

  *width = pad_width;
  *height = pad_height;
}

static GstVideoRectangle
clamp_rectangle (gint x, gint y, gint w, gint h, gint outer_width,
    gint outer_height)
{
  gint x2 = x + w;
  gint y2 = y + h;
  GstVideoRectangle clamped;

  /* Clamp the x/y coordinates of this frame to the output boundaries to cover
   * the case where (say, with negative xpos/ypos or w/h greater than the output
   * size) the non-obscured portion of the frame could be outside the bounds of
   * the video itself and hence not visible at all */
  clamped.x = CLAMP (x, 0, outer_width);
  clamped.y = CLAMP (y, 0, outer_height);
  clamped.w = CLAMP (x2, 0, outer_width) - clamped.x;
  clamped.h = CLAMP (y2, 0, outer_height) - clamped.y;

  return clamped;
}

static gboolean
gst_cuda_compositor_pad_check_frame_obscured (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  auto self = GST_CUDA_COMPOSITOR_PAD (pad);
  auto priv = self->priv;
  gint width, height;
  GstVideoInfo *info = &vagg->info;
  /* The rectangle representing this frame, clamped to the video's boundaries.
   * Due to the clamping, this is different from the frame width/height above. */
  GstVideoRectangle frame_rect;
  gint x_offset, y_offset;

  /* There's three types of width/height here:
   * 1. GST_VIDEO_FRAME_WIDTH/HEIGHT:
   *     The frame width/height (same as pad->info.height/width;
   *     see gst_video_frame_map())
   * 2. cpad->width/height:
   *     The optional pad property for scaling the frame (if zero, the video is
   *     left unscaled)
   */

  if (priv->alpha == 0)
    return TRUE;

  gst_cuda_compositor_pad_get_output_size (self, GST_VIDEO_INFO_PAR_N (info),
      GST_VIDEO_INFO_PAR_D (info), &width, &height, &x_offset, &y_offset);

  frame_rect = clamp_rectangle (priv->xpos + x_offset, priv->ypos + y_offset,
      width, height, GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

  if (frame_rect.w == 0 || frame_rect.h == 0) {
    GST_DEBUG_OBJECT (pad, "Resulting frame is zero-width or zero-height "
        "(w: %i, h: %i), skipping", frame_rect.w, frame_rect.h);
    return TRUE;
  }

  return FALSE;
}

static gboolean
gst_cuda_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame)
{
  auto self = GST_CUDA_COMPOSITOR_PAD (pad);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (gst_cuda_compositor_pad_check_frame_obscured (pad, vagg))
      return TRUE;
  }

  return GST_VIDEO_AGGREGATOR_PAD_CLASS (parent_pad_class)->prepare_frame (pad,
      vagg, buffer, prepared_frame);
}

static gboolean
gst_cuda_compositor_pad_setup_converter (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  auto cuda_agg = GST_CUDA_AGGREGATOR (vagg);
  auto self = GST_CUDA_COMPOSITOR (vagg);
  auto cpad = GST_CUDA_COMPOSITOR_PAD (pad);
  auto priv = cpad->priv;
  gint width, height;
  GstVideoInfo *info = &vagg->info;
  GstVideoRectangle frame_rect;
  gint x_offset, y_offset;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!priv->conv) {
    priv->conv = gst_cuda_converter_new (cuda_agg->context, &pad->info,
        &vagg->info, nullptr);
    if (!priv->conv) {
      GST_ERROR_OBJECT (self, "Couldn't create converter");
      return FALSE;
    }

    priv->config_updated = TRUE;
  }

  if (!priv->config_updated)
    return TRUE;

  gst_cuda_compositor_pad_get_output_size (cpad, GST_VIDEO_INFO_PAR_N (info),
      GST_VIDEO_INFO_PAR_D (info), &width, &height, &x_offset, &y_offset);

  frame_rect = clamp_rectangle (priv->xpos + x_offset, priv->ypos + y_offset,
      width, height, GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

#ifndef GST_DISABLE_GST_DEBUG
  guint zorder = 0;
  g_object_get (pad, "zorder", &zorder, nullptr);

  GST_LOG_OBJECT (pad, "Update position, pad-xpos %d, pad-ypos %d, "
      "pad-zorder %d, pad-width %d, pad-height %d, in-resolution %dx%d, "
      "out-resoution %dx%d, dst-{x,y,width,height} %d-%d-%d-%d",
      priv->xpos, priv->ypos, zorder, priv->width, priv->height,
      GST_VIDEO_INFO_WIDTH (&pad->info), GST_VIDEO_INFO_HEIGHT (&pad->info),
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
      frame_rect.x, frame_rect.y, frame_rect.w, frame_rect.h);
#endif

  g_object_set (priv->conv, "dest-x", frame_rect.x,
      "dest-y", frame_rect.y, "dest-width", frame_rect.w,
      "dest-height", frame_rect.h, "alpha", priv->alpha,
      "blend", priv->op == GST_CUDA_COMPOSITOR_OPERATOR_SOURCE ? FALSE : TRUE,
      nullptr);
  priv->config_updated = FALSE;

  return TRUE;
}

#define GST_CUDA_COMPOSITOR_FORMATS \
    "{ I420, YV12, NV12, NV21, P010_10LE, P012_LE, P016_LE, I420_10LE, I420_12LE, Y444, " \
    "Y444_10LE, Y444_12LE, Y444_16LE, BGRA, RGBA, RGBx, BGRx, ARGB, ABGR, RGB, " \
    "BGR, BGR10A2_LE, RGB10A2_LE, Y42B, I422_10LE, I422_12LE, RGBP, BGRP, GBR, " \
    "GBRA, GBR_10LE, GBR_12LE, GBR_16LE, VUYA }"

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_COMPOSITOR_FORMATS)));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_COMPOSITOR_FORMATS)));

static void gst_cuda_compositor_child_proxy_init (gpointer g_iface,
    gpointer iface_data);
static void gst_cuda_compositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cuda_compositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_cuda_compositor_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_cuda_compositor_release_pad (GstElement * element,
    GstPad * pad);

static gboolean gst_cuda_compositor_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query);
static GstCaps *gst_cuda_compositor_fixate_src_caps (GstAggregator * agg,
    GstCaps * caps);
static gboolean gst_cuda_compositor_negotiated_src_caps (GstAggregator * agg,
    GstCaps * caps);
static gboolean
gst_cuda_compositor_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query);
static GstFlowReturn
gst_cuda_compositor_aggregate_cuda_frames (GstCudaAggregator * cagg,
    GstCudaStream * stream, GstBuffer * outbuf);

#define gst_cuda_compositor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCudaCompositor, gst_cuda_compositor,
    GST_TYPE_CUDA_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_cuda_compositor_child_proxy_init));

static void
gst_cuda_compositor_class_init (GstCudaCompositorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto agg_class = GST_AGGREGATOR_CLASS (klass);
  auto cagg_class = GST_CUDA_AGGREGATOR_CLASS (klass);

  object_class->set_property = gst_cuda_compositor_set_property;
  object_class->get_property = gst_cuda_compositor_get_property;

  g_object_class_install_property (object_class,
      PROP_IGNORE_INACTIVE_PADS, g_param_spec_boolean ("ignore-inactive-pads",
          "Ignore inactive pads",
          "Avoid timing out waiting for inactive pads", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_cuda_compositor_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_cuda_compositor_release_pad);

  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_cuda_compositor_sink_query);
  agg_class->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_compositor_fixate_src_caps);
  agg_class->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_cuda_compositor_negotiated_src_caps);
  agg_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_compositor_propose_allocation);

  cagg_class->aggregate_cuda_frames =
      GST_DEBUG_FUNCPTR (gst_cuda_compositor_aggregate_cuda_frames);

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &sink_template, GST_TYPE_CUDA_COMPOSITOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_template, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_set_static_metadata (element_class, "CUDA Compositor",
      "Filter/Editor/Video/Compositor/Hardware", "A CUDA compositor",
      "Seungha Yang <seungha@centricular.com>");

  gst_type_mark_as_plugin_api (GST_TYPE_CUDA_COMPOSITOR_PAD,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_cuda_compositor_debug,
      "cudacompositor", 0, "cudacompositor");
}

static void
gst_cuda_compositor_init (GstCudaCompositor * self)
{
}

static void
gst_cuda_compositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_IGNORE_INACTIVE_PADS:
      gst_aggregator_set_ignore_inactive_pads (GST_AGGREGATOR (object),
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_compositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_IGNORE_INACTIVE_PADS:
      g_value_set_boolean (value,
          gst_aggregator_get_ignore_inactive_pads (GST_AGGREGATOR (object)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GObject *
gst_cuda_compositor_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  auto self = GST_CUDA_COMPOSITOR (proxy);
  GObject *obj = nullptr;

  GST_OBJECT_LOCK (self);
  obj = (GObject *) g_list_nth_data (GST_ELEMENT_CAST (self)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (self);

  return obj;
}

static guint
gst_cuda_compositor_child_proxy_get_children_count (GstChildProxy * proxy)
{
  auto self = GST_CUDA_COMPOSITOR (proxy);
  guint count = 0;

  GST_OBJECT_LOCK (self);
  count = GST_ELEMENT_CAST (self)->numsinkpads;
  GST_OBJECT_UNLOCK (self);
  GST_INFO_OBJECT (self, "Children Count: %d", count);

  return count;
}

static void
gst_cuda_compositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index =
      gst_cuda_compositor_child_proxy_get_child_by_index;
  iface->get_children_count =
      gst_cuda_compositor_child_proxy_get_children_count;
}

static GstPad *
gst_cuda_compositor_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps)
{
  GstPad *pad;

  pad = GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, name, caps);

  if (!pad) {
    GST_DEBUG_OBJECT (element, "could not create/add pad");
    return nullptr;
  }

  gst_child_proxy_child_added (GST_CHILD_PROXY (element), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_DEBUG_OBJECT (element, "Created new pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  return pad;
}

static void
gst_cuda_compositor_release_pad (GstElement * element, GstPad * pad)
{
  auto self = GST_CUDA_COMPOSITOR (element);

  GST_DEBUG_OBJECT (self, "Releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gst_child_proxy_child_removed (GST_CHILD_PROXY (self), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}

static GstCaps *
gst_cuda_compositor_sink_getcaps (GstPad * pad, GstCaps * filter)
{
  GstCaps *sinkcaps;
  GstCaps *template_caps;
  GstCaps *filtered_caps;
  GstCaps *returned_caps;

  template_caps = gst_pad_get_pad_template_caps (pad);

  sinkcaps = gst_pad_get_current_caps (pad);
  if (sinkcaps == nullptr) {
    sinkcaps = gst_caps_ref (template_caps);
  } else {
    sinkcaps = gst_caps_merge (sinkcaps, gst_caps_ref (template_caps));
  }

  if (filter) {
    filtered_caps = gst_caps_intersect (sinkcaps, filter);
    gst_caps_unref (sinkcaps);
  } else {
    filtered_caps = sinkcaps;   /* pass ownership */
  }

  returned_caps = gst_caps_intersect (filtered_caps, template_caps);

  gst_caps_unref (template_caps);
  gst_caps_unref (filtered_caps);

  GST_DEBUG_OBJECT (pad, "returning %" GST_PTR_FORMAT, returned_caps);

  return returned_caps;
}

static gboolean
gst_cuda_compositor_sink_acceptcaps (GstPad * pad, GstCaps * caps)
{
  gboolean ret;
  GstCaps *template_caps;

  GST_DEBUG_OBJECT (pad, "try accept caps of %" GST_PTR_FORMAT, caps);

  template_caps = gst_pad_get_pad_template_caps (pad);
  template_caps = gst_caps_make_writable (template_caps);

  ret = gst_caps_can_intersect (caps, template_caps);
  GST_DEBUG_OBJECT (pad, "%saccepted caps %" GST_PTR_FORMAT,
      (ret ? "" : "not "), caps);
  gst_caps_unref (template_caps);

  return ret;
}

static gboolean
gst_cuda_compositor_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query)
{
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_cuda_compositor_sink_getcaps (GST_PAD (pad), filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_cuda_compositor_sink_acceptcaps (GST_PAD (pad), caps);
      gst_query_set_accept_caps_result (query, ret);
      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, pad, query);
}

static GstCaps *
gst_cuda_compositor_fixate_src_caps (GstAggregator * agg, GstCaps * caps)
{
  auto vagg = GST_VIDEO_AGGREGATOR (agg);
  GList *l;
  gint best_width = -1, best_height = -1;
  gint best_fps_n = -1, best_fps_d = -1;
  gint par_n, par_d;
  gdouble best_fps = 0.;
  GstCaps *ret = nullptr;
  GstStructure *s;

  ret = gst_caps_make_writable (caps);

  /* we need this to calculate how large to make the output frame */
  s = gst_caps_get_structure (ret, 0);
  if (gst_structure_has_field (s, "pixel-aspect-ratio")) {
    gst_structure_fixate_field_nearest_fraction (s, "pixel-aspect-ratio", 1, 1);
    gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d);
  } else {
    par_n = par_d = 1;
  }

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    auto vaggpad = GST_VIDEO_AGGREGATOR_PAD (l->data);
    auto cpad = GST_CUDA_COMPOSITOR_PAD (vaggpad);
    auto priv = cpad->priv;
    gint this_width, this_height;
    gint width, height;
    gint fps_n, fps_d;
    gdouble cur_fps;
    gint x_offset;
    gint y_offset;

    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);
    gst_cuda_compositor_pad_get_output_size (cpad,
        par_n, par_d, &width, &height, &x_offset, &y_offset);

    if (width == 0 || height == 0)
      continue;

    /* {x,y}_offset represent padding size of each top and left area.
     * To calculate total resolution, count bottom and right padding area
     * as well here */
    this_width = width + MAX (priv->xpos + 2 * x_offset, 0);
    this_height = height + MAX (priv->ypos + 2 * y_offset, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;

    if (fps_d == 0)
      cur_fps = 0.0;
    else
      gst_util_fraction_to_double (fps_n, fps_d, &cur_fps);

    if (best_fps < cur_fps) {
      best_fps = cur_fps;
      best_fps_n = fps_n;
      best_fps_d = fps_d;
    }
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_fps_n <= 0 || best_fps_d <= 0 || best_fps == 0.0) {
    best_fps_n = 25;
    best_fps_d = 1;
    best_fps = 25.0;
  }

  if (best_width <= 0 || best_height <= 0) {
    best_width = 320;
    best_height = 240;
  }

  gst_structure_fixate_field_nearest_int (s, "width", best_width);
  gst_structure_fixate_field_nearest_int (s, "height", best_height);
  gst_structure_fixate_field_nearest_fraction (s, "framerate", best_fps_n,
      best_fps_d);
  ret = gst_caps_fixate (ret);

  GST_LOG_OBJECT (agg, "Fixated caps %" GST_PTR_FORMAT, ret);

  return ret;
}

static gboolean
gst_cuda_compositor_clear_pad_context (GstCudaCompositor * self,
    GstCudaCompositorPad * cpad, gpointer user_data)
{
  auto priv = cpad->priv;

  gst_clear_object (&priv->conv);

  return TRUE;
}

static gboolean
gst_cuda_compositor_negotiated_src_caps (GstAggregator * agg, GstCaps * caps)
{
  gst_element_foreach_sink_pad (GST_ELEMENT_CAST (agg),
      (GstElementForeachPadFunc) gst_cuda_compositor_clear_pad_context,
      nullptr);

  return GST_AGGREGATOR_CLASS (parent_class)->negotiated_src_caps (agg, caps);
}

static gboolean
gst_cuda_compositor_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query)
{
  if (!GST_AGGREGATOR_CLASS (parent_class)->propose_allocation (agg,
          pad, decide_query, query)) {
    return FALSE;
  }

  /* We support crop meta as well */
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, nullptr);

  return TRUE;
}

static GstFlowReturn
gst_cuda_compositor_aggregate_cuda_frames (GstCudaAggregator * cagg,
    GstCudaStream * stream, GstBuffer * outbuf)
{
  auto self = GST_CUDA_COMPOSITOR (cagg);
  auto vagg = GST_VIDEO_AGGREGATOR (cagg);
  GList *iter;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFrame frame;

  GST_LOG_OBJECT (self, "aggregate");

  if (!gst_video_frame_map (&frame, &vagg->info, outbuf,
          (GstMapFlags) (GST_MAP_WRITE | GST_MAP_CUDA))) {
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    return GST_FLOW_ERROR;
  }

  auto stream_handle = gst_cuda_stream_get_handle (stream);
  gfloat clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
  if (GST_VIDEO_INFO_IS_YUV (&vagg->info)) {
    clear_color[1] = 0.5;
    clear_color[2] = 0.5;
  }

  if (!gst_cuda_fill_video_frame (cagg->context, stream, &frame, clear_color)) {
    GST_ERROR_OBJECT (self, "Couldn't draw background");
    ret = GST_FLOW_ERROR;
    goto out;
  }

  GST_OBJECT_LOCK (self);
  for (iter = GST_ELEMENT (vagg)->sinkpads; iter; iter = g_list_next (iter)) {
    auto pad = GST_VIDEO_AGGREGATOR_PAD (iter->data);
    auto cpad = GST_CUDA_COMPOSITOR_PAD (pad);
    auto pad_priv = cpad->priv;
    auto in_frame = gst_video_aggregator_pad_get_prepared_frame (pad);

    if (!in_frame)
      continue;

    if (!gst_cuda_compositor_pad_setup_converter (pad, vagg)) {
      GST_ERROR_OBJECT (self, "Couldn't setup converter");
      ret = GST_FLOW_ERROR;
      break;
    }

    gint x, y, w, h;
    gint x_offset = 0;
    gint y_offset = 0;

    if (pad_priv->xpos < 0)
      x_offset = pad_priv->xpos;

    if (pad_priv->ypos < 0)
      y_offset = pad_priv->ypos;

    auto crop_meta = gst_buffer_get_video_crop_meta (in_frame->buffer);
    if (crop_meta) {
      x = crop_meta->x;
      y = crop_meta->y;
      w = crop_meta->width;
      h = crop_meta->height;
    } else {
      x = y = 0;
      w = pad->info.width;
      h = pad->info.height;
    }

    g_object_set (pad_priv->conv, "src-x", x - x_offset, "src-y", y - y_offset,
        "src-width", w + x_offset, "src-height", h + y_offset, nullptr);

    if (!gst_cuda_converter_convert_frame (pad_priv->conv, stream, in_frame,
            &frame)) {
      GST_ERROR_OBJECT (pad, "Couldn't convert frame");
      ret = GST_FLOW_ERROR;
      break;
    }
  }
  GST_OBJECT_UNLOCK (self);

  if (ret == GST_FLOW_OK)
    CuStreamSynchronize (stream_handle);

out:
  gst_video_frame_unmap (&frame);

  return ret;
}
