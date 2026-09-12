/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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

#include <gst/d3d12/gstd3d12.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_DECODER_SESSION_POOL (gst_d3d12_decoder_session_pool_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12DecoderSessionPool,
    gst_d3d12_decoder_session_pool, GST, D3D12_DECODER_SESSION_POOL, GstObject);

typedef struct _GstD3D12DecoderSession GstD3D12DecoderSession;
typedef gboolean (*GstD3D12DecoderSessionMatchFunc) (gpointer data,
    gpointer user_data);

GstD3D12DecoderSessionPool * gst_d3d12_decoder_session_pool_new (GstD3D12Device * device);

GstD3D12Device * gst_d3d12_decoder_session_pool_get_device (GstD3D12DecoderSessionPool * pool);

void gst_d3d12_decoder_session_pool_flush (GstD3D12DecoderSessionPool * pool);

gboolean gst_d3d12_decoder_session_pool_acquire (GstD3D12DecoderSessionPool * pool,
                                                 GstD3D12DecoderSessionMatchFunc match,
                                                 gpointer user_data,
                                                 GstD3D12DecoderSession ** session);

GType gst_d3d12_decoder_session_get_type(void);

GstD3D12DecoderSession * gst_d3d12_decoder_session_new (void);

GstD3D12DecoderSession * gst_d3d12_decoder_session_ref (GstD3D12DecoderSession * session);

void gst_d3d12_decoder_session_unref (GstD3D12DecoderSession * session);

gpointer gst_d3d12_decoder_session_get_data (GstD3D12DecoderSession * session);

void gst_d3d12_decoder_session_set_data (GstD3D12DecoderSession * session,
                                         gpointer data,
                                         GDestroyNotify notify);

G_END_DECLS
