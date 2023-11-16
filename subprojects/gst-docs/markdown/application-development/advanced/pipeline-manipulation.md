---
title: Pipeline manipulation
...

# Pipeline manipulation

This chapter presents many ways in which you can manipulate pipelines from
your application. These are some of the topics that will be covered:

- How to insert data from an application into a pipeline
- How to read data from a pipeline
- How to manipulate the pipeline's speed, length and starting point
- How to *listen* to a pipeline's data processing.

Parts of this chapter are very low level so you'll need some programming
experience and a good understanding of GStreamer to follow them.

## Using probes

Probing is best envisioned as having access to a pad listener. Technically, a
probe is nothing more than a callback that can be attached to a pad using
`gst_pad_add_probe ()`. Conversely, you can use `gst_pad_remove_probe ()` to
remove the callback. While attached, the probe notifies you of any activity
on the pad. You can define what kind of notifications you are interested in when
you add the probe.

Probe types:

  - A buffer is pushed or pulled. You want to specify the
    `GST_PAD_PROBE_TYPE_BUFFER` when registering the probe. Because
    the pad can be scheduled in different ways. It is also possible to
    specify in what scheduling mode you are interested with the optional
    `GST_PAD_PROBE_TYPE_PUSH` and `GST_PAD_PROBE_TYPE_PULL` flags.
    You can use this probe to inspect, modify or drop the buffer. See
    [Data probes](#data-probes).

  - A buffer list is pushed. Use the `GST_PAD_PROBE_TYPE_BUFFER_LIST`
    when registering the probe.

  - An event travels over a pad. Use the
    `GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM` and
    `GST_PAD_PROBE_TYPE_EVENT_UPSTREAM` flags to select downstream
    and upstream events. There is also a convenience
    `GST_PAD_PROBE_TYPE_EVENT_BOTH` to be notified of events going
    in both directions. By default, flush events do not cause
    a notification. You need to explicitly enable
    `GST_PAD_PROBE_TYPE_EVENT_FLUSH` to receive callbacks from
    flushing events. Events are always only notified in push mode.
    You can use this type of probe to inspect, modify or drop the event.

  - A query travels over a pad. Use the
    `GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM` and
    `GST_PAD_PROBE_TYPE_QUERY_UPSTREAM` flags to select downstream
    and upstream queries. The convenience
    `GST_PAD_PROBE_TYPE_QUERY_BOTH` can also be used to select both
    directions. Query probes are notified twice: when the query
    travels upstream/downstream and when the query result is
    returned. You can select in what stage the callback will be called
    with the `GST_PAD_PROBE_TYPE_PUSH` and
    `GST_PAD_PROBE_TYPE_PULL`, respectively when the query is
    performed and when the query result is returned.

    You can use a query probe to inspect or modify queries, or even to answer
    them in the probe callback. To answer a query you place the result value
    in the query and return `GST_PAD_PROBE_DROP` from the callback.

  - In addition to notifying you of dataflow, you can also ask the probe
    to block the dataflow when the callback returns. This is called a
    blocking probe and is activated by specifying the
    `GST_PAD_PROBE_TYPE_BLOCK` flag. You can use this flag with the
    other flags to only block dataflow on selected activity. A pad
    becomes unblocked again if you remove the probe or when you return
    `GST_PAD_PROBE_REMOVE` from the callback. You can let only the
    currently blocked item pass by returning `GST_PAD_PROBE_PASS` from
    the callback, it will block again on the next item.

    Blocking probes are used to temporarily block pads because they are
    unlinked or because you are going to unlink them. If the dataflow is
    not blocked, the pipeline would go into an error state if data is
    pushed on an unlinked pad. We will see how to use blocking probes to
    partially preroll a pipeline. See also [Play a section of a media
    file](#play-a-section-of-a-media-file).

  - Be notified when no activity is happening on a pad. You install this
    probe with the `GST_PAD_PROBE_TYPE_IDLE` flag. You can specify
    `GST_PAD_PROBE_TYPE_PUSH` and/or `GST_PAD_PROBE_TYPE_PULL` to
    only be notified depending on the pad scheduling mode. The IDLE
    probe is also a blocking probe in that it will not let any data pass
    on the pad for as long as the IDLE probe is installed.

    You can use idle probes to dynamically relink a pad. We will see how
    to use idle probes to replace an element in the pipeline. See also
    [Dynamically changing the
    pipeline](#dynamically-changing-the-pipeline).

### Data probes

Data probes notify you when there is data passing on a pad. Pass
`GST_PAD_PROBE_TYPE_BUFFER` and/or `GST_PAD_PROBE_TYPE_BUFFER_LIST` to
`gst_pad_add_probe ()` for creating this kind of probe. Most common buffer
operations elements can do in `_chain ()` functions, can be done in probe
callbacks.

Data probes run in the pipeline's streaming thread context, so callbacks
should try to avoid blocking and generally, avoid doing weird stuff. Doing so
could have a negative impact on the pipeline's performance or, in case of bugs,
lead to deadlocks or crashes. More precisely, one should usually avoid calling
GUI-related functions from within a probe callback, nor try to change the state
of the pipeline. An application may post custom messages on the pipeline's bus
to communicate with the main application thread and have it do things like stop
the pipeline.

The following is an example on using data probes. Compare this program's output
with that of `gst-launch-1.0 videotestsrc !  xvimagesink` if you are not
sure what to look for:

``` c
#include <gst/gst.h>

static GstPadProbeReturn
cb_have_data (GstPad          *pad,
              GstPadProbeInfo *info,
              gpointer         user_data)
{
  gint x, y;
  GstMapInfo map;
  guint16 *ptr, t;
  GstBuffer *buffer;

  buffer = GST_PAD_PROBE_INFO_BUFFER (info);

  buffer = gst_buffer_make_writable (buffer);

  /* Making a buffer writable can fail (for example if it
   * cannot be copied and is used more than once)
   */
  if (buffer == NULL)
    return GST_PAD_PROBE_OK;

  /* Mapping a buffer can fail (non-writable) */
  if (gst_buffer_map (buffer, &map, GST_MAP_WRITE)) {
    ptr = (guint16 *) map.data;
    /* invert data */
    for (y = 0; y < 288; y++) {
      for (x = 0; x < 384 / 2; x++) {
        t = ptr[384 - 1 - x];
        ptr[384 - 1 - x] = ptr[x];
        ptr[x] = t;
      }
      ptr += 384;
    }
    gst_buffer_unmap (buffer, &map);
  }

  GST_PAD_PROBE_INFO_DATA (info) = buffer;

  return GST_PAD_PROBE_OK;
}

gint
main (gint   argc,
      gchar *argv[])
{
  GMainLoop *loop;
  GstElement *pipeline, *src, *sink, *filter, *csp;
  GstCaps *filtercaps;
  GstPad *pad;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* build */
  pipeline = gst_pipeline_new ("my-pipeline");
  src = gst_element_factory_make ("videotestsrc", "src");
  if (src == NULL)
    g_error ("Could not create 'videotestsrc' element");

  filter = gst_element_factory_make ("capsfilter", "filter");
  g_assert (filter != NULL); /* should always exist */

  csp = gst_element_factory_make ("videoconvert", "csp");
  if (csp == NULL)
    g_error ("Could not create 'videoconvert' element");

  sink = gst_element_factory_make ("xvimagesink", "sink");
  if (sink == NULL) {
    sink = gst_element_factory_make ("ximagesink", "sink");
    if (sink == NULL)
      g_error ("Could not create neither 'xvimagesink' nor 'ximagesink' element");
  }

  gst_bin_add_many (GST_BIN (pipeline), src, filter, csp, sink, NULL);
  gst_element_link_many (src, filter, csp, sink, NULL);
  filtercaps = gst_caps_new_simple ("video/x-raw",
               "format", G_TYPE_STRING, "RGB16",
               "width", G_TYPE_INT, 384,
               "height", G_TYPE_INT, 288,
               "framerate", GST_TYPE_FRACTION, 25, 1,
               NULL);
  g_object_set (G_OBJECT (filter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);

  pad = gst_element_get_static_pad (src, "src");
  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) cb_have_data, NULL, NULL);
  gst_object_unref (pad);

  /* run */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* wait until it's up and running or failed */
  if (gst_element_get_state (pipeline, NULL, NULL, -1) == GST_STATE_CHANGE_FAILURE) {
    g_error ("Failed to go into PLAYING state");
  }

  g_print ("Running ...\n");
  g_main_loop_run (loop);

  /* exit */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  return 0;
}
```

Strictly speaking, a pad probe callback is only allowed to modify the
buffer content if the buffer is writable. Whether this is the case or
not depends a lot on the pipeline and the elements involved. Often
enough, this is the case, but sometimes it is not, and if it is not then
unexpected modification of the data or metadata can introduce bugs that
are very hard to debug and track down. You can check if a buffer is
writable with `gst_buffer_is_writable ()`. Since you can pass back a
different buffer than the one passed in, it is a good idea to make the
buffer writable in the callback function with `gst_buffer_make_writable
()`.

Pad probes are best suited for looking at data as it passes through the
pipeline. If you need to modify data, you should rather write your own
GStreamer element. Base classes like `GstAudioFilter`, `GstVideoFilter` or
`GstBaseTransform` make this fairly easy.

If you just want to inspect buffers as they pass through the pipeline,
you don't even need to set up pad probes. You could also just insert an
identity element into the pipeline and connect to its "handoff" signal.
The identity element also provides a few useful debugging tools like the
`dump` and `last-message` properties; the latter is enabled by
passing the '-v' switch to `gst-launch` and setting the `silent` property
on the identity to `FALSE`.

### Play a section of a media file

In this example we will show you how to play back a section of a media
file. The goal is to only play the part from 2 to 5 seconds and then
quit.

In a first step we will set a `uridecodebin` element to the `PAUSED` state
and make sure that we block all the source pads that are created. When
all the source pads are blocked, we have data on all of them and we say
that the `uridecodebin` is prerolled.

In a prerolled pipeline we can ask for the duration of the media and we
can also perform seeks. We are interested in performing a seek operation
on the pipeline to select the 2-to-5-seconds section.

After we configure the section we want, we can link the sink element, unblock the
source pads and set the pipeline to the `PLAYING` state. You will see that
exactly the requested region is displayed by the sink before it goes to `EOS`.

Here is the code:

``` c
#include <gst/gst.h>

static GMainLoop *loop;
static gint counter;
static GstBus *bus;
static gboolean prerolled = FALSE;
static GstPad *sinkpad;

static void
dec_counter (GstElement * pipeline)
{
  if (prerolled)
    return;

  if (g_atomic_int_dec_and_test (&counter)) {
    /* all probes blocked and no-more-pads signaled, post
     * message on the bus. */
    prerolled = TRUE;

    gst_bus_post (bus, gst_message_new_application (
          GST_OBJECT_CAST (pipeline),
          gst_structure_new_empty ("ExPrerolled")));
  }
}

/* called when a source pad of uridecodebin is blocked */
static GstPadProbeReturn
cb_blocked (GstPad          *pad,
            GstPadProbeInfo *info,
            gpointer         user_data)
{
  GstElement *pipeline = GST_ELEMENT (user_data);

  if (prerolled)
    return GST_PAD_PROBE_REMOVE;

  dec_counter (pipeline);

  return GST_PAD_PROBE_OK;
}

/* called when uridecodebin has a new pad */
static void
cb_pad_added (GstElement *element,
              GstPad     *pad,
              gpointer    user_data)
{
  GstElement *pipeline = GST_ELEMENT (user_data);

  if (prerolled)
    return;

  g_atomic_int_inc (&counter);

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      (GstPadProbeCallback) cb_blocked, pipeline, NULL);

  /* try to link to the video pad */
  gst_pad_link (pad, sinkpad);
}

/* called when uridecodebin has created all pads */
static void
cb_no_more_pads (GstElement *element,
                 gpointer    user_data)
{
  GstElement *pipeline = GST_ELEMENT (user_data);

  if (prerolled)
    return;

  dec_counter (pipeline);
}

/* called when a new message is posted on the bus */
static void
cb_message (GstBus     *bus,
            GstMessage *message,
            gpointer    user_data)
{
  GstElement *pipeline = GST_ELEMENT (user_data);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("we received an error!\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("we reached EOS\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_APPLICATION:
    {
      if (gst_message_has_name (message, "ExPrerolled")) {
        /* it's our message */
        g_print ("we are all prerolled, do seek\n");
        gst_element_seek (pipeline,
            1.0, GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE,
            GST_SEEK_TYPE_SET, 2 * GST_SECOND,
            GST_SEEK_TYPE_SET, 5 * GST_SECOND);

        gst_element_set_state (pipeline, GST_STATE_PLAYING);
      }
      break;
    }
    default:
      break;
  }
}

gint
main (gint   argc,
      gchar *argv[])
{
  GstElement *pipeline, *src, *csp, *vs, *sink;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  if (argc < 2) {
    g_print ("usage: %s <uri>", argv[0]);
    return -1;
  }

  /* build */
  pipeline = gst_pipeline_new ("my-pipeline");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) cb_message,
      pipeline);

  src = gst_element_factory_make ("uridecodebin", "src");
  if (src == NULL)
    g_error ("Could not create 'uridecodebin' element");

  g_object_set (src, "uri", argv[1], NULL);

  csp = gst_element_factory_make ("videoconvert", "csp");
  if (csp == NULL)
    g_error ("Could not create 'videoconvert' element");

  vs = gst_element_factory_make ("videoscale", "vs");
  if (vs == NULL)
    g_error ("Could not create 'videoscale' element");

  sink = gst_element_factory_make ("autovideosink", "sink");
  if (sink == NULL)
    g_error ("Could not create 'autovideosink' element");

  gst_bin_add_many (GST_BIN (pipeline), src, csp, vs, sink, NULL);

  /* can't link src yet, it has no pads */
  gst_element_link_many (csp, vs, sink, NULL);

  sinkpad = gst_element_get_static_pad (csp, "sink");

  /* for each pad block that is installed, we will increment
   * the counter. for each pad block that is signaled, we
   * decrement the counter. When the counter is 0 we post
   * an app message to tell the app that all pads are
   * blocked. Start with 1 that is decremented when no-more-pads
   * is signaled to make sure that we only post the message
   * after no-more-pads */
  g_atomic_int_set (&counter, 1);

  g_signal_connect (src, "pad-added",
      (GCallback) cb_pad_added, pipeline);
  g_signal_connect (src, "no-more-pads",
      (GCallback) cb_no_more_pads, pipeline);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (sinkpad);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
  g_main_loop_unref (loop);

  return 0;
}
```

Note that we use a custom application message to signal the main thread that the
`uridecodebin` is prerolled. The main thread will then issue a flushing seek to
the requested region. The flush will temporarily unblock the pad and reblock
them when new data arrives again. We detect this second block to remove the
probes. Then we set the pipeline to `PLAYING` and it should play the selected
2-to-5-seconds section; the application waits for the `EOS` message and quits.

## Manually adding or removing data from/to a pipeline

Many people have expressed the wish to use their own sources to inject data into
a pipeline, others, the wish to grab a pipeline's output and take care of it in
their application. While these methods are strongly discouraged, GStreamer
offers support for them -- *Beware\! You need to know what you are doing* --.
Since you don't have any support from a base class you need to thoroughly
understand state changes and synchronization. If it doesn't work, there are a
million ways to shoot yourself in the foot. It's always better to simply write a
plugin and have the base class manage it. See the Plugin Writer's Guide for more
information on this topic. Additionally, review the next section, which explains
how to statically embed plugins in your application.

There are two possible elements that you can use for the above-mentioned
purposes: `appsrc` (an imaginary source) and `appsink` (an imaginary sink). The
same method applies to these elements. We will discuss how to use them to insert
(using `appsrc`) or to grab (using `appsink`) data from a pipeline, and how to set
negotiation.

Both `appsrc` and `appsink` provide 2 sets of API. One API uses standard
`GObject` (action) signals and properties. The same API is also available
as a regular C API. The C API is more performant but requires you to
link to the app library in order to use the elements.

### Inserting data with appsrc

Let's take a look at `appsrc` and how to insert application data into the
pipeline.

`appsrc` has some configuration options that control the way it operates. You
should decide about the following:

  - Will `appsrc` operate in push or pull mode. The `stream-type`
    property can be used to control this. A `random-access` `stream-type`
    will make `appsrc` activate pull mode scheduling while the other
    `stream-types` activate push mode.

  - The caps of the buffers that `appsrc` will push out. This needs to be
    configured with the `caps` property. This property must be set to a fixed
    caps and will be used to negotiate a format downstream.

  - Whether `appsrc` operates in live mode or not. This is configured
    with the `is-live` property. When operating in live-mode it is
    also important to set the `min-latency` and `max-latency` properties.
    `min-latency` should be set to the amount of time it takes between
    capturing a buffer and when it is pushed inside `appsrc`. In live
    mode, you should timestamp the buffers with the pipeline `running-time`
    when the first byte of the buffer was captured before feeding them to
    `appsrc`. You can let `appsrc` do the timestamping with
    the `do-timestamp` property, but then the `min-latency` must be set to 0
    because `appsrc` timestamps based on what was the `running-time` when it got
    a given buffer.

  - The format of the SEGMENT event that `appsrc` will push. This format
    has implications for how the buffers' `running-time` will be calculated,
    so you must be sure you understand this. For live sources
    you probably want to set the format property to `GST_FORMAT_TIME`.
    For non-live sources, it depends on the media type that you are
    handling. If you plan to timestamp the buffers, you should probably
    use `GST_FORMAT_TIME` as format, if you don't, `GST_FORMAT_BYTES` might
    be appropriate.

  - If `appsrc` operates in random-access mode, it is important to
    configure the size property with the number of bytes in the stream. This
    will allow downstream elements to know the size of the media and seek to the
    end of the stream when needed.

The main way of handling data to `appsrc` is by using the
`gst_app_src_push_buffer ()` function or by emitting the `push-buffer` action
signal. This will put the buffer onto a queue from which `appsrc` will
read in its streaming thread. It's important to note that data
transport will not happen from the thread that performed the `push-buffer`
call.

The `max-bytes` property controls how much data can be queued in `appsrc`
before `appsrc` considers the queue full. A filled internal queue will
always signal the `enough-data` signal, which signals the application
that it should stop pushing data into `appsrc`. The `block` property will
cause `appsrc` to block the `push-buffer` method until free data becomes
available again.

When the internal queue is running out of data, the `need-data` signal
is emitted, which signals the application that it should start pushing
more data into `appsrc`.

In addition to the `need-data` and `enough-data` signals, `appsrc` can
emit `seek-data` when the `stream-mode` property is set to
`seekable` or `random-access`. The signal argument will contain the
new desired position in the stream expressed in the unit set with the
`format` property. After receiving the `seek-data` signal, the
application should push buffers from the new position.

When the last byte is pushed into `appsrc`, you must call
`gst_app_src_end_of_stream ()` to make it send an `EOS` downstream.

These signals allow the application to operate `appsrc` in push and pull
mode as will be explained next.

#### Using appsrc in push mode

When `appsrc` is configured in push mode (`stream-type` is stream or
seekable), the application repeatedly calls the `push-buffer` method with
a new buffer. Optionally, the queue size in the `appsrc` can be controlled
with the `enough-data` and `need-data` signals by respectively
stopping/starting the `push-buffer` calls. The value of the `min-percent`
property defines how empty the internal `appsrc` queue needs to be before
the `need-data` signal is issued. You can set this to some positive value
to avoid completely draining the queue.

Don't forget to implement a `seek-data` callback when the `stream-type` is
set to `GST_APP_STREAM_TYPE_SEEKABLE`.

Use this mode when implementing various network protocols or hardware
devices.

#### Using appsrc in pull mode

In pull mode, data is fed to `appsrc` from the `need-data` signal
handler. You should push exactly the amount of bytes requested in the
`need-data` signal. You are only allowed to push less bytes when you are
at the end of the stream.

Use this mode for file access or other randomly accessible sources.

#### Appsrc example

This example application will generate black/white (it switches every
second) video to an Xv-window output by using `appsrc` as a source with
caps to force a format. We use a colorspace conversion element to make
sure that we feed the right format to the X server. We configure a
video stream with a variable framerate (0/1) and we set the timestamps
on the outgoing buffers in such a way that we play 2 frames per second.

Note how we use the pull mode method of pushing new buffers into `appsrc`
although `appsrc` is running in push mode.

``` c
#include <gst/gst.h>

static GMainLoop *loop;

static void
cb_need_data (GstElement *appsrc,
          guint       unused_size,
          gpointer    user_data)
{
  static gboolean white = FALSE;
  static GstClockTime timestamp = 0;
  GstBuffer *buffer;
  guint size;
  GstFlowReturn ret;

  size = 385 * 288 * 2;

  buffer = gst_buffer_new_allocate (NULL, size, NULL);

  /* this makes the image black/white */
  gst_buffer_memset (buffer, 0, white ? 0xff : 0x0, size);

  white = !white;

  GST_BUFFER_PTS (buffer) = timestamp;
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 2);

  timestamp += GST_BUFFER_DURATION (buffer);

  g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
  gst_buffer_unref (buffer);

  if (ret != GST_FLOW_OK) {
    /* something wrong, stop pushing */
    g_main_loop_quit (loop);
  }
}

gint
main (gint   argc,
      gchar *argv[])
{
  GstElement *pipeline, *appsrc, *conv, *videosink;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* setup pipeline */
  pipeline = gst_pipeline_new ("pipeline");
  appsrc = gst_element_factory_make ("appsrc", "source");
  conv = gst_element_factory_make ("videoconvert", "conv");
  videosink = gst_element_factory_make ("xvimagesink", "videosink");

  /* setup */
  g_object_set (G_OBJECT (appsrc), "caps",
        gst_caps_new_simple ("video/x-raw",
                     "format", G_TYPE_STRING, "RGB16",
                     "width", G_TYPE_INT, 384,
                     "height", G_TYPE_INT, 288,
                     "framerate", GST_TYPE_FRACTION, 0, 1,
                     NULL), NULL);
  gst_bin_add_many (GST_BIN (pipeline), appsrc, conv, videosink, NULL);
  gst_element_link_many (appsrc, conv, videosink, NULL);

  /* setup appsrc */
  g_object_set (G_OBJECT (appsrc),
        "stream-type", 0,
        "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (appsrc, "need-data", G_CALLBACK (cb_need_data), NULL);

  /* play */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));
  g_main_loop_unref (loop);

  return 0;
}
```

### Grabbing data with appsink

Unlike `appsrc`, `appsink` is a little easier to use. It also supports
pull and push-based modes for getting data from the pipeline.

The normal way of retrieving samples from appsink is by using the
`gst_app_sink_pull_sample()` and `gst_app_sink_pull_preroll()` methods
or by using the `pull-sample` and `pull-preroll` signals. These methods
block until a sample becomes available in the sink or when the sink is
shut down or reaches `EOS`.

`appsink` will internally use a queue to collect buffers from the
streaming thread. If the application is not pulling samples fast enough,
this queue will consume a lot of memory over time. The `max-buffers`
property can be used to limit the queue size. The `drop` property
controls whether the streaming thread blocks or if older buffers are
dropped when the maximum queue size is reached. Note that blocking the
streaming thread can negatively affect real-time performance and should
be avoided.

If a blocking behaviour is not desirable, setting the `emit-signals`
property to `TRUE` will make appsink emit the `new-sample` and
`new-preroll` signals when a sample can be pulled without blocking.

The `caps` property on `appsink` can be used to control the formats that
the latter can receive. This property can contain non-fixed caps, the
format of the pulled samples can be obtained by getting the sample caps.

If one of the pull-preroll or pull-sample methods return `NULL`, the
`appsink` is stopped or in the `EOS` state. You can check for the `EOS` state
with the `eos` property or with the `gst_app_sink_is_eos()` method.

The `eos` signal can also be used to be informed when the `EOS` state is
reached to avoid polling.

Consider configuring the following properties in the `appsink`:

  - The `sync` property if you want to have the sink base class
    synchronize the buffer against the pipeline clock before handing you
    the sample.

  - Enable Quality-of-Service with the `qos` property. If you are
    dealing with raw video frames and let the base class synchronize on
    the clock. It might also be a good idea to let the base class send
    `QOS` events upstream.

  - The caps property that contains the accepted caps. Upstream elements
    will try to convert the format so that it matches the configured
    caps on `appsink`. You must still check the `GstSample` to get the
    actual caps of the buffer.

#### Appsink example

What follows is an example on how to capture a snapshot of a video
stream using `appsink`.

``` c
#include <gst/gst.h>
#ifdef HAVE_GTK
#include <gtk/gtk.h>
#endif

#include <stdlib.h>

#define CAPS "video/x-raw,format=RGB,width=160,pixel-aspect-ratio=1/1"

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *sink;
  gint width, height;
  GstSample *sample;
  gchar *descr;
  GError *error = NULL;
  gint64 duration, position;
  GstStateChangeReturn ret;
  gboolean res;
  GstMapInfo map;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <uri>\n Writes snapshot.png in the current directory\n",
        argv[0]);
    exit (-1);
  }

  /* create a new pipeline */
  descr =
      g_strdup_printf ("uridecodebin uri=%s ! videoconvert ! videoscale ! "
      " appsink name=sink caps=\"" CAPS "\"", argv[1]);
  pipeline = gst_parse_launch (descr, &error);

  if (error != NULL) {
    g_print ("could not construct pipeline: %s\n", error->message);
    g_clear_error (&error);
    exit (-1);
  }

  /* get sink */
  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");

  /* set to PAUSED to make the first frame arrive in the sink */
  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);
  switch (ret) {
    case GST_STATE_CHANGE_FAILURE:
      g_print ("failed to play the file\n");
      exit (-1);
    case GST_STATE_CHANGE_NO_PREROLL:
      /* for live sources, we need to set the pipeline to PLAYING before we can
       * receive a buffer. We don't do that yet */
      g_print ("live sources not supported yet\n");
      exit (-1);
    default:
      break;
  }
  /* This can block for up to 5 seconds. If your machine is really overloaded,
   * it might time out before the pipeline prerolled and we generate an error. A
   * better way is to run a mainloop and catch errors there. */
  ret = gst_element_get_state (pipeline, NULL, NULL, 5 * GST_SECOND);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_print ("failed to play the file\n");
    exit (-1);
  }

  /* get the duration */
  gst_element_query_duration (pipeline, GST_FORMAT_TIME, &duration);

  if (duration != -1)
    /* we have a duration, seek to 5% */
    position = duration * 5 / 100;
  else
    /* no duration, seek to 1 second, this could EOS */
    position = 1 * GST_SECOND;

  /* seek to the position in the file. Most files have a black first frame so
   * by seeking to somewhere else we have a bigger chance of getting something
   * more interesting. An optimisation would be to detect black images and then
   * seek a little more */
  gst_element_seek_simple (pipeline, GST_FORMAT_TIME,
      GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH, position);

  /* get the preroll buffer from appsink, this block untils appsink really
   * prerolls */
  g_signal_emit_by_name (sink, "pull-preroll", &sample, NULL);

  /* if we have a buffer now, convert it to a pixbuf. It's possible that we
   * don't have a buffer because we went EOS right away or had an error. */
  if (sample) {
    GstBuffer *buffer;
    GstCaps *caps;
    GstStructure *s;

    /* get the snapshot buffer format now. We set the caps on the appsink so
     * that it can only be an rgb buffer. The only thing we have not specified
     * on the caps is the height, which is dependant on the pixel-aspect-ratio
     * of the source material */
    caps = gst_sample_get_caps (sample);
    if (!caps) {
      g_print ("could not get snapshot format\n");
      exit (-1);
    }
    s = gst_caps_get_structure (caps, 0);

    /* we need to get the final caps on the buffer to get the size */
    res = gst_structure_get_int (s, "width", &width);
    res |= gst_structure_get_int (s, "height", &height);
    if (!res) {
      g_print ("could not get snapshot dimension\n");
      exit (-1);
    }

    /* create pixmap from buffer and save, gstreamer video buffers have a stride
     * that is rounded up to the nearest multiple of 4 */
    buffer = gst_sample_get_buffer (sample);
    /* Mapping a buffer can fail (non-readable) */
    if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
#ifdef HAVE_GTK
      pixbuf = gdk_pixbuf_new_from_data (map.data,
          GDK_COLORSPACE_RGB, FALSE, 8, width, height,
          GST_ROUND_UP_4 (width * 3), NULL, NULL);

      /* save the pixbuf */
      gdk_pixbuf_save (pixbuf, "snapshot.png", "png", &error, NULL);
#endif
      gst_buffer_unmap (buffer, &map);
    }
    gst_sample_unref (sample);
  } else {
    g_print ("could not make snapshot\n");
  }

  /* cleanup and exit */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (sink);
  gst_object_unref (pipeline);

  exit (0);
}
```

## Forcing a format

Sometimes you'll want to set a specific format. You can do this with a
`capsfilter` element.

If you want, for example, a specific video size and color format or an audio
bitsize and a number of channels; you can force a specific `GstCaps` on the
pipeline using *filtered caps*. You set *filtered caps* on a link by putting a
`capsfilter` between two elements and specifying your desired `GstCaps` in its
`caps` property. The `capsfilter` will only allow types compatible with these
capabilities to be negotiated.

See also [Creating capabilities for filtering][filter-caps].

[filter-caps]: application-development/basics/pads.md#creating-capabilities-for-filtering

### Changing format in a PLAYING pipeline

It is also possible to dynamically change the format in a pipeline while
`PLAYING`. This can simply be done by changing the `caps` property on a
`capsfilter`. The `capsfilter` will send a `RECONFIGURE` event upstream that
will make the upstream element attempt to renegotiate a new format and
allocator. This only works if the upstream element is not using fixed caps on
its source pad.

Below is an example of how you can change the caps of a pipeline while
in the `PLAYING` state:

``` c
#include <stdlib.h>

#include <gst/gst.h>

#define MAX_ROUND 100

int
main (int argc, char **argv)
{
  GstElement *pipe, *filter;
  GstCaps *caps;
  gint width, height;
  gint xdir, ydir;
  gint round;
  GstMessage *message;

  gst_init (&argc, &argv);

  pipe = gst_parse_launch_full ("videotestsrc ! capsfilter name=filter ! "
             "ximagesink", NULL, GST_PARSE_FLAG_NONE, NULL);
  g_assert (pipe != NULL);

  filter = gst_bin_get_by_name (GST_BIN (pipe), "filter");
  g_assert (filter);

  width = 320;
  height = 240;
  xdir = ydir = -10;

  for (round = 0; round < MAX_ROUND; round++) {
    gchar *capsstr;
    g_print ("resize to %dx%d (%d/%d)   \r", width, height, round, MAX_ROUND);

    /* we prefer our fixed width and height but allow other dimensions to pass
     * as well */
    capsstr = g_strdup_printf ("video/x-raw, width=(int)%d, height=(int)%d",
        width, height);

    caps = gst_caps_from_string (capsstr);
    g_free (capsstr);
    g_object_set (filter, "caps", caps, NULL);
    gst_caps_unref (caps);

    if (round == 0)
      gst_element_set_state (pipe, GST_STATE_PLAYING);

    width += xdir;
    if (width >= 320)
      xdir = -10;
    else if (width < 200)
      xdir = 10;

    height += ydir;
    if (height >= 240)
      ydir = -10;
    else if (height < 150)
      ydir = 10;

    message =
        gst_bus_poll (GST_ELEMENT_BUS (pipe), GST_MESSAGE_ERROR,
        50 * GST_MSECOND);
    if (message) {
      g_print ("got error           \n");

      gst_message_unref (message);
    }
  }
  g_print ("done                    \n");

  gst_object_unref (filter);
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);

  return 0;
}
```

Note how we use `gst_bus_poll()` with a small timeout to get messages
and also introduce a short sleep.

It is possible to set multiple caps for the capsfilter separated with a
`;`. The capsfilter will try to renegotiate to the first possible format
from the list.

## Dynamically changing the pipeline

In this section we talk about some techniques for dynamically modifying
the pipeline. We are talking specifically about changing the pipeline
while in `PLAYING` state and without interrupting the data flow.

There are some important things to consider when building dynamic
pipelines:

  - There are `insertbin` and `switchbin` elements, that target some
    cases of dynamical pipeline changes, and might fulfill your needs.

  - When removing elements from the pipeline, make sure that there is no
    dataflow on unlinked pads because that will cause a fatal pipeline
    error. Always block source pads (in push mode) or sink pads (in pull
    mode) before unlinking pads. See also [Changing elements in a
    pipeline](#changing-elements-in-a-pipeline).

  - When adding elements to a pipeline, make sure to put the element
    into the right state, usually the same state as the parent, before
    allowing dataflow. When an element is newly created, it is in the
    `NULL` state and will return an error when it receives data.
    See also [Changing elements in a pipeline](#changing-elements-in-a-pipeline).

  - When adding elements to a pipeline, GStreamer will by default set
    the clock and base-time on the element to the current values of the
    pipeline. This means that the element will be able to construct the
    same pipeline running-time as the other elements in the pipeline.
    This means that sinks will synchronize buffers like the other sinks
    in the pipeline and that sources produce buffers with a running-time
    that matches the other sources.

  - When unlinking elements from an upstream chain, always make sure to
    flush any queued data in the element by sending an `EOS` event down
    the element sink pad(s) and by waiting that the `EOS` leaves the
    elements (with an event probe).

    If you don't perform a flush, you will lose the data buffered by the
    unlinked element. This can result in a simple frame loss (a few video frames,
    several milliseconds of audio, etc) but If you remove a muxer -- and in
    some cases an encoder or similar elements --, you risk getting a corrupted
    file which can't be played properly because some relevant metadata (header,
    seek/index tables, internal sync tags) might not be properly stored or updated.

    See also [Changing elements in a pipeline](#changing-elements-in-a-pipeline).

  - A live source will produce buffers with a `running-time` equal to the
    pipeline's current `running-time`.

    A pipeline without a live source produces buffers with a
    `running-time` starting from 0. Likewise, after a flushing seek, these
    pipelines reset the `running-time` back to 0.

    The `running-time` can be changed with `gst_pad_set_offset ()`. It is
    important to know the `running-time` of the elements in the pipeline
    in order to maintain synchronization.

  - Adding elements might change the state of the pipeline. Adding a
    non-prerolled sink, for example, brings the pipeline back to the
    prerolling state. Removing a non-prerolled sink, for example, might
    change the pipeline to PAUSED and PLAYING state.

    Adding a live source cancels the preroll stage and puts the pipeline
    in the playing state. Adding any live element might also change the
    pipeline's latency.

    Adding or removing pipeline's elements might change the clock
    selection of the pipeline. If the newly added element provides a
    clock, it might be good for the pipeline to use the new clock. If, on
    the other hand, the element that is providing the clock for the
    pipeline is removed, a new clock has to be selected.

  - Adding and removing elements might cause upstream or downstream
    elements to renegotiate caps and/or allocators. You don't really
    need to do anything from the application, plugins largely adapt
    themselves to the new pipeline topology in order to optimize their
    formats and allocation strategy.

    What is important is that when you add, remove or change elements in
    a pipeline, it is possible that the pipeline needs to negotiate a
    new format and this can fail. Usually you can fix this by inserting
    the right converter elements where needed. See also [Changing
    elements in a pipeline](#changing-elements-in-a-pipeline).

GStreamer offers support for doing almost any dynamic pipeline modification but
you need to know a few details before you can do this without causing pipeline
errors. In the following sections we will demonstrate a few typical modification
use-cases.

### Changing elements in a pipeline

In this example we have the following element chain:

```
   - ----.      .----------.      .---- -
element1 |      | element2 |      | element3
       src -> sink       src -> sink
   - ----'      '----------'      '---- -

```

We want to replace element2 by element4 while the pipeline is in the
PLAYING state. Let's say that element2 is a visualization and that you
want to switch the visualization in the pipeline.

We can't just unlink element2's sinkpad from element1's source pad
because that would leave element1's source pad unlinked and would cause
a streaming error in the pipeline when data is pushed on the source pad.
The technique is to block the dataflow from element1's source pad before
we replace element2 by element4 and then resume dataflow as shown in the
following steps:

  - Block element1's source pad with a blocking pad probe. When the pad
    is blocked, the probe callback will be called.

  - Inside the block callback nothing is flowing between element1 and
    element2 and nothing will flow until unblocked.

  - Unlink element1 and element2.

  - Make sure data is flushed out of element2. Some elements might
    internally keep some data, you need to make sure not to lose any by
    forcing it out of element2. You can do this by pushing `EOS` into
    element2, like this:

      - Put an event probe on element2's source pad.

      - Send `EOS` to element2's sink pad. This makes sure that all the data
        inside element2 is forced out.

      - Wait for the `EOS` event to appear on element2's source pad. When
        the `EOS` is received, drop it and remove the event probe.

  - Unlink element2 and element3. You can now also remove element2 from
    the pipeline and set the state to `NULL`.

  - Add element4 to the pipeline, if not already added. Link element4
    and element3. Link element1 and element4.

  - Make sure element4 is in the same state as the rest of the elements
    in the pipeline. It should be at least in the `PAUSED` state before it
    can receive buffers and events.

  - Unblock element1's source pad probe. This will let new data into
    element4 and continue streaming.

The above algorithm works when the source pad is blocked, i.e. when
there is dataflow in the pipeline. If there is no dataflow, there is
also no point in changing the element (just yet) so this algorithm can
be used in the `PAUSED` state as well.

This example changes the video effect on a simple pipeline once per
second:

``` c
#include <gst/gst.h>

static gchar *opt_effects = NULL;

#define DEFAULT_EFFECTS "identity,exclusion,navigationtest," \
    "agingtv,videoflip,vertigotv,gaussianblur,shagadelictv,edgetv"

static GstPad *blockpad;
static GstElement *conv_before;
static GstElement *conv_after;
static GstElement *cur_effect;
static GstElement *pipeline;

static GQueue effects = G_QUEUE_INIT;

static GstPadProbeReturn
event_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GMainLoop *loop = user_data;
  GstElement *next;

  if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_DATA (info)) != GST_EVENT_EOS)
    return GST_PAD_PROBE_OK;

  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

  /* take next effect from the queue */
  next = g_queue_pop_head (&effects);
  if (next == NULL) {
    GST_DEBUG_OBJECT (pad, "no more effects");
    g_main_loop_quit (loop);
    return GST_PAD_PROBE_DROP;
  }

  g_print ("Switching from '%s' to '%s'..\n", GST_OBJECT_NAME (cur_effect),
      GST_OBJECT_NAME (next));

  gst_element_set_state (cur_effect, GST_STATE_NULL);

  /* remove unlinks automatically */
  GST_DEBUG_OBJECT (pipeline, "removing %" GST_PTR_FORMAT, cur_effect);
  gst_bin_remove (GST_BIN (pipeline), cur_effect);

  /* push current effect back into the queue */
  g_queue_push_tail (&effects, g_steal_pointer (&cur_effect));

  /* add, link and start the new effect */
  GST_DEBUG_OBJECT (pipeline, "adding   %" GST_PTR_FORMAT, next);
  gst_bin_add (GST_BIN (pipeline), next);

  GST_DEBUG_OBJECT (pipeline, "linking..");
  gst_element_link_many (conv_before, next, conv_after, NULL);

  gst_element_set_state (next, GST_STATE_PLAYING);

  cur_effect = next;
  GST_DEBUG_OBJECT (pipeline, "done");

  return GST_PAD_PROBE_DROP;
}

static GstPadProbeReturn
pad_probe_cb (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (pad, "pad is blocked now");

  /* remove the probe first */
  gst_pad_remove_probe (pad, GST_PAD_PROBE_INFO_ID (info));

  /* install new probe for EOS */
  srcpad = gst_element_get_static_pad (cur_effect, "src");
  gst_pad_add_probe (srcpad, GST_PAD_PROBE_TYPE_BLOCK |
      GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, event_probe_cb, user_data, NULL);
  gst_object_unref (srcpad);

  /* push EOS into the element, the probe will be fired when the
   * EOS leaves the effect and it has thus drained all of its data */
  sinkpad = gst_element_get_static_pad (cur_effect, "sink");
  gst_pad_send_event (sinkpad, gst_event_new_eos ());
  gst_object_unref (sinkpad);

  return GST_PAD_PROBE_OK;
}

static gboolean
timeout_cb (gpointer user_data)
{
  gst_pad_add_probe (blockpad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
      pad_probe_cb, user_data, NULL);

  return TRUE;
}

static gboolean
bus_cb (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GMainLoop *loop = user_data;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:{
      GError *err = NULL;
      gchar *dbg;

      gst_message_parse_error (msg, &err, &dbg);
      gst_object_default_error (msg->src, err, dbg);
      g_clear_error (&err);
      g_free (dbg);
      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

int
main (int argc, char **argv)
{
  GOptionEntry options[] = {
    {"effects", 'e', 0, G_OPTION_ARG_STRING, &opt_effects,
        "Effects to use (comma-separated list of element names)", NULL},
    {NULL}
  };
  GOptionContext *ctx;
  GError *err = NULL;
  GMainLoop *loop;
  GstElement *src, *q1, *q2, *effect, *filter, *sink;
  gchar **effect_names, **e;

  ctx = g_option_context_new ("");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_error ("Error initializing: %s\n", err->message);
    return 1;
  }
  g_option_context_free (ctx);

  if (opt_effects != NULL)
    effect_names = g_strsplit (opt_effects, ",", -1);
  else
    effect_names = g_strsplit (DEFAULT_EFFECTS, ",", -1);

  for (e = effect_names; e != NULL && *e != NULL; ++e) {
    GstElement *el;

    el = gst_element_factory_make (*e, NULL);
    if (el) {
      g_print ("Adding effect '%s'\n", *e);
      g_queue_push_tail (&effects, gst_object_ref_sink (el));
    }
  }

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (src, "is-live", TRUE, NULL);

  filter = gst_element_factory_make ("capsfilter", NULL);
  gst_util_set_object_arg (G_OBJECT (filter), "caps",
      "video/x-raw, width=320, height=240, "
      "format={ I420, YV12, YUY2, UYVY, AYUV, Y41B, Y42B, "
      "YVYU, Y444, v210, v216, NV12, NV21, UYVP, A420, YUV9, YVU9, IYU1 }");

  q1 = gst_element_factory_make ("queue", NULL);

  blockpad = gst_element_get_static_pad (q1, "src");

  conv_before = gst_element_factory_make ("videoconvert", NULL);

  effect = g_queue_pop_head (&effects);
  cur_effect = effect;

  conv_after = gst_element_factory_make ("videoconvert", NULL);

  q2 = gst_element_factory_make ("queue", NULL);

  sink = gst_element_factory_make ("ximagesink", NULL);

  gst_bin_add_many (GST_BIN (pipeline), src, filter, q1, conv_before, effect,
      conv_after, q2, sink, NULL);

  gst_element_link_many (src, filter, q1, conv_before, effect, conv_after,
      q2, sink, NULL);

  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_error ("Error starting pipeline");
    return 1;
  }

  loop = g_main_loop_new (NULL, FALSE);

  gst_bus_add_watch (GST_ELEMENT_BUS (pipeline), bus_cb, loop);

  g_timeout_add_seconds (1, timeout_cb, loop);

  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (blockpad);
  gst_bus_remove_watch (GST_ELEMENT_BUS (pipeline));
  gst_object_unref (pipeline);
  g_main_loop_unref (loop);

  g_queue_clear_full (&effects, (GDestroyNotify) gst_object_unref);
  gst_object_unref (cur_effect);
  g_strfreev (effect_names);

  return 0;
}
```

Note how we added `videoconvert` elements before and after the effect.
This is needed because some elements might operate in different
colorspaces; by inserting the conversion elements, we can help ensure
a proper format can be negotiated.
