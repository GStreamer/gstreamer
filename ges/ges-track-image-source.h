/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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

#ifndef _GES_TRACK_IMAGE_SOURCE
#define _GES_TRACK_IMAGE_SOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_IMAGE_SOURCE ges_track_image_source_get_type()

#define GES_TRACK_IMAGE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_IMAGE_SOURCE, GESTrackImageSource))

#define GES_TRACK_IMAGE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_IMAGE_SOURCE, GESTrackImageSourceClass))

#define GES_IS_TRACK_IMAGE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_IMAGE_SOURCE))

#define GES_IS_TRACK_IMAGE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_IMAGE_SOURCE))

#define GES_TRACK_IMAGE_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_IMAGE_SOURCE, GESTrackImageSourceClass))
/** 
 * GESTrackImageSource:
 * @uri: #gchar *, the URI of the media file to play
 *
 */
struct _GESTrackImageSource {
  GESTrackSource parent;

  /*< public >*/
  gchar *uri;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTrackImageSourceClass:
 * @parent_class: parent class
 */

struct _GESTrackImageSourceClass {
  GESTrackSourceClass parent_class;

  /* <public> */

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_track_image_source_get_type (void);

GESTrackImageSource* ges_track_image_source_new (gchar *uri);

G_END_DECLS

#endif /* _GES_TRACK_IMAGE_SOURCE */

