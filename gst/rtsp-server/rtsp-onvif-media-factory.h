/* GStreamer
 * Copyright (C) 2017 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_RTSP_ONVIF_MEDIA_FACTORY_H__
#define __GST_RTSP_ONVIF_MEDIA_FACTORY_H__

#include <gst/gst.h>
#include "rtsp-media-factory.h"

#define GST_TYPE_RTSP_ONVIF_MEDIA_FACTORY              (gst_rtsp_onvif_media_factory_get_type ())
#define GST_IS_RTSP_ONVIF_MEDIA_FACTORY(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_ONVIF_MEDIA_FACTORY))
#define GST_IS_RTSP_ONVIF_MEDIA_FACTORY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_ONVIF_MEDIA_FACTORY))
#define GST_RTSP_ONVIF_MEDIA_FACTORY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_ONVIF_MEDIA_FACTORY, GstRTSPOnvifMediaFactoryClass))
#define GST_RTSP_ONVIF_MEDIA_FACTORY(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_ONVIF_MEDIA_FACTORY, GstRTSPOnvifMediaFactory))
#define GST_RTSP_ONVIF_MEDIA_FACTORY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_ONVIF_MEDIA_FACTORY, GstRTSPOnvifMediaFactoryClass))
#define GST_RTSP_ONVIF_MEDIA_FACTORY_CAST(obj)         ((GstRTSPOnvifMediaFactory*)(obj))
#define GST_RTSP_ONVIF_MEDIA_FACTORY_CLASS_CAST(klass) ((GstRTSPOnvifMediaFactoryClass*)(klass))

typedef struct GstRTSPOnvifMediaFactoryClass GstRTSPOnvifMediaFactoryClass;
typedef struct GstRTSPOnvifMediaFactory GstRTSPOnvifMediaFactory;
typedef struct GstRTSPOnvifMediaFactoryPrivate GstRTSPOnvifMediaFactoryPrivate;

/**
 * GstRTSPOnvifMediaFactory:
 *
 * Since: 1.14
 */
struct GstRTSPOnvifMediaFactoryClass
{
  GstRTSPMediaFactoryClass parent;
  gboolean (*has_backchannel_support) (GstRTSPOnvifMediaFactory * factory);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING_LARGE];
};

struct GstRTSPOnvifMediaFactory
{
  GstRTSPMediaFactory parent;
  GstRTSPOnvifMediaFactoryPrivate *priv;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_RTSP_SERVER_API
GType gst_rtsp_onvif_media_factory_get_type (void);

GST_RTSP_SERVER_API
GstRTSPMediaFactory *gst_rtsp_onvif_media_factory_new (void);

GST_RTSP_SERVER_API
void gst_rtsp_onvif_media_factory_set_backchannel_launch (GstRTSPOnvifMediaFactory *
    factory, const gchar * launch);
GST_RTSP_SERVER_API
gchar * gst_rtsp_onvif_media_factory_get_backchannel_launch (GstRTSPOnvifMediaFactory * factory);

GST_RTSP_SERVER_API
gboolean gst_rtsp_onvif_media_factory_has_backchannel_support (GstRTSPOnvifMediaFactory * factory);

GST_RTSP_SERVER_API
gboolean gst_rtsp_onvif_media_factory_has_replay_support (GstRTSPOnvifMediaFactory * factory);

GST_RTSP_SERVER_API
void gst_rtsp_onvif_media_factory_set_replay_support (GstRTSPOnvifMediaFactory * factory, gboolean has_replay_support);

GST_RTSP_SERVER_API
void gst_rtsp_onvif_media_factory_set_backchannel_bandwidth (GstRTSPOnvifMediaFactory * factory, guint bandwidth);
GST_RTSP_SERVER_API
guint gst_rtsp_onvif_media_factory_get_backchannel_bandwidth (GstRTSPOnvifMediaFactory * factory);

GST_RTSP_SERVER_API
gboolean gst_rtsp_onvif_media_factory_requires_backchannel (GstRTSPMediaFactory * factory, GstRTSPContext * ctx);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstRTSPOnvifMediaFactory, gst_object_unref)
#endif

#endif /* __GST_RTSP_ONVIF_MEDIA_FACTORY_H__ */
