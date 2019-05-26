# Latency

The latency is the time it takes for a sample captured at timestamp 0 to
reach the sink. This time is measured against the pipeline's clock.
For pipelines where the only elements that synchronize against the clock
are the sinks, the latency is always 0, since no other element is
delaying the buffer.

For pipelines with live sources, a latency is introduced, mostly because
of the way a live source works. Consider an audio source, it will start
capturing the first sample at time 0. If the source pushes buffers with
44100 samples at a time at 44100Hz, it will have collected the buffer at
second 1. Since the timestamp of the buffer is 0 and the time of the
clock is now `>= 1` second, the sink will drop this buffer because it is
too late. Without any latency compensation in the sink, all buffers will
be dropped.

The situation becomes more complex in the presence of:

- 2 live sources connected to 2 live sinks with different latencies
  - audio/video capture with synchronized live preview.
  - added latencies due to effects (delays, resamplers…)

- 1 live source connected to 2 live sinks
  - firewire DV
  - RTP, with added latencies because of jitter buffers.

- mixed live source and non-live source scenarios.
  - synchronized audio capture with non-live playback. (overdubs,..)

- clock slaving in the sinks due to the live sources providing their
    own clocks.

To perform the needed latency corrections in the above scenarios, we
must develop an algorithm to calculate a global latency for the
pipeline. This algorithm must be extensible, so that it can optimize the
latency at runtime. It must also be possible to disable or tune the
algorithm based on specific application needs (required minimal
latency).

## Pipelines without latency compensation

We show some examples to demonstrate the problem of latency in typical
capture pipelines.

### Example 1

An audio capture/playback pipeline.

* asrc: audio source, provides a clock
* asink audio sink, provides a clock

```
+--------------------------+
| pipeline                 |
| +------+      +-------+  |
| | asrc |      | asink |  |
| |     src -> sink     |  |
| +------+      +-------+  |
+--------------------------+
```

* *NULL→READY*:
    * asink: *NULL→READY*: probes device, returns `SUCCESS`
    * asrc: *NULL→READY*:  probes device, returns `SUCCESS`

* *READY→PAUSED*:
    * asink: *READY:→PAUSED* open device, returns `ASYNC`
    * asrc: *READY→PAUSED*:  open device, returns `NO_PREROLL`

- Since the source is a live source, it will only produce data in
the `PLAYING` state. To note this fact, it returns `NO_PREROLL`
from the state change function.

- This sink returns `ASYNC` because it can only complete the state
change to `PAUSED` when it receives the first buffer.

At this point the pipeline is not processing data and the clock is not
running. Unless a new action is performed on the pipeline, this situation will
never change.

* *PAUSED→PLAYING*: asrc clock selected because it is the most upstream clock
provider. asink can only provide a clock when it received the first buffer and
configured the device with the samplerate in the caps.

* sink: *PAUSED:→PLAYING*, sets pending state to `PLAYING`, returns `ASYNC` because it
is not prerolled. The sink will commit state to `PLAYING` when it prerolls.
* src: *PAUSED→PLAYING*: starts pushing buffers.

- since the sink is still performing a state change from `READY→PAUSED`, it remains `ASYNC`. The pending state will be set to
`PLAYING`.

- The clock starts running as soon as all the elements have been
set to `PLAYING`.

- the source is a live source with a latency. Since it is
synchronized with the clock, it will produce a buffer with
timestamp 0 and duration D after time D, ie. it will only be
able to produce the last sample of the buffer (with timestamp D)
at time D. This latency depends on the size of the buffer.

- the sink will receive the buffer with timestamp 0 at time `>= D`.
At this point the buffer is too late already and might be
dropped. This state of constantly dropping data will not change
unless a constant latency correction is added to the incoming
buffer timestamps.

The problem is due to the fact that the sink is set to (pending) `PLAYING`
without being prerolled, which only happens in live pipelines.

### Example 2

An audio/video capture/playback pipeline. We capture both audio and video and
have them played back synchronized again.

* asrc: audio source, provides a clock
* asink audio sink, provides a clock
* vsrc: video source
* vsink video sink

```
.--------------------------.
| pipeline                 |
| .------.      .-------.  |
| | asrc |      | asink |  |
| |     src -> sink     |  |
| '------'      '-------'  |
| .------.      .-------.  |
| | vsrc |      | vsink |  |
| |     src -> sink     |  |
| '------'      '-------'  |
'--------------------------'
```

The state changes happen in the same way as example 1. Both sinks end up with
pending state of `PLAYING` and a return value of `ASYNC` until they receive the
first buffer.

For audio and video to be played in sync, both sinks must compensate for the
latency of its source but must also use exactly the same latency correction.

Suppose asrc has a latency of 20ms and vsrc a latency of 33ms, the total
latency in the pipeline has to be at least 33ms. This also means that the
pipeline must have at least a `33 - 20 = 13ms` buffering on the audio stream or
else the audio src will underrun while the audiosink waits for the previous
sample to play.

### Example 3

An example of the combination of a non-live (file) and a live source (vsrc)
connected to live sinks (vsink, sink).

```
.--------------------------.
| pipeline                 |
| .------.      .-------.  |
| | file |      | sink  |  |
| |     src -> sink     |  |
| '------'      '-------'  |
| .------.      .-------.  |
| | vsrc |      | vsink |  |
| |     src -> sink     |  |
| '------'      '-------'  |
'--------------------------'
```

The state changes happen in the same way as example 1. Except sink will be
able to preroll (commit its state to `PAUSED`).

In this case sink will have no latency but vsink will. The total latency
should be that of vsink.

Note that because of the presence of a live source (vsrc), the pipeline can be
set to playing before the sink is able to preroll. Without compensation for the
live source, this might lead to synchronisation problems because the latency
should be configured in the element before it can go to `PLAYING`.

### Example 4

An example of the combination of a non-live and a live source. The non-live
source is connected to a live sink and the live source to a non-live sink.

```
.--------------------------.
| pipeline                 |
| .------.      .-------.  |
| | file |      | sink  |  |
| |     src -> sink     |  |
| '------'      '-------'  |
| .------.      .-------.  |
| | vsrc |      | files |  |
| |     src -> sink     |  |
| '------'      '-------'  |
'--------------------------'
```

The state changes happen in the same way as example 3. Sink will be
able to preroll (commit its state to `PAUSED`). files will not be able to
preroll.

sink will have no latency since it is not connected to a live source. files
does not do synchronisation so it does not care about latency.

The total latency in the pipeline is 0. The vsrc captures in sync with the
playback in sink.

As in example 3, sink can only be set to `PLAYING` after it successfully
prerolled.

## State Changes

A sink is never set to `PLAYING` before it is prerolled. In order to do
this, the pipeline (at the `GstBin` level) keeps track of all elements
that require preroll (the ones that return `ASYNC` from the state change).
These elements posted an `ASYNC_START` message without a matching
`ASYNC_DONE` one.

The pipeline will not change the state of the elements that are still
doing an `ASYNC` state change.

When an ASYNC element prerolls, it commits its state to `PAUSED` and posts
an `ASYNC_DONE` message. The pipeline notices this `ASYNC_DONE` message
and matches it with the `ASYNC_START` message it cached for the
corresponding element.

When all `ASYNC_START` messages are matched with an `ASYNC_DONE` message,
the pipeline proceeds with setting the elements to the final state
again.

The base time of the element was already set by the pipeline when it
changed the `NO_PREROLL` element to `PLAYING`. This operation has to be
performed in the separate async state change thread (like the one
currently used for going from `PAUSED→PLAYING` in a non-live pipeline).

## Query

The pipeline latency is queried with the `LATENCY` query.

* **`live`** `G_TYPE_BOOLEAN` (default `FALSE`): - if a live element is found upstream

* **`min-latency`** `G_TYPE_UINT64` (default 0, must not be `NONE`): - the minimum
latency in the pipeline, meaning the minimum time downstream elements
synchronizing to the clock have to wait until they can be sure all data
for the current running time has been received.

Elements answering the latency query and introducing latency must
set this to the maximum time for which they will delay data, while
considering upstream's minimum latency. As such, from an element's
perspective this is *not* its own minimum latency but its own
maximum latency.
Considering upstream's minimum latency generally means that the
element's own value is added to upstream's value, as this will give
the overall minimum latency of all elements from the source to the
current element:

```c
min_latency = upstream_min_latency + own_min_latency
```

* **`max-latency`** `G_TYPE_UINT64` (default 0, `NONE` meaning infinity): - the
maximum latency in the pipeline, meaning the maximum time an element
synchronizing to the clock is allowed to wait for receiving all data for the
current running time. Waiting for a longer time will result in data loss,
buffer overruns and underruns and, in general, breaks synchronized data flow
in the pipeline.

Elements answering the latency query should set this to the maximum
time for which they can buffer upstream data without blocking or
dropping further data. For an element, this value will generally be
its own minimum latency, but might be bigger than that if it can
buffer more data. As such, queue elements can be used to increase
the maximum latency.

The value set in the query should again consider upstream's maximum
latency:

- If the current element has blocking buffering, i.e. it does not drop data by
itself when its internal buffer is full, it should just add its own maximum
latency (i.e. the size of its internal buffer) to upstream's value. If
upstream's maximum latency, or the elements internal maximum latency was NONE
(i.e. infinity), it will be set to infinity.

```c
if (upstream_max_latency == NONE || own_max_latency == NONE)
  max_latency = NONE;
else
  max_latency = upstream_max_latency + own_max_latency;
```

If the element has multiple sinkpads, the minimum upstream latency is
the maximum of all live upstream minimum latencies.

If the current element has leaky buffering, i.e. it drops data by itself
when its internal buffer is full, it should take the minimum of its own
maximum latency and upstream’s. Examples for such elements are audio sinks
and sources with an internal ringbuffer, leaky queues and in general live
sources with a limited amount of internal buffers that can be used.

```c
max_latency = MIN (upstream_max_latency, own_max_latency)
```

> Note: many GStreamer base classes allow subclasses to set a
> minimum and maximum latency and handle the query themselves. These
> base classes assume non-leaky (i.e. blocking) buffering for the
> maximum latency. The base class' default query handler needs to be
> overridden to correctly handle leaky buffering.

 If the element has multiple sinkpads, the maximum upstream latency is the
 minimum of all live upstream maximum latencies.

## Event

The latency in the pipeline is configured with the `LATENCY` event, which
contains the following fields:

* **`latency`** `G_TYPE_UINT64`: the configured latency in the pipeline

## Latency compensation

Latency calculation and compensation is performed before the pipeline
proceeds to the `PLAYING` state.

When the pipeline collected all `ASYNC_DONE` messages it can calculate
the global latency as follows:

- perform a latency query on all sinks
- sources set their minimum and maximum latency
- other elements add their own values as described above
- latency = MAX (all min latencies)
- `if MIN (all max latencies) < latency`, we have an impossible
situation and we must generate an error indicating that this
pipeline cannot be played. This usually means that there is not
enough buffering in some chain of the pipeline. A queue can be added
to those chains.

The sinks gather this information with a `LATENCY` query upstream.
Intermediate elements pass the query upstream and add the amount of
latency they add to the result.

```
ex1: sink1: [20 - 20] sink2: [33 - 40]

    MAX (20, 33) = 33
    MIN (20, 40) = 20 < 33 -> impossible

ex2: sink1: [20 - 50] sink2: [33 - 40]

    MAX (20, 33) = 33
    MIN (50, 40) = 40 >= 33 -> latency = 33
```

The latency is set on the pipeline by sending a `LATENCY` event to the
sinks in the pipeline. This event configures the total latency on the
sinks. The sink forwards this `LATENCY` event upstream so that
intermediate elements can configure themselves as well.

After this step, the pipeline continues setting the pending state on its
elements.

A sink adds the latency value, received in the `LATENCY` event, to the
times used for synchronizing against the clock. This will effectively
delay the rendering of the buffer with the required latency. Since this
delay is the same for all sinks, all sinks will render data relatively
synchronised.

## Flushing a playing pipeline

We can implement resynchronisation after an uncontrolled `FLUSH` in (part
of) a pipeline in the same way. Indeed, when a flush is performed on a
`PLAYING` live element, a new base time must be distributed to this
element.

A flush in a pipeline can happen in the following cases:

  - flushing seek in the pipeline

  - performed by the application on the pipeline

  - performed by the application on an element

  - flush preformed by an element

  - after receiving a navigation event (DVD, …)

When a playing sink is flushed by a `FLUSH_START` event, an `ASYNC_START`
message is posted by the element. As part of the message, the fact that
the element got flushed is included. The element also goes to a pending
PAUSED state and has to be set to the `PLAYING` state again later.

The `ASYNC_START` message is kept by the parent bin. When the element
prerolls, it posts an `ASYNC_DONE` message.

When all `ASYNC_START` messages are matched with an `ASYNC_DONE` message,
the bin will capture a new `base_time` from the clock and will bring all
the sinks back to `PLAYING` after setting the new base time on them. It’s
also possible to perform additional latency calculations and adjustments
before doing this.

## Dynamically adjusting latency

An element that wants to change the latency in the pipeline can do this
by posting a `LATENCY` message on the bus. This message instructs the
pipeline to:

  - query the latency in the pipeline (which might now have changed)
    with a `LATENCY` query.

  - redistribute a new global latency to all elements with a `LATENCY`
    event.

A use case where the latency in a pipeline can change could be a network
element that observes an increased inter-packet arrival jitter or
excessive packet loss and decides to increase its internal buffering
(and thus the latency). The element must post a `LATENCY` message and
perform the additional latency adjustments when it receives the `LATENCY`
event from the downstream peer element.

In a similar way, the latency can be decreased when network conditions
improve.

Latency adjustments will introduce playback glitches in the sinks and
must only be performed in special conditions.
