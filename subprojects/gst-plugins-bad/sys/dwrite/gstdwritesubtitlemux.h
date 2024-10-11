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

#pragma once

#include <gst/gst.h>
#include <gst/base/base.h>

G_BEGIN_DECLS

#define GST_TYPE_DWRITE_SUBTITLE_MUX_PAD (gst_dwrite_subtitle_mux_pad_get_type())
G_DECLARE_FINAL_TYPE (GstDWriteSubtitleMuxPad, gst_dwrite_subtitle_mux_pad,
    GST, DWRITE_SUBTITLE_MUX_PAD, GstAggregatorPad)

#define GST_TYPE_DWRITE_SUBTITLE_MUX (gst_dwrite_subtitle_mux_get_type())
G_DECLARE_FINAL_TYPE (GstDWriteSubtitleMux, gst_dwrite_subtitle_mux,
    GST, DWRITE_SUBTITLE_MUX, GstAggregator)

G_END_DECLS
