/* Gstreamer Editing Services
 *
 * Copyright (C) <2012> Thibault Saunier <thibault.saunier@collabora.com>
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

/**
 * SECTION: geseffectasset
 * @title: GESEffectAsset
 * @short_description: A GESAsset subclass specialized in GESEffect extraction
 *
 * This asset has a GStreamer bin-description as ID and is able to determine
 * to what track type the effect should be used in.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-effect-asset.h"
#include "ges-track-element.h"
#include "ges-internal.h"

struct _GESEffectAssetPrivate
{
  gpointer nothing;
};

G_DEFINE_TYPE_WITH_PRIVATE (GESEffectAsset, ges_effect_asset,
    GES_TYPE_TRACK_ELEMENT_ASSET);

static void
_fill_track_type (GESAsset * asset)
{
  GESTrackType ttype;
  gchar *bin_desc;
  const gchar *id = ges_asset_get_id (asset);

  bin_desc = ges_effect_asset_id_get_type_and_bindesc (id, &ttype, NULL);

  if (bin_desc) {
    ges_track_element_asset_set_track_type (GES_TRACK_ELEMENT_ASSET (asset),
        ttype);
    g_free (bin_desc);
  } else {
    GST_WARNING_OBJECT (asset, "No track type set, you should"
        " specify one in [audio, video] as first component" " in the asset id");
  }
}

/* GESAsset virtual methods implementation */
static GESExtractable *
_extract (GESAsset * asset, GError ** error)
{
  GESExtractable *effect;

  effect = GES_ASSET_CLASS (ges_effect_asset_parent_class)->extract (asset,
      error);

  if (effect == NULL || (error && *error)) {
    effect = NULL;

    return NULL;
  }

  return effect;
}

static void
ges_effect_asset_init (GESEffectAsset * self)
{
  self->priv = ges_effect_asset_get_instance_private (self);
}

static void
ges_effect_asset_constructed (GObject * object)
{
  _fill_track_type (GES_ASSET (object));
}

static void
ges_effect_asset_finalize (GObject * object)
{
  /* TODO: Add deinitalization code here */

  G_OBJECT_CLASS (ges_effect_asset_parent_class)->finalize (object);
}

static void
ges_effect_asset_class_init (GESEffectAssetClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESAssetClass *asset_class = GES_ASSET_CLASS (klass);

  object_class->finalize = ges_effect_asset_finalize;
  object_class->constructed = ges_effect_asset_constructed;
  asset_class->extract = _extract;
}

static gboolean
find_compatible_pads (GstElement * bin, const gchar * bin_desc,
    GstElement * child, GstCaps * valid_caps, GstPad ** srcpad,
    GList ** sinkpads, GList ** elems_with_reqsink,
    GList ** elems_with_reqsrc, GError ** error)
{
  GList *tmp, *tmptemplate;

  for (tmp = child->pads; tmp; tmp = tmp->next) {
    GstCaps *caps;
    GstPad *pad = tmp->data;

    if (GST_PAD_PEER (pad))
      continue;

    if (GST_PAD_IS_SRC (pad) && *srcpad) {
      g_set_error (error, GES_ERROR, GES_ERROR_INVALID_EFFECT_BIN_DESCRIPTION,
          "More than 1 source pad in effect '%s', that is not handled",
          bin_desc);
      return FALSE;
    }

    caps = gst_pad_query_caps (pad, NULL);
    if (gst_caps_can_intersect (caps, valid_caps)) {
      if (GST_PAD_IS_SINK (pad))
        *sinkpads = g_list_append (*sinkpads, gst_object_ref (pad));
      else
        *srcpad = gst_object_ref (pad);
    } else {
      GST_LOG_OBJECT (pad, "Can't link pad %" GST_PTR_FORMAT, caps);
    }

    gst_caps_unref (caps);
  }

  tmptemplate =
      gst_element_class_get_pad_template_list (GST_ELEMENT_GET_CLASS (child));
  for (; tmptemplate; tmptemplate = tmptemplate->next) {
    GstPadTemplate *template = tmptemplate->data;

    if (template->direction == GST_PAD_SINK) {
      if (template->presence == GST_PAD_REQUEST)
        *elems_with_reqsink = g_list_append (*elems_with_reqsink, child);
    }
  }

  return TRUE;
}

static GstPad *
request_pad (GstElement * element, GstPadDirection direction)
{
  GstPad *pad = NULL;
  GList *templates;

  templates = gst_element_class_get_pad_template_list
      (GST_ELEMENT_GET_CLASS (element));

  for (; templates; templates = templates->next) {
    GstPadTemplate *templ = (GstPadTemplate *) templates->data;

    GST_LOG_OBJECT (element, "Trying template %s",
        GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));

    if ((GST_PAD_TEMPLATE_DIRECTION (templ) == direction) &&
        (GST_PAD_TEMPLATE_PRESENCE (templ) == GST_PAD_REQUEST)) {
      pad =
          gst_element_request_pad_simple (element,
          GST_PAD_TEMPLATE_NAME_TEMPLATE (templ));
      if (pad)
        break;
    }
  }

  return pad;
}

static GstPad *
get_pad_from_elements_with_request_pad (GstElement * effect,
    const gchar * bin_desc, GList * requestable, GstPadDirection direction,
    GError ** error)
{
  GstElement *request_element = NULL;

  if (!requestable) {
    g_set_error (error, GES_ERROR, GES_ERROR_INVALID_EFFECT_BIN_DESCRIPTION,
        "No %spads available for effect: %s",
        (direction == GST_PAD_SRC) ? "src" : "sink", bin_desc);

    return NULL;
  }

  request_element = requestable->data;
  if (requestable->next) {
    GstIterator *it = gst_bin_iterate_sorted (GST_BIN (effect));
    GValue v;

    while (gst_iterator_next (it, &v) != GST_ITERATOR_DONE) {
      GstElement *tmpe = g_value_get_object (&v);

      if (g_list_find (requestable, tmpe)) {
        request_element = tmpe;
        if (direction == GST_PAD_SRC) {
          break;
        }
      }
      g_value_reset (&v);
    }
    gst_iterator_free (it);
  }

  return request_pad (request_element, direction);
}

static gboolean
ghost_pad (GstElement * effect, const gchar * bin_desc, GstPad * pad,
    gint n_pad, const gchar * converter_str, GError ** error)
{
  gchar *name;
  GstPad *peer, *ghosted;
  GstPadLinkReturn lret;
  GstElement *converter;

  if (!converter_str) {
    ghosted = pad;
    goto ghost;
  }

  converter = gst_parse_bin_from_description_full (converter_str, TRUE, NULL,
      GST_PARSE_FLAG_NO_SINGLE_ELEMENT_BINS | GST_PARSE_FLAG_PLACE_IN_BIN,
      error);

  if (!converter) {
    GST_ERROR_OBJECT (effect, "Could not create converter '%s'", converter_str);
    return FALSE;
  }

  peer =
      GST_PAD_IS_SINK (pad) ? converter->srcpads->data : converter->sinkpads->
      data;

  gst_bin_add (GST_BIN (effect), converter);
  lret =
      gst_pad_link (GST_PAD_IS_SINK (pad) ? peer : pad,
      GST_PAD_IS_SINK (pad) ? pad : peer);

  if (lret != GST_PAD_LINK_OK) {
    gst_object_unref (converter);
    g_set_error (error, GES_ERROR, GES_ERROR_INVALID_EFFECT_BIN_DESCRIPTION,
        "Effect %s can not link converter %s with %s", bin_desc, converter_str,
        gst_pad_link_get_name (lret));
    return FALSE;
  }

  ghosted =
      GST_PAD_IS_SRC (pad) ? converter->srcpads->data : converter->sinkpads->
      data;

ghost:

  if (GST_PAD_IS_SINK (pad))
    name = g_strdup_printf ("sink_%d", n_pad);
  else
    name = g_strdup_printf ("src");

  gst_element_add_pad (effect, gst_ghost_pad_new (name, ghosted));
  g_free (name);

  return TRUE;
}

GstElement *
ges_effect_from_description (const gchar * bin_desc, GESTrackType type,
    GError ** error)
{

  gint n_sink = 0;
  GstPad *srcpad = NULL;
  GstCaps *valid_caps = NULL;
  const gchar *converter_str = NULL;
  GList *tmp, *sinkpads = NULL, *elems_with_reqsink = NULL,
      *elems_with_reqsrc = NULL;
  GstElement *effect =
      gst_parse_bin_from_description_full (bin_desc, FALSE, NULL,
      GST_PARSE_FLAG_PLACE_IN_BIN | GST_PARSE_FLAG_FATAL_ERRORS, error);

  if (!effect) {
    GST_ERROR ("An error occurred while creating: %s",
        (error && *error) ? (*error)->message : "Unknown error");
    goto err;
  }

  if (type == GES_TRACK_TYPE_VIDEO) {
    valid_caps = gst_caps_from_string ("video/x-raw(ANY)");
    converter_str = "videoconvert";
  } else if (type == GES_TRACK_TYPE_AUDIO) {
    valid_caps = gst_caps_from_string ("audio/x-raw(ANY)");
    converter_str = "audioconvert ! audioresample ! audioconvert";
  } else {
    valid_caps = gst_caps_new_any ();
  }

  for (tmp = GST_BIN_CHILDREN (effect); tmp; tmp = tmp->next) {
    if (!find_compatible_pads (effect, bin_desc, tmp->data, valid_caps, &srcpad,
            &sinkpads, &elems_with_reqsink, &elems_with_reqsrc, error))
      goto err;
  }

  if (!sinkpads) {
    GstPad *sinkpad = get_pad_from_elements_with_request_pad (effect, bin_desc,
        elems_with_reqsink, GST_PAD_SINK, error);
    if (!sinkpad)
      goto err;
    sinkpads = g_list_append (sinkpads, sinkpad);
  }

  if (!srcpad) {
    srcpad = get_pad_from_elements_with_request_pad (effect, bin_desc,
        elems_with_reqsrc, GST_PAD_SRC, error);
    if (!srcpad)
      goto err;
  }

  for (tmp = sinkpads; tmp; tmp = tmp->next) {
    if (!ghost_pad (effect, bin_desc, tmp->data, n_sink, converter_str, error))
      goto err;
    n_sink++;
  }

  if (!ghost_pad (effect, bin_desc, srcpad, 0, converter_str, error))
    goto err;

done:
  g_list_free (elems_with_reqsink);
  g_list_free (elems_with_reqsrc);
  g_list_free_full (sinkpads, gst_object_unref);
  gst_clear_caps (&valid_caps);
  gst_clear_object (&srcpad);

  return effect;

err:
  gst_clear_object (&effect);
  goto done;
}

gchar *
ges_effect_asset_id_get_type_and_bindesc (const char *id,
    GESTrackType * track_type, GError ** error)
{
  GList *tmp;
  GstElement *effect;
  gchar **typebin_desc = NULL;
  const gchar *user_bindesc;
  gchar *bindesc = NULL;

  *track_type = GES_TRACK_TYPE_UNKNOWN;
  typebin_desc = g_strsplit (id, " ", 2);
  if (!g_strcmp0 (typebin_desc[0], "audio")) {
    *track_type = GES_TRACK_TYPE_AUDIO;
    user_bindesc = typebin_desc[1];
  } else if (!g_strcmp0 (typebin_desc[0], "video")) {
    *track_type = GES_TRACK_TYPE_VIDEO;
    user_bindesc = typebin_desc[1];
  } else {
    *track_type = GES_TRACK_TYPE_UNKNOWN;
    user_bindesc = id;
  }

  bindesc = g_strdup (user_bindesc);
  g_strfreev (typebin_desc);

  effect = gst_parse_bin_from_description (bindesc, TRUE, error);
  if (effect == NULL) {
    GST_ERROR ("Could not create element from: %s", bindesc);
    g_free (bindesc);
    return NULL;
  }

  if (*track_type != GES_TRACK_TYPE_UNKNOWN) {
    gst_object_unref (effect);

    return bindesc;
  }

  for (tmp = GST_BIN_CHILDREN (effect); tmp; tmp = tmp->next) {
    GstElementFactory *factory =
        gst_element_get_factory (GST_ELEMENT (tmp->data));
    const gchar *klass =
        gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);

    if (g_strrstr (klass, "Effect") || g_strrstr (klass, "Filter")) {
      if (g_strrstr (klass, "Audio")) {
        *track_type = GES_TRACK_TYPE_AUDIO;
        break;
      } else if (g_strrstr (klass, "Video")) {
        *track_type = GES_TRACK_TYPE_VIDEO;
        break;
      }
    }
  }

  gst_object_unref (effect);

  if (*track_type == GES_TRACK_TYPE_UNKNOWN) {
    *track_type = GES_TRACK_TYPE_VIDEO;
    GST_ERROR ("Could not determine track type for %s, defaulting to video",
        id);

  }

  if (!(effect = ges_effect_from_description (bindesc, *track_type, error))) {
    g_free (bindesc);

    return NULL;
  }
  gst_object_unref (effect);

  return bindesc;
}
