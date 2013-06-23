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
 * SECTION:ges-uri-source
 * @short_description: outputs a single media stream from a given file
 *
 * Outputs a single media stream from a given file. The stream chosen depends on
 * the type of the track which contains the object.
 */

#include "ges-utils.h"
#include "ges-internal.h"
#include "ges-track-element.h"
#include "ges-uri-source.h"
#include "ges-uri-asset.h"
#include "ges-extractable.h"
#include "ges-layer.h"
#include "gstframepositionner.h"

struct _GESUriSourcePrivate
{
  GHashTable *props_hashtable;
  GstFramePositionner *positionner;
};

enum
{
  PROP_0,
  PROP_URI
};

/* Callbacks */

static void
_pad_added_cb (GstElement * element, GstPad * srcpad, GstPad * sinkpad)
{
  gst_element_no_more_pads (element);
  gst_pad_link (srcpad, sinkpad);
}

static void
_ghost_pad_added_cb (GstElement * element, GstPad * srcpad, GstElement * bin)
{
  GstPad *ghost;

  ghost = gst_ghost_pad_new ("src", srcpad);
  gst_pad_set_active (ghost, TRUE);
  gst_element_add_pad (bin, ghost);
  gst_element_no_more_pads (element);
}

/* Internal methods */

static GstElement *
_create_bin (const gchar * bin_name, GstElement * decodebin, ...)
{
  va_list argp;

  GstElement *element;
  GstElement *prev = NULL;
  GstElement *first = NULL;
  GstElement *bin;

  va_start (argp, decodebin);
  bin = gst_bin_new (bin_name);
  gst_bin_add (GST_BIN (bin), decodebin);

  while ((element = va_arg (argp, GstElement *)) != NULL) {
    gst_bin_add (GST_BIN (bin), element);
    if (prev)
      gst_element_link (prev, element);
    prev = element;
    if (first == NULL)
      first = element;
  }

  va_end (argp);

  if (prev != NULL) {
    GstPad *srcpad, *sinkpad, *ghost;

    srcpad = gst_element_get_static_pad (prev, "src");
    ghost = gst_ghost_pad_new ("src", srcpad);
    gst_pad_set_active (ghost, TRUE);
    gst_element_add_pad (bin, ghost);

    sinkpad = gst_element_get_static_pad (first, "sink");
    g_signal_connect (decodebin, "pad-added", G_CALLBACK (_pad_added_cb),
        sinkpad);

    gst_object_unref (srcpad);
    gst_object_unref (sinkpad);

  } else {
    /* Our decodebin is alone in the bin, we need to ghost its source when it appears */

    g_signal_connect (decodebin, "pad-added", G_CALLBACK (_ghost_pad_added_cb),
        bin);
  }

  return bin;
}

static void
_add_element_properties_to_hashtable (GESUriSource * self, GstElement * element,
    ...)
{
  GObjectClass *class;
  GParamSpec *pspec;
  va_list argp;
  const gchar *propname;

  class = G_OBJECT_GET_CLASS (element);
  va_start (argp, element);

  while ((propname = va_arg (argp, const gchar *)) != NULL)
  {
    pspec = g_object_class_find_property (class, propname);
    if (!pspec) {
      GST_WARNING ("no such property : %s in element : %s", propname,
          gst_element_get_name (element));
      continue;
    }

    if (self->priv->props_hashtable == NULL)
      self->priv->props_hashtable =
          g_hash_table_new_full ((GHashFunc) pspec_hash, pspec_equal,
          (GDestroyNotify) g_param_spec_unref, gst_object_unref);

    if (pspec->flags & G_PARAM_WRITABLE) {
      g_hash_table_insert (self->priv->props_hashtable,
          g_param_spec_ref (pspec), gst_object_ref (element));
      GST_LOG_OBJECT (self,
          "added property %s to controllable properties successfully !",
          propname);
    } else
      GST_WARNING ("the property %s for element %s exists but is not writable",
          propname, gst_element_get_name (element));
  }

  va_end (argp);
}

static void
_sync_element_to_layer_property_float (GESTrackElement * trksrc,
    GstElement * element, const gchar * meta, const gchar * propname)
{
  GESTimelineElement *parent;
  GESLayer *layer;
  gfloat value;

  parent = ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (trksrc));
  layer = ges_clip_get_layer (GES_CLIP (parent));

  gst_object_unref (parent);

  if (layer != NULL) {

    ges_meta_container_get_float (GES_META_CONTAINER (layer), meta, &value);
    g_object_set (element, propname, value, NULL);
    GST_DEBUG_OBJECT (trksrc, "Setting %s to %f", propname, value);

  } else {

    GST_DEBUG_OBJECT (trksrc, "NOT setting the %s", propname);
  }

  gst_object_unref (layer);
}

/* TrackElement VMethods */

static void
update_z_order_cb (GESClip * clip, GParamSpec * arg G_GNUC_UNUSED,
    GESUriSource * self)
{
  GESLayer *layer = ges_clip_get_layer (clip);

  if (layer == NULL)
    return;

  /* 10000 is the max value of zorder on videomixerpad, hardcoded */

  g_object_set (self->priv->positionner, "zorder",
      10000 - ges_layer_get_priority (layer), NULL);

  gst_object_unref (layer);
}

static GstElement *
ges_uri_source_create_element (GESTrackElement * trksrc)
{
  GESUriSource *self;
  GESTrack *track;
  GstElement *decodebin;
  GstElement *topbin, *volume;
  GstElement *positionner;
  GESTimelineElement *parent;

  self = (GESUriSource *) trksrc;
  track = ges_track_element_get_track (trksrc);

  switch (track->type) {
    case GES_TRACK_TYPE_AUDIO:
      GST_DEBUG_OBJECT (trksrc, "Creating a bin uridecodebin ! volume");

      decodebin = gst_element_factory_make ("uridecodebin", NULL);
      volume = gst_element_factory_make ("volume", NULL);

      topbin = _create_bin ("audio-src-bin", decodebin, volume, NULL);

      _sync_element_to_layer_property_float (trksrc, volume, GES_META_VOLUME,
          "volume");
      _add_element_properties_to_hashtable (self, volume, "volume", "mute",
          NULL);
      break;
    case GES_TRACK_TYPE_VIDEO:
      decodebin = gst_element_factory_make ("uridecodebin", NULL);

      /* That positionner will add metadata to buffers according to its
         properties, acting like a proxy for our smart-mixer dynamic pads. */
      positionner =
          gst_element_factory_make ("framepositionner", "frame_tagger");
      _add_element_properties_to_hashtable (self, positionner, "alpha", "posx",
          "posy", NULL);
      topbin = _create_bin ("video-src-bin", decodebin, positionner, NULL);
      parent = ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (trksrc));
      if (parent) {
        self->priv->positionner = GST_FRAME_POSITIONNER (positionner);
        g_signal_connect (parent, "notify::layer",
            (GCallback) update_z_order_cb, trksrc);
        update_z_order_cb (GES_CLIP (parent), NULL, self);
        gst_object_unref (parent);
      } else {
        GST_WARNING ("No parent timeline element, SHOULD NOT HAPPEN");
      }
      break;
    default:
      decodebin = gst_element_factory_make ("uridecodebin", NULL);
      topbin = _create_bin ("video-src-bin", decodebin, NULL);
      break;
  }

  g_object_set (decodebin, "caps", ges_track_get_caps (track),
      "expose-all-streams", FALSE, "uri", self->uri, NULL);

  return topbin;
}

static GHashTable *
ges_uri_source_get_props_hashtable (GESTrackElement * element)
{
  GESUriSource *self = (GESUriSource *) element;

  if (self->priv->props_hashtable == NULL)
    self->priv->props_hashtable =
        g_hash_table_new_full ((GHashFunc) pspec_hash, pspec_equal,
        (GDestroyNotify) g_param_spec_unref, gst_object_unref);

  return self->priv->props_hashtable;
}

/* Extractable interface implementation */

static gchar *
ges_extractable_check_id (GType type, const gchar * id, GError ** error)
{
  return g_strdup (id);
}

static void
extractable_set_asset (GESExtractable * self, GESAsset * asset)
{
  /* FIXME That should go into #GESTrackElement, but
   * some work is needed to make sure it works properly */

  if (ges_track_element_get_track_type (GES_TRACK_ELEMENT (self)) ==
      GES_TRACK_TYPE_UNKNOWN) {
    ges_track_element_set_track_type (GES_TRACK_ELEMENT (self),
        ges_track_element_asset_get_track_type (GES_TRACK_ELEMENT_ASSET
            (asset)));
  }
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_URI_SOURCE_ASSET;
  iface->check_id = ges_extractable_check_id;
  iface->set_asset = extractable_set_asset;
}

G_DEFINE_TYPE_WITH_CODE (GESUriSource, ges_track_filesource,
    GES_TYPE_SOURCE,
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));


/* GObject VMethods */

static void
ges_track_filesource_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESUriSource *uriclip = GES_URI_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      g_value_set_string (value, uriclip->uri);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_filesource_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESUriSource *uriclip = GES_URI_SOURCE (object);

  switch (property_id) {
    case PROP_URI:
      if (uriclip->uri) {
        GST_WARNING_OBJECT (object, "Uri already set to %s", uriclip->uri);
        return;
      }
      uriclip->uri = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_filesource_dispose (GObject * object)
{
  GESUriSource *uriclip = GES_URI_SOURCE (object);

  if (uriclip->uri)
    g_free (uriclip->uri);

  G_OBJECT_CLASS (ges_track_filesource_parent_class)->dispose (object);
}

static void
ges_track_filesource_class_init (GESUriSourceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTrackElementClass *track_class = GES_TRACK_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESUriSourcePrivate));

  object_class->get_property = ges_track_filesource_get_property;
  object_class->set_property = ges_track_filesource_set_property;
  object_class->dispose = ges_track_filesource_dispose;

  /**
   * GESUriSource:uri:
   *
   * The location of the file/resource to use.
   */
  g_object_class_install_property (object_class, PROP_URI,
      g_param_spec_string ("uri", "URI", "uri of the resource",
          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  track_class->create_element = ges_uri_source_create_element;
  track_class->get_props_hastable = ges_uri_source_get_props_hashtable;
}

static void
ges_track_filesource_init (GESUriSource * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_URI_SOURCE, GESUriSourcePrivate);
  self->priv->props_hashtable = NULL;
  self->priv->positionner = NULL;
}

/**
 * ges_track_filesource_new:
 * @uri: the URI the source should control
 *
 * Creates a new #GESUriSource for the provided @uri.
 *
 * Returns: The newly created #GESUriSource, or %NULL if there was an
 * error.
 */
GESUriSource *
ges_track_filesource_new (gchar * uri)
{
  return g_object_new (GES_TYPE_URI_SOURCE, "uri", uri, NULL);
}
