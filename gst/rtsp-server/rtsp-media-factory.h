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

#define GST_TYPE_RTSP_MEDIA_FACTORY              (gst_rtsp_media_get_type ())
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
 *
 * Creates a #GstRTSPMedia object for a given url.
 */
struct _GstRTSPMediaFactory {
  GObject       parent;
};

struct _GstRTSPMediaFactoryClass {
  GObjectClass  parent_class;

  GstRTSPMedia * (*create_media)  (GstRTSPMediaFactory *factory, const gchar *url);
};

GType                 gst_rtsp_media_factory_get_type     (void);

/* creating a factory */
GstRTSPMediaFactory * gst_rtsp_media_factory_new          (void);

/* creating a media */
GstRTSPMedia *        gst_rtsp_media_factory_create       (GstRTSPMediaFactory *factory, const gchar *url);


/* managing media GTypes to a path */
void                  gst_rtsp_media_factory_add          (GstRTSPMediaFactory *factory, const gchar *path,
                                                           GType type);
void                  gst_rtsp_media_factory_remove       (GstRTSPMediaFactory *factory, const gchar *path,
                                                           GType type);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_FACTORY_H__ */
