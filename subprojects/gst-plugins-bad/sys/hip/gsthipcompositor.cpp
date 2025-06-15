/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsthip.h"
#include "gsthipcompositor.h"
#include "gsthipconverter.h"
#include <mutex>

GST_DEBUG_CATEGORY_STATIC (gst_hip_compositor_debug);
#define GST_CAT_DEFAULT gst_hip_compositor_debug

enum GstHipCompositorOperator
{
  GST_HIP_COMPOSITOR_OPERATOR_SOURCE,
  GST_HIP_COMPOSITOR_OPERATOR_OVER,
};

#define GST_TYPE_HIP_COMPOSITOR_OPERATOR (gst_hip_compositor_operator_get_type())
static GType
gst_hip_compositor_operator_get_type (void)
{
  static GType compositor_operator_type = 0;
  static const GEnumValue compositor_operator[] = {
    {GST_HIP_COMPOSITOR_OPERATOR_SOURCE, "Source", "source"},
    {GST_HIP_COMPOSITOR_OPERATOR_OVER, "Over", "over"},
    {0, nullptr, nullptr},
  };
  static std::once_flag once;

  std::call_once (once,[&] {
        compositor_operator_type =
        g_enum_register_static ("GstHipCompositorOperator",
            compositor_operator);
      });

  return compositor_operator_type;
}

enum GstHipCompositorSizingPolicy
{
  GST_HIP_COMPOSITOR_SIZING_POLICY_NONE,
  GST_HIP_COMPOSITOR_SIZING_POLICY_KEEP_ASPECT_RATIO,
};

#define GST_TYPE_HIP_COMPOSITOR_SIZING_POLICY (gst_hip_compositor_sizing_policy_get_type())
static GType
gst_hip_compositor_sizing_policy_get_type (void)
{
  static GType sizing_policy_type = 0;

  static const GEnumValue sizing_polices[] = {
    {GST_HIP_COMPOSITOR_SIZING_POLICY_NONE,
        "None: Image is scaled to fill configured destination rectangle without "
          "padding or keeping the aspect ratio", "none"},
    {GST_HIP_COMPOSITOR_SIZING_POLICY_KEEP_ASPECT_RATIO,
          "Keep Aspect Ratio: Image is scaled to fit destination rectangle "
          "specified by GstD3D12CompositorPad:{xpos, ypos, width, height} "
          "with preserved aspect ratio. Resulting image will be centered in "
          "the destination rectangle with padding if necessary",
        "keep-aspect-ratio"},
    {0, nullptr, nullptr},
  };
  static std::once_flag once;

  std::call_once (once,[&] {
        sizing_policy_type =
        g_enum_register_static ("GstHipCompositorSizingPolicy", sizing_polices);
      });

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
#define DEFAULT_PAD_OPERATOR GST_HIP_COMPOSITOR_OPERATOR_OVER
#define DEFAULT_PAD_SIZING_POLICY GST_HIP_COMPOSITOR_SIZING_POLICY_NONE

enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_VENDOR,
  PROP_IGNORE_INACTIVE_PADS,
};

#define DEFAULT_DEVICE_ID -1
#define DEFAULT_VENDOR GST_HIP_VENDOR_UNKNOWN

/* *INDENT-OFF* */
struct GstHipCompositorPadPrivate
{
  ~GstHipCompositorPadPrivate ()
  {
    gst_clear_object (&conv);
    gst_clear_buffer (&prepared_buf);
    if (fallback_pool) {
      gst_buffer_pool_set_active (fallback_pool, FALSE);
      gst_object_unref (fallback_pool);
    }
  }

  GstHipConverter *conv = nullptr;
  GstBufferPool *fallback_pool = nullptr;
  GstBuffer *prepared_buf = nullptr;

  gboolean config_updated = FALSE;

  std::recursive_mutex lock;

  /* properties */
  gint xpos = DEFAULT_PAD_XPOS;
  gint ypos = DEFAULT_PAD_YPOS;
  gint width = DEFAULT_PAD_WIDTH;
  gint height = DEFAULT_PAD_HEIGHT;
  gdouble alpha = DEFAULT_PAD_ALPHA;
  GstHipCompositorOperator op = DEFAULT_PAD_OPERATOR;
  GstHipCompositorSizingPolicy sizing_policy = DEFAULT_PAD_SIZING_POLICY;
};

struct _GstHipCompositorPad
{
  GstVideoAggregatorConvertPad parent;

  GstHipCompositorPadPrivate *priv;
};

struct GstHipCompositorPrivate
{
  std::recursive_mutex lock;

  /* properties */
  gint device_id = DEFAULT_DEVICE_ID;
  GstHipVendor vendor = DEFAULT_VENDOR;
};
/* *INDENT-ON* */

struct _GstHipCompositor
{
  GstVideoAggregator parent;

  GstHipDevice *device;

  GstHipCompositorPrivate *priv;
};

static void gst_hip_compositor_pad_finalize (GObject * object);
static void gst_hip_compositor_pad_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_hip_compositor_pad_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static gboolean
gst_hip_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame);
static void gst_hip_compositor_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame);

#define gst_hip_compositor_pad_parent_class parent_pad_class
G_DEFINE_TYPE (GstHipCompositorPad, gst_hip_compositor_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

static void
gst_hip_compositor_pad_class_init (GstHipCompositorPadClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto vagg_pad_class = GST_VIDEO_AGGREGATOR_PAD_CLASS (klass);
  auto param_flags = (GParamFlags)
      (G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  object_class->finalize = gst_hip_compositor_pad_finalize;
  object_class->set_property = gst_hip_compositor_pad_set_property;
  object_class->get_property = gst_hip_compositor_pad_get_property;

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
          GST_TYPE_HIP_COMPOSITOR_OPERATOR, DEFAULT_PAD_OPERATOR, param_flags));
  g_object_class_install_property (object_class, PROP_PAD_SIZING_POLICY,
      g_param_spec_enum ("sizing-policy", "Sizing policy",
          "Sizing policy to use for image scaling",
          GST_TYPE_HIP_COMPOSITOR_SIZING_POLICY, DEFAULT_PAD_SIZING_POLICY,
          param_flags));

  vagg_pad_class->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_pad_prepare_frame);
  vagg_pad_class->clean_frame =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_pad_clean_frame);

  gst_type_mark_as_plugin_api (GST_TYPE_HIP_COMPOSITOR_OPERATOR,
      (GstPluginAPIFlags) 0);
  gst_type_mark_as_plugin_api (GST_TYPE_HIP_COMPOSITOR_SIZING_POLICY,
      (GstPluginAPIFlags) 0);
}

static void
gst_hip_compositor_pad_init (GstHipCompositorPad * self)
{
  self->priv = new GstHipCompositorPadPrivate ();
}

static void
gst_hip_compositor_pad_finalize (GObject * object)
{
  auto self = GST_HIP_COMPOSITOR_PAD (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_pad_class)->finalize (object);
}

static void
pad_update_position (GstHipCompositorPad * self,
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
gst_hip_compositor_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_COMPOSITOR_PAD (object);
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
      auto op = (GstHipCompositorOperator) g_value_get_enum (value);
      if (op != priv->op) {
        priv->op = op;
        priv->config_updated = TRUE;
      }
      break;
    }
    case PROP_PAD_SIZING_POLICY:
    {
      auto policy = (GstHipCompositorSizingPolicy) g_value_get_enum (value);
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
gst_hip_compositor_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_COMPOSITOR_PAD (object);
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
gst_hip_compositor_pad_get_output_size (GstHipCompositorPad * self,
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
    case GST_HIP_COMPOSITOR_SIZING_POLICY_NONE:
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
    case GST_HIP_COMPOSITOR_SIZING_POLICY_KEEP_ASPECT_RATIO:
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
gst_hip_compositor_pad_check_frame_obscured (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  auto self = GST_HIP_COMPOSITOR_PAD (pad);
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

  gst_hip_compositor_pad_get_output_size (self, GST_VIDEO_INFO_PAR_N (info),
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

static GstBuffer *
gst_hip_compositor_upload_frame (GstHipCompositor * self,
    GstVideoAggregatorPad * pad, GstBuffer * buffer)
{
  auto cpad = GST_HIP_COMPOSITOR_PAD (pad);
  auto priv = cpad->priv;
  GstVideoFrame src, dst;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (gst_is_hip_memory (mem)) {
    auto hmem = GST_HIP_MEMORY_CAST (mem);
    if (gst_hip_device_is_equal (hmem->device, self->device))
      return gst_buffer_ref (buffer);
  }

  if (!priv->fallback_pool) {
    priv->fallback_pool = gst_hip_buffer_pool_new (self->device);
    auto config = gst_buffer_pool_get_config (priv->fallback_pool);

    auto caps = gst_video_info_to_caps (&pad->info);
    gst_buffer_pool_config_set_params (config, caps, pad->info.size, 0, 0);
    gst_caps_unref (caps);
    if (!gst_buffer_pool_set_config (priv->fallback_pool, config)) {
      GST_ERROR_OBJECT (pad, "Set config failed");
      gst_clear_object (&priv->fallback_pool);
      return nullptr;
    }

    if (!gst_buffer_pool_set_active (priv->fallback_pool, TRUE)) {
      GST_ERROR_OBJECT (pad, "Set active failed");
      gst_clear_object (&priv->fallback_pool);
      return nullptr;
    }
  }

  GstBuffer *outbuf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->fallback_pool, &outbuf, nullptr);
  if (!outbuf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
    return nullptr;
  }

  if (!gst_video_frame_map (&src, &pad->info, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (pad, "Couldn't map src frame");
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  if (!gst_video_frame_map (&dst, &pad->info, outbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (pad, "Couldn't map dst frame");
    gst_video_frame_unmap (&src);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  auto ret = gst_video_frame_copy (&dst, &src);
  gst_video_frame_unmap (&dst);
  gst_video_frame_unmap (&src);

  if (!ret) {
    GST_ERROR_OBJECT (pad, "Couldn't copy frame");
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  return outbuf;
}

static gboolean
gst_hip_compositor_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame)
{
  auto self = GST_HIP_COMPOSITOR_PAD (pad);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (gst_hip_compositor_pad_check_frame_obscured (pad, vagg))
    return TRUE;

  buffer = gst_hip_compositor_upload_frame (GST_HIP_COMPOSITOR (vagg),
      pad, buffer);
  if (!buffer)
    return FALSE;

  if (!gst_video_frame_map (prepared_frame,
          &pad->info, buffer, GST_MAP_READ_HIP)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame");
    gst_buffer_unref (buffer);
    return FALSE;
  }

  priv->prepared_buf = buffer;

  return TRUE;
}

static void
gst_hip_compositor_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame)
{
  auto self = GST_HIP_COMPOSITOR_PAD (pad);
  auto priv = self->priv;

  if (prepared_frame->buffer)
    gst_video_frame_unmap (prepared_frame);

  memset (prepared_frame, 0, sizeof (GstVideoFrame));
  gst_clear_buffer (&priv->prepared_buf);
}

static gboolean
gst_hip_compositor_pad_setup_converter (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg)
{
  auto self = GST_HIP_COMPOSITOR (vagg);
  auto cpad = GST_HIP_COMPOSITOR_PAD (pad);
  auto priv = cpad->priv;
  gint width, height;
  GstVideoInfo *info = &vagg->info;
  GstVideoRectangle frame_rect;
  gint x_offset, y_offset;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  if (!priv->conv) {
    priv->conv = gst_hip_converter_new (self->device, &pad->info, &vagg->info,
        nullptr);
    if (!priv->conv) {
      GST_ERROR_OBJECT (self, "Couldn't create converter");
      return FALSE;
    }

    priv->config_updated = TRUE;
  }

  if (!priv->config_updated)
    return TRUE;

  gst_hip_compositor_pad_get_output_size (cpad, GST_VIDEO_INFO_PAR_N (info),
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
      "blend", priv->op == GST_HIP_COMPOSITOR_OPERATOR_SOURCE ? FALSE : TRUE,
      nullptr);
  priv->config_updated = FALSE;

  return TRUE;
}

#define GST_HIP_COMPOSITOR_FORMATS \
    "{ I420, YV12, NV12, NV21, P010_10LE, P012_LE, P016_LE, I420_10LE, I420_12LE, Y444, " \
    "Y444_10LE, Y444_12LE, Y444_16LE, BGRA, RGBA, RGBx, BGRx, ARGB, ABGR, RGB, " \
    "BGR, BGR10A2_LE, RGB10A2_LE, Y42B, I422_10LE, I422_12LE, RGBP, BGRP, GBR, " \
    "GBRA, GBR_10LE, GBR_12LE, GBR_16LE, VUYA }"

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_COMPOSITOR_FORMATS)));

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_HIP_MEMORY, GST_HIP_COMPOSITOR_FORMATS)));

static void gst_hip_compositor_child_proxy_init (gpointer g_iface,
    gpointer iface_data);
static void gst_hip_compositor_finalize (GObject * object);
static void gst_hip_compositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_hip_compositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstPad *gst_hip_compositor_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_hip_compositor_release_pad (GstElement * element, GstPad * pad);
static void gst_hip_compositor_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_hip_compositor_start (GstAggregator * agg);
static gboolean gst_hip_compositor_stop (GstAggregator * agg);
static gboolean gst_hip_compositor_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query);
static gboolean gst_hip_compositor_src_query (GstAggregator * agg,
    GstQuery * query);
static GstCaps *gst_hip_compositor_fixate_src_caps (GstAggregator * agg,
    GstCaps * caps);
static gboolean gst_hip_compositor_negotiated_src_caps (GstAggregator * agg,
    GstCaps * caps);
static gboolean
gst_hip_compositor_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query);
static gboolean gst_hip_compositor_decide_allocation (GstAggregator * agg,
    GstQuery * query);
static GstFlowReturn
gst_hip_compositor_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf);

#define gst_hip_compositor_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstHipCompositor, gst_hip_compositor,
    GST_TYPE_VIDEO_AGGREGATOR, G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_hip_compositor_child_proxy_init));

static void
gst_hip_compositor_class_init (GstHipCompositorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto agg_class = GST_AGGREGATOR_CLASS (klass);
  auto vagg_class = GST_VIDEO_AGGREGATOR_CLASS (klass);

  object_class->finalize = gst_hip_compositor_finalize;
  object_class->set_property = gst_hip_compositor_set_property;
  object_class->get_property = gst_hip_compositor_get_property;

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_int ("device-id",
          "Device ID", "HIP device ID to use (-1 = auto)",
          -1, G_MAXINT, DEFAULT_DEVICE_ID,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class, PROP_VENDOR,
      g_param_spec_enum ("vendor", "Vendor", "Vendor type",
          GST_TYPE_HIP_VENDOR, GST_HIP_VENDOR_UNKNOWN,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (object_class,
      PROP_IGNORE_INACTIVE_PADS, g_param_spec_boolean ("ignore-inactive-pads",
          "Ignore inactive pads",
          "Avoid timing out waiting for inactive pads", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_release_pad);
  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_set_context);

  agg_class->start = GST_DEBUG_FUNCPTR (gst_hip_compositor_start);
  agg_class->stop = GST_DEBUG_FUNCPTR (gst_hip_compositor_stop);
  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_hip_compositor_sink_query);
  agg_class->src_query = GST_DEBUG_FUNCPTR (gst_hip_compositor_src_query);
  agg_class->fixate_src_caps =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_fixate_src_caps);
  agg_class->negotiated_src_caps =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_negotiated_src_caps);
  agg_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_propose_allocation);
  agg_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_decide_allocation);

  vagg_class->aggregate_frames =
      GST_DEBUG_FUNCPTR (gst_hip_compositor_aggregate_frames);

  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &sink_template, GST_TYPE_HIP_COMPOSITOR_PAD);
  gst_element_class_add_static_pad_template_with_gtype (element_class,
      &src_template, GST_TYPE_AGGREGATOR_PAD);

  gst_element_class_set_static_metadata (element_class, "HIP Compositor",
      "Filter/Editor/Video/Compositor/Hardware", "A HIP compositor",
      "Seungha Yang <seungha@centricular.com>");

  gst_type_mark_as_plugin_api (GST_TYPE_HIP_COMPOSITOR_PAD,
      (GstPluginAPIFlags) 0);

  GST_DEBUG_CATEGORY_INIT (gst_hip_compositor_debug,
      "hipcompositor", 0, "hipcompositor");
}

static void
gst_hip_compositor_init (GstHipCompositor * self)
{
  self->priv = new GstHipCompositorPrivate ();
}

static void
gst_hip_compositor_finalize (GObject * object)
{
  auto self = GST_HIP_COMPOSITOR (object);

  delete self->priv;

  gst_clear_object (&self->device);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_hip_compositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_COMPOSITOR (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int (value);
      break;
    case PROP_VENDOR:
      priv->vendor = (GstHipVendor) g_value_get_enum (value);
      break;
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
gst_hip_compositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  auto self = GST_HIP_COMPOSITOR (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int (value, priv->device_id);
      break;
    case PROP_VENDOR:
      g_value_set_enum (value, priv->vendor);
      break;
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
gst_hip_compositor_child_proxy_get_child_by_index (GstChildProxy * proxy,
    guint index)
{
  auto self = GST_HIP_COMPOSITOR (proxy);
  GObject *obj = nullptr;

  GST_OBJECT_LOCK (self);
  obj = (GObject *) g_list_nth_data (GST_ELEMENT_CAST (self)->sinkpads, index);
  if (obj)
    gst_object_ref (obj);
  GST_OBJECT_UNLOCK (self);

  return obj;
}

static guint
gst_hip_compositor_child_proxy_get_children_count (GstChildProxy * proxy)
{
  auto self = GST_HIP_COMPOSITOR (proxy);
  guint count = 0;

  GST_OBJECT_LOCK (self);
  count = GST_ELEMENT_CAST (self)->numsinkpads;
  GST_OBJECT_UNLOCK (self);
  GST_INFO_OBJECT (self, "Children Count: %d", count);

  return count;
}

static void
gst_hip_compositor_child_proxy_init (gpointer g_iface, gpointer iface_data)
{
  GstChildProxyInterface *iface = (GstChildProxyInterface *) g_iface;

  iface->get_child_by_index = gst_hip_compositor_child_proxy_get_child_by_index;
  iface->get_children_count = gst_hip_compositor_child_proxy_get_children_count;
}

static GstPad *
gst_hip_compositor_request_new_pad (GstElement * element,
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
gst_hip_compositor_release_pad (GstElement * element, GstPad * pad)
{
  auto self = GST_HIP_COMPOSITOR (element);

  GST_DEBUG_OBJECT (self, "Releasing pad %s:%s", GST_DEBUG_PAD_NAME (pad));

  gst_child_proxy_child_removed (GST_CHILD_PROXY (self), G_OBJECT (pad),
      GST_OBJECT_NAME (pad));

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);
}

static void
gst_hip_compositor_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_HIP_COMPOSITOR (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_hip_handle_set_context (element, context, priv->vendor, priv->device_id,
        &self->device);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_hip_compositor_start (GstAggregator * agg)
{
  auto self = GST_HIP_COMPOSITOR (agg);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!gst_hip_ensure_element_data (GST_ELEMENT_CAST (self),
            priv->vendor, priv->device_id, &self->device)) {
      GST_ERROR_OBJECT (self, "Failed to get device");
      return FALSE;
    }
  }

  return GST_AGGREGATOR_CLASS (parent_class)->start (agg);
}

static gboolean
gst_hip_compositor_stop (GstAggregator * agg)
{
  auto self = GST_HIP_COMPOSITOR (agg);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_clear_object (&self->device);
  }

  return GST_AGGREGATOR_CLASS (parent_class)->stop (agg);
}

static GstCaps *
gst_hip_compositor_sink_getcaps (GstPad * pad, GstCaps * filter)
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
gst_hip_compositor_sink_acceptcaps (GstPad * pad, GstCaps * caps)
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
gst_hip_compositor_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query)
{
  auto self = GST_HIP_COMPOSITOR (agg);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      if (gst_hip_handle_context_query (GST_ELEMENT (agg), query, self->device)) {
        return TRUE;
      }
      break;
    }
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_hip_compositor_sink_getcaps (GST_PAD (pad), filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_hip_compositor_sink_acceptcaps (GST_PAD (pad), caps);
      gst_query_set_accept_caps_result (query, ret);
      return TRUE;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, pad, query);
}

static gboolean
gst_hip_compositor_src_query (GstAggregator * agg, GstQuery * query)
{
  auto self = GST_HIP_COMPOSITOR (agg);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_hip_handle_context_query (GST_ELEMENT (agg), query, self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
}

static GstCaps *
gst_hip_compositor_fixate_src_caps (GstAggregator * agg, GstCaps * caps)
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
    auto cpad = GST_HIP_COMPOSITOR_PAD (vaggpad);
    auto priv = cpad->priv;
    gint this_width, this_height;
    gint width, height;
    gint fps_n, fps_d;
    gdouble cur_fps;
    gint x_offset;
    gint y_offset;

    fps_n = GST_VIDEO_INFO_FPS_N (&vaggpad->info);
    fps_d = GST_VIDEO_INFO_FPS_D (&vaggpad->info);
    gst_hip_compositor_pad_get_output_size (cpad,
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
gst_hip_compositor_clear_pad_context (GstHipCompositor * self,
    GstHipCompositorPad * cpad, gpointer user_data)
{
  auto priv = cpad->priv;

  gst_clear_object (&priv->conv);

  return TRUE;
}

static gboolean
gst_hip_compositor_negotiated_src_caps (GstAggregator * agg, GstCaps * caps)
{
  gst_element_foreach_sink_pad (GST_ELEMENT_CAST (agg),
      (GstElementForeachPadFunc) gst_hip_compositor_clear_pad_context, nullptr);

  return GST_AGGREGATOR_CLASS (parent_class)->negotiated_src_caps (agg, caps);
}

static gboolean
gst_hip_compositor_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query)
{
  auto self = GST_HIP_COMPOSITOR (agg);
  GstVideoInfo info;
  GstCaps *caps;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    auto pool = gst_hip_buffer_pool_new (self->device);

    if (!pool) {
      GST_ERROR_OBJECT (self, "Failed to create buffer pool");
      return FALSE;
    }

    auto config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    guint size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (pool, "Couldn't set config");
      gst_object_unref (pool);

      return FALSE;
    }

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config,
        nullptr, &size, nullptr, nullptr);
    gst_structure_free (config);

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_hip_compositor_decide_allocation (GstAggregator * agg, GstQuery * query)
{
  auto self = GST_HIP_COMPOSITOR (agg);
  GstCaps *caps;
  GstBufferPool *pool = nullptr;
  guint n, size, min, max;
  GstVideoInfo info;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_DEBUG_OBJECT (self, "No output caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps");
    return FALSE;
  }

  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  /* create our own pool */
  if (pool) {
    if (!GST_IS_HIP_BUFFER_POOL (pool)) {
      GST_DEBUG_OBJECT (self,
          "Downstream pool is not hip, will create new one");
      gst_clear_object (&pool);
    } else {
      auto hpool = GST_HIP_BUFFER_POOL (pool);
      if (!gst_hip_device_is_equal (hpool->device, self->device)) {
        GST_DEBUG_OBJECT (self, "Different device, will create new one");
        gst_clear_object (&pool);
      }
    }
  }

  size = (guint) info.size;

  if (!pool) {
    pool = gst_hip_buffer_pool_new (self->device);
    min = 0;
    max = 0;
  }

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Set config failed");
    gst_object_unref (pool);
    return FALSE;
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_hip_compositor_draw_background (GstHipCompositor * self,
    GstVideoFrame * frame, hipStream_t stream)
{
  hipError_t ret;
  hipDeviceptr_t data;
  guint height, stride;
  guint16 uv_val;
  auto vendor = gst_hip_device_get_vendor (self->device);
  auto format = GST_VIDEO_FRAME_FORMAT (frame);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y444:
      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 0);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

      ret = HipMemsetD8Async (vendor, data, 0, stride * height, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;

      for (guint i = 1; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
        data = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, i);
        stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);

        ret = HipMemsetD8Async (vendor, data, 128, stride * height, stream);
        if (!gst_hip_result (ret, vendor))
          return FALSE;
      }
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 0);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

      ret = HipMemsetD8Async (vendor, data, 0, stride * height, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;

      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 1);
      ret = HipMemsetD8Async (vendor, data, 128, stride * height, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;
      break;
    case GST_VIDEO_FORMAT_P010_10LE:
    case GST_VIDEO_FORMAT_P012_LE:
    case GST_VIDEO_FORMAT_P016_LE:
      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 0);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

      ret = HipMemsetD16Async (vendor, data, 0, stride * height / 2, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;

      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 1);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 1);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 1);
      ret = HipMemsetD16Async (vendor,
          data, G_MAXUINT16 / 2, stride * height / 2, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_16LE:
      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
      height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, 0);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

      ret = HipMemsetD16Async (vendor, data, 0, stride * height / 2, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;

      uv_val = (((guint) 1 << GST_VIDEO_FRAME_COMP_DEPTH (frame, 0)) / 2);
      for (guint i = 1; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
        data = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, i);
        stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);

        ret = HipMemsetD16Async (vendor,
            data, uv_val, stride * height / 2, stream);
        if (!gst_hip_result (ret, vendor))
          return FALSE;
      }
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGB10A2_LE:
    case GST_VIDEO_FORMAT_BGR10A2_LE:
    case GST_VIDEO_FORMAT_VUYA:
    {
      guint32 packed = 0;
      if (format == GST_VIDEO_FORMAT_ARGB || format == GST_VIDEO_FORMAT_ABGR) {
        packed = 0xff;
      } else if (format == GST_VIDEO_FORMAT_RGB10A2_LE ||
          format == GST_VIDEO_FORMAT_BGR10A2_LE) {
        packed = ((guint32) 0x3) << 30;
      } else if (format == GST_VIDEO_FORMAT_VUYA) {
        packed = (((guint32) 0xff) << 24) | (((guint32) 0x80) << 8) |
            ((guint32) 0x80);
      } else {
        packed = ((guint32) 0xff) << 24;
      }

      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
      height = GST_VIDEO_FRAME_HEIGHT (frame);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

      ret = HipMemsetD32Async (vendor,
          data, packed, stride * height / 4, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;
      break;
    }
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      data = GST_VIDEO_FRAME_PLANE_DATA (frame, 0);
      height = GST_VIDEO_FRAME_HEIGHT (frame);
      stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, 0);

      ret = HipMemsetD8Async (vendor, data, 0, stride * height, stream);
      if (!gst_hip_result (ret, vendor))
        return FALSE;
      break;
    case GST_VIDEO_FORMAT_RGBP:
    case GST_VIDEO_FORMAT_BGRP:
    case GST_VIDEO_FORMAT_GBR:
    case GST_VIDEO_FORMAT_GBRA:
      for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
        guint8 val = 0;
        if (format == GST_VIDEO_FORMAT_GBRA && i == 3)
          val = 255;

        data = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, i);
        stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);

        ret = HipMemsetD8Async (vendor, data, val, stride * height, stream);
        if (!gst_hip_result (ret, vendor))
          return FALSE;
      }
      break;
    case GST_VIDEO_FORMAT_GBR_10LE:
    case GST_VIDEO_FORMAT_GBR_12LE:
    case GST_VIDEO_FORMAT_GBR_16LE:
      for (guint i = 0; i < GST_VIDEO_FRAME_N_PLANES (frame); i++) {
        data = GST_VIDEO_FRAME_PLANE_DATA (frame, i);
        height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, i);
        stride = GST_VIDEO_FRAME_PLANE_STRIDE (frame, i);

        ret = HipMemsetD16Async (vendor, data, 0, stride * height / 2, stream);
        if (!gst_hip_result (ret, vendor))
          return FALSE;
      }
      break;
    default:
      g_assert_not_reached ();
      return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_hip_compositor_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  auto self = GST_HIP_COMPOSITOR (vagg);
  GList *iter;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoFrame frame;
  hipStream_t stream = nullptr;
  auto gst_stream = gst_hip_device_get_stream (self->device);
  stream = gst_hip_stream_get_handle (gst_stream);

  GST_LOG_OBJECT (self, "aggregate");

  if (!gst_hip_device_set_current (self->device)) {
    GST_ERROR_OBJECT (self, "Couldn't set device");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&frame, &vagg->info, outbuf, GST_MAP_WRITE_HIP)) {
    GST_ERROR_OBJECT (self, "Couldn't map output frame");
    return GST_FLOW_ERROR;
  }

  if (!gst_hip_compositor_draw_background (self, &frame, stream)) {
    GST_ERROR_OBJECT (self, "Couldn't draw background");
    gst_video_frame_unmap (&frame);
    return GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&frame);

  GST_OBJECT_LOCK (self);
  for (iter = GST_ELEMENT (vagg)->sinkpads; iter; iter = g_list_next (iter)) {
    auto pad = GST_VIDEO_AGGREGATOR_PAD (iter->data);
    auto cpad = GST_HIP_COMPOSITOR_PAD (pad);
    auto pad_priv = cpad->priv;
    auto in_frame = gst_video_aggregator_pad_get_prepared_frame (pad);

    if (!in_frame)
      continue;

    if (!gst_hip_compositor_pad_setup_converter (pad, vagg)) {
      GST_ERROR_OBJECT (self, "Couldn't setup converter");
      ret = GST_FLOW_ERROR;
      break;
    }

    if (!gst_hip_converter_convert_frame (pad_priv->conv, in_frame->buffer,
            outbuf)) {
      GST_ERROR_OBJECT (pad, "Couldn't convert frame");
      ret = GST_FLOW_ERROR;
      break;
    }
  }
  GST_OBJECT_UNLOCK (self);

  if (ret == GST_FLOW_OK)
    HipStreamSynchronize (gst_hip_device_get_vendor (self->device), stream);

  return ret;
}
