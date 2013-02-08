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
#include "libxml/encoding.h"
#include "libxml/xmlwriter.h"

#include "ges-internal.h"
#include <ges/ges.h>
#include <inttypes.h>
#include <unistd.h>
#define GetCurrentDir getcwd

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
  GESClip *obj;
  guint priority;
  GList *tck_obj_ids;
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

/* Memory freeing functions */
static void
free_src_map (SrcMapping * srcmap)
{
  g_free (srcmap->id);
  g_object_unref (srcmap->obj);
  g_list_foreach (srcmap->tck_obj_ids, (GFunc) g_free, NULL);
  g_list_free (srcmap->tck_obj_ids);
  g_slice_free (SrcMapping, srcmap);
}

static void
list_table_destroyer (gpointer key, gpointer value, void *unused)
{
  g_list_foreach (value, (GFunc) g_free, NULL);
  g_list_free (value);
}

static gboolean
pitivi_can_load_uri (GESFormatterClass * class, const gchar * uri,
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
  gchar *str = g_strdup_printf ("%s%" PRIu64, type, nb);
  xmlTextWriterWriteAttribute (writer, BAD_CAST attr, BAD_CAST str);
  g_free (str);
}

static void
save_track_elements (xmlTextWriterPtr writer, GList * source_list,
    GESTrackType type, gint * id)
{
  GList *tmp, *tck_objs, *tmp_tck;
  gchar *bin_desc;
  xmlTextWriterStartElement (writer, BAD_CAST "track-objects");

  GST_DEBUG ("Saving track elements");
  for (tmp = source_list; tmp; tmp = tmp->next) {
    SrcMapping *srcmap;
    GESClip *object;
    guint i, j;
    guint64 inpoint, duration, start;

    srcmap = (SrcMapping *) tmp->data;
    object = srcmap->obj;

    /* Save track associated objects */
    tck_objs = ges_clip_get_track_elements (object);
    for (tmp_tck = tck_objs; tmp_tck; tmp_tck = tmp_tck->next) {
      xmlChar *cast;
      GESTrackElement *trackelement = GES_TRACK_ELEMENT (tmp_tck->data);
      GESTrack *track = ges_track_element_get_track (trackelement);
      const gchar *active, *locked;

      if (!track) {
        GST_WARNING ("Track element %p not in a track yet", trackelement);
        continue;
      }

      /* We are serializing this track type */
      if (track->type != type)
        continue;

      /* Save properties */
      /* Set important properties */
      xmlTextWriterStartElement (writer, BAD_CAST "track-object");

      active =
          ges_track_element_is_active (trackelement) ? "(bool)True" :
          "(bool)False";
      xmlTextWriterWriteAttribute (writer, BAD_CAST "active", BAD_CAST active);
      locked =
          ges_track_element_is_locked (trackelement) ? "(bool)True" :
          "(bool)False";
      xmlTextWriterWriteAttribute (writer, BAD_CAST "locked", BAD_CAST locked);

      /*  Here the priority correspond to the layer priority */
      write_int_attribute (writer, srcmap->priority, "priority", "(int)");
      g_object_get (G_OBJECT (trackelement), "duration", &duration, "start",
          &start, "in-point", &inpoint, NULL);
      write_int_attribute (writer, duration, "duration", "(gint64)");
      write_int_attribute (writer, start, "start", "(gint64)");
      write_int_attribute (writer, inpoint, "in_point", "(gint64)");

      cast = xmlXPathCastNumberToString (*id);
      xmlTextWriterWriteAttribute (writer, BAD_CAST "id", BAD_CAST cast);
      xmlFree (cast);

      if (GES_IS_BASE_EFFECT (trackelement)) {
        GParamSpec **pspecs, *spec;
        gchar *serialized, *concatenated;
        guint n_props = 0;

        xmlTextWriterWriteAttribute (writer, BAD_CAST "type",
            BAD_CAST "pitivi.timeline.track.TrackEffect");

        g_object_get (trackelement, "bin-description", &bin_desc, NULL);
        xmlTextWriterStartElement (writer, BAD_CAST "effect");
        xmlTextWriterStartElement (writer, BAD_CAST "factory");
        xmlTextWriterWriteAttribute (writer, BAD_CAST "name",
            BAD_CAST bin_desc);
        xmlTextWriterEndElement (writer);
        xmlTextWriterStartElement (writer, BAD_CAST "gst-element-properties");

        pspecs =
            ges_track_element_list_children_properties (trackelement, &n_props);

        j = 0;

        while (j < n_props) {
          GValue val = { 0 };

          spec = pspecs[j];
          g_value_init (&val, spec->value_type);
          ges_track_element_get_child_property_by_pspec (trackelement, spec,
              &val);
          serialized = gst_value_serialize (&val);
          if (!g_strcmp0 (spec->name, (gchar *) "preset")) {
            concatenated =
                g_strconcat ("(GEnum)",
                xmlXPathCastNumberToString ((g_value_get_enum (&val))), NULL);
            xmlTextWriterWriteAttribute (writer, BAD_CAST spec->name,
                BAD_CAST concatenated);
          } else {
            concatenated =
                g_strconcat ("(", g_type_name (spec->value_type), ")",
                serialized, NULL);
            xmlTextWriterWriteAttribute (writer, BAD_CAST spec->name,
                BAD_CAST concatenated);
          }
          j++;
        }

        xmlTextWriterEndElement (writer);

        for (i = 0; i < n_props; i++) {
          g_param_spec_unref (pspecs[i]);
        }

        g_free (pspecs);

      } else {
        xmlTextWriterWriteAttribute (writer, BAD_CAST "type",
            BAD_CAST "pitivi.timeline.track.SourceTrackObject");

        xmlTextWriterStartElement (writer, BAD_CAST "factory-ref");
        xmlTextWriterWriteAttribute (writer, BAD_CAST "id",
            BAD_CAST srcmap->id);
      }
      xmlTextWriterEndElement (writer);
      xmlTextWriterEndElement (writer);

      /* We add effects at the end of the trackelement list */
      if (GES_IS_BASE_EFFECT (trackelement)) {
        srcmap->tck_obj_ids = g_list_append (srcmap->tck_obj_ids,
            xmlXPathCastNumberToString (*id));
      } else {
        srcmap->tck_obj_ids = g_list_prepend (srcmap->tck_obj_ids,
            xmlXPathCastNumberToString (*id));
      }
      *id = *id + 1;
    }
  }

  xmlTextWriterEndElement (writer);
}

static void
save_tracks (GESTimeline * timeline, xmlTextWriterPtr writer,
    GList * source_list)
{
  GList *tracks, *tmp;
  gint id = 0;

  xmlTextWriterStartElement (writer, BAD_CAST "timeline");
  xmlTextWriterStartElement (writer, BAD_CAST "tracks");

  GST_DEBUG ("Saving tracks");

  tracks = ges_timeline_get_tracks (timeline);
  for (tmp = tracks; tmp; tmp = tmp->next) {
    gchar *caps;
    GESTrackType type;

    GESTrack *track = GES_TRACK (tmp->data);

    xmlTextWriterStartElement (writer, BAD_CAST "track");
    xmlTextWriterStartElement (writer, BAD_CAST "stream");

    /* Serialize track type and caps */
    g_object_get (G_OBJECT (track), "track-type", &type, NULL);
    caps = gst_caps_to_string (ges_track_get_caps (track));
    xmlTextWriterWriteAttribute (writer, BAD_CAST "caps", BAD_CAST caps);
    g_free (caps);

    if (type == GES_TRACK_TYPE_AUDIO) {
      xmlTextWriterWriteAttribute (writer, BAD_CAST "type",
          BAD_CAST "pitivi.stream.AudioStream");
      xmlTextWriterEndElement (writer);
    } else if (type == GES_TRACK_TYPE_VIDEO) {
      xmlTextWriterWriteAttribute (writer, BAD_CAST "type",
          BAD_CAST "pitivi.stream.VideoStream");
      xmlTextWriterEndElement (writer);
    } else {
      GST_WARNING ("Track type %i not supported", type);

      continue;
    }

    save_track_elements (writer, source_list, type, &id);
    xmlTextWriterEndElement (writer);
  }

  g_list_foreach (tracks, (GFunc) g_object_unref, NULL);
  g_list_free (tracks);
  xmlTextWriterEndElement (writer);
}

static void
write_source (gchar * uri, gchar * id, xmlTextWriterPtr writer)
{
  xmlTextWriterStartElement (writer, BAD_CAST "source");

  xmlTextWriterWriteAttribute (writer, BAD_CAST "filename", BAD_CAST uri);
  xmlTextWriterWriteAttribute (writer, BAD_CAST "id", BAD_CAST id);
  xmlTextWriterEndElement (writer);
}

static GList *
save_sources (GESPitiviFormatter * formatter, GList * layers,
    xmlTextWriterPtr writer)
{
  GList *clips, *tmp, *tmplayer;
  GESTimelineLayer *layer;
  GESPitiviFormatterPrivate *priv = formatter->priv;

  GList *source_list = NULL;

  GST_DEBUG ("Saving sources");

  g_hash_table_foreach (priv->saving_source_table, (GHFunc) write_source,
      writer);

  for (tmplayer = layers; tmplayer; tmplayer = tmplayer->next) {
    layer = GES_TIMELINE_LAYER (tmplayer->data);

    clips = ges_timeline_layer_get_objects (layer);
    for (tmp = clips; tmp; tmp = tmp->next) {
      SrcMapping *srcmap = g_slice_new0 (SrcMapping);
      GESClip *clip;
      gchar *uriclip_uri;
      clip = tmp->data;

      if (GES_IS_URI_CLIP (clip)) {

        uriclip_uri = (gchar *) ges_uri_clip_get_uri (GES_URI_CLIP (clip));

        if (!g_hash_table_lookup (priv->saving_source_table, uriclip_uri)) {
          gchar *strid = g_strdup_printf ("%i", priv->nb_sources);

          g_hash_table_insert (priv->saving_source_table,
              g_strdup (uriclip_uri), strid);
          write_source (uriclip_uri, strid, writer);
          priv->nb_sources++;
        }

        srcmap->id =
            g_strdup (g_hash_table_lookup (priv->saving_source_table,
                uriclip_uri));
        srcmap->obj = g_object_ref (clip);
        srcmap->priority = ges_timeline_layer_get_priority (layer);
        /* We fill up the tck_obj_ids in save_track_elements */
        source_list = g_list_append (source_list, srcmap);
      }
    }
    g_list_foreach (clips, (GFunc) g_object_unref, NULL);
    g_list_free (clips);
    g_object_unref (G_OBJECT (layer));
  }

  return source_list;
}

static void
save_clips (xmlTextWriterPtr writer, GList * list)
{
  GList *tmp, *tck_obj_ids;

  xmlTextWriterStartElement (writer, BAD_CAST "timeline-objects");

  GST_DEBUG ("Saving clips");

  for (tmp = list; tmp; tmp = tmp->next) {

    SrcMapping *srcmap = (SrcMapping *) tmp->data;

    xmlTextWriterStartElement (writer, BAD_CAST "timeline-object");
    xmlTextWriterStartElement (writer, BAD_CAST "factory-ref");
    xmlTextWriterWriteAttribute (writer, BAD_CAST "id", BAD_CAST srcmap->id);
    xmlTextWriterEndElement (writer);
    xmlTextWriterStartElement (writer, BAD_CAST "track-object-refs");

    for (tck_obj_ids = srcmap->tck_obj_ids; tck_obj_ids;
        tck_obj_ids = tck_obj_ids->next) {
      xmlTextWriterStartElement (writer, BAD_CAST "track-object-ref");
      xmlTextWriterWriteAttribute (writer, BAD_CAST "id",
          BAD_CAST tck_obj_ids->data);
      xmlTextWriterEndElement (writer);
    }
    xmlTextWriterEndElement (writer);
    xmlTextWriterEndElement (writer);
  }
  xmlTextWriterEndElement (writer);
}

static gboolean
save_pitivi_timeline_to_uri (GESFormatter * formatter,
    GESTimeline * timeline, const gchar * uri, gboolean overwrite,
    GError ** error)
{
  xmlTextWriterPtr writer;
  GList *list = NULL, *layers = NULL;

  writer = xmlNewTextWriterFilename (uri, 0);
  xmlTextWriterSetIndent (writer, 1);
  xmlTextWriterStartElement (writer, BAD_CAST "pitivi");
  xmlTextWriterWriteAttribute (writer, BAD_CAST "formatter", BAD_CAST "GES");
  /*  */
  xmlTextWriterWriteAttribute (writer, BAD_CAST "version", BAD_CAST VERSION);

  xmlTextWriterStartElement (writer, BAD_CAST "factories");
  xmlTextWriterStartElement (writer, BAD_CAST "sources");

  layers = ges_timeline_get_layers (timeline);
  list = save_sources (GES_PITIVI_FORMATTER (formatter), layers, writer);

  xmlTextWriterEndElement (writer);
  xmlTextWriterEndElement (writer);

  save_tracks (timeline, writer, list);
  save_clips (writer, list);
  xmlTextWriterEndDocument (writer);
  xmlFreeTextWriter (writer);

  g_list_free (layers);
  g_list_foreach (list, (GFunc) free_src_map, NULL);
  g_list_free (list);

  return TRUE;
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
    g_list_foreach (tracks, (GFunc) g_object_unref, NULL);
    g_list_free (tracks);
    return TRUE;
  }

  priv->tracka = ges_track_audio_raw_new ();
  priv->trackv = ges_track_video_raw_new ();

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
track_element_added_cb (GESClip * object,
    GESTrackElement * track_element, GHashTable * props_table)
{
  gchar *media_type = NULL, *lockedstr;
  GList *tck_objs = NULL, *tmp = NULL;
  GESTrack *track;
  gint64 start, duration;
  gboolean has_effect = FALSE, locked = TRUE;
  gint type = 0;
  GESPitiviFormatter *formatter;

  tck_objs = ges_clip_get_track_elements (object);
  media_type = (gchar *) g_hash_table_lookup (props_table, "media_type");
  lockedstr = (gchar *) g_hash_table_lookup (props_table, "locked");

  formatter = GES_PITIVI_FORMATTER (g_hash_table_lookup (props_table,
          "current-formatter"));

  if (formatter) {
    GESPitiviFormatterPrivate *priv = formatter->priv;

    /* Make sure the hack to get a ref to the formatter
     * doesn't break everything */
    g_hash_table_steal (props_table, "current-formatter");

    priv->sources_to_load = g_list_remove (priv->sources_to_load, object);
    if (!priv->sources_to_load && GES_FORMATTER (formatter)->project)
      ges_project_set_loaded (GES_FORMATTER (formatter)->project,
          GES_FORMATTER (formatter));
  }

  if (lockedstr && !g_strcmp0 (lockedstr, "(bool)False"))
    locked = FALSE;

  for (tmp = tck_objs; tmp; tmp = tmp->next) {

    if (!GES_IS_TRACK_ELEMENT (tmp->data)) {
      /* If we arrive here something massively screwed */
      GST_ERROR ("Not a TrackElement, this is a bug");
      continue;
    }

    track = ges_track_element_get_track (tmp->data);
    if (!track) {
      GST_WARNING ("TrackElement not in a track yet");
      continue;
    }

    if (GES_IS_EFFECT (tmp->data)) {
      has_effect = TRUE;
      continue;
    }

    if ((!g_strcmp0 (media_type, "pitivi.stream.VideoStream")
            && track->type == GES_TRACK_TYPE_VIDEO)
        || (!g_strcmp0 (media_type, "pitivi.stream.AudioStream")
            && track->type == GES_TRACK_TYPE_AUDIO)) {

      /* We unlock the track element so we do not move the whole Clip */
      ges_track_element_set_locked (tmp->data, FALSE);
      set_properties (G_OBJECT (tmp->data), props_table);

      if (locked)
        ges_track_element_set_locked (tmp->data, TRUE);

      type = track->type;
      g_object_get (tmp->data, "start", &start, "duration", &duration, NULL);
    }
  }

  if (has_effect) {
    tck_objs = ges_clip_get_track_elements (object);

    /* FIXME make sure this is the way we want to handle that
     * ie: set duration and start as the other trackelement
     * and no let full control to the user. */

    for (tmp = tck_objs; tmp; tmp = tmp->next) {
      /* We set the effects start and duration */
      track = ges_track_element_get_track (tmp->data);

      if (GES_IS_EFFECT (tmp->data)
          && (type == track->type)) {
        /* We lock the track elements so we do not move the whole Clip */
        ges_track_element_set_locked (tmp->data, FALSE);
        g_object_set (tmp->data, "start", start, "duration", duration, NULL);
        if (locked)
          ges_track_element_set_locked (tmp->data, TRUE);
      }
    }
  }

  /* Disconnect the signal */
  g_signal_handlers_disconnect_by_func (object, track_element_added_cb,
      props_table);
}

static void
make_source (GESFormatter * self, GList * reflist, GHashTable * source_table)
{
  GHashTable *props_table, *effect_table;
  gchar **prio_array;
  GESTimelineLayer *layer;
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
      layer = ges_timeline_layer_new ();
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
       * track-element-added */
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
        ges_timeline_layer_add_object (layer, GES_CLIP (src));

        g_signal_connect (src, "track-element-added",
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

      ges_clip_add_track_element (GES_CLIP (src), GES_TRACK_ELEMENT (effect));

      if (!g_strcmp0 (active, (gchar *) "(bool)False"))
        ges_track_element_set_active (GES_TRACK_ELEMENT (effect), FALSE);

      if (video)
        ges_track_add_element (priv->trackv, GES_TRACK_ELEMENT (effect));
      else
        ges_track_add_element (priv->tracka, GES_TRACK_ELEMENT (effect));

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
  GESTimelineLayer *layer;
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;

  gboolean ret = TRUE;
  gint *prio = malloc (sizeof (gint));

  *prio = 0;
  layer = ges_timeline_layer_new ();
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
  formatter_klass->save_to_uri = save_pitivi_timeline_to_uri;
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
      g_hash_table_new_full (g_int_hash, g_str_equal, g_free, g_object_unref);

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
