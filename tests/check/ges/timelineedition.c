/* GStreamer Editing Services
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "test-utils.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

static gboolean
my_fill_track_func (GESClip * object,
    GESTrackObject * trobject, GstElement * gnlobj, gpointer user_data)
{
  GstElement *src;

  GST_DEBUG ("timelineobj:%p, trackobjec:%p, gnlobj:%p",
      object, trobject, gnlobj);

  /* Let's just put a fakesource in for the time being */
  src = gst_element_factory_make ("fakesrc", NULL);
  /* If this fails... that means that there already was something
   * in it */
  fail_unless (gst_bin_add (GST_BIN (gnlobj), src));

  return TRUE;
}

static inline GESClip *
create_custom_clip (void)
{
  return GES_CLIP (ges_custom_source_clip_new (my_fill_track_func, NULL));
}

#define CHECK_OBJECT_PROPS(obj, start, inpoint, duration) {\
  assert_equals_uint64 (_START (obj), start);\
  assert_equals_uint64 (_INPOINT (obj), inpoint);\
  assert_equals_uint64 (_DURATION (obj), duration);\
}

GST_START_TEST (test_basic_timeline_edition)
{
  GESTrack *track;
  GESTimeline *timeline;
  GESTrackObject *tckobj, *tckobj1, *tckobj2;
  GESClip *obj, *obj1, *obj2;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  fail_unless (ges_timeline_add_track (timeline, track));

  obj = create_custom_clip ();
  obj1 = create_custom_clip ();
  obj2 = create_custom_clip ();


  fail_unless (obj && obj1 && obj2);

  /**
   * Our timeline
   *
   * inpoints 0-------   0--------      0-----------
   *          |  obj  |  |  obj1  |     |     obj2  |
   * time     0------- 10 --------20    50---------60
   */
  g_object_set (obj, "start", (guint64) 0, "duration", (guint64) 10,
      "in-point", (guint64) 0, NULL);
  g_object_set (obj1, "start", (guint64) 10, "duration", (guint64) 10,
      "in-point", (guint64) 0, NULL);
  g_object_set (obj2, "start", (guint64) 50, "duration", (guint64) 60,
      "in-point", (guint64) 0, NULL);

  tckobj = ges_clip_create_track_object (obj, track->type);
  fail_unless (tckobj != NULL);
  fail_unless (ges_clip_add_track_object (obj, tckobj));
  fail_unless (ges_track_add_object (track, tckobj));
  assert_equals_uint64 (_DURATION (tckobj), 10);

  tckobj1 = ges_clip_create_track_object (obj1, track->type);
  fail_unless (tckobj1 != NULL);
  fail_unless (ges_clip_add_track_object (obj1, tckobj1));
  fail_unless (ges_track_add_object (track, tckobj1));
  assert_equals_uint64 (_DURATION (tckobj1), 10);

  tckobj2 = ges_clip_create_track_object (obj2, track->type);
  fail_unless (ges_clip_add_track_object (obj2, tckobj2));
  fail_unless (tckobj2 != NULL);
  fail_unless (ges_track_add_object (track, tckobj2));
  assert_equals_uint64 (_DURATION (tckobj2), 60);

  /**
   * Simple rippling obj to: 10
   *
   * New timeline:
   * ------------
   *
   * inpoints 0-------   0--------      0-----------
   *          |  obj  |  |  obj1  |     |   obj2    |
   * time    10------- 20 --------30    60---------120
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 10) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 10, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 60, 0, 60);


  /* FIXME find a way to check that we are using the same MovingContext
   * inside the GESTrack */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 40) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 10, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 40, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 80, 0, 60);

  /**
   * Rippling obj1 back to: 20 (getting to the exact same timeline as before
   */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 20) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 10, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 60, 0, 60);

  /**
   * Simple move obj to: 27 and obj2 to 35
   *
   * New timeline:
   * ------------
   *                    0------------
   * inpoints   0-------|---  obj 0--|----------
   *            |  obj1 27 -|-----|-37   obj2   |
   * time      20-----------30   35-------------120
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 27) == TRUE);
  fail_unless (ges_clip_edit (obj2, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 35) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 27, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 35, 0, 60);

  /**
   * Trim start obj to: 32 and obj2 to 35
   *
   * New timeline:
   * ------------
   *                           5--------
   * inpoints   0-----------   | obj 0--|----------
   *            |  obj1     |  32----|-37   obj2   |
   * time      20-----------30      35-------------120
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 32) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 5);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 35, 0, 60);

  /* Ripple end obj to 42
   * New timeline:
   * ------------
   *                           5--------
   * inpoints   0-----------   | obj 0--|----------
   *            |  obj1     |  32----|-42   obj2   |
   * time      20-----------30      35-------------120
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 42) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 10);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 35, 0, 60);

  /**
   * New timeline:
   * ------------
   * inpoints 0-------     5-------- 0-----------
   *          |  obj1 |    |  obj1  ||  obj2    |
   * time    20-------30  32--------52 ---------112
   */
  fail_unless (ges_clip_edit (obj2, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 42) == TRUE);
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 52) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 20);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 52, 0, 60);

  /**
   * New timeline:
   * ------------
   * inpoints 0-------     5-------- 0------------
   *          |  obj1 |    |  obj   ||    obj2    |
   * time    20-------40  42--------62 ---------122
   */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 40) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 42, 5, 20);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 20);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /**
   * New timeline:
   * ------------
   * inpoints 0------- 0-------- 0-----------
   *          |  obj1 ||   obj  ||  obj2     |
   * time    20------ 25 ------ 62 ---------122
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 40) == TRUE);
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_START, 25) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /* Make sure that not doing anything when not able to roll */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_START, 65) == TRUE);
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 65) == TRUE, 0);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  g_object_unref (timeline);
  g_object_unref (obj);
  g_object_unref (obj1);
  g_object_unref (obj2);
}

GST_END_TEST;

GST_START_TEST (test_snapping)
{
  GESTrack *track;
  GESTimeline *timeline;
  GESTrackObject *tckobj, *tckobj1, *tckobj2;
  GESClip *obj, *obj1, *obj2;
  GESTimelineLayer *layer;
  GList *tckobjs;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  fail_unless (ges_timeline_add_track (timeline, track));

  obj = create_custom_clip ();
  obj1 = create_custom_clip ();
  obj2 = create_custom_clip ();

  fail_unless (obj && obj1 && obj2);

  /**
   * Our timeline
   * ------------
   * inpoints 0------- 0-------- 0-----------
   *          |  obj1 ||   obj  ||  obj2     |
   * time    20------ 25 ------ 62 ---------122
   */
  g_object_set (obj, "start", (guint64) 25, "duration", (guint64) 37,
      "in-point", (guint64) 0, NULL);
  g_object_set (obj1, "start", (guint64) 20, "duration", (guint64) 15,
      "in-point", (guint64) 0, NULL);
  g_object_set (obj2, "start", (guint64) 62, "duration", (guint64) 60,
      "in-point", (guint64) 0, NULL);

  fail_unless ((layer = ges_timeline_append_layer (timeline)) != NULL);
  assert_equals_int (ges_timeline_layer_get_priority (layer), 0);


  fail_unless (ges_timeline_layer_add_object (layer, obj));
  fail_unless ((tckobjs = ges_clip_get_track_objects (obj)) != NULL);
  fail_unless ((tckobj = GES_TRACK_OBJECT (tckobjs->data)) != NULL);
  fail_unless (ges_track_object_get_track (tckobj) == track);
  assert_equals_uint64 (_DURATION (tckobj), 37);
  g_list_free_full (tckobjs, g_object_unref);

  /* We have 3 references to tckobj from:
   *  track + timeline + obj */
  ASSERT_OBJECT_REFCOUNT (tckobj, "First tckobj", 3);
  /* We have 1 ref to obj1:
   * + layer */
  ASSERT_OBJECT_REFCOUNT (obj, "First clip", 1);

  fail_unless (ges_timeline_layer_add_object (layer, obj1));
  fail_unless ((tckobjs = ges_clip_get_track_objects (obj1)) != NULL);
  fail_unless ((tckobj1 = GES_TRACK_OBJECT (tckobjs->data)) != NULL);
  fail_unless (ges_track_object_get_track (tckobj1) == track);
  assert_equals_uint64 (_DURATION (tckobj1), 15);
  g_list_free_full (tckobjs, g_object_unref);

  /* Same ref logic */
  ASSERT_OBJECT_REFCOUNT (tckobj1, "First tckobj", 3);
  ASSERT_OBJECT_REFCOUNT (obj1, "First clip", 1);

  fail_unless (ges_timeline_layer_add_object (layer, obj2));
  fail_unless ((tckobjs = ges_clip_get_track_objects (obj2)) != NULL);
  fail_unless ((tckobj2 = GES_TRACK_OBJECT (tckobjs->data)) != NULL);
  fail_unless (ges_track_object_get_track (tckobj2) == track);
  assert_equals_uint64 (_DURATION (tckobj2), 60);
  g_list_free_full (tckobjs, g_object_unref);

  /* Same ref logic */
  ASSERT_OBJECT_REFCOUNT (tckobj2, "First tckobj", 3);
  ASSERT_OBJECT_REFCOUNT (obj2, "First clip", 1);

  /* Snaping to edge, so no move */
  g_object_set (timeline, "snapping-distance", (guint64) 3, NULL);
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /* Snaping to edge, so no move */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /**
   * New timeline:
   * ------------
   *                    0----------- 0-------------
   * inpoints   0-------|--   obj   ||   obj2      |
   *            |  obj1 25-|------- 62 -----------122
   * time      20----------30
   */
  g_object_set (timeline, "snapping-distance", (guint64) 0, NULL);
  ges_timeline_element_set_duration (GES_TIMELINE_ELEMENT (obj1), 10);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /**
   * New timeline(the "layers" are just to help reading diagram, nothing else):
   * ------------
   *                    0----------
   *                    |   obj    |
   *                    25---------62
   * inpoints   0----------------------- 10--------
   *            |       obj1            ||  obj2   |
   * time      20---------------------- 72 --------122
   */
  /* Rolling involves only neighbour that are currently snapping */
  fail_unless (ges_timeline_element_roll_end (GES_TIMELINE_ELEMENT (obj1), 62));
  fail_unless (ges_timeline_element_roll_end (GES_TIMELINE_ELEMENT (obj1),
          72) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 52);
  CHECK_OBJECT_PROPS (tckobj2, 72, 10, 50);

  /**
   *                    0----------
   *                    |   obj    |
   *                    25---------62
   * inpoints           5--------------- 10--------
   *                    |     obj1      ||  obj2   |
   * time               25------------- 72 --------122
   */
  g_object_set (timeline, "snapping-distance", (guint64) 4, NULL);
  fail_unless (ges_timeline_element_trim (GES_TIMELINE_ELEMENT (obj1),
          28) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 25, 5, 47);
  CHECK_OBJECT_PROPS (tckobj2, 72, 10, 50);

  /**
   *                    0----------
   *                    |   obj    |
   *                    25---------62
   * inpoints           5---------- 0---------
   *                    |  obj1    ||  obj2   |
   * time               25-------- 62 --------122
   */
  fail_unless (ges_timeline_element_roll_start (GES_TIMELINE_ELEMENT (obj2),
          59) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 25, 5, 37);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

   /**
   * inpoints           0----------5---------- 0----------
   *                    |   obj    ||  obj1    ||  obj2   |
   * time               25---------62-------- 99 --------170
   */
  fail_unless (ges_timeline_element_ripple (GES_TIMELINE_ELEMENT (obj1),
          58) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 62, 5, 37);
  CHECK_OBJECT_PROPS (tckobj2, 99, 0, 60);

  /**
   * inpoints     0----------5----------     0----------
   *              |   obj    ||  obj1    |   |  obj2    |
   * time         25---------62-------- 99  110--------170
   */
  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (obj2), 110);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 62, 5, 37);
  CHECK_OBJECT_PROPS (tckobj2, 110, 0, 60);

  /**
   * inpoints     0----------5    5 --------- 0----------
   *              |   obj    |    |  obj1    ||  obj2    |
   * time         25---------62   73---------110--------170
   */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 72) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 73, 5, 37);
  CHECK_OBJECT_PROPS (tckobj2, 110, 0, 60);

  /**
   * inpoints     0----------5----------     0----------
   *              |   obj    ||  obj1    |   |  obj2    |
   * time         25---------62-------- 99  110--------170
   */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 58) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 62, 5, 37);
  CHECK_OBJECT_PROPS (tckobj2, 110, 0, 60);


  /**
   * inpoints     0----------5---------- 0----------
   *              |   obj    ||  obj1   ||  obj2    |
   * time         25---------62--------110--------170
   */
  g_object_set (obj1, "duration", 46, NULL);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 62, 5, 48);
  CHECK_OBJECT_PROPS (tckobj2, 110, 0, 60);

  /**
   * inpoints     5----------- 0--------- 0----------
   *              |   obj1    ||  obj2   ||  obj     |
   * time         62---------110--------170--------207
   */
  g_object_set (obj, "start", 168, NULL);
  CHECK_OBJECT_PROPS (tckobj, 170, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 62, 5, 48);
  CHECK_OBJECT_PROPS (tckobj2, 110, 0, 60);

  /* Check we didn't lose/screwed any references */
  ASSERT_OBJECT_REFCOUNT (tckobj, "First tckobj", 3);
  ASSERT_OBJECT_REFCOUNT (tckobj1, "Second tckobj", 3);
  ASSERT_OBJECT_REFCOUNT (tckobj2, "Third tckobj", 3);
  ASSERT_OBJECT_REFCOUNT (obj, "First clip", 1);
  ASSERT_OBJECT_REFCOUNT (obj1, "Second clip", 1);
  ASSERT_OBJECT_REFCOUNT (obj2, "Third clip", 1);

  g_object_unref (timeline);

  /* Check we destroyed everything */
  fail_if (G_IS_OBJECT (tckobj));
  fail_if (G_IS_OBJECT (tckobj1));
  fail_if (G_IS_OBJECT (tckobj2));
  fail_if (G_IS_OBJECT (obj));
  fail_if (G_IS_OBJECT (obj1));
  fail_if (G_IS_OBJECT (obj2));
  fail_if (G_IS_OBJECT (layer));
}

GST_END_TEST;

GST_START_TEST (test_timeline_edition_mode)
{
  guint i;
  GESTrack *track;
  GESTimeline *timeline;
  GESTrackObject *tckobj, *tckobj1, *tckobj2;
  GESClip *obj, *obj1, *obj2;
  GESTimelineLayer *layer, *layer1, *layer2;
  GList *tckobjs, *layers, *tmp;

  ges_init ();

  track = ges_track_new (GES_TRACK_TYPE_CUSTOM, gst_caps_ref (GST_CAPS_ANY));
  fail_unless (track != NULL);

  timeline = ges_timeline_new ();
  fail_unless (timeline != NULL);

  fail_unless (ges_timeline_add_track (timeline, track));

  obj = create_custom_clip ();
  obj1 = create_custom_clip ();
  obj2 = create_custom_clip ();

  fail_unless (obj && obj1 && obj2);

  /**
   * Our timeline
   *
   *          0-------
   * layer:   |  obj  |
   *          0-------10
   *
   *                   0--------     0-----------
   * layer1:           |  obj1  |    |     obj2  |
   *                  10--------20   50---------60
   */
  g_object_set (obj, "start", (guint64) 0, "duration", (guint64) 10,
      "in-point", (guint64) 0, NULL);
  g_object_set (obj1, "start", (guint64) 10, "duration", (guint64) 10,
      "in-point", (guint64) 0, NULL);
  g_object_set (obj2, "start", (guint64) 50, "duration", (guint64) 60,
      "in-point", (guint64) 0, NULL);

  fail_unless ((layer = ges_timeline_append_layer (timeline)) != NULL);
  assert_equals_int (ges_timeline_layer_get_priority (layer), 0);


  fail_unless (ges_timeline_layer_add_object (layer, obj));
  fail_unless ((tckobjs = ges_clip_get_track_objects (obj)) != NULL);
  fail_unless ((tckobj = GES_TRACK_OBJECT (tckobjs->data)) != NULL);
  fail_unless (ges_track_object_get_track (tckobj) == track);
  assert_equals_uint64 (_DURATION (tckobj), 10);
  g_list_free_full (tckobjs, g_object_unref);

  /* Add a new layer and add objects to it */
  fail_unless ((layer1 = ges_timeline_append_layer (timeline)) != NULL);
  fail_unless (layer != layer1);
  assert_equals_int (ges_timeline_layer_get_priority (layer1), 1);

  fail_unless (ges_timeline_layer_add_object (layer1, obj1));
  fail_unless ((tckobjs = ges_clip_get_track_objects (obj1)) != NULL);
  fail_unless ((tckobj1 = GES_TRACK_OBJECT (tckobjs->data)) != NULL);
  fail_unless (ges_track_object_get_track (tckobj1) == track);
  assert_equals_uint64 (_DURATION (tckobj1), 10);
  g_list_free_full (tckobjs, g_object_unref);

  fail_unless (ges_timeline_layer_add_object (layer1, obj2));
  fail_unless ((tckobjs = ges_clip_get_track_objects (obj2)) != NULL);
  fail_unless ((tckobj2 = GES_TRACK_OBJECT (tckobjs->data)) != NULL);
  fail_unless (ges_track_object_get_track (tckobj2) == track);
  assert_equals_uint64 (_DURATION (tckobj2), 60);
  g_list_free_full (tckobjs, g_object_unref);

  /**
   * Simple rippling obj to: 10
   *
   * New timeline:
   * ------------
   *
   * inpoints 0-------
   *          |  obj  |
   * time    10-------20
   *
   *                   0--------      0-----------
   *                   |  obj1  |     |   obj2    |
   *                  20--------30    60--------120
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 10) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 10, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 60, 0, 60);


  /* FIXME find a way to check that we are using the same MovingContext
   * inside the GESTimeline */
  fail_unless (ges_clip_edit (obj1, NULL, 3, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 40) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 10, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 40, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 80, 0, 60);
  layer2 = ges_clip_get_layer (obj1);
  assert_equals_int (ges_timeline_layer_get_priority (layer2), 3);
  /* obj2 should have moved layer too */
  fail_unless (ges_clip_get_layer (obj2) == layer2);
  /* We got 2 reference to the same object, unref them */
  g_object_unref (layer2);
  g_object_unref (layer2);

  /**
   * Rippling obj1 back to: 20 (getting to the exact same timeline as before
   */
  fail_unless (ges_clip_edit (obj1, NULL, 1, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_NONE, 20) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 10, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 60, 0, 60);
  layer2 = ges_clip_get_layer (obj1);
  assert_equals_int (ges_timeline_layer_get_priority (layer2), 1);
  /* obj2 should have moved layer too */
  fail_unless (ges_clip_get_layer (obj2) == layer2);
  /* We got 2 reference to the same object, unref them */
  g_object_unref (layer2);
  g_object_unref (layer2);

  /**
   * Simple move obj to 27 and obj2 to 35
   *
   * New timeline:
   * ------------
   *
   * inpoints 0-------
   *          |  obj  |
   * time    27-------37
   *
   *                   0--------   0-----------
   *                   |  obj1  |  |   obj2    |
   *                  20--------30 35---------95
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 27) == TRUE);
  fail_unless (ges_clip_edit (obj2, NULL, -1, GES_EDIT_MODE_NORMAL,
          GES_EDGE_NONE, 35) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 27, 0, 10);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 35, 0, 60);

  /**
   * Simple trimming start obj to: 32
   *
   * New timeline:
   * ------------
   *
   *                      5-------
   * layer 0:             |  obj  |
   *                     32-------37
   *
   *               0--------      0-----------
   * layer 1       |  obj1  |     |   obj2    |
   *              20--------30    35---------95
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 32) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 5);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 35, 0, 60);

  /* Ripple end obj to 35 and move to layer 2
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 1:   |  obj1  |         |   obj2    |
   *            20--------30       35---------95
   *
   *                        5------
   * layer 2:               |  obj |
   *                       32------35
   */
  fail_unless (ges_clip_edit (obj, NULL, 2, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 35) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 3);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 35, 0, 60);
  layer = ges_clip_get_layer (obj);
  assert_equals_int (ges_timeline_layer_get_priority (layer), 2);
  g_object_unref (layer);

  /* Roll end obj to 50
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 1:   |  obj1  |         |   obj2    |
   *            20--------30       50---------95
   *
   *                        5------
   * layer 2:               |  obj |
   *                       32------50
   */
  fail_unless (ges_clip_edit (obj, NULL, 2, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 50) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 18);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 50, 15, 45);
  layer = ges_clip_get_layer (obj);
  assert_equals_int (ges_timeline_layer_get_priority (layer), 2);
  g_object_unref (layer);

  /* Some more intensive roll testing */
  for (i = 0; i < 20; i++) {
    gint32 random = g_random_int_range (35, 94);
    guint64 tck3_inpoint = random - 35;

    fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_ROLL,
            GES_EDGE_END, random) == TRUE);
    CHECK_OBJECT_PROPS (tckobj, 32, 5, random - 32);
    CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
    CHECK_OBJECT_PROPS (tckobj2, random, tck3_inpoint, 95 - random);
  }

  /* Roll end obj back to 35
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 1:   |  obj1  |         |   obj2    |
   *            20--------30       35---------95
   *
   *                        5------
   * layer 2:               |  obj |
   *                       32------35
   */
  fail_unless (ges_clip_edit (obj, NULL, 2, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 35) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 3);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 35, 0, 60);
  layer = ges_clip_get_layer (obj);
  assert_equals_int (ges_timeline_layer_get_priority (layer), 2);
  g_object_unref (layer);

  /* Ripple obj end to 52
   * New timeline:
   * ------------
   *
   *            0--------          0----------
   * layer 1:   |  obj1  |         |   obj2   |
   *            20-------30       52---------112
   *
   *                        5------
   * layer 2:               |  obj |
   *                       32------52
   *
   */
  /* Can not move to the first layer as obj2 should move to a layer with priority < 0 */
  fail_unless (ges_clip_edit (obj, NULL, 0, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 52) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 32, 5, 20);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 52, 0, 60)
      layer = ges_clip_get_layer (obj);
  assert_equals_int (ges_timeline_layer_get_priority (layer), 2);
  g_object_unref (layer);


  /* Little check that we have 4 layers in the timeline */
  layers = ges_timeline_get_layers (timeline);
  assert_equals_int (g_list_length (layers), 4);

  /* Some refcount checkings */
  /*  We have a reference to each layer in layers */
  for (tmp = layers; tmp; tmp = tmp->next)
    ASSERT_OBJECT_REFCOUNT (layer, "Layer", 2);
  g_list_free_full (layers, g_object_unref);

  /* We have 3 references:
   *  track  + timeline  + obj
   */
  ASSERT_OBJECT_REFCOUNT (tckobj, "First tckobj", 3);
  ASSERT_OBJECT_REFCOUNT (tckobj1, "Second tckobj", 3);
  ASSERT_OBJECT_REFCOUNT (tckobj2, "Third tckobj", 3);
  /* We have 1 ref:
   * + layer */
  ASSERT_OBJECT_REFCOUNT (obj, "First clip", 1);
  ASSERT_OBJECT_REFCOUNT (obj1, "Second clip", 1);
  ASSERT_OBJECT_REFCOUNT (obj2, "Third clip", 1);

  /* Ripple obj end to 52
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 0:   |  obj1  |         |   obj2    |
   *            20-------40       62----------112
   *
   *                        5------
   * layer 1:               |  obj |
   *                       42------60
   *
   */
  fail_unless (ges_clip_edit (obj1, NULL, 0, GES_EDIT_MODE_RIPPLE,
          GES_EDGE_END, 40) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 42, 5, 20);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 20);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /* Check that movement between layer has been done properly */
  layer1 = ges_clip_get_layer (obj);
  layer = ges_clip_get_layer (obj1);
  assert_equals_int (ges_timeline_layer_get_priority (layer1), 1);
  assert_equals_int (ges_timeline_layer_get_priority (layer), 0);
  fail_unless (ges_clip_get_layer (obj2) == layer);
  g_object_unref (layer1);
  /* We have 2 references to @layer that we do not need anymore */ ;
  g_object_unref (layer);
  g_object_unref (layer);

  /* Trim obj start to 40
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 0:   |  obj1  |         |   obj2    |
   *            20-------40       62---------112
   *
   *                      0------
   * layer 1:             |  obj |
   *                     40------62
   *
   */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 40) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 40, 3, 22);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 20);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /* Roll obj end to 25
   * New timeline:
   * ------------
   *
   *            0--------          0-----------
   * layer 0:   |  obj1  |         |   obj2    |
   *            20-------25       62---------112
   *
   *                      0------
   * layer 1:             |  obj |
   *                     25------62
   *
   */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 25) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /* Make sure that not doing anything when not able to roll */
  fail_unless (ges_clip_edit (obj, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_START, 65) == TRUE);
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 65) == TRUE, 0);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /* Snaping to edge, so no move */
  g_object_set (timeline, "snapping-distance", (guint64) 3, NULL);
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /* Snaping to edge, so no move */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 27) == TRUE);

  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 5);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /**
   * New timeline:
   * ------------
   *                    0----------- 0-------------
   * inpoints   0-------|--   obj   ||   obj2      |
   *            |  obj1 25-|------- 62 -----------122
   * time      20----------30
   */
  g_object_set (timeline, "snapping-distance", (guint64) 0, NULL);
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_END, 30) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 10);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

  /**
   * New timeline
   * ------------
   *                    0----------
   *                    |   obj    |
   *                    25---------62
   * inpoints   0----------------------- 10--------
   *            |       obj1            ||  obj2   |
   * time      20---------------------- 72 --------122
   */
  /* Rolling involves only neighbours that are currently snapping */
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 62) == TRUE);
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_END, 72) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 20, 0, 52);
  CHECK_OBJECT_PROPS (tckobj2, 72, 10, 50);

  /* Test Snapping */
  /**
   *                    0----------
   *                    |   obj    |
   *                    25---------62
   * inpoints           5--------------- 10--------
   *                    |     obj1      ||  obj2   |
   * time               25------------- 72 --------122
   */
  g_object_set (timeline, "snapping-distance", (guint64) 4, NULL);
  fail_unless (ges_clip_edit (obj1, NULL, -1, GES_EDIT_MODE_TRIM,
          GES_EDGE_START, 28) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 25, 5, 47);
  CHECK_OBJECT_PROPS (tckobj2, 72, 10, 50);

  /**
   *                    0----------
   *                    |   obj    |
   *                    25---------62
   * inpoints           5---------- 0---------
   *                    |  obj1    ||  obj2   |
   * time               25-------- 62 --------122
   */
  fail_unless (ges_clip_edit (obj2, NULL, -1, GES_EDIT_MODE_ROLL,
          GES_EDGE_START, 59) == TRUE);
  CHECK_OBJECT_PROPS (tckobj, 25, 0, 37);
  CHECK_OBJECT_PROPS (tckobj1, 25, 5, 37);
  CHECK_OBJECT_PROPS (tckobj2, 62, 0, 60);

}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges-timeline-edition");
  TCase *tc_chain = tcase_create ("timeline-edition");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_basic_timeline_edition);
  tcase_add_test (tc_chain, test_snapping);
  tcase_add_test (tc_chain, test_timeline_edition_mode);

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
