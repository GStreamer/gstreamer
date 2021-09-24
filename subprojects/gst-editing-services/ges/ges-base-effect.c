/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <tsaunier@gnome.org>
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
 * SECTION:gesbaseeffect
 * @title: GESBaseEffect
 * @short_description: adds an effect to a stream in a GESSourceClip or a
 * GESLayer
 *
 * A #GESBaseEffect is some operation that applies an effect to the data
 * it receives.
 *
 * ## Time Effects
 *
 * Some operations will change the timing of the stream data they receive
 * in some way. In particular, the #GstElement that they wrap could alter
 * the times of the segment they receive in a #GST_EVENT_SEGMENT event,
 * or the times of a seek they receive in a #GST_EVENT_SEEK event. Such
 * operations would be considered time effects since they translate the
 * times they receive on their source to different times at their sink,
 * and vis versa. This introduces two sets of time coordinates for the
 * event: (internal) sink coordinates and (internal) source coordinates,
 * where segment times are translated from the sink coordinates to the
 * source coordinates, and seek times are translated from the source
 * coordinates to the sink coordinates.
 *
 * If you use such an effect in GES, you will need to inform GES of the
 * properties that control the timing with
 * ges_base_effect_register_time_property(), and the effect's timing
 * behaviour using ges_base_effect_set_time_translation_funcs().
 *
 * Note that a time effect should not have its
 * #GESTrackElement:has-internal-source set to %TRUE.
 *
 * In addition, note that GES only *fully* supports time effects whose
 * mapping from the source to sink coordinates (those applied to seeks)
 * obeys:
 *
 * + Maps the time `0` to `0`. So initial time-shifting effects are
 *   excluded.
 * + Is monotonically increasing. So reversing effects, and effects that
 *   jump backwards in the stream are excluded.
 * + Can handle a reasonable #GstClockTime, relative to the project. So
 *   this would exclude a time effect with an extremely large speed-up
 *   that would cause the converted #GstClockTime seeks to overflow.
 * + Is 'continuously reversible'. This essentially means that for every
 *   time in the sink coordinates, we can, to 'good enough' accuracy,
 *   calculate the corresponding time in the source coordinates. Moreover,
 *   this should correspond to how segment times are translated from
 *   sink to source.
 * + Only depends on the registered time properties, rather than the
 *   state of the #GstElement or the data it receives. This would exclude,
 *   say, an effect that would speedup if there is more red in the image
 *   it receives.
 *
 * Note that a constant-rate-change effect that is not extremely fast or
 * slow would satisfy these conditions. For such effects, you may wish to
 * use ges_effect_class_register_rate_property().
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gprintf.h>

#include "ges-utils.h"
#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-base-effect.h"

typedef struct _TimePropertyData
{
  gchar *property_name;
  GObject *child;
  GParamSpec *pspec;
} TimePropertyData;

static void
_time_property_data_free (gpointer data_p)
{
  TimePropertyData *data = data_p;
  g_free (data->property_name);
  gst_object_unref (data->child);
  g_param_spec_unref (data->pspec);
  g_free (data);
}

struct _GESBaseEffectPrivate
{
  GList *time_properties;
  GESBaseEffectTimeTranslationFunc source_to_sink;
  GESBaseEffectTimeTranslationFunc sink_to_source;
  gpointer translation_data;
  GDestroyNotify destroy_translation_data;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GESBaseEffect, ges_base_effect,
    GES_TYPE_OPERATION);

static gboolean
ges_base_effect_set_child_property_full (GESTimelineElement * element,
    GObject * child, GParamSpec * pspec, const GValue * value, GError ** error)
{
  GESClip *parent = GES_IS_CLIP (element->parent) ?
      GES_CLIP (element->parent) : NULL;

  if (parent && !ges_clip_can_set_time_property_of_child (parent,
          GES_TRACK_ELEMENT (element), child, pspec, value, error)) {
    GST_INFO_OBJECT (element, "Cannot set time property '%s::%s' "
        "because the parent clip %" GES_FORMAT " would not allow it",
        G_OBJECT_TYPE_NAME (child), pspec->name, GES_ARGS (parent));
    return FALSE;
  }

  return
      GES_TIMELINE_ELEMENT_CLASS
      (ges_base_effect_parent_class)->set_child_property_full (element, child,
      pspec, value, error);
}

static void
ges_base_effect_dispose (GObject * object)
{
  GESBaseEffectPrivate *priv = GES_BASE_EFFECT (object)->priv;

  g_list_free_full (priv->time_properties, _time_property_data_free);
  priv->time_properties = NULL;
  if (priv->destroy_translation_data)
    priv->destroy_translation_data (priv->translation_data);
  priv->destroy_translation_data = NULL;
  priv->source_to_sink = NULL;
  priv->sink_to_source = NULL;

  G_OBJECT_CLASS (ges_base_effect_parent_class)->dispose (object);
}

static void
ges_base_effect_class_init (GESBaseEffectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  object_class->dispose = ges_base_effect_dispose;
  element_class->set_child_property_full =
      ges_base_effect_set_child_property_full;
}

static void
ges_base_effect_init (GESBaseEffect * self)
{
  self->priv = ges_base_effect_get_instance_private (self);
}

static void
_child_property_removed (GESTimelineElement * element, GObject * child,
    GParamSpec * pspec, gpointer user_data)
{
  GList *tmp;
  GESBaseEffectPrivate *priv = GES_BASE_EFFECT (element)->priv;

  for (tmp = priv->time_properties; tmp; tmp = tmp->next) {
    TimePropertyData *data = tmp->data;
    if (data->child == child && data->pspec == pspec) {
      priv->time_properties = g_list_remove (priv->time_properties, data);
      _time_property_data_free (data);
      return;
    }
  }
}

/**
 * ges_base_effect_register_time_property:
 * @effect: A #GESBaseEffect
 * @child_property_name: The name of the child property to register as
 * a time property
 *
 * Register a child property of the effect as a property that, when set,
 * can change the timing of its input data. The child property should be
 * specified as in ges_timeline_element_lookup_child().
 *
 * You should also set the corresponding time translation using
 * ges_base_effect_set_time_translation_funcs().
 *
 * Note that @effect must not be part of a clip, nor can it have
 * #GESTrackElement:has-internal-source set to %TRUE.
 *
 * Returns: %TRUE if the child property was found and newly registered.
 * Since: 1.18
 */
gboolean
ges_base_effect_register_time_property (GESBaseEffect * effect,
    const gchar * child_property_name)
{
  GESTimelineElement *element;
  GESTrackElement *el;
  GParamSpec *pspec;
  GObject *child;
  GList *tmp;
  TimePropertyData *data;

  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);
  el = GES_TRACK_ELEMENT (effect);
  element = GES_TIMELINE_ELEMENT (el);

  g_return_val_if_fail (element->parent == NULL, FALSE);
  g_return_val_if_fail (ges_track_element_has_internal_source (el) == FALSE,
      FALSE);

  if (!ges_timeline_element_lookup_child (element, child_property_name,
          &child, &pspec))
    return FALSE;

  for (tmp = effect->priv->time_properties; tmp; tmp = tmp->next) {
    data = tmp->data;
    if (data->child == child && data->pspec == pspec) {
      GST_WARNING_OBJECT (effect, "Already registered the time effect for %s",
          child_property_name);
      g_object_unref (child);
      g_param_spec_unref (pspec);
      return FALSE;
    }
  }

  ges_track_element_set_has_internal_source_is_forbidden (el);

  data = g_new0 (TimePropertyData, 1);
  data->child = child;
  data->pspec = pspec;
  data->property_name = g_strdup (child_property_name);

  effect->priv->time_properties =
      g_list_prepend (effect->priv->time_properties, data);

  g_signal_handlers_disconnect_by_func (effect, _child_property_removed, NULL);
  g_signal_connect (effect, "child-property-removed",
      G_CALLBACK (_child_property_removed), NULL);

  return TRUE;
}

/**
 * ges_base_effect_set_time_translation_funcs:
 * @effect: A #GESBaseEffect
 * @source_to_sink_func: (nullable) (scope notified): The function to use
 * for querying how a time is translated from the source coordinates to
 * the sink coordinates of @effect
 * @sink_to_source_func: (nullable) (scope notified): The function to use
 * for querying how a time is translated from the sink coordinates to the
 * source coordinates of @effect
 * @user_data: (closure): Data to pass to both @source_to_sink_func and
 * @sink_to_source_func
 * @destroy: (destroy user_data) (nullable): Method to call to destroy
 * @user_data, or %NULL
 *
 * Set the time translation query functions for the time effect. If an
 * effect is a time effect, it will have two sets of coordinates: one
 * at its sink and one at its source. The given functions should be able
 * to translate between these two sets of coordinates. More specifically,
 * @source_to_sink_func should *emulate* how the corresponding #GstElement
 * would translate the #GstSegment @time field, and @sink_to_source_func
 * should emulate how the corresponding #GstElement would translate the
 * seek query @start and @stop values, as used in gst_element_seek(). As
 * such, @sink_to_source_func should act as an approximate reverse of
 * @source_to_sink_func.
 *
 * Note, these functions will be passed a table of time properties, as
 * registered in ges_base_effect_register_time_property(), and their
 * values. The functions should emulate what the translation *would* be
 * *if* the time properties were set to the given values. They should not
 * use the currently set values.
 *
 * Note that @effect must not be part of a clip, nor can it have
 * #GESTrackElement:has-internal-source set to %TRUE.
 *
 * Returns: %TRUE if the translation functions were set.
 * Since: 1.18
 */
gboolean
ges_base_effect_set_time_translation_funcs (GESBaseEffect * effect,
    GESBaseEffectTimeTranslationFunc source_to_sink_func,
    GESBaseEffectTimeTranslationFunc sink_to_source_func,
    gpointer user_data, GDestroyNotify destroy)
{
  GESTimelineElement *element;
  GESTrackElement *el;
  GESBaseEffectPrivate *priv;

  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);

  element = GES_TIMELINE_ELEMENT (effect);
  el = GES_TRACK_ELEMENT (element);

  g_return_val_if_fail (element->parent == NULL, FALSE);
  g_return_val_if_fail (ges_track_element_has_internal_source (el) == FALSE,
      FALSE);

  ges_track_element_set_has_internal_source_is_forbidden (el);

  priv = effect->priv;
  if (priv->destroy_translation_data)
    priv->destroy_translation_data (priv->translation_data);

  priv->translation_data = user_data;
  priv->destroy_translation_data = destroy;
  priv->source_to_sink = source_to_sink_func;
  priv->sink_to_source = sink_to_source_func;

  return TRUE;
}

/**
 * ges_base_effect_is_time_effect:
 * @effect: A #GESBaseEffect
 *
 * Get whether the effect is considered a time effect or not. An effect
 * with registered time properties or set translation functions is
 * considered a time effect.
 *
 * Returns: %TRUE if @effect is considered a time effect.
 * Since: 1.18
 */
gboolean
ges_base_effect_is_time_effect (GESBaseEffect * effect)
{
  GESBaseEffectPrivate *priv;
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);

  priv = effect->priv;
  if (priv->time_properties || priv->source_to_sink || priv->sink_to_source)
    return TRUE;
  return FALSE;
}

gchar *
ges_base_effect_get_time_property_name (GESBaseEffect * effect,
    GObject * child, GParamSpec * pspec)
{
  GList *tmp;
  for (tmp = effect->priv->time_properties; tmp; tmp = tmp->next) {
    TimePropertyData *data = tmp->data;
    if (data->pspec == pspec && data->child == child)
      return g_strdup (data->property_name);
  }
  return NULL;
}

static void
_gvalue_free (gpointer data)
{
  GValue *val = data;
  g_value_unset (val);
  g_free (val);
}

GHashTable *
ges_base_effect_get_time_property_values (GESBaseEffect * effect)
{
  GList *tmp;
  GHashTable *ret =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, _gvalue_free);

  for (tmp = effect->priv->time_properties; tmp; tmp = tmp->next) {
    TimePropertyData *data = tmp->data;
    GValue *value = g_new0 (GValue, 1);

    /* FIXME: once we move to GLib 2.60, g_object_get_property() will
     * automatically initialize the type */
    g_value_init (value, data->pspec->value_type);
    g_object_get_property (data->child, data->pspec->name, value);

    g_hash_table_insert (ret, g_strdup (data->property_name), value);
  }

  return ret;
}

GstClockTime
ges_base_effect_translate_source_to_sink_time (GESBaseEffect * effect,
    GstClockTime time, GHashTable * time_property_values)
{
  GESBaseEffectPrivate *priv = effect->priv;

  if (!GST_CLOCK_TIME_IS_VALID (time))
    return GST_CLOCK_TIME_NONE;

  if (priv->source_to_sink)
    return priv->source_to_sink (effect, time, time_property_values,
        priv->translation_data);

  if (time_property_values && g_hash_table_size (time_property_values))
    GST_ERROR_OBJECT (effect, "The time effect is missing its source to "
        "sink translation function");
  return time;
}

GstClockTime
ges_base_effect_translate_sink_to_source_time (GESBaseEffect * effect,
    GstClockTime time, GHashTable * time_property_values)
{
  GESBaseEffectPrivate *priv = effect->priv;

  if (!GST_CLOCK_TIME_IS_VALID (time))
    return GST_CLOCK_TIME_NONE;

  if (priv->sink_to_source)
    return effect->priv->sink_to_source (effect, time, time_property_values,
        priv->translation_data);

  if (time_property_values && g_hash_table_size (time_property_values))
    GST_ERROR_OBJECT (effect, "The time effect is missing its sink to "
        "source translation function");
  return time;
}
