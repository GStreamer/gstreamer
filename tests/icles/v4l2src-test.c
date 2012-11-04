/* GStreamer
 *
 * Copyright (C) 2006 Edgard Lima <edgard dot lima at indt dot org dot br>
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <gst/gst.h>
#include <gst/video/colorbalance.h>
#include <gst/video/videoorientation.h>

GstElement *pipeline, *source, *sink;
GMainLoop *loop;
volatile int exit_read = 0;

static void
print_options (void)
{
  printf ("\nf - to change the fequency\n");
  printf ("i - to change the input\n");
  printf ("n - to change the norm\n");
  printf ("c - list color balance\n");
  printf ("v - change video orientarion\n");
  printf ("e - to exit\n");
}

static void
run_options (char opt)
{
  int res;

  switch (opt) {
#if 0
    case 'f':
    {
      GstTuner *tuner = GST_TUNER (source);
      GstTunerChannel *channel;
      guint freq;

      channel = gst_tuner_get_channel (tuner);

      freq = gst_tuner_get_frequency (tuner, channel);

      printf ("\ntype the new frequency (current = %u) (-1 to cancel): ", freq);
      res = scanf ("%u", &freq);
      if (res != 1 || freq != -1)
        gst_tuner_set_frequency (tuner, channel, freq);
    }
      break;
    case 'n':
    {
      GstTuner *tuner = GST_TUNER (source);
      const GList *item, *list;
      const GstTunerNorm *current_norm;
      GstTunerNorm *norm = NULL;
      gint index, next_norm;


      list = gst_tuner_list_norms (tuner);

      current_norm = gst_tuner_get_norm (tuner);

      printf ("\nlist of norms:\n");
      for (item = list, index = 0; item != NULL; item = item->next, ++index) {
        norm = item->data;
        if (current_norm == norm) {
          printf (" * %u - %s\n", index, norm->label);
        } else {
          printf ("   %u - %s\n", index, norm->label);
        }
      }
      printf ("\ntype the number of norm you want (-1 to cancel): ");
      res = scanf ("%d", &next_norm);
      if (res != 1 || next_norm < 0) {
        break;
      }
      if (index <= next_norm) {
        printf ("Norm %d not available\n", next_norm);
        break;
      }
      for (item = list, index = 0; item != NULL && index <= next_norm;
          item = item->next, ++index) {
        norm = item->data;
      }
      if (norm)
        gst_tuner_set_norm (tuner, norm);
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
      res = scanf ("%d", &next_channel);
      if (res != 1 || next_channel < 0) {
        break;
      }
      if (index <= next_channel) {
        printf ("Input %d not available\n", next_channel);
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
#endif
    case 'e':
      gst_element_set_state (pipeline, GST_STATE_NULL);
      g_main_loop_quit (loop);
      printf ("Bye\n");
      g_thread_exit (0);
      break;
    case 'c':
    {
      GstColorBalance *balance = GST_COLOR_BALANCE (source);
      const GList *controls;
      GstColorBalanceChannel *channel;
      const GList *item;
      gint index, new_value;

      controls = gst_color_balance_list_channels (balance);

      printf ("\n");

      if (controls == NULL) {
        printf ("There is no list of colorbalance controls\n");
        goto done;
      }

      if (controls) {
        printf ("list of controls:\n");
        for (item = controls, index = 0; item != NULL;
            item = item->next, ++index) {
          channel = item->data;
          printf ("   %u - %s (%d - %d) = %d\n", index, channel->label,
              channel->min_value, channel->max_value,
              gst_color_balance_get_value (balance, channel));
        }
        printf ("\ntype the number of color control you want (-1 to cancel): ");
        res = scanf ("%d", &new_value);
        if (res != 1 || new_value == -1)
          break;
        for (item = controls, index = 0; item != NULL && index <= new_value;
            item = item->next, ++index) {
          channel = item->data;
        }
        printf ("   %u - %s (%d - %d) = %d, type the new value: ", index - 1,
            channel->label, channel->min_value, channel->max_value,
            gst_color_balance_get_value (balance, channel));
        res = scanf ("%d", &new_value);
        if (res != 1 || new_value == -1)
          break;
        gst_color_balance_set_value (balance, channel, new_value);
      }
    }
    case 'v':
    {
      GstVideoOrientation *vidorient = GST_VIDEO_ORIENTATION (source);
      gboolean flip = FALSE;
      gint center = 0;

      printf ("\n");

      if (gst_video_orientation_get_hflip (vidorient, &flip)) {
        gint new_value;

        printf ("Horizontal flip is %s\n", flip ? "on" : "off");
        printf ("\ntype 1 to toggle (-1 to cancel): ");
        res = scanf ("%d", &new_value);
        if (res != 1 || new_value == 1) {
          flip = !flip;
          if (gst_video_orientation_set_hflip (vidorient, flip)) {
            gst_video_orientation_get_hflip (vidorient, &flip);
            printf ("Now horizontal flip is %s\n", flip ? "on" : "off");
          } else {
            printf ("Error toggling horizontal flip\n");
          }
        } else {
        }
      } else {
        printf ("Horizontal flip control not available\n");
      }

      if (gst_video_orientation_get_vflip (vidorient, &flip)) {
        gint new_value;

        printf ("\nVertical flip is %s\n", flip ? "on" : "off");
        printf ("\ntype 1 to toggle (-1 to cancel): ");
        res = scanf ("%d", &new_value);
        if (res != 1 || new_value == 1) {
          flip = !flip;
          if (gst_video_orientation_set_vflip (vidorient, flip)) {
            gst_video_orientation_get_vflip (vidorient, &flip);
            printf ("Now vertical flip is %s\n", flip ? "on" : "off");
          } else {
            printf ("Error toggling vertical flip\n");
          }
        } else {
        }
      } else {
        printf ("Vertical flip control not available\n");
      }

      if (gst_video_orientation_get_hcenter (vidorient, &center)) {
        printf ("Horizontal center is %d\n", center);
        printf ("\ntype the new horizontal center value (-1 to cancel): ");
        res = scanf ("%d", &center);
        if (res != 1 || center != -1) {
          if (gst_video_orientation_set_hcenter (vidorient, center)) {
            gst_video_orientation_get_hcenter (vidorient, &center);
            printf ("Now horizontal center is %d\n", center);
          } else {
            printf ("Error setting horizontal center\n");
          }
        } else {
        }
      } else {
        printf ("Horizontal center control not available\n");
      }

      if (gst_video_orientation_get_vcenter (vidorient, &center)) {
        printf ("Vertical center is %d\n", center);
        printf ("\ntype the new vertical center value (-1 to cancel): ");
        res = scanf ("%d", &center);
        if (res != 1 || center != -1) {
          if (gst_video_orientation_set_vcenter (vidorient, center)) {
            gst_video_orientation_get_vcenter (vidorient, &center);
            printf ("Now vertical center is %d\n", center);
          } else {
            printf ("Error setting vertical center\n");
          }
        } else {
        }
      } else {
        printf ("Vertical center control not available\n");
      }

    }
      break;
      break;
    default:
      if (opt != 10)
        printf ("error: invalid option %c", opt);
      break;
  }

done:

  return;

}

static gpointer
read_user (gpointer data)
{

  char opt;

  while (!exit_read) {

    print_options ();

    do {
      opt = getchar ();
      if (exit_read) {
        break;
      }
    } while (opt == '\n');

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
      gchar *str;

      gst_message_parse_error (message, &err, &debug);
      str = gst_element_get_name (message->src);
      g_print ("%s error: %s\n", str, err->message);
      g_free (str);
      g_print ("Debug: %s\n", debug);

      g_error_free (err);
      g_free (debug);

      printf ("presse <ENTER> key to exit\n");
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
  gint numbuffers = -1;
  gchar device[128] = { '\0' };
  gchar input[128] = { '\0' };
  gulong frequency = 0;
  GstBus *bus;

  /* see for input option */

  int c;

  while (1) {
    static char long_options_desc[][64] = {
      {"Number of buffers to output before sending EOS"},
      {"Device location. Common in /dev/video0"},
      {"input/output (channel) to switch to"},
      {"frequency to tune to (in Hz)"},
      {0, 0, 0, 0}
    };
    static struct option long_options[] = {
      {"numbuffers", 1, 0, 'n'},
      {"device", 1, 0, 'd'},
      {"input", 1, 0, 'i'},
      {"frequency", 1, 0, 'f'},
      {0, 0, 0, 0}
    };
    /* getopt_long stores the option index here. */
    int option_index = 0;

    c = getopt_long (argc, argv, "n:d:i:f:h", long_options, &option_index);

    /* Detect the end of the options. */
    if (c == -1) {
      printf ("tip: use -h to see help message.\n");
      break;
    }

    switch (c) {
      case 0:
        /* If this option set a flag, do nothing else now. */
        if (long_options[option_index].flag != 0)
          break;
        printf ("option %s", long_options[option_index].name);
        if (optarg)
          printf (" with arg %s", optarg);
        printf ("\n");
        break;

      case 'n':
        numbuffers = atoi (optarg);
        break;

      case 'd':
        strncpy (device, optarg, sizeof (device) / sizeof (device[0]));
        break;

      case 'i':
        strncpy (input, optarg, sizeof (input) / sizeof (input[0]));
        break;

      case 'f':
        frequency = atol (optarg);
        break;

      case 'h':
        printf ("Usage: v4l2src-test [OPTION]...\n");
        for (c = 0; long_options[c].name; ++c) {
          printf ("-%c, --%s\r\t\t\t\t%s\n", long_options[c].val,
              long_options[c].name, long_options_desc[c]);
        }
        exit (0);
        break;

      case '?':
        /* getopt_long already printed an error message. */
        printf ("Use -h to see help message.\n");
        break;

      default:
        abort ();
    }
  }

  /* Print any remaining command line arguments (not options). */
  if (optind < argc) {
    printf ("Use -h to see help message.\n" "non-option ARGV-elements: ");
    while (optind < argc)
      printf ("%s ", argv[optind++]);
    putchar ('\n');
  }

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

  if (numbuffers > -1) {
    g_object_set (source, "num-buffers", numbuffers, NULL);
  }
  if (device[0]) {
    g_object_set (source, "device", device, NULL);
  }
  if (input[0]) {
    g_object_set (source, "input", input, NULL);
  }
  if (frequency) {
    g_object_set (source, "frequency", frequency, NULL);
  }

  /* you would normally check that the elements were created properly */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, NULL);

  /* put together a pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  gst_element_link_pads (source, "src", sink, "sink");

  /* start the pipeline */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  loop = g_main_loop_new (NULL, FALSE);

  input_thread = g_thread_try_new ("v4l2src-test", read_user, source, NULL);

  if (input_thread == NULL) {
    fprintf (stderr, "error: g_thread_try_new() failed");
    return -1;
  }

  g_main_loop_run (loop);
  g_thread_join (input_thread);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);

  gst_object_unref (bus);
  gst_object_unref (pipeline);

  gst_deinit ();

  return 0;

}
