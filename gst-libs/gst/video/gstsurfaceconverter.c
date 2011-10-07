/* GStreamer
 * Copyright (C) 2011 Collabora Ltd.
 * Copyright (C) 2011 Intel
 *
 * Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsurfaceconverter.h"
#include "gstsurfacebuffer.h"

/**
 * SECTION:gstsurfaceconverter
 * @short_description: Interface for #GstSurfaceBuffer convertion
 *
 * Objects implementing this interface are used as a convertion context. This
 * allow element optimizing the upload by keeping required resources between
 * uploads. The context must be discarded when the pipeline goes to
 * #GST_STATE_NULL or renewed whenever the caps are changed.
 * <note>
 *   The GstVideoContext interface is unstable API and may change in future.
 *   One can define GST_USE_UNSTABLE_API to acknowledge and avoid this warning.
 * </note>
 *
 * <refsect2>
 * <title>Example uploading to GL texture</title>
 * |[
 * if (G_UNLIKELY (priv->converter == NULL))
 *   priv->converter = gst_surface_buffer_create_converter (surface, "opengl", &value);
 *
 * gst_surface_converter_uplaod (priv->converter, surface);
 * ]|
 * </refsect2>
 */

G_DEFINE_INTERFACE (GstSurfaceConverter, gst_surface_converter, G_TYPE_INVALID);

static void
gst_surface_converter_default_init (GstSurfaceConverterInterface * iface)
{
  /* default virtual functions */
  iface->upload = NULL;
}

/**
 * gst_surface_converter_upload:
 * @converter: a #GstSurfaceConverter
 * @buffer: the #GstSurfaceBuffer to upload
 *
 * Convert and uploads the #GstSurfaceBuffer to the converter destination.
 *
 * Returns: #TRUE on success, #FALSE otherwise
 */
gboolean
gst_surface_converter_upload (GstSurfaceConverter * converter,
    GstSurfaceBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_SURFACE_CONVERTER (converter), FALSE);
  g_return_val_if_fail (GST_IS_SURFACE_BUFFER (buffer), FALSE);
  return GST_SURFACE_CONVERTER_GET_IFACE (converter)->upload (converter,
      buffer);
}
