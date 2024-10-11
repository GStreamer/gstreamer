/**
 * Gstreamer Editing Services
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
#pragma once

#include <ges/ges.h>
#include <gst/check/gstcheck.h>
#include "../../../ges/ges-internal.h"

GESPipeline * ges_test_create_pipeline (GESTimeline *timeline);
/*  The first 2 NLE priorities are used for:
 *    0- The Mixing element
 *    1- The Gaps
 */
#define MIN_NLE_PRIO 2
#define TRANSITIONS_HEIGHT 1
#define LAYER_HEIGHT 1000

gchar * ges_test_get_tmp_uri (const gchar * filename);
gchar * ges_test_get_audio_only_uri (void);
gchar * ges_test_get_audio_video_uri (void);
gchar * ges_test_get_image_uri (void);
gchar * ges_test_file_uri (const gchar *filename);

void check_destroyed (GObject *object_to_unref, GObject *first_object, ...) G_GNUC_NULL_TERMINATED;
gchar * ges_test_file_name (const gchar *filename);
gboolean
ges_generate_test_file_audio_video (const gchar * filedest,
    const gchar * audio_enc,
    const gchar * video_enc,
    const gchar * mux, const gchar * video_pattern, const gchar * audio_wave);
gboolean
play_timeline (GESTimeline * timeline);

GParamSpec **
append_children_properties (GParamSpec ** list, GESTimelineElement * element, guint * num_props);
void
free_children_properties (GParamSpec ** list, guint num_props);

#define nle_object_check(nleobj, start, duration, mstart, mduration, priority, active) { \
  guint64 pstart, inpoint;						\
  gint64 pdur;								\
  guint pprio;								\
  gboolean pact;							\
  g_object_get (nleobj, "start", &pstart, "duration", &pdur,		\
		"inpoint", &inpoint, "priority", &pprio, "active", &pact,	\
		NULL);							\
  assert_equals_uint64 (pstart, start);					\
  assert_equals_int64 (pdur, duration);					\
  assert_equals_uint64 (inpoint, mstart);				\
  assert_equals_int (pprio, priority);					\
  assert_equals_int (pact, active);					\
  }

/* copied from nle */
#define fail_error_message(msg)			\
  G_STMT_START {				\
    GError *error;				\
    gst_message_parse_error(msg, &error, NULL);				\
    fail_unless(FALSE, "Error Message from %s : %s",			\
		GST_OBJECT_NAME (GST_MESSAGE_SRC(msg)), error->message); \
    g_error_free (error);						\
  } G_STMT_END;

#define assert_is_type(object, type)                    \
G_STMT_START {                                          \
 fail_unless (g_type_is_a(G_OBJECT_TYPE(object), type), \
     "%s is not a %s", G_OBJECT_TYPE_NAME(object),      \
     g_type_name (type));                               \
} G_STMT_END;

#define _START(obj) GES_TIMELINE_ELEMENT_START (obj)
#define _DURATION(obj) GES_TIMELINE_ELEMENT_DURATION (obj)
#define _INPOINT(obj) GES_TIMELINE_ELEMENT_INPOINT (obj)
#define _MAX_DURATION(obj) GES_TIMELINE_ELEMENT_MAX_DURATION (obj)
#define _PRIORITY(obj) GES_TIMELINE_ELEMENT_PRIORITY (obj)
#ifndef _END
#define _END(obj) (_START(obj) + _DURATION(obj))
#endif

#define CHECK_OBJECT_PROPS(obj, start, inpoint, duration) {\
  fail_unless (_START (obj) == start, "%s start is %" GST_TIME_FORMAT " != %" GST_TIME_FORMAT, GES_TIMELINE_ELEMENT_NAME(obj), GST_TIME_ARGS (_START(obj)), GST_TIME_ARGS (start));\
  fail_unless (_INPOINT (obj) == inpoint, "%s inpoint is %" GST_TIME_FORMAT " != %" GST_TIME_FORMAT, GES_TIMELINE_ELEMENT_NAME(obj), GST_TIME_ARGS (_INPOINT(obj)), GST_TIME_ARGS (inpoint));\
  fail_unless (_DURATION (obj) == duration, "%s duration is %" GST_TIME_FORMAT " != %" GST_TIME_FORMAT, GES_TIMELINE_ELEMENT_NAME(obj), GST_TIME_ARGS (_DURATION(obj)), GST_TIME_ARGS (duration));\
}

#define CHECK_OBJECT_PROPS_MAX(obj, start, inpoint, duration, max_duration) {\
  CHECK_OBJECT_PROPS (obj, start, inpoint, duration); \
  fail_unless (_MAX_DURATION(obj) == max_duration, "%s max-duration is " \
      "%" GST_TIME_FORMAT " != %" GST_TIME_FORMAT, \
      GES_TIMELINE_ELEMENT_NAME(obj), \
      GST_TIME_ARGS (_MAX_DURATION(obj)), GST_TIME_ARGS (max_duration)); \
}

#define __assert_timeline_element_set(obj, prop, val) \
  fail_unless (ges_timeline_element_set_ ## prop ( \
        GES_TIMELINE_ELEMENT (obj), val), "Could not set the " # prop \
        " of " #obj "(%s) to " #val, GES_TIMELINE_ELEMENT_NAME (obj))

#define __fail_timeline_element_set(obj, prop, val) \
  fail_if (ges_timeline_element_set_ ## prop ( \
        GES_TIMELINE_ELEMENT (obj), val), "Setting the " # prop \
        " of " #obj "(%s) to " #val " did not fail as expected", \
        GES_TIMELINE_ELEMENT_NAME (obj))


#define assert_set_start(obj, val) \
  __assert_timeline_element_set (obj, start, val)

#define assert_set_duration(obj, val) \
  __assert_timeline_element_set (obj, duration, val)

#define assert_set_inpoint(obj, val) \
  __assert_timeline_element_set (obj, inpoint, val)

#define assert_set_max_duration(obj, val) \
  __assert_timeline_element_set (obj, max_duration, val)


#define assert_fail_set_start(obj, val) \
  __fail_timeline_element_set (obj, start, val)

#define assert_fail_set_duration(obj, val) \
  __fail_timeline_element_set (obj, duration, val)

#define assert_fail_set_inpoint(obj, val) \
  __fail_timeline_element_set (obj, inpoint, val)

#define assert_fail_set_max_duration(obj, val) \
  __fail_timeline_element_set (obj, max_duration, val)


#define assert_num_in_track(track, val) \
{ \
  GList *tmp = ges_track_get_elements (track); \
  guint length = g_list_length (tmp); \
  fail_unless (length == val, "Track %" GST_PTR_FORMAT \
      " contains %u track elements, rather than %u", track, length, val); \
  g_list_free_full (tmp, gst_object_unref); \
}

#define assert_num_children(clip, cmp) \
{ \
  guint num_children = g_list_length (GES_CONTAINER_CHILDREN (clip)); \
  fail_unless (cmp == num_children, \
      "clip %s contains %u children rather than %u", \
      GES_TIMELINE_ELEMENT_NAME (clip), num_children, cmp); \
}

/* assert that the time property (start, duration or in-point) is the
 * same as @cmp for the clip and all its children */
#define assert_clip_children_time_val(clip, property, cmp) \
{ \
  GList *tmp; \
  GstClockTime read_val; \
  gchar *name = GES_TIMELINE_ELEMENT (clip)->name; \
  gboolean is_inpoint = (g_strcmp0 (property, "in-point") == 0); \
  g_object_get (clip, property, &read_val, NULL); \
  fail_unless (read_val == cmp, "The %s property for clip %s is %" \
      GST_TIME_FORMAT ", rather than the expected value of %" \
      GST_TIME_FORMAT, property, name, GST_TIME_ARGS (read_val), \
      GST_TIME_ARGS (cmp)); \
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp != NULL; \
      tmp = tmp->next) { \
    GESTimelineElement *child = tmp->data; \
    g_object_get (child, property, &read_val, NULL); \
    if (!is_inpoint || ges_track_element_has_internal_source ( \
          GES_TRACK_ELEMENT (child))) \
      fail_unless (read_val == cmp, "The %s property for the child %s " \
          "of clip %s is %" GST_TIME_FORMAT ", rather than the expected" \
          " value of %" GST_TIME_FORMAT, property, child->name, name, \
          GST_TIME_ARGS (read_val), GST_TIME_ARGS (cmp)); \
    else \
      fail_unless (read_val == 0, "The %s property for the child %s " \
          "of clip %s is %" GST_TIME_FORMAT ", rather than 0", \
          property, child->name, name, GST_TIME_ARGS (read_val)); \
  } \
}

#define check_layer(clip, layer_prio) {                                      \
  fail_unless (GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip) ==  (layer_prio),  \
    "%s in layer %d instead of %d", GES_TIMELINE_ELEMENT_NAME (clip),        \
    GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip), layer_prio);                 \
}

#define assert_layer(clip, layer) \
{ \
  GESLayer *tmp_layer = ges_clip_get_layer (GES_CLIP (clip)); \
  fail_unless (tmp_layer == GES_LAYER (layer), "clip %s belongs to " \
      "layer %u (timeline %" GST_PTR_FORMAT ") rather than layer %u " \
      "(timeline %" GST_PTR_FORMAT ")", \
      tmp_layer ? ges_layer_get_priority (tmp_layer) : 0, \
      tmp_layer ? tmp_layer->timeline : NULL, \
      layer ? ges_layer_get_priority (GES_LAYER (layer)) : 0, \
      layer ? GES_LAYER (layer)->timeline : NULL); \
  if (tmp_layer) \
    gst_object_unref (tmp_layer); \
  if (layer) { \
    GList *layer_clips = ges_layer_get_clips (GES_LAYER (layer)); \
    fail_unless (g_list_find (layer_clips, clip), "clip %s not found " \
        "in layer %u (timeline %" GST_PTR_FORMAT ")", \
        ges_layer_get_priority (GES_LAYER (layer)), layer->timeline); \
    g_list_free_full (layer_clips, gst_object_unref); \
  } \
}

/* test that the two property lists contain the same properties the same
 * number of times */
#define assert_property_list_match(list1, len1, list2, len2) \
  { \
    gboolean *found_count_in_list2; \
    guint *count_list1; \
    guint i, j; \
    found_count_in_list2 = g_new0 (gboolean, len1); \
    count_list1 = g_new0 (guint, len1); \
    for (i = 0; i < len1; i++) { \
      found_count_in_list2[i] = 0; \
      count_list1[i] = 0; \
      for (j = 0; j < len1; j++) { \
        if (list1[i] == list1[j]) \
          count_list1[i] ++; \
      } \
    } \
    for (j = 0; j < len2; j++) { \
      guint count_list2 = 0; \
      guint found_count_in_list1 = 0; \
      GParamSpec *prop = list2[j]; \
      for (i = 0; i < len2; i++) { \
        if (list2[i] == prop) \
          count_list2 ++; \
      } \
      for (i = 0; i < len1; i++) { \
        if (list1[i] == prop) { \
          found_count_in_list2[i] ++; \
          found_count_in_list1 ++; \
        } \
      } \
      fail_unless (found_count_in_list1 == count_list2, \
          "Found property '%s' %u times, rather than %u times, in " #list1, \
          prop->name, found_count_in_list1, count_list2); \
    } \
    /* make sure we found each one once */ \
    for (i = 0; i < len1; i++) { \
      GParamSpec *prop = list1[i]; \
      fail_unless (found_count_in_list2[i] == count_list1[i], \
          "Found property '%s' %u times, rather than %u times, in " #list2, \
          prop->name, found_count_in_list2[i], count_list1[i]); \
    } \
    g_free (found_count_in_list2); \
    g_free (count_list1); \
  }

#define assert_equal_children_properties(el1, el2) \
{ \
  guint i, num1, num2; \
  const gchar *name1 = GES_TIMELINE_ELEMENT_NAME (el1); \
  const gchar *name2 = GES_TIMELINE_ELEMENT_NAME (el2); \
  GParamSpec **el_props1 = ges_timeline_element_list_children_properties ( \
      GES_TIMELINE_ELEMENT (el1), &num1); \
  GParamSpec **el_props2 = ges_timeline_element_list_children_properties ( \
      GES_TIMELINE_ELEMENT (el2), &num2); \
  assert_property_list_match (el_props1, num1, el_props2, num2); \
  \
  for (i = 0; i < num1; i++) { \
    gchar *ser1, *ser2; \
    GParamSpec *prop = el_props1[i]; \
    GValue val1 = G_VALUE_INIT, val2 = G_VALUE_INIT; \
    /* name property can be different */ \
    if (g_strcmp0 (prop->name, "name") == 0) \
      continue; \
    if (g_strcmp0 (prop->name, "parent") == 0) \
      continue; \
    g_value_init (&val1, prop->value_type); \
    g_value_init (&val2, prop->value_type); \
    ges_timeline_element_get_child_property_by_pspec ( \
        GES_TIMELINE_ELEMENT (el1), prop, &val1); \
    ges_timeline_element_get_child_property_by_pspec ( \
        GES_TIMELINE_ELEMENT (el2), prop, &val2); \
    ser1 = gst_value_serialize (&val1); \
    ser2 = gst_value_serialize (&val2); \
    fail_unless (gst_value_compare (&val1, &val2) == GST_VALUE_EQUAL, \
        "Child property '%s' for %s does not match that for %s (%s vs %s)", \
        prop->name, name1, name2, ser1, ser2); \
    g_free (ser1); \
    g_free (ser2); \
    g_value_unset (&val1); \
    g_value_unset (&val2); \
  } \
  free_children_properties (el_props1, num1); \
  free_children_properties (el_props2, num2); \
}

#define assert_equal_bindings(el1, el2) \
{ \
  guint i, num1, num2; \
  const gchar *name1 = GES_TIMELINE_ELEMENT_NAME (el1); \
  const gchar *name2 = GES_TIMELINE_ELEMENT_NAME (el2); \
  GParamSpec **props1 = ges_timeline_element_list_children_properties ( \
      GES_TIMELINE_ELEMENT (el1), &num1); \
  GParamSpec **props2 = ges_timeline_element_list_children_properties ( \
      GES_TIMELINE_ELEMENT (el2), &num2); \
  assert_property_list_match (props1, num1, props2, num2); \
  \
  for (i = 0; i < num1; i++) { \
    const gchar *prop = props1[i]->name; \
    GList *tmp1, *tmp2; \
    GList *timed_vals1, *timed_vals2; \
    GObject *object1, *object2; \
    gboolean abs1, abs2; \
    GstControlSource *source1, *source2; \
    GstInterpolationMode mode1, mode2; \
    GstControlBinding *binding1, *binding2; \
    guint j; \
    \
    binding1 = ges_track_element_get_control_binding ( \
        GES_TRACK_ELEMENT (el1), prop); \
    binding2 = ges_track_element_get_control_binding ( \
        GES_TRACK_ELEMENT (el2), prop); \
    if (binding1 == NULL) { \
      fail_unless (binding2 == NULL, "%s has a binding for property " \
          " '%s', whilst %s does not", name2, prop, name1); \
      continue; \
    } \
    if (binding2 == NULL) { \
      fail_unless (binding1 == NULL, "%s has a binding for property " \
          "'%s', whilst %s does not", name1, prop, name2); \
      continue; \
    } \
    \
    fail_unless (G_OBJECT_TYPE (binding1) == GST_TYPE_DIRECT_CONTROL_BINDING, \
        "%s binding for property '%s' is not a direct control binding, " \
        "so cannot be handled", prop, name1); \
    fail_unless (G_OBJECT_TYPE (binding2) == GST_TYPE_DIRECT_CONTROL_BINDING, \
        "%s binding for property '%s' is not a direct control binding, " \
        "so cannot be handled", prop, name2); \
    \
    g_object_get (G_OBJECT (binding1), "control-source", &source1, \
        "absolute", &abs1, "object", &object1, NULL); \
    g_object_get (G_OBJECT (binding2), "control-source", &source2, \
        "absolute", &abs2, "object", &object2, NULL); \
    \
    fail_unless (G_OBJECT_TYPE (object1) == G_OBJECT_TYPE (object2), \
        "The child object for property '%s' for %s and %s correspond " \
        "to different object types (%s vs %s)", prop, name1, name2, \
        G_OBJECT_TYPE_NAME (object1), G_OBJECT_TYPE_NAME (object2)); \
    gst_object_unref (object1); \
    gst_object_unref (object2); \
    \
    fail_unless (abs1 == abs2, "control biding for property '%s' " \
        " is %s absolute for %s, but %s absolute for %s", prop, \
        abs1 ? "" : "not", name1, abs2 ? "" : "not", name2); \
    \
    fail_unless (GST_IS_INTERPOLATION_CONTROL_SOURCE (source1), \
        "%s does not have an interpolation control source for " \
        "property '%s', so cannot be handled", name1, prop); \
    fail_unless (GST_IS_INTERPOLATION_CONTROL_SOURCE (source2), \
        "%s does not have an interpolation control source for " \
        "property '%s', so cannot be handled", name2, prop); \
    g_object_get (G_OBJECT (source1), "mode", &mode1, NULL); \
    g_object_get (G_OBJECT (source2), "mode", &mode2, NULL); \
    fail_unless (mode1 == mode2, "control source for property '%s' " \
        "has different modes for %s and %s (%i vs %i)", prop, \
        name1, name2, mode1, mode2); \
    \
    timed_vals1 = gst_timed_value_control_source_get_all ( \
      GST_TIMED_VALUE_CONTROL_SOURCE (source1)); \
    timed_vals2 = gst_timed_value_control_source_get_all ( \
      GST_TIMED_VALUE_CONTROL_SOURCE (source2)); \
    \
    for (j = 0, tmp1 = timed_vals1, tmp2 = timed_vals2; tmp1 && tmp2; \
        j++, tmp1 = tmp1->next, tmp2 = tmp2->next) { \
      GstTimedValue *val1 = tmp1->data, *val2 = tmp2->data; \
      fail_unless (val1->timestamp == val2->timestamp && \
          val1->value == val2->value, "The %uth timed value for property " \
          "'%s' is different for %s and %s: (%" G_GUINT64_FORMAT ": %g) vs " \
          "(%" G_GUINT64_FORMAT ": %g)", j, prop, name1, name2, \
          val1->timestamp, val1->value, val2->timestamp, val2->value); \
    } \
    fail_unless (tmp1 == NULL, "Found too many timed values for " \
        "property '%s' for %s", prop, name1); \
    fail_unless (tmp2 == NULL, "Found too many timed values for " \
        "property '%s' for %s", prop, name2); \
    \
    g_list_free (timed_vals1); \
    g_list_free (timed_vals2); \
    gst_object_unref (source1); \
    gst_object_unref (source2); \
  } \
  free_children_properties (props1, num1); \
  free_children_properties (props2, num2); \
}

#define assert_GESError(error, error_code) \
{ \
  fail_unless (error); \
  fail_unless (error->domain == GES_ERROR); \
  assert_equals_int (error->code, error_code); \
  g_error_free (error); \
  error = NULL; \
}

void print_timeline(GESTimeline *timeline);
