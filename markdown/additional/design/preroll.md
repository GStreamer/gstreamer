# Preroll

A sink element can only complete the state change to `PAUSED` after a
buffer has been queued on the input pad or pads. This process is called
prerolling and is needed to fill the pipeline with buffers so that the
transition to `PLAYING` goes as fast as possible, with no visual delay for
the user.

Preroll is also crucial in maintaining correct audio and video
synchronisation and ensuring that no buffers are dropped in the sinks.

After receiving a buffer (or EOS) on a pad the chain/event function
should wait to render the buffers or in the EOS case, wait to post the
EOS message. While waiting, the sink will wait for the preroll cond to
be signalled.

Several things can happen that require the preroll cond to be signalled.
These include state changes or flush events. The prerolling is
implemented in sinks (see [Sink elements](additional/design/element-sink.md)).

## Committing the state

When going to `PAUSED` and `PLAYING` a buffer should be queued in the pad.
We also make this a requirement for going to `PLAYING` since a flush event
in the `PAUSED` state could unqueue the buffer again.

The state is committed in the following conditions:

- a buffer is received on a sinkpad;
- a GAP event is received on a sinkpad;
- an EOS event is received on a sinkpad.

We require the state change to be committed in EOS as well, since an EOS
, by definition, means no buffer is going to arrive anymore.

After the state is committed, a blocking wait should be performed for the
next event. Some sinks might render the preroll buffer before starting
this blocking wait.

## Unlocking the preroll

The following conditions unlock the preroll:

- a state change
- a flush event

When the preroll is unlocked by a flush event, a return value of
`GST_FLOW_FLUSHING` is to be returned to the peer pad.

When preroll is unlocked by a state change to `PLAYING`, playback and
rendering of the buffers shall start.

When preroll is unlocked by a state change to READY, the buffer is to be
discarded and a `GST_FLOW_FLUSHING` shall be returned to the peer
element.
