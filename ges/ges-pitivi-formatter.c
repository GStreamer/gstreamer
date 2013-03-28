/* GStreamer Editing Services Pitivi Formatter
 * Copyright (C) 2011-2012 Mathieu Duponchelle <seeed@laposte.net>
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
 * SECTION: ges-pitivi-formatter
 * @short_description: A formatter for the PiTiVi project file format
 */

#include <libxml/xmlreader.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

#include "ges-internal.h"
#include <ges/ges.h>

/* The PiTiVi etree formatter is 0.1 we set GES one to 0.2 */
#define VERSION "0.2"
#define DOUBLE_VERSION 0.2

G_DEFINE_TYPE (GESPitiviFormatter, ges_pitivi_formatter, GES_TYPE_FORMATTER);

#undef GST_CAT_DEFAULT
GST_DEBUG_CATEGORY_STATIC (ges_pitivi_formatter_debug);
#define GST_CAT_DEFAULT ges_pitivi_formatter_debug

typedef struct SrcMapping
{
  gchar *id;
  GESClip *clip;
  guint priority;
  GList *track_element_ids;
} SrcMapping;

struct _GESPitiviFormatterPrivate
{
  xmlXPathContextPtr xpathCtx;

  /* {"sourceId" : {"prop": "value"}} */
  GHashTable *sources_table;

  /* Used as a set of the uris */
  GHashTable *source_uris;

  /* {trackId: {"factory_ref": factoryId, ""}
   * if effect:
   *      {"factory_ref": "effect",
   *       "effect_name": name
   *       "effect_props": {"propname": value}}}
   */
  GHashTable *track_elements_table;

  /* {factory-ref: [track-object-ref-id,...]} */
  GHashTable *clips_table;

  /* {layerPriority: layer} */
  GHashTable *layers_table;

  GESTimeline *timeline;

  GESTrack *tracka, *trackv;

  /* List the Clip that haven't been loaded yet */
  GList *sources_to_load;

  /* Saving context */
  /* {factory_id: uri} */
  GHashTable *saving_source_table;
  guint nb_sources;
};

static void
list_table_destroyer (gpointer key, gpointer value, void *unused)
{
  g_list_foreach (value, (GFunc) g_free, NULL);
  g_list_free (value);
}

static gboolean
pitivi_can_load_uri (GESFormatter * dummy_instance, const gchar * uri,
    GError ** error)
{
  xmlDocPtr doc;
  gboolean ret = TRUE;
  xmlXPathObjectPtr xpathObj;
  xmlXPathContextPtr xpathCtx;

  if (!(doc = xmlParseFile (uri))) {
    GST_ERROR ("The xptv file for uri %s was badly formed or did not exist",
        uri);
    return FALSE;
  }

  xpathCtx = xmlXPathNewContext (doc);
  xpathObj = xmlXPathEvalExpression ((const xmlChar *) "/pitivi", xpathCtx);
  if (!xpathObj || !xpathObj->nodesetval || xpathObj->nodesetval->nodeNr == 0)
    ret = FALSE;

  xmlFreeDoc (doc);
  xmlXPathFreeObject (xpathObj);
  xmlXPathFreeContext (xpathCtx);

  return ret;
}

/* Project saving functions */

static void inline
write_int_attribute (xmlTextWriterPtr writer, guint64 nb, const gchar * attr,
    const gchar * type)
{
  gchar *str = g_strdup_printf ("%s%" G_GUINT64_FORMAT, type, nb);
  xmlTextWriterWriteAttribute (writer, BAD_CAST attr, BAD_CAST str);
  g_free (str);
}

/* Project loading functions */

/* Return: a GHashTable containing:
 *    {attr: value}
 */
static GHashTable *
get_nodes_infos (xmlNodePtr node)
{
  xmlAttr *cur_attr;
  GHashTable *props_table;
  gchar *name, *value;

  props_table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  for (cur_attr = node->properties; cur_attr; cur_attr = cur_attr->next) {
    name = (gchar *) cur_attr->name;
    value = (gchar *) xmlGetProp (node, cur_attr->name);
    g_hash_table_insert (props_table, g_strdup (name), g_strdup (value));
    xmlFree (value);
  }

  return props_table;
}

static gboolean
create_tracks (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  GList *tracks = NULL;

  tracks = ges_timeline_get_tracks (self->timeline);

  GST_DEBUG ("Creating tracks, current number of tracks %d",
      g_list_length (tracks));

  if (tracks) {
    GList *tmp = NULL;
    GESTrack *track;
    for (tmp = tracks; tmp; tmp = tmp->next) {
      track = tmp->data;
      if (track->type == GES_TRACK_TYPE_AUDIO) {
        priv->tracka = track;
      } else {
        priv->trackv = track;
      }
    }
    g_list_foreach (tracks, (GFunc) gst_object_unref, NULL);
    g_list_free (tracks);
    return TRUE;
  }

  priv->tracka = GES_TRACK (ges_audio_track_new ());
  priv->trackv = GES_TRACK (ges_video_track_new ());

  if (!ges_timeline_add_track (self->timeline, priv->trackv)) {
    return FALSE;
  }

  if (!ges_timeline_add_track (self->timeline, priv->tracka)) {
    return FALSE;
  }

  return TRUE;
}

static void
parse_metadatas (GESFormatter * self)
{
  guint i, size;
  xmlNodePtr node;
  xmlAttr *cur_attr;
  xmlNodeSetPtr nodes;
  xmlXPathObjectPtr xpathObj;
  GESMetaContainer *metacontainer = GES_META_CONTAINER (self->project);

  xpathObj = xmlXPathEvalExpression ((const xmlChar *)
      "/pitivi/metadata", GES_PITIVI_FORMATTER (self)->priv->xpathCtx);
  nodes = xpathObj->nodesetval;

  size = (nodes) ? nodes->nodeNr : 0;
  for (i = 0; i < size; i++) {
    node = nodes->nodeTab[i];
    for (cur_attr = node->properties; cur_attr; cur_attr = cur_attr->next) {
      ges_meta_container_set_string (metacontainer, (gchar *) cur_attr->name,
          (gchar *) xmlGetProp (node, cur_attr->name));
    }
  }
}

static void
list_sources (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  xmlXPathObjectPtr xpathObj;
  GHashTable *table;
  int size, j;
  gchar *id, *filename;
  xmlNodeSetPtr nodes;

  xpathObj = xmlXPathEvalExpression ((const xmlChar *)
      "/pitivi/factories/sources/source", priv->xpathCtx);
  nodes = xpathObj->nodesetval;

  size = (nodes) ? nodes->nodeNr : 0;
  for (j = 0; j < size; ++j) {
    table = get_nodes_infos (nodes->nodeTab[j]);
    id = (gchar *) g_hash_table_lookup (table, (gchar *) "id");
    filename = (gchar *) g_hash_table_lookup (table, (gchar *) "filename");
    g_hash_table_insert (priv->sources_table, g_strdup (id), table);
    g_hash_table_insert (priv->source_uris, g_strdup (filename),
        g_strdup (filename));
    if (self->project)
      ges_project_create_asset (self->project, filename, GES_TYPE_URI_CLIP);
  }

  xmlXPathFreeObject (xpathObj);
}

static gboolean
parse_track_elements (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  xmlXPathObjectPtr xpathObj;
  xmlNodeSetPtr nodes;
  int size, j;
  gchar *id, *fac_ref;
  GHashTable *table = NULL, *effect_table = NULL;
  xmlNode *first_child;
  gchar *media_type;

  /* FIXME Make this whole function cleaner starting from
   * "/pitivi/timeline/tracks/track/stream" and descending
   * into the children. */
  xpathObj = xmlXPathEvalExpression ((const xmlChar *)
      "/pitivi/timeline/tracks/track/track-objects/track-object",
      priv->xpathCtx);

  if (xpathObj == NULL) {
    GST_DEBUG ("No track object found");

    return FALSE;
  }

  nodes = xpathObj->nodesetval;
  size = (nodes) ? nodes->nodeNr : 0;

  for (j = 0; j < size; ++j) {
    xmlNodePtr node = nodes->nodeTab[j];

    table = get_nodes_infos (nodes->nodeTab[j]);
    id = (gchar *) g_hash_table_lookup (table, (gchar *) "id");
    first_child = nodes->nodeTab[j]->children->next;
    fac_ref = (gchar *) xmlGetProp (first_child, (xmlChar *) "id");

    /* We check if the first child is "effect" */
    if (!g_strcmp0 ((gchar *) first_child->name, (gchar *) "effect")) {
      xmlChar *effect_name;
      xmlNodePtr fact_node = first_child->children->next;

      /* We have a node called "text" in between thus ->next->next */
      xmlNodePtr elem_props_node = fact_node->next->next;

      effect_name = xmlGetProp (fact_node, (xmlChar *) "name");
      g_hash_table_insert (table, g_strdup ((gchar *) "effect_name"),
          g_strdup ((gchar *) effect_name));
      xmlFree (effect_name);

      /* We put the effects properties in an hacktable (Lapsus is on :) */
      effect_table = get_nodes_infos (elem_props_node);

      g_hash_table_insert (table, g_strdup ((gchar *) "fac_ref"),
          g_strdup ("effect"));

      xmlFree (fac_ref);
    } else {

      g_hash_table_insert (table, g_strdup ((gchar *) "fac_ref"),
          g_strdup (fac_ref));
      xmlFree (fac_ref);
    }

    /* Same as before, we got a text node in between, thus the 2 prev
     * node->parent is <track-objects>, the one before is <stream>
     */
    media_type = (gchar *) xmlGetProp (node->parent->prev->prev,
        (const xmlChar *) "type");
    g_hash_table_insert (table, g_strdup ((gchar *) "media_type"),
        g_strdup (media_type));
    xmlFree (media_type);


    if (effect_table)
      g_hash_table_insert (table, g_strdup ("effect_props"), effect_table);

    g_hash_table_insert (priv->track_elements_table, g_strdup (id), table);
  }

  xmlXPathFreeObject (xpathObj);
  return TRUE;
}

static gboolean
parse_clips (GESFormatter * self)
{
  int size, j;
  xmlNodeSetPtr nodes;
  xmlXPathObjectPtr xpathObj;
  xmlNodePtr clip_nd, tmp_nd, tmp_nd2;
  xmlChar *trackelementrefId, *facrefId = NULL;

  GList *reflist = NULL;
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  GHashTable *clips_table = priv->clips_table;

  xpathObj = xmlXPathEvalExpression ((const xmlChar *)
      "/pitivi/timeline/timeline-objects/timeline-object", priv->xpathCtx);

  if (xpathObj == NULL) {
    xmlXPathFreeObject (xpathObj);
    return FALSE;
  }

  nodes = xpathObj->nodesetval;
  size = (nodes) ? nodes->nodeNr : 0;

  for (j = 0; j < size; j++) {
    clip_nd = nodes->nodeTab[j];

    for (tmp_nd = clip_nd->children; tmp_nd; tmp_nd = tmp_nd->next) {
      /* We assume that factory-ref is always before the tckobjs-ref */
      if (!xmlStrcmp (tmp_nd->name, (xmlChar *) "factory-ref")) {
        facrefId = xmlGetProp (tmp_nd, (xmlChar *) "id");

      } else if (!xmlStrcmp (tmp_nd->name, (xmlChar *) "track-object-refs")) {

        for (tmp_nd2 = tmp_nd->children; tmp_nd2; tmp_nd2 = tmp_nd2->next) {
          if (!xmlStrcmp (tmp_nd2->name, (xmlChar *) "track-object-ref")) {
            /* We add the track object ref ID to the list of the current
             * Clip tracks, this way we can merge 2
             * Clip-s into 1 when we have unlinked TrackElement-s */
            reflist = g_hash_table_lookup (clips_table, facrefId);
            trackelementrefId = xmlGetProp (tmp_nd2, (xmlChar *) "id");
            reflist =
                g_list_append (reflist, g_strdup ((gchar *) trackelementrefId));
            g_hash_table_insert (clips_table, g_strdup ((gchar *) facrefId),
                reflist);

            xmlFree (trackelementrefId);
          }
        }
      }
    }
  }

  xmlXPathFreeObject (xpathObj);
  return TRUE;
}

static void
set_properties (GObject * obj, GHashTable * props_table)
{
  gint i;
  gchar **prop_array, *valuestr;
  gint64 value;

  gchar props[3][10] = { "duration", "in_point", "start" };

  for (i = 0; i < 3; i++) {
    valuestr = g_hash_table_lookup (props_table, props[i]);
    prop_array = g_strsplit (valuestr, ")", 0);
    value = g_ascii_strtoll (prop_array[1], NULL, 0);
    g_object_set (obj, props[i], value, NULL);

    g_strfreev (prop_array);
  }
}

static void
track_element_added_cb (GESClip * clip,
    GESTrackElement * track_element, GHashTable * props_table)
{
  GESPitiviFormatter *formatter;

  formatter = GES_PITIVI_FORMATTER (g_hash_table_lookup (props_table,
          "current-formatter"));
  if (formatter) {
    GESPitiviFormatterPrivate *priv = formatter->priv;

    /* Make sure the hack to get a ref to the formatter
     * doesn't break everything */
    g_hash_table_steal (props_table, "current-formatter");

    priv->sources_to_load = g_list_remove (priv->sources_to_load, clip);
    if (!priv->sources_to_load && GES_FORMATTER (formatter)->project)
      ges_project_set_loaded (GES_FORMATTER (formatter)->project,
          GES_FORMATTER (formatter));
  }

  /* Disconnect the signal */
  g_signal_handlers_disconnect_by_func (clip, track_element_added_cb,
      props_table);
}

static void
make_source (GESFormatter * self, GList * reflist, GHashTable * source_table)
{
  GHashTable *props_table, *effect_table;
  gchar **prio_array;
  GESLayer *layer;
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;

  gchar *fac_ref = NULL, *media_type = NULL, *filename = NULL, *prio_str;
  GList *tmp = NULL, *keys, *tmp_key;
  GESUriClip *src = NULL;
  gint prio;
  gboolean a_avail = FALSE, v_avail = FALSE, video;
  GHashTable *trackelement_table = priv->track_elements_table;

  for (tmp = reflist; tmp; tmp = tmp->next) {

    /* Get the layer */
    props_table = g_hash_table_lookup (trackelement_table, (gchar *) tmp->data);
    prio_str = (gchar *) g_hash_table_lookup (props_table, "priority");
    prio_array = g_strsplit (prio_str, ")", 0);
    prio = (gint) g_ascii_strtod (prio_array[1], NULL);
    g_strfreev (prio_array);

    /* If we do not have any layer with this priority, create it */
    if (!(layer = g_hash_table_lookup (priv->layers_table, &prio))) {
      layer = ges_layer_new ();
      g_object_set (layer, "auto-transition", TRUE, "priority", prio, NULL);
      ges_timeline_add_layer (self->timeline, layer);
      g_hash_table_insert (priv->layers_table, g_memdup (&prio,
              sizeof (guint64)), layer);
    }

    fac_ref = (gchar *) g_hash_table_lookup (props_table, "fac_ref");
    media_type = (gchar *) g_hash_table_lookup (props_table, "media_type");

    if (!g_strcmp0 (media_type, "pitivi.stream.VideoStream"))
      video = TRUE;
    else
      video = FALSE;

    /* FIXME I am sure we could reimplement this whole part
     * in a simpler way */

    if (g_strcmp0 (fac_ref, (gchar *) "effect")) {
      /* FIXME this is a hack to get a ref to the formatter when receiving
       * child-added */
      g_hash_table_insert (props_table, (gchar *) "current-formatter", self);
      if (a_avail && (!video)) {
        a_avail = FALSE;
      } else if (v_avail && (video)) {
        v_avail = FALSE;
      } else {

        /* If we only have audio or only video in the previous source,
         * set it has such */
        if (a_avail) {
          ges_clip_set_supported_formats (GES_CLIP (src), GES_TRACK_TYPE_VIDEO);
        } else if (v_avail) {
          ges_clip_set_supported_formats (GES_CLIP (src), GES_TRACK_TYPE_AUDIO);
        }

        filename = (gchar *) g_hash_table_lookup (source_table, "filename");

        src = ges_uri_clip_new (filename);

        if (!video) {
          v_avail = TRUE;
          a_avail = FALSE;
        } else {
          a_avail = TRUE;
          v_avail = FALSE;
        }

        set_properties (G_OBJECT (src), props_table);
        ges_layer_add_clip (layer, GES_CLIP (src));

        g_signal_connect (src, "child-added",
            G_CALLBACK (track_element_added_cb), props_table);

        priv->sources_to_load = g_list_prepend (priv->sources_to_load, src);
      }

    } else {
      GESEffect *effect;
      gchar *active = (gchar *) g_hash_table_lookup (props_table, "active");

      effect = ges_effect_new ((gchar *)
          g_hash_table_lookup (props_table, (gchar *) "effect_name"));
      ges_track_element_set_track_type (GES_TRACK_ELEMENT (effect),
          (video ? GES_TRACK_TYPE_VIDEO : GES_TRACK_TYPE_AUDIO));
      effect_table =
          g_hash_table_lookup (props_table, (gchar *) "effect_props");

      ges_container_add (GES_CONTAINER (src), GES_TIMELINE_ELEMENT (effect));

      if (!g_strcmp0 (active, (gchar *) "(bool)False"))
        ges_track_element_set_active (GES_TRACK_ELEMENT (effect), FALSE);

      /* Set effect properties */
      keys = g_hash_table_get_keys (effect_table);
      for (tmp_key = keys; tmp_key; tmp_key = tmp_key->next) {
        GstStructure *structure;
        const GValue *value;
        GParamSpec *spec;
        GstCaps *caps;
        gchar *prop_val;

        prop_val = (gchar *) g_hash_table_lookup (effect_table,
            (gchar *) tmp_key->data);

        if (g_strstr_len (prop_val, -1, "(GEnum)")) {
          gchar **val = g_strsplit (prop_val, ")", 2);

          ges_track_element_set_child_properties (GES_TRACK_ELEMENT (effect),
              (gchar *) tmp_key->data, atoi (val[1]), NULL);
          g_strfreev (val);

        } else if (ges_track_element_lookup_child (GES_TRACK_ELEMENT (effect),
                (gchar *) tmp->data, NULL, &spec)) {
          gchar *caps_str = g_strdup_printf ("structure1, property1=%s;",
              prop_val);

          caps = gst_caps_from_string (caps_str);
          g_free (caps_str);
          structure = gst_caps_get_structure (caps, 0);
          value = gst_structure_get_value (structure, "property1");

          ges_track_element_set_child_property_by_pspec (GES_TRACK_ELEMENT
              (effect), spec, (GValue *) value);
          gst_caps_unref (caps);
        }
      }
    }
  }

  if (a_avail) {
    ges_clip_set_supported_formats (GES_CLIP (src), GES_TRACK_TYPE_VIDEO);
  } else if (v_avail) {
    ges_clip_set_supported_formats (GES_CLIP (src), GES_TRACK_TYPE_AUDIO);
  }
}

static gboolean
make_clips (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  GHashTable *source_table;

  GList *keys = NULL, *tmp = NULL, *reflist = NULL;

  keys = g_hash_table_get_keys (priv->clips_table);

  for (tmp = keys; tmp; tmp = tmp->next) {
    gchar *fac_id = (gchar *) tmp->data;

    reflist = g_hash_table_lookup (priv->clips_table, fac_id);
    source_table = g_hash_table_lookup (priv->sources_table, fac_id);
    make_source (self, reflist, source_table);
  }

  g_list_free (keys);
  return TRUE;
}

static gboolean
load_pitivi_file_from_uri (GESFormatter * self,
    GESTimeline * timeline, const gchar * uri, GError ** error)
{
  xmlDocPtr doc;
  GESLayer *layer;
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;

  gboolean ret = TRUE;
  gint *prio = malloc (sizeof (gint));

  *prio = 0;
  layer = ges_layer_new ();
  g_object_set (layer, "auto-transition", TRUE, NULL);

  g_hash_table_insert (priv->layers_table, prio, layer);
  g_object_set (layer, "priority", (gint32) 0, NULL);

  if (!ges_timeline_add_layer (timeline, layer)) {
    GST_ERROR ("Couldn't add layer");
    return FALSE;
  }

  if (!(doc = xmlParseFile (uri))) {
    GST_ERROR ("The xptv file for uri %s was badly formed or did not exist",
        uri);
    return FALSE;
  }

  priv->xpathCtx = xmlXPathNewContext (doc);

  if (self->project)
    parse_metadatas (self);

  if (!create_tracks (self)) {
    GST_ERROR ("Couldn't create tracks");
    return FALSE;
  }

  list_sources (self);

  if (!parse_clips (self)) {
    GST_ERROR ("Couldn't find clips markup in the xptv file");
    return FALSE;
  }

  if (!parse_track_elements (self)) {
    GST_ERROR ("Couldn't find track objects markup in the xptv file");
    return FALSE;
  }



  /* If there are no clips to load we should emit
   * 'project-loaded' signal.
   */
  if (!g_hash_table_size (priv->clips_table) && GES_FORMATTER (self)->project) {
    ges_project_set_loaded (GES_FORMATTER (self)->project,
        GES_FORMATTER (self));
  } else {
    if (!make_clips (self)) {
      GST_ERROR ("Couldn't deserialise the project properly");
      return FALSE;
    }
  }

  xmlXPathFreeContext (priv->xpathCtx);
  xmlFreeDoc (doc);
  return ret;
}

/* Object functions */
static void
ges_pitivi_formatter_finalize (GObject * object)
{
  GESPitiviFormatter *self = GES_PITIVI_FORMATTER (object);
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;

  g_hash_table_destroy (priv->sources_table);
  g_hash_table_destroy (priv->source_uris);

  g_hash_table_destroy (priv->saving_source_table);
  g_list_free (priv->sources_to_load);

  if (priv->clips_table != NULL) {
    g_hash_table_foreach (priv->clips_table,
        (GHFunc) list_table_destroyer, NULL);
  }

  if (priv->layers_table != NULL)
    g_hash_table_destroy (priv->layers_table);

  if (priv->track_elements_table != NULL) {
    g_hash_table_destroy (priv->track_elements_table);
  }

  G_OBJECT_CLASS (ges_pitivi_formatter_parent_class)->finalize (object);
}

static void
ges_pitivi_formatter_class_init (GESPitiviFormatterClass * klass)
{
  GESFormatterClass *formatter_klass;
  GObjectClass *object_class;

  GST_DEBUG_CATEGORY_INIT (ges_pitivi_formatter_debug, "ges_pitivi_formatter",
      GST_DEBUG_FG_YELLOW, "ges pitivi formatter");

  object_class = G_OBJECT_CLASS (klass);
  formatter_klass = GES_FORMATTER_CLASS (klass);
  g_type_class_add_private (klass, sizeof (GESPitiviFormatterPrivate));

  formatter_klass->can_load_uri = pitivi_can_load_uri;
  formatter_klass->save_to_uri = NULL;
  formatter_klass->load_from_uri = load_pitivi_file_from_uri;
  object_class->finalize = ges_pitivi_formatter_finalize;

  ges_formatter_class_register_metas (formatter_klass, "pitivi",
      "Legacy Pitivi project files", "xptv", "text/x-xptv",
      DOUBLE_VERSION, GST_RANK_MARGINAL);
}

static void
ges_pitivi_formatter_init (GESPitiviFormatter * self)
{
  GESPitiviFormatterPrivate *priv;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_PITIVI_FORMATTER, GESPitiviFormatterPrivate);

  priv = self->priv;

  priv->track_elements_table =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) g_hash_table_destroy);

  priv->clips_table =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  priv->layers_table =
      g_hash_table_new_full (g_int_hash, g_str_equal, g_free, gst_object_unref);

  priv->sources_table =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      (GDestroyNotify) g_hash_table_destroy);

  priv->source_uris =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  priv->sources_to_load = NULL;

  /* Saving context */
  priv->saving_source_table =
      g_hash_table_new_full (g_str_hash, g_int_equal, g_free, g_free);
  priv->nb_sources = 1;
}

GESPitiviFormatter *
ges_pitivi_formatter_new (void)
{
  return g_object_new (GES_TYPE_PITIVI_FORMATTER, NULL);
}
