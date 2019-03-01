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
  GESTimelineElement *toplevel =
      ges_timeline_element_get_toplevel_parent (element);

  gst_object_unref (toplevel);
  if (element->timeline
      && !ELEMENT_FLAG_IS_SET (element, GES_TIMELINE_ELEMENT_SET_SIMPLE)
      && !ELEMENT_FLAG_IS_SET (toplevel, GES_TIMELINE_ELEMENT_SET_SIMPLE)) {
    ges_timeline_move_object_simple (element->timeline, element, NULL,
        GES_EDGE_NONE, start);
    return FALSE;
  }

  return
      GES_TIMELINE_ELEMENT_CLASS (ges_source_clip_parent_class)->set_start
      (element, start);
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GESTimelineElement *toplevel =
      ges_timeline_element_get_toplevel_parent (element);

  gst_object_unref (toplevel);
  if (element->timeline
      && !ELEMENT_FLAG_IS_SET (element, GES_TIMELINE_ELEMENT_SET_SIMPLE)
      && !ELEMENT_FLAG_IS_SET (toplevel, GES_TIMELINE_ELEMENT_SET_SIMPLE)) {
    return !timeline_trim_object (element->timeline, element,
        GES_TIMELINE_ELEMENT_LAYER_PRIORITY (element), NULL, GES_EDGE_END,
        element->start + duration);
  }

  return
      GES_TIMELINE_ELEMENT_CLASS (ges_source_clip_parent_class)->set_duration
      (element, duration);
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
  element_class->set_duration = _set_duration;
}

static void
ges_source_clip_init (GESSourceClip * self)
{
  self->priv = ges_source_clip_get_instance_private (self);
}
