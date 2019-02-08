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

/**
 * SECTION:gessourceclip
 * @title: GESSourceClip
 * @short_description: Base Class for sources of a GESLayer
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-clip.h"
#include "ges-source-clip.h"
#include "ges-source.h"


struct _GESSourceClipPrivate
{
  /*  dummy variable */
  void *nothing;
};

enum
{
  PROP_0,
};

G_DEFINE_TYPE_WITH_PRIVATE (GESSourceClip, ges_source_clip, GES_TYPE_CLIP);

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp;
  GESTimeline *timeline;
  GESContainer *container = GES_CONTAINER (element);
  GstClockTime rollback_start = GES_TIMELINE_ELEMENT_START (element);

  GST_DEBUG_OBJECT (element, "Setting children start, (initiated_move: %"
      GST_PTR_FORMAT ")", container->initiated_move);

  element->start = start;
  g_object_notify (G_OBJECT (element), "start");
  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    if (child != container->initiated_move) {
      /* Make the snapping happen if in a timeline */
      timeline = GES_TIMELINE_ELEMENT_TIMELINE (child);
      if (timeline && !container->initiated_move) {
        if (!ges_timeline_move_object_simple (timeline, child, NULL,
                GES_EDGE_NONE, start)) {
          for (tmp = container->children; tmp; tmp = g_list_next (tmp))
            ges_timeline_element_set_start (tmp->data, rollback_start);

          element->start = rollback_start;
          g_object_notify (G_OBJECT (element), "start");
          container->children_control_mode = GES_CHILDREN_UPDATE;
          return FALSE;
        }
      }

      _set_start0 (GES_TIMELINE_ELEMENT (child), start);
    }
  }

  container->children_control_mode = GES_CHILDREN_UPDATE;

  return FALSE;
}

static void
ges_source_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_source_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_source_clip_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_source_clip_parent_class)->finalize (object);
}

static void
ges_source_clip_class_init (GESSourceClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  object_class->get_property = ges_source_clip_get_property;
  object_class->set_property = ges_source_clip_set_property;
  object_class->finalize = ges_source_clip_finalize;

  element_class->set_start = _set_start;
}

static void
ges_source_clip_init (GESSourceClip * self)
{
  self->priv = ges_source_clip_get_instance_private (self);
}
