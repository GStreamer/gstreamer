# Seqnums (Sequence numbers)

Seqnums are integers associated to events and messages. They are used to
identify a group of events and messages as being part of the same
*operation* over the pipeline.

Whenever a new event or message is created, a seqnum is set into them.
This seqnum is created from an ever increasing source (starting from 0
but it might wrap around), so each new event and message gets a new and
hopefully unique seqnum.

Suppose an element receives an event A and, as part of the logic of
handling the event A, creates a new event B. B should have its seqnum be
the same as A, because they are part of the same operation. The same
logic applies if this element had to create multiple events or messages,
all of those should have the seqnum set to the value on the received
event. For example, when a sink element receives an EOS event and
creates a new EOS message to post, it should copy the seqnum from the
event to the message because the EOS message is a consequence of the EOS
event being received.

Preserving the seqnums accross related events and messages allows the
elements and applications to identify a set of events/messages as being
part of a single operation on the pipeline. For example, flushes,
segments and EOS that are related to a seek event started by the
application.

Seqnums are also useful for elements to discard duplicated events,
avoiding handling them again.

## Scenarios

Below are some scenarios as examples of how to handle seqnums when
receving events:

### Forcing EOS on the pipeline

The application has a pipeline running and does a
`gst_element_send_event()` to the pipeline with an EOS event. All the
sources in the pipeline will have their `send_event` handlers called and
will receive the event from the application.

When handling this event, the sources will push either the same EOS
downstream or create their own EOS event and push. In the later case,
the source should copy the seqnum from the original EOS to the newly
created one. This same logic applies to all elements that receive the EOS
downstream, either push the same event or, if creating a new one, copy
the seqnum.

When the EOS reaches the sink, it will create an EOS message, copy the
seqnum to the message and post to the bus. The application receives the
message and can compare its seqnum with the one from the
original event sent to the pipeline. If they match, it knows that this
EOS message was caused by the event it pushed and not from other reason
(input finished or configured segment was over).

### Seeking

A seek event sent to the pipeline is forwarded to all sinks in it. These
sinks, then, push the `SEEK` event upstream until they reach an element
that is capable of handling it. If the element handling the seek has
multiple source pads (tipically a demuxer is handling the seek) it might
receive the same seek event on all pads. To prevent handling the same
seek event multiple times, the seqnum can be used to identify those
events as being the same and only handle the one received first.

Also, when handling the seek, the element might push `FLUSH_START`,
`FLUSH_STOP` and a segment event. All these events should have the
seqnum of the received seek event. When this segment is over and an
`EOS/SEGMENT_DONE` event is going to be pushed, it should also have the
seqnum of the seek that originated the segment to be played.

Having the same seqnum as the seek on the `SEGMENT_DONE` or EOS events is
important for the application to identify that the segment requested by
its seek has finished playing.

## Questions

What happens if the application has sent a seek to the pipeline and,
while the segment relative to this seek is playing, it sends an EOS
event? Should the EOS pushed by the source have the seqnum of the
segment or the EOS from the application?

If the EOS was received from the application before the segment ended,
it should have the EOS from the application event. If the segment ends
before the application event is received/handled, it should have the
seek/segment seqnum.
