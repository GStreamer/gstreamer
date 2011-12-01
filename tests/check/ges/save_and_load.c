/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
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

#include <ges/ges.h>
#include <gst/check/gstcheck.h>
#include <string.h>

#define KEY_FILE_START {\
  if (cmp) g_key_file_free (cmp);\
  cmp = g_key_file_new ();\
}

#define KEY(group, key, value) \
  g_key_file_set_value (cmp, group, key, value)

#define COMPARE fail_unless(compare (cmp, formatter, timeline))

static gboolean
compare (GKeyFile * cmp, GESFormatter * formatter, GESTimeline * timeline)
{
  gchar *data, *fmt_data;
  gsize length;
  gboolean result = TRUE;

  data = g_key_file_to_data (cmp, &length, NULL);
  ges_formatter_save (formatter, timeline);
  fmt_data = ges_formatter_get_data (formatter, &length);

  if (!(g_strcmp0 (data, fmt_data) == 0)) {
    GST_ERROR ("difference between expected and output");
    GST_ERROR ("expected: \n%s", data);
    GST_ERROR ("actual: \n%s", fmt_data);
    result = FALSE;
  }
  g_free (data);
  return result;
}

GST_START_TEST (test_keyfile_save)
{
  GESTimeline *timeline;
  GESTimelineLayer *layer, *layer2;
  GESTrack *track;
  GESTimelineObject *source;
  GESFormatter *formatter;
  GKeyFile *cmp = NULL;

  ges_init ();

  /* setup timeline */

  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  /* create serialization object */

  GST_DEBUG ("creating a keyfile formatter");
  formatter = GES_FORMATTER (ges_keyfile_formatter_new ());

  /* add a layer and make sure it's serialized */

  GST_DEBUG ("Create a layer");
  layer = GES_TIMELINE_LAYER (ges_simple_timeline_layer_new ());
  fail_unless (layer != NULL);

  GST_DEBUG ("Add the layer to the timeline");
  fail_unless (ges_timeline_add_layer (timeline, layer));

  KEY_FILE_START;
  KEY ("General", "version", "1");
  KEY ("Layer0", "priority", "0");
  KEY ("Layer0", "type", "simple");
  COMPARE;

  /* add a track and make sure it's serialized */

  GST_DEBUG ("Create a Track");
  track = ges_track_audio_raw_new ();
  fail_unless (track != NULL);

  GST_DEBUG ("Add the track to the timeline");
  fail_unless (ges_timeline_add_track (timeline, track));

  KEY_FILE_START;
  KEY ("General", "version", "1");
  KEY ("Track0", "type", "GES_TRACK_TYPE_AUDIO");
  KEY ("Track0", "caps", "audio/x-raw-int; audio/x-raw-float");
  KEY ("Layer0", "priority", "0");
  KEY ("Layer0", "type", "simple");
  COMPARE;

  /* add sources */

  GST_DEBUG ("Adding first source");
  source = (GESTimelineObject *) ges_timeline_test_source_new ();
  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (layer),
      source, -1);
  g_object_set (G_OBJECT (source), "duration", (guint64) 2 * GST_SECOND, NULL);

  KEY ("Object0", "type", "GESTimelineTestSource");
  KEY ("Object0", "start", "0");
  KEY ("Object0", "in-point", "0");
  KEY ("Object0", "duration", "2000000000");
  KEY ("Object0", "priority", "2");
  KEY ("Object0", "supported-formats", "GES_TRACK_TYPE_UNKNOWN");
  KEY ("Object0", "mute", "false");
  KEY ("Object0", "vpattern", "100% Black");
  KEY ("Object0", "freq", "440");
  KEY ("Object0", "volume", "0");
  COMPARE;

  GST_DEBUG ("Adding transition");
  source = (GESTimelineObject *)
      ges_timeline_standard_transition_new_for_nick ((gchar *) "bar-wipe-lr");

  g_object_set (G_OBJECT (source), "duration", (guint64) GST_SECOND / 2, NULL);
  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (layer),
      source, -1);

  KEY ("Object1", "type", "GESTimelineStandardTransition");
  KEY ("Object1", "start", "1500000000");
  KEY ("Object1", "in-point", "0");
  KEY ("Object1", "duration", "500000000");
  KEY ("Object1", "priority", "1");
  KEY ("Object1", "supported-formats", "GES_TRACK_TYPE_UNKNOWN");
  KEY ("Object1", "vtype", "A bar moves from left to right");
  COMPARE;

  GST_DEBUG ("Adding second source");
  source = (GESTimelineObject *) ges_timeline_test_source_new ();
  g_object_set (G_OBJECT (source), "duration", (guint64) 2 * GST_SECOND, NULL);
  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (layer),
      source, -1);

  KEY ("Object2", "type", "GESTimelineTestSource");
  KEY ("Object2", "start", "1500000000");
  KEY ("Object2", "in-point", "0");
  KEY ("Object2", "duration", "2000000000");
  KEY ("Object2", "priority", "3");
  KEY ("Object2", "supported-formats", "GES_TRACK_TYPE_UNKNOWN");
  KEY ("Object2", "mute", "false");
  KEY ("Object2", "vpattern", "100% Black");
  KEY ("Object2", "freq", "440");
  KEY ("Object2", "volume", "0");
  COMPARE;

  /* add a second layer to the timeline */

  GST_DEBUG ("Adding a second layer to the timeline");
  layer2 = ges_timeline_layer_new ();
  ges_timeline_layer_set_priority (layer2, 1);
  fail_unless (layer != NULL);
  fail_unless (ges_timeline_add_layer (timeline, layer2));

  KEY ("Layer1", "priority", "1");
  KEY ("Layer1", "type", "default");
  COMPARE;

  GST_DEBUG ("Adding a few more sources");
  source = (GESTimelineObject *) ges_timeline_title_source_new ();
  g_object_set (G_OBJECT (source),
      "duration", (guint64) GST_SECOND,
      "start", (guint64) 5 * GST_SECOND, "text", "the quick brown fox", NULL);
  fail_unless (ges_timeline_layer_add_object (layer2, source));

  KEY ("Object3", "type", "GESTimelineTitleSource");
  KEY ("Object3", "start", "5000000000");
  KEY ("Object3", "in-point", "0");
  KEY ("Object3", "duration", "1000000000");
  KEY ("Object3", "priority", "0");
  KEY ("Object3", "supported-formats", "GES_TRACK_TYPE_UNKNOWN");
  KEY ("Object3", "mute", "false");
  KEY ("Object3", "text", "\"the\\\\ quick\\\\ brown\\\\ fox\"");
  KEY ("Object3", "font-desc", "\"Serif\\\\ 36\"");
  KEY ("Object3", "halignment", "center");
  KEY ("Object3", "valignment", "baseline");
  KEY ("Object3", "color", "4294967295");
  KEY ("Object3", "xpos", "0.5");
  KEY ("Object3", "ypos", "0.5");
  COMPARE;

  /* tear-down */
  g_key_file_free (cmp);


  GST_DEBUG ("Removing layer from the timeline");
  fail_unless (ges_timeline_remove_layer (timeline, layer));
  fail_unless (ges_timeline_remove_layer (timeline, layer2));

  GST_DEBUG ("Removing track from the timeline");
  g_object_ref (track);
  fail_unless (ges_timeline_remove_track (timeline, track));
  fail_unless (ges_track_get_timeline (track) == NULL);
  ASSERT_OBJECT_REFCOUNT (track, "track", 1);
  g_object_unref (track);

  ASSERT_OBJECT_REFCOUNT (timeline, "timeline", 1);

  g_object_unref (timeline);
  g_object_unref (formatter);
}

GST_END_TEST;

/* do action for every item and then free the list */

#define g_list_free_all(list)				\
  {							\
    g_list_foreach(list, (GFunc) g_object_unref, NULL);	\
    g_list_free(list);					\
  }

/* print out a helpful error message when a comparison fails. Works with the
 * TIMELINE_COMPARE_*, LAYER*, SIMPLE_LAYER*, abd TRACK, macros below to give
 * information about the source line where the failing object was created.
 */

#define CMP_FAIL(obj, ...) \
{\
  gchar *file, *func;\
  guint line;\
  file = g_object_get_data (G_OBJECT(obj), "file");\
  func = g_object_get_data (G_OBJECT(obj), "function");\
  line = GPOINTER_TO_UINT(g_object_get_data (G_OBJECT(obj), "line"));\
  gst_debug_log (GST_CAT_DEFAULT, GST_LEVEL_ERROR, file, func,\
      line, G_OBJECT(obj), __VA_ARGS__);\
}

/* compare two GObjects for equality. pointer identity and GType short-circuit
 * the comparison. If a and b are not identical pointers and of the same
 * GType, compares every readable property for equality using
 * g_param_values_cmp.
 */

static gboolean
ges_objs_equal (GObject * a, GObject * b)
{
  GType at;
  GObjectClass *klass;
  GParamSpec **props = NULL, **iter = NULL;
  guint n_props, i;
  guint ret = FALSE;
  gchar *typename;

  GST_DEBUG ("comparing %s (%p) and %s (%p)\n",
      G_OBJECT_TYPE_NAME (a), a, G_OBJECT_TYPE_NAME (b), b);

  if (a == b)
    return TRUE;

  at = G_TYPE_FROM_INSTANCE (a);

  fail_unless (at == G_TYPE_FROM_INSTANCE (b));

  typename = (gchar *) g_type_name (at);

  /* compare every readable property */

  klass = G_OBJECT_GET_CLASS (a);
  props = g_object_class_list_properties (klass, &n_props);

  for (i = 0, iter = props; i < n_props; i++, iter++) {
    GValue av = { 0 }
    , bv = {
    0};

    /* ignore name and layer properties */
    if (!g_strcmp0 ("name", (*iter)->name) ||
        !g_strcmp0 ("layer", (*iter)->name))
      continue;

    /* special case caps property */
    if (!g_strcmp0 ("caps", (*iter)->name)) {
      GstCaps *acaps, *bcaps;

      g_object_get (a, "caps", &acaps, NULL);
      g_object_get (b, "caps", &bcaps, NULL);
      if (gst_caps_is_equal (acaps, bcaps)) {
        gst_caps_unref (acaps);
        gst_caps_unref (bcaps);
        continue;
      } else {
        gst_caps_unref (acaps);
        gst_caps_unref (bcaps);
        CMP_FAIL (b, "%s's %p and %p differ by property caps", a, b);
        goto fail;
      }
    }

    g_value_init (&av, (*iter)->value_type);
    g_value_init (&bv, (*iter)->value_type);

    if (!((*iter)->flags & G_PARAM_READABLE))
      continue;

    g_object_get_property (a, (*iter)->name, &av);
    g_object_get_property (b, (*iter)->name, &bv);

    if (g_param_values_cmp (*iter, &av, &bv) != 0) {
      const gchar *a_str, *b_str;

      a_str = gst_value_serialize (&av);
      b_str = gst_value_serialize (&bv);

      CMP_FAIL (b, "%s's %p and %p differ by property %s (%s != %s)",
          typename, a, b, (*iter)->name, a_str, b_str);

      goto fail;
    }

    g_value_unset (&av);
    g_value_unset (&bv);
  }

  ret = TRUE;

fail:
  if (props)
    g_free (props);
  return ret;
}

static gboolean
ges_tracks_equal (GESTrack * a, GESTrack * b)
{
  return ges_objs_equal (G_OBJECT (a), G_OBJECT (b));
}

static gboolean
ges_layers_equal (GESTimelineLayer * a, GESTimelineLayer * b)
{
  GList *a_objs = NULL, *b_objs = NULL, *a_iter, *b_iter;
  gboolean ret = FALSE;
  guint i;

  if (!ges_objs_equal (G_OBJECT (a), G_OBJECT (b)))
    return FALSE;

  a_objs = ges_timeline_layer_get_objects (a);
  b_objs = ges_timeline_layer_get_objects (b);

  /* one shortcoming of this procedure is that the objects need to be stored
   * in the same order. Not sure if this is a problem in practice */

  for (i = 0, a_iter = a_objs, b_iter = b_objs; a_iter && b_iter; a_iter =
      a_iter->next, b_iter = b_iter->next, i++) {
    if (!ges_objs_equal (a_iter->data, b_iter->data)) {
      CMP_FAIL (b, "layers %p and %p differ by obj at position %d", a, b, i);
      goto fail;
    }
  }

  if (a_iter || b_iter) {
    CMP_FAIL (b, "layers %p and %p have differing number of objects", a, b);
    goto fail;
  }

  ret = TRUE;

fail:

  g_list_free_all (a_objs);
  g_list_free_all (b_objs);

  return ret;
}

static gboolean
ges_timelines_equal (GESTimeline * a, GESTimeline * b)
{
  GList *a_tracks, *b_tracks, *a_iter, *b_iter, *a_layers, *b_layers;

  gboolean ret = FALSE;
  guint i;

  if (!ges_objs_equal (G_OBJECT (a), G_OBJECT (b))) {
    CMP_FAIL (b, "%p and %p are not of the same type");
    return FALSE;
  }

  a_tracks = ges_timeline_get_tracks (a);
  b_tracks = ges_timeline_get_tracks (b);
  a_layers = ges_timeline_get_layers (a);
  b_layers = ges_timeline_get_layers (b);

  /* one shortcoming of this procedure is that the objects need to be stored
   * in the same order. Not sure if this is a problem in practice */

  for (i = 0, a_iter = a_tracks, b_iter = b_tracks; a_iter && b_iter; a_iter =
      a_iter->next, b_iter = b_iter->next, i++) {
    if (!ges_tracks_equal (a_iter->data, b_iter->data)) {
      CMP_FAIL (b, "GESTimelines %p and %p differ by tracks at position %d", a,
          b, i);
      goto fail;
    }
  }

  if (a_iter || b_iter) {
    CMP_FAIL (b, "GESTimelines %p and %p have differing number of tracks", a,
        b);
    goto fail;
  }

  for (i = 0, a_iter = a_layers, b_iter = b_layers; a_iter && b_iter; a_iter =
      a_iter->next, b_iter = b_iter->next, i++) {
    if (!ges_layers_equal (a_iter->data, b_iter->data)) {
      goto fail;
    }
  }

  if (a_iter || b_iter) {
    CMP_FAIL (b, "GESTimelines %p and %p have differing numbre of layers", a,
        b);
    goto fail;
  }

  ret = TRUE;

fail:

  g_list_free_all (a_tracks);
  g_list_free_all (b_tracks);
  g_list_free_all (a_layers);
  g_list_free_all (b_layers);

  return ret;
}

#define TIMELINE_BEGIN(location) \
{\
  GESTimeline **a, *b;\
  a = &(location);\
  if (*a) g_object_unref (*a);\
  b = ges_timeline_new();\
  *a = b;\

#define TIMELINE_END }

#define TIMELINE_COMPARE(a, b)\
{\
  fail_unless (ges_timelines_equal(a, b));\
}

#define TRACK(type, caps) \
{\
  GESTrack *trk;\
  GstCaps *c;\
  c = gst_caps_from_string(caps);\
  trk = ges_track_new (type, c);\
  ges_timeline_add_track (b, trk);\
  g_object_set_data(G_OBJECT(trk),"file", (void *) __FILE__);\
  g_object_set_data(G_OBJECT(trk),"line", (void *) __LINE__);\
  g_object_set_data(G_OBJECT(trk),"function", (void *) GST_FUNCTION);\
}

#define LAYER_BEGIN(priority) \
{\
  GESTimelineLayer *l;\
  l = ges_timeline_layer_new ();\
  ges_timeline_add_layer (b, l);\
  ges_timeline_layer_set_priority (l, priority);\
  g_object_set_data(G_OBJECT(l),"file", (void *) __FILE__);\
  g_object_set_data(G_OBJECT(l),"line", (void *) __LINE__);\
  g_object_set_data(G_OBJECT(l),"function", (void *) GST_FUNCTION);

#define LAYER_END \
}

#define LAYER_OBJECT(type, ...) \
{\
  GESTimelineObject *obj;\
  obj = GES_TIMELINE_OBJECT(\
        g_object_new ((type), __VA_ARGS__, NULL));\
  ges_timeline_layer_add_object (l, obj);\
  g_object_set_data(G_OBJECT(obj),"file", (void *) __FILE__);\
  g_object_set_data(G_OBJECT(obj),"line", (void *) __LINE__);\
  g_object_set_data(G_OBJECT(obj),"function", (void *) GST_FUNCTION);\
}

#define SIMPLE_LAYER_BEGIN(priority) \
{\
  GESSimpleTimelineLayer *l;\
  l = ges_simple_timeline_layer_new ();\
  ges_timeline_add_layer (b, GES_TIMELINE_LAYER(l));\
  ges_timeline_layer_set_priority(GES_TIMELINE_LAYER(l), priority);\
  g_object_set_data(G_OBJECT(l),"file", (void *) __FILE__);\
  g_object_set_data(G_OBJECT(l),"line", (void *) __LINE__);\
  g_object_set_data(G_OBJECT(l),"function", (void *) GST_FUNCTION);

#define SIMPLE_LAYER_OBJECT(type, position, ...) \
{\
  GESTimelineObject *obj;\
  obj = GES_TIMELINE_OBJECT(\
        g_object_new ((type), __VA_ARGS__, NULL));\
  ges_simple_timeline_layer_add_object (l, obj, position);\
  g_object_set_data(G_OBJECT(obj),"file", (void *) __FILE__);\
  g_object_set_data(G_OBJECT(obj),"line", (void *) __LINE__);\
  g_object_set_data(G_OBJECT(obj),"function", (void *) GST_FUNCTION);\
}

/* */
static const gchar *data = "\n[General]\n"
    "[Track0]\n"
    "type=GES_TRACK_TYPE_AUDIO\n"
    "caps=audio/x-raw-int; audio/x-raw-float\n"
    "\n"
    "[Layer0]\n"
    "priority=0\n"
    "type=simple\n"
    "\n"
    "[Object0]\n"
    "type=GESTimelineTestSource\n"
    "start=0\n"
    "in-point=0\n"
    "duration=2000000000\n"
    "priority=2\n"
    "mute=false\n"
    "vpattern=100% Black\n"
    "freq=440\n"
    "volume=0\n"
    "\n"
    "[Object1]\n"
    "type=GESTimelineStandardTransition\n"
    "start=1500000000\n"
    "in-point=0\n"
    "duration=500000000\n"
    "priority=1\n"
    "vtype=A bar moves from left to right\n"
    "\n"
    "[Object2]\n"
    "type=GESTimelineTestSource\n"
    "start=1500000000\n"
    "in-point=0\n"
    "duration=2000000000\n"
    "priority=2\n"
    "mute=false\n"
    "vpattern=100% Black\n"
    "freq=440\n"
    "volume=0\n"
    "\n"
    "[Layer1]\n"
    "priority=1\n"
    "type=default\n"
    "\n"
    "[Object3]\n"
    "type=GESTimelineTitleSource\n"
    "start=5000000000\n"
    "in-point=0\n"
    "duration=1000000000\n"
    "priority=2\n"
    "mute=false\n"
    "text=\"the\\\\ quick\\\\ brown\\\\ fox\"\n"
    "font-desc=\"Serif\\\\ 36\"\n"
    "halignment=center\n" "valignment=baseline\n";

GST_START_TEST (test_keyfile_load)
{
  GESTimeline *timeline = NULL, *expected = NULL;
  GESFormatter *formatter;

  ges_init ();

  /* setup timeline */

  GST_DEBUG ("Create a timeline");
  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  /* create serialization object */

  GST_DEBUG ("creating a default formatter");
  formatter = GES_FORMATTER (ges_keyfile_formatter_new ());

  ges_formatter_set_data (formatter, g_strdup (data), strlen (data));

  fail_unless (ges_formatter_load (formatter, timeline));

  TIMELINE_BEGIN (expected) {

    TRACK (GES_TRACK_TYPE_AUDIO, "audio/x-raw-float; audio/x-raw-int");

    SIMPLE_LAYER_BEGIN (0) {

      SIMPLE_LAYER_OBJECT ((GES_TYPE_TIMELINE_TEST_SOURCE), -1,
          "duration", (guint64) 2 * GST_SECOND);

      SIMPLE_LAYER_OBJECT ((GES_TYPE_TIMELINE_STANDARD_TRANSITION), -1,
          "duration", (guint64) GST_SECOND / 2,
          "vtype", GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_LR);

      SIMPLE_LAYER_OBJECT ((GES_TYPE_TIMELINE_TEST_SOURCE), -1,
          "duration", (guint64) 2 * GST_SECOND);

    } LAYER_END;

    LAYER_BEGIN (1) {

      LAYER_OBJECT (GES_TYPE_TIMELINE_TITLE_SOURCE,
          "start", (guint64) 5 * GST_SECOND,
          "duration", (guint64) GST_SECOND, "priority", 2, "text",
          "the quick brown fox");

    } LAYER_END;

  } TIMELINE_END;

  TIMELINE_COMPARE (timeline, expected);

  /* tear-down */
  g_object_unref (formatter);
  g_object_unref (timeline);
  g_object_unref (expected);
}

GST_END_TEST;

GST_START_TEST (test_keyfile_identity)
{

  /* we will create several timelines. they will first be serialized, then
   * deseriailzed and compared against the original. */

  GESTimeline *orig = NULL, *serialized = NULL;
  GESFormatter *formatter;

  ges_init ();

  formatter = GES_FORMATTER (ges_keyfile_formatter_new ());

  TIMELINE_BEGIN (orig) {

    TRACK (GES_TRACK_TYPE_AUDIO, "audio/x-raw-int,width=32,rate=8000");
    TRACK (GES_TRACK_TYPE_VIDEO, "video/x-raw-rgb");

    LAYER_BEGIN (5) {

      LAYER_OBJECT (GES_TYPE_TIMELINE_TEXT_OVERLAY,
          "start", (guint64) GST_SECOND,
          "duration", (guint64) 2 * GST_SECOND,
          "priority", 1,
          "text", "Hello, world!",
          "font-desc", "Sans 9",
          "halignment", GES_TEXT_HALIGN_LEFT,
          "valignment", GES_TEXT_VALIGN_TOP);

      LAYER_OBJECT (GES_TYPE_TIMELINE_TEST_SOURCE,
          "start", (guint64) 0,
          "duration", (guint64) 5 * GST_SECOND,
          "priority", 2,
          "freq", (gdouble) 500,
          "volume", 1.0, "vpattern", GES_VIDEO_TEST_PATTERN_WHITE);

      LAYER_OBJECT (GES_TYPE_TIMELINE_TEXT_OVERLAY,
          "start", (guint64) 7 * GST_SECOND,
          "duration", (guint64) 2 * GST_SECOND,
          "priority", 2,
          "text", "Hello, world!",
          "font-desc", "Sans 9",
          "halignment", GES_TEXT_HALIGN_LEFT,
          "valignment", GES_TEXT_VALIGN_TOP);

      LAYER_OBJECT (GES_TYPE_TIMELINE_TEST_SOURCE,
          "start", (guint64) 6 * GST_SECOND,
          "duration", (guint64) 5 * GST_SECOND,
          "priority", 3,
          "freq", (gdouble) 600,
          "volume", 1.0, "vpattern", GES_VIDEO_TEST_PATTERN_RED);

    } LAYER_END;

  } TIMELINE_END;

  serialized = ges_timeline_new ();

  ges_formatter_save (formatter, orig);
  ges_formatter_load (formatter, serialized);

  TIMELINE_COMPARE (serialized, orig);

  g_object_unref (formatter);
  g_object_unref (serialized);
  g_object_unref (orig);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-save-load");
  TCase *tc_chain = tcase_create ("basic");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_keyfile_save);
  tcase_add_test (tc_chain, test_keyfile_load);
  tcase_add_test (tc_chain, test_keyfile_identity);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ges_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
