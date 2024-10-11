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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d12/gstd3d12.h>
#include <d3d11.h>
#include <d2d1_3.h>
#include <d3d11on12.h>
#include <dwrite.h>
#include <wrl.h>
#include <string>
#include <locale>
#include <codecvt>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;

struct OverlayContext
{
  GstElement *pipeline = nullptr;

  ComPtr<IDWriteFactory> dwrite_factory;
  ComPtr<IDWriteTextFormat> format;
  ComPtr<IDWriteTextLayout> layout;

  std::wstring text;

  gint width = 0;
  gint height = 0;
  FLOAT origin_y = 0;
  gint text_width = 0;
  gint last_position = 0;
  gboolean draw_on_viewport;

  GMainLoop *loop = nullptr;
};
/* *INDENT-ON* */

static gboolean
bus_msg (GstBus * bus, GstMessage * msg, OverlayContext * context)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      gst_printerrln ("ERROR %s", err->message);
      if (dbg)
        gst_printerrln ("ERROR debug information: %s", dbg);
      g_clear_error (&err);
      g_free (dbg);

      g_main_loop_quit (context->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      gst_println ("Got EOS");
      g_main_loop_quit (context->loop);
      break;
    default:
      break;
  }

  return TRUE;
}

/* Calculate text size again per resize event */
static void
calculate_size (OverlayContext * context, gint width, gint height)
{
  if (width == context->width && height == context->height)
    return;

  HRESULT hr;
  DWRITE_TEXT_RANGE range;
  range.startPosition = 0;
  range.length = context->text.length ();

  FLOAT text_height = height / 10.0f;
  bool was_decreased = false;

  ComPtr < IDWriteTextLayout > text_layout;
  hr = context->dwrite_factory->CreateTextLayout (context->text.c_str (),
      context->text.length (), context->format.Get (), G_MAXFLOAT, G_MAXFLOAT,
      &text_layout);
  g_assert (SUCCEEDED (hr));

  DWRITE_TEXT_METRICS metrics;
  FLOAT font_size;
  do {
    hr = text_layout->GetMetrics (&metrics);
    g_assert (SUCCEEDED (hr));

    text_layout->GetFontSize (0, &font_size);
    if (metrics.height >= text_height) {
      if (font_size > 1.0f) {
        font_size -= 0.5f;
        was_decreased = true;
        hr = text_layout->SetFontSize (font_size, range);
        g_assert (SUCCEEDED (hr));
        continue;
      }

      break;
    }

    if (was_decreased)
      break;

    if (metrics.height < text_height) {
      if (metrics.height >= text_height * 0.9)
        break;

      font_size += 0.5f;
      hr = text_layout->SetFontSize (font_size, range);
      g_assert (SUCCEEDED (hr));
      continue;
    }
  } while (true);

  text_layout->SetMaxWidth (metrics.widthIncludingTrailingWhitespace + 10);
  text_layout->SetMaxHeight (metrics.height);

  context->layout = nullptr;
  context->layout = text_layout;

  context->origin_y = height - text_height;
  context->text_width = metrics.widthIncludingTrailingWhitespace + 10;
  context->width = width;
  context->height = height;
  context->last_position = 0;
}

static void
on_overlay_2d (GstElement * sink, ID3D12CommandQueue * queue,
    ID3D12Resource * resource12, ID3D11On12Device * device11on12,
    ID3D11Texture2D * resource11, ID2D1DeviceContext2 * context2d,
    D3D12_RECT * viewport, OverlayContext * context)
{
  ComPtr < ID2D1SolidColorBrush > text_brush;
  ComPtr < ID2D1SolidColorBrush > bg_brush;
  ComPtr < ID3D11Device > device11;
  ComPtr < ID3D11DeviceContext > context11;
  HRESULT hr;
  FLOAT position;
  UINT width, height;

  if (context->draw_on_viewport) {
    width = viewport->right - viewport->left;
    height = viewport->bottom - viewport->top;
  } else {
    D3D11_TEXTURE2D_DESC desc;
    resource11->GetDesc (&desc);
    width = desc.Width;
    height = desc.Height;
  }

  calculate_size (context, width, height);

  /* We need d3d11 immediate context to call Flush() */
  hr = device11on12->QueryInterface (IID_PPV_ARGS (&device11));
  g_assert (SUCCEEDED (hr));
  device11->GetImmediateContext (&context11);

  /* Acquire wrapped resource to build GPU command by using d2d/d3d11 for
   * the wrapped d3d12 resrouce */
  ID3D11Resource *resources[] = { resource11 };
  device11on12->AcquireWrappedResources (resources, 1);

  context2d->BeginDraw ();
  hr = context2d->CreateSolidColorBrush (D2D1::ColorF (D2D1::ColorF::Black),
      &bg_brush);
  g_assert (SUCCEEDED (hr));

  hr = context2d->CreateSolidColorBrush (D2D1::ColorF (D2D1::ColorF::White),
      &text_brush);
  g_assert (SUCCEEDED (hr));

  ComPtr < ID2D1Layer > layer;
  hr = context2d->CreateLayer (&layer);
  g_assert (SUCCEEDED (hr));

  D2D1_RECT_F bg_rect;
  if (context->draw_on_viewport) {
    bg_rect = D2D1::RectF (viewport->left, context->origin_y + viewport->top,
        viewport->left + context->width, viewport->top + context->height);
  } else {
    bg_rect = D2D1::RectF (0, context->origin_y, context->width,
        context->height);
  }

  /* Draw background */
  context2d->FillRectangle (bg_rect, bg_brush.Get ());

  /* Create layer to restrict drawing area to specific rect */
  D2D1_LAYER_PARAMETERS params = { };
  params.contentBounds = bg_rect;
  params.maskAntialiasMode = D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
  params.maskTransform = D2D1::IdentityMatrix ();
  params.opacity = 1;
  context2d->PushLayer (&params, layer.Get ());

  /* Draw text */
  position = -context->last_position;
  do {
    context2d->DrawTextLayout (D2D1::Point2F (position, bg_rect.top),
        context->layout.Get (), text_brush.Get (),
        static_cast < D2D1_DRAW_TEXT_OPTIONS >
        (D2D1_DRAW_TEXT_OPTIONS_CLIP |
            D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT));
    position = position + context->text_width;
  } while (position < context->width);

  context2d->PopLayer ();
  context2d->EndDraw ();

  /* After recording GPU command, should release wrapped resource first,
   * then Flush() must be called for the recorded GPU command to be
   * executed via d3d12 command queue */
  device11on12->ReleaseWrappedResources (resources, 1);
  context11->Flush ();

  /* Update text position for the next rendering */
  context->last_position += 2;
  if (context->last_position >= context->text_width) {
    context->last_position %= context->text_width;
  }
}

gint
main (gint argc, gchar ** argv)
{
  GOptionContext *option_ctx;
  GError *error = nullptr;
  gboolean ret;
  gchar *text = nullptr;
  gint width = 1280;
  gint height = 720;
  gboolean draw_on_viewport = FALSE;
  GOptionEntry options[] = {
    {"text", 0, 0, G_OPTION_ARG_STRING, &text, "Text to render", nullptr},
    {"width", 0, 0, G_OPTION_ARG_INT, &width, "Width of video stream", nullptr},
    {"height", 0, 0, G_OPTION_ARG_INT, &height, "Height of video stream",
        nullptr},
    {"draw-on-viewport", 0, 0, G_OPTION_ARG_NONE, &draw_on_viewport,
        "Draw image only on viewport area", nullptr},
    {nullptr}
  };
  OverlayContext context;
  GstStateChangeReturn sret;
  HRESULT hr;
  std::wstring text_wide;
  std::string pipeline_str;
  DWRITE_TEXT_METRICS metrics;
  FLOAT font_size;
  bool was_decreased = false;
  DWRITE_TEXT_RANGE range;
  FLOAT text_height;
  gchar **win32_argv;
  ComPtr < IDWriteTextFormat > text_format;
  ComPtr < IDWriteTextLayout > text_layout;

  win32_argv = g_win32_get_command_line ();

  option_ctx = g_option_context_new ("d3d12videosink present-on-11 example");
  g_option_context_add_main_entries (option_ctx, options, nullptr);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());
  ret = g_option_context_parse_strv (option_ctx, &win32_argv, &error);
  g_option_context_free (option_ctx);
  g_strfreev (win32_argv);

  if (!ret) {
    gst_printerrln ("option parsing failed: %s", error->message);
    g_clear_error (&error);
    return 1;
  }

  /* Prepare device independent D2D objects */
  hr = DWriteCreateFactory (DWRITE_FACTORY_TYPE_SHARED,
      __uuidof (context.dwrite_factory.Get ()),
      reinterpret_cast < IUnknown ** >(context.dwrite_factory.GetAddressOf ()));
  if (FAILED (hr)) {
    gst_printerrln ("Couldn't create DirectWrite factory");
    return 1;
  }

  hr = context.dwrite_factory->CreateTextFormat (L"Arial", nullptr,
      DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us", &text_format);
  if (FAILED (hr)) {
    gst_printerrln ("Couldn't create IDWriteTextFormat");
    return 1;
  }

  text_format->SetTextAlignment (DWRITE_TEXT_ALIGNMENT_LEADING);
  text_format->SetParagraphAlignment (DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  text_format->SetWordWrapping (DWRITE_WORD_WRAPPING_NO_WRAP);

  if (text && text[0] != '\0') {
    std::wstring_convert < std::codecvt_utf8_utf16 < wchar_t >>converter;
    text_wide = converter.from_bytes (text);
  } else {
    /* *INDENT-OFF* */
    text_wide =
        std::wstring (L"Hello GStreamer! 😊 안녕하세요 GStreamer! 😉 ") +
        std::wstring (L"नमस्ते GStreamer! ❤️ Bonjour GStreamer! 😁 ") +
        std::wstring (L"Hallo GStreamer! 😎 Hola GStreamer! 😍 ") +
        std::wstring (L"こんにちは GStreamer! ✌️ 你好 GStreamer! 👍");
    /* *INDENT-ON* */
  }

  text_height = (FLOAT) height / 10.0f;

  hr = context.dwrite_factory->CreateTextLayout (text_wide.c_str (),
      text_wide.length (), text_format.Get (), G_MAXFLOAT, G_MAXFLOAT,
      &text_layout);
  if (FAILED (hr)) {
    gst_printerrln ("Couldn't create IDWriteTextLayout");
    return 1;
  }

  /* Calculate best font size */
  range.startPosition = 0;
  range.length = text_wide.length ();

  do {
    hr = text_layout->GetMetrics (&metrics);
    g_assert (SUCCEEDED (hr));

    text_layout->GetFontSize (0, &font_size);
    if (metrics.height >= (FLOAT) text_height) {
      if (font_size > 1.0f) {
        font_size -= 0.5f;
        was_decreased = true;
        hr = text_layout->SetFontSize (font_size, range);
        g_assert (SUCCEEDED (hr));
        continue;
      }

      break;
    }

    if (was_decreased)
      break;

    if (metrics.height < text_height) {
      if (metrics.height >= text_height * 0.9)
        break;

      font_size += 0.5f;
      hr = text_layout->SetFontSize (font_size, range);
      g_assert (SUCCEEDED (hr));
      continue;
    }
  } while (true);

  gst_println ("Calculated font size %lf", font_size);

  /* 10 pixels padding per loop */
  text_layout->SetMaxWidth (metrics.widthIncludingTrailingWhitespace + 10);
  text_layout->SetMaxHeight (metrics.height);

  context.text = text_wide;
  context.origin_y = (FLOAT) height - text_height;
  context.width = width;
  context.height = height;
  context.text_width = metrics.widthIncludingTrailingWhitespace + 10;
  context.layout = text_layout;
  context.format = text_format;
  context.draw_on_viewport = draw_on_viewport;

  context.loop = g_main_loop_new (nullptr, FALSE);

  pipeline_str =
      "d3d12testsrc ! video/x-raw(memory:D3D12Memory),format=RGBA,width=" +
      std::to_string (width) + ",height=" + std::to_string (height) +
      ",framerate=60/1 ! queue ! d3d12videosink name=sink overlay-mode=d2d "
      "display-format=r8g8b8a8-unorm";

  context.pipeline = gst_parse_launch (pipeline_str.c_str (), nullptr);
  g_assert (context.pipeline);

  auto sink = gst_bin_get_by_name (GST_BIN (context.pipeline), "sink");
  g_assert (sink);

  g_signal_connect (sink, "overlay", G_CALLBACK (on_overlay_2d), &context);
  gst_object_unref (sink);

  gst_bus_add_watch (GST_ELEMENT_BUS (context.pipeline),
      (GstBusFunc) bus_msg, &context);

  sret = gst_element_set_state (context.pipeline, GST_STATE_PLAYING);
  g_assert (sret != GST_STATE_CHANGE_FAILURE);

  g_main_loop_run (context.loop);

  gst_element_set_state (context.pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (GST_ELEMENT_BUS (context.pipeline));
  gst_object_unref (context.pipeline);

  g_main_loop_unref (context.loop);
  g_free (text);

  return 0;
}
