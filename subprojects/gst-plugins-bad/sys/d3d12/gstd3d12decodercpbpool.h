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

#pragma once

#include <gst/gst.h>
#include <gst/d3d12/gstd3d12.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_DECODER_CPB_POOL (gst_d3d12_decoder_cpb_pool_get_type())
G_DECLARE_FINAL_TYPE (GstD3D12DecoderCpbPool,
    gst_d3d12_decoder_cpb_pool, GST, D3D12_DECODER_CPB_POOL, GstObject);

typedef struct _GstD3D12DecoderCpb GstD3D12DecoderCpb;

GType                    gst_d3d12_decoder_cpb_get_type (void);

GstD3D12DecoderCpbPool * gst_d3d12_decoder_cpb_pool_new (ID3D12Device * device);

HRESULT                  gst_d3d12_decoder_cpb_pool_acquire (GstD3D12DecoderCpbPool * pool,
                                                             gpointer data,
                                                             gsize size,
                                                            GstD3D12DecoderCpb ** cpb);

GstD3D12DecoderCpb *     gst_d3d12_decoder_cpb_ref (GstD3D12DecoderCpb * cpb);

void                     gst_d3d12_decoder_cpb_unref (GstD3D12DecoderCpb * cpb);

gboolean                 gst_d3d12_decoder_cpb_get_bitstream (GstD3D12DecoderCpb * cpb,
                                                              D3D12_VIDEO_DECODE_COMPRESSED_BITSTREAM * bs);

ID3D12CommandAllocator * gst_d3d12_decoder_cpb_get_command_allocator (GstD3D12DecoderCpb * cpb);

G_END_DECLS

