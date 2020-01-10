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

#ifndef __GST_D3D11_VIDEO_PROCESSOR_H__
#define __GST_D3D11_VIDEO_PROCESSOR_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstd3d11_fwd.h"

G_BEGIN_DECLS

typedef struct _GstD3D11VideoProcessor GstD3D11VideoProcessor;

GQuark gst_d3d11_video_processor_input_view_quark (void);
GQuark gst_d3d11_video_processor_output_view_quark (void);

GstD3D11VideoProcessor * gst_d3d11_video_processor_new  (GstD3D11Device * device,
                                                         guint in_width,
                                                         guint in_height,
                                                         guint out_width,
                                                         guint out_height);

void      gst_d3d11_video_processor_free (GstD3D11VideoProcessor * processor);

gboolean  gst_d3d11_video_processor_supports_input_format (GstD3D11VideoProcessor * processor,
                                                           DXGI_FORMAT format);

gboolean  gst_d3d11_video_processor_supports_output_format (GstD3D11VideoProcessor * processor,
                                                            DXGI_FORMAT format);

gboolean  gst_d3d11_video_processor_get_caps (GstD3D11VideoProcessor * processor,
                                              D3D11_VIDEO_PROCESSOR_CAPS * caps);

gboolean  gst_d3d11_video_processor_set_input_color_space  (GstD3D11VideoProcessor * processor,
                                                            GstVideoColorimetry * color);

gboolean  gst_d3d11_video_processor_set_output_color_space (GstD3D11VideoProcessor * processor,
                                                            GstVideoColorimetry * color);

#if (DXGI_HEADER_VERSION >= 4)
gboolean  gst_d3d11_video_processor_set_input_dxgi_color_space (GstD3D11VideoProcessor * processor,
                                                                DXGI_COLOR_SPACE_TYPE color_space);

gboolean  gst_d3d11_video_processor_set_output_dxgi_color_space (GstD3D11VideoProcessor * processor,
                                                                DXGI_COLOR_SPACE_TYPE color_space);
#endif

#if (DXGI_HEADER_VERSION >= 5)
gboolean  gst_d3d11_video_processor_set_input_hdr10_metadata (GstD3D11VideoProcessor * processor,
                                                              DXGI_HDR_METADATA_HDR10 * hdr10_meta);

gboolean  gst_d3d11_video_processor_set_output_hdr10_metadata (GstD3D11VideoProcessor * processor,
                                                              DXGI_HDR_METADATA_HDR10 * hdr10_meta);
#endif

gboolean  gst_d3d11_video_processor_create_input_view  (GstD3D11VideoProcessor * processor,
                                                        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC * desc,
                                                        ID3D11Resource *resource,
                                                        ID3D11VideoProcessorInputView ** view);

gboolean  gst_d3d11_video_processor_create_output_view (GstD3D11VideoProcessor * processor,
                                                        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC * desc,
                                                        ID3D11Resource *resource,
                                                        ID3D11VideoProcessorOutputView ** view);

void      gst_d3d11_video_processor_input_view_release  (ID3D11VideoProcessorInputView * view);

void      gst_d3d11_video_processor_output_view_release (ID3D11VideoProcessorOutputView * view);

gboolean  gst_d3d11_video_processor_render             (GstD3D11VideoProcessor * processor,
                                                        RECT *in_rect,
                                                        ID3D11VideoProcessorInputView * in_view,
                                                        RECT *out_rect,
                                                        ID3D11VideoProcessorOutputView * out_view);

gboolean  gst_d3d11_video_processor_render_unlocked    (GstD3D11VideoProcessor * processor,
                                                        RECT *in_rect,
                                                        ID3D11VideoProcessorInputView * in_view,
                                                        RECT *out_rect,
                                                        ID3D11VideoProcessorOutputView * out_view);

/* utils */
gboolean gst_d3d11_video_processor_check_bind_flags_for_input_view (guint bind_flags);

gboolean gst_d3d11_video_processor_check_bind_flags_for_output_view (guint bind_flags);

G_END_DECLS

#endif /* __GST_D3D11_VIDEO_PROCESSOR_H__ */
