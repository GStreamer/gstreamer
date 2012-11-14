/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans at gmail.com>
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

#include <gst/gst.h>

#ifndef __GST_RTSP_ADDRESS_POOL_H__
#define __GST_RTSP_ADDRESS_POOL_H__

G_BEGIN_DECLS

#define GST_TYPE_RTSP_ADDRESS_POOL              (gst_rtsp_address_pool_get_type ())
#define GST_IS_RTSP_ADDRESS_POOL(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_ADDRESS_POOL))
#define GST_IS_RTSP_ADDRESS_POOL_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_ADDRESS_POOL))
#define GST_RTSP_ADDRESS_POOL_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_ADDRESS_POOL, GstRTSPAddressPoolClass))
#define GST_RTSP_ADDRESS_POOL(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_ADDRESS_POOL, GstRTSPAddressPool))
#define GST_RTSP_ADDRESS_POOL_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_ADDRESS_POOL, GstRTSPAddressPoolClass))
#define GST_RTSP_ADDRESS_POOL_CAST(obj)         ((GstRTSPAddressPool*)(obj))
#define GST_RTSP_ADDRESS_POOL_CLASS_CAST(klass) ((GstRTSPAddressPoolClass*)(klass))

typedef struct _GstRTSPAddressPool GstRTSPAddressPool;
typedef struct _GstRTSPAddressPoolClass GstRTSPAddressPoolClass;
typedef struct _GstRTSPAddressPoolPrivate GstRTSPAddressPoolPrivate;

typedef enum {
  GST_RTSP_ADDRESS_FLAG_NONE      = 0,
  GST_RTSP_ADDRESS_FLAG_IPV4      = (1 << 0),
  GST_RTSP_ADDRESS_FLAG_IPV6      = (1 << 1),
  GST_RTSP_ADDRESS_FLAG_EVEN_PORT = (1 << 2)
} GstRTSPAddressFlags;

/**
 * GstRTSPAddressPool:
 * @parent: the parent GObject
 *
 */
struct _GstRTSPAddressPool {
  GObject       parent;

  GstRTSPAddressPoolPrivate *priv;
};

struct _GstRTSPAddressPoolClass {
  GObjectClass  parent_class;
};

GType                  gst_rtsp_address_pool_get_type        (void);

/* create a new address pool */
GstRTSPAddressPool *   gst_rtsp_address_pool_new             (void);

void                   gst_rtsp_address_pool_clear           (GstRTSPAddressPool * pool);
void                   gst_rtsp_address_pool_dump            (GstRTSPAddressPool * pool);

gboolean               gst_rtsp_address_pool_add_range       (GstRTSPAddressPool * pool,
                                                              const gchar *min_address,
                                                              const gchar *max_address,
                                                              guint16 min_port,
                                                              guint16 max_port,
                                                              guint8 ttl);

gpointer               gst_rtsp_address_pool_acquire_address (GstRTSPAddressPool * pool,
                                                              GstRTSPAddressFlags flags,
                                                              gint n_ports,
                                                              gchar **address,
                                                              guint16 *port, guint8 *ttl);
void                   gst_rtsp_address_pool_release_address (GstRTSPAddressPool * pool,
                                                              gpointer);
G_END_DECLS

#endif /* __GST_RTSP_ADDRESS_POOL_H__ */
