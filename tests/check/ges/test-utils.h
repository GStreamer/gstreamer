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

#ifndef _GES_TEST_UTILS
#define _GES_TEST_UTILS

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
  guint64 pstart, pdur, inpoint, pprio, pact;			\
  g_object_get (nleobj, "start", &pstart, "duration", &pdur,		\
		"inpoint", &inpoint, "priority", &pprio, "active", &pact,			\
		NULL);							\
  assert_equals_uint64 (pstart, start);					\
  assert_equals_uint64 (pdur, duration);					\
  assert_equals_uint64 (inpoint, mstart);					\
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


void print_timeline(GESTimeline *timeline);

#endif /* _GES_TEST_UTILS */
