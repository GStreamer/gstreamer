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

/**
 * SECTION:ges-custom-timeline-source
 * @short_description: Convenience #GESTimelineSource
 *
 * #GESCustomTimelineSource allows creating #GESTimelineSource(s) without the
 * need to subclass.
 * 
 * Its usage should be limited to testing and prototyping purposes.
 */

#include "ges-internal.h"
#include "ges-custom-timeline-source.h"
#include "ges-timeline-source.h"
#include "ges-track-source.h"

struct _GESCustomTimelineSourcePrivate
{
  GESFillTrackObjectUserFunc filltrackobjectfunc;
  gpointer user_data;
};

G_DEFINE_TYPE (GESCustomTimelineSource, ges_custom_timeline_source,
    GES_TYPE_TIMELINE_SOURCE);

static gboolean
ges_custom_timeline_source_fill_track_object (GESTimelineObject * object,
    GESTrackObject * trobject, GstElement * gnlobj);

static GESTrackObject *
ges_custom_timeline_source_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  return g_object_new (GES_TYPE_TRACK_SOURCE, NULL);
}

static void
ges_custom_timeline_source_class_init (GESCustomTimelineSourceClass * klass)
{
  GESTimelineObjectClass *tlobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESCustomTimelineSourcePrivate));

  tlobj_class->fill_track_object = ges_custom_timeline_source_fill_track_object;
  tlobj_class->create_track_object =
      ges_custom_timeline_source_create_track_object;
}

static void
ges_custom_timeline_source_init (GESCustomTimelineSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_CUSTOM_TIMELINE_SOURCE, GESCustomTimelineSourcePrivate);
}

static gboolean
ges_custom_timeline_source_fill_track_object (GESTimelineObject * object,
    GESTrackObject * trobject, GstElement * gnlobj)
{
  gboolean res;
  GESCustomTimelineSourcePrivate *priv;

  GST_DEBUG ("Calling callback (timelineobj:%p, trackobj:%p, gnlobj:%p)",
      object, trobject, gnlobj);

  priv = GES_CUSTOM_TIMELINE_SOURCE (object)->priv;
  res = priv->filltrackobjectfunc (object, trobject, gnlobj, priv->user_data);

  GST_DEBUG ("Returning res:%d", res);

  return res;
}

/**
 * ges_custom_timeline_source_new:
 * @func: (scope notified): The #GESFillTrackObjectUserFunc that will be used to fill the track
 * objects.
 * @user_data: (closure): a gpointer that will be used when @func is called.
 *
 * Creates a new #GESCustomTimelineSource.
 *
 * Returns: The new #GESCustomTimelineSource.
 */
GESCustomTimelineSource *
ges_custom_timeline_source_new (GESFillTrackObjectUserFunc func,
    gpointer user_data)
{
  GESCustomTimelineSource *src;

  src = g_object_new (GES_TYPE_CUSTOM_TIMELINE_SOURCE, NULL);
  src->priv->filltrackobjectfunc = func;
  src->priv->user_data = user_data;

  return src;
}
