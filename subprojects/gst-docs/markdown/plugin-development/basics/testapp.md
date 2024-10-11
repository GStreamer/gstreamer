---
title: Building a Test Application
...

# Building a Test Application

Often, you will want to test your newly written plugin in as small
a setting as possible. Usually, `gst-launch-1.0` is a good first step at
testing a plugin. If you have not installed your plugin in a directory
that GStreamer searches, then you will need to set the plugin path.
Either set GST\_PLUGIN\_PATH to the directory containing your plugin, or
use the command-line option --gst-plugin-path. If you based your plugin
off of the gst-plugin template, then this will look something like `
gst-launch-1.0 --gst-plugin-path=$HOME/gst-template/gst-plugin/src/.libs
TESTPIPELINE
` However, you will often need more testing features than gst-launch-1.0
can provide, such as seeking, events, interactivity and more. Writing
your own small testing program is the easiest way to accomplish this.
This section explains - in a few words - how to do that. For a complete
application development guide, see the [Application Development
Manual](application-development/index.md).

At the start, you need to initialize the GStreamer core library by
calling `gst_init ()`. You can alternatively call
`gst_init_get_option_group ()`, which will return a pointer to
GOptionGroup. You can then use GOption to handle the initialization, and
this will finish the GStreamer initialization.

You can create elements using `gst_element_factory_make ()`, where the
first argument is the element type that you want to create, and the
second argument is a free-form name. The example at the end uses a
simple filesource - decoder - soundcard output pipeline, but you can use
specific debugging elements if that's necessary. For example, an
`identity` element can be used in the middle of the pipeline to act as a
data-to-application transmitter. This can be used to check the data for
misbehaviours or correctness in your test application. Also, you can use
a `fakesink` element at the end of the pipeline to dump your data to the
stdout (in order to do this, set the `dump` property to TRUE). Lastly,
you can use valgrind to check for memory errors.

During pipeline linking, your test application can use filtered caps as a way to
drive a specific type of data to or from your element. This is a very
simple and effective way of checking multiple types of input and output
in your element.

Note that during running, you should listen for at least the “error” and
“eos” messages on the bus and/or your plugin/element to check for
correct handling of this. Also, you should add events into the pipeline
and make sure your plugin handles these correctly (with respect to
clocking, internal caching, etc.).

Never forget to clean up memory in your plugin or your test application.
When going to the NULL state, your element should clean up allocated
memory and caches. Also, it should close down any references held to
possible support libraries. Your application should `unref ()` the
pipeline and make sure it doesn't crash.

``` c
#include <gst/gst.h>

static gboolean
bus_call (GstBus     *bus,
      GstMessage *msg,
      gpointer    data)
{
  GMainLoop *loop = data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      g_print ("End-of-stream\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR: {
      gchar *debug = NULL;
      GError *err = NULL;

      gst_message_parse_error (msg, &err, &debug);

      g_print ("Error: %s\n", err->message);
      g_error_free (err);

      if (debug) {
        g_print ("Debug details: %s\n", debug);
        g_free (debug);
      }

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

gint
main (gint   argc,
      gchar *argv[])
{
  GstStateChangeReturn ret;
  GstElement *pipeline, *filesrc, *decoder, *filter, *sink;
  GstElement *convert1, *convert2, *resample;
  GMainLoop *loop;
  GstBus *bus;
  guint watch_id;

  /* initialization */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);
  if (argc != 2) {
    g_print ("Usage: %s <mp3 filename>\n", argv[0]);
    return 01;
  }

  /* create elements */
  pipeline = gst_pipeline_new ("my_pipeline");

  /* watch for messages on the pipeline's bus (note that this will only
   * work like this when a GLib main loop is running) */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  filesrc  = gst_element_factory_make ("filesrc", "my_filesource");
  decoder  = gst_element_factory_make ("mad", "my_decoder");

  /* putting an audioconvert element here to convert the output of the
   * decoder into a format that my_filter can handle (we are assuming it
   * will handle any sample rate here though) */
  convert1 = gst_element_factory_make ("audioconvert", "audioconvert1");

  /* use "identity" here for a filter that does nothing */
  filter   = gst_element_factory_make ("my_filter", "my_filter");

  /* there should always be audioconvert and audioresample elements before
   * the audio sink, since the capabilities of the audio sink usually vary
   * depending on the environment (output used, sound card, driver etc.) */
  convert2 = gst_element_factory_make ("audioconvert", "audioconvert2");
  resample = gst_element_factory_make ("audioresample", "audioresample");
  sink     = gst_element_factory_make ("pulsesink", "audiosink");

  if (!sink || !decoder) {
    g_print ("Decoder or output could not be found - check your install\n");
    return -1;
  } else if (!convert1 || !convert2 || !resample) {
    g_print ("Could not create audioconvert or audioresample element, "
             "check your installation\n");
    return -1;
  } else if (!filter) {
    g_print ("Your self-written filter could not be found. Make sure it "
             "is installed correctly in $(libdir)/gstreamer-1.0/ or "
             "~/.gstreamer-1.0/plugins/ and that gst-inspect-1.0 lists it. "
             "If it doesn't, check with 'GST_DEBUG=*:2 gst-inspect-1.0' for "
             "the reason why it is not being loaded.");
    return -1;
  }

  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);

  gst_bin_add_many (GST_BIN (pipeline), filesrc, decoder, convert1, filter,
                    convert2, resample, sink, NULL);

  /* link everything together */
  if (!gst_element_link_many (filesrc, decoder, convert1, filter, convert2,
                              resample, sink, NULL)) {
    g_print ("Failed to link one or more elements!\n");
    return -1;
  }

  /* run */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    GstMessage *msg;

    g_print ("Failed to start up pipeline!\n");

    /* check if there is an error message with details on the bus */
    msg = gst_bus_poll (bus, GST_MESSAGE_ERROR, 0);
    if (msg) {
      GError *err = NULL;

      gst_message_parse_error (msg, &err, NULL);
      g_print ("ERROR: %s\n", err->message);
      g_error_free (err);
      gst_message_unref (msg);
    }
    return -1;
  }

  g_main_loop_run (loop);

  /* clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  g_source_remove (watch_id);
  g_main_loop_unref (loop);

  return 0;
}

```
