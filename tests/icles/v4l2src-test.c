
#include <gst/gst.h>
#include <gst/interfaces/tuner.h>
#include <gst/interfaces/colorbalance.h>

GstElement *pipeline, *source, *sink;
GMainLoop *loop;
volatile int exit_read = 0;

void
print_options ()
{
  printf
      ("\nLaunch \"./v4l2src-test.c devname\" to choose the device (default: /dev/video0)\n");
  printf ("\nf - to change the fequency\n");
  printf ("i - to change the input\n");
  printf ("c - list color balance\n");
  printf ("e - to exit\n");
}

void
run_options (char opt)
{
  switch (opt) {
    case 'f':
    {
      GstTuner *tuner = GST_TUNER (source);
      GstTunerChannel *channel = gst_tuner_get_channel (tuner);
      guint freq;

      printf ("type the new frequency (current = %lu) (-1 to cancel): ",
          gst_tuner_get_frequency (tuner, channel));
      scanf ("%u", &freq);
      if (freq != -1)
        gst_tuner_set_frequency (tuner, channel, freq);
    }
      break;
    case 'i':
    {
      GstTuner *tuner = GST_TUNER (source);
      const GList *item, *list;
      const GstTunerChannel *current_channel;
      GstTunerChannel *channel = NULL;
      gint index, next_channel;

      list = gst_tuner_list_channels (tuner);
      current_channel = gst_tuner_get_channel (tuner);

      printf ("\nlist of inputs:\n");
      for (item = list, index = 0; item != NULL; item = item->next, ++index) {
        channel = item->data;
        if (current_channel == channel) {
          printf (" * %u - %s\n", index, channel->label);
        } else {
          printf ("   %u - %s\n", index, channel->label);
        }
      }
      printf ("\ntype the number of input you want (-1 to cancel): ");
      scanf ("%d", &next_channel);
      if (next_channel < 0 || index <= next_channel) {
        break;
      }
      for (item = list, index = 0; item != NULL && index <= next_channel;
          item = item->next, ++index) {
        channel = item->data;
      }
      if (channel)
        gst_tuner_set_channel (tuner, channel);
    }
      break;
    case 'e':
      gst_element_set_state (pipeline, GST_STATE_NULL);
      g_main_loop_quit (loop);
      printf ("Bye\n");
      g_thread_exit (0);
      break;
    case 'c':
    {
      GstColorBalance *balance = GST_COLOR_BALANCE (source);
      const GList *controls = gst_color_balance_list_channels (balance);
      GstColorBalanceChannel *channel;
      const GList *item;
      gint index, new_value;

      if (controls) {
        printf ("\nlist of controls:\n");
        for (item = controls, index = 0; item != NULL;
            item = item->next, ++index) {
          channel = item->data;
          printf ("   %u - %s (%d - %d) = %d\n", index, channel->label,
              channel->min_value, channel->max_value,
              gst_color_balance_get_value (balance, channel));
        }
        printf ("\ntype the number of color control you want (-1 to cancel): ");
        scanf ("%d", &new_value);
        if (new_value == -1)
          break;
        for (item = controls, index = 0; item != NULL && index <= new_value;
            item = item->next, ++index) {
          channel = item->data;
        }
        printf ("   %u - %s (%d - %d) = %d, type the new value: ", index,
            channel->label, channel->min_value, channel->max_value,
            gst_color_balance_get_value (balance, channel));
        scanf ("%d", &new_value);
        if (new_value == -1)
          break;
        gst_color_balance_set_value (balance, channel, new_value);
      }
    }
      break;
    default:
      if (opt != 10)
        printf ("error: invalid option %c", opt);
      break;
  }

}

gpointer
read_user (gpointer data)
{

  char opt;

  while (!exit_read) {

    print_options ();

    opt = getchar ();
    if (exit_read) {
      break;
    }

    run_options (opt);

  }

  return NULL;

}

static gboolean
my_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s - element %s\n", err->message,
          gst_element_get_name (message->src));
      g_error_free (err);
      g_free (debug);

      printf ("presse any key to exit\n");
      exit_read = 1;
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      printf ("presse any key to exit\n");
      exit_read = 1;
      g_main_loop_quit (loop);
      break;
    default:
      break;
    }
  }
  return TRUE;
}

int
main (int argc, char *argv[])
{

  GThread *input_thread;

  /* init */
  gst_init (&argc, &argv);

  /* create elements */
  if (!(pipeline = gst_pipeline_new ("my_pipeline"))) {
    fprintf (stderr, "error: gst_pipeline_new return NULL");
    return -1;
  }

  if (!(source = gst_element_factory_make ("v4l2src", NULL))) {
    fprintf (stderr,
        "error: gst_element_factory_make (\"v4l2src\", NULL) return NULL");
    return -1;
  }

  if (!(sink = gst_element_factory_make ("xvimagesink", NULL))) {
    fprintf (stderr,
        "error: gst_element_factory_make (\"xvimagesink\", NULL) return NULL");
    return -1;
  }

  if (argc < 2) {
    g_object_set (source, "device", "/dev/video0", NULL);
  } else {
    g_object_set (source, "device", argv[1], NULL);
  }

  /* you would normally check that the elements were created properly */
  gst_bus_add_watch (gst_pipeline_get_bus (GST_PIPELINE (pipeline)),
      my_bus_callback, NULL);

  /* put together a pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  gst_element_link_pads (source, "src", sink, "sink");

  /* start the pipeline */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  loop = g_main_loop_new (NULL, FALSE);

  if (!(input_thread = g_thread_create (read_user, source, TRUE, NULL))) {
    fprintf (stderr, "error: g_thread_create return NULL");
    return -1;
  }

  if (argc < 2)
    printf
        ("\nOpening /dev/video0. Launch ./v4l2src-test.c devname to try another one\n");


  g_main_loop_run (loop);
  g_thread_join (input_thread);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  gst_object_unref (pipeline);

  gst_deinit ();

  return 0;

}
