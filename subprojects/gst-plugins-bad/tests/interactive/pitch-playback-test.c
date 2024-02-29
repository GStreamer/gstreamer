#include <gst/gst.h>

typedef struct
{
  gdouble playback_rate;
  gfloat pitch;
  gfloat tempo;
} TestCase;

static const TestCase test_cases[] = {
  /* Next 2 tests must sound the same as the original file */
  {1.0, 1.0, 1.0}, {-1.0, 1.0, 1.0},
  /* Next 2 tests must sound the same with a higher pitch */
  {1.0, 1.25, 1.0}, {-1.0, 1.25, 1.0},
  /* Next 2 tests must sound the same with a lower pitch */
  {1.0, 0.75, 1.0}, {-1.0, 0.75, 1.0},
  /* Next 4 tests must sound the same 25% faster */
  {1.0, 1.0, 1.25}, {-1.0, 1.0, 1.25},
  {1.25, 1.0, 1.0}, {-1.25, 1.0, 1.0},
  /* Next 4 tests must sound the same 25% slower */
  {1.0, 1.0, 0.75}, {-1.0, 1.0, 0.75},
  {0.75, 1.0, 1.0}, {-0.75, 1.0, 1.0}
};

static const gint nb_test_cases = G_N_ELEMENTS (test_cases);

typedef enum
{ SEEK_REQUIRED = 0, SEEK_RUNNING, SEEK_DONE } SeekState;

GST_DEBUG_CATEGORY_STATIC (pitch_test_debug);
#define GST_CAT_DEFAULT pitch_test_debug

static void
on_pad_added_cb (GstElement * element, GstPad * src_pad,
    GstElement * next_element)
{
  (void) element;

  GstPad *sink_pad = gst_element_get_static_pad (next_element, "sink");
  if (!gst_pad_is_linked (sink_pad)) {
    gst_pad_link (src_pad, sink_pad);
  }

  gst_object_unref (sink_pad);
}

static GstPadProbeReturn
on_downstream_data_cb (GstPad * pad,
    GstPadProbeInfo * info, gint * reverse_playback_atomic)
{
  (void) pad;

  if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
    GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER (info);

    GstClockTime start = GST_BUFFER_PTS (buffer);
    GstClockTime end = start;

    guint64 start_offset = GST_BUFFER_OFFSET (buffer);
    if (start_offset == GST_CLOCK_TIME_NONE)
      start_offset = 0;

    guint64 end_offset = GST_BUFFER_OFFSET_END (buffer);
    if (end_offset == GST_CLOCK_TIME_NONE)
      end_offset = 0;

    if (g_atomic_int_get (reverse_playback_atomic)) {
      start += GST_BUFFER_DURATION (buffer);

      guint64 offset = start_offset;
      start_offset = end_offset;
      end_offset = offset;
    } else {
      end += GST_BUFFER_DURATION (buffer);
    }

    GST_INFO ("Buffer: %" GST_TIME_FORMAT " -> %" GST_TIME_FORMAT
        ", offset: %" G_GUINT64_FORMAT " -> %" G_GUINT64_FORMAT,
        GST_TIME_ARGS (start), GST_TIME_ARGS (end), start_offset, end_offset);
  } else if (info->type & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);

    if (event->type == GST_EVENT_SEGMENT) {
      const GstSegment *segment = NULL;
      gst_event_parse_segment (event, &segment);
      GST_INFO ("Segment event: %" GST_SEGMENT_FORMAT, (void *) segment);
    } else if (event->type == GST_EVENT_EOS) {
      GST_INFO ("EOS event");
    }
  }

  return GST_PAD_PROBE_OK;
}

int
main (int argc, char *argv[])
{
  const gchar *debug_env = g_getenv ("GST_DEBUG");
  if (debug_env && *debug_env) {
    gchar *value = g_strdup_printf ("%s,pitchtest:7", debug_env);
    g_setenv ("GST_DEBUG", value, TRUE);
    g_free (value);
  } else {
    g_setenv ("GST_DEBUG", "pitchtest:7", TRUE);
  }

  gst_init (&argc, &argv);
  GST_DEBUG_CATEGORY_INIT (pitch_test_debug, "pitchtest", 0,
      "Pitch playback test");

  gboolean passthrough = FALSE;
  for (gint i = 0; i < argc; ++i) {
    if (g_strcmp0 (argv[i], "--passthrough") == 0) {
      passthrough = TRUE;
      break;
    }
  }

  gchar *exe_dir;
  if (argc > 0) {
    exe_dir = g_path_get_dirname (argv[0]);
  } else {
    exe_dir = g_get_current_dir ();
  }
  gchar *audio_file =
      g_build_filename (exe_dir, "audio-8s-then-reverse.ogg", NULL);
  g_free (exe_dir);

  GstElement *pipeline = gst_pipeline_new (NULL);
  GstElement *filesrc = gst_element_factory_make ("filesrc", NULL);
  g_object_set (filesrc, "location", audio_file, NULL);
  g_free (audio_file);

  GstElement *decodebin = gst_element_factory_make ("decodebin", NULL);
  GstElement *audioconvert = gst_element_factory_make ("audioconvert", NULL);
  g_signal_connect (decodebin, "pad-added", G_CALLBACK (on_pad_added_cb),
      audioconvert);

  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GstElement *pitch =
      gst_element_factory_make (passthrough ? "identity" : "pitch", NULL);
  GstElement *audiosink = gst_element_factory_make ("autoaudiosink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), filesrc, decodebin, audioconvert, queue,
      pitch, audiosink, NULL);
  gst_element_link_many (filesrc, decodebin, NULL);
  gst_element_link_many (audioconvert, queue, pitch, audiosink, NULL);

  gint reverse_playback_atomic = 0;
  GstPad *pad = gst_element_get_static_pad (pitch, "src");
  gst_pad_add_probe (pad,
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) on_downstream_data_cb, &reverse_playback_atomic,
      NULL);
  gst_object_unref (pad);

  SeekState seek_state = SEEK_REQUIRED;
  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  gint current_test_case = 0;
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gboolean quit = FALSE;
  while (!quit) {
    GstMessage *msg = gst_bus_timed_pop (bus, GST_CLOCK_TIME_NONE);
    if (!msg) {
      quit = TRUE;
      break;
    }

    switch (msg->type) {
      case GST_MESSAGE_ERROR:{
        GError *error = NULL;
        gchar *debug_info = NULL;

        gst_message_parse_error (msg, &error, &debug_info);
        g_printerr ("Unrecoverable error from %s: %s\n",
            GST_OBJECT_NAME (msg->src), error->message);
        g_printerr ("Debugging info: %s\n", debug_info ? debug_info : "none");
        g_error_free (error);
        g_free (debug_info);

        quit = TRUE;
      }
        break;

      case GST_MESSAGE_EOS:
        if (++current_test_case < nb_test_cases) {
          seek_state = SEEK_REQUIRED;
          gst_element_set_state (pipeline, GST_STATE_PAUSED);
        } else {
          GST_WARNING ("#### All tests finished ####");
          quit = TRUE;
        }
        break;

      case GST_MESSAGE_STATE_CHANGED:
        if (msg->src == GST_OBJECT (pipeline)) {
          GstState new_state;
          gst_message_parse_state_changed (msg, NULL, &new_state, NULL);

          if (new_state == GST_STATE_PAUSED) {
            switch (seek_state) {
              case SEEK_REQUIRED:{
                seek_state = SEEK_RUNNING;

                const TestCase *test_case = test_cases + current_test_case;
                GST_WARNING
                    ("#### Starting test %02d%s: playback_rate=%f, pitch=%f, tempo=%f ####",
                    current_test_case + 1, passthrough ? " (passthrough)" : "",
                    test_case->playback_rate, test_case->pitch,
                    test_case->tempo);

                if (!passthrough) {
                  g_object_set (pitch, "pitch", test_case->pitch, "tempo",
                      test_case->tempo, NULL);
                }

                GstEvent *seek_event;
                if (test_case->playback_rate >= 0.0) {
                  g_atomic_int_set (&reverse_playback_atomic, 0);
                  seek_event =
                      gst_event_new_seek (test_case->playback_rate,
                      GST_FORMAT_TIME,
                      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 8 * GST_SECOND);
                } else {
                  g_atomic_int_set (&reverse_playback_atomic, 1);
                  seek_event =
                      gst_event_new_seek (test_case->playback_rate,
                      GST_FORMAT_TIME,
                      GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
                      GST_SEEK_TYPE_SET, 8 * GST_SECOND, GST_SEEK_TYPE_END, 0);
                }

                gst_element_send_event (decodebin, seek_event);
              }
                break;

              case SEEK_RUNNING:
                seek_state = SEEK_DONE;
                gst_element_set_state (pipeline, GST_STATE_PLAYING);
                break;

              default:
                break;
            }
          }
        }
        break;

      default:
        break;
    }

    gst_message_unref (msg);
  }
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
