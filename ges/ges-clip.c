/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
 *               2012 Collabora Ltd.
 *                 Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:ges-clip
 * @short_description: Base Class for objects in a GESTimelineLayer
 *
 * A #GESClip is a 'natural' object which controls one or more
 * #GESTrackElement(s) in one or more #GESTrack(s).
 *
 * Keeps a reference to the #GESTrackElement(s) it created and
 * sets/updates their properties.
 */

#include "ges-clip.h"
#include "ges.h"
#include "ges-internal.h"

#include <string.h>

gboolean
ges_clip_fill_track_element_func (GESClip * object,
    GESTrackElement * trackelement, GstElement * gnlobj);

GList *ges_clip_create_track_elements_func (GESClip * object,
    GESTrackType type);

static gboolean _set_max_duration (GESTimelineElement * element,
    GstClockTime maxduration);

static void
track_element_start_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object);
static void
track_element_inpoint_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object);
static void
track_element_duration_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object);
static void
track_element_priority_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object);
static void update_height (GESClip * object);

static gint sort_base_effects (gpointer a, gpointer b, GESClip * object);
static void
get_layer_priorities (GESTimelineLayer * layer, guint32 * layer_min_gnl_prio,
    guint32 * layer_max_gnl_prio);

static gboolean _set_start (GESTimelineElement * element, GstClockTime start);
static gboolean _set_inpoint (GESTimelineElement * element,
    GstClockTime inpoint);
static gboolean _set_duration (GESTimelineElement * element,
    GstClockTime duration);
static gboolean _set_priority (GESTimelineElement * element, guint32 priority);

static gboolean _ripple (GESTimelineElement * element, GstClockTime start);
static gboolean _ripple_end (GESTimelineElement * element, GstClockTime end);
static gboolean _roll_start (GESTimelineElement * element, GstClockTime start);
static gboolean _roll_end (GESTimelineElement * element, GstClockTime end);
static gboolean _trim (GESTimelineElement * element, GstClockTime start);

G_DEFINE_ABSTRACT_TYPE (GESClip, ges_clip, GES_TYPE_TIMELINE_ELEMENT);

/* Mapping of relationship between a Clip and the TrackElements
 * it controls
 *
 * NOTE : how do we make this public in the future ?
 */
typedef struct
{
  GESTrackElement *object;
  gint64 start_offset;
  gint64 duration_offset;
  gint64 inpoint_offset;
  gint32 priority_offset;

  guint start_notifyid;
  guint duration_notifyid;
  guint inpoint_notifyid;
  guint priority_notifyid;

  /* track mapping ?? */
} ObjectMapping;

enum
{
  EFFECT_ADDED,
  EFFECT_REMOVED,
  TRACK_ELEMENT_ADDED,
  TRACK_ELEMENT_REMOVED,
  LAST_SIGNAL
};

static guint ges_clip_signals[LAST_SIGNAL] = { 0 };

struct _GESClipPrivate
{
  /*< public > */
  GESTimelineLayer *layer;

  /*< private > */

  /* Set to TRUE when the clip is doing updates of track element
   * properties so we don't end up in infinite property update loops
   */
  gboolean ignore_notifies;
  gboolean is_moving;

  GList *mappings;

  guint nb_effects;

  GESTrackElement *initiated_move;

  /* The formats supported by this Clip */
  GESTrackType supportedformats;

  GESAsset *asset;
};

enum
{
  PROP_0,
  PROP_HEIGHT,
  PROP_LAYER,
  PROP_SUPPORTED_FORMATS,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void
ges_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESClip *tobj = GES_CLIP (object);

  switch (property_id) {
    case PROP_HEIGHT:
      g_value_set_uint (value, tobj->height);
      break;
    case PROP_LAYER:
      g_value_set_object (value, tobj->priv->layer);
      break;
    case PROP_SUPPORTED_FORMATS:
      g_value_set_flags (value, tobj->priv->supportedformats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESClip *tobj = GES_CLIP (object);

  switch (property_id) {
    case PROP_SUPPORTED_FORMATS:
      ges_clip_set_supported_formats (tobj, g_value_get_flags (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_clip_class_init (GESClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESClipPrivate));

  object_class->get_property = ges_clip_get_property;
  object_class->set_property = ges_clip_set_property;
  klass->create_track_elements = ges_clip_create_track_elements_func;
  klass->create_track_element = NULL;
  klass->track_element_added = NULL;
  klass->track_element_released = NULL;

  /**
   * GESClip:height:
   *
   * The span of layer priorities which this object occupies.
   */
  properties[PROP_HEIGHT] = g_param_spec_uint ("height", "Height",
      "The span of priorities this object occupies", 0, G_MAXUINT, 1,
      G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_HEIGHT,
      properties[PROP_HEIGHT]);

  /**
   * GESClip:supported-formats:
   *
   * The formats supported by the object.
   *
   * Since: 0.10.XX
   */
  properties[PROP_SUPPORTED_FORMATS] = g_param_spec_flags ("supported-formats",
      "Supported formats", "Formats supported by the file",
      GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_AUDIO | GES_TRACK_TYPE_VIDEO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_SUPPORTED_FORMATS,
      properties[PROP_SUPPORTED_FORMATS]);

  /**
   * GESClip:layer:
   *
   * The GESTimelineLayer where this object is being used.
   */
  properties[PROP_LAYER] = g_param_spec_object ("layer", "Layer",
      "The GESTimelineLayer where this object is being used.",
      GES_TYPE_TIMELINE_LAYER, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_LAYER,
      properties[PROP_LAYER]);

  /**
   * GESClip::effect-added:
   * @object: the #GESClip
   * @effect: the #GESBaseEffect that was added.
   *
   * Will be emitted after an effect was added to the object.
   *
   * Since: 0.10.2
   */
  ges_clip_signals[EFFECT_ADDED] =
      g_signal_new ("effect-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_BASE_EFFECT);

  /**
   * GESClip::effect-removed:
   * @object: the #GESClip
   * @effect: the #GESBaseEffect that was added.
   *
   * Will be emitted after an effect was remove from the object.
   *
   * Since: 0.10.2
   */
  ges_clip_signals[EFFECT_REMOVED] =
      g_signal_new ("effect-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_BASE_EFFECT);

  /**
   * GESClip::track-element-added:
   * @object: the #GESClip
   * @trackelement: the #GESTrackElement that was added.
   *
   * Will be emitted after a track element was added to the object.
   *
   * Since: 0.10.2
   */
  ges_clip_signals[TRACK_ELEMENT_ADDED] =
      g_signal_new ("track-element-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_ELEMENT);

  /**
   * GESClip::track-element-removed:
   * @object: the #GESClip
   * @trackelement: the #GESTrackElement that was removed.
   *
   * Will be emitted after a track element was removed from @object.
   *
   * Since: 0.10.2
   */
  ges_clip_signals[TRACK_ELEMENT_REMOVED] =
      g_signal_new ("track-element-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_ELEMENT);


  element_class->set_start = _set_start;
  element_class->set_duration = _set_duration;
  element_class->set_inpoint = _set_inpoint;
  element_class->set_priority = _set_priority;

  element_class->ripple = _ripple;
  element_class->ripple_end = _ripple_end;
  element_class->roll_start = _roll_start;
  element_class->roll_end = _roll_end;
  element_class->trim = _trim;
  element_class->set_max_duration = _set_max_duration;
/* TODO implement the deep_copy Virtual method */

  klass->need_fill_track = TRUE;
  klass->snaps = FALSE;
}

static void
ges_clip_init (GESClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_CLIP, GESClipPrivate);
  /* FIXME, check why it was done this way _DURATION (self) = GST_SECOND; */
  self->height = 1;
  self->trackelements = NULL;
  self->priv->layer = NULL;
  self->priv->nb_effects = 0;
  self->priv->is_moving = FALSE;
}

/**
 * ges_clip_create_track_element:
 * @object: The origin #GESClip
 * @type: The #GESTrackType to create a #GESTrackElement for.
 *
 * Creates a #GESTrackElement for the provided @type. The clip
 * keep a reference to the newly created trackelement, you therefore need to
 * call @ges_clip_release_track_element when you are done with it.
 *
 * Returns: (transfer none): A #GESTrackElement. Returns NULL if the #GESTrackElement could not
 * be created.
 */
GESTrackElement *
ges_clip_create_track_element (GESClip * object, GESTrackType type)
{
  GESClipClass *class;
  GESTrackElement *res;

  g_return_val_if_fail (GES_IS_CLIP (object), NULL);

  GST_DEBUG_OBJECT (object, "Creating track element for %s",
      ges_track_type_name (type));
  if (!(type & object->priv->supportedformats)) {
    GST_DEBUG_OBJECT (object, "We don't support this track type %i", type);
    return NULL;
  }

  class = GES_CLIP_GET_CLASS (object);

  if (G_UNLIKELY (class->create_track_element == NULL)) {
    GST_ERROR ("No 'create_track_element' implementation available fo type %s",
        G_OBJECT_TYPE_NAME (object));
    return NULL;
  }

  res = class->create_track_element (object, type);
  return res;

}

/**
 * ges_clip_create_track_elements:
 * @object: The origin #GESClip
 * @type: The #GESTrackType to create each #GESTrackElement for.
 *
 * Creates all #GESTrackElements supported by this object for the track type.
 *
 * Returns: (element-type GESTrackElement) (transfer full): A #GList of
 * newly created #GESTrackElement-s
 */

GList *
ges_clip_create_track_elements (GESClip * object, GESTrackType type)
{
  GESClipClass *klass;

  g_return_val_if_fail (GES_IS_CLIP (object), NULL);

  klass = GES_CLIP_GET_CLASS (object);

  if (!(klass->create_track_elements)) {
    GST_WARNING ("no GESClip::create_track_elements implentation");
    return NULL;
  }

  GST_DEBUG_OBJECT (object, "Creating TrackElements for type: %s",
      ges_track_type_name (type));
  return klass->create_track_elements (object, type);
}

gboolean
_set_max_duration (GESTimelineElement * element, GstClockTime maxduration)
{
  GList *tmp;

  for (tmp = GES_CLIP (element)->trackelements; tmp; tmp = g_list_next (tmp))
    ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (tmp->data),
        maxduration);

  return TRUE;
}

/*
 * default implementation of GESClipClass::create_track_elements
 */
GList *
ges_clip_create_track_elements_func (GESClip * object, GESTrackType type)
{
  GESTrackElement *result;

  GST_DEBUG_OBJECT (object, "Creating trackelement for track: %s",
      ges_track_type_name (type));
  result = ges_clip_create_track_element (object, type);
  if (!result) {
    GST_DEBUG ("Did not create track element");
    return NULL;
  }

  return g_list_append (NULL, result);
}

/**
 * ges_clip_add_track_element:
 * @object: a #GESClip
 * @trobj: the GESTrackElement
 *
 * Add a track element to the clip. Should only be called by
 * subclasses implementing the create_track_elements (plural) vmethod.
 *
 * Takes a reference on @trobj.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */

gboolean
ges_clip_add_track_element (GESClip * object, GESTrackElement * trobj)
{
  GList *tmp;
  gboolean is_effect;
  ObjectMapping *mapping;
  guint max_prio, min_prio;
  GESClipClass *klass;
  GESClipPrivate *priv;

  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (trobj), FALSE);

  priv = object->priv;
  is_effect = GES_IS_BASE_EFFECT (trobj);

  GST_LOG ("Got a TrackElement : %p , setting the clip as its"
      "creator. Is a BaseEffect %i", trobj, is_effect);

  if (!trobj)
    return FALSE;

  ges_track_element_set_clip (trobj, object);

  g_object_ref (trobj);

  mapping = g_slice_new0 (ObjectMapping);
  mapping->object = trobj;
  priv->mappings = g_list_append (priv->mappings, mapping);

  GST_DEBUG ("Adding TrackElement to the list of controlled track elements");
  /* We steal the initial reference */

  GST_DEBUG ("Setting properties on newly created TrackElement");

  mapping->priority_offset = priv->nb_effects;

  /* If the trackelement is an effect:
   *  - We add it on top of the list of BaseEffect
   *  - We put all TrackElement present in the Clip
   *    which are not BaseEffect on top of them
   *
   * FIXME: Let the full control over priorities to the user
   */
  if (is_effect) {
    GST_DEBUG
        ("Moving non on top effect under other TrackElement-s, nb effects %i",
        priv->nb_effects);
    for (tmp = g_list_nth (object->trackelements, priv->nb_effects); tmp;
        tmp = tmp->next) {
      GESTrackElement *tmpo = GES_TRACK_ELEMENT (tmp->data);

      /* We make sure not to move the entire #Clip */
      ges_track_element_set_locked (tmpo, FALSE);
      _set_priority0 (GES_TIMELINE_ELEMENT (tmpo), _PRIORITY (tmpo) + 1);
      ges_track_element_set_locked (tmpo, TRUE);
    }

    priv->nb_effects++;
  }

  object->trackelements =
      g_list_insert_sorted_with_data (object->trackelements, trobj,
      (GCompareDataFunc) sort_base_effects, object);

  _set_start0 (GES_TIMELINE_ELEMENT (trobj), _START (object));
  _set_duration0 (GES_TIMELINE_ELEMENT (trobj), _DURATION (object));
  _set_inpoint0 (GES_TIMELINE_ELEMENT (trobj), _INPOINT (object));
  ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (trobj),
      _MAXDURATION (object));

  klass = GES_CLIP_GET_CLASS (object);
  if (klass->track_element_added) {
    GST_DEBUG ("Calling track_element_added subclass method");
    klass->track_element_added (object, trobj);
  } else {
    GST_DEBUG ("%s doesn't have any track_element_added vfunc implementation",
        G_OBJECT_CLASS_NAME (klass));
  }

  /* Listen to all property changes */
  mapping->start_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::start",
      G_CALLBACK (track_element_start_changed_cb), object);
  mapping->duration_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::duration",
      G_CALLBACK (track_element_duration_changed_cb), object);
  mapping->inpoint_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::inpoint",
      G_CALLBACK (track_element_inpoint_changed_cb), object);
  mapping->priority_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::priority",
      G_CALLBACK (track_element_priority_changed_cb), object);

  get_layer_priorities (priv->layer, &min_prio, &max_prio);
  _set_priority0 (GES_TIMELINE_ELEMENT (trobj), min_prio +
      _PRIORITY (object) + mapping->priority_offset);

  GST_DEBUG ("Returning trobj:%p", trobj);
  if (!GES_IS_BASE_EFFECT (trobj)) {
    g_signal_emit (object, ges_clip_signals[TRACK_ELEMENT_ADDED], 0,
        GES_TRACK_ELEMENT (trobj));
  } else {
    /* emit 'effect-added' */
    g_signal_emit (object, ges_clip_signals[EFFECT_ADDED], 0,
        GES_BASE_EFFECT (trobj));
  }

  return TRUE;
}

/**
 * ges_clip_release_track_element:
 * @object: a #GESClip
 * @trackelement: the #GESTrackElement to release
 *
 * Release the @trackelement from the control of @object.
 *
 * Returns: %TRUE if the @trackelement was properly released, else %FALSE.
 */
gboolean
ges_clip_release_track_element (GESClip * object,
    GESTrackElement * trackelement)
{
  GList *tmp;
  ObjectMapping *mapping = NULL;
  GESClipClass *klass;

  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (trackelement), FALSE);

  GST_DEBUG ("object:%p, trackelement:%p", object, trackelement);
  klass = GES_CLIP_GET_CLASS (object);

  if (!(g_list_find (object->trackelements, trackelement))) {
    GST_WARNING ("TrackElement isn't controlled by this object");
    return FALSE;
  }

  for (tmp = object->priv->mappings; tmp; tmp = tmp->next) {
    mapping = (ObjectMapping *) tmp->data;
    if (mapping->object == trackelement)
      break;
  }

  if (tmp && mapping) {

    /* Disconnect all notify listeners */
    g_signal_handler_disconnect (trackelement, mapping->start_notifyid);
    g_signal_handler_disconnect (trackelement, mapping->duration_notifyid);
    g_signal_handler_disconnect (trackelement, mapping->inpoint_notifyid);
    g_signal_handler_disconnect (trackelement, mapping->priority_notifyid);

    g_slice_free (ObjectMapping, mapping);

    object->priv->mappings = g_list_delete_link (object->priv->mappings, tmp);
  }

  object->trackelements = g_list_remove (object->trackelements, trackelement);

  if (GES_IS_BASE_EFFECT (trackelement)) {
    /* emit 'clip-removed' */
    object->priv->nb_effects--;
    g_signal_emit (object, ges_clip_signals[EFFECT_REMOVED], 0,
        GES_BASE_EFFECT (trackelement));
  } else
    g_signal_emit (object, ges_clip_signals[TRACK_ELEMENT_REMOVED], 0,
        GES_TRACK_ELEMENT (trackelement));

  ges_track_element_set_clip (trackelement, NULL);

  GST_DEBUG ("Removing reference to track element %p", trackelement);

  if (klass->track_element_released) {
    GST_DEBUG ("Calling track_element_released subclass method");
    klass->track_element_released (object, trackelement);
  }

  g_object_unref (trackelement);

  /* FIXME : resync properties ? */

  return TRUE;
}

void
ges_clip_set_layer (GESClip * object, GESTimelineLayer * layer)
{
  GST_DEBUG ("object:%p, layer:%p", object, layer);

  object->priv->layer = layer;
}

gboolean
ges_clip_fill_track_element (GESClip * object,
    GESTrackElement * trackelement, GstElement * gnlobj)
{
  GESClipClass *class;
  gboolean res = TRUE;

  GST_DEBUG ("object:%p, trackelement:%p, gnlobject:%p",
      object, trackelement, gnlobj);

  class = GES_CLIP_GET_CLASS (object);

  if (class->need_fill_track) {
    if (G_UNLIKELY (class->fill_track_element == NULL)) {
      GST_WARNING ("No 'fill_track_element' implementation available");
      return FALSE;
    }

    res = class->fill_track_element (object, trackelement, gnlobj);
  }

  GST_DEBUG ("Returning res:%d", res);

  return res;
}

gboolean
ges_clip_fill_track_element_func (GESClip * object,
    GESTrackElement * trackelement, GstElement * gnlobj)
{
  GST_WARNING ("No 'fill_track_element' implementation !");

  return FALSE;
}

static ObjectMapping *
find_object_mapping (GESClip * object, GESTrackElement * child)
{
  GList *tmp;

  for (tmp = object->priv->mappings; tmp; tmp = tmp->next) {
    ObjectMapping *map = (ObjectMapping *) tmp->data;
    if (map->object == child)
      return map;
  }

  return NULL;
}

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp;
  GESTrackElement *tr;
  ObjectMapping *map;
  gboolean snap = FALSE;
  GESTimeline *timeline = NULL;

  GESClip *object = GES_CLIP (element);
  GESClipPrivate *priv = object->priv;

  /* If the class has snapping enabled and the object is in a timeline,
   * we snap */
  if (priv->layer && GES_CLIP_GET_CLASS (object)->snaps)
    timeline = ges_timeline_layer_get_timeline (object->priv->layer);
  snap = timeline && priv->initiated_move == NULL ? TRUE : FALSE;

  object->priv->ignore_notifies = TRUE;

  for (tmp = object->trackelements; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackElement *) tmp->data;
    map = find_object_mapping (object, tr);

    if (ges_track_element_is_locked (tr) && tr != object->priv->initiated_move) {
      gint64 new_start = start - map->start_offset;

      /* Move the child... */
      if (new_start < 0) {
        GST_ERROR ("Trying to set start to a negative value %" GST_TIME_FORMAT,
            GST_TIME_ARGS (-(start + map->start_offset)));
        continue;
      }

      /* Make the snapping happen if in a timeline */
      if (snap)
        ges_timeline_move_object_simple (timeline, tr, NULL, GES_EDGE_NONE,
            start);
      else
        _set_start0 (GES_TIMELINE_ELEMENT (tr), start);
    } else {
      /* ... or update the offset */
      map->start_offset = start - _START (tr);
    }
  }

  object->priv->ignore_notifies = FALSE;

  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  GList *tmp;
  GESTrackElement *tr;

  GESClip *object = GES_CLIP (element);

  for (tmp = object->trackelements; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackElement *) tmp->data;

    if (ges_track_element_is_locked (tr))
      /* call set_inpoint on each trackelement */
      _set_inpoint0 (GES_TIMELINE_ELEMENT (tr), inpoint);
  }

  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GList *tmp;
  GESTrackElement *tr;
  GESTimeline *timeline = NULL;
  gboolean snap = FALSE;

  GESClip *object = GES_CLIP (element);
  GESClipPrivate *priv = object->priv;

  if (priv->layer && GES_CLIP_GET_CLASS (object)->snaps)
    timeline = ges_timeline_layer_get_timeline (object->priv->layer);

  /* If the class has snapping enabled, the object is in a timeline,
   * and we are not following a moved TrackElement, we snap */
  snap = timeline && priv->initiated_move == NULL ? TRUE : FALSE;

  object->priv->ignore_notifies = TRUE;
  for (tmp = object->trackelements; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackElement *) tmp->data;

    if (ges_track_element_is_locked (tr)) {
      /* call set_duration on each trackelement
       * and make the snapping happen if in a timeline */
      if (G_LIKELY (snap))
        ges_timeline_trim_object_simple (timeline, tr, NULL, GES_EDGE_END,
            _START (tr) + duration, TRUE);
      else
        _set_duration0 (GES_TIMELINE_ELEMENT (tr), duration);
    }
  }
  object->priv->ignore_notifies = FALSE;

  return TRUE;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GList *tmp;
  GESTrackElement *tr;
  ObjectMapping *map;
  GESClipPrivate *priv;
  guint32 layer_min_gnl_prio, layer_max_gnl_prio;

  GESClip *object = GES_CLIP (element);

  priv = object->priv;

  get_layer_priorities (priv->layer, &layer_min_gnl_prio, &layer_max_gnl_prio);

  priv->ignore_notifies = TRUE;
  for (tmp = object->trackelements; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackElement *) tmp->data;
    map = find_object_mapping (object, tr);

    if (ges_track_element_is_locked (tr)) {
      guint32 real_tck_prio;

      /* Move the child... */
      real_tck_prio = layer_min_gnl_prio + priority + map->priority_offset;

      if (real_tck_prio > layer_max_gnl_prio) {
        GST_WARNING ("%p priority of %i, is outside of the its containing "
            "layer space. (%d/%d) setting it to the maximum it can be", object,
            priority, layer_min_gnl_prio, layer_max_gnl_prio);

        real_tck_prio = layer_max_gnl_prio;
      }

      _set_priority0 (GES_TIMELINE_ELEMENT (tr), real_tck_prio);

    } else {
      /* ... or update the offset */
      map->priority_offset = _PRIORITY (tr) - layer_min_gnl_prio + priority;
    }
  }

  object->trackelements = g_list_sort_with_data (object->trackelements,
      (GCompareDataFunc) sort_base_effects, object);
  priv->ignore_notifies = FALSE;

  return TRUE;
}

/**
 * ges_clip_set_moving_from_layer:
 * @object: a #GESClip
 * @is_moving: %TRUE if you want to start moving @object to another layer
 * %FALSE when you finished moving it.
 *
 * Sets the object in a moving to layer state. You might rather use the
 * ges_clip_move_to_layer function to move #GESClip-s
 * from a layer to another.
 **/
void
ges_clip_set_moving_from_layer (GESClip * object, gboolean is_moving)
{
  g_return_if_fail (GES_IS_CLIP (object));

  object->priv->is_moving = is_moving;
}

/**
 * ges_clip_is_moving_from_layer:
 * @object: a #GESClip
 *
 * Tells you if the object is currently moving from a layer to another.
 * You might rather use the ges_clip_move_to_layer function to
 * move #GESClip-s from a layer to another.
 *
 *
 * Returns: %TRUE if @object is currently moving from its current layer
 * %FALSE otherwize
 **/
gboolean
ges_clip_is_moving_from_layer (GESClip * object)
{
  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);

  return object->priv->is_moving;
}

/**
 * ges_clip_move_to_layer:
 * @object: a #GESClip
 * @layer: the new #GESTimelineLayer
 *
 * Moves @object to @layer. If @object is not in any layer, it adds it to
 * @layer, else, it removes it from its current layer, and adds it to @layer.
 *
 * Returns: %TRUE if @object could be moved %FALSE otherwize
 */
gboolean
ges_clip_move_to_layer (GESClip * object, GESTimelineLayer * layer)
{
  gboolean ret;
  GESTimelineLayer *current_layer;

  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);

  current_layer = object->priv->layer;

  if (current_layer == NULL) {
    GST_DEBUG ("Not moving %p, only adding it to %p", object, layer);

    return ges_timeline_layer_add_clip (layer, object);
  }

  GST_DEBUG_OBJECT (object, "moving to layer %p, priority: %d", layer,
      ges_timeline_layer_get_priority (layer));

  object->priv->is_moving = TRUE;
  g_object_ref (object);
  ret = ges_timeline_layer_remove_clip (current_layer, object);

  if (!ret) {
    g_object_unref (object);
    return FALSE;
  }

  ret = ges_timeline_layer_add_clip (layer, object);
  object->priv->is_moving = FALSE;

  g_object_unref (object);

  return ret;
}

/**
 * ges_clip_find_track_element:
 * @object: a #GESClip
 * @track: a #GESTrack or NULL
 * @type: a #GType indicating the type of track element you are looking
 * for or %G_TYPE_NONE if you do not care about the track type.
 *
 * Finds the #GESTrackElement controlled by @object that is used in @track. You
 * may optionally specify a GType to further narrow search criteria.
 *
 * Note: If many objects match, then the one with the highest priority will be
 * returned.
 *
 * Returns: (transfer full): The #GESTrackElement used by @track, else %NULL.
 * Unref after usage.
 */

GESTrackElement *
ges_clip_find_track_element (GESClip * object, GESTrack * track, GType type)
{
  GESTrackElement *ret = NULL;
  GList *tmp;
  GESTrackElement *otmp;

  g_return_val_if_fail (GES_IS_CLIP (object), NULL);
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);

  for (tmp = object->trackelements; tmp; tmp = g_list_next (tmp)) {
    otmp = (GESTrackElement *) tmp->data;

    if (ges_track_element_get_track (otmp) == track) {
      if ((type != G_TYPE_NONE) &&
          !G_TYPE_CHECK_INSTANCE_TYPE (tmp->data, type))
        continue;

      ret = GES_TRACK_ELEMENT (tmp->data);
      g_object_ref (ret);
      break;
    }
  }

  return ret;
}

/**
 * ges_clip_get_layer:
 * @object: a #GESClip
 *
 * Get the #GESTimelineLayer to which this object belongs.
 *
 * Returns: (transfer full): The #GESTimelineLayer where this @object is being
 * used, or %NULL if it is not used on any layer. The caller should unref it
 * usage.
 */
GESTimelineLayer *
ges_clip_get_layer (GESClip * object)
{
  g_return_val_if_fail (GES_IS_CLIP (object), NULL);

  if (object->priv->layer != NULL)
    g_object_ref (G_OBJECT (object->priv->layer));

  return object->priv->layer;
}

/**
 * ges_clip_get_track_elements:
 * @object: a #GESClip
 *
 * Get the list of #GESTrackElement contained in @object
 *
 * Returns: (transfer full) (element-type GESTrackElement): The list of
 * trackelement contained in @object.
 * The user is responsible for unreffing the contained objects
 * and freeing the list.
 */
GList *
ges_clip_get_track_elements (GESClip * object)
{
  GList *ret;
  GList *tmp;

  g_return_val_if_fail (GES_IS_CLIP (object), NULL);

  ret = g_list_copy (object->trackelements);

  for (tmp = ret; tmp; tmp = tmp->next) {
    g_object_ref (tmp->data);
  }

  return ret;
}

static gint
sort_base_effects (gpointer a, gpointer b, GESClip * object)
{
  guint prio_offset_a, prio_offset_b;
  ObjectMapping *map_a, *map_b;
  GESTrackElement *obj_a, *obj_b;

  obj_a = GES_TRACK_ELEMENT (a);
  obj_b = GES_TRACK_ELEMENT (b);

  map_a = find_object_mapping (object, obj_a);
  map_b = find_object_mapping (object, obj_b);

  prio_offset_a = map_a->priority_offset;
  prio_offset_b = map_b->priority_offset;

  if ((gint) prio_offset_a > (guint) prio_offset_b)
    return 1;
  if ((guint) prio_offset_a < (guint) prio_offset_b)
    return -1;

  return 0;
}

/**
 * ges_clip_get_top_effects:
 * @object: The origin #GESClip
 *
 * Get effects applied on @object
 *
 * Returns: (transfer full) (element-type GESTrackElement): a #GList of the
 * #GESBaseEffect that are applied on @object order by ascendant priorities.
 * The refcount of the objects will be increased. The user will have to
 * unref each #GESBaseEffect and free the #GList.
 *
 * Since: 0.10.2
 */
GList *
ges_clip_get_top_effects (GESClip * object)
{
  GList *tmp, *ret;
  guint i;

  g_return_val_if_fail (GES_IS_CLIP (object), NULL);

  GST_DEBUG_OBJECT (object, "Getting the %i top effects",
      object->priv->nb_effects);
  ret = NULL;

  for (tmp = object->trackelements, i = 0; i < object->priv->nb_effects;
      tmp = tmp->next, i++) {
    ret = g_list_append (ret, g_object_ref (tmp->data));
  }

  return ret;
}

/**
 * ges_clip_get_top_effect_position:
 * @object: The origin #GESClip
 * @effect: The #GESBaseEffect we want to get the top position from
 *
 * Gets the top position of an effect.
 *
 * Returns: The top position of the effect, -1 if something went wrong.
 *
 * Since: 0.10.2
 */
gint
ges_clip_get_top_effect_position (GESClip * object, GESBaseEffect * effect)
{
  g_return_val_if_fail (GES_IS_CLIP (object), -1);

  return find_object_mapping (object,
      GES_TRACK_ELEMENT (effect))->priority_offset;
}

/**
 * ges_clip_set_top_effect_priority:
 * @object: The origin #GESClip
 * @effect: The #GESBaseEffect to move
 * @newpriority: the new position at which to move the @effect inside this
 * #GESClip
 *
 * This is a convenience method that lets you set the priority of a top effect.
 *
 * Returns: %TRUE if @effect was successfuly moved, %FALSE otherwise.
 *
 * Since: 0.10.2
 */
gboolean
ges_clip_set_top_effect_priority (GESClip * object,
    GESBaseEffect * effect, guint newpriority)
{
  gint inc;
  GList *tmp;
  guint current_prio;
  GESTrackElement *track_element;

  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);

  track_element = GES_TRACK_ELEMENT (effect);
  current_prio = _PRIORITY (track_element);

  /*  We don't change the priority */
  if (current_prio == newpriority ||
      (G_UNLIKELY (ges_track_element_get_clip (track_element) != object)))
    return FALSE;

  if (newpriority > (object->priv->nb_effects - 1)) {
    GST_DEBUG ("You are trying to make %p not a top effect", effect);
    return FALSE;
  }

  if (current_prio > object->priv->nb_effects) {
    GST_DEBUG ("%p is not a top effect", effect);
    return FALSE;
  }

  if (_PRIORITY (track_element) < newpriority)
    inc = -1;
  else
    inc = +1;

  _set_priority0 (GES_TIMELINE_ELEMENT (track_element), newpriority);
  for (tmp = object->trackelements; tmp; tmp = tmp->next) {
    GESTrackElement *tmpo = GES_TRACK_ELEMENT (tmp->data);
    guint tck_priority = _PRIORITY (tmpo);

    if ((inc == +1 && tck_priority >= newpriority) ||
        (inc == -1 && tck_priority <= newpriority)) {
      _set_priority0 (GES_TIMELINE_ELEMENT (tmpo), tck_priority + inc);
    }
  }

  object->trackelements = g_list_sort_with_data (object->trackelements,
      (GCompareDataFunc) sort_base_effects, object);

  return TRUE;
}

/**
 * ges_clip_edit:
 * @object: the #GESClip to edit
 * @layers: (element-type GESTimelineLayer): The layers you want the edit to
 *  happen in, %NULL means that the edition is done in all the
 *  #GESTimelineLayers contained in the current timeline.
 * @new_layer_priority: The priority of the layer @object should land in.
 *  If the layer you're trying to move the object to doesn't exist, it will
 *  be created automatically. -1 means no move.
 * @mode: The #GESEditMode in which the editition will happen.
 * @edge: The #GESEdge the edit should happen on.
 * @position: The position at which to edit @object (in nanosecond)
 *
 * Edit @object in the different exisiting #GESEditMode modes. In the case of
 * slide, and roll, you need to specify a #GESEdge
 *
 * Returns: %TRUE if the object as been edited properly, %FALSE if an error
 * occured
 *
 * Since: 0.10.XX
 */
gboolean
ges_clip_edit (GESClip * object, GList * layers,
    gint new_layer_priority, GESEditMode mode, GESEdge edge, guint64 position)
{
  GList *tmp;
  gboolean ret = TRUE;
  GESTimelineLayer *layer;

  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);

  if (!G_UNLIKELY (object->trackelements)) {
    GST_WARNING_OBJECT (object, "Trying to edit, but not containing"
        "any TrackElement yet.");
    return FALSE;
  }

  for (tmp = object->trackelements; tmp; tmp = g_list_next (tmp)) {
    if (ges_track_element_is_locked (tmp->data)
        && GES_IS_SOURCE (tmp->data)) {
      ret &= ges_track_element_edit (tmp->data, layers, mode, edge, position);
      break;
    }
  }

  /* Moving to layer */
  if (new_layer_priority == -1) {
    GST_DEBUG_OBJECT (object, "Not moving new prio %d", new_layer_priority);
  } else {
    gint priority_offset;

    layer = object->priv->layer;
    if (layer == NULL) {
      GST_WARNING_OBJECT (object, "Not in any layer yet, not moving");

      return FALSE;
    }
    priority_offset = new_layer_priority -
        ges_timeline_layer_get_priority (layer);

    ret &= timeline_context_to_layer (layer->timeline, priority_offset);
  }

  return ret;
}

/**
 * ges_clip_split:
 * @object: the #GESClip to split
 * @position: a #GstClockTime representing the position at which to split
 *
 * The function modifies @object, and creates another #GESClip so
 * we have two clips at the end, splitted at the time specified by @position.
 *
 * Returns: (transfer floating): The newly created #GESClip resulting from the splitting
 */
GESClip *
ges_clip_split (GESClip * object, guint64 position)
{
  GList *tmp;
  gboolean locked;
  GESClip *new_object;

  GstClockTime start, inpoint, duration;

  g_return_val_if_fail (GES_IS_CLIP (object), NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (position), NULL);

  duration = _DURATION (object);
  start = _START (object);
  inpoint = _INPOINT (object);

  if (position >= start + duration || position <= start) {
    GST_WARNING_OBJECT (object, "Can not split %" GST_TIME_FORMAT
        " out of boundaries", GST_TIME_ARGS (position));
    return NULL;
  }

  GST_DEBUG_OBJECT (object, "Spliting at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));

  /* Create the new Clip */
  new_object =
      GES_CLIP (ges_timeline_element_copy (GES_TIMELINE_ELEMENT
          (object), FALSE));

  /* Set new timing properties on the Clip */
  _set_start0 (GES_TIMELINE_ELEMENT (new_object), position);
  _set_inpoint0 (GES_TIMELINE_ELEMENT (new_object),
      _INPOINT (object) + duration - (duration + start - position));
  _set_duration0 (GES_TIMELINE_ELEMENT (new_object),
      duration + start - position);

  if (object->priv->layer) {
    /* We do not want the timeline to create again TrackElement-s */
    ges_clip_set_moving_from_layer (new_object, TRUE);
    ges_timeline_layer_add_clip (object->priv->layer, new_object);
    ges_clip_set_moving_from_layer (new_object, FALSE);
  }

  /* We first set the new duration and the child mapping will be updated
   * properly in the following loop
   * FIXME: Avoid setting it oureself reworking the API */
  GES_TIMELINE_ELEMENT (object)->duration = position - _START (object);
  for (tmp = object->trackelements; tmp; tmp = tmp->next) {
    GESTrack *track;

    GESTrackElement *new_trackelement, *trackelement =
        GES_TRACK_ELEMENT (tmp->data);

    duration = _DURATION (trackelement);
    start = _START (trackelement);
    inpoint = _INPOINT (trackelement);

    if (position <= start || position >= (start + duration)) {
      GST_DEBUG_OBJECT (trackelement,
          "Outside %" GST_TIME_FORMAT "the boundaries "
          "not copying it ( start %" GST_TIME_FORMAT ", end %" GST_TIME_FORMAT
          ")", GST_TIME_ARGS (position), GST_TIME_ARGS (_START (trackelement)),
          GST_TIME_ARGS (_START (trackelement) + _DURATION (trackelement)));
      continue;
    }

    new_trackelement =
        GES_TRACK_ELEMENT (ges_timeline_element_copy (GES_TIMELINE_ELEMENT
            (trackelement), TRUE));
    if (new_trackelement == NULL) {
      GST_WARNING_OBJECT (trackelement, "Could not create a copy");
      continue;
    }

    ges_clip_add_track_element (new_object, new_trackelement);

    track = ges_track_element_get_track (trackelement);
    if (track == NULL)
      GST_DEBUG_OBJECT (trackelement, "Was not in a track, not adding %p to"
          "any track", new_trackelement);
    else
      ges_track_add_element (track, new_trackelement);

    /* Unlock TrackElement-s as we do not want the container to move
     * syncronously */
    locked = ges_track_element_is_locked (trackelement);
    ges_track_element_set_locked (new_trackelement, FALSE);
    ges_track_element_set_locked (trackelement, FALSE);

    /* Set 'new' track element timing propeties */
    _set_start0 (GES_TIMELINE_ELEMENT (new_trackelement), position);
    _set_inpoint0 (GES_TIMELINE_ELEMENT (new_trackelement),
        inpoint + duration - (duration + start - position));
    _set_duration0 (GES_TIMELINE_ELEMENT (new_trackelement),
        duration + start - position);

    /* Set 'old' track element duration */
    _set_duration0 (GES_TIMELINE_ELEMENT (trackelement), position - start);

    /* And let track elements in the same locking state as before. */
    ges_track_element_set_locked (trackelement, locked);
    ges_track_element_set_locked (new_trackelement, locked);
  }

  return new_object;
}

/**
 * ges_clip_set_supported_formats:
 * @object: the #GESClip to set supported formats on
 * @supportedformats: the #GESTrackType defining formats supported by @object
 *
 * Sets the formats supported by the file.
 *
 * Since: 0.10.XX
 */
void
ges_clip_set_supported_formats (GESClip * object, GESTrackType supportedformats)
{
  g_return_if_fail (GES_IS_CLIP (object));

  object->priv->supportedformats = supportedformats;
}

/**
 * ges_clip_get_supported_formats:
 * @object: the #GESClip
 *
 * Get the formats supported by @object.
 *
 * Returns: The formats supported by @object.
 *
 * Since: 0.10.XX
 */
GESTrackType
ges_clip_get_supported_formats (GESClip * object)
{
  g_return_val_if_fail (GES_IS_CLIP (object), GES_TRACK_TYPE_UNKNOWN);

  return object->priv->supportedformats;
}

/**
 * ges_clip_objects_set_locked:
 * @object: the #GESClip
 * @locked: whether the #GESTrackElement contained in @object are locked to it.
 *
 * Set the locking status of all the #GESTrackElement contained in @object to @locked.
 * See the ges_track_element_set_locked documentation for more details.
 *
 * Since: 0.10.XX
 */
void
ges_clip_objects_set_locked (GESClip * object, gboolean locked)
{
  GList *tmp;

  g_return_if_fail (GES_IS_CLIP (object));

  for (tmp = object->priv->mappings; tmp; tmp = g_list_next (tmp)) {
    ges_track_element_set_locked (((ObjectMapping *) tmp->data)->object,
        locked);
  }
}

gboolean
_ripple (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp, *trackelements;
  gboolean ret = TRUE;
  GESTimeline *timeline;
  GESClip *object = GES_CLIP (element);

  timeline = ges_timeline_layer_get_timeline (object->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = g_list_next (tmp)) {
    if (ges_track_element_is_locked (tmp->data)) {
      ret = timeline_ripple_object (timeline, GES_TRACK_ELEMENT (tmp->data),
          NULL, GES_EDGE_NONE, start);
      /* As we work only with locked objects, the changes will be reflected
       * to others controlled TrackElements */
      break;
    }
  }
  g_list_free_full (trackelements, g_object_unref);

  return ret;
}

static gboolean
_ripple_end (GESTimelineElement * element, GstClockTime end)
{
  GList *tmp, *trackelements;
  gboolean ret = TRUE;
  GESTimeline *timeline;
  GESClip *object = GES_CLIP (element);

  timeline = ges_timeline_layer_get_timeline (object->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = g_list_next (tmp)) {
    if (ges_track_element_is_locked (tmp->data)) {
      ret = timeline_ripple_object (timeline, GES_TRACK_ELEMENT (tmp->data),
          NULL, GES_EDGE_END, end);
      /* As we work only with locked objects, the changes will be reflected
       * to others controlled TrackElements */
      break;
    }
  }
  g_list_free_full (trackelements, g_object_unref);

  return ret;
}

gboolean
_roll_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp, *trackelements;
  gboolean ret = TRUE;
  GESTimeline *timeline;

  GESClip *object = GES_CLIP (element);

  timeline = ges_timeline_layer_get_timeline (object->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = g_list_next (tmp)) {
    if (ges_track_element_is_locked (tmp->data)) {
      ret = timeline_roll_object (timeline, GES_TRACK_ELEMENT (tmp->data),
          NULL, GES_EDGE_START, start);
      /* As we work only with locked objects, the changes will be reflected
       * to others controlled TrackElements */
      break;
    }
  }
  g_list_free_full (trackelements, g_object_unref);

  return ret;
}

gboolean
_roll_end (GESTimelineElement * element, GstClockTime end)
{
  GList *tmp, *trackelements;
  gboolean ret = TRUE;
  GESTimeline *timeline;

  GESClip *object = GES_CLIP (element);

  timeline = ges_timeline_layer_get_timeline (object->priv->layer);
  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }


  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = g_list_next (tmp)) {
    if (ges_track_element_is_locked (tmp->data)) {
      ret = timeline_roll_object (timeline, GES_TRACK_ELEMENT (tmp->data),
          NULL, GES_EDGE_END, end);
      /* As we work only with locked objects, the changes will be reflected
       * to others controlled TrackElements */
      break;
    }
  }
  g_list_free_full (trackelements, g_object_unref);

  return ret;
}

gboolean
_trim (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp, *trackelements;
  gboolean ret = TRUE;
  GESTimeline *timeline;

  GESClip *object = GES_CLIP (element);

  timeline = ges_timeline_layer_get_timeline (object->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  trackelements = ges_clip_get_track_elements (object);
  for (tmp = trackelements; tmp; tmp = g_list_next (tmp)) {
    if (ges_track_element_is_locked (tmp->data)) {
      ret = timeline_trim_object (timeline, GES_TRACK_ELEMENT (tmp->data),
          NULL, GES_EDGE_START, start);
      break;
    }
  }
  g_list_free_full (trackelements, g_object_unref);

  return ret;
}

/**
 * ges_clip_add_asset:
 * @object: a #GESClip
 * @asset: a #GESAsset with #GES_TYPE_TRACK_ELEMENT as extractable_type
 *
 * Extracts a #GESTrackElement from @asset and adds it to the @object.
 * Should only be called in order to add operations to a #GESClip,
 * ni other cases TrackElement are added automatically when adding the
 * #GESClip/#GESAsset to a layer.
 *
 * Takes a reference on @trobj.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */

gboolean
ges_clip_add_asset (GESClip * object, GESAsset * asset)
{
  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);
  g_return_val_if_fail (GES_IS_ASSET (asset), FALSE);
  g_return_val_if_fail (g_type_is_a (ges_asset_get_extractable_type
          (asset), GES_TYPE_TRACK_ELEMENT), FALSE);

  return ges_clip_add_track_element (object,
      GES_TRACK_ELEMENT (ges_asset_extract (asset, NULL)));
}

static void
update_height (GESClip * object)
{
  GList *tmp;
  guint32 min_prio = G_MAXUINT32, max_prio = 0;

  /* Go over all childs and check if height has changed */
  for (tmp = object->trackelements; tmp; tmp = tmp->next) {
    guint tck_priority = _PRIORITY (tmp->data);

    if (tck_priority < min_prio)
      min_prio = tck_priority;
    if (tck_priority > max_prio)
      max_prio = tck_priority;
  }

  /* FIXME : We only grow the height */
  if (object->height < (max_prio - min_prio + 1)) {
    object->height = max_prio - min_prio + 1;
    GST_DEBUG ("Updating height %i", object->height);
#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_HEIGHT]);
#else
    g_object_notify (G_OBJECT (object), "height");
#endif
  }
}

/*
 * PROPERTY NOTIFICATIONS FROM TRACK ELEMENTS
 */

static void
track_element_start_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object)
{
  ObjectMapping *map;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (object);

  if (object->priv->ignore_notifies)
    return;

  map = find_object_mapping (object, child);
  if (G_UNLIKELY (map == NULL))
    /* something massively screwed up if we get this */
    return;

  if (!ges_track_element_is_locked (child)) {
    /* Update the internal start_offset */
    map->start_offset = _START (element) - _START (child);
  } else {
    /* Or update the parent start */
    object->priv->initiated_move = child;
    _set_start0 (element, _START (child) + map->start_offset);
    object->priv->initiated_move = NULL;
  }
}

static void
track_element_inpoint_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object)
{
  ObjectMapping *map;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (object);

  if (object->priv->ignore_notifies)
    return;

  map = find_object_mapping (GES_CLIP (element), child);
  if (G_UNLIKELY (map == NULL))
    /* something massively screwed up if we get this */
    return;

  if (!ges_track_element_is_locked (child)) {
    /* Update the internal start_offset */
    map->inpoint_offset = _INPOINT (element) - _INPOINT (child);
  } else {
    /* Or update the parent start */
    object->priv->initiated_move = child;
    _set_inpoint0 (element, _INPOINT (child) + map->inpoint_offset);
    object->priv->initiated_move = NULL;
  }

}

static void
track_element_duration_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object)
{
  ObjectMapping *map;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (object);

  if (object->priv->ignore_notifies)
    return;

  map = find_object_mapping (object, child);
  if (G_UNLIKELY (map == NULL))
    /* something massively screwed up if we get this */
    return;

  if (!ges_track_element_is_locked (child)) {
    /* Update the internal start_offset */
    map->duration_offset = _DURATION (element) - _DURATION (child);
  } else {
    /* Or update the parent start */
    object->priv->initiated_move = child;
    _set_duration0 (element, _DURATION (child) + map->duration_offset);
    object->priv->initiated_move = NULL;
  }

}

static void
track_element_priority_changed_cb (GESTrackElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESClip * object)
{
  ObjectMapping *map;
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (object);
  guint32 layer_min_gnl_prio, layer_max_gnl_prio;

  guint tck_priority = _PRIORITY (child);

  GST_DEBUG ("TrackElement %p priority changed to %i", child,
      _PRIORITY (child));

  if (object->priv->ignore_notifies)
    return;

  update_height (object);
  map = find_object_mapping (object, child);
  get_layer_priorities (object->priv->layer, &layer_min_gnl_prio,
      &layer_max_gnl_prio);

  if (G_UNLIKELY (map == NULL))
    /* something massively screwed up if we get this */
    return;

  if (!ges_track_element_is_locked (child)) {
    if (tck_priority < layer_min_gnl_prio || tck_priority > layer_max_gnl_prio) {
      GST_WARNING ("%p priority of %i, is outside of its containing "
          "layer space. (%d/%d). This is a bug in the program.", element,
          tck_priority, layer_min_gnl_prio, layer_max_gnl_prio);
    }

    /* Update the internal priority_offset */
    map->priority_offset = tck_priority - (layer_min_gnl_prio +
        _PRIORITY (element));

  } else if (tck_priority < layer_min_gnl_prio + _PRIORITY (element)) {
    /* Or update the parent priority, the element priority is always the
     * highest priority (smaller number) */
    if (tck_priority < layer_min_gnl_prio || layer_max_gnl_prio < tck_priority) {

      GST_WARNING ("%p priority of %i, is outside of its containing "
          "layer space. (%d/%d). This is a bug in the program.", element,
          tck_priority, layer_min_gnl_prio, layer_max_gnl_prio);
      return;
    }

    _set_priority0 (element, tck_priority - layer_min_gnl_prio);
  }

  GST_DEBUG_OBJECT (element, "priority %d child %p priority %d",
      _PRIORITY (element), child, _PRIORITY (child));
}

static void
get_layer_priorities (GESTimelineLayer * layer, guint32 * layer_min_gnl_prio,
    guint32 * layer_max_gnl_prio)
{
  if (layer) {
    *layer_min_gnl_prio = layer->min_gnl_priority;
    *layer_max_gnl_prio = layer->max_gnl_priority;
  } else {
    *layer_min_gnl_prio = 0;
    *layer_max_gnl_prio = G_MAXUINT32;
  }
}
