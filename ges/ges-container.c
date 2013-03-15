/* GStreamer Editing Services
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
 *               <2013> Collabora Ltd.
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
 * SECTION:ges-container
 * @short_description: Base Class for objects responsible for controlling other
 * GESTimelineElement-s
 */

#include "ges-container.h"
#include "ges.h"
#include "ges-internal.h"

#include <string.h>

#define _GET_PRIV(o) (G_TYPE_INSTANCE_GET_PRIVATE (self, GES_TYPE_CONTAINER, GESContainerPrivate))

G_DEFINE_ABSTRACT_TYPE (GESContainer, ges_container, GES_TYPE_TIMELINE_ELEMENT);

GST_DEBUG_CATEGORY_STATIC (ges_container_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT ges_container_debug

/* Mapping of relationship between a Container and the TimelineElements
 * it controls
 *
 * NOTE : Does it make sense to make it public in the future ?
 */
typedef struct
{
  GESTimelineElement *child;

  GstClockTime start_offset;
  GstClockTime duration_offset;
  GstClockTime inpoint_offset;
  gint32 priority_offset;

  guint start_notifyid;
  guint duration_notifyid;
  guint inpoint_notifyid;
  guint priority_notifyid;
} ChildMapping;

enum
{
  CHILD_ADDED_SIGNAL,
  CHILD_REMOVED_SIGNAL,
  LAST_SIGNAL
};

static guint ges_container_signals[LAST_SIGNAL] = { 0 };

struct _GESContainerPrivate
{
  /*< public > */
  GESTimelineLayer *layer;

  /*< private > */
  /* Set to TRUE when the container is doing updates of track object
   * properties so we don't end up in infinite property update loops
   */
  gboolean ignore_notifies;
  GHashTable *mappings;
  guint nb_effects;
  GESTimelineElement *initiated_move;
};

enum
{
  PROP_0,
  PROP_HEIGHT,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

/************************
 *                      *
 *   Private  methods   *
 *                      *
 ************************/
static void
update_height (GESContainer * container)
{
  GList *tmp;
  guint32 min_prio = G_MAXUINT32, max_prio = 0;

  GST_FIXME_OBJECT (container, "No children, we should reset our height to 0");
  if (container->children == NULL)
    return;

  /* Go over all childs and check if height has changed */
  for (tmp = container->children; tmp; tmp = tmp->next) {
    guint tck_priority = _PRIORITY (tmp->data);

    if (tck_priority < min_prio)
      min_prio = tck_priority;
    if (tck_priority > max_prio)
      max_prio = tck_priority;
  }

  if (container->height < (max_prio - min_prio + 1)) {
    container->height = max_prio - min_prio + 1;
    GST_DEBUG_OBJECT (container, "Updating height %i", container->height);
    g_object_notify (G_OBJECT (container), "height");
  } else
    GST_FIXME_OBJECT (container, "We only grow the height!");
}

static void
_free_mapping (ChildMapping * mapping)
{
  GESTimelineElement *child = mapping->child;

  /* Disconnect all notify listeners */
  g_signal_handler_disconnect (child, mapping->start_notifyid);
  g_signal_handler_disconnect (child, mapping->duration_notifyid);
  g_signal_handler_disconnect (child, mapping->inpoint_notifyid);
  g_signal_handler_disconnect (child, mapping->priority_notifyid);

  ges_timeline_element_set_parent (child, NULL);
  g_object_unref (child);
  g_slice_free (ChildMapping, mapping);
}

/*****************************************************
 *                                                   *
 * GESTimelineElement virtual methods implementation *
 *                                                   *
 *****************************************************/
static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp;
  ChildMapping *map;
  GESTimeline *timeline;
  GESContainer *container = GES_CONTAINER (element);
  GESContainerPrivate *priv = container->priv;

  GST_DEBUG_OBJECT (element, "Setting children start, (initiated_move: %"
      GST_PTR_FORMAT ")", container->priv->initiated_move);

  container->priv->ignore_notifies = TRUE;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    map = g_hash_table_lookup (priv->mappings, child);
    if (child != container->priv->initiated_move) {
      gint64 new_start = start - map->start_offset;

      /* Move the child... */
      if (new_start < 0) {
        GST_ERROR ("Trying to set start to a negative value -%" GST_TIME_FORMAT,
            GST_TIME_ARGS (-(start + map->start_offset)));
        continue;
      }

      /* Make the snapping happen if in a timeline */
      timeline = GES_TIMELINE_ELEMENT_TIMELINE (child);
      if (timeline == NULL || ges_timeline_move_object_simple (timeline, child,
              NULL, GES_EDGE_NONE, start) == FALSE)
        _set_start0 (GES_TIMELINE_ELEMENT (child), start);

    } else {
      /* ... update the offset for the child that initiated the move */
      map->start_offset = start - _START (child);
    }
  }
  container->priv->ignore_notifies = FALSE;

  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  GList *tmp;
  GESContainer *container = GES_CONTAINER (element);

  container->priv->ignore_notifies = TRUE;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;
    ChildMapping *map = g_hash_table_lookup (container->priv->mappings, child);

    if (child == container->priv->initiated_move) {
      map->inpoint_offset = inpoint - _INPOINT (child);
      continue;
    }

    _set_inpoint0 (child, inpoint);
  }
  container->priv->ignore_notifies = FALSE;

  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GList *tmp;
  GESTimeline *timeline;

  GESContainer *container = GES_CONTAINER (element);
  GESContainerPrivate *priv = container->priv;

  priv->ignore_notifies = TRUE;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;
    ChildMapping *map = g_hash_table_lookup (priv->mappings, child);

    if (child == container->priv->initiated_move) {
      map->duration_offset = duration - _DURATION (child);
      continue;
    }

    /* Make the snapping happen if in a timeline */
    timeline = GES_TIMELINE_ELEMENT_TIMELINE (child);
    if (timeline == NULL || ges_timeline_trim_object_simple (timeline, child,
            NULL, GES_EDGE_END, _START (child) + duration, TRUE) == FALSE)
      _set_duration0 (GES_TIMELINE_ELEMENT (child), duration);
  }
  priv->ignore_notifies = FALSE;

  return TRUE;
}

static gboolean
_set_max_duration (GESTimelineElement * element, GstClockTime maxduration)
{
  GList *tmp;

  for (tmp = GES_CONTAINER (element)->children; tmp; tmp = g_list_next (tmp))
    ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (tmp->data),
        maxduration);

  return TRUE;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GList *tmp;
  GESContainerPrivate *priv;
  guint32 min_prio, max_prio;

  GESContainer *container = GES_CONTAINER (element);

  priv = container->priv;

  GES_CONTAINER_GET_CLASS (element)->get_priorty_range (container, &min_prio,
      &max_prio);

  priv->ignore_notifies = TRUE; /*  */
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;
    ChildMapping *map = g_hash_table_lookup (priv->mappings, child);
    guint32 real_tck_prio = min_prio + priority + map->priority_offset;

    if (real_tck_prio > max_prio) {
      GST_WARNING ("%p priority of %i, is outside of the its containing "
          "layer space. (%d/%d) setting it to the maximum it can be",
          container, priority, min_prio, max_prio);

      real_tck_prio = max_prio;
    }
    _set_priority0 (child, real_tck_prio);
  }
  priv->ignore_notifies = FALSE;

  update_height (container);

  return TRUE;
}

/******************************************
 *                                        *
 * GObject virtual methods implementation *
 *                                        *
 ******************************************/
static void
_dispose (GObject * object)
{
  GESContainer *self = GES_CONTAINER (object);

  g_hash_table_unref (self->priv->mappings);
}

static void
_get_property (GObject * container, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESContainer *tobj = GES_CONTAINER (container);

  switch (property_id) {
    case PROP_HEIGHT:
      g_value_set_uint (value, tobj->height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (container, property_id, pspec);
  }
}

static void
_set_property (GObject * container, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (container, property_id, pspec);
  }
}

static void
ges_container_class_init (GESContainerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (ges_container_debug, "gescontainer",
      GST_DEBUG_FG_YELLOW, "ges container");

  g_type_class_add_private (klass, sizeof (GESContainerPrivate));

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;
  object_class->dispose = _dispose;

  /**
   * GESContainer:height:
   *
   * The span of priorities which this container occupies.
   */
  properties[PROP_HEIGHT] = g_param_spec_uint ("height", "Height",
      "The span of priorities this container occupies", 0, G_MAXUINT, 1,
      G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_HEIGHT,
      properties[PROP_HEIGHT]);

  /**
   * GESContainer::child-added:
   * @container: the #GESContainer
   * @element: the #GESTimelineElement that was added.
   *
   * Will be emitted after a child was added to @container.
   */
  ges_container_signals[CHILD_ADDED_SIGNAL] =
      g_signal_new ("child-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESContainerClass, child_added),
      NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_TIMELINE_ELEMENT);

  /**
   * GESContainer::child-removed:
   * @container: the #GESContainer
   * @element: the #GESTimelineElement that was removed.
   *
   * Will be emitted after a child was removed from @container.
   */
  ges_container_signals[CHILD_REMOVED_SIGNAL] =
      g_signal_new ("child-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESContainerClass, child_removed),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_ELEMENT);


  element_class->set_start = _set_start;
  element_class->set_duration = _set_duration;
  element_class->set_inpoint = _set_inpoint;
  element_class->set_priority = _set_priority;
  element_class->set_max_duration = _set_max_duration;

  /* No default implementations */
  klass->remove_child = NULL;
  klass->add_child = NULL;
  klass->ungroup = NULL;
  klass->group = NULL;
}

static void
ges_container_init (GESContainer * self)
{
  self->priv = _GET_PRIV (self);

  /* FIXME, check why default was GST_SECOND? (before the existend of
   * ges-container)
   *
   * _DURATION (self) = GST_SECOND; */
  self->height = 1;             /* FIXME Why 1 and not 0? */
  self->children = NULL;

  self->priv->mappings = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) _free_mapping);
}

/**********************************************
 *                                            *
 *    Property notifications from Children    *
 *                                            *
 **********************************************/
static void
_child_start_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  ChildMapping *map;

  GESContainerPrivate *priv = container->priv;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (container);

  if (priv->ignore_notifies)
    return;

  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  GST_FIXME_OBJECT (container, "We should make sure that our child does not"
      "involve our start becoming < 0. In that case, undo the child move.");

  /* We update all the children calling our set_start method */
  container->priv->initiated_move = child;
  _set_start0 (element, _START (child) + map->start_offset);
  container->priv->initiated_move = NULL;
}

static void
_child_inpoint_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  ChildMapping *map;

  GESContainerPrivate *priv = container->priv;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (container);

  if (priv->ignore_notifies)
    return;

  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  /* We update all the children calling our set_inpoint method */
  container->priv->initiated_move = child;
  _set_inpoint0 (element, _INPOINT (child) + map->inpoint_offset);
  container->priv->initiated_move = NULL;
}

static void
_child_duration_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  ChildMapping *map;

  GESContainerPrivate *priv = container->priv;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (container);

  if (priv->ignore_notifies)
    return;

  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  /* We update all the children calling our set_duration method */
  container->priv->initiated_move = child;
  _set_duration0 (element, _DURATION (child) + map->duration_offset);
  container->priv->initiated_move = NULL;
}

static void
_child_priority_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  ChildMapping *map;
  guint32 min_prio, max_prio;
  GESContainerPrivate *priv = container->priv;

  GST_DEBUG_OBJECT (container, "TimelineElement %p priority changed to %i",
      child, _PRIORITY (child));

  if (priv->ignore_notifies)
    return;

  update_height (container);

  /* Update mapping */
  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  GES_CONTAINER_GET_CLASS (container)->get_priorty_range (container, &min_prio,
      &max_prio);

  map->priority_offset = min_prio + _PRIORITY (container) - _PRIORITY (child);
}

/****************************************************
 *                                                  *
 *         Internal methods implementation          *
 *                                                  *
 ****************************************************/

void
_ges_container_sort_children (GESContainer * container)
{
  container->children = g_list_sort (container->children,
      (GCompareFunc) element_start_compare);
}

void
_ges_container_set_ignore_notifies (GESContainer * container,
    gboolean ignore_notifies)
{
  container->priv->ignore_notifies = ignore_notifies;
}

/**********************************************
 *                                            *
 *            API implementation              *
 *                                            *
 **********************************************/

/**
 * ges_container_add:
 * @container: a #GESContainer
 * @child: the #GESTimelineElement
 *
 * Add the #GESTimelineElement to the container.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */
gboolean
ges_container_add (GESContainer * container, GESTimelineElement * child)
{
  ChildMapping *mapping;
  GESContainerClass *class;
  GESContainerPrivate *priv;
  guint32 min_prio, max_prio;

  g_return_val_if_fail (GES_IS_CONTAINER (container), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (child), FALSE);

  class = GES_CONTAINER_GET_CLASS (container);
  priv = container->priv;

  GST_DEBUG_OBJECT (container, "adding timeline element %" GST_PTR_FORMAT,
      child);

  priv->ignore_notifies = TRUE;
  if (class->add_child) {
    if (class->add_child (container, child) == FALSE) {
      GST_WARNING_OBJECT (container, "Erreur adding child %p", child);
      return FALSE;
    }
  }
  priv->ignore_notifies = FALSE;

  mapping = g_slice_new0 (ChildMapping);
  mapping->child = g_object_ref (child);
  mapping->start_offset = _START (container) - _START (child);
  mapping->duration_offset = _DURATION (container) - _DURATION (child);
  mapping->inpoint_offset = _INPOINT (container) - _INPOINT (child);
  GES_CONTAINER_GET_CLASS (container)->get_priorty_range (container,
      &min_prio, &max_prio);
  mapping->priority_offset =
      min_prio + _PRIORITY (container) - _PRIORITY (child);

  g_hash_table_insert (priv->mappings, child, mapping);

  container->children = g_list_prepend (container->children, child);

  /* Listen to all property changes */
  mapping->start_notifyid =
      g_signal_connect (G_OBJECT (child), "notify::start",
      G_CALLBACK (_child_start_changed_cb), container);
  mapping->duration_notifyid =
      g_signal_connect (G_OBJECT (child), "notify::duration",
      G_CALLBACK (_child_duration_changed_cb), container);
  mapping->inpoint_notifyid =
      g_signal_connect (G_OBJECT (child), "notify::inpoint",
      G_CALLBACK (_child_inpoint_changed_cb), container);
  mapping->priority_notifyid =
      g_signal_connect (G_OBJECT (child), "notify::priority",
      G_CALLBACK (_child_priority_changed_cb), container);
  update_height (container);


  if (ges_timeline_element_set_parent (child, GES_TIMELINE_ELEMENT (container))
      == FALSE) {
    GST_FIXME_OBJECT (container, "Revert everything that was done before!");

    return FALSE;
  }

  g_signal_emit (container, ges_container_signals[CHILD_ADDED_SIGNAL], 0,
      child);

  return TRUE;
}

/**
 * ges_container_remove:
 * @container: a #GESContainer
 * @child: the #GESTimelineElement to release
 *
 * Release the @child from the control of @container.
 *
 * Returns: %TRUE if the @child was properly released, else %FALSE.
 */
gboolean
ges_container_remove (GESContainer * container, GESTimelineElement * child)
{
  GESContainerClass *klass;
  GESContainerPrivate *priv;

  g_return_val_if_fail (GES_IS_CONTAINER (container), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (child), FALSE);

  GST_DEBUG_OBJECT (container, "removing child: %" GST_PTR_FORMAT, child);

  klass = GES_CONTAINER_GET_CLASS (container);
  priv = container->priv;

  if (!(g_hash_table_lookup (priv->mappings, child))) {
    GST_WARNING_OBJECT (container, "Element isn't controlled by this "
        "container");
    return FALSE;
  }

  if (klass->remove_child) {
    if (klass->remove_child (container, child) == FALSE)
      return FALSE;
  }

  container->children = g_list_remove (container->children, child);
  /* Let it live removing from our mappings */
  g_hash_table_remove (priv->mappings, child);

  g_signal_emit (container, ges_container_signals[CHILD_REMOVED_SIGNAL], 0,
      child);

  return TRUE;
}

/**
 * ges_container_get_children:
 * @container: a #GESContainer
 *
 * Get the list of #GESTimelineElement contained in @container
 * The user is responsible for unreffing the contained objects
 * and freeing the list.
 *
 * Returns: (transfer full) (element-type GESTimelineElement): The list of
 * timeline element contained in @container.
 */
GList *
ges_container_get_children (GESContainer * container)
{
  g_return_val_if_fail (GES_IS_CONTAINER (container), NULL);

  return g_list_copy_deep (container->children, (GCopyFunc) gst_object_ref,
      NULL);
}

/**
 * ges_container_ungroup:
 * @container: (transfer full): The #GESContainer to ungroup
 * @recursive: Wether to recursively ungroup @container
 *
 * Ungroups the #GESTimelineElement contained in this GESContainer,
 * creating new #GESContainer containing those #GESTimelineElement
 * apropriately.
 *
 * Returns: (transfer container) (element-type GESContainer): The list of
 * #GESContainer resulting from the ungrouping operation
 * The user is responsible for unreffing the contained objects
 * and freeing the list.
 */
GList *
ges_container_ungroup (GESContainer * container, gboolean recursive)
{
  GESContainerClass *klass;

  g_return_val_if_fail (GES_IS_CONTAINER (container), NULL);

  GST_DEBUG_OBJECT (container, "Ungrouping container %s recursively",
      recursive ? "" : "not");

  klass = GES_CONTAINER_GET_CLASS (container);
  if (klass->ungroup == NULL) {
    GST_INFO_OBJECT (container, "No ungoup virtual method, doint nothing");
    return NULL;
  }

  return klass->ungroup (container, recursive);
}

/**
 * ges_container_group:
 * @containers: (transfer none)(element-type GESContainer): The
 * #GESContainer to group, they must all be in a same #GESTimeline
 *
 * Groups the #GESContainer-s provided in @containers. It creates a subclass
 * of #GESContainer, depending on the containers provided in @containers.
 * Basically, if all the containers in @containers should be contained in a same
 * clip (all the #GESTrackElement they contain have the exact same
 * start/inpoint/duration and are in the same layer), it will create a #GESClip
 * otherwise a #GESGroup will be created
 *
 * Returns: (transfer none): The #GESContainer (subclass) resulting of the
 * grouping
 */
GESContainer *
ges_container_group (GList * containers)
{
  GList *tmp;
  GESContainer *ret;
  GESTimeline *timeline;
  GESTimelineElement *element;
  GObjectClass *clip_class;

  g_return_val_if_fail (containers, NULL);
  element = GES_TIMELINE_ELEMENT (containers->data);
  timeline = ges_timeline_element_get_timeline (element);
  g_return_val_if_fail (timeline, NULL);

  if (g_list_length (containers) == 1)
    return containers->data;

  for (tmp = containers; tmp; tmp = tmp->next) {
    g_return_val_if_fail (GES_IS_CONTAINER (tmp->data), NULL);
    g_return_val_if_fail (ges_timeline_element_get_timeline
        (GES_TIMELINE_ELEMENT (tmp->data)) == timeline, NULL);
  }

  clip_class = g_type_class_peek (GES_TYPE_CLIP);
  ret = GES_CONTAINER_CLASS (clip_class)->group (containers);

  return ret;
}
