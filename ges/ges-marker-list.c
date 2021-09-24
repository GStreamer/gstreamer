/* GStreamer Editing Services

 * Copyright (C) <2019> Mathieu Duponchelle <mathieu@centricular.com>
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
 * SECTION: gesmarkerlist
 * @title: GESMarkerList
 * @short_description: implements a list of markers with metadata asociated to time positions
 * @see_also: #GESMarker
 *
 * A #GESMarker can be colored by setting the #GES_META_MARKER_COLOR meta.
 *
 * Since: 1.18
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-marker-list.h"
#include "ges.h"
#include "ges-internal.h"
#include "ges-meta-container.h"

static void ges_meta_container_interface_init (GESMetaContainerInterface *
    iface);

struct _GESMarker
{
  GObject parent;
  GstClockTime position;
};

G_DEFINE_TYPE_WITH_CODE (GESMarker, ges_marker, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER,
        ges_meta_container_interface_init));

enum
{
  PROP_MARKER_0,
  PROP_MARKER_POSITION,
  PROP_MARKER_LAST
};

static GParamSpec *marker_properties[PROP_MARKER_LAST];

/* GObject Standard vmethods*/
static void
ges_marker_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESMarker *marker = GES_MARKER (object);

  switch (property_id) {
    case PROP_MARKER_POSITION:
      g_value_set_uint64 (value, marker->position);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
ges_marker_init (GESMarker * self)
{
  ges_meta_container_register_static_meta (GES_META_CONTAINER (self),
      GES_META_READ_WRITE, GES_META_MARKER_COLOR, G_TYPE_UINT);
}

static void
ges_marker_class_init (GESMarkerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_marker_get_property;
  /**
   * GESMarker:position:
   *
   * Current position (in nanoseconds) of the #GESMarker
   *
   * Since: 1.18
   */
  marker_properties[PROP_MARKER_POSITION] =
      g_param_spec_uint64 ("position", "Position",
      "The position of the marker", 0, G_MAXUINT64,
      GST_CLOCK_TIME_NONE, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_MARKER_POSITION,
      marker_properties[PROP_MARKER_POSITION]);

}

static void
ges_meta_container_interface_init (GESMetaContainerInterface * iface)
{
}

/* GESMarkerList */

struct _GESMarkerList
{
  GObject parent;

  GSequence *markers;
  GHashTable *markers_iters;
  GESMarkerFlags flags;
};

enum
{
  PROP_MARKER_LIST_0,
  PROP_MARKER_LIST_FLAGS,
  PROP_MARKER_LIST_LAST
};

static GParamSpec *list_properties[PROP_MARKER_LIST_LAST];

enum
{
  MARKER_ADDED,
  MARKER_REMOVED,
  MARKER_MOVED,
  LAST_SIGNAL
};

static guint ges_marker_list_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GESMarkerList, ges_marker_list, G_TYPE_OBJECT);

static void
ges_marker_list_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESMarkerList *self = GES_MARKER_LIST (object);

  switch (property_id) {
    case PROP_MARKER_LIST_FLAGS:
      g_value_set_flags (value, self->flags);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
ges_marker_list_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESMarkerList *self = GES_MARKER_LIST (object);

  switch (property_id) {
    case PROP_MARKER_LIST_FLAGS:
      self->flags = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
remove_marker (gpointer data)
{
  GESMarker *marker = (GESMarker *) data;

  g_object_unref (marker);
}

static void
ges_marker_list_init (GESMarkerList * self)
{
  self->markers = g_sequence_new (remove_marker);
  self->markers_iters = g_hash_table_new (g_direct_hash, g_direct_equal);
}

static void
ges_marker_list_finalize (GObject * object)
{
  GESMarkerList *self = GES_MARKER_LIST (object);

  g_sequence_free (self->markers);
  g_hash_table_unref (self->markers_iters);

  G_OBJECT_CLASS (ges_marker_list_parent_class)->finalize (object);
}

static void
ges_marker_list_class_init (GESMarkerListClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ges_marker_list_finalize;
  object_class->get_property = ges_marker_list_get_property;
  object_class->set_property = ges_marker_list_set_property;

/**
  * GESMarkerList:flags:
  *
  * Flags indicating how markers on the list should be treated.
  *
  * Since: 1.20
  */
  list_properties[PROP_MARKER_LIST_FLAGS] =
      g_param_spec_flags ("flags", "Flags",
      "Functionalities the marker list should be used for",
      GES_TYPE_MARKER_FLAGS, GES_MARKER_FLAG_NONE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (object_class, PROP_MARKER_LIST_FLAGS,
      list_properties[PROP_MARKER_LIST_FLAGS]);

/**
  * GESMarkerList::marker-added:
  * @marker-list: the #GESMarkerList
  * @position: the position of the added marker
  * @marker: the #GESMarker that was added.
  *
  * Will be emitted after the marker was added to the marker-list.
  * Since: 1.18
  */
  ges_marker_list_signals[MARKER_ADDED] =
      g_signal_new ("marker-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, G_TYPE_UINT64, GES_TYPE_MARKER);

/**
  * GESMarkerList::marker-removed:
  * @marker_list: the #GESMarkerList
  * @marker: the #GESMarker that was removed.
  *
  * Will be emitted after the marker was removed the marker-list.
  * Since: 1.18
  */
  ges_marker_list_signals[MARKER_REMOVED] =
      g_signal_new ("marker-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_MARKER);

/**
  * GESMarkerList::marker-moved:
  * @marker_list: the #GESMarkerList
  * @previous_position: the previous position of the marker
  * @new_position: the new position of the marker
  * @marker: the #GESMarker that was moved.
  *
  * Will be emitted after the marker was moved to.
  * Since: 1.18
  */
  ges_marker_list_signals[MARKER_MOVED] =
      g_signal_new ("marker-moved", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 3, G_TYPE_UINT64, G_TYPE_UINT64, GES_TYPE_MARKER);
}

static gint
cmp_marker (gconstpointer a, gconstpointer b, G_GNUC_UNUSED gpointer data)
{
  GESMarker *marker_a = (GESMarker *) a;
  GESMarker *marker_b = (GESMarker *) b;

  if (marker_a->position < marker_b->position)
    return -1;
  else if (marker_a->position == marker_b->position)
    return 0;
  else
    return 1;
}

/**
 * ges_marker_list_new:
 *
 * Creates a new #GESMarkerList.

 * Returns: A new #GESMarkerList
 * Since: 1.18
 */
GESMarkerList *
ges_marker_list_new (void)
{
  GESMarkerList *ret;

  ret = (GESMarkerList *) g_object_new (GES_TYPE_MARKER_LIST, NULL);

  return ret;
}

/**
 * ges_marker_list_add:
 * @position: The position of the new marker
 *
 * Returns: (transfer none): The newly-added marker, the list keeps ownership
 * of the marker
 * Since: 1.18
 */
GESMarker *
ges_marker_list_add (GESMarkerList * self, GstClockTime position)
{
  GESMarker *ret;
  GSequenceIter *iter;

  g_return_val_if_fail (GES_IS_MARKER_LIST (self), NULL);

  ret = (GESMarker *) g_object_new (GES_TYPE_MARKER, NULL);

  ret->position = position;

  iter = g_sequence_insert_sorted (self->markers, ret, cmp_marker, NULL);

  g_hash_table_insert (self->markers_iters, ret, iter);

  g_signal_emit (self, ges_marker_list_signals[MARKER_ADDED], 0, position, ret);

  return ret;
}

/**
 * ges_marker_list_size:
 *
 * Returns: The number of markers in @list
 * Since: 1.18
 */
guint
ges_marker_list_size (GESMarkerList * self)
{
  g_return_val_if_fail (GES_IS_MARKER_LIST (self), 0);

  return g_sequence_get_length (self->markers);
}

/**
 * ges_marker_list_remove:
 *
 * Removes @marker from @list, this decreases the refcount of the
 * marker by 1.
 *
 * Returns: %TRUE if the marker could be removed, %FALSE otherwise
 *   (if the marker was not present in the list for example)
 * Since: 1.18
 */
gboolean
ges_marker_list_remove (GESMarkerList * self, GESMarker * marker)
{
  GSequenceIter *iter;
  gboolean ret = FALSE;

  g_return_val_if_fail (GES_IS_MARKER_LIST (self), FALSE);

  if (!g_hash_table_lookup_extended (self->markers_iters,
          marker, NULL, (gpointer *) & iter))
    goto done;
  g_assert (iter != NULL);
  g_hash_table_remove (self->markers_iters, marker);

  g_signal_emit (self, ges_marker_list_signals[MARKER_REMOVED], 0, marker);

  g_sequence_remove (iter);

  ret = TRUE;

done:
  return ret;
}

/**
 * ges_marker_list_get_markers:
 *
 * Returns: (element-type GESMarker) (transfer full): a #GList
 * of the #GESMarker within the GESMarkerList. The user will have
 * to unref each #GESMarker and free the #GList.
 *
 * Since: 1.18
 */
GList *
ges_marker_list_get_markers (GESMarkerList * self)
{
  GESMarker *marker;
  GSequenceIter *iter;
  GList *ret;

  g_return_val_if_fail (GES_IS_MARKER_LIST (self), NULL);
  ret = NULL;

  for (iter = g_sequence_get_begin_iter (self->markers);
      !g_sequence_iter_is_end (iter); iter = g_sequence_iter_next (iter)) {
    marker = GES_MARKER (g_sequence_get (iter));

    ret = g_list_append (ret, g_object_ref (marker));
  }

  return ret;
}

/*
 * ges_marker_list_get_closest:
 * @position: The position which we want to find the closest marker to
 *
 * Returns: (transfer full): The marker found to be the closest
 * to the given position. If two markers are at equal distance from position,
 * the "earlier" one will be returned.
 */
GESMarker *
ges_marker_list_get_closest (GESMarkerList * self, GstClockTime position)
{
  GESMarker *new_marker, *ret = NULL;
  GstClockTime distance_next, distance_prev;
  GSequenceIter *iter;

  if (g_sequence_is_empty (self->markers))
    goto done;

  new_marker = (GESMarker *) g_object_new (GES_TYPE_MARKER, NULL);
  new_marker->position = position;
  iter = g_sequence_search (self->markers, new_marker, cmp_marker, NULL);
  g_object_unref (new_marker);

  if (g_sequence_iter_is_begin (iter)) {
    /* We know the sequence isn't empty, this is safe */
    ret = g_sequence_get (iter);
  } else if (g_sequence_iter_is_end (iter)) {
    /* We know the sequence isn't empty, this is safe */
    ret = g_sequence_get (g_sequence_iter_prev (iter));
  } else {
    GESMarker *next_marker, *prev_marker;

    prev_marker = g_sequence_get (g_sequence_iter_prev (iter));
    next_marker = g_sequence_get (iter);

    distance_next = next_marker->position - position;
    distance_prev = position - prev_marker->position;

    ret = distance_prev <= distance_next ? prev_marker : next_marker;
  }

done:
  if (ret)
    return g_object_ref (ret);
  return NULL;
}

/**
 * ges_marker_list_move:
 *
 * Moves a @marker in a @list to a new @position
 *
 * Returns: %TRUE if the marker could be moved, %FALSE otherwise
 *   (if the marker was not present in the list for example)
 *
 * Since: 1.18
 */
gboolean
ges_marker_list_move (GESMarkerList * self, GESMarker * marker,
    GstClockTime position)
{
  GSequenceIter *iter;
  gboolean ret = FALSE;
  GstClockTime previous_position;

  g_return_val_if_fail (GES_IS_MARKER_LIST (self), FALSE);

  if (!g_hash_table_lookup_extended (self->markers_iters,
          marker, NULL, (gpointer *) & iter)) {
    GST_WARNING ("GESMarkerList doesn't contain GESMarker");
    goto done;
  }

  previous_position = marker->position;
  marker->position = position;

  g_signal_emit (self, ges_marker_list_signals[MARKER_MOVED], 0,
      previous_position, position, marker);

  g_sequence_sort_changed (iter, cmp_marker, NULL);

  ret = TRUE;

done:
  return ret;
}

gboolean
ges_marker_list_deserialize (GValue * dest, const gchar * s)
{
  gboolean ret = FALSE;
  GstCaps *caps = NULL;
  GESMarkerList *list = ges_marker_list_new ();
  guint caps_len, i = 0;
  gsize string_len;
  gchar *escaped, *caps_str;
  GstStructure *data_s;
  gint flags;

  string_len = strlen (s);
  if (G_UNLIKELY (*s != '"' || string_len < 2 || s[string_len - 1] != '"')) {
    /* "\"" is not an accepted string, so len must be at least 2 */
    GST_ERROR ("Failed deserializing marker list: expected string to start "
        "and end with '\"'");
    goto done;
  }
  escaped = g_strdup (s + 1);
  escaped[string_len - 2] = '\0';
  /* removed trailing '"' */
  caps_str = g_strcompress (escaped);
  g_free (escaped);

  caps = gst_caps_from_string (caps_str);
  g_free (caps_str);
  if (G_UNLIKELY (caps == NULL)) {
    GST_ERROR ("Failed deserializing marker list: could not extract caps");
    goto done;
  }

  caps_len = gst_caps_get_size (caps);
  if (G_UNLIKELY (caps_len == 0)) {
    GST_DEBUG ("Got empty caps: %s", s);
    goto done;
  }

  data_s = gst_caps_get_structure (caps, i);
  if (gst_structure_has_name (data_s, "marker-list-flags")) {
    if (!gst_structure_get_int (data_s, "flags", &flags)) {
      GST_ERROR_OBJECT (dest,
          "Failed deserializing marker list: unexpected structure %"
          GST_PTR_FORMAT, data_s);
      goto done;
    }

    list->flags = flags;
    i += 1;
  }

  if (G_UNLIKELY ((caps_len - i) % 2)) {
    GST_ERROR ("Failed deserializing marker list: incomplete marker caps");
  }

  for (; i < caps_len - 1; i += 2) {
    const GstStructure *pos_s = gst_caps_get_structure (caps, i);
    const GstStructure *meta_s = gst_caps_get_structure (caps, i + 1);
    GstClockTime position;
    GESMarker *marker;
    gchar *metas;

    if (!gst_structure_has_name (pos_s, "marker-times")) {
      GST_ERROR_OBJECT (dest,
          "Failed deserializing marker list: unexpected structure %"
          GST_PTR_FORMAT, pos_s);
      goto done;
    }

    if (!gst_structure_get_uint64 (pos_s, "position", &position)) {
      GST_ERROR_OBJECT (dest,
          "Failed deserializing marker list: unexpected structure %"
          GST_PTR_FORMAT, pos_s);
      goto done;
    }

    marker = ges_marker_list_add (list, position);

    metas = gst_structure_to_string (meta_s);
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER (marker),
        metas);
    g_free (metas);
  }

  ret = TRUE;

done:
  if (caps)
    gst_caps_unref (caps);

  if (!ret)
    g_object_unref (list);
  else
    g_value_take_object (dest, list);

  return ret;
}

gchar *
ges_marker_list_serialize (const GValue * v)
{
  GESMarkerList *list = GES_MARKER_LIST (g_value_get_object (v));
  GSequenceIter *iter;
  GstCaps *caps = gst_caps_new_empty ();
  gchar *caps_str, *escaped, *res;
  GstStructure *s;

  s = gst_structure_new ("marker-list-flags", "flags", G_TYPE_INT,
      list->flags, NULL);
  gst_caps_append_structure (caps, s);

  iter = g_sequence_get_begin_iter (list->markers);

  while (!g_sequence_iter_is_end (iter)) {
    GESMarker *marker = (GESMarker *) g_sequence_get (iter);
    gchar *metas;

    metas = ges_meta_container_metas_to_string (GES_META_CONTAINER (marker));

    s = gst_structure_new ("marker-times", "position", G_TYPE_UINT64,
        marker->position, NULL);
    gst_caps_append_structure (caps, s);
    s = gst_structure_from_string (metas, NULL);
    gst_caps_append_structure (caps, s);

    g_free (metas);

    iter = g_sequence_iter_next (iter);
  }

  caps_str = gst_caps_to_string (caps);
  escaped = g_strescape (caps_str, NULL);
  g_free (caps_str);
  res = g_strdup_printf ("\"%s\"", escaped);
  g_free (escaped);
  gst_caps_unref (caps);

  return res;
}
