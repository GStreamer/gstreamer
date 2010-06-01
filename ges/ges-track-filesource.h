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

#ifndef _GES_TRACK_FILESOURCE
#define _GES_TRACK_FILESOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_FILESOURCE ges_track_filesource_get_type()

#define GES_TRACK_FILESOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_FILESOURCE, GESTrackFileSource))

#define GES_TRACK_FILESOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_FILESOURCE, GESTrackFileSourceClass))

#define GES_IS_TRACK_FILESOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_FILESOURCE))

#define GES_IS_TRACK_FILESOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_FILESOURCE))

#define GES_TRACK_FILESOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_FILESOURCE, GESTrackFileSourceClass))
/** 
 * GESTrackFileSource:
 * @uri: #gchar *, the URI of the media file to play
 *
 */
struct _GESTrackFileSource {
  GESTrackSource parent;

  /*< public >*/
  gchar *uri;
};

/**
 * GESTrackFileSourceClass:
 * @parent_class: parent class
 */

struct _GESTrackFileSourceClass {
  GESTrackSourceClass parent_class;

  /* <public> */
};

GType ges_track_filesource_get_type (void);

GESTrackFileSource* ges_track_filesource_new (gchar *uri);

G_END_DECLS

#endif /* _GES_TRACK_FILESOURCE */

