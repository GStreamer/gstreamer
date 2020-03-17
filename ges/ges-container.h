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

#pragma once

#include <glib-object.h>
#include <gst/gst.h>
#include <ges/ges-timeline-element.h>
#include <ges/ges-types.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_CONTAINER             ges_container_get_type()
GES_DECLARE_TYPE(Container, container, CONTAINER);

/**
 * GESChildrenControlMode:
 *
 * To be used by subclasses only. This indicate how to handle a change in
 * a child.
 */
typedef enum
{
  GES_CHILDREN_UPDATE,
  GES_CHILDREN_IGNORE_NOTIFIES,
  GES_CHILDREN_UPDATE_OFFSETS,
  GES_CHILDREN_UPDATE_ALL_VALUES,
  GES_CHILDREN_LAST
} GESChildrenControlMode;

/**
 * GES_CONTAINER_HEIGHT:
 * @obj: a #GESContainer
 *
 * The #GESContainer:height of @obj.
 */
#define GES_CONTAINER_HEIGHT(obj) (((GESContainer*)obj)->height)

/**
 * GES_CONTAINER_CHILDREN:
 * @obj: a #GESContainer
 *
 * The #GList containing the children of @obj.
 */
#define GES_CONTAINER_CHILDREN(obj) (((GESContainer*)obj)->children)

/**
 * GESContainer:
 * @children: (element-type GES.TimelineElement): The list of
 * #GESTimelineElement-s controlled by this Container
 * @height: The #GESContainer:height of @obj
 *
 * Note, you may read, but should not modify these properties.
 */
struct _GESContainer
{
  GESTimelineElement parent;

  /*< public > */
  /*< readonly >*/
  GList *children;

  /* We don't add those properties to the priv struct for optimization and code
   * readability purposes */
  guint32 height;       /* the span of priorities this object needs */

  /* <protected> */
  GESChildrenControlMode children_control_mode;
  /*< readonly >*/
  GESTimelineElement *initiated_move;

  /*< private >*/
  GESContainerPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

/**
 * GESContainerClass:
 * @child_added: Virtual method that is called right after a #GESTimelineElement is added
 * @child_removed: Virtual method that is called right after a #GESTimelineElement is removed
 * @remove_child: Virtual method to remove a child
 * @add_child: Virtual method to add a child
 * @ungroup: Virtual method to ungroup a container into a list of
 * containers
 * @group: Virtual method to group a list of containers together under a
 * single container
 * @edit: Deprecated
 */
struct _GESContainerClass
{
  /*< private > */
  GESTimelineElementClass parent_class;

  /*< public > */
  /* signals */
  void (*child_added)             (GESContainer *container, GESTimelineElement *element);
  void (*child_removed)           (GESContainer *container, GESTimelineElement *element);
  gboolean (*add_child)           (GESContainer *container, GESTimelineElement *element);
  gboolean (*remove_child)        (GESContainer *container, GESTimelineElement *element);
  GList* (*ungroup)               (GESContainer *container, gboolean recursive);
  GESContainer * (*group)         (GList *containers);

  /* Deprecated and not used anymore */
  gboolean (*edit)                (GESContainer * container,
                                   GList * layers, gint new_layer_priority,
                                   GESEditMode mode,
                                   GESEdge edge,
                                   guint64 position);



  /*< private >*/
  guint grouping_priority;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING_LARGE];
};

/* Children handling */
GES_API
GList* ges_container_get_children (GESContainer *container, gboolean recursive);
GES_API
gboolean ges_container_add        (GESContainer *container, GESTimelineElement *child);
GES_API
gboolean ges_container_remove     (GESContainer *container, GESTimelineElement *child);
GES_API
GList * ges_container_ungroup     (GESContainer * container, gboolean recursive);
GES_API
GESContainer *ges_container_group (GList *containers);

GES_DEPRECATED_FOR(ges_timeline_element_edit)
gboolean ges_container_edit       (GESContainer * container,
                                   GList * layers, gint new_layer_priority,
                                   GESEditMode mode,
                                   GESEdge edge,
                                   guint64 position);

G_END_DECLS
