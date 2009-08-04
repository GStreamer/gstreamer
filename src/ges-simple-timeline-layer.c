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

#include "ges-simple-timeline-layer.h"

G_DEFINE_TYPE (GESSimpleTimelineLayer, ges_simple_timeline_layer, GES_TIMELINE_LAYER)

#define GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GES_TYPE_SIMPLE_TIMELINE_LAYER, GESSimpleTimelineLayerPrivate))

typedef struct _GESSimpleTimelineLayerPrivate GESSimpleTimelineLayerPrivate;

struct _GESSimpleTimelineLayerPrivate {
    int dummy;
};

static void
ges_simple_timeline_layer_get_property (GObject *object, guint property_id,
                              GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_timeline_layer_set_property (GObject *object, guint property_id,
                              const GValue *value, GParamSpec *pspec)
{
  switch (property_id) {
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_timeline_layer_dispose (GObject *object)
{
  G_OBJECT_CLASS (ges_simple_timeline_layer_parent_class)->dispose (object);
}

static void
ges_simple_timeline_layer_finalize (GObject *object)
{
  G_OBJECT_CLASS (ges_simple_timeline_layer_parent_class)->finalize (object);
}

static void
ges_simple_timeline_layer_class_init (GESSimpleTimelineLayerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESSimpleTimelineLayerPrivate));

  object_class->get_property = ges_simple_timeline_layer_get_property;
  object_class->set_property = ges_simple_timeline_layer_set_property;
  object_class->dispose = ges_simple_timeline_layer_dispose;
  object_class->finalize = ges_simple_timeline_layer_finalize;
}

static void
ges_simple_timeline_layer_init (GESSimpleTimelineLayer *self)
{
}

GESSimpleTimelineLayer*
ges_simple_timeline_layer_new (void)
{
  return g_object_new (GES_TYPE_SIMPLE_TIMELINE_LAYER, NULL);
}

