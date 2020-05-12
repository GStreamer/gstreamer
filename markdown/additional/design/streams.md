# Streams

This document describes the objects that are passed from element to
element in the streaming thread.


## Stream objects

The following objects are to be expected in the streaming thread:

  - events, including:
    - `STREAM_START` (START)
    - `SEGMENT` (SEGMENT)
    - `EOS` * (EOS)
    - `TAG` (T)
  - buffers * (B)

Objects marked with * need to be synchronised to the clock in sinks and
live sources.


## Typical stream

A typical stream starts with a `STREAM_START` event that marks the start of the
stream, followed by a `SEGMENT` event that marks the buffer timestamp
range. After that buffers are sent one after the other. After the last buffer an
`EOS` event marks the end of the stream. No more buffers are to be processed
after the `EOS` event.

```
+-----+-------+ +-++-+     +-+ +---+
|START|SEGMENT| |B||B| ... |B| |EOS|
+-----+-------+ +-++-+     +-+ +---+
```

1) **`STREAM_START`**
   - marks the start of a stream; unlike the `SEGMENT` event, there
     will be no `STREAM_START` event after flushing seeks.

2) **`SEGMENT`**, rate, start/stop, time. (See also
   [Segments](additional/design/segments.md))
   - marks valid buffer timestamp range (start, stop)
   - marks `stream_time` of buffers (time). This is the stream time of buffers
     with a timestamp of `S.start`.
   - marks playback rate (rate). This is the required playback rate.
   - marks applied rate (`applied_rate`). This is the already applied playback
     rate. (See also [trickmodes](additional/design/trickmodes.md))
   - marks `running_time` of buffers. This is the time used to synchronize
     against the clock.

3) **N buffers**
   - displayable buffers are between start/stop of the `SEGMENT` (S). Buffers
     outside the segment range should be dropped or clipped.

   - `running_time`: For each buffer, a monotonically increasing value that can
     be used to synchronize against the clock (See also
     [synchronisation](additional/design/synchronisation.md)).

     ``` c
     if (S.rate > 0.0)
       running_time = (B.timestamp - S.start) / ABS (S.rate) + S.base;
     else
       running_time = (S.stop - B.timestamp) / ABS (S.rate) + S.base;
     ```

   - `stream_time`: The current position in the stream, between 0 and duration.

     ``` c
     stream_time = (B.timestamp - S.start) * ABS (S.applied_rate) + S.time;
     ```


4) **`EOS`**
   - marks the end of data, nothing is to be expected after `EOS`, elements
     should refuse more data and return `GST_FLOW_EOS`. A `FLUSH_STOP`
     event clears the `EOS` state of an element.


## Elements

These events are generated typically either by the `GstBaseSrc` class for
sources operating in push mode, or by a parser/demuxer operating in
pull-mode and pushing parsed/demuxed data downstream.
