/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * gst-plugins-bad
 * Copyright (C) Carl-Anton Ingmarsson 2010 <ca.ingmarsson@gmail.com>
 * 
 * gst-plugins-bad is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * gst-plugins-bad is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gstvdpdevice.h"
#include "gstvdpvideobuffer.h"

#include "gstvdpvideobufferpool.h"


struct _GstVdpVideoBufferPool
{
  GstVdpBufferPool buffer_pool;

  VdpChromaType chroma_type;
  guint width, height;
};

G_DEFINE_TYPE (GstVdpVideoBufferPool, gst_vdp_video_buffer_pool,
    GST_TYPE_VDP_BUFFER_POOL);

GstVdpBufferPool *
gst_vdp_video_buffer_pool_new (GstVdpDevice * device)
{
  g_return_val_if_fail (GST_IS_VDP_DEVICE (device), NULL);

  return g_object_new (GST_TYPE_VDP_VIDEO_BUFFER_POOL, "device", device, NULL);
}

static gboolean
parse_caps (const GstCaps * caps, VdpChromaType * chroma_type, gint * width,
    gint * height)
{
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "chroma-type", (gint *) chroma_type))
    return FALSE;
  if (!gst_structure_get_int (structure, "width", width))
    return FALSE;
  if (!gst_structure_get_int (structure, "height", height))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vdp_video_buffer_pool_check_caps (GstVdpBufferPool * bpool,
    const GstCaps * caps)
{
  GstVdpVideoBufferPool *vpool = GST_VDP_VIDEO_BUFFER_POOL (bpool);

  VdpChromaType chroma_type;
  gint width, height;

  if (!parse_caps (caps, &chroma_type, &width, &height))
    return FALSE;

  if (chroma_type != vpool->chroma_type || width != vpool->width ||
      height != vpool->height)
    return FALSE;

  return TRUE;
}

static gboolean
gst_vdp_video_buffer_pool_set_caps (GstVdpBufferPool * bpool,
    const GstCaps * caps, gboolean * clear_bufs)
{
  GstVdpVideoBufferPool *vpool = GST_VDP_VIDEO_BUFFER_POOL (bpool);

  VdpChromaType chroma_type;
  gint width, height;

  if (!parse_caps (caps, &chroma_type, &width, &height))
    return FALSE;

  if (chroma_type != vpool->chroma_type || width != vpool->width ||
      height != vpool->height)
    *clear_bufs = TRUE;
  else
    *clear_bufs = FALSE;

  vpool->chroma_type = chroma_type;
  vpool->width = width;
  vpool->height = height;

  return TRUE;
}

static GstVdpBuffer *
gst_vdp_video_buffer_pool_alloc_buffer (GstVdpBufferPool * bpool,
    GError ** error)
{
  GstVdpVideoBufferPool *vpool = GST_VDP_VIDEO_BUFFER_POOL (bpool);
  GstVdpDevice *device;

  device = gst_vdp_buffer_pool_get_device (bpool);
  return GST_VDP_BUFFER_CAST (gst_vdp_video_buffer_new (device,
          vpool->chroma_type, vpool->width, vpool->height, error));
}

static void
gst_vdp_video_buffer_pool_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (gst_vdp_video_buffer_pool_parent_class)->finalize (object);
}

static void
gst_vdp_video_buffer_pool_init (GstVdpVideoBufferPool * vpool)
{
  vpool->chroma_type = -1;
  vpool->width = 0;
  vpool->height = 0;
}

static void
gst_vdp_video_buffer_pool_class_init (GstVdpVideoBufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstVdpBufferPoolClass *buffer_pool_class = GST_VDP_BUFFER_POOL_CLASS (klass);

  buffer_pool_class->alloc_buffer = gst_vdp_video_buffer_pool_alloc_buffer;
  buffer_pool_class->set_caps = gst_vdp_video_buffer_pool_set_caps;
  buffer_pool_class->check_caps = gst_vdp_video_buffer_pool_check_caps;

  object_class->finalize = gst_vdp_video_buffer_pool_finalize;
}
