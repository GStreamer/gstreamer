/* GStreamer Editing Services
 *
 * Copyright (C) 2012 Thibault Saunier <thibault.saunier@collabora.com>
 * Copyright (C) 2012 Volodymyr Rudyi <vladimir.rudoy@gmail.com>
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
#ifndef _GES_ASSET_FILESOURCE_
#define _GES_ASSET_FILESOURCE_

#include <glib-object.h>
#include <gio/gio.h>
#include <ges/ges-types.h>
#include <ges/ges-asset.h>
#include <ges/ges-asset-clip.h>
#include <ges/ges-asset-track-object.h>

G_BEGIN_DECLS
#define GES_TYPE_ASSET_FILESOURCE ges_asset_filesource_get_type()
#define GES_ASSET_FILESOURCE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_ASSET_FILESOURCE, GESAssetFileSource))
#define GES_ASSET_FILESOURCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_ASSET_FILESOURCE, GESAssetFileSourceClass))
#define GES_IS_ASSET_FILESOURCE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_ASSET_FILESOURCE))
#define GES_IS_ASSET_FILESOURCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_ASSET_FILESOURCE))
#define GES_ASSET_FILESOURCE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_ASSET_FILESOURCE, GESAssetFileSourceClass))

typedef struct _GESAssetFileSourcePrivate GESAssetFileSourcePrivate;

GType ges_asset_filesource_get_type (void);

struct _GESAssetFileSource
{
  GESAssetClip parent;

  /* <private> */
  GESAssetFileSourcePrivate *priv;

  /* Padding for API extension */
  gpointer __ges_reserved[GES_PADDING];
};

struct _GESAssetFileSourceClass
{
  GESAssetClipClass parent_class;

  /* <private> */
  GstDiscoverer *discoverer;

  gpointer _ges_reserved[GES_PADDING];
};

GstDiscovererInfo *ges_asset_filesource_get_info      (const GESAssetFileSource * self);
GstClockTime ges_asset_filesource_get_duration        (GESAssetFileSource *self);
gboolean ges_asset_filesource_is_image                (GESAssetFileSource *self);
void ges_asset_filesource_new                         (const gchar *uri,
                                                       GCancellable *cancellable,
                                                       GAsyncReadyCallback callback,
                                                       gpointer user_data);
void ges_asset_filesource_set_timeout                 (GESAssetFileSourceClass *class,
                                                       GstClockTime timeout);
const GList * ges_asset_filesource_get_stream_assets  (GESAssetFileSource *self);

#define GES_TYPE_ASSET_TRACK_FILESOURCE ges_asset_track_filesource_get_type()
#define GES_ASSET_TRACK_FILESOURCE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_ASSET_TRACK_FILESOURCE, GESAssetTrackFileSource))
#define GES_ASSET_TRACK_FILESOURCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_ASSET_TRACK_FILESOURCE, GESAssetTrackFileSourceClass))
#define GES_IS_ASSET_TRACK_FILESOURCE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_ASSET_TRACK_FILESOURCE))
#define GES_IS_ASSET_TRACK_FILESOURCE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_ASSET_TRACK_FILESOURCE))
#define GES_ASSET_TRACK_FILESOURCE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_ASSET_TRACK_FILESOURCE, GESAssetTrackFileSourceClass))

typedef struct _GESAssetTrackFileSourcePrivate GESAssetTrackFileSourcePrivate;

GType ges_asset_track_filesource_get_type (void);

struct _GESAssetTrackFileSource
{
  GESAssetTrackObject parent;

  /* <private> */
  GESAssetTrackFileSourcePrivate *priv;

  /* Padding for API extension */
  gpointer __ges_reserved[GES_PADDING];
};

struct _GESAssetTrackFileSourceClass
{
  GESAssetTrackObjectClass parent_class;

  gpointer _ges_reserved[GES_PADDING];
};
GstDiscovererStreamInfo * ges_asset_track_filesource_get_stream_info     (GESAssetTrackFileSource *asset);
const gchar * ges_asset_track_filesource_get_stream_uri                  (GESAssetTrackFileSource *asset);
const GESAssetFileSource *ges_asset_track_filesource_get_filesource_asset (GESAssetTrackFileSource *asset);

G_END_DECLS
#endif /* _GES_ASSET_FILESOURCE */
