/*
 * GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d11window_dummy.h"
#include "gstd3d11pluginutils.h"
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (gst_d3d11_window_debug);
#define GST_CAT_DEFAULT gst_d3d11_window_debug

G_END_DECLS
/* *INDENT-ON* */

struct _GstD3D11WindowDummy
{
  GstD3D11Window parent;

  ID3D11Texture2D *fallback_texture;
  ID3D11VideoProcessorOutputView *fallback_pov;
  ID3D11RenderTargetView *fallback_rtv;
};

#define gst_d3d11_window_dummy_parent_class parent_class
G_DEFINE_TYPE (GstD3D11WindowDummy, gst_d3d11_window_dummy,
    GST_TYPE_D3D11_WINDOW);

static void gst_d3d11_window_dummy_on_resize (GstD3D11Window * window,
    guint width, guint height);
static gboolean gst_d3d11_window_dummy_prepare (GstD3D11Window * window,
    guint display_width, guint display_height, GstCaps * caps,
    gboolean * video_processor_available, GError ** error);
static void gst_d3d11_window_dummy_unprepare (GstD3D11Window * window);
static gboolean
gst_d3d11_window_dummy_open_shared_handle (GstD3D11Window * window,
    GstD3D11WindowSharedHandleData * data);
static gboolean
gst_d3d11_window_dummy_release_shared_handle (GstD3D11Window * window,
    GstD3D11WindowSharedHandleData * data);

static void
gst_d3d11_window_dummy_class_init (GstD3D11WindowDummyClass * klass)
{
  GstD3D11WindowClass *window_class = GST_D3D11_WINDOW_CLASS (klass);

  window_class->on_resize =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_dummy_on_resize);
  window_class->prepare = GST_DEBUG_FUNCPTR (gst_d3d11_window_dummy_prepare);
  window_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_dummy_unprepare);
  window_class->open_shared_handle =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_dummy_open_shared_handle);
  window_class->release_shared_handle =
      GST_DEBUG_FUNCPTR (gst_d3d11_window_dummy_release_shared_handle);
}

static void
gst_d3d11_window_dummy_init (GstD3D11WindowDummy * self)
{
}

static gboolean
gst_d3d11_window_dummy_prepare (GstD3D11Window * window,
    guint display_width, guint display_height, GstCaps * caps,
    gboolean * video_processor_available, GError ** error)
{
  g_clear_pointer (&window->processor, gst_d3d11_video_processor_free);
  g_clear_pointer (&window->converter, gst_d3d11_converter_free);
  g_clear_pointer (&window->compositor, gst_d3d11_overlay_compositor_free);

  /* We are supporting only RGBA, BGRA or RGB10A2_LE formats but we don't know
   * which format texture will be used at this moment */

  gst_video_info_from_caps (&window->info, caps);
  window->render_rect.left = 0;
  window->render_rect.top = 0;
  window->render_rect.right = display_width;
  window->render_rect.bottom = display_height;

  window->input_rect.left = 0;
  window->input_rect.top = 0;
  window->input_rect.right = GST_VIDEO_INFO_WIDTH (&window->info);
  window->input_rect.bottom = GST_VIDEO_INFO_HEIGHT (&window->info);

  gst_video_info_set_format (&window->render_info,
      GST_VIDEO_FORMAT_BGRA, display_width, display_height);

  /* TODO: not sure which colorspace should be used, let's use BT709 since
   * it's default and most common one */
  window->render_info.colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
  window->render_info.colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
  window->render_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

  gst_d3d11_device_lock (window->device);

#if (GST_D3D11_DXGI_HEADER_VERSION >= 4)
  {
    const GstDxgiColorSpace *in_color_space =
        gst_d3d11_video_info_to_dxgi_color_space (&window->info);
    const GstD3D11Format *in_format =
        gst_d3d11_device_format_from_gst (window->device,
        GST_VIDEO_INFO_FORMAT (&window->info));
    gboolean hardware = FALSE;
    GstD3D11VideoProcessor *processor = NULL;
    guint i;
    DXGI_FORMAT formats_to_check[] = {
      DXGI_FORMAT_R8G8B8A8_UNORM,
      DXGI_FORMAT_B8G8R8A8_UNORM,
      DXGI_FORMAT_R10G10B10A2_UNORM
    };

    if (in_color_space && in_format &&
        in_format->dxgi_format != DXGI_FORMAT_UNKNOWN) {
      g_object_get (window->device, "hardware", &hardware, NULL);
    }

    if (hardware) {
      processor =
          gst_d3d11_video_processor_new (window->device,
          GST_VIDEO_INFO_WIDTH (&window->info),
          GST_VIDEO_INFO_HEIGHT (&window->info), display_width, display_height);
    }

    /* Check if video processor can support all possible output dxgi formats */
    for (i = 0; i < G_N_ELEMENTS (formats_to_check) && processor; i++) {
      DXGI_FORMAT in_dxgi_format = in_format->dxgi_format;
      DXGI_FORMAT out_dxgi_format = formats_to_check[i];
      DXGI_COLOR_SPACE_TYPE in_dxgi_color_space =
          (DXGI_COLOR_SPACE_TYPE) in_color_space->dxgi_color_space_type;

      if (!gst_d3d11_video_processor_check_format_conversion (processor,
              in_dxgi_format, in_dxgi_color_space, out_dxgi_format,
              DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)) {
        GST_DEBUG_OBJECT (window, "Conversion is not supported by device");
        g_clear_pointer (&processor, gst_d3d11_video_processor_free);
        break;
      }
    }

    if (processor) {
      gst_d3d11_video_processor_set_input_dxgi_color_space (processor,
          (DXGI_COLOR_SPACE_TYPE) in_color_space->dxgi_color_space_type);
      gst_d3d11_video_processor_set_output_dxgi_color_space (processor,
          DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }

    window->processor = processor;
  }
#endif
  *video_processor_available = !!window->processor;

  window->converter =
      gst_d3d11_converter_new (window->device, &window->info,
      &window->render_info, nullptr);

  if (!window->converter) {
    GST_ERROR_OBJECT (window, "Cannot create converter");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create converter");
    goto error;
  }

  window->compositor =
      gst_d3d11_overlay_compositor_new (window->device, &window->render_info);
  if (!window->compositor) {
    GST_ERROR_OBJECT (window, "Cannot create overlay compositor");
    g_set_error (error, GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_FAILED,
        "Cannot create overlay compositor");
    goto error;
  }

  gst_d3d11_device_unlock (window->device);

  return TRUE;

error:
  gst_d3d11_device_unlock (window->device);

  return FALSE;
}

static void
gst_d3d11_window_dummy_clear_resources (GstD3D11WindowDummy * self)
{
  GST_D3D11_CLEAR_COM (self->fallback_pov);
  GST_D3D11_CLEAR_COM (self->fallback_rtv);
  GST_D3D11_CLEAR_COM (self->fallback_texture);
}

static void
gst_d3d11_window_dummy_unprepare (GstD3D11Window * window)
{
  GstD3D11WindowDummy *self = GST_D3D11_WINDOW_DUMMY (window);

  gst_d3d11_window_dummy_clear_resources (self);
}

static void
gst_d3d11_window_dummy_on_resize (GstD3D11Window * window,
    guint width, guint height)
{
  GstVideoRectangle src_rect, dst_rect, rst_rect;

  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.w = width;
  dst_rect.h = height;

  if (window->force_aspect_ratio) {
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.w = GST_VIDEO_INFO_WIDTH (&window->render_info);
    src_rect.h = GST_VIDEO_INFO_HEIGHT (&window->render_info);

    gst_video_sink_center_rect (src_rect, dst_rect, &rst_rect, TRUE);
  } else {
    rst_rect = dst_rect;
  }

  window->render_rect.left = rst_rect.x;
  window->render_rect.top = rst_rect.y;
  window->render_rect.right = rst_rect.x + rst_rect.w;
  window->render_rect.bottom = rst_rect.y + rst_rect.h;

  window->first_present = TRUE;
}

static gboolean
gst_d3d11_window_dummy_setup_fallback_texture (GstD3D11Window * window,
    D3D11_TEXTURE2D_DESC * shared_desc)
{
  GstD3D11WindowDummy *self = GST_D3D11_WINDOW_DUMMY (window);
  D3D11_TEXTURE2D_DESC desc = { 0, };
  D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
  ID3D11Device *device_handle =
      gst_d3d11_device_get_device_handle (window->device);
  gboolean need_new_texture = FALSE;
  HRESULT hr;

  if (!self->fallback_texture) {
    GST_DEBUG_OBJECT (self,
        "We have no configured fallback texture, create new one");
    need_new_texture = TRUE;
  } else {
    self->fallback_texture->GetDesc (&desc);
    if (shared_desc->Format != desc.Format) {
      GST_DEBUG_OBJECT (self, "Texture formats are different, create new one");
      need_new_texture = TRUE;
    } else if (shared_desc->Width > desc.Width ||
        shared_desc->Height > desc.Height) {
      GST_DEBUG_OBJECT (self, "Needs larger size of fallback texture");
      need_new_texture = TRUE;
    }
  }

  if (!need_new_texture)
    return TRUE;

  gst_d3d11_window_dummy_clear_resources (self);

  desc.Width = shared_desc->Width;
  desc.Height = shared_desc->Height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = shared_desc->Format;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

  hr = device_handle->CreateTexture2D (&desc, NULL, &self->fallback_texture);
  if (!gst_d3d11_result (hr, window->device)) {
    GST_ERROR_OBJECT (self, "Couldn't create fallback texture");
    return FALSE;
  }

  rtv_desc.Format = DXGI_FORMAT_UNKNOWN;
  rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
  rtv_desc.Texture2D.MipSlice = 0;

  hr = device_handle->CreateRenderTargetView (self->fallback_texture, &rtv_desc,
      &self->fallback_rtv);
  if (!gst_d3d11_result (hr, window->device)) {
    GST_ERROR_OBJECT (self,
        "Couldn't get render target view from fallback texture");
    gst_d3d11_window_dummy_clear_resources (self);
    return FALSE;
  }

  if (window->processor) {
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC pov_desc;

    pov_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    pov_desc.Texture2D.MipSlice = 0;

    if (!gst_d3d11_video_processor_create_output_view (window->processor,
            &pov_desc, (ID3D11Resource *) self->fallback_texture,
            &self->fallback_pov)) {
      GST_ERROR_OBJECT (window,
          "ID3D11VideoProcessorOutputView is unavailable");
      gst_d3d11_window_dummy_clear_resources (self);
      return FALSE;
    }
  }

  return TRUE;
}

/* *INDENT-OFF* */
static gboolean
gst_d3d11_window_dummy_open_shared_handle (GstD3D11Window * window,
    GstD3D11WindowSharedHandleData * data)
{
  GstD3D11WindowDummy *self = GST_D3D11_WINDOW_DUMMY (window);
  GstD3D11Device *device = window->device;
  ID3D11Device *device_handle;
  HRESULT hr;
  ID3D11Texture2D *texture = NULL;
  IDXGIKeyedMutex *keyed_mutex = NULL;
  ID3D11VideoProcessorOutputView *pov = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  D3D11_TEXTURE2D_DESC desc;
  gboolean use_keyed_mutex = FALSE;
  gboolean need_fallback_texture = FALSE;

  device_handle = gst_d3d11_device_get_device_handle (device);

  if ((data->texture_misc_flags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) ==
      D3D11_RESOURCE_MISC_SHARED_NTHANDLE) {
    ComPtr<ID3D11Device1> device1_handle;

    hr = device_handle->QueryInterface (IID_PPV_ARGS (&device1_handle));
    if (!gst_d3d11_result (hr, device))
      return FALSE;

    hr = device1_handle->OpenSharedResource1 (data->shared_handle,
        IID_PPV_ARGS (&texture));
  } else {
    hr = device_handle->OpenSharedResource (data->shared_handle,
        IID_PPV_ARGS (&texture));
  }

  if (!gst_d3d11_result (hr, device))
    return FALSE;

  texture->GetDesc (&desc);
  use_keyed_mutex = (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) ==
      D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

  if (use_keyed_mutex) {
    hr = texture->QueryInterface (IID_PPV_ARGS (&keyed_mutex));
    if (!gst_d3d11_result (hr, device))
      goto out;
  }

  if (window->processor) {
    if (use_keyed_mutex) {
      D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC pov_desc;

      pov_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
      pov_desc.Texture2D.MipSlice = 0;

      if (!gst_d3d11_video_processor_create_output_view (window->processor,
          &pov_desc, (ID3D11Resource *) texture, &pov)) {
        GST_WARNING_OBJECT (window,
            "ID3D11VideoProcessorOutputView is unavailable");
      }
    } else {
      /* HACK: If external texture was created without keyed mutext
       * and we need to used videoprocessor to convert decoder output texture
       * to external texture, converted texture by videoprocessor seems to be broken
       * Probably that's because of missing flush/sync API around videoprocessor.
       * (e.g., ID3D11VideoContext and ID3D11VideoProcessor have no
       * flushing api such as ID3D11DeviceContext::Flush).
       * To workaround the case, we need to use fallback texture and copy back
       * to external texture
       */

      need_fallback_texture = TRUE;

      GST_TRACE_OBJECT (window,
          "We are using video processor but keyed mutex is unavailable");
      if (!gst_d3d11_window_dummy_setup_fallback_texture (window, &desc)) {
        goto out;
      }
    }
  }

  hr = device_handle->CreateRenderTargetView ((ID3D11Resource *) texture,
      NULL, &rtv);
  if (!gst_d3d11_result (hr, device))
    goto out;

  if (keyed_mutex) {
    hr = keyed_mutex->AcquireSync(data->acquire_key, INFINITE);
    if (!gst_d3d11_result (hr, device))
      goto out;
  }

  /* Everything is prepared now */
  gst_d3d11_window_dummy_on_resize (window, desc.Width, desc.Height);

  /* Move owned resources */
  data->texture = texture;
  data->keyed_mutex = keyed_mutex;
  data->pov = pov;
  data->rtv = rtv;

  if (need_fallback_texture) {
    data->fallback_pov = self->fallback_pov;
    data->fallback_rtv = self->fallback_rtv;
  } else {
    data->fallback_pov = nullptr;
    data->fallback_rtv = nullptr;
  }

  return TRUE;

out:
  GST_D3D11_CLEAR_COM (texture);
  GST_D3D11_CLEAR_COM (keyed_mutex);
  GST_D3D11_CLEAR_COM (pov);
  GST_D3D11_CLEAR_COM (rtv);

  return FALSE;
}
/* *INDENT-ON* */

static gboolean
gst_d3d11_window_dummy_release_shared_handle (GstD3D11Window * window,
    GstD3D11WindowSharedHandleData * data)
{
  GstD3D11WindowDummy *self = GST_D3D11_WINDOW_DUMMY (window);
  GstD3D11Device *device = window->device;
  HRESULT hr;

  /* TODO: cache owned resource for the later reuse? */
  if (data->keyed_mutex) {
    hr = data->keyed_mutex->ReleaseSync (data->release_key);
    gst_d3d11_result (hr, device);

    data->keyed_mutex->Release ();
  } else {
    /* *INDENT-OFF* */
    ComPtr<ID3D11Query> query;
    /* *INDENT-ON* */
    D3D11_QUERY_DESC query_desc;
    ID3D11Device *device_handle = gst_d3d11_device_get_device_handle (device);
    ID3D11DeviceContext *context_handle =
        gst_d3d11_device_get_device_context_handle (device);
    BOOL sync_done = FALSE;

    /* If keyed mutex is not used, let's handle sync manually by using
     * ID3D11Query. Issued GPU commands might not be finished yet */
    query_desc.Query = D3D11_QUERY_EVENT;
    query_desc.MiscFlags = 0;

    hr = device_handle->CreateQuery (&query_desc, &query);
    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't Create event query");
      return FALSE;
    }

    /* Copy from fallback texture to user's texture */
    if (data->fallback_rtv) {
      D3D11_BOX src_box;
      D3D11_TEXTURE2D_DESC desc;
      ID3D11DeviceContext *context_handle =
          gst_d3d11_device_get_device_context_handle (device);

      data->texture->GetDesc (&desc);

      src_box.left = 0;
      src_box.top = 0;
      src_box.front = 0;
      src_box.back = 1;
      src_box.right = desc.Width;
      src_box.bottom = desc.Height;

      context_handle->CopySubresourceRegion (data->texture, 0, 0, 0, 0,
          self->fallback_texture, 0, &src_box);
    }
    context_handle->End (query.Get ());

    /* Wait until all issued GPU commands are finished */
    do {
      context_handle->GetData (query.Get (), &sync_done, sizeof (BOOL), 0);
    } while (!sync_done && (hr == S_OK || hr == S_FALSE));

    if (!gst_d3d11_result (hr, device)) {
      GST_ERROR_OBJECT (self, "Couldn't sync GPU operation");
      return FALSE;
    }
  }

  GST_D3D11_CLEAR_COM (data->rtv);
  GST_D3D11_CLEAR_COM (data->pov);
  GST_D3D11_CLEAR_COM (data->texture);

  return TRUE;
}

GstD3D11Window *
gst_d3d11_window_dummy_new (GstD3D11Device * device)
{
  GstD3D11Window *window;

  g_return_val_if_fail (GST_IS_D3D11_DEVICE (device), NULL);

  window = (GstD3D11Window *)
      g_object_new (GST_TYPE_D3D11_WINDOW_DUMMY, "d3d11device", device, NULL);

  window->initialized = TRUE;
  g_object_ref_sink (window);

  return window;
}
