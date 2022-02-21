---
title: Your first application
...

# Your first application

This chapter will summarize everything you've learned in the previous
chapters. It describes all aspects of a simple GStreamer application,
including initializing libraries, creating elements, packing elements
together in a pipeline and playing this pipeline. By doing all this, you
will be able to build a simple Ogg/Vorbis audio player.

## Hello world

We're going to create a simple first application, a simple Ogg/Vorbis
command-line audio player. For this, we will use only standard GStreamer
components. The player will read a file specified on the command-line.
Let's get started\!

We've learned, in [Initializing GStreamer][gst-init], that the
first thing to do in your application is to initialize GStreamer by
calling `gst_init ()`. Also, make sure that the application includes
`gst/gst.h` so all function names and objects are properly defined. Use
`#include
<gst/gst.h>` to do that.

Next, you'll want to create the different elements using
`gst_element_factory_make ()`. For an Ogg/Vorbis audio player, we'll
need a source element that reads files from a disk. GStreamer includes
this element under the name “filesrc”. Next, we'll need something to
parse the file and decode it into raw audio. GStreamer has two elements
for this: the first parses Ogg streams into elementary streams (video,
audio) and is called “oggdemux”. The second is a Vorbis audio decoder,
it's conveniently called “vorbisdec”. Since “oggdemux” creates dynamic
pads for each elementary stream, you'll need to set a “pad-added” event
handler on the “oggdemux” element, like you've learned in [Dynamic (or
sometimes) pads][dynamic-pads], to link the
Ogg demuxer and the Vorbis decoder elements together. At last, we'll
also need an audio output element, we will use “autoaudiosink”, which
automatically detects your audio device.

The last thing left to do is to add all elements into a container
element, a `GstPipeline`, and wait until we've played the whole song.
We've previously learned how to add elements to a container bin in
[Bins][bins], and we've learned about element states in
[Element States][element-states]. We will also attach a message handler to
the pipeline bus so we can retrieve errors and detect the end-of-stream.

Let's now add all the code together to get our very first audio player:


``` c
#include <gst/gst.h>
#include <glib.h>


static gboolean
bus_call (GstBus     *bus,
          GstMessage *msg,
          gpointer    data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_print ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}


static void
on_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    data)
{
  GstPad *sinkpad;
  GstElement *decoder = (GstElement *) data;

  /* We can now link this pad with the vorbis-decoder sink pad */
  g_print ("Dynamic pad created, linking demuxer/decoder\n");

  sinkpad = gst_element_get_static_pad (decoder, "sink");

  gst_pad_link (pad, sinkpad);

  gst_object_unref (sinkpad);
}



int
main (int   argc,
      char *argv[])
{
  GMainLoop *loop;

  GstElement *pipeline, *source, *demuxer, *decoder, *conv, *sink;
  GstBus *bus;
  guint bus_watch_id;

  /* Initialisation */
  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);


  /* Check input arguments */
  if (argc != 2) {
    g_printerr ("Usage: %s <Ogg/Vorbis filename>\n", argv[0]);
    return -1;
  }


  /* Create gstreamer elements */
  pipeline = gst_pipeline_new ("audio-player");
  source   = gst_element_factory_make ("filesrc",       "file-source");
  demuxer  = gst_element_factory_make ("oggdemux",      "ogg-demuxer");
  decoder  = gst_element_factory_make ("vorbisdec",     "vorbis-decoder");
  conv     = gst_element_factory_make ("audioconvert",  "converter");
  sink     = gst_element_factory_make ("autoaudiosink", "audio-output");

  if (!pipeline || !source || !demuxer || !decoder || !conv || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  /* Set up the pipeline */

  /* we set the input filename to the source element */
  g_object_set (G_OBJECT (source), "location", argv[1], NULL);

  /* we add a message handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
  gst_object_unref (bus);

  /* we add all elements into the pipeline */
  /* file-source | ogg-demuxer | vorbis-decoder | converter | alsa-output */
  gst_bin_add_many (GST_BIN (pipeline),
                    source, demuxer, decoder, conv, sink, NULL);

  /* we link the elements together */
  /* file-source -> ogg-demuxer ~> vorbis-decoder -> converter -> alsa-output */
  gst_element_link (source, demuxer);
  gst_element_link_many (decoder, conv, sink, NULL);
  g_signal_connect (demuxer, "pad-added", G_CALLBACK (on_pad_added), decoder);

  /* note that the demuxer will be linked to the decoder dynamically.
     The reason is that Ogg may contain various streams (for example
     audio and video). The source pad(s) will be created at run time,
     by the demuxer when it detects the amount and nature of streams.
     Therefore we connect a callback function which will be executed
     when the "pad-added" is emitted.*/


  /* Set the pipeline to "playing" state*/
  g_print ("Now playing: %s\n", argv[1]);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);


  /* Iterate */
  g_print ("Running...\n");
  g_main_loop_run (loop);


  /* Out of the main loop, clean up nicely */
  g_print ("Returned, stopping playback\n");
  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_print ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pipeline));
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}

```

We now have created a complete pipeline. We can visualise the pipeline
as follows:

![The "hello world" pipeline](images/hello-world.png "fig:")

[gst-init]: application-development/basics/init.md
[advanced]: application-development/advanced/index.md
[dynamic-pads]: application-development/basics/pads.md#dynamic-or-sometimes-pads
[bins]: application-development/basics/bins.md
[element-states]: application-development/basics/elements.md#element-states

## Compiling and Running helloworld.c

To compile the helloworld example, use:

```
gcc -Wall helloworld.c -o helloworld $(pkg-config --cflags --libs gstreamer-1.0)
```

GStreamer makes use of `pkg-config` to get compiler and linker
flags needed to compile this application.

If you're running a non-standard installation (ie. you've installed
GStreamer from source yourself instead of using pre-built packages),
make sure the `PKG_CONFIG_PATH` environment variable is set to the
correct location (`$libdir/pkgconfig`).

In the unlikely case that you are using the GStreamer development environment
(ie. gst-env), you will need to use libtool to build the hello
world program, like this:

```
libtool --mode=link gcc -Wall helloworld.c -o helloworld $(pkg-config --cflags --libs gstreamer-1.0)
```

You can run this example application with `./helloworld
file.ogg`. Substitute `file.ogg` with your favourite Ogg/Vorbis file.

## Conclusion

This concludes our first example. As you see, setting up a pipeline is
very low-level but powerful. You will see later in this manual how you
can create a more powerful media player with even less effort using
higher-level interfaces. We will discuss all that in [Higher-level
interfaces for GStreamer applications][highlevel]. We will
first, however, go more in-depth into more advanced GStreamer internals.

It should be clear from the example that we can very easily replace the
“filesrc” element with some other element that reads data from a
network, or some other data source element that is better integrated
with your desktop environment. Also, you can use other decoders and
parsers/demuxers to support other media types. You can use another audio
sink if you're not running Linux, but Mac OS X, Windows or FreeBSD, or
you can instead use a filesink to write audio files to disk instead of
playing them back. By using an audio card source, you can even do audio
capture instead of playback. All this shows the reusability of GStreamer
elements, which is its greatest advantage.

[highlevel]: application-development/highlevel/index.md
