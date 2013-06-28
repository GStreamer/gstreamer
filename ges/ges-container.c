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
  GESLayer *layer;

  /*< private > */
  /* Set to TRUE when the container is doing updates of track object
   * properties so we don't end up in infinite property update loops
   */
  GESChildrenControlMode children_control_mode;
  GHashTable *mappings;
  guint nb_effects;
};

enum
{
  PROP_0,
  PROP_HEIGHT,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

/************************
 *   Private  methods   *
 ************************/
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
  g_slice_free (ChildMapping, mapping);
}

static gint
compare_grouping_prio (GType * a, GType * b)
{
  gint ret = 0;
  GObjectClass *aclass = g_type_class_ref (*a);
  GObjectClass *bclass = g_type_class_ref (*b);

  /* We want higher prios to be first */
  if (GES_CONTAINER_CLASS (aclass)->grouping_priority <
      GES_CONTAINER_CLASS (bclass)->grouping_priority)
    ret = 1;
  else if (GES_CONTAINER_CLASS (aclass)->grouping_priority >
      GES_CONTAINER_CLASS (bclass)->grouping_priority)
    ret = -1;

  g_type_class_unref (aclass);
  g_type_class_unref (bclass);
  return ret;
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
  GESContainer *container = GES_CONTAINER (element);
  GESContainerPrivate *priv = container->priv;

  GST_DEBUG_OBJECT (element, "Updating children offsets, (initiated_move: %"
      GST_PTR_FORMAT ")", container->initiated_move);

  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    map = g_hash_table_lookup (priv->mappings, child);
    map->start_offset = start - _START (child);
  }
  priv->children_control_mode = GES_CHILDREN_UPDATE;

  return TRUE;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  GList *tmp;
  GESContainer *container = GES_CONTAINER (element);

  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;
    ChildMapping *map = g_hash_table_lookup (container->priv->mappings, child);

    map->inpoint_offset = inpoint - _INPOINT (child);
  }

  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GList *tmp;
  GESContainer *container = GES_CONTAINER (element);
  GESContainerPrivate *priv = container->priv;

  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;
    ChildMapping *map = g_hash_table_lookup (priv->mappings, child);

    map->duration_offset = duration - _DURATION (child);
  }

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

  /* No default implementations */
  klass->remove_child = NULL;
  klass->add_child = NULL;
  klass->ungroup = NULL;
  klass->group = NULL;
  klass->grouping_priority = 0;
  klass->edit = NULL;
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

  if (priv->children_control_mode == GES_CHILDREN_IGNORE_NOTIFIES)
    return;

  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  GST_FIXME_OBJECT (container, "We should make sure that our child does not"
      "involve our start becoming < 0. In that case, undo the child move.");

  /* We update all the children calling our set_start method */
  container->initiated_move = child;
  _set_start0 (element, _START (child) + map->start_offset);
  container->initiated_move = NULL;
}

static void
_child_inpoint_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  ChildMapping *map;

  GESContainerPrivate *priv = container->priv;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (container);

  if (priv->children_control_mode == GES_CHILDREN_IGNORE_NOTIFIES)
    return;

  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  /* We update all the children calling our set_inpoint method */
  container->initiated_move = child;
  _set_inpoint0 (element, _INPOINT (child) + map->inpoint_offset);
  container->initiated_move = NULL;
}

static void
_child_duration_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  ChildMapping *map;

  GESContainerPrivate *priv = container->priv;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (container);

  if (priv->children_control_mode == GES_CHILDREN_IGNORE_NOTIFIES)
    return;

  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  /* We update all the children calling our set_duration method */
  container->initiated_move = child;
  _set_duration0 (element, _DURATION (child) + map->duration_offset);
  container->initiated_move = NULL;
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

  if (priv->children_control_mode == GES_CHILDREN_IGNORE_NOTIFIES)
    return;

  /* Update mapping */
  map = g_hash_table_lookup (priv->mappings, child);
  g_assert (map);

  GES_CONTAINER_GET_CLASS (container)->get_priority_range (container, &min_prio,
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
_ges_container_sort_children_by_end (GESContainer * container)
{
  container->children = g_list_sort (container->children,
      (GCompareFunc) element_end_compare);
}

void
_ges_container_set_children_control_mode (GESContainer * container,
    GESChildrenControlMode children_control_mode)
{
  container->priv->children_control_mode = children_control_mode;
}

void
_ges_container_set_height (GESContainer * container, guint32 height)
{
  if (container->height != height) {
    container->height = height;
    GST_DEBUG_OBJECT (container, "Updating height %i", container->height);
    g_object_notify (G_OBJECT (container), "height");
  }
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

  priv->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  if (class->add_child) {
    if (class->add_child (container, child) == FALSE) {
      priv->children_control_mode = GES_CHILDREN_UPDATE;
      GST_WARNING_OBJECT (container, "Erreur adding child %p", child);
      return FALSE;
    }
  }
  priv->children_control_mode = GES_CHILDREN_UPDATE;

  mapping = g_slice_new0 (ChildMapping);
  mapping->child = gst_object_ref (child);
  mapping->start_offset = _START (container) - _START (child);
  mapping->duration_offset = _DURATION (container) - _DURATION (child);
  mapping->inpoint_offset = _INPOINT (container) - _INPOINT (child);
  GES_CONTAINER_GET_CLASS (container)->get_priority_range (container,
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
      g_signal_connect (G_OBJECT (child), "notify::in-point",
      G_CALLBACK (_child_inpoint_changed_cb), container);
  mapping->priority_notifyid =
      g_signal_connect (G_OBJECT (child), "notify::priority",
      G_CALLBACK (_child_priority_changed_cb), container);


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
  gst_object_unref (child);

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
 * Returns: (transfer full) (element-type GESContainer): The list of
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
  guint n_children;
  GESTimeline *timeline;
  GType *children_types;
  GESTimelineElement *element;
  GObjectClass *clip_class;

  guint i = 0;
  GESContainer *ret = NULL;

  g_return_val_if_fail (containers, NULL);
  element = GES_TIMELINE_ELEMENT (containers->data);
  timeline = GES_TIMELINE_ELEMENT_TIMELINE (element);
  g_return_val_if_fail (timeline, NULL);

  if (g_list_length (containers) == 1)
    return containers->data;

  for (tmp = containers; tmp; tmp = tmp->next) {
    g_return_val_if_fail (GES_IS_CONTAINER (tmp->data), NULL);
    g_return_val_if_fail (GES_TIMELINE_ELEMENT_PARENT (tmp->data) == NULL,
        NULL);
    g_return_val_if_fail (GES_TIMELINE_ELEMENT_TIMELINE (tmp->data) == timeline,
        NULL);
  }

  children_types = g_type_children (GES_TYPE_CONTAINER, &n_children);
  g_qsort_with_data (children_types, n_children, sizeof (GType),
      (GCompareDataFunc) compare_grouping_prio, NULL);

  for (i = 0; i < n_children; i++) {
    clip_class = g_type_class_peek (children_types[i]);
    ret = GES_CONTAINER_CLASS (clip_class)->group (containers);

    if (ret)
      break;
  }

  g_free (children_types);
  return ret;
}

/**
 * ges_container_edit:
 * @container: the #GESClip to edit
 * @layers: (element-type GESLayer): The layers you want the edit to
 *  happen in, %NULL means that the edition is done in all the
 *  #GESLayers contained in the current timeline.
 * @new_layer_priority: The priority of the layer @container should land in.
 *  If the layer you're trying to move the container to doesn't exist, it will
 *  be created automatically. -1 means no move.
 * @mode: The #GESEditMode in which the editition will happen.
 * @edge: The #GESEdge the edit should happen on.
 * @position: The position at which to edit @container (in nanosecond)
 *
 * Edit @container in the different exisiting #GESEditMode modes. In the case of
 * slide, and roll, you need to specify a #GESEdge
 *
 * Returns: %TRUE if the container as been edited properly, %FALSE if an error
 * occured
 */
gboolean
ges_container_edit (GESContainer * container, GList * layers,
    gint new_layer_priority, GESEditMode mode, GESEdge edge, guint64 position)
{
  g_return_val_if_fail (GES_IS_CONTAINER (container), FALSE);

  if (G_UNLIKELY (GES_CONTAINER_GET_CLASS (container)->edit == NULL)) {
    GST_WARNING_OBJECT (container, "No edit vmethod implementation");
    return FALSE;
  }

  return GES_CONTAINER_GET_CLASS (container)->edit (container, layers,
      new_layer_priority, mode, edge, position);
}
