/* GStreamer Editing Services
 * Copyright (C) 2010 Thibault Saunier <thibault.saunier@collabora.co.uk>
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
 * SECTION:geseffect
 * @title: GESEffect
 * @short_description: adds an effect build from a parse-launch style
 * bin description to a stream in a GESSourceClip or a GESLayer
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-extractable.h"
#include "ges-track-element.h"
#include "ges-base-effect.h"
#include "ges-effect-asset.h"
#include "ges-effect.h"

static void ges_extractable_interface_init (GESExtractableInterface * iface);


static void ges_effect_dispose (GObject * object);
static void ges_effect_finalize (GObject * object);
static GstElement *ges_effect_create_element (GESTrackElement * self);

struct _GESEffectPrivate
{
  gchar *bin_description;
};

enum
{
  PROP_0,
  PROP_BIN_DESCRIPTION,
};

G_DEFINE_TYPE_WITH_CODE (GESEffect,
    ges_effect, GES_TYPE_BASE_EFFECT, G_ADD_PRIVATE (GESEffect)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

static gchar *
extractable_check_id (GType type, const gchar * id, GError ** error)
{
  gchar *bin_desc, *real_id;
  GESTrackType ttype;

  bin_desc = ges_effect_assect_id_get_type_and_bindesc (id, &ttype, error);

  if (bin_desc == NULL)
    return NULL;

  if (ttype == GES_TRACK_TYPE_AUDIO)
    real_id = g_strdup_printf ("audio %s", bin_desc);
  else if (ttype == GES_TRACK_TYPE_VIDEO)
    real_id = g_strdup_printf ("video %s", bin_desc);
  else
    g_assert_not_reached ();

  g_free (bin_desc);

  return real_id;
}

static GParameter *
extractable_get_parameters_from_id (const gchar * id, guint * n_params)
{
  GParameter *params = g_new0 (GParameter, 3);
  gchar *bin_desc;
  GESTrackType ttype;

  bin_desc = ges_effect_assect_id_get_type_and_bindesc (id, &ttype, NULL);

  params[0].name = "bin-description";
  g_value_init (&params[0].value, G_TYPE_STRING);
  g_value_set_string (&params[0].value, bin_desc);

  params[1].name = "track-type";
  g_value_init (&params[1].value, GES_TYPE_TRACK_TYPE);
  g_value_set_flags (&params[1].value, ttype);

  *n_params = 2;

  g_free (bin_desc);
  return params;
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  return g_strdup (GES_EFFECT (self)->priv->bin_description);
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_EFFECT_ASSET;
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_parameters_from_id = extractable_get_parameters_from_id;
  iface->get_id = extractable_get_id;
}

static int
property_name_compare (gconstpointer s1, gconstpointer s2)
{
  return g_strcmp0 ((const gchar *) s1, (const gchar *) s2);
}

static void
_set_child_property (GESTimelineElement * self G_GNUC_UNUSED, GObject * child,
    GParamSpec * pspec, GValue * value)
{
  GESEffectClass *klass = GES_EFFECT_GET_CLASS (self);
  gchar *full_property_name;

  GES_TIMELINE_ELEMENT_CLASS
      (ges_effect_parent_class)->set_child_property (self, child, pspec, value);

  full_property_name = g_strdup_printf ("%s::%s", G_OBJECT_TYPE_NAME (child),
      pspec->name);

  if (g_list_find_custom (klass->rate_properties, full_property_name,
          property_name_compare)) {
    GstElement *nleobject =
        ges_track_element_get_nleobject (GES_TRACK_ELEMENT (self));
    gdouble media_duration_factor =
        ges_timeline_element_get_media_duration_factor (self);

    g_object_set (nleobject, "media-duration-factor", media_duration_factor,
        NULL);
  }

  g_free (full_property_name);
}

static void
ges_effect_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESEffectPrivate *priv = GES_EFFECT (object)->priv;

  switch (property_id) {
    case PROP_BIN_DESCRIPTION:
      g_value_set_string (value, priv->bin_description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_effect_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESEffect *self = GES_EFFECT (object);

  switch (property_id) {
    case PROP_BIN_DESCRIPTION:
      self->priv->bin_description = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_effect_class_init (GESEffectClass * klass)
{
  GObjectClass *object_class;
  GESTrackElementClass *obj_bg_class;
  GESTimelineElementClass *element_class;

  object_class = G_OBJECT_CLASS (klass);
  obj_bg_class = GES_TRACK_ELEMENT_CLASS (klass);
  element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  object_class->get_property = ges_effect_get_property;
  object_class->set_property = ges_effect_set_property;
  object_class->dispose = ges_effect_dispose;
  object_class->finalize = ges_effect_finalize;

  obj_bg_class->create_element = ges_effect_create_element;
  element_class->set_child_property = _set_child_property;

  klass->rate_properties = NULL;
  ges_effect_class_register_rate_property (klass, "scaletempo", "rate");
  ges_effect_class_register_rate_property (klass, "pitch", "tempo");
  ges_effect_class_register_rate_property (klass, "pitch", "rate");
  ges_effect_class_register_rate_property (klass, "videorate", "rate");

  /**
   * GESEffect:bin-description:
   *
   * The description of the effect bin with a gst-launch-style
   * pipeline description.
   *
   * Example: "videobalance saturation=1.5 hue=+0.5"
   */
  g_object_class_install_property (object_class, PROP_BIN_DESCRIPTION,
      g_param_spec_string ("bin-description",
          "bin description",
          "Bin description of the effect",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
ges_effect_init (GESEffect * self)
{
  self->priv = ges_effect_get_instance_private (self);
}

static void
ges_effect_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_effect_parent_class)->dispose (object);
}

static void
ges_effect_finalize (GObject * object)
{
  GESEffect *self = GES_EFFECT (object);

  if (self->priv->bin_description)
    g_free (self->priv->bin_description);

  G_OBJECT_CLASS (ges_effect_parent_class)->finalize (object);
}

static void
ghost_compatible_pads (GstElement * bin, GstElement * child,
    GstCaps * valid_caps, gint * n_src, gint * n_sink)
{
  GList *tmp;

  for (tmp = child->pads; tmp; tmp = tmp->next) {
    GstCaps *caps;
    GstPad *pad = tmp->data;

    if (GST_PAD_PEER (pad))
      continue;

    caps = gst_pad_query_caps (pad, NULL);

    if (gst_caps_can_intersect (caps, valid_caps)) {
      gchar *name =
          g_strdup_printf ("%s_%d", GST_PAD_IS_SINK (pad) ? "sink" : "src",
          GST_PAD_IS_SINK (pad) ? *n_sink++ : *n_src++);

      GST_DEBUG_OBJECT (bin, "Ghosting pad: %" GST_PTR_FORMAT, pad);
      gst_element_add_pad (GST_ELEMENT (bin), gst_ghost_pad_new (name, pad));
      g_free (name);
    } else {
      GST_DEBUG_OBJECT (pad, "Can't ghost pad %" GST_PTR_FORMAT, caps);
    }

    gst_caps_unref (caps);
  }
}

static GstElement *
ges_effect_create_element (GESTrackElement * object)
{
  GList *tmp;
  GstElement *effect;
  gchar *bin_desc;
  GstCaps *valid_caps;
  gint n_src = 0, n_sink = 0;

  GError *error = NULL;
  GESEffect *self = GES_EFFECT (object);
  const gchar *blacklisted_factories[] =
      { "audioconvert", "audioresample", "videoconvert", NULL };

  GESTrackType type = ges_track_element_get_track_type (object);

  if (type == GES_TRACK_TYPE_VIDEO) {
    bin_desc = g_strconcat ("videoconvert name=pre_video_convert ! ",
        self->priv->bin_description, " ! videoconvert name=post_video_convert",
        NULL);
    valid_caps = gst_caps_from_string ("video/x-raw(ANY)");
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    bin_desc =
        g_strconcat ("audioconvert ! audioresample !",
        self->priv->bin_description, NULL);
    valid_caps = gst_caps_from_string ("audio/x-raw(ANY)");
  } else {
    g_assert_not_reached ();
  }

  effect = gst_parse_bin_from_description (bin_desc, FALSE, &error);
  g_free (bin_desc);
  if (error != NULL) {
    GST_ERROR ("An error occured while creating the GstElement: %s",
        error->message);
    g_error_free (error);
    goto fail;
  }

  for (tmp = GST_BIN_CHILDREN (effect); tmp; tmp = tmp->next) {
    ghost_compatible_pads (effect, tmp->data, valid_caps, &n_src, &n_sink);

    if (n_src > 1) {
      GST_ERROR ("More than 1 source pad in the effect, that is not possible");

      goto fail;
    }
  }

  ges_track_element_add_children_props (object, effect, NULL,
      blacklisted_factories, NULL);

done:
  gst_clear_caps (&valid_caps);

  return effect;

fail:
  gst_clear_object (&effect);

  goto done;
}

/**
 * ges_effect_new:
 * @bin_description: The gst-launch like bin description of the effect
 *
 * Creates a new #GESEffect from the description of the bin. It should be
 * possible to determine the type of the effect through the element
 * 'klass' metadata of the GstElements that will be created.
 * In that corner case, you should use:
 * #ges_asset_request (GES_TYPE_EFFECT, "audio your ! bin ! description", NULL);
 * and extract that asset to be in full control.
 *
 * Returns: (nullable): a newly created #GESEffect, or %NULL if something went
 * wrong.
 */
GESEffect *
ges_effect_new (const gchar * bin_description)
{
  GESEffect *effect;
  GESAsset *asset = ges_asset_request (GES_TYPE_EFFECT,
      bin_description, NULL);

  g_return_val_if_fail (asset, NULL);

  effect = GES_EFFECT (ges_asset_extract (asset, NULL));

  gst_object_unref (asset);

  return effect;
}

/**
 * ges_effect_class_register_rate_property:
 * @klass: Instance of the GESEffectClass
 * @element_name: Name of the GstElement that changes the rate
 * @property_name: Name of the property that changes the rate
 *
 * Register an element that can change the rate at which media is playing. The
 * property type must be float or double, and must be a factor of the rate,
 * i.e. a value of 2.0 must mean that the media plays twice as fast. For
 * example, this is true for the properties 'rate' and 'tempo' of the element
 * 'pitch', which is already registered by default. By registering the element,
 * timeline duration can be correctly converted into media duration, allowing
 * the right segment seeks to be sent to the sources.
 *
 * A reference to the GESEffectClass can be obtained as follows:
 *   GES_EFFECT_CLASS (g_type_class_ref (GES_TYPE_EFFECT));
 *
 * Returns: whether the rate property was succesfully registered. When this
 * method returns false, a warning is emitted with more information.
 */
gboolean
ges_effect_class_register_rate_property (GESEffectClass * klass,
    const gchar * element_name, const gchar * property_name)
{
  GstElementFactory *element_factory = NULL;
  GstElement *element = NULL;
  GParamSpec *pspec = NULL;
  gchar *full_property_name = NULL;
  GType param_type;
  gboolean res = FALSE;

  element_factory = gst_element_factory_find (element_name);
  if (element_factory == NULL) {
    GST_WARNING
        ("Did not add rate property '%s' for element '%s': the element factory could not be found",
        property_name, element_name);
    goto fail;
  }

  element = gst_element_factory_create (element_factory, NULL);
  if (element == NULL) {
    GST_WARNING
        ("Did not add rate property '%s' for element '%s': the element could not be constructed",
        property_name, element_name);
    goto fail;
  }

  pspec =
      g_object_class_find_property (G_OBJECT_GET_CLASS (element),
      property_name);
  if (pspec == NULL) {
    GST_WARNING
        ("Did not add rate property '%s' for element '%s': the element did not have the property name specified",
        property_name, element_name);
    goto fail;
  }

  param_type = G_PARAM_SPEC_VALUE_TYPE (pspec);
  if (param_type != G_TYPE_FLOAT && param_type != G_TYPE_DOUBLE) {
    GST_WARNING
        ("Did not add rate property '%s' for element '%s': the property is not of float or double type",
        property_name, element_name);
    goto fail;
  }

  full_property_name = g_strdup_printf ("%s::%s",
      g_type_name (gst_element_factory_get_element_type (element_factory)),
      property_name);

  if (g_list_find_custom (klass->rate_properties, full_property_name,
          property_name_compare) == NULL) {
    klass->rate_properties =
        g_list_append (klass->rate_properties, full_property_name);
    GST_DEBUG ("Added rate property %s", full_property_name);
  } else {
    g_free (full_property_name);
  }

  res = TRUE;

fail:
  if (element_factory != NULL)
    gst_object_unref (element_factory);
  if (element != NULL)
    gst_object_unref (element);
  if (pspec != NULL)
    g_param_spec_unref (pspec);

  return res;
}
