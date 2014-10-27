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

GESPipeline * ges_test_create_pipeline (GESTimeline *timeline);
/*  The first 2 NLE priorities are used for:
 *    0- The Mixing element
 *    1- The Gaps
 */
#define MIN_NLE_PRIO 2

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
#define _PRIORITY(obj) GES_TIMELINE_ELEMENT_PRIORITY (obj)

#define CHECK_OBJECT_PROPS(obj, start, inpoint, duration) {\
  assert_equals_uint64 (_START (obj), start);\
  assert_equals_uint64 (_INPOINT (obj), inpoint);\
  assert_equals_uint64 (_DURATION (obj), duration);\
}

#define check_layer(clip, layer_prio) {                                        \
  GESLayer *tmplayer = ges_clip_get_layer ((clip));                            \
  assert_equals_int (ges_layer_get_priority (tmplayer),  (layer_prio));        \
  gst_object_unref (tmplayer);                                                 \
}

#endif /* _GES_TEST_UTILS */
