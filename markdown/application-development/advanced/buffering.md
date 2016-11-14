---
title: Buffering
...

# Buffering

The purpose of buffering is to accumulate enough data in a pipeline so
that playback can occur smoothly and without interruptions. It is
typically done when reading from a (slow) and non-live network source
but can also be used for live sources.

GStreamer provides support for the following use cases:

  - Buffering up to a specific amount of data, in memory, before
    starting playback so that network fluctuations are minimized. See
    [Stream buffering](#stream-buffering).

  - Download of the network file to a local disk with fast seeking in
    the downloaded data. This is similar to the quicktime/youtube
    players. See [Download buffering](#download-buffering).

  - Caching of (semi)-live streams to a local, on disk, ringbuffer with
    seeking in the cached area. This is similar to tivo-like
    timeshifting. See [Timeshift buffering](#timeshift-buffering).

GStreamer can provide the application with progress reports about the
current buffering state as well as let the application decide on how to
buffer and when the buffering stops.

In the most simple case, the application has to listen for BUFFERING
messages on the bus. If the percent indicator inside the BUFFERING
message is smaller than 100, the pipeline is buffering. When a message
is received with 100 percent, buffering is complete. In the buffering
state, the application should keep the pipeline in the PAUSED state.
When buffering completes, it can put the pipeline (back) in the PLAYING
state.

What follows is an example of how the message handler could deal with
the BUFFERING messages. We will see more advanced methods in [Buffering
strategies](#buffering-strategies).

``` c

  [...]

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      /* no state management needed for live pipelines */
      if (is_live)
        break;

      gst_message_parse_buffering (message, &percent);

      if (percent == 100) {
        /* a 100% message means buffering is done */
        buffering = FALSE;
        /* if the desired state is playing, go back */
        if (target_state == GST_STATE_PLAYING) {
          gst_element_set_state (pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering busy */
        if (!buffering && target_state == GST_STATE_PLAYING) {
          /* we were not buffering but PLAYING, PAUSE  the pipeline. */
          gst_element_set_state (pipeline, GST_STATE_PAUSED);
        }
        buffering = TRUE;
      }
      break;
    case ...

  [...]


```

## Stream buffering

```
      +---------+     +---------+     +-------+
      | httpsrc |     | buffer  |     | demux |
      |        src - sink      src - sink     ....
      +---------+     +---------+     +-------+

```

In this case we are reading from a slow network source into a buffer
element (such as queue2).

The buffer element has a low and high watermark expressed in bytes. The
buffer uses the watermarks as follows:

  - The buffer element will post BUFFERING messages until the high
    watermark is hit. This instructs the application to keep the
    pipeline PAUSED, which will eventually block the srcpad from pushing
    while data is prerolled in the sinks.

  - When the high watermark is hit, a BUFFERING message with 100% will
    be posted, which instructs the application to continue playback.

  - When during playback, the low watermark is hit, the queue will start
    posting BUFFERING messages again, making the application PAUSE the
    pipeline again until the high watermark is hit again. This is called
    the rebuffering stage.

  - During playback, the queue level will fluctuate between the high and
    the low watermark as a way to compensate for network irregularities.

This buffering method is usable when the demuxer operates in push mode.
Seeking in the stream requires the seek to happen in the network source.
It is mostly desirable when the total duration of the file is not known,
such as in live streaming or when efficient seeking is not
possible/required.

The problem is configuring a good low and high watermark. Here are some
ideas:

  - It is possible to measure the network bandwidth and configure the
    low/high watermarks in such a way that buffering takes a fixed
    amount of time.

    The queue2 element in GStreamer core has the max-size-time property
    that, together with the use-rate-estimate property, does exactly
    that. Also the playbin buffer-duration property uses the rate
    estimate to scale the amount of data that is buffered.

  - Based on the codec bitrate, it is also possible to set the
    watermarks in such a way that a fixed amount of data is buffered
    before playback starts. Normally, the buffering element doesn't know
    about the bitrate of the stream but it can get this with a query.

  - Start with a fixed amount of bytes, measure the time between
    rebuffering and increase the queue size until the time between
    rebuffering is within the application's chosen limits.

The buffering element can be inserted anywhere in the pipeline. You
could, for example, insert the buffering element before a decoder. This
would make it possible to set the low/high watermarks based on time.

The buffering flag on playbin, performs buffering on the parsed data.
Another advantage of doing the buffering at a later stage is that you
can let the demuxer operate in pull mode. When reading data from a slow
network drive (with filesrc) this can be an interesting way to buffer.

## Download buffering

```
      +---------+     +---------+     +-------+
      | httpsrc |     | buffer  |     | demux |
      |        src - sink      src - sink     ....
      +---------+     +----|----+     +-------+
                           V
                          file

```

If we know the server is streaming a fixed length file to the client,
the application can choose to download the entire file on disk. The
buffer element will provide a push or pull based srcpad to the demuxer
to navigate in the downloaded file.

This mode is only suitable when the client can determine the length of
the file on the server.

In this case, buffering messages will be emitted as usual when the
requested range is not within the downloaded area + buffersize. The
buffering message will also contain an indication that incremental
download is being performed. This flag can be used to let the
application control the buffering in a more intelligent way, using the
BUFFERING query, for example. See [Buffering
strategies](#buffering-strategies).

## Timeshift buffering

```
      +---------+     +---------+     +-------+
      | httpsrc |     | buffer  |     | demux |
      |        src - sink      src - sink     ....
      +---------+     +----|----+     +-------+
                           V
                       file-ringbuffer

```

In this mode, a fixed size ringbuffer is kept to download the server
content. This allows for seeking in the buffered data. Depending on the
size of the ringbuffer one can seek further back in time.

This mode is suitable for all live streams. As with the incremental
download mode, buffering messages are emitted along with an indication
that timeshifting download is in progress.

## Live buffering

In live pipelines we usually introduce some fixed latency between the
capture and the playback elements. This latency can be introduced by a
queue (such as a jitterbuffer) or by other means (in the audiosink).

Buffering messages can be emitted in those live pipelines as well and
serve as an indication to the user of the latency buffering. The
application usually does not react to these buffering messages with a
state change.

## Buffering strategies

What follows are some ideas for implementing different buffering
strategies based on the buffering messages and buffering query.

### No-rebuffer strategy

We would like to buffer enough data in the pipeline so that playback
continues without interruptions. What we need to know to implement this
is know the total remaining playback time in the file and the total
remaining download time. If the buffering time is less than the playback
time, we can start playback without interruptions.

We have all this information available with the DURATION, POSITION and
BUFFERING queries. We need to periodically execute the buffering query
to get the current buffering status. We also need to have a large enough
buffer to hold the complete file, worst case. It is best to use this
buffering strategy with download buffering (see [Download
buffering](#download-buffering)).

This is what the code would look like:

``` c


#include <gst/gst.h>

GstState target_state;
static gboolean is_live;
static gboolean is_buffering;

static gboolean
buffer_timeout (gpointer data)
{
  GstElement *pipeline = data;
  GstQuery *query;
  gboolean busy;
  gint percent;
  gint64 estimated_total;
  gint64 position, duration;
  guint64 play_left;

  query = gst_query_new_buffering (GST_FORMAT_TIME);

  if (!gst_element_query (pipeline, query))
    return TRUE;

  gst_query_parse_buffering_percent (query, &busy, &percent);
  gst_query_parse_buffering_range (query, NULL, NULL, NULL, &estimated_total);

  if (estimated_total == -1)
    estimated_total = 0;

  /* calculate the remaining playback time */
  if (!gst_element_query_position (pipeline, GST_FORMAT_TIME, &position))
    position = -1;
  if (!gst_element_query_duration (pipeline, GST_FORMAT_TIME, &duration))
    duration = -1;

  if (duration != -1 && position != -1)
    play_left = GST_TIME_AS_MSECONDS (duration - position);
  else
    play_left = 0;

  g_message ("play_left %" G_GUINT64_FORMAT", estimated_total %" G_GUINT64_FORMAT
      ", percent %d", play_left, estimated_total, percent);

  /* we are buffering or the estimated download time is bigger than the
   * remaining playback time. We keep buffering. */
  is_buffering = (busy || estimated_total * 1.1 > play_left);

  if (!is_buffering)
    gst_element_set_state (pipeline, target_state);

  return is_buffering;
}

static void
on_message_buffering (GstBus *bus, GstMessage *message, gpointer user_data)
{
  GstElement *pipeline = user_data;
  gint percent;

  /* no state management needed for live pipelines */
  if (is_live)
    return;

  gst_message_parse_buffering (message, &percent);

  if (percent < 100) {
    /* buffering busy */
    if (!is_buffering) {
      is_buffering = TRUE;
      if (target_state == GST_STATE_PLAYING) {
        /* we were not buffering but PLAYING, PAUSE  the pipeline. */
        gst_element_set_state (pipeline, GST_STATE_PAUSED);
      }
    }
  }
}

static void
on_message_async_done (GstBus *bus, GstMessage *message, gpointer user_data)
{
  GstElement *pipeline = user_data;

  if (!is_buffering)
    gst_element_set_state (pipeline, target_state);
  else
    g_timeout_add (500, buffer_timeout, pipeline);
}

gint
main (gint   argc,
      gchar *argv[])
{
  GstElement *pipeline;
  GMainLoop *loop;
  GstBus *bus;
  GstStateChangeReturn ret;

  /* init GStreamer */
  gst_init (&amp;argc, &amp;argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* make sure we have a URI */
  if (argc != 2) {
    g_print ("Usage: %s &lt;URI&gt;\n", argv[0]);
    return -1;
  }

  /* set up */
  pipeline = gst_element_factory_make ("playbin", "pipeline");
  g_object_set (G_OBJECT (pipeline), "uri", argv[1], NULL);
  g_object_set (G_OBJECT (pipeline), "flags", 0x697 , NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "message::buffering",
    (GCallback) on_message_buffering, pipeline);
  g_signal_connect (bus, "message::async-done",
    (GCallback) on_message_async_done, pipeline);
  gst_object_unref (bus);

  is_buffering = FALSE;
  target_state = GST_STATE_PLAYING;
  ret = gst_element_set_state (pipeline, GST_STATE_PAUSED);

  switch (ret) {
    case GST_STATE_CHANGE_SUCCESS:
      is_live = FALSE;
      break;

    case GST_STATE_CHANGE_FAILURE:
      g_warning ("failed to PAUSE");
      return -1;

    case GST_STATE_CHANGE_NO_PREROLL:
      is_live = TRUE;
      break;

    default:
      break;
  }

  /* now run */
  g_main_loop_run (loop);

  /* also clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));
  g_main_loop_unref (loop);

  return 0;
}



```

See how we set the pipeline to the PAUSED state first. We will receive
buffering messages during the preroll state when buffering is needed.
When we are prerolled (on\_message\_async\_done) we see if buffering is
going on, if not, we start playback. If buffering was going on, we start
a timeout to poll the buffering state. If the estimated time to download
is less than the remaining playback time, we start playback.
