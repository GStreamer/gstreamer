/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "ges-internal.h"
#include "ges-custom-timeline-source.h"
#include "ges-timeline-source.h"

G_DEFINE_TYPE (GESCustomTimelineSource, ges_cust_timeline_src,
    GES_TYPE_TIMELINE_SOURCE)

     static gboolean
         ges_cust_timeline_src_fill_track_object (GESTimelineObject * object,
    GESTrackObject * trobject, GstElement * gnlobj);

     static void
         ges_cust_timeline_src_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_cust_timeline_src_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_cust_timeline_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_cust_timeline_src_parent_class)->dispose (object);
}

static void
ges_cust_timeline_src_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_cust_timeline_src_parent_class)->finalize (object);
}

static void
ges_cust_timeline_src_class_init (GESCustomTimelineSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *tlobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  object_class->get_property = ges_cust_timeline_src_get_property;
  object_class->set_property = ges_cust_timeline_src_set_property;
  object_class->dispose = ges_cust_timeline_src_dispose;
  object_class->finalize = ges_cust_timeline_src_finalize;

  tlobj_class->fill_track_object = ges_cust_timeline_src_fill_track_object;
}

static void
ges_cust_timeline_src_init (GESCustomTimelineSource * self)
{
}

static gboolean
ges_cust_timeline_src_fill_track_object (GESTimelineObject * object,
    GESTrackObject * trobject, GstElement * gnlobj)
{
  gboolean res;

  GST_DEBUG ("Calling callback (timelineobj:%p, trackobj:%, gnlobj:%p)",
      object, trobject, gnlobj);

  res =
      GES_CUSTOM_TIMELINE_SOURCE (object)->filltrackobjectfunc (object,
      trobject, gnlobj);

  GST_DEBUG ("Returning res:%d", res);

  return res;
}

GESCustomTimelineSource *
ges_custom_timeline_source_new (FillTrackObjectFunc func)
{
  GESCustomTimelineSource *src;

  src = g_object_new (GES_TYPE_CUSTOM_TIMELINE_SOURCE, NULL);
  src->filltrackobjectfunc = func;

  return src;
}
