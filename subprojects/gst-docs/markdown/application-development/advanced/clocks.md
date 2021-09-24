---
title: Clocks and synchronization in GStreamer
...

# Clocks and synchronization in GStreamer

When playing complex media, each sound and video sample must be played
in a specific order at a specific time. For this purpose, GStreamer
provides a synchronization mechanism.

GStreamer provides support for the following use cases:

  - Non-live sources with access faster than playback rate. This is the
    case where one is reading media from a file and playing it back in a
    synchronized fashion. In this case, multiple streams need to be
    synchronized, like audio, video and subtitles.

  - Capture and synchronized muxing/mixing of media from multiple live
    sources. This is a typical use case where you record audio and video
    from a microphone/camera and mux it into a file for storage.

  - Streaming from (slow) network streams with buffering. This is the
    typical web streaming case where you access content from a streaming
    server using HTTP.

  - Capture from live source and playback with configurable latency. This
    is used, for example, when capturing from a camera, applying an
    effect and displaying the result. It is also used when streaming
    low latency content over a network with UDP.

  - Simultaneous live capture and playback from prerecorded content.
    This is used in audio recording cases where you play a previously
    recorded audio and record new samples, the purpose is to have the
    new audio perfectly in sync with the previously recorded data.

GStreamer uses a `GstClock` object, buffer timestamps and a `SEGMENT`
event to synchronize streams in a pipeline as we will see in the next
sections.

## Clock running-time

In a typical computer, there are many sources that can be used as a time
source, e.g., the system time, soundcards, CPU performance counters, etc.
For this reason, GStreamer has many `GstClock` implementations available.
Note that clock time doesn't have to start from 0 or any other known
value. Some clocks start counting from a particular start date, others
from the last reboot, etc.

A `GstClock` returns the **absolute-time** according to that clock with
`gst_clock_get_time ()`. The **absolute-time** (or clock time) of a clock
is monotonically increasing.

A **running-time** is the difference between a previous snapshot of the
**absolute-time** called the **base-time**, and any other **absolute-time**.

```
running-time = absolute-time - base-time
```

A GStreamer `GstPipeline` object maintains a `GstClock` object and a
base-time when it goes to the `PLAYING` state. The pipeline gives a handle
to the selected `GstClock` to each element in the pipeline along with
selected base-time. The pipeline will select a base-time in such a way
that the running-time reflects the total time spent in the `PLAYING`
state. As a result, when the pipeline is `PAUSED`, the running-time stands
still.

Because all objects in the pipeline have the same clock and base-time,
they can thus all calculate the running-time according to the pipeline
clock.

## Buffer running-time

To calculate a buffer running-time, we need a buffer timestamp and the
`SEGMENT` event that preceded the buffer. First we can convert the
`SEGMENT` event into a `GstSegment` object and then we can use the
`gst_segment_to_running_time ()` function to perform the calculation of
the buffer running-time.

Synchronization is now a matter of making sure that a buffer with a
certain running-time is played when the clock reaches the same
running-time. Usually, this task is performed by sink elements. These
elements also have to take into account the configured pipeline's latency
and add it to the buffer running-time before synchronizing to the pipeline
clock.

Non-live sources timestamp buffers with a running-time starting from 0.
After a flushing seek, they will produce buffers again from a
running-time of 0.

Live sources need to timestamp buffers with a running-time matching the
pipeline running-time when the first byte of the buffer was captured.

## Buffer stream-time

The buffer stream-time, also known as the position in the stream, is
a value between 0 and the total duration of the media and it's calculated
from the buffer timestamps and the preceding `SEGMENT` event.

The stream-time is used in:

  - Report the current position in the stream with the `POSITION` query.

  - The position used in the seek events and queries.

  - The position used to synchronize controlled values.

The stream-time is never used to synchronize streams, this is only done
with the running-time.

## Time overview

Here is an overview of the various timelines used in GStreamer.

The image below represents the different times in the pipeline when
playing a 100ms sample and repeating the part between 50ms and 100ms.

![GStreamer clock and various times](images/clocks.png "fig:")

You can see how the running-time of a buffer always increments
monotonically along with the clock-time. Buffers are played when their
running-time is equal to the clock-time - base-time. The stream-time
represents the position in the stream and jumps backwards when
repeating.

## Clock providers

A clock provider is an element in the pipeline that can provide a
`GstClock` object. The clock object needs to report an absolute-time
that is monotonically increasing when the element is in the `PLAYING`
state. It is allowed to pause the clock while the element is `PAUSED`.

Clock providers exist because they play back media at some rate, and
this rate is not necessarily the same as the system clock rate. For
example, a soundcard may play back at 44.1 kHz, but that doesn't mean
that after *exactly* 1 second *according to the system clock*, the
soundcard has played back 44100 samples. This is only true by
approximation. In fact, the audio device has an internal clock based on
the number of samples played that we can expose.

If an element with an internal clock needs to synchronize, it needs to
estimate when a time according to the pipeline clock will take place
according to the internal clock. To estimate this, it needs to slave its
clock to the pipeline clock.

If the pipeline clock is exactly the internal clock of an element, the
element can skip the slaving step and directly use the pipeline clock to
schedule playback. This can be both faster and more accurate. Therefore,
generally, elements with an internal clock like audio input or output
devices will be a clock provider for the pipeline.

When the pipeline goes to the `PLAYING` state, it will go over all
elements in the pipeline from sink to source and ask each element if
they can provide a clock. The last element that can provide a clock will
be used as the clock provider in the pipeline. This algorithm prefers a
clock from an audio sink in a typical playback pipeline and a clock from
source elements in a typical capture pipeline.

There exist some bus messages to let you know about the clock and clock
providers in the pipeline. You can see what clock is selected in the
pipeline by looking at the `NEW_CLOCK` message on the bus. When a clock
provider is removed from the pipeline, a `CLOCK_LOST` message is posted
and the application should go to `PAUSED` and back to `PLAYING` to select a
new clock.

## Latency

The latency is the time it takes for a sample captured at timestamp X to
reach the sink. This time is measured against the clock in the pipeline.
For pipelines where the only elements that synchronize against the clock
are the sinks, the latency is always 0 since no other element is
delaying the buffer.

For pipelines with live sources, a latency is introduced, mostly because
of the way a live source works. Consider an audio source, it will start
capturing the first sample at time 0. If the source pushes buffers with
44100 samples at a time at 44100Hz it will have collected the buffer at
second 1. Since the timestamp of the buffer is 0 and the time of the
clock is now `>= 1` second, the sink will drop this buffer because it is
too late. Without any latency compensation in the sink, all buffers will
be dropped.

### Latency compensation

Before the pipeline goes to the `PLAYING` state, it will, in addition to
selecting a clock and calculating a base-time, calculate the latency in
the pipeline. It does this by doing a `LATENCY` query on all the sinks in
the pipeline. The pipeline then selects the maximum latency in the
pipeline and configures this with a `LATENCY` event.

All sink elements will delay playback by the value in the `LATENCY` event.
Since all sinks delay with the same amount of time, they will be
relatively in sync.

### Dynamic Latency

Adding/removing elements to/from a pipeline or changing element
properties can change the latency in a pipeline. An element can request
a latency change in the pipeline by posting a `LATENCY` message on the
bus. The application can then decide to query and redistribute a new
latency or not. Changing the latency in a pipeline might cause visual or
audible glitches and should therefore only be done by the application
when it is allowed.
