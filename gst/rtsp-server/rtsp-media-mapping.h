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

#include "rtsp-media-factory.h"

#ifndef __GST_RTSP_MEDIA_MAPPING_H__
#define __GST_RTSP_MEDIA_MAPPING_H__

G_BEGIN_DECLS

#define GST_TYPE_RTSP_MEDIA_MAPPING              (gst_rtsp_media_mapping_get_type ())
#define GST_IS_RTSP_MEDIA_MAPPING(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_RTSP_MEDIA_MAPPING))
#define GST_IS_RTSP_MEDIA_MAPPING_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_RTSP_MEDIA_MAPPING))
#define GST_RTSP_MEDIA_MAPPING_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_RTSP_MEDIA_MAPPING, GstRTSPMediaMappingClass))
#define GST_RTSP_MEDIA_MAPPING(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_RTSP_MEDIA_MAPPING, GstRTSPMediaMapping))
#define GST_RTSP_MEDIA_MAPPING_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_RTSP_MEDIA_MAPPING, GstRTSPMediaMappingClass))
#define GST_RTSP_MEDIA_MAPPING_CAST(obj)         ((GstRTSPMediaMapping*)(obj))
#define GST_RTSP_MEDIA_MAPPING_CLASS_CAST(klass) ((GstRTSPMediaMappingClass*)(klass))

typedef struct _GstRTSPMediaMapping GstRTSPMediaMapping;
typedef struct _GstRTSPMediaMappingClass GstRTSPMediaMappingClass;

/**
 * GstRTSPMediaMapping:
 * @mappings: the mountpoint to media mappings
 *
 * Creates a #GstRTSPMediaFactory object for a given url.
 */
struct _GstRTSPMediaMapping {
  GObject       parent;

  GHashTable   *mappings;
};

/**
 * GstRTSPMediaMappingClass:
 * @find_media: Create or return a previously cached #GstRTSPMediaFactory object
 *        for the given url. the default implementation will use the mappings
 *        added with gst_rtsp_media_mapping_add_factory ().
 *
 * The class for the media mapping object.
 */
struct _GstRTSPMediaMappingClass {
  GObjectClass  parent_class;

  GstRTSPMediaFactory * (*find_media)  (GstRTSPMediaMapping *mapping, const GstRTSPUrl *url);
};

GType                 gst_rtsp_media_mapping_get_type     (void);

/* creating a mapping */
GstRTSPMediaMapping * gst_rtsp_media_mapping_new          (void);

/* finding a media factory */
GstRTSPMediaFactory * gst_rtsp_media_mapping_find_factory   (GstRTSPMediaMapping *mapping, const GstRTSPUrl *url);

/* managing media to a path */
void                  gst_rtsp_media_mapping_add_factory    (GstRTSPMediaMapping *mapping, const gchar *path,
                                                             GstRTSPMediaFactory *factory);
void                  gst_rtsp_media_mapping_remove_factory (GstRTSPMediaMapping *mapping, const gchar *path);

G_END_DECLS

#endif /* __GST_RTSP_MEDIA_MAPPING_H__ */
