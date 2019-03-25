---
title: Position tracking and seeking
...

# Position tracking and seeking

So far, we've looked at how to create a pipeline to do media processing
and how to make it run. Most application developers will be interested
in providing feedback to the user on media progress. Media players, for
example, will want to show a slider showing the progress in the song,
and usually also a label indicating stream length. Transcoding
applications will want to show a progress bar on how much percent of the
task is done. GStreamer has built-in support for doing all this using a
concept known as *querying*. Since seeking is very similar, it will be
discussed here as well. Seeking is done using the concept of *events*.

## Querying: getting the position or length of a stream

Querying is defined as requesting a specific stream property related to
progress tracking. This includes getting the length of a stream (if
available) or getting the current position. Those stream properties can
be retrieved in various formats such as time, audio samples, video
frames or bytes. The function most commonly used for this is
`gst_element_query ()`, although some convenience wrappers are provided
as well (such as `gst_element_query_position ()` and
`gst_element_query_duration ()`). You can generally query the pipeline
directly, and it'll figure out the internal details for you, like which
element to query.

Internally, queries will be sent to the sinks, and “dispatched”
backwards until one element can handle it; that result will be sent back
to the function caller. Usually, that is the demuxer, although with live
sources (from a webcam), it is the source itself.

``` c

#include <gst/gst.h>




static gboolean
cb_print_position (GstElement *pipeline)
{
  gint64 pos, len;

  if (gst_element_query_position (pipeline, GST_FORMAT_TIME, &pos)
    && gst_element_query_duration (pipeline, GST_FORMAT_TIME, &len)) {
    g_print ("Time: %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT "\r",
         GST_TIME_ARGS (pos), GST_TIME_ARGS (len));
  }

  /* call me again */
  return TRUE;
}

gint
main (gint   argc,
      gchar *argv[])
{
  GstElement *pipeline;

[..]

  /* run pipeline */
  g_timeout_add (200, (GSourceFunc) cb_print_position, pipeline);
  g_main_loop_run (loop);

[..]

}

```

## Events: seeking (and more)

Events work in a very similar way as queries. Dispatching, for example,
works exactly the same for events (and also has the same limitations),
and they can similarly be sent to the toplevel pipeline and it will
figure out everything for you. Although there are more ways in which
applications and elements can interact using events, we will only focus
on seeking here. This is done using the seek-event. A seek-event
contains a playback rate, a seek offset format (which is the unit of the
offsets to follow, e.g. time, audio samples, video frames or bytes),
optionally a set of seeking-related flags (e.g. whether internal buffers
should be flushed), a seek method (which indicates relative to what the
offset was given), and seek offsets.

The first offset (`start`) is the new position to seek to, while the second
offset (`stop`) is optional and specifies a position where streaming is
supposed to stop. Usually it is fine to just specify `GST_SEEK_TYPE_NONE`
as `stop_type` and `GST_CLOCK_TIME_NONE` as `stop` offset.

In case of reverse playback (`rate` < 0) the meaning of `start` and `stop` is
reversed and `stop` is the position to seek to.

The behaviour of a seek is also wrapped in the `gst_element_seek()` and
`gst_element_seek_simple()` and you would usually use those functions to
initiate a seek on a pipeline.

``` c
static void
seek_to_time (GstElement *pipeline,
          gint64      time_nanoseconds)
{
  if (!gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                         GST_SEEK_TYPE_SET, time_nanoseconds,
                         GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
    g_print ("Seek failed!\n");
  }
}

```

Seeks with the GST\_SEEK\_FLAG\_FLUSH should be done when the pipeline
is in PAUSED or PLAYING state. The pipeline will automatically go to
preroll state until the new data after the seek will cause the pipeline
to preroll again. After the pipeline is prerolled, it will go back to
the state (PAUSED or PLAYING) it was in when the seek was executed. You
can wait (blocking) for the seek to complete with
`gst_element_get_state()` or by waiting for the ASYNC\_DONE message to
appear on the bus.

Seeks without the GST\_SEEK\_FLAG\_FLUSH should only be done when the
pipeline is in the PLAYING state. Executing a non-flushing seek in the
PAUSED state might deadlock because the pipeline streaming threads might
be blocked in the sinks.

It is important to realise that seeks will not happen instantly in the
sense that they are finished when the function `gst_element_seek ()`
returns. Depending on the specific elements involved, the actual seeking
might be done later in another thread (the streaming thread), and it
might take a short time until buffers from the new seek position will
reach downstream elements such as sinks (if the seek was non-flushing
then it might take a bit longer).

It is possible to do multiple seeks in short time-intervals, such as a
direct response to slider movement. After a seek, internally, the
pipeline will be paused (if it was playing), the position will be re-set
internally, the demuxers and decoders will decode from the new position
onwards and this will continue until all sinks have data again. If it
was playing originally, it will be set to playing again, too. Since the
new position is immediately available in a video output, you will see
the new frame, even if your pipeline is not in the playing state.
