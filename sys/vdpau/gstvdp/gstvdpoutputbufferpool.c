/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * gst-plugins-bad
 * Copyright (C) Carl-Anton Ingmarsson 2010 <ca.ingmarsson@gmail.com>
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

#include "gstvdpdevice.h"
#include "gstvdpoutputbuffer.h"

#include "gstvdpoutputbufferpool.h"


struct _GstVdpOutputBufferPool
{
  GstVdpBufferPool buffer_pool;

  VdpRGBAFormat rgba_format;
  guint width, height;
};

G_DEFINE_TYPE (GstVdpOutputBufferPool, gst_vdp_output_buffer_pool,
    GST_TYPE_VDP_BUFFER_POOL);

GstVdpBufferPool *
gst_vdp_output_buffer_pool_new (GstVdpDevice * device)
{
  g_return_val_if_fail (GST_IS_VDP_DEVICE (device), NULL);

  return g_object_new (GST_TYPE_VDP_OUTPUT_BUFFER_POOL, "device", device, NULL);
}

static gboolean
parse_caps (const GstCaps * caps, VdpChromaType * rgba_format, gint * width,
    gint * height)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rgba-format", (gint *) rgba_format))
    return FALSE;
  if (!gst_structure_get_int (structure, "width", width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", height))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vdp_output_buffer_pool_check_caps (GstVdpBufferPool * bpool,
    const GstCaps * caps)
{
  GstVdpOutputBufferPool *opool = GST_VDP_OUTPUT_BUFFER_POOL (bpool);

  VdpChromaType rgba_format;
  gint width, height;

  if (!parse_caps (caps, &rgba_format, &width, &height))
    return FALSE;

  if (rgba_format != opool->rgba_format || width != opool->width ||
      height != opool->height)
    return FALSE;

  return TRUE;
}

static gboolean
gst_vdp_output_buffer_pool_set_caps (GstVdpBufferPool * bpool,
    const GstCaps * caps, gboolean * clear_bufs)
{
  GstVdpOutputBufferPool *opool = GST_VDP_OUTPUT_BUFFER_POOL (bpool);

  VdpChromaType rgba_format;
  gint width, height;

  if (!parse_caps (caps, &rgba_format, &width, &height))
    return FALSE;

  if (rgba_format != opool->rgba_format || width != opool->width ||
      height != opool->height)
    *clear_bufs = TRUE;
  else
    *clear_bufs = FALSE;

  opool->rgba_format = rgba_format;
  opool->width = width;
  opool->height = height;

  return TRUE;
}

static GstVdpBuffer *
gst_vdp_output_buffer_pool_alloc_buffer (GstVdpBufferPool * bpool,
    GError ** error)
{
  GstVdpOutputBufferPool *opool = GST_VDP_OUTPUT_BUFFER_POOL (bpool);
  GstVdpDevice *device;

  device = gst_vdp_buffer_pool_get_device (bpool);
  return GST_VDP_BUFFER_CAST (gst_vdp_output_buffer_new (device,
          opool->rgba_format, opool->width, opool->height, error));
}

static void
gst_vdp_output_buffer_pool_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (gst_vdp_output_buffer_pool_parent_class)->finalize (object);
}

static void
gst_vdp_output_buffer_pool_init (GstVdpOutputBufferPool * opool)
{
  opool->rgba_format = -1;
  opool->width = 0;
  opool->height = 0;
}

static void
gst_vdp_output_buffer_pool_class_init (GstVdpOutputBufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstVdpBufferPoolClass *buffer_pool_class = GST_VDP_BUFFER_POOL_CLASS (klass);

  buffer_pool_class->alloc_buffer = gst_vdp_output_buffer_pool_alloc_buffer;
  buffer_pool_class->set_caps = gst_vdp_output_buffer_pool_set_caps;
  buffer_pool_class->check_caps = gst_vdp_output_buffer_pool_check_caps;

  object_class->finalize = gst_vdp_output_buffer_pool_finalize;
}
