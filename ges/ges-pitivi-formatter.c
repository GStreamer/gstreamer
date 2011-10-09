#include <ges/ges.h>
#include <unistd.h>
#define GetCurrentDir getcwd

G_DEFINE_TYPE (GESPitiviFormatter, ges_pitivi_formatter, GES_TYPE_FORMATTER);

static gboolean save_pitivi_timeline_to_uri (GESFormatter * pitivi_formatter,
    GESTimeline * timeline, const gchar * uri);
static gboolean load_pitivi_file_from_uri (GESFormatter * self,
    GESTimeline * timeline, const gchar * uri);

static void ges_pitivi_formatter_finalize (GObject * object);

static xmlDocPtr create_doc (const gchar * uri);

static GHashTable *get_nodes_infos (xmlNodePtr nodes);
static gboolean create_tracks (GESFormatter * self);
static GHashTable *list_sources (GESFormatter * self);
static gboolean parse_track_objects (GESFormatter * self);
static gboolean parse_timeline_objects (GESFormatter * self);

static void save_tracks (GESTimeline * timeline, xmlTextWriterPtr writer,
    GList * source_list);
static GList *save_sources (GESTimelineLayer * layer, xmlTextWriterPtr writer);
static void save_track_objects (xmlTextWriterPtr writer, GList * source_list,
    gchar * res, gint * id);
static void save_timeline_objects (xmlTextWriterPtr writer, GList * list);
static void destroy_all (GList * list);
static void create_new_source_table (gchar * key, gchar * value,
    GHashTable * table);
static gboolean make_timeline_objects (GESFormatter * self);
void set_properties (GObject * obj, GHashTable * props_table);
void make_source (GList * ref_list,
    GHashTable * source_table, GESFormatter * self);

void layers_table_destroyer (gpointer data, gpointer data2, void *unused);
void list_table_destroyer (gpointer data, gpointer data2, void *unused);
void destroyer (gpointer data, gpointer data2, void *unused);
void ultimate_table_destroyer (gpointer data, gpointer data2, void *unused);
static void
track_object_added_cb (GESTimelineObject * object,
    GESTrackObject * track_object, GHashTable * props_table);

struct _GESPitiviFormatterPrivate
{
  gint not_done;
  xmlXPathContextPtr xpathCtx;
  GHashTable *source_table, *track_objects_table, *timeline_objects_table,
      *layers_table;
  GESTimeline *timeline;
  gboolean parsed;
  GESTrack *tracka, *trackv;
  GESTimelineTestSource *background;
};

static void
ges_pitivi_formatter_class_init (GESPitiviFormatterClass * klass)
{
  GESFormatterClass *formatter_klass;
  GObjectClass *object_class;
  object_class = G_OBJECT_CLASS (klass);
  formatter_klass = GES_FORMATTER_CLASS (klass);
  g_type_class_add_private (klass, sizeof (GESPitiviFormatterPrivate));

  formatter_klass->save_to_uri = save_pitivi_timeline_to_uri;
  formatter_klass->load_from_uri = load_pitivi_file_from_uri;
  object_class->finalize = ges_pitivi_formatter_finalize;
}

static void
ges_pitivi_formatter_init (GESPitiviFormatter * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_PITIVI_FORMATTER, GESPitiviFormatterPrivate);

  self->priv->not_done = 0;

  self->priv->track_objects_table =
      g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  self->priv->timeline_objects_table =
      g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  self->priv->layers_table =
      g_hash_table_new_full (g_int_hash, g_str_equal, NULL, NULL);

  self->priv->parsed = FALSE;
}

static void
ges_pitivi_formatter_finalize (GObject * object)
{
  GESPitiviFormatter *self = GES_PITIVI_FORMATTER (object);
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;

  if (priv->source_table != NULL) {
    g_hash_table_foreach (priv->source_table,
        (GHFunc) ultimate_table_destroyer, NULL);
    g_hash_table_destroy (priv->source_table);
  }

  if (priv->timeline_objects_table != NULL) {
    g_hash_table_foreach (priv->timeline_objects_table,
        (GHFunc) list_table_destroyer, NULL);
    g_hash_table_destroy (priv->timeline_objects_table);
  }

  if (priv->layers_table != NULL) {
    g_hash_table_foreach (priv->layers_table,
        (GHFunc) layers_table_destroyer, NULL);
    g_hash_table_destroy (priv->layers_table);
  }

  if (priv->track_objects_table != NULL) {
    g_hash_table_foreach (priv->track_objects_table,
        (GHFunc) ultimate_table_destroyer, NULL);
    g_hash_table_destroy (priv->track_objects_table);
  }

  G_OBJECT_CLASS (ges_pitivi_formatter_parent_class)->finalize (object);
}

static gboolean
save_pitivi_timeline_to_uri (GESFormatter * pitivi_formatter,
    GESTimeline * timeline, const gchar * uri)
{
  xmlTextWriterPtr writer;
  GList *list = NULL, *layers = NULL, *tmp = NULL;
  writer = xmlNewTextWriterFilename (uri, 0);

  xmlTextWriterSetIndent (writer, 1);
  xmlTextWriterStartElement (writer, BAD_CAST "pitivi");

  layers = ges_timeline_get_layers (timeline);
  xmlTextWriterStartElement (writer, BAD_CAST "factories");
  xmlTextWriterStartElement (writer, BAD_CAST "sources");
  for (tmp = layers; tmp; tmp = tmp->next) {

    /* 99 is the priority of the background source. */
    if (ges_timeline_layer_get_priority (tmp->data) != 99) {
      list = save_sources (tmp->data, writer);
    }
  }
  xmlTextWriterEndElement (writer);
  xmlTextWriterEndElement (writer);
  save_tracks (timeline, writer, list);
  save_timeline_objects (writer, list);
  xmlTextWriterEndDocument (writer);
  xmlFreeTextWriter (writer);

  g_list_free (layers);
  g_list_foreach (list, (GFunc) destroy_all, NULL);
  g_list_free (list);

  return TRUE;
}

static gboolean
load_pitivi_file_from_uri (GESFormatter * self,
    GESTimeline * timeline, const gchar * uri)
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
  priv->timeline = timeline;
  g_object_set (layer, "priority", (gint32) 0, NULL);

  if (!ges_timeline_add_layer (timeline, layer)) {
    GST_ERROR ("Couldn't add layer");
    return FALSE;
  }

  if (!(doc = create_doc (uri))) {
    GST_ERROR ("The xptv file for uri %s was badly formed or did not exist",
        uri);
    return FALSE;
  }

  priv->xpathCtx = xmlXPathNewContext (doc);

  if (!create_tracks (self)) {
    GST_ERROR ("Couldn't create tracks");
    ret = FALSE;
    goto fail;
  }

  priv->source_table = list_sources (self);

  if (!parse_timeline_objects (self)) {
    GST_ERROR ("Couldn't find timeline objects markup in the xptv file");
    ret = FALSE;
    goto fail;
  }

  if (!parse_track_objects (self)) {
    GST_ERROR ("Couldn't find track objects markup in the xptv file");
    ret = FALSE;
  }

  if (!make_timeline_objects (self))
    ret = FALSE;

fail:
  xmlXPathFreeContext (priv->xpathCtx);
  xmlFreeDoc (doc);
  return ret;
}

static void
save_timeline_objects (xmlTextWriterPtr writer, GList * list)
{
  GList *tmp;
  gint n_objects, i;

  xmlTextWriterStartElement (writer, BAD_CAST "timeline-objects");

  for (tmp = list; tmp; tmp = tmp->next) {

    GList *elem;
    xmlChar *cast;

    xmlTextWriterStartElement (writer, BAD_CAST "timeline-object");
    elem = tmp->data;
    xmlTextWriterStartElement (writer, BAD_CAST "factory-ref");
    cast = g_list_first (elem)->data;
    xmlTextWriterWriteAttribute (writer, BAD_CAST "id", BAD_CAST cast);
    xmlTextWriterEndElement (writer);
    xmlTextWriterStartElement (writer, BAD_CAST "track-object-refs");

    n_objects = g_list_length (elem) - 4;
    for (i = 0; i < n_objects; i++) {
      xmlTextWriterStartElement (writer, BAD_CAST "track-object-ref");
      xmlTextWriterWriteAttribute (writer, BAD_CAST "id",
          BAD_CAST (gchar *) (g_list_nth (elem, (guint) 4 + i)->data));
      xmlTextWriterEndElement (writer);
    }
    xmlTextWriterEndElement (writer);
    xmlTextWriterEndElement (writer);
  }
  xmlTextWriterEndElement (writer);
}

static GList *
save_sources (GESTimelineLayer * layer, xmlTextWriterPtr writer)
{
  GList *objects, *tmp;
  GHashTable *source_table;

  GList *source_list = NULL;
  int id = 1;
  objects = ges_timeline_layer_get_objects (layer);
  source_table =
      g_hash_table_new_full (g_str_hash, g_int_equal, g_free, g_free);

  for (tmp = objects; tmp; tmp = tmp->next) {
    GList *ref_type_list = NULL;
    GESTimelineObject *object;
    gchar *tfs_uri;
    xmlChar *cast;
    object = tmp->data;

    if GES_IS_TIMELINE_FILE_SOURCE
      (object) {

      tfs_uri = (gchar *) ges_timeline_filesource_get_uri
          (GES_TIMELINE_FILE_SOURCE (object));

      if (!g_hash_table_lookup (source_table, tfs_uri)) {
        cast = xmlXPathCastNumberToString (id);
        g_hash_table_insert (source_table, g_strdup (tfs_uri),
            g_strdup ((gchar *) cast));
        xmlFree (cast);
        xmlTextWriterStartElement (writer, BAD_CAST "source");
        xmlTextWriterWriteAttribute (writer, BAD_CAST "filename",
            BAD_CAST tfs_uri);
        cast = xmlXPathCastNumberToString (id);
        xmlTextWriterWriteAttribute (writer, BAD_CAST "id", BAD_CAST cast);
        xmlFree (cast);
        xmlTextWriterEndElement (writer);
        id++;
      }

      ref_type_list =
          g_list_append (ref_type_list,
          g_strdup (g_hash_table_lookup (source_table, tfs_uri)));
      ref_type_list = g_list_append (ref_type_list, object);
      ref_type_list = g_list_append (ref_type_list, g_strdup ("simple"));
      ref_type_list =
          g_list_append (ref_type_list,
          GINT_TO_POINTER (ges_timeline_layer_get_priority (layer)));
      source_list = g_list_append (source_list, g_list_copy (ref_type_list));
      g_list_free (ref_type_list);
      }
  }

  g_object_unref (G_OBJECT (layer));
  g_list_free (objects);
  g_hash_table_destroy (source_table);
  return source_list;
}

static void
save_tracks (GESTimeline * timeline, xmlTextWriterPtr writer,
    GList * source_list)
{
  GList *tracks, *tmp;

  gint id = 0;

  xmlTextWriterStartElement (writer, BAD_CAST "timeline");
  xmlTextWriterStartElement (writer, BAD_CAST "tracks");
  tracks = ges_timeline_get_tracks (timeline);

  for (tmp = tracks; tmp; tmp = tmp->next) {
    gchar *type, *caps, *res;
    GESTrack *track;
    GValue v = { 0 };
    track = GES_TRACK (tmp->data);
    xmlTextWriterStartElement (writer, BAD_CAST "track");
    xmlTextWriterStartElement (writer, BAD_CAST "stream");
    g_value_init (&v, GES_TYPE_TRACK_TYPE);
    g_object_get_property (G_OBJECT (track), "track-type", &v);
    type = gst_value_serialize (&v);
    caps = gst_caps_to_string (ges_track_get_caps (track));
    xmlTextWriterWriteAttribute (writer, BAD_CAST "caps", BAD_CAST caps);
    g_free (caps);

    if (!g_strcmp0 (type, "GES_TRACK_TYPE_AUDIO")) {
      xmlTextWriterWriteAttribute (writer, BAD_CAST "type",
          BAD_CAST "pitivi.stream.AudioStream");
      xmlTextWriterEndElement (writer);
      res = (gchar *) "audio";
    } else {
      xmlTextWriterWriteAttribute (writer, BAD_CAST "type",
          BAD_CAST "pitivi.stream.VideoStream");
      xmlTextWriterEndElement (writer);
      res = (gchar *) "video";
    }
    g_free (type);
    save_track_objects (writer, source_list, res, &id);
    xmlTextWriterEndElement (writer);
  }

  g_list_free (tracks);
  xmlTextWriterEndElement (writer);
}

static void
save_track_objects (xmlTextWriterPtr writer, GList * source_list, gchar * res,
    gint * id)
{
  GList *tmp, *tck_objs, *tmp_tck;
  gchar *bin_desc;
  xmlTextWriterStartElement (writer, BAD_CAST "track-objects");

  for (tmp = source_list; tmp; tmp = tmp->next) {
    GList *elem;
    GESTimelineObject *object;
    guint i, n, j;

    elem = tmp->data;
    object = g_list_next (elem)->data;
    tck_objs = ges_timeline_object_get_track_objects (object);

    for (tmp_tck = tck_objs; tmp_tck; tmp_tck = tmp_tck->next) {
      GParamSpec **properties;
      xmlChar *cast;
      gchar *prio_str;

      if (!ges_track_object_is_active (tmp_tck->data)) {
        continue;
      }

      if (((ges_track_object_get_track (tmp_tck->data)->type ==
                  GES_TRACK_TYPE_VIDEO)
              && (!g_strcmp0 (res, (gchar *) "video")))
          || ((ges_track_object_get_track (tmp_tck->data)->type ==
                  GES_TRACK_TYPE_AUDIO)
              && (!g_strcmp0 (res, (gchar *) "audio")))) {
      } else {
        continue;
      }

      xmlTextWriterStartElement (writer, BAD_CAST "track-object");
      cast =
          xmlXPathCastNumberToString (GPOINTER_TO_INT (g_list_nth (elem,
                  (guint) 3)->data));
      prio_str = g_strconcat ((gchar *) "(int)", (gchar *) cast, NULL);
      xmlFree (cast);
      xmlTextWriterWriteAttribute (writer, BAD_CAST "priority",
          BAD_CAST prio_str);
      g_free (prio_str);
      properties =
          g_object_class_list_properties (G_OBJECT_GET_CLASS (tmp_tck->data),
          &n);

      for (i = 0; i < n; i++) {
        GParamSpec *p = properties[i];
        GValue v = { 0 };
        gchar *serialized, *concatenated;

        if (!g_strcmp0 (p->name, (gchar *) "duration") ||
            !g_strcmp0 (p->name, (gchar *) "start") ||
            !g_strcmp0 (p->name, (gchar *) "in-point")) {
          g_value_init (&v, p->value_type);
          g_object_get_property (G_OBJECT (tmp_tck->data), p->name, &v);
          serialized = gst_value_serialize (&v);
          concatenated = g_strconcat ((gchar *) "(gint64)", serialized, NULL);

          if (!g_strcmp0 (p->name, (gchar *) "in-point")) {
            xmlTextWriterWriteAttribute (writer, BAD_CAST (gchar *) "in_point",
                BAD_CAST concatenated);
          } else {
            xmlTextWriterWriteAttribute (writer, BAD_CAST p->name,
                BAD_CAST concatenated);
          }
          g_free (concatenated);
          g_free (serialized);
        }
      }
      g_free (properties);
      cast = xmlXPathCastNumberToString (*id);
      xmlTextWriterWriteAttribute (writer, BAD_CAST "id", BAD_CAST cast);
      xmlFree (cast);

      if (GES_IS_TRACK_EFFECT (tmp_tck->data)) {
        GParamSpec **pspecs, *spec;
        gchar *serialized, *concatenated;
        guint n_props = 0;

        g_object_get (tmp_tck->data, "bin-description", &bin_desc, NULL);
        xmlTextWriterStartElement (writer, BAD_CAST "effect");
        xmlTextWriterStartElement (writer, BAD_CAST "factory");
        xmlTextWriterWriteAttribute (writer, BAD_CAST "name",
            BAD_CAST bin_desc);
        xmlTextWriterEndElement (writer);
        xmlTextWriterStartElement (writer, BAD_CAST "gst-element-properties");

        pspecs =
            ges_track_object_list_children_properties (tmp_tck->data, &n_props);

        j = 0;

        while (j < n_props) {
          GValue val = { 0 };
          spec = pspecs[j];
          g_value_init (&val, spec->value_type);
          ges_track_object_get_child_property_by_pspec (tmp_tck->data, spec,
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
        xmlTextWriterStartElement (writer, BAD_CAST "factory-ref");
        cast = g_list_first (elem)->data;
        xmlTextWriterWriteAttribute (writer, BAD_CAST "id", BAD_CAST cast);
      }
      xmlTextWriterEndElement (writer);
      xmlTextWriterEndElement (writer);

      if (GES_IS_TRACK_EFFECT (tmp_tck->data)) {
        elem = g_list_append (elem, xmlXPathCastNumberToString (*id));
      } else {
        elem = g_list_insert (elem, xmlXPathCastNumberToString (*id), 4);
      }
      *id = *id + 1;
    }
  }

  xmlTextWriterEndElement (writer);
}

static xmlDocPtr
create_doc (const gchar * uri)
{
  xmlDocPtr doc;
  doc = xmlParseFile (uri);
  return doc;
}

static GHashTable *
list_sources (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  xmlXPathObjectPtr xpathObj;
  GHashTable *table, *sources_table;
  int size, j;
  gchar *id;
  xmlNodeSetPtr nodes;

  sources_table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  xpathObj = xmlXPathEvalExpression ((const xmlChar *)
      "/pitivi/factories/sources/source", priv->xpathCtx);
  nodes = xpathObj->nodesetval;
  size = (nodes) ? nodes->nodeNr : 0;
  for (j = 0; j < size; ++j) {
    table = get_nodes_infos (nodes->nodeTab[j]);
    id = (gchar *) g_hash_table_lookup (table, (gchar *) "id");
    g_hash_table_insert (sources_table, g_strdup (id), table);
  }

  xmlXPathFreeObject (xpathObj);
  return sources_table;
}

static gboolean
make_timeline_objects (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  GHashTable *source_table;
  GESTimelineLayer *back_layer;
  gint i;
  gint *prio = malloc (sizeof (gint));

  GList *keys = NULL, *tmp = NULL, *ref_list = NULL;

  *prio = 0;

  priv->background = ges_timeline_test_source_new ();
  back_layer = ges_timeline_layer_new ();
  ges_timeline_layer_set_priority (back_layer, 99);
  if (!ges_timeline_add_layer (priv->timeline, back_layer)) {
    GST_ERROR ("Couldn't add layer");
    return FALSE;
  }

  if (!ges_timeline_layer_add_object (back_layer,
          GES_TIMELINE_OBJECT (priv->background))) {
    GST_ERROR ("Couldn't add background to the layer");
    return FALSE;
  }

  keys = g_hash_table_get_keys (priv->timeline_objects_table);

  for (tmp = keys, i = 1; tmp; tmp = tmp->next, i++) {
    if (i == g_list_length (keys))
      priv->parsed = TRUE;
    ref_list =
        g_hash_table_lookup (priv->timeline_objects_table, (gchar *) tmp->data);
    source_table =
        g_hash_table_lookup (priv->source_table, (gchar *) tmp->data);
    make_source (ref_list, source_table, self);
  }
  g_hash_table_insert (priv->layers_table, prio, back_layer);
  free (prio);

  g_list_free (keys);
  return TRUE;
}

void
make_source (GList * ref_list, GHashTable * source_table, GESFormatter * self)
{
  GHashTable *props_table, *effect_table;
  gchar **prio_array;
  GESTimelineLayer *layer;
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;

  gchar *fac_ref = NULL, *media_type = NULL, *filename = NULL;
  GList *tmp = NULL, *keys, *tmp_key;
  GESTimelineFileSource *src = NULL;
  gint cast_prio = 0;
  gint *prio = malloc (sizeof (gint));
  gboolean a_avail = FALSE, v_avail = FALSE, video = FALSE;

  for (tmp = ref_list; tmp; tmp = tmp->next) {
    props_table =
        g_hash_table_lookup (priv->track_objects_table, (gchar *) tmp->data);
    prio_array =
        g_strsplit ((gchar *) g_hash_table_lookup (props_table,
            (gchar *) "priority"), ")", 0);
    cast_prio = (gint) g_ascii_strtod (prio_array[1], NULL);
    *prio = cast_prio;
    fac_ref = (gchar *) g_hash_table_lookup (props_table, (gchar *) "fac_ref");
    media_type =
        (gchar *) g_hash_table_lookup (props_table, (gchar *) "media_type");

    g_strfreev (prio_array);

    if (!g_strcmp0 (media_type, (gchar *) "pitivi.stream.VideoStream"))
      video = TRUE;
    else
      video = FALSE;

    if (!(layer = g_hash_table_lookup (priv->layers_table, &cast_prio))) {
      layer = ges_timeline_layer_new ();
      g_object_set (layer, "auto-transition", TRUE, NULL);
      ges_timeline_layer_set_priority (layer, cast_prio);
      ges_timeline_add_layer (priv->timeline, layer);
      g_hash_table_insert (priv->layers_table, prio, layer);
      free (prio);
    }

    if (g_strcmp0 (fac_ref, (gchar *) "effect") && a_avail && (!video)) {
      a_avail = FALSE;
      g_signal_connect (src, "track-object-added",
          G_CALLBACK (track_object_added_cb), props_table);

    } else if (g_strcmp0 (fac_ref, (gchar *) "effect") && v_avail && (video)) {
      v_avail = FALSE;
      g_signal_connect (src, "track-object-added",
          G_CALLBACK (track_object_added_cb), props_table);

    } else if (g_strcmp0 (fac_ref, (gchar *) "effect")) {
      char cCurrentPath[FILENAME_MAX], *path;

      if (a_avail) {
        ges_timeline_filesource_set_supported_formats (src,
            GES_TRACK_TYPE_VIDEO);
      } else if (v_avail) {
        ges_timeline_filesource_set_supported_formats (src,
            GES_TRACK_TYPE_AUDIO);
      }

      filename =
          (gchar *) g_hash_table_lookup (source_table, (gchar *) "filename");
      path = GetCurrentDir (cCurrentPath, sizeof (cCurrentPath));

      if (!g_strcmp0 (filename, (gchar *) "/DJ5r3oNFVeE.flv") ||
          !g_strcmp0 (filename, (gchar *) "/a1Y73sPHKxw.flv")) {
        filename = g_strconcat ("file://", path, filename, NULL);
        src = ges_timeline_filesource_new (filename);
        g_free (filename);
      } else {
        src = ges_timeline_filesource_new (filename);
      }

      if (!video) {
        v_avail = TRUE;
        a_avail = FALSE;
      } else {
        a_avail = TRUE;
        v_avail = FALSE;
      }
      set_properties (G_OBJECT (src), props_table);
      ges_timeline_layer_add_object (layer, GES_TIMELINE_OBJECT (src));

    } else if (!g_strcmp0 (fac_ref, (gchar *) "effect")) {
      GESTrackParseLaunchEffect *effect;
      gchar *active = (gchar *)
          g_hash_table_lookup (props_table, (gchar *) "active");

      effect = ges_track_parse_launch_effect_new ((gchar *)
          g_hash_table_lookup (props_table, (gchar *) "effect_name"));
      effect_table =
          g_hash_table_lookup (props_table, (gchar *) "effect_props");

      ges_timeline_object_add_track_object (GES_TIMELINE_OBJECT (src),
          GES_TRACK_OBJECT (effect));

      if (!g_strcmp0 (active, (gchar *) "(bool)False"))
        ges_track_object_set_active (GES_TRACK_OBJECT (effect), FALSE);
      if (video)
        ges_track_add_object (priv->trackv, GES_TRACK_OBJECT (effect));
      else
        ges_track_add_object (priv->tracka, GES_TRACK_OBJECT (effect));
      keys = g_hash_table_get_keys (effect_table);

      for (tmp_key = keys; tmp_key; tmp_key = tmp_key->next) {
        gchar **value_array =
            g_strsplit ((gchar *) g_hash_table_lookup (effect_table,
                (gchar *) tmp_key->data),
            (gchar *) ")", (gint) 0);
        gchar *value = g_ascii_strdown (value_array[1], -1);

        if (!g_strcmp0 (value, (gchar *) "true"))
          ges_track_object_set_child_property (GES_TRACK_OBJECT (effect),
              (gchar *) tmp_key->data, TRUE, NULL);
        else if (!g_strcmp0 (value, (gchar *) "false"))
          ges_track_object_set_child_property (GES_TRACK_OBJECT (effect),
              (gchar *) tmp_key->data, FALSE, NULL);
        else if (!g_strcmp0 (value, (gchar *) "effect"))
          continue;
        else if (!g_strcmp0 ((gchar *) "(guint", value_array[0])
            || !g_strcmp0 ((gchar *) "(GEnum", value_array[0])
            || !g_strcmp0 ((gchar *) "(gint", value_array[0]))
          ges_track_object_set_child_property (GES_TRACK_OBJECT (effect),
              (gchar *) tmp_key->data, g_ascii_strtoll (value, NULL, 0), NULL);
        else
          ges_track_object_set_child_property (GES_TRACK_OBJECT (effect),
              (gchar *) tmp_key->data, g_ascii_strtod (value, NULL), NULL);
      }
    }
  }

  if (a_avail) {
    ges_timeline_filesource_set_supported_formats (src, GES_TRACK_TYPE_VIDEO);
  } else if (v_avail) {
    ges_timeline_filesource_set_supported_formats (src, GES_TRACK_TYPE_AUDIO);
  }
  free (prio);
}

void
set_properties (GObject * obj, GHashTable * props_table)
{
  gint i;
  gchar **prop_array;
  gint64 prop_value;

  gchar list[3][10] = { "duration", "in_point", "start" };

  for (i = 0; i < 3; i++) {
    prop_array =
        g_strsplit ((gchar *) g_hash_table_lookup (props_table, list[i]),
        (gchar *) ")", (gint) 0);
    prop_value = g_ascii_strtoll ((gchar *) prop_array[1], NULL, 0);
    g_object_set (obj, list[i], prop_value, NULL);
    g_strfreev (prop_array);
  }
}

static gboolean
parse_track_objects (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  xmlXPathObjectPtr xpathObj;
  xmlNodeSetPtr nodes;
  int size, j;
  gchar *id, *fac_ref;
  GHashTable *table = NULL, *new_table, *effect_table;
  xmlNode *ref_node;
  gchar *media_type;

  xpathObj = xmlXPathEvalExpression ((const xmlChar *)
      "/pitivi/timeline/tracks/track/track-objects/track-object",
      priv->xpathCtx);

  if (xpathObj == NULL) {
    xmlXPathFreeObject (xpathObj);
    return FALSE;
  }
  nodes = xpathObj->nodesetval;
  size = (nodes) ? nodes->nodeNr : 0;

  for (j = 0; j < size; ++j) {
    GHashTable *new_effect_table = NULL;
    table = get_nodes_infos (nodes->nodeTab[j]);
    id = (gchar *) g_hash_table_lookup (table, (gchar *) "id");
    ref_node = nodes->nodeTab[j]->children->next;
    fac_ref = (gchar *) xmlGetProp (ref_node, (xmlChar *) "id");
    new_table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

    if (!g_strcmp0 ((gchar *) ref_node->name, (gchar *) "effect")) {
      fac_ref = (gchar *) "effect";
      ref_node = ref_node->children->next;
      new_effect_table =
          g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
      g_hash_table_insert (table, g_strdup ((gchar *) "effect_name"),
          g_strdup ((gchar *) xmlGetProp (ref_node, (xmlChar *) "name")));
      effect_table = get_nodes_infos (ref_node->next->next);
      g_hash_table_foreach (effect_table, (GHFunc) create_new_source_table,
          new_effect_table);
    }

    g_hash_table_insert (table, g_strdup ((gchar *) "fac_ref"),
        g_strdup (fac_ref));
    media_type =
        (gchar *) xmlGetProp (nodes->nodeTab[j]->parent->prev->prev,
        (xmlChar *) "type");
    g_hash_table_insert (table, g_strdup ((gchar *) "media_type"),
        g_strdup (media_type));
    g_hash_table_foreach (table, (GHFunc) create_new_source_table, new_table);
    if (new_effect_table) {
      g_hash_table_insert (new_table, (gchar *) "effect_props",
          new_effect_table);
    }
    g_hash_table_insert (priv->track_objects_table, g_strdup (id), new_table);
    xmlFree (media_type);
    if (g_strcmp0 (fac_ref, (gchar *) "effect")) {
      xmlFree (fac_ref);
    }
    g_hash_table_foreach (table, (GHFunc) destroyer, NULL);
    g_hash_table_destroy (table);
  }

  xmlXPathFreeObject (xpathObj);
  return TRUE;
}

static gboolean
parse_timeline_objects (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  xmlXPathObjectPtr xpathObj;
  xmlNodeSetPtr nodes;
  int size, j;
  gchar *id, *ref;
  xmlChar *refxml;

  GList *ref_list = NULL, *tmp_list = NULL, *tmp = NULL;
  xmlNode *cur_node = NULL;
  xpathObj = xmlXPathEvalExpression ((const xmlChar *)
      "/pitivi/timeline/timeline-objects/timeline-object/factory-ref",
      priv->xpathCtx);

  if (xpathObj == NULL) {
    xmlXPathFreeObject (xpathObj);
    return FALSE;
  }

  nodes = xpathObj->nodesetval;
  size = (nodes) ? nodes->nodeNr : 0;

  for (j = 0; j < size; ++j) {
    cur_node = nodes->nodeTab[j];
    id = (gchar *) xmlGetProp (cur_node, (xmlChar *) "id");
    cur_node = cur_node->next->next->children->next;
    ref_list = NULL;
    for (cur_node = cur_node; cur_node; cur_node = cur_node->next->next) {

      refxml = xmlGetProp (cur_node, (xmlChar *) "id");
      ref = (gchar *) refxml;
      ref_list = g_list_append (ref_list, g_strdup (ref));
      xmlFree (refxml);
    }
    tmp_list = g_hash_table_lookup (priv->timeline_objects_table, id);
    if (tmp_list != NULL) {
      for (tmp = tmp_list; tmp; tmp = tmp->next) {
        ref_list = g_list_append (ref_list, tmp->data);
      }
    }
    g_hash_table_insert (priv->timeline_objects_table, g_strdup (id),
        g_list_copy (ref_list));
    xmlFree (id);
    g_list_free (ref_list);
    g_list_free (tmp_list);
    g_list_free (tmp);
  }
  xmlXPathFreeObject (xpathObj);
  return TRUE;
}

static void
create_new_source_table (gchar * key, gchar * value, GHashTable * table)
{
  g_hash_table_insert (table, g_strdup (key), g_strdup (value));
}

static gboolean
create_tracks (GESFormatter * self)
{
  GESPitiviFormatterPrivate *priv = GES_PITIVI_FORMATTER (self)->priv;
  GList *tracks = NULL;

  tracks = ges_timeline_get_tracks (priv->timeline);
  if (g_list_length (tracks)) {
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
    g_list_free (tracks);
    return TRUE;
  }

  priv->tracka = ges_track_audio_raw_new ();
  priv->trackv = ges_track_video_raw_new ();

  if (!ges_timeline_add_track (priv->timeline, priv->trackv)) {
    return FALSE;
  }

  if (!ges_timeline_add_track (priv->timeline, priv->tracka)) {
    return FALSE;
  }

  return TRUE;
}

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

static void
destroy_all (GList * list)
{
  g_free (g_list_nth (list, (guint) 2)->data);
  g_object_unref (G_OBJECT (g_list_nth (list, (guint) 1)->data));
  g_free (g_list_nth (list, (guint) 3)->data);

  if (g_list_length (list) == 6) {
    g_free (g_list_nth (list, (guint) 5)->data);
  }

  g_free (g_list_nth (list, (guint) 0)->data);
  g_list_free (list);
}

static void
track_object_added_cb (GESTimelineObject * object,
    GESTrackObject * track_object, GHashTable * props_table)
{
  gchar *media_type = NULL;
  GList *tck_objs = NULL, *tmp = NULL;
  GESTrack *object_track;
  gint64 start, duration;
  gboolean has_effect = FALSE;
  gint type = 0;
  tck_objs = ges_timeline_object_get_track_objects (object);
  media_type =
      (gchar *) g_hash_table_lookup (props_table, (gchar *) "media_type");

  for (tmp = tck_objs; tmp; tmp = tmp->next) {
    object_track = ges_track_object_get_track (tmp->data);
    if (GES_IS_TRACK_PARSE_LAUNCH_EFFECT (tmp->data)) {
      has_effect = TRUE;
      continue;
    }
    if ((!g_strcmp0 (media_type, "pitivi.stream.VideoStream")
            && object_track->type == GES_TRACK_TYPE_VIDEO)
        || (!g_strcmp0 (media_type, "pitivi.stream.AudioStream")
            && object_track->type == GES_TRACK_TYPE_AUDIO)) {
      ges_track_object_set_locked (tmp->data, FALSE);
      set_properties (G_OBJECT (tmp->data), props_table);
      ges_track_object_set_locked (tmp->data, TRUE);
      type = object_track->type;
      g_object_get (tmp->data, "start", &start, "duration", &duration, NULL);
    }
  }

  if (has_effect) {
    tck_objs = ges_timeline_object_get_track_objects (object);
    for (tmp = tck_objs; tmp; tmp = tmp->next) {
      object_track = ges_track_object_get_track (tmp->data);
      if (GES_IS_TRACK_PARSE_LAUNCH_EFFECT (tmp->data)
          && (type == object_track->type)) {
        ges_track_object_set_locked (tmp->data, FALSE);
        g_object_set (tmp->data, "start", start, "duration", duration, NULL);
        ges_track_object_set_locked (tmp->data, TRUE);
      }
    }
  }
}

void
ultimate_table_destroyer (gpointer data, gpointer data2, void *unused)
{
  g_free (data);
  g_hash_table_foreach (data2, (GHFunc) destroyer, NULL);
  g_hash_table_destroy (data2);
}

void
layers_table_destroyer (gpointer data, gpointer data2, void *unused)
{
  g_object_unref (data2);
  g_free (data);
}

void
list_table_destroyer (gpointer data, gpointer data2, void *unused)
{
  g_list_foreach (data2, (GFunc) g_free, NULL);
  g_list_free (data2);
  g_free (data);
}

void
destroyer (gpointer data, gpointer data2, void *unused)
{
  if (!g_strcmp0 ((gchar *) data, (gchar *) "private")) {
  } else if (!g_strcmp0 ((gchar *) data, (gchar *) "effect_props")) {
    g_hash_table_foreach (data2, (GHFunc) destroyer, NULL);
    g_hash_table_destroy (data2);
  } else {
    g_free (data2);
    g_free (data);
  }
}

GESPitiviFormatter *
ges_pitivi_formatter_new (void)
{
  return g_object_new (GES_TYPE_PITIVI_FORMATTER, NULL);
}
