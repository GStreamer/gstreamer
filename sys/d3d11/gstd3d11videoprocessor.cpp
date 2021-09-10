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

#include "gstd3d11videoprocessor.h"
#include "gstd3d11pluginutils.h"

#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_video_processor_debug);
#define GST_CAT_DEFAULT gst_d3d11_video_processor_debug

#if (GST_D3D11_HEADER_VERSION >= 1 && GST_D3D11_DXGI_HEADER_VERSION >= 4)
#define HAVE_VIDEO_CONTEXT_ONE
#endif

#if (GST_D3D11_HEADER_VERSION >= 4) && (GST_D3D11_DXGI_HEADER_VERSION >= 5)
#define HAVE_VIDEO_CONTEXT_TWO
#endif

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
#ifdef HAVE_VIDEO_CONTEXT_ONE
  ID3D11VideoProcessorEnumerator1 *enumerator1;
#endif
  D3D11_VIDEO_PROCESSOR_CAPS processor_caps;
};

GstD3D11VideoProcessor *
gst_d3d11_video_processor_new (GstD3D11Device * device, guint in_width,
    guint in_height, guint out_width, guint out_height)
{
  GstD3D11VideoProcessor *self;
  ID3D11VideoDevice *video_device;
  ID3D11VideoContext *video_context;
  HRESULT hr;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  video_device = gst_d3d11_device_get_video_device_handle (device);
  if (!video_device) {
    GST_WARNING_OBJECT (device, "ID3D11VideoDevice is not available");
    return NULL;
  }

  video_context = gst_d3d11_device_get_video_context_handle (device);
  if (!video_context) {
    GST_WARNING_OBJECT (device, "ID3D11VideoContext is not availale");
    return NULL;
  }

  memset (&desc, 0, sizeof (desc));

  self = g_new0 (GstD3D11VideoProcessor, 1);
  self->device = (GstD3D11Device *) gst_object_ref (device);

  self->video_device = video_device;
  video_device->AddRef ();

  self->video_context = video_context;
  video_context->AddRef ();

  /* FIXME: Add support intelace */
  desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  desc.InputWidth = in_width;
  desc.InputHeight = in_height;
  desc.OutputWidth = out_width;
  desc.OutputHeight = out_height;
  /* TODO: make option for this */
  desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

  hr = self->video_device->CreateVideoProcessorEnumerator (&desc,
      &self->enumerator);
  if (!gst_d3d11_result (hr, device))
    goto fail;
#ifdef HAVE_VIDEO_CONTEXT_ONE
  hr = self->enumerator->QueryInterface (IID_PPV_ARGS (&self->enumerator1));
  if (gst_d3d11_result (hr, device)) {
    GST_DEBUG ("ID3D11VideoProcessorEnumerator1 interface available");
  }
#endif

  hr = self->enumerator->GetVideoProcessorCaps (&self->processor_caps);
  if (!gst_d3d11_result (hr, device))
    goto fail;

  hr = self->video_device->CreateVideoProcessor (self->enumerator, 0,
      &self->processor);
  if (!gst_d3d11_result (hr, device))
    goto fail;

#ifdef HAVE_VIDEO_CONTEXT_ONE
  hr = self->video_context->
      QueryInterface (IID_PPV_ARGS (&self->video_context1));
  if (gst_d3d11_result (hr, device)) {
    GST_DEBUG ("ID3D11VideoContext1 interface available");
  }
#endif
#ifdef HAVE_VIDEO_CONTEXT_TWO
  hr = self->video_context->
      QueryInterface (IID_PPV_ARGS (&self->video_context2));
  if (gst_d3d11_result (hr, device)) {
    GST_DEBUG ("ID3D11VideoContext2 interface available");
  }
#endif

  /* Setting up default options */
  gst_d3d11_device_lock (self->device);
  /* We don't want auto processing by driver */
  self->video_context->VideoProcessorSetStreamAutoProcessingMode
      (self->processor, 0, FALSE);
  gst_d3d11_device_unlock (self->device);

  return self;

fail:
  gst_d3d11_video_processor_free (self);

  return NULL;
}

void
gst_d3d11_video_processor_free (GstD3D11VideoProcessor * processor)
{
  g_return_if_fail (processor != NULL);

  GST_D3D11_CLEAR_COM (processor->video_device);
  GST_D3D11_CLEAR_COM (processor->video_context);
#ifdef HAVE_VIDEO_CONTEXT_ONE
  GST_D3D11_CLEAR_COM (processor->video_context1);
#endif
#ifdef HAVE_VIDEO_CONTEXT_TWO
  GST_D3D11_CLEAR_COM (processor->video_context2);
#endif
  GST_D3D11_CLEAR_COM (processor->processor);
  GST_D3D11_CLEAR_COM (processor->enumerator);
#ifdef HAVE_VIDEO_CONTEXT_ONE
  GST_D3D11_CLEAR_COM (processor->enumerator1);
#endif

  gst_clear_object (&processor->device);
  g_free (processor);
}

static gboolean
gst_d3d11_video_processor_supports_format (GstD3D11VideoProcessor *
    self, DXGI_FORMAT format, gboolean is_input)
{
  HRESULT hr;
  UINT flag = 0;

  hr = self->enumerator->CheckVideoProcessorFormat (format, &flag);

  if (!gst_d3d11_result (hr, self->device))
    return FALSE;

  if (is_input) {
    /* D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT, missing in mingw header */
    if ((flag & 0x1) != 0)
      return TRUE;
  } else {
    /* D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT, missing in mingw header */
    if ((flag & 0x2) != 0)
      return TRUE;
  }

  return FALSE;
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

  processor->video_context->VideoProcessorSetStreamColorSpace
      (processor->processor, 0, &color_space);

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

  processor->video_context->VideoProcessorSetOutputColorSpace
      (processor->processor, &color_space);

  return TRUE;
}

#if (GST_D3D11_DXGI_HEADER_VERSION >= 4)
gboolean
gst_d3d11_video_processor_check_format_conversion (GstD3D11VideoProcessor *
    processor, DXGI_FORMAT in_format, DXGI_COLOR_SPACE_TYPE in_color_space,
    DXGI_FORMAT out_format, DXGI_COLOR_SPACE_TYPE out_color_space)
{
#ifdef HAVE_VIDEO_CONTEXT_ONE
  HRESULT hr;
  BOOL supported = TRUE;

  g_return_val_if_fail (processor != NULL, FALSE);

  if (!processor->enumerator1)
    return FALSE;

  hr = processor->enumerator1->CheckVideoProcessorFormatConversion
      (in_format, in_color_space, out_format, out_color_space, &supported);
  if (!gst_d3d11_result (hr, processor->device)) {
    GST_WARNING ("Failed to check conversion support");
    return FALSE;
  }

  return supported;
#endif

  return FALSE;
}

gboolean
gst_d3d11_video_processor_set_input_dxgi_color_space (GstD3D11VideoProcessor *
    processor, DXGI_COLOR_SPACE_TYPE color_space)
{
  g_return_val_if_fail (processor != NULL, FALSE);

#ifdef HAVE_VIDEO_CONTEXT_ONE
  if (processor->video_context1) {
    processor->video_context1->VideoProcessorSetStreamColorSpace1
        (processor->processor, 0, color_space);
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
    processor->video_context1->VideoProcessorSetOutputColorSpace1
        (processor->processor, color_space);
    return TRUE;
  }
#endif

  return FALSE;
}
#endif

#if (GST_D3D11_DXGI_HEADER_VERSION >= 5)
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
      processor->video_context2->VideoProcessorSetStreamHDRMetaData
          (processor->processor, 0,
          DXGI_HDR_METADATA_TYPE_HDR10, sizeof (DXGI_HDR_METADATA_HDR10),
          hdr10_meta);
    } else {
      processor->video_context2->VideoProcessorSetStreamHDRMetaData
          (processor->processor, 0, DXGI_HDR_METADATA_TYPE_NONE, 0, NULL);
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
      processor->video_context2->VideoProcessorSetOutputHDRMetaData
          (processor->processor, DXGI_HDR_METADATA_TYPE_HDR10,
          sizeof (DXGI_HDR_METADATA_HDR10), hdr10_meta);
    } else {
      processor->video_context2->VideoProcessorSetOutputHDRMetaData
          (processor->processor, DXGI_HDR_METADATA_TYPE_NONE, 0, NULL);
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

  hr = processor->video_device->CreateVideoProcessorInputView (resource,
      processor->enumerator, desc, view);
  if (!gst_d3d11_result (hr, processor->device))
    return FALSE;

  return TRUE;
}

ID3D11VideoProcessorInputView *
gst_d3d11_video_processor_get_input_view (GstD3D11VideoProcessor * processor,
    GstD3D11Memory * mem)
{
  return gst_d3d11_memory_get_processor_input_view (mem,
      processor->video_device, processor->enumerator);
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

  hr = processor->video_device->CreateVideoProcessorOutputView
      (resource, processor->enumerator, desc, view);
  if (!gst_d3d11_result (hr, processor->device))
    return FALSE;

  return TRUE;
}

ID3D11VideoProcessorOutputView *
gst_d3d11_video_processor_get_output_view (GstD3D11VideoProcessor *
    processor, GstD3D11Memory * mem)
{
  return gst_d3d11_memory_get_processor_output_view (mem,
      processor->video_device, processor->enumerator);
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
  ID3D11VideoContext *context;
  ID3D11VideoProcessor *proc;

  g_return_val_if_fail (processor != NULL, FALSE);
  g_return_val_if_fail (in_view != NULL, FALSE);
  g_return_val_if_fail (out_view != NULL, FALSE);

  stream.Enable = TRUE;
  stream.pInputSurface = in_view;
  context = processor->video_context;
  proc = processor->processor;

  if (in_rect) {
    context->VideoProcessorSetStreamSourceRect (proc, 0, TRUE, in_rect);
  } else {
    context->VideoProcessorSetStreamSourceRect (proc, 0, FALSE, NULL);
  }

  if (out_rect) {
    context->VideoProcessorSetStreamDestRect (proc, 0, TRUE, out_rect);
    context->VideoProcessorSetOutputTargetRect (proc, TRUE, out_rect);
  } else {
    context->VideoProcessorSetStreamDestRect (proc, 0, FALSE, NULL);
    context->VideoProcessorSetOutputTargetRect (proc, FALSE, NULL);
  }

  hr = context->VideoProcessorBlt (proc, out_view, 0, 1, &stream);
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
