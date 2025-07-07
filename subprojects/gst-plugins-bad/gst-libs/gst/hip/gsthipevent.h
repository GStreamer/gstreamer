/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
#include <gst/hip/gsthip_fwd.h>
#include <gst/hip/gsthip-enums.h>

G_BEGIN_DECLS

#define GST_TYPE_HIP_EVENT_POOL                (gst_hip_event_pool_get_type ())
#define GST_HIP_EVENT_POOL(obj)                (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_HIP_EVENT_POOL, GstHipEventPool))
#define GST_HIP_EVENT_POOL_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_HIP_EVENT_POOL, GstHipEventPoolClass))
#define GST_IS_HIP_EVENT_POOL(obj)             (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_HIP_EVENT_POOL))
#define GST_IS_HIP_EVENT_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_HIP_EVENT_POOL))
#define GST_HIP_EVENT_POOL_GET_CLASS(obj)      (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_HIP_EVENT_POOL, GstHipEventPoolClass))
#define GST_HIP_EVENT_POOL_CAST(obj)           ((GstHipEventPool*)(obj))

/**
 * GstHipEventPool:
 *
 * Opaque GstHipEventPool struct
 *
 * Since: 1.28
 */
struct _GstHipEventPool
{
  GstObject parent;

  /*< private >*/
  GstHipEventPoolPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstHipEventPoolClass:
 *
 * Opaque GstHipEventPoolClass struct
 *
 * Since: 1.28
 */
struct _GstHipEventPoolClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_HIP_API
GType gst_hip_event_pool_get_type (void);

GST_HIP_API
GType gst_hip_event_get_type (void);

GST_HIP_API
GstHipEventPool * gst_hip_event_pool_new (GstHipVendor vendor,
                                          guint device_id);

GST_HIP_API
gboolean          gst_hip_event_pool_acquire (GstHipEventPool * pool,
                                              GstHipEvent ** event);

GST_HIP_API
GstHipVendor      gst_hip_event_get_vendor (GstHipEvent * event);

GST_HIP_API
guint             gst_hip_event_get_device_id (GstHipEvent * event);

GST_HIP_API
hipError_t        gst_hip_event_record (GstHipEvent * event,
                                        hipStream_t stream);

GST_HIP_API
hipError_t        gst_hip_event_query (GstHipEvent * event);

GST_HIP_API
hipError_t        gst_hip_event_synchronize (GstHipEvent * event);

GST_HIP_API
GstHipEvent *     gst_hip_event_ref (GstHipEvent * event);

GST_HIP_API
void              gst_hip_event_unref (GstHipEvent * event);

GST_HIP_API
void              gst_clear_hip_event (GstHipEvent ** event);

G_END_DECLS

