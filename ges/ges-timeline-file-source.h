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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GES_TIMELINE_FILESOURCE
#define _GES_TIMELINE_FILESOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-timeline-source.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_FILE_SOURCE ges_timeline_filesource_get_type()

#define GES_TIMELINE_FILE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_FILE_SOURCE, GESTimelineFileSource))

#define GES_TIMELINE_FILE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_FILE_SOURCE, GESTimelineFileSourceClass))

#define GES_IS_TIMELINE_FILE_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_FILE_SOURCE))

#define GES_IS_TIMELINE_FILE_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_FILE_SOURCE))

#define GES_TIMELINE_FILE_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_FILE_SOURCE, GESTimelineFileSourceClass))

typedef struct _GESTimelineFileSourcePrivate GESTimelineFileSourcePrivate;

struct _GESTimelineFileSource {
  GESTimelineSource parent;

  /*< private >*/
  GESTimelineFileSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTimelineFileSourceClass:
 */

struct _GESTimelineFileSourceClass {
  /*< private >*/
  GESTimelineSourceClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_timeline_filesource_get_type (void);

void
ges_timeline_filesource_set_mute (GESTimelineFileSource * self, gboolean mute);

void
ges_timeline_filesource_set_is_image (GESTimelineFileSource * self,
    gboolean is_image);

gboolean ges_timeline_filesource_is_muted (GESTimelineFileSource * self);
gboolean ges_timeline_filesource_is_image (GESTimelineFileSource * self);
const gchar *ges_timeline_filesource_get_uri (GESTimelineFileSource * self);

GESTimelineFileSource* ges_timeline_filesource_new (gchar *uri);

G_END_DECLS

#endif /* _GES_TIMELINE_FILESOURCE */

