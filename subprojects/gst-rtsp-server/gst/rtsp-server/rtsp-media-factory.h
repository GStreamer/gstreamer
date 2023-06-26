/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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
#include <gst/rtsp/gstrtspurl.h>

#include "rtsp-media.h"
#include "rtsp-permissions.h"
#include "rtsp-address-pool.h"

#ifndef __GST_RTSP_MEDIA_FACTORY_H__
#define __GST_RTSP_MEDIA_FACTORY_H__

G_BEGIN_DECLS

/* types for the media factory */
#define GST_TYPE_RTSP_MEDIA_FACTORY              (gst_rtsp_media_factory_get_type ())
#define GST_IS_RTSP_MEDIA_FACTORY(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_MEDIA_FACTORY))
#define GST_IS_RTSP_MEDIA_FACTORY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_MEDIA_FACTORY))
#define GST_RTSP_MEDIA_FACTORY_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_MEDIA_FACTORY, GstRTSPMediaFactoryClass))
#define GST_RTSP_MEDIA_FACTORY(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_MEDIA_FACTORY, GstRTSPMediaFactory))
#define GST_RTSP_MEDIA_FACTORY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_MEDIA_FACTORY, GstRTSPMediaFactoryClass))
#define GST_RTSP_MEDIA_FACTORY_CAST(obj)         ((GstRTSPMediaFactory*)(obj))
#define GST_RTSP_MEDIA_FACTORY_CLASS_CAST(klass) ((GstRTSPMediaFactoryClass*)(klass))

typedef struct _GstRTSPMediaFactory GstRTSPMediaFactory;
typedef struct _GstRTSPMediaFactoryClass GstRTSPMediaFactoryClass;
typedef struct _GstRTSPMediaFactoryPrivate GstRTSPMediaFactoryPrivate;

/**
 * GstRTSPMediaFactory:
 *
 * The definition and logic for constructing the pipeline for a media. The media
 * can contain multiple streams like audio and video.
 */
struct _GstRTSPMediaFactory {
  GObject            parent;

  /*< private >*/
  GstRTSPMediaFactoryPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstRTSPMediaFactoryClass:
 * @gen_key: convert @url to a key for caching shared #GstRTSPMedia objects.
 *       The default implementation of this function will use the complete URL
 *       including the query parameters to return a key.
 * @create_element: Construct and return a #GstElement that is a #GstBin containing
 *       the elements to use for streaming the media. The bin should contain
 *       payloaders pay\%d for each stream. The default implementation of this
 *       function returns the bin created from the launch parameter.
 * @construct: the vmethod that will be called when the factory has to create the
 *       #GstRTSPMedia for @url. The default implementation of this
 *       function calls create_element to retrieve an element and then looks for
 *       pay\%d to create the streams.
 * @create_pipeline: create a new pipeline or re-use an existing one and
 *       add the #GstRTSPMedia's element created by @construct to the pipeline.
 * @configure: configure the media created with @construct. The default
 *       implementation will configure the 'shared' property of the media.
 * @media_constructed: signal emitted when a media was constructed
 * @media_configure: signal emitted when a media should be configured
 *
 * The #GstRTSPMediaFactory class structure.
 */
struct _GstRTSPMediaFactoryClass {
  GObjectClass  parent_class;

  gchar *         (*gen_key)            (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);

  GstElement *    (*create_element)     (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);
  GstRTSPMedia *  (*construct)          (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);
  GstElement *    (*create_pipeline)    (GstRTSPMediaFactory *factory, GstRTSPMedia *media);
  void            (*configure)          (GstRTSPMediaFactory *factory, GstRTSPMedia *media);

  /* signals */
  void            (*media_constructed)  (GstRTSPMediaFactory *factory, GstRTSPMedia *media);
  void            (*media_configure)    (GstRTSPMediaFactory *factory, GstRTSPMedia *media);

  /*< private >*/
  gpointer         _gst_reserved[GST_PADDING_LARGE];
};

GST_RTSP_SERVER_API
GType                 gst_rtsp_media_factory_get_type     (void);

/* creating the factory */

GST_RTSP_SERVER_API
GstRTSPMediaFactory * gst_rtsp_media_factory_new          (void);

/* configuring the factory */

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_launch       (GstRTSPMediaFactory *factory,
                                                               const gchar *launch);

GST_RTSP_SERVER_API
gchar *               gst_rtsp_media_factory_get_launch       (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_permissions  (GstRTSPMediaFactory *factory,
                                                               GstRTSPPermissions *permissions);

GST_RTSP_SERVER_API
GstRTSPPermissions *  gst_rtsp_media_factory_get_permissions  (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_add_role         (GstRTSPMediaFactory *factory,
                                                               const gchar *role,
                                                               const gchar *fieldname, ...);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_add_role_from_structure (GstRTSPMediaFactory * factory,
                                                               GstStructure *structure);
GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_shared       (GstRTSPMediaFactory *factory,
                                                               gboolean shared);

GST_RTSP_SERVER_API
gboolean              gst_rtsp_media_factory_is_shared        (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_stop_on_disconnect       (GstRTSPMediaFactory *factory,
                                                                           gboolean stop_on_disconnect);

GST_RTSP_SERVER_API
gboolean              gst_rtsp_media_factory_is_stop_on_disonnect        (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_suspend_mode (GstRTSPMediaFactory *factory,
                                                               GstRTSPSuspendMode mode);

GST_RTSP_SERVER_API
GstRTSPSuspendMode    gst_rtsp_media_factory_get_suspend_mode (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_eos_shutdown (GstRTSPMediaFactory *factory,
                                                               gboolean eos_shutdown);

GST_RTSP_SERVER_API
gboolean              gst_rtsp_media_factory_is_eos_shutdown  (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_profiles     (GstRTSPMediaFactory *factory,
                                                               GstRTSPProfile profiles);

GST_RTSP_SERVER_API
GstRTSPProfile        gst_rtsp_media_factory_get_profiles     (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_protocols    (GstRTSPMediaFactory *factory,
                                                               GstRTSPLowerTrans protocols);

GST_RTSP_SERVER_API
GstRTSPLowerTrans     gst_rtsp_media_factory_get_protocols    (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_address_pool (GstRTSPMediaFactory * factory,
                                                               GstRTSPAddressPool * pool);

GST_RTSP_SERVER_API
GstRTSPAddressPool *  gst_rtsp_media_factory_get_address_pool (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_multicast_iface (GstRTSPMediaFactory *factory, const gchar *multicast_iface);

GST_RTSP_SERVER_API
gchar *               gst_rtsp_media_factory_get_multicast_iface (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_buffer_size  (GstRTSPMediaFactory * factory,
                                                               guint size);

GST_RTSP_SERVER_API
guint                 gst_rtsp_media_factory_get_buffer_size  (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_ensure_keyunit_on_start  (GstRTSPMediaFactory * factory,
                                                                           gboolean ensure_keyunit_on_start);

GST_RTSP_SERVER_API
gboolean              gst_rtsp_media_factory_get_ensure_keyunit_on_start  (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_ensure_keyunit_on_start_timeout  (GstRTSPMediaFactory * factory,
                                                                                   guint timeout);

GST_RTSP_SERVER_API
guint          gst_rtsp_media_factory_get_ensure_keyunit_on_start_timeout  (GstRTSPMediaFactory * factory);


GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_retransmission_time (GstRTSPMediaFactory * factory,
                                                                      GstClockTime time);

GST_RTSP_SERVER_API
GstClockTime          gst_rtsp_media_factory_get_retransmission_time (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_do_retransmission (GstRTSPMediaFactory * factory,
                                                                    gboolean do_retransmission);

GST_RTSP_SERVER_API
gboolean              gst_rtsp_media_factory_get_do_retransmission (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_latency      (GstRTSPMediaFactory * factory,
                                                               guint                 latency);

GST_RTSP_SERVER_API
guint                 gst_rtsp_media_factory_get_latency      (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_transport_mode (GstRTSPMediaFactory *factory,
                                                                 GstRTSPTransportMode mode);

GST_RTSP_SERVER_API
GstRTSPTransportMode  gst_rtsp_media_factory_get_transport_mode (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_media_gtype  (GstRTSPMediaFactory * factory,
                                                               GType media_gtype);

GST_RTSP_SERVER_API
GType                 gst_rtsp_media_factory_get_media_gtype  (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_clock        (GstRTSPMediaFactory *factory,
                                                               GstClock * clock);

GST_RTSP_SERVER_API
GstClock *            gst_rtsp_media_factory_get_clock        (GstRTSPMediaFactory *factory);

GST_RTSP_SERVER_API
void                    gst_rtsp_media_factory_set_publish_clock_mode (GstRTSPMediaFactory * factory, GstRTSPPublishClockMode mode);

GST_RTSP_SERVER_API
GstRTSPPublishClockMode gst_rtsp_media_factory_get_publish_clock_mode (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
gboolean                gst_rtsp_media_factory_set_max_mcast_ttl (GstRTSPMediaFactory * factory,
                                                                  guint                 ttl);

GST_RTSP_SERVER_API
guint                 gst_rtsp_media_factory_get_max_mcast_ttl (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_bind_mcast_address (GstRTSPMediaFactory * factory,
                                                                     gboolean bind_mcast_addr);
GST_RTSP_SERVER_API
gboolean              gst_rtsp_media_factory_is_bind_mcast_address (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_dscp_qos (GstRTSPMediaFactory * factory,
                                                           gint dscp_qos);
GST_RTSP_SERVER_API
gint                  gst_rtsp_media_factory_get_dscp_qos (GstRTSPMediaFactory * factory);

GST_RTSP_SERVER_API
void                  gst_rtsp_media_factory_set_enable_rtcp (GstRTSPMediaFactory * factory,
                                                              gboolean enable);

GST_RTSP_SERVER_API
gboolean              gst_rtsp_media_factory_is_enable_rtcp (GstRTSPMediaFactory * factory);

/* creating the media from the factory and a url */

GST_RTSP_SERVER_API
GstRTSPMedia *        gst_rtsp_media_factory_construct        (GstRTSPMediaFactory *factory,
                                                               const GstRTSPUrl *url);

GST_RTSP_SERVER_API
GstElement *          gst_rtsp_media_factory_create_element   (GstRTSPMediaFactory *factory,
                                                               const GstRTSPUrl *url);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstRTSPMediaFactory, gst_object_unref)
#endif

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_FACTORY_H__ */
