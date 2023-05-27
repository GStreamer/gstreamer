/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstdwritetextoverlay.h"

GST_DEBUG_CATEGORY_STATIC (dwrite_text_overlay_debug);
#define GST_CAT_DEFAULT dwrite_text_overlay_debug

struct _GstDWriteTextOverlay
{
  GstDWriteBaseOverlay parent;
};

static WString gst_dwrite_text_overlay_get_text (GstDWriteBaseOverlay * overlay,
    const WString & default_text, GstBuffer * buffer);

#define gst_dwrite_text_overlay_parent_class parent_class
G_DEFINE_TYPE (GstDWriteTextOverlay, gst_dwrite_text_overlay,
    GST_TYPE_DWRITE_BASE_OVERLAY);

static void
gst_dwrite_text_overlay_class_init (GstDWriteTextOverlayClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstDWriteBaseOverlayClass *overlay_class =
      GST_DWRITE_BASE_OVERLAY_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "DirectWrite Text Overlay", "Filter/Editor/Video",
      "Adds text strings on top of a video buffer",
      "Seungha Yang <seungha@centricular.com>");

  overlay_class->get_text =
      GST_DEBUG_FUNCPTR (gst_dwrite_text_overlay_get_text);

  GST_DEBUG_CATEGORY_INIT (dwrite_text_overlay_debug,
      "dwritetextoverlay", 0, "dwritetextoverlay");
}

static void
gst_dwrite_text_overlay_init (GstDWriteTextOverlay * self)
{
  g_object_set (self, "text-alignment", DWRITE_TEXT_ALIGNMENT_CENTER,
      "paragraph-alignment", DWRITE_PARAGRAPH_ALIGNMENT_FAR, nullptr);
}

static WString
gst_dwrite_text_overlay_get_text (GstDWriteBaseOverlay * overlay,
    const WString & default_text, GstBuffer * buffer)
{
  return default_text;
}
