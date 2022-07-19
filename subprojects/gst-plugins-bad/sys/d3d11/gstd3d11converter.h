/* GStreamer
 * Copyright (C) <2019> Seungha Yang <seungha.yang@navercorp.com>
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

#ifndef __GST_D3D11_COLOR_CONVERTER_H__
#define __GST_D3D11_COLOR_CONVERTER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/d3d11/gstd3d11.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D11_CONVERTER             (gst_d3d11_converter_get_type())
#define GST_D3D11_CONVERTER(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_D3D11_CONVERTER,GstD3D11Converter))
#define GST_D3D11_CONVERTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_D3D11_CONVERTER,GstD3D11ConverterClass))
#define GST_D3D11_CONVERTER_GET_CLASS(obj)   (GST_D3D11_CONVERTER_CLASS(G_OBJECT_GET_CLASS(obj)))
#define GST_IS_D3D11_CONVERTER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_D3D11_CONVERTER))
#define GST_IS_D3D11_CONVERTER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_D3D11_CONVERTER))
#define GST_D3D11_CONVERTER_CAST(obj)        ((GstD3D11Converter*)(obj))

typedef struct _GstD3D11Converter GstD3D11Converter;
typedef struct _GstD3D11ConverterClass GstD3D11ConverterClass;
typedef struct _GstD3D11ConverterPrivate GstD3D11ConverterPrivate;

typedef enum
{
  GST_D3D11_CONVERTER_BACKEND_SHADER = (1 << 0),
  GST_D3D11_CONVERTER_BACKEND_VIDEO_PROCESSOR = (1 << 1),
} GstD3D11ConverterBackend;
#define GST_TYPE_D3D11_CONVERTER_BACKEND (gst_d3d11_converter_backend_get_type())

#define GST_D3D11_CONVERTER_OPT_BACKEND "GstD3D11Converter.backend"

struct _GstD3D11Converter
{
  GstObject parent;

  GstD3D11Device *device;

  /*< private >*/
  GstD3D11ConverterPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstD3D11ConverterClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType               gst_d3d11_converter_backend_get_type (void);

GType               gst_d3d11_converter_get_type (void);

GstD3D11Converter * gst_d3d11_converter_new  (GstD3D11Device * device,
                                              const GstVideoInfo * in_info,
                                              const GstVideoInfo * out_info,
                                              GstStructure * config);

gboolean            gst_d3d11_converter_convert_buffer (GstD3D11Converter * converter,
                                                        GstBuffer * in_buf,
                                                        GstBuffer * out_buf);

gboolean            gst_d3d11_converter_convert_buffer_unlocked (GstD3D11Converter * converter,
                                                                 GstBuffer * in_buf,
                                                                 GstBuffer * out_buf);

G_END_DECLS

#endif /* __GST_D3D11_COLOR_CONVERTER_H__ */
