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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <gst/gst.h>
#include <gst/rtsp/gstrtspurl.h>

#include "rtsp-media.h"

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

/**
 * GstRTSPMediaFactory:
 * @lock: mutex protecting the datastructure.
 * @launch: the launch description
 * @shared: if media from this factory can be shared between clients
 * @media_lock: mutex protecting the medias.
 * @media: hashtable of shared media
 *
 * The definition and logic for constructing the pipeline for a media. The media
 * can contain multiple streams like audio and video.
 */
struct _GstRTSPMediaFactory {
  GObject       parent;

  GMutex       *lock;
  gchar        *launch;
  gboolean      shared;

  GMutex       *medias_lock;
  GHashTable   *medias;
};

/**
 * GstRTSPMediaFactoryClass:
 * @gen_key: convert @url to a key for caching shared #GstRTSPMedia objects.
 *       The default implementation of this function will use the complete URL
 *       including the query parameters to return a key.
 * @get_element: Construct and return a #GstElement that is a #GstBin containing
 *       the elements to use for streaming the media. The bin should contain
 *       payloaders pay%d for each stream. The default implementation of this
 *       function returns the bin created from the launch parameter.
 * @construct: the vmethod that will be called when the factory has to create the
 *       #GstRTSPMedia for @url. The default implementation of this
 *       function calls get_element to retrieve an element and then looks for
 *       pay%d to create the streams.
 * @configure: configure the media created with @construct. The default
 *       implementation will configure the 'shared' property of the media.
 * @create_pipeline: create a new pipeline or re-use an existing one and
 *       add the #GstRTSPMedia's element created by @construct to the pipeline.
 *
 * The #GstRTSPMediaFactory class structure.
 */
struct _GstRTSPMediaFactoryClass {
  GObjectClass  parent_class;

  gchar *           (*gen_key)        (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);

  GstElement *      (*get_element)    (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);
  GstRTSPMedia *    (*construct)      (GstRTSPMediaFactory *factory, const GstRTSPUrl *url);
  void              (*configure)      (GstRTSPMediaFactory *factory, GstRTSPMedia *media);
  GstElement *      (*create_pipeline)(GstRTSPMediaFactory *factory, GstRTSPMedia *media);
};

GType                 gst_rtsp_media_factory_get_type     (void);

/* creating the factory */
GstRTSPMediaFactory * gst_rtsp_media_factory_new          (void);

/* configuring the factory */
void                  gst_rtsp_media_factory_set_launch   (GstRTSPMediaFactory *factory,
                                                           const gchar *launch);
gchar *               gst_rtsp_media_factory_get_launch   (GstRTSPMediaFactory *factory);

void                  gst_rtsp_media_factory_set_shared   (GstRTSPMediaFactory *factory,
                                                           gboolean shared);
gboolean              gst_rtsp_media_factory_is_shared    (GstRTSPMediaFactory *factory);

/* creating the media from the factory and a url */
GstRTSPMedia *        gst_rtsp_media_factory_construct    (GstRTSPMediaFactory *factory,
                                                           const GstRTSPUrl *url);

void                  gst_rtsp_media_factory_collect_streams (GstRTSPMediaFactory *factory,
                                                              const GstRTSPUrl *url,
                                                              GstRTSPMedia *media);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_FACTORY_H__ */
