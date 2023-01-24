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

#include "test-utils.h"
#include <gio/gio.h>

typedef struct _DestroyedObjectStruct
{
  GObject *object;
  gboolean destroyed;
} DestroyedObjectStruct;

gchar *
ges_test_get_audio_only_uri (void)
{
  return ges_test_file_uri ("audio_only.ogg");
}

gchar *
ges_test_get_audio_video_uri (void)
{
  return ges_test_file_uri ("audio_video.ogg");
}

gchar *
ges_test_get_image_uri (void)
{
  return ges_test_file_uri ("image.png");
}

gchar *
ges_test_file_uri (const gchar * filename)
{
  gchar *path, *uri;

  path = g_build_filename (GES_TEST_FILES_PATH, filename, NULL);
  uri = gst_filename_to_uri (path, NULL);

  g_free (path);

  return uri;
}

GESPipeline *
ges_test_create_pipeline (GESTimeline * timeline)
{
  GESPipeline *pipeline;

  pipeline = ges_pipeline_new ();
  fail_unless (ges_pipeline_set_timeline (pipeline, timeline));

  g_object_set (pipeline, "audio-sink",
      gst_element_factory_make ("fakeaudiosink", "test-audiofakesink"),
      "video-sink", gst_element_factory_make ("fakevideosink",
          "test-videofakesink"), NULL);

  return pipeline;
}

gchar *
ges_test_file_name (const gchar * filename)
{
  return g_strjoin ("/", "file:/", g_get_current_dir (), filename, NULL);
}

gboolean
ges_generate_test_file_audio_video (const gchar * filedest,
    const gchar * audio_enc,
    const gchar * video_enc,
    const gchar * mux, const gchar * video_pattern, const gchar * audio_wave)
{
  GError *error = NULL;
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *message;
  gchar *pipeline_str;
  gboolean done = FALSE;
  gboolean ret = FALSE;

  if (g_file_test (filedest, G_FILE_TEST_EXISTS)) {
    GST_INFO ("The file %s already existed.", filedest);
    return TRUE;
  }

  pipeline_str = g_strdup_printf ("audiotestsrc num-buffers=430 wave=%s "
      "%c %s ! %s name=m ! filesink location= %s/%s "
      "videotestsrc pattern=%s num-buffers=300 ! %s ! m.",
      audio_wave,
      audio_enc ? '!' : ' ',
      audio_enc ? audio_enc : "",
      mux, g_get_current_dir (), filedest, video_pattern, video_enc);

  pipeline = gst_parse_launch (pipeline_str, &error);

  if (pipeline == NULL)
    return FALSE;

  g_free (pipeline_str);

  bus = gst_element_get_bus (GST_ELEMENT (pipeline));
  gst_bus_add_signal_watch (bus);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  while (!done) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_CLOCK_TIME_NONE);
    if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS) {
      done = TRUE;
      ret = TRUE;
    } else if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
      gchar *debug = NULL;
      GError *err = NULL;

      gst_message_parse_error (message, &err, &debug);
      done = TRUE;
      ret = FALSE;
      GST_ERROR ("Got error %s from %s fron the bus while generation: %s"
          "debug infos: %s", GST_OBJECT_NAME (message->src), err->message,
          debug ? debug : "none", filedest);
      g_clear_error (&err);
      g_free (debug);
    }
  }

  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return ret;
}

static void
weak_notify (DestroyedObjectStruct * destroyed, GObject ** object)
{
  destroyed->destroyed = TRUE;
}

void
check_destroyed (GObject * object_to_unref, GObject * first_object, ...)
{
  GObject *object;
  GList *objs = NULL, *tmp;
  DestroyedObjectStruct *destroyed = g_new0 (DestroyedObjectStruct, 1);

  destroyed->object = object_to_unref;
  g_object_weak_ref (object_to_unref, (GWeakNotify) weak_notify, destroyed);
  objs = g_list_prepend (objs, destroyed);

  if (first_object) {
    va_list varargs;

    object = first_object;

    va_start (varargs, first_object);
    while (object) {
      destroyed = g_new0 (DestroyedObjectStruct, 1);
      destroyed->object = object;
      g_object_weak_ref (object, (GWeakNotify) weak_notify, destroyed);
      objs = g_list_prepend (objs, destroyed);
      object = va_arg (varargs, GObject *);
    }
    va_end (varargs);
  }
  gst_object_unref (object_to_unref);

  for (tmp = objs; tmp; tmp = tmp->next) {
    fail_unless (((DestroyedObjectStruct *) tmp->data)->destroyed == TRUE,
        "%p is not destroyed", ((DestroyedObjectStruct *) tmp->data)->object);
    g_free (tmp->data);
  }
  g_list_free (objs);

}

static gboolean
my_bus_callback (GstBus * bus, GstMessage * message, GMainLoop * loop)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);

      g_assert_no_error (err);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      GST_INFO ("EOS\n");
      g_main_loop_quit (loop);
      break;
    default:
      /* unhandled message */
      break;
  }
  return TRUE;
}

gboolean
play_timeline (GESTimeline * timeline)
{
  GstBus *bus;
  GESPipeline *pipeline;
  GMainLoop *loop = g_main_loop_new (NULL, FALSE);

  ges_timeline_commit (timeline);
  pipeline = ges_pipeline_new ();

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, (GstBusFunc) my_bus_callback, loop);
  gst_object_unref (bus);

  ges_pipeline_set_timeline (pipeline, timeline);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);

  g_main_loop_run (loop);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_element_get_state (GST_ELEMENT (pipeline), NULL, NULL, -1);

  gst_object_unref (pipeline);

  return TRUE;
}

gchar *
ges_test_get_tmp_uri (const gchar * filename)
{
  gchar *location, *uri;

  location = g_build_filename (g_get_tmp_dir (), filename, NULL);

  uri = g_strconcat ("file://", location, NULL);
  g_free (location);

  return uri;
}

void
print_timeline (GESTimeline * timeline)
{
  GList *layer, *clip, *clips, *group;

  gst_printerr
      ("\n\n=========================== GESTimeline: %p ==================\n",
      timeline);
  for (layer = timeline->layers; layer; layer = layer->next) {
    clips = ges_layer_get_clips (layer->data);

    gst_printerr ("layer %04d: ", ges_layer_get_priority (layer->data));
    for (clip = clips; clip; clip = clip->next) {
      gst_printerr ("{ %s [ %" G_GUINT64_FORMAT "(%" G_GUINT64_FORMAT ") %"
          G_GUINT64_FORMAT "] } ", GES_TIMELINE_ELEMENT_NAME (clip->data),
          GES_TIMELINE_ELEMENT_START (clip->data),
          GES_TIMELINE_ELEMENT_INPOINT (clip->data),
          GES_TIMELINE_ELEMENT_END (clip->data));
    }
    if (layer->next)
      gst_printerr ("\n--------------------------------------------------\n");

    g_list_free_full (clips, gst_object_unref);
  }

  if (ges_timeline_get_groups (timeline)) {
    gst_printerr ("\n--------------------------------------------------\n");
    gst_printerr ("\nGROUPS:");
    gst_printerr ("\n~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  }

  for (group = ges_timeline_get_groups (timeline); group; group = group->next) {
    gst_printerr ("%" GES_FORMAT ": ", GES_ARGS (group->data));
    for (clip = GES_CONTAINER_CHILDREN (group->data); clip; clip = clip->next)
      gst_printerr ("[ %s ]", GES_TIMELINE_ELEMENT_NAME (clip->data));
  }

  gst_printerr
      ("\n=====================================================================\n");
}

/* append the properties found in element to list, num_props should point
 * to the current list length.
 */
GParamSpec **
append_children_properties (GParamSpec ** list, GESTimelineElement * element,
    guint * num_props)
{
  guint i, num;
  GParamSpec **props =
      ges_timeline_element_list_children_properties (element, &num);
  fail_unless (props);
  list = g_realloc_n (list, num + *num_props, sizeof (GParamSpec *));

  for (i = 0; i < num; i++)
    list[*num_props + i] = props[i];

  g_free (props);
  *num_props += num;
  return list;
}

void
free_children_properties (GParamSpec ** list, guint num_props)
{
  guint i;
  for (i = 0; i < num_props; i++)
    g_param_spec_unref (list[i]);
  g_free (list);
}
