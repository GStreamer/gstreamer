# Negotiation

Capabilities negotiation is the process of deciding on an adequate
format for dataflow within a GStreamer pipeline. Ideally, negotiation
(also known as "capsnego") transfers information from those parts of the
pipeline that have information to those parts of the pipeline that are
flexible, constrained by those parts of the pipeline that are not
flexible.

## Basic rules

These simple rules must be followed:

1) downstream suggests formats
2) upstream decides on format

There are 4 queries/events used in caps negotiation:

1) `GST_QUERY_CAPS`: get possible formats
2) `GST_QUERY_ACCEPT_CAPS`: check if format is possible
3) `GST_EVENT_CAPS`: configure format (downstream)
4) `GST_EVENT_RECONFIGURE`: inform upstream of possibly new caps

## Queries

A pad can ask the peer pad for its supported `GstCaps`. It does this with
the CAPS query. The list of supported caps can be used to choose an
appropriate `GstCaps` for the data transfer. The CAPS query works
recursively, elements should take their peers into consideration when
constructing the possible caps. Because the result caps can be very
large, a filter can be used to restrict the caps. Only the caps that
match the filter will be returned as the result caps. The order of the
filter caps gives the order of preference of the caller and should be
taken into account for the returned caps.

* **`filter`** (in) `GST_TYPE_CAPS` (default NULL): - a `GstCaps` to filter the results against
* **`caps`** (out) `GST_TYPE_CAPS` (default NULL): - the result caps

A pad can ask the peer pad if it supports a given caps. It does this
with the `ACCEPT_CAPS` query. The caps must be fixed. The `ACCEPT_CAPS`
query is not required to work recursively, it can simply return TRUE if
a subsequent CAPS event with those caps would return success.

* **`caps`** (in) `GST_TYPE_CAPS`: - a `GstCaps` to check, must be fixed
* **`result`** (out) `G_TYPE_BOOLEAN` (default FALSE): - TRUE if the caps are accepted

## Events

When a media format is negotiated, peer elements are notified of the
`GstCaps` with the CAPS event. The caps must be fixed.

* **`caps`** `GST_TYPE_CAPS`: - the negotiated `GstCaps`, must be fixed

## Operation

GStreamer’s two scheduling modes, push mode and pull mode, lend
themselves to different mechanisms to achieve this goal. As it is more
common we describe push mode negotiation first.

### Push-mode negotiation

Push-mode negotiation happens when elements want to push buffers and
need to decide on the format. This is called downstream negotiation
because the upstream element decides the format for the downstream
element. This is the most common case.

Negotiation can also happen when a downstream element wants to receive
another data format from an upstream element. This is called upstream
negotiation.

The basics of negotiation are as follows:

- `GstCaps` (see [caps](additional/design/caps.md)) are refcounted before they are pushed as
an event to describe the contents of the following buffer.

- An element should reconfigure itself to the new format received as a
CAPS event before processing the following buffers. If the data type
in the caps event is not acceptable, the element should refuse the
event. The element should also refuse the next buffers by returning
an appropriate `GST_FLOW_NOT_NEGOTIATED` return value from the
chain function.

- Downstream elements can request a format change of the stream by
sending a RECONFIGURE event upstream. Upstream elements will
renegotiate a new format when they receive a RECONFIGURE event.

The general flow for a source pad starting the negotiation.

```
            src              sink
             |                 |
             |  querycaps?     |
             |---------------->|
             |     caps        |
select caps  |< - - - - - - - -|
from the     |                 |
candidates   |                 |
             |                 |-.
             |  accepts?       | |
 type A      |---------------->| | optional
             |      yes        | |
             |< - - - - - - - -| |
             |                 |-'
             |  send_event()   |
send CAPS    |---------------->| Receive type A, reconfigure to
event A      |                 | process type A.
             |                 |
             |  push           |
push buffer  |---------------->| Process buffer of type A
             |                 |
```

One possible implementation in pseudo code:

```
[element wants to create a buffer]
if not format
  # see what we can do
  ourcaps = gst_pad_query_caps (srcpad)
  # see what the peer can do filtered against our caps
  candidates = gst_pad_peer_query_caps (srcpad, ourcaps)

  foreach candidate in candidates
    # make sure the caps is fixed
    fixedcaps = gst_pad_fixate_caps (srcpad, candidate)

    # see if the peer accepts it
    if gst_pad_peer_accept_caps (srcpad, fixedcaps)
      # store the caps as the negotiated caps, this will
      # call the setcaps function on the pad
      gst_pad_push_event (srcpad, gst_event_new_caps (fixedcaps))
      break
    endif
  done
endif
```

#### Negotiate allocator/bufferpool with the ALLOCATION query

```
    buffer = gst_buffer_new_allocate (NULL, size, 0);
    # fill buffer and push
```

The general flow for a sink pad starting a renegotiation.

```
            src              sink
             |                 |
             |  accepts?       |
             |<----------------| type B
             |      yes        |
             |- - - - - - - - >|-.
             |                 | | suggest B caps next
             |                 |<'
             |                 |
             |   push_event()  |
 mark      .-|<----------------| send RECONFIGURE event
renegotiate| |                 |
           '>|                 |
             |  querycaps()    |
renegotiate  |---------------->|
             |  suggest B      |
             |< - - - - - - - -|
             |                 |
             |  send_event()   |
send CAPS    |---------------->| Receive type B, reconfigure to
event B      |                 | process type B.
             |                 |
             |  push           |
push buffer  |---------------->| Process buffer of type B
             |                 |
```

#### Use cases:

##### `videotestsrc ! xvimagesink`

* Who decides what format to use?
    - src pad always decides, by convention. sinkpad can suggest a format
    by putting it high in the caps query result `GstCaps`.
    - since the src decides, it can always choose something that it can do,
    so this step can only fail if the sinkpad stated it could accept
    something while later on it couldn't.

* When does negotiation happen?
    - before srcpad does a push, it figures out a type as stated in 1), then
    it pushes a caps event with the type. The sink checks the media type and
    configures itself for this type.
    - the source then usually does an `ALLOCATION` query to negotiate a
    bufferpool with the sink. It then allocates a buffer from the pool and
    pushes it to the sink. Since the sink accepted the caps, it can create a
    pool for the format.
    - since the sink stated in 1) it could accept the type, it will be able to
    handle it.

* How can sink request another format?
    - sink asks if new format is possible for the source.
    - sink pushes `RECONFIGURE` event upstream
    - src receives the `RECONFIGURE` event and marks renegotiation
    - On the next buffer push, the source renegotiates the caps and the
    bufferpool. The sink will put the new new preferred format high in the list
    of caps it returns from its caps query.

##### `videotestsrc ! queue ! xvimagesink`

- queue proxies all accept and caps queries to the other peer pad.
- queue proxies the bufferpool
- queue proxies the `RECONFIGURE` event
- queue stores `CAPS` event in the queue. This means that the queue can
contain buffers with different types.

### Pull-mode negotiation

A pipeline in pull mode has different negotiation needs than one
activated in push mode. Push mode is optimized for two use cases:

- Playback of media files, in which the demuxers and the decoders are
the points from which format information should disseminate to the
rest of the pipeline; and

- Recording from live sources, in which users are accustomed to
putting a capsfilter directly after the source element; thus the
caps information flow proceeds from the user, through the potential
caps of the source, to the sinks of the pipeline.

In contrast, pull mode has other typical use cases:

- Playback from a lossy source, such as RTP, in which more knowledge
about the latency of the pipeline can increase quality; or

- Audio synthesis, in which audio APIs are tuned to produce only the
necessary number of samples, typically driven by a hardware
interrupt to fill a DMA buffer or a Jack[0] port buffer.

- Low-latency effects processing, whereby filters should be applied as
data is transferred from a ring buffer to a sink instead of
beforehand. For example, instead of using the internal alsasink
ringbuffer thread in push-mode wavsrc \! volume \! alsasink, placing
the volume inside the sound card writer thread via wavsrc \!
audioringbuffer \! volume \! alsasink.

[0] <http://jackit.sf.net>

The problem with pull mode is that the sink has to know the format in
order to know how many bytes to pull via `gst_pad_pull_range()`. This
means that before pulling, the sink must initiate negotation to decide
on a format.

Recalling the principles of capsnego, whereby information must flow from
those that have it to those that do not, we see that the three named use
cases have different negotiation requirements:

- RTP and low-latency playback are both like the normal playback case,
in which information flows downstream.

- In audio synthesis, the part of the pipeline that has the most
information is the sink, constrained by the capabilities of the
graph that feeds it. However the caps are not completely specified;
at some point the user has to intervene to choose the sample rate,
at least. This can be done externally to gstreamer, as in the jack
elements, or internally via a capsfilter, as is customary with live
sources.

Given that sinks potentially need the input of sources, as in the RTP
case and at least as a filter in the synthesis case, there must be a
negotiation phase before the pull thread is activated. Also, given the
low latency offered by pull mode, we want to avoid capsnego from within
the pulling thread, in case it causes us to miss our scheduling
deadlines.

The pull thread is usually started in the `PAUSED→PLAYING` state change.
We must be able to complete the negotiation before this state change
happens.

The time to do capsnego, then, is after the `SCHEDULING` query has
succeeded, but before the sink has spawned the pulling thread.

#### Mechanism

The sink determines that the upstream elements support pull based
scheduling by doing a `SCHEDULING` query.

The sink initiates the negotiation process by intersecting the results
of `gst_pad_query_caps()` on its sink pad and its peer src pad. This is
the operation performed by `gst_pad_get_allowed_caps()` In the simple
passthrough case, the peer pad’s caps query should return the
intersection of calling `get_allowed_caps()` on all of its sink pads. In
this way the sink element knows the capabilities of the entire pipeline.

The sink element then fixates the resulting caps, if necessary,
resulting in the flow caps. From now on, the caps query of the sinkpad
will only return these fixed caps meaning that upstream elements will
only be able to produce this format.

If the sink element could not set caps on its sink pad, it should post
an error message on the bus indicating that negotiation was not
possible.

When negotiation succeeded, the sinkpad and all upstream internally
linked pads are activated in pull mode. Typically, this operation will
trigger negotiation on the downstream elements, which will now be forced
to negotiate to the final fixed desired caps of the sinkpad.

After these steps, the sink element returns `ASYNC` from the state change
function. The state will commit to `PAUSED` when the first buffer is
received in the sink. This is needed to provide a consistent API to the
applications that expect `ASYNC` return values from sinks but it also
allows us to perform the remainder of the negotiation outside of the
context of the pulling thread.

### Patterns

We can identify 3 patterns in negotiation:

* Fixed : Can't choose the output format
    - Caps encoded in the stream
    - A video/audio decoder
    - usually uses `gst_pad_use_fixed_caps()`

* Transform
    - Caps not modified (passthrough)
    - can do caps transform based on element property
    - fixed caps get transformed into fixed caps
    - videobox

* Dynamic : can choose output format
    - A converter element
    - depends on downstream caps, needs to do a CAPS query to find
    transform.
    - usually prefers to use the identity transform
    - fixed caps can be transformed into unfixed caps.
