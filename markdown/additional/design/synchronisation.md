# Synchronisation

This document outlines the techniques used for doing synchronised
playback of multiple streams.

Synchronisation in a `GstPipeline` is achieved using the following 3
components:

  - a `GstClock`, which is global for all elements in a `GstPipeline`.

  - Timestamps on a `GstBuffer`.

  - the SEGMENT event preceding the buffers.

## A GstClock

This object provides a counter that represents the current time in
nanoseconds. This value is called the `absolute_time`. A `GstClock`
always counts time upwards and does not necessarily start at 0.

Different sources exist for this counter:

  - the system time (with `g_get_current_time()` and with microsecond
    accuracy)

  - monotonic time (with `g_get_monotonic_time()` with microsecond
    accuracy)

  - an audio device (based on number of samples played)

  - a network source based on packets received + timestamps in those
    packets (a typical example is an RTP source)

  - …

In GStreamer any element can provide a `GstClock` object that can be used
in the pipeline. The `GstPipeline` object will select a clock from all the
providers and will distribute it to all other elements (see
[gstpipeline](additional/design/gstpipeline.md)).

While it is possible, it is not recommended to create a clock derived
from the contents of a stream (for example, create a clock from the PCR
in an mpeg-ts stream).

## Running time

After a pipeline selected a clock it will maintain the `running_time`
based on the selected clock. This `running_time` represents the total
time spent in the PLAYING state and is calculated as follows:

  - If the pipeline is NULL/READY, the `running_time` is undefined.

  - In PAUSED, the `running_time` remains at the time when it was last
    PAUSED. When the stream is `PAUSED` for the first time, the
    `running_time` is 0.

  - In PLAYING, the `running_time` is the delta between the
    `absolute_time` and the base time. The base time is defined as the
    `absolute_time` minus the `running_time` at the time when the pipeline
    is set to `PLAYING`.

  - after a flushing seek, the `running_time` is set to 0 (see
    [seeking](additional/design/seeking.md)). This is accomplished by redistributing a new
    base\_time to the elements that got flushed.

This algorithm captures the `running_time` when the pipeline is set from
`PLAYING` to `PAUSED` and restores this time based on the current
`absolute_time` when going back to `PLAYING`. This allows for both clocks
that progress when in the `PAUSED` state (systemclock) and clocks that
don’t (audioclock).

The clock and pipeline now provide a `running_time` to all elements that
want to perform synchronisation. Indeed, the running time can be
observed in each element (during the PLAYING state) as:

```
    C.running_time = absolute_time - base_time
```

We note `C.running_time` as the `running_time` obtained by looking at the
clock. This value is monotonically increasing at the rate of the clock.

## Timestamps

The `GstBuffer` timestamps and the preceding SEGMENT event (See
[streams](additional/design/streams.md)) define a transformation of the buffer timestamps
to `running_time` as follows:

The following notation is used:

**B**: `GstBuffer`
    - B.timestamp = buffer timestamp (`GST_BUFFER_PTS` or `GST_BUFFER_DTS`)

**S**:  SEGMENT event preceding the buffers.
    - S.start: start field in the SEGMENT event. This is the lowest allowed
            timestamp.
    - S.stop: stop field in the SEGMENT event. This is the highers allowed
           timestamp.
    - S.rate: rate field of SEGMENT event. This is the playback rate.
    - S.base: a base time for the time. This is the total elapsed `running_time`
 of any previous segments.
    - S.offset: an offset to apply to S.start or S.stop. This is the amount that
             has already been elapsed in the segment.

Valid buffers for synchronisation are those with B.timestamp between
`S.start` and `S.stop` (after applying the `S.offset`). All other buffers
outside this range should be dropped or clipped to these boundaries (see
also [segments](additional/design/segments.md)).

The following transformation to `running_time` exist:

```
    if (S.rate > 0.0)
      B.running_time = (B.timestamp - (S.start + S.offset)) / ABS (S.rate) + S.base
      =>
      B.timestamp = (B.running_time - S.base) * ABS (S.rate) + S.start + S.offset
    else
      B.running_time = ((S.stop - S.offset) - B.timestamp) / ABS (S.rate) + S.base
      =>
      B.timestamp = S.stop - S.offset - ((B.running_time - S.base) * ABS (S.rate))
```

We write `B.running_time` as the `running_time` obtained from the `SEGMENT`
event and the buffers of that segment.

The first displayable buffer will yield a value of 0 (since `B.timestamp
== S.start and S.offset and S.base == 0`).

For `S.rate > 1.0`, the timestamps will be scaled down to increase the
playback rate. Likewise, a rate between 0.0 and 1.0 will slow down
playback.

For negative rates, timestamps are received stop S.stop to `S.start` so
that the first buffer received will be transformed into `B.running_time`
of 0 (`B.timestamp == S.stop and S.base == 0`).

This makes it so that `B.running_time` is always monotonically increasing
starting from 0 with both positive and negative rates.

## Synchronisation

As we have seen, we can get a `running_time`:

  - using the clock and the element’s `base_time` with:

```
        C.running_time = absolute_time - base_time
```

- using the buffer timestamp and the preceding `SEGMENT` event as (assuming
positive playback rate):

```
        B.running_time = (B.timestamp - (S.start + S.offset)) / ABS (S.rate) + S.base
```

We prefix C. and B. before the two running times to note how they were
calculated.

The task of synchronized playback is to make sure that we play a buffer
with `B.running_time` at the moment when the clock reaches the same
`C.running_time`.

Thus the following must hold:

```
    B.running_time = C.running_time
```

expaning:

```
    B.running_time = absolute_time - base_time
```

or:

```
    absolute_time = B.running_time + base_time
```

The `absolute_time` when a buffer with `B.running_time` should be played
is noted with `B.sync_time`. Thus:

```
    B.sync_time = B.running_time + base_time
```

One then waits for the clock to reach `B.sync_time` before rendering the
buffer in the sink (See also [clocks](additional/design/clocks.md)).

For multiple streams this means that buffers with the same `running_time`
are to be displayed at the same time.

A demuxer must make sure that the `SEGMENT` it emits on its output pads
yield the same `running_time` for buffers that should be played
synchronized. This usually means sending the same `SEGMENT` on all pads
and making sure that the synchronized buffers have the same timestamps.

## Stream time

The stream time is also known as the position in the stream and is a
value between 0 and the total duration of the media file.

It is the stream time that is used for:

  - report the `POSITION` query in the pipeline

  - the position used in seek events/queries

  - the position used to synchronize controller values

Additional fields in the `SEGMENT` are used:

  - `S.time`: time field in the `SEGMENT` event. This the stream-time of
    `S.start`

  - `S.applied_rate`: The rate already applied to the segment.

Stream time is calculated using the buffer times and the preceding
`SEGMENT` event as follows:

```
    stream_time = (B.timestamp - S.start) * ABS (S.applied_rate) + S.time
    => B.timestamp = (stream_time - S.time) / ABS(S.applied_rate) + S.start
```

For negative rates, `B.timestamp` will go backwards from `S.stop` to
`S.start`, making the stream time go backwards:

```
    stream_time = (S.stop - B.timestamp) * ABS(S.applied_rate) + S.time
    => B.timestamp = S.stop - (stream_time - S.time) / ABS(S.applied_rate)
```

In the `PLAYING` state, it is also possible to use the pipeline clock to
derive the current `stream_time`.

Give the two formulas above to match the clock times with buffer
timestamps allows us to rewrite the above formula for `stream_time` (and
for positive rates).

```
    C.running_time = absolute_time - base_time
    B.running_time = (B.timestamp - (S.start + S.offset)) / ABS (S.rate) + S.base

    =>
      (B.timestamp - (S.start + S.offset)) / ABS (S.rate) + S.base = absolute_time - base_time;

    =>
      (B.timestamp - (S.start + S.offset)) / ABS (S.rate) = absolute_time - base_time - S.base;

    =>
      (B.timestamp - (S.start + S.offset)) = (absolute_time - base_time - S.base) * ABS (S.rate)

    =>
      (B.timestamp - S.start) = S.offset + (absolute_time - base_time - S.base) * ABS (S.rate)

    filling (B.timestamp - S.start) in the above formule for stream time

    =>
      stream_time = (S.offset + (absolute_time - base_time - S.base) * ABS (S.rate)) * ABS (S.applied_rate) + S.time
```

This last formula is typically used in sinks to report the current
position in an accurate and efficient way.

Note that the stream time is never used for synchronisation against the
clock.
