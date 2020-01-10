/* GStreamer
 * Copyright (C) <2020> Seungha Yang <seungha.yang@navercorp.com>
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
#  include <config.h>
#endif

#include "gstd3d11device.h"
#include "gstd3d11utils.h"
#include "gstd3d11format.h"
#include "gstd3d11memory.h"
#include "gstd3d11videoprocessor.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_video_processor_debug);
#define GST_CAT_DEFAULT gst_d3d11_video_processor_debug

#if (D3D11_HEADER_VERSION >= 1 && DXGI_HEADER_VERSION >= 4)
#define HAVE_VIDEO_CONTEXT_ONE
#endif

#if (D3D11_HEADER_VERSION >= 4) && (DXGI_HEADER_VERSION >= 5)
#define HAVE_VIDEO_CONTEXT_TWO
#endif

GQuark
gst_d3d11_video_processor_input_view_quark (void)
{
  static volatile gsize quark = 0;

  if (g_once_init_enter (&quark)) {
    GQuark q = g_quark_from_static_string ("GstD3D11VideoProcessorInputView");
    g_once_init_leave (&quark, (gsize) q);
  }

  return (GQuark) quark;
}

GQuark
gst_d3d11_video_processor_output_view_quark (void)
{
  static volatile gsize quark = 0;

  if (g_once_init_enter (&quark)) {
    GQuark q = g_quark_from_static_string ("GstD3D11VideoProcessorOutputView");
    g_once_init_leave (&quark, (gsize) q);
  }

  return (GQuark) quark;
}

struct _GstD3D11VideoProcessor
{
  GstD3D11Device *device;

  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;
#ifdef HAVE_VIDEO_CONTEXT_ONE
  ID3D11VideoContext1 *video_context1;
#endif
#ifdef HAVE_VIDEO_CONTEXT_TWO
  ID3D11VideoContext2 *video_context2;
#endif
  ID3D11VideoProcessor *processor;
  ID3D11VideoProcessorEnumerator *enumerator;

  D3D11_VIDEO_PROCESSOR_CAPS processor_caps;
};

GstD3D11VideoProcessor *
gst_d3d11_video_processor_new (GstD3D11Device * device, guint in_width,
    guint in_height, guint out_width, guint out_height)
{
  GstD3D11VideoProcessor *self;
  ID3D11Device *device_handle;
  ID3D11DeviceContext *context_handle;
  HRESULT hr;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = { 0, };

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  device_handle = gst_d3d11_device_get_device_handle (device);
  context_handle = gst_d3d11_device_get_device_context_handle (device);

  self = g_new0 (GstD3D11VideoProcessor, 1);
  self->device = gst_object_ref (device);

  hr = ID3D11Device_QueryInterface (device_handle,
      &IID_ID3D11VideoDevice, (void **) &self->video_device);
  if (!gst_d3d11_result (hr, device))
    goto fail;

  hr = ID3D11DeviceContext_QueryInterface (context_handle,
      &IID_ID3D11VideoContext, (void **) &self->video_context);
  if (!gst_d3d11_result (hr, device))
    goto fail;

  /* FIXME: Add support intelace */
  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth = in_width;
  desc.InputHeight = in_height;
  desc.OutputWidth = out_width;
  desc.OutputHeight = out_height;
  /* TODO: make option for this */
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  hr = ID3D11VideoDevice_CreateVideoProcessorEnumerator (self->video_device,
      &desc, &self->enumerator);
  if (!gst_d3d11_result (hr, device))
    goto fail;

  hr = ID3D11VideoProcessorEnumerator_GetVideoProcessorCaps (self->enumerator,
      &self->processor_caps);
  if (!gst_d3d11_result (hr, device))
    goto fail;

  hr = ID3D11VideoDevice_CreateVideoProcessor (self->video_device,
      self->enumerator, 0, &self->processor);
  if (!gst_d3d11_result (hr, device))
    goto fail;

#ifdef HAVE_VIDEO_CONTEXT_ONE
  hr = ID3D11VideoContext_QueryInterface (self->video_context,
      &IID_ID3D11VideoContext1, (void **) &self->video_context1);
  if (gst_d3d11_result (hr, device)) {
    GST_DEBUG ("ID3D11VideoContext1 interface available");
  }
#endif
#ifdef HAVE_VIDEO_CONTEXT_TWO
  hr = ID3D11VideoContext_QueryInterface (self->video_context,
      &IID_ID3D11VideoContext2, (void **) &self->video_context2);
  if (gst_d3d11_result (hr, device)) {
    GST_DEBUG ("ID3D11VideoContext2 interface available");
  }
#endif

  return self;

fail:
  gst_d3d11_video_processor_free (self);

  return NULL;
}

void
gst_d3d11_video_processor_free (GstD3D11VideoProcessor * processor)
{
  g_return_if_fail (processor != NULL);

  if (processor->video_device)
    ID3D11VideoDevice_Release (processor->video_device);
  if (processor->video_context)
    ID3D11VideoContext_Release (processor->video_context);
#ifdef HAVE_VIDEO_CONTEXT_ONE
  if (processor->video_context1)
    ID3D11VideoContext1_Release (processor->video_context1);
#endif
#ifdef HAVE_VIDEO_CONTEXT_TWO
  if (processor->video_context2)
    ID3D11VideoContext2_Release (processor->video_context2);
#endif
  if (processor->processor)
    ID3D11VideoProcessor_Release (processor->processor);
  if (processor->enumerator)
    ID3D11VideoProcessorEnumerator_Release (processor->enumerator);

  gst_clear_object (&processor->device);
  g_free (processor);
}

static gboolean
gst_d3d11_video_processor_supports_format (GstD3D11VideoProcessor *
    self, DXGI_FORMAT format, gboolean is_input)
{
  HRESULT hr;
  UINT flag;

  if (is_input) {
    /* D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT, missing in mingw header */
    flag = 1;
  } else {
    /* D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT, missing in mingw header */
    flag = 2;
  }

  hr = ID3D11VideoProcessorEnumerator_CheckVideoProcessorFormat
      (self->enumerator, format, &flag);

  return gst_d3d11_result (hr, self->device);
}

gboolean
gst_d3d11_video_processor_supports_input_format (GstD3D11VideoProcessor *
    processor, DXGI_FORMAT format)
{
  g_return_val_if_fail (processor != NULL, FALSE);

  if (format == DXGI_FORMAT_UNKNOWN)
    return FALSE;

  return gst_d3d11_video_processor_supports_format (processor, format, TRUE);
}

gboolean
gst_d3d11_video_processor_supports_output_format (GstD3D11VideoProcessor *
    processor, DXGI_FORMAT format)
{
  g_return_val_if_fail (processor != NULL, FALSE);

  if (format == DXGI_FORMAT_UNKNOWN)
    return FALSE;

  return gst_d3d11_video_processor_supports_format (processor, format, FALSE);
}

gboolean
gst_d3d11_video_processor_get_caps (GstD3D11VideoProcessor * processor,
    D3D11_VIDEO_PROCESSOR_CAPS * caps)
{
  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  *caps = processor->processor_caps;

  return TRUE;
}

static void
video_processor_color_space_from_gst (GstD3D11VideoProcessor * self,
    GstVideoColorimetry * color, D3D11_VIDEO_PROCESSOR_COLOR_SPACE * colorspace)
{
  /* D3D11_VIDEO_PROCESSOR_DEVICE_CAPS_xvYCC */
  UINT can_xvYCC = 2;

  /* 0: playback, 1: video processing */
  colorspace->Usage = 0;

  if (color->range == GST_VIDEO_COLOR_RANGE_0_255) {
    colorspace->RGB_Range = 0;
    colorspace->Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
  } else {
    /* 16-235 */
    colorspace->RGB_Range = 1;
    colorspace->Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  }

  if (color->matrix == GST_VIDEO_COLOR_MATRIX_BT601) {
    colorspace->YCbCr_Matrix = 0;
  } else {
    /* BT.709, no other options (such as BT2020) */
    colorspace->YCbCr_Matrix = 1;
  }

  if ((self->processor_caps.DeviceCaps & can_xvYCC) == can_xvYCC) {
    colorspace->YCbCr_xvYCC = 1;
  } else {
    colorspace->YCbCr_xvYCC = 0;
  }
}

gboolean
gst_d3d11_video_processor_set_input_color_space (GstD3D11VideoProcessor *
    processor, GstVideoColorimetry * color)
{
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE color_space;

  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);

  video_processor_color_space_from_gst (processor, color, &color_space);

  ID3D11VideoContext_VideoProcessorSetStreamColorSpace
      (processor->video_context, processor->processor, 0, &color_space);

  return TRUE;
}

gboolean
gst_d3d11_video_processor_set_output_color_space (GstD3D11VideoProcessor *
    processor, GstVideoColorimetry * color)
{
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE color_space;

  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (color != NULL, FALSE);

  video_processor_color_space_from_gst (processor, color, &color_space);

  ID3D11VideoContext_VideoProcessorSetOutputColorSpace
      (processor->video_context, processor->processor, &color_space);

  return TRUE;
}

#if (DXGI_HEADER_VERSION >= 4)
gboolean
gst_d3d11_video_processor_set_input_dxgi_color_space (GstD3D11VideoProcessor *
    processor, DXGI_COLOR_SPACE_TYPE color_space)
{
  g_return_val_if_fail (processor != NULL, FALSE);

#ifdef HAVE_VIDEO_CONTEXT_ONE
  if (processor->video_context1) {
    ID3D11VideoContext1_VideoProcessorSetStreamColorSpace1
        (processor->video_context1, processor->processor, 0, color_space);
    return TRUE;
  }
#endif

  return FALSE;
}

gboolean
gst_d3d11_video_processor_set_output_dxgi_color_space (GstD3D11VideoProcessor *
    processor, DXGI_COLOR_SPACE_TYPE color_space)
{
  g_return_val_if_fail (processor != NULL, FALSE);

#ifdef HAVE_VIDEO_CONTEXT_ONE
  if (processor->video_context1) {
    ID3D11VideoContext1_VideoProcessorSetOutputColorSpace1
        (processor->video_context1, processor->processor, color_space);
    return TRUE;
  }
#endif

  return FALSE;
}
#endif

#if (DXGI_HEADER_VERSION >= 5)
/* D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_METADATA_HDR10
 * missing in mingw header */
#define FEATURE_CAPS_METADATA_HDR10 (0x800)

gboolean
gst_d3d11_video_processor_set_input_hdr10_metadata (GstD3D11VideoProcessor *
    processor, DXGI_HDR_METADATA_HDR10 * hdr10_meta)
{
  g_return_val_if_fail (processor != NULL, FALSE);

#ifdef HAVE_VIDEO_CONTEXT_TWO
  if (processor->video_context2 && (processor->processor_caps.FeatureCaps &
          FEATURE_CAPS_METADATA_HDR10)) {
    if (hdr10_meta) {
      ID3D11VideoContext2_VideoProcessorSetStreamHDRMetaData
          (processor->video_context2, processor->processor, 0,
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof (DXGI_HDR_METADATA_HDR10),
          hdr10_meta);
    } else {
      ID3D11VideoContext2_VideoProcessorSetStreamHDRMetaData
          (processor->video_context2, processor->processor, 0,
          DXGI_HDR_METADATA_TYPE_NONE, 0, NULL);
    }

    return TRUE;
  }
#endif

  return FALSE;
}

gboolean
gst_d3d11_video_processor_set_output_hdr10_metadata (GstD3D11VideoProcessor *
    processor, DXGI_HDR_METADATA_HDR10 * hdr10_meta)
{
  g_return_val_if_fail (processor != NULL, FALSE);

#ifdef HAVE_VIDEO_CONTEXT_TWO
  if (processor->video_context2 && (processor->processor_caps.FeatureCaps &
          FEATURE_CAPS_METADATA_HDR10)) {
    if (hdr10_meta) {
      ID3D11VideoContext2_VideoProcessorSetOutputHDRMetaData
          (processor->video_context2, processor->processor,
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof (DXGI_HDR_METADATA_HDR10),
          hdr10_meta);
    } else {
      ID3D11VideoContext2_VideoProcessorSetOutputHDRMetaData
          (processor->video_context2, processor->processor,
          DXGI_HDR_METADATA_TYPE_NONE, 0, NULL);
    }

    return TRUE;
  }
#endif

  return FALSE;
}
#endif

gboolean
gst_d3d11_video_processor_create_input_view (GstD3D11VideoProcessor * processor,
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC * desc, ID3D11Resource * resource,
    ID3D11VideoProcessorInputView ** view)
{
  HRESULT hr;

  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (desc != NULL, FALSE);
  g_return_val_if_fail (resource != NULL, FALSE);
  g_return_val_if_fail (view != NULL, FALSE);

  hr = ID3D11VideoDevice_CreateVideoProcessorInputView (processor->video_device,
      resource, processor->enumerator, desc, view);
  if (!gst_d3d11_result (hr, processor->device))
    return FALSE;

  return TRUE;
}

gboolean
gst_d3d11_video_processor_create_output_view (GstD3D11VideoProcessor *
    processor, D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC * desc,
    ID3D11Resource * resource, ID3D11VideoProcessorOutputView ** view)
{
  HRESULT hr;

  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (desc != NULL, FALSE);
  g_return_val_if_fail (resource != NULL, FALSE);
  g_return_val_if_fail (view != NULL, FALSE);

  hr = ID3D11VideoDevice_CreateVideoProcessorOutputView
      (processor->video_device, resource, processor->enumerator, desc, view);
  if (!gst_d3d11_result (hr, processor->device))
    return FALSE;

  return TRUE;
}

void
gst_d3d11_video_processor_input_view_release (ID3D11VideoProcessorInputView *
    view)
{
  if (!view)
    return;

  ID3D11VideoProcessorInputView_Release (view);
}

void
gst_d3d11_video_processor_output_view_release (ID3D11VideoProcessorOutputView *
    view)
{
  if (!view)
    return;

  ID3D11VideoProcessorOutputView_Release (view);
}

gboolean
gst_d3d11_video_processor_render (GstD3D11VideoProcessor * processor,
    RECT * in_rect, ID3D11VideoProcessorInputView * in_view,
    RECT * out_rect, ID3D11VideoProcessorOutputView * out_view)
{
  gboolean ret;

  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (in_view != NULL, FALSE);
  g_return_val_if_fail (out_view != NULL, FALSE);

  gst_d3d11_device_lock (processor->device);
  ret = gst_d3d11_video_processor_render_unlocked (processor, in_rect, in_view,
      out_rect, out_view);
  gst_d3d11_device_unlock (processor->device);

  return ret;
}

gboolean
gst_d3d11_video_processor_render_unlocked (GstD3D11VideoProcessor * processor,
    RECT * in_rect, ID3D11VideoProcessorInputView * in_view,
    RECT * out_rect, ID3D11VideoProcessorOutputView * out_view)
{
  HRESULT hr;
  D3D11_VIDEO_PROCESSOR_STREAM stream = { 0, };

  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (in_view != NULL, FALSE);
  g_return_val_if_fail (out_view != NULL, FALSE);

  stream.Enable = TRUE;
  stream.pInputSurface = in_view;

  if (in_rect) {
    ID3D11VideoContext_VideoProcessorSetStreamSourceRect
        (processor->video_context, processor->processor, 0, TRUE, in_rect);
  } else {
    ID3D11VideoContext_VideoProcessorSetStreamSourceRect
        (processor->video_context, processor->processor, 0, FALSE, NULL);
  }

  if (out_rect) {
    ID3D11VideoContext_VideoProcessorSetStreamDestRect
        (processor->video_context, processor->processor, 0, TRUE, out_rect);
    ID3D11VideoContext_VideoProcessorSetOutputTargetRect
        (processor->video_context, processor->processor, TRUE, out_rect);
  } else {
    ID3D11VideoContext_VideoProcessorSetStreamDestRect
        (processor->video_context, processor->processor, 0, FALSE, NULL);
    ID3D11VideoContext_VideoProcessorSetOutputTargetRect
        (processor->video_context, processor->processor, FALSE, NULL);
  }

  hr = ID3D11VideoContext_VideoProcessorBlt (processor->video_context,
      processor->processor, out_view, 0, 1, &stream);
  if (!gst_d3d11_result (hr, processor->device))
    return FALSE;

  return TRUE;
}

gboolean
gst_d3d11_video_processor_check_bind_flags_for_input_view (guint bind_flags)
{
  static const guint compatible_flags = (D3D11_BIND_DECODER |
      D3D11_BIND_VIDEO_ENCODER | D3D11_BIND_RENDER_TARGET |
      D3D11_BIND_UNORDERED_ACCESS);

  if (bind_flags == 0)
    return TRUE;

  if ((bind_flags & compatible_flags) != 0)
    return TRUE;

  return FALSE;
}

gboolean
gst_d3d11_video_processor_check_bind_flags_for_output_view (guint bind_flags)
{
  if ((bind_flags & D3D11_BIND_RENDER_TARGET) == D3D11_BIND_RENDER_TARGET)
    return TRUE;

  return FALSE;
}
