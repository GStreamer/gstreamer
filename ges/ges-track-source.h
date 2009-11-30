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

#ifndef _GES_TRACK_SOURCE
#define _GES_TRACK_SOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-object.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_SOURCE ges_track_source_get_type()

#define GES_TRACK_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_SOURCE, GESTrackSource))

#define GES_TRACK_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_SOURCE, GESTrackSourceClass))

#define GES_IS_TRACK_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_SOURCE))

#define GES_IS_TRACK_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_SOURCE))

#define GES_TRACK_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_SOURCE, GESTrackSourceClass))

struct _GESTrackSource {
  GESTrackObject parent;
};

struct _GESTrackSourceClass {
  GESTrackObjectClass parent_class;
};

GType ges_track_source_get_type (void);

GESTrackSource* ges_track_source_new (void);

G_END_DECLS

#endif /* _GES_TRACK_SOURCE */

