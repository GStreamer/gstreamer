---
title: Clocking
...

# Clocking

When playing complex media, each sound and video sample must be played
in a specific order at a specific time. For this purpose, GStreamer
provides a synchronization mechanism.

## Clocks

Time in GStreamer is defined as the value returned from a particular
`GstClock` object from the method `gst_clock_get_time ()`.

In a typical computer, there are many sources that can be used as a time
source, e.g., the system time, soundcards, CPU performance counters, ...
For this reason, there are many `GstClock` implementations available in
GStreamer. The clock time doesn't always start from 0 or from some known
value. Some clocks start counting from some known start date, other
clocks start counting since last reboot, etc...

As clocks return an absolute measure of time, they are not usually used
directly. Instead, differences between two clock times are used to
measure elapsed time according to a clock.

## Clock running-time

A clock returns the **absolute-time** according to that clock with
`gst_clock_get_time ()`. From the absolute-time is a **running-time**
calculated, which is simply the difference between a previous snapshot
of the absolute-time called the **base-time**. So:

running-time = absolute-time - base-time

A GStreamer `GstPipeline` object maintains a `GstClock` object and a
base-time when it goes to the PLAYING state. The pipeline gives a handle
to the selected `GstClock` to each element in the pipeline along with
selected base-time. The pipeline will select a base-time in such a way
that the running-time reflects the total time spent in the PLAYING
state. As a result, when the pipeline is PAUSED, the running-time stands
still.

Because all objects in the pipeline have the same clock and base-time,
they can thus all calculate the running-time according to the pipeline
clock.

## Buffer running-time

To calculate a buffer running-time, we need a buffer timestamp and the
SEGMENT event that preceded the buffer. First we can convert the SEGMENT
event into a `GstSegment` object and then we can use the
`gst_segment_to_running_time ()` function to perform the calculation of
the buffer running-time.

Synchronization is now a matter of making sure that a buffer with a
certain running-time is played when the clock reaches the same
running-time. Usually this task is done by sink elements. Sink also have
to take into account the latency configured in the pipeline and add this
to the buffer running-time before synchronizing to the pipeline clock.

## Obligations of each element.

Let us clarify the contract between GStreamer and each element in the
pipeline.

### Non-live source elements

Non-live source elements must place a timestamp in each buffer that they
deliver when this is possible. They must choose the timestamps and the
values of the SEGMENT event in such a way that the running-time of the
buffer starts from 0.

Some sources, such as filesrc, is not able to generate timestamps on all
buffers. It can and must however create a timestamp on the first buffer
(with a running-time of 0).

The source then pushes out the SEGMENT event followed by the timestamped
buffers.

### Live source elements

Live source elements must place a timestamp in each buffer that they
deliver. They must choose the timestamps and the values of the SEGMENT
event in such a way that the running-time of the buffer matches exactly
the running-time of the pipeline clock when the first byte in the buffer
was captured.

### Parser/Decoder/Encoder elements

Parser/Decoder elements must use the incoming timestamps and transfer
those to the resulting output buffers. They are allowed to interpolate
or reconstruct timestamps on missing input buffers when they can.

### Demuxer elements

Demuxer elements can usually set the timestamps stored inside the media
file onto the outgoing buffers. They need to make sure that outgoing
buffers that are to be played at the same time have the same
running-time. Demuxers also need to take into account the incoming
timestamps on buffers and use that to calculate an offset on the
outgoing buffer timestamps.

### Muxer elements

Muxer elements should use the incoming buffer running-time to mux the
different streams together. They should copy the incoming running-time
to the outgoing buffers.

### Sink elements

If the element is intended to emit samples at a specific time (real time
playing), the element should require a clock, and thus implement the
method `set_clock`.

The sink should then make sure that the sample with running-time is
played exactly when the pipeline clock reaches that running-time +
latency. Some elements might use the clock API such as
`gst_clock_id_wait()` to perform this action. Other sinks might need to
use other means of scheduling timely playback of the data.
