---
short-description: The mandatory 'Hello world' example
...

# Basic tutorial 1: Hello world!

## Goal

Nothing better to get a first impression about a software library than
to print “Hello World” on the screen!

But since we are dealing with multimedia frameworks, we are going to
play a video instead.

Do not be scared by the amount of code below: there are only 4 lines
which do *real* work. The rest is cleanup code, and, in C, this is
always a bit verbose.

Without further ado, get ready for your first GStreamer application...

## Hello world

Copy this code into a text file named `basic-tutorial-1.c` (or find it
in the SDK installation).

**basic-tutorial-1.c**

{{ tutorials/basic-tutorial-1.c }}

Compile it as described in [Installing on Linux], [Installing on Mac OS
X] or [Installing on Windows]. If you get compilation errors,
double-check the instructions given in those sections.

If everything built fine, fire up the executable! You should see a
window pop up, containing a video being played straight from the
Internet, along with audio. Congratulations!

> ![Information] Need help?
>
> If you need help to compile this code, refer to the **Building the
> tutorials** section for your platform: [Linux], [Mac OS X] or
> [Windows], or use this specific command on Linux:
>
> `` gcc basic-tutorial-1.c -o basic-tutorial-1 `pkg-config --cflags --libs gstreamer-1.0` ``
>
> If you need help to run this code, refer to the **Running the
> tutorials** section for your platform: [Linux][1], [Mac OS X][2] or
> [Windows][3].
>
> This tutorial opens a window and displays a movie, with accompanying
> audio. The media is fetched from the Internet, so the window might
> take a few seconds to appear, depending on your connection speed.
> Also, there is no latency management (buffering), so on slow
> connections, the movie might stop after a few seconds. See how [Basic
> tutorial 12: Streaming] solves this issue.
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

Let's review these lines of code and see what they do:

``` c
    /* Initialize GStreamer */
    gst_init (&argc, &argv);
```

This must always be your first GStreamer command. Among other things,
`gst_init()`:

-   Initializes all internal structures

-   Checks what plug-ins are available

-   Executes any command-line option intended for GStreamer

If you always pass your command-line parameters
`argc` and `argv` to `gst_init()` your application will automatically
benefit from the GStreamer standard command-line options (more on this
in [Basic tutorial 10: GStreamer tools])

``` c
    /* Build the pipeline */
    pipeline = gst_parse_launch ("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);
```

This line is the heart of this tutorial, and exemplifies **two** key
points: `gst_parse_launch()` and `playbin`.

### gst\_parse\_launch

GStreamer is a framework designed to handle multimedia flows. Media
travels from the “source” elements (the producers), down to the “sink”
elements (the consumers), passing through a series of intermediate
elements performing all kinds of tasks. The set of all the
interconnected elements is called a “pipeline”.

In GStreamer you usually build the pipeline by manually assembling the
individual elements, but, when the pipeline is easy enough, and you do
not need any advanced features, you can take the shortcut:
`gst_parse_launch()`.

This function takes a textual representation of a pipeline and turns it
into an actual pipeline, which is very handy. In fact, this function is
so handy there is a tool built completely around it which you will get
very acquainted with (see [Basic tutorial 10: GStreamer tools][Basic
tutorial 10: GStreamer tools] to learn about `gst-launch-1.0` and the
`gst-launch-1.0` syntax).

### playbin

So, what kind of pipeline are we asking `gst_parse_launch()`to build for
us? Here enters the second key point: We are building a pipeline
composed of a single element called `playbin`.

`playbin` is a special element which acts as a source and as a sink, and
is a whole pipeline. Internally, it creates and connects all the
necessary elements to play your media, so you do not have to worry about
it.

It does not allow the control granularity that a manual pipeline does,
but, still, it permits enough customization to suffice for a wide range
of applications. Including this tutorial.

In this example, we are only passing one parameter to `playbin`, which
is the URI of the media we want to play. Try changing it to something
else! Whether it is an `http://` or `file://` URI, `playbin` will
instantiate the appropriate GStreamer source transparently!

If you mistype the URI, or the file does not exist, or you are missing a
plug-in, GStreamer provides several notification mechanisms, but the
only thing we are doing in this example is exiting on error, so do not
expect much feedback.

``` c
    /* Start playing */
    gst_element_set_state (pipeline, GST_STATE_PLAYING);
```

This line highlights another interesting concept: the state. Every
GStreamer element has an associated state, which you can more or less
think of as the Play/Pause button in your regular DVD player. For now,
suffice to say that playback will not start unless you set the pipeline
to the PLAYING state.

In this line, `gst_element_set_state()` is setting `pipeline` (our only
element, remember) to the PLAYING state, thus initiating playback.

``` c
    /* Wait until error or EOS */
    bus = gst_element_get_bus (pipeline);
    gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
```

These lines will wait until an error occurs or the end of the stream is
found. `gst_element_get_bus()` retrieves the pipeline's bus, and
`gst_bus_timed_pop_filtered()` will block until you receive either an
ERROR or an EOS (End-Of-Stream) through that bus. Do not worry much
about this line, the GStreamer bus is explained in [Basic tutorial 2:
GStreamer concepts].

And that's it! From this point onwards, GStreamer takes care of
everything. Execution will end when the media reaches its end (EOS) or
an error is encountered (try closing the video window, or unplugging the
network cable). The application can always be stopped by pressing
control-C in the console.

### Cleanup

Before terminating the application, though, there is a couple of things
we need to do to tidy up correctly after ourselves.

``` c
    /* Free resources */
    if (msg != NULL)
      gst_message_unref (msg);
    gst_object_unref (bus);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);
```

Always read the documentation of the functions you use, to know if you
should free the objects they return after using them.

In this case, `gst_bus_timed_pop_filtered()` returned a message which
needs to be freed with `gst_message_unref()` (more about messages in
[Basic tutorial 2: GStreamer concepts][Basic tutorial 2: GStreamer
concepts]).

`gst_element_get_bus()` added a reference to the bus that must be freed
with `gst_object_unref()`. Setting the pipeline to the NULL state will
make sure it frees any resources it has allocated (More about states in
[Basic tutorial 3: Dynamic pipelines]). Finally, unreferencing the
pipeline will destroy it, and all its contents.

_______________________________________________________________________________

## Conclusion

And so ends your first tutorial with GStreamer. We hope its brevity
serves as an example of how powerful this framework is!

Let's recap a bit. Today we have learned:

-   How to initialize GStreamer using `gst_init()`.

-   How to quickly build a pipeline from a textual description using
    `gst_parse_launch()`.

-   How to create an automatic playback pipeline using `playbin`.

-   How to signal GStreamer to start playback using
    `gst_element_set_state()`.

-   How to sit back and relax, while GStreamer takes care of everything,
    using `gst_element_get_bus()` and `gst_bus_timed_pop_filtered()`.

The next tutorial will keep introducing more basic GStreamer elements,
and show you how to build a pipeline manually.

It has been a pleasure having you here, and see you soon!

  [Installing on Linux]: installing/on-linux.md
  [Installing on Mac OS X]: installing/on-mac-osx.md
  [Installing on Windows]: installing/on-windows.md
  [Information]: images/icons/emoticons/information.png
  [Linux]: installing/on-linux.md#InstallingonLinux-Build
  [Mac OS X]: installing/on-mac-osx.md#InstallingonMacOSX-Build
  [Windows]: installing/on-windows.md#InstallingonWindows-Build
  [1]: installing/on-linux.md#InstallingonLinux-Run
  [2]: installing/on-mac-osx.md#InstallingonMacOSX-Run
  [3]: installing/on-windows.md#InstallingonWindows-Run
  [Basic tutorial 12: Streaming]: tutorials/basic/streaming.md
  [Basic tutorial 10: GStreamer tools]: tutorials/basic/gstreamer-tools.md
  [Basic tutorial 2: GStreamer concepts]: tutorials/basic/concepts.md
  [Basic tutorial 3: Dynamic pipelines]: tutorials/basic/dynamic-pipelines.md
