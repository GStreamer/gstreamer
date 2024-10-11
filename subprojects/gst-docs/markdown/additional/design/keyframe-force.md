# Forcing keyframes

Consider the following use case:

We have a pipeline that performs video and audio capture from a live
source, compresses and muxes the streams and writes the resulting data
into a file.

Inside the uncompressed video data we have a specific pattern inserted
at specific moments that should trigger a switch to a new file, meaning,
we close the existing file we are writing to and start writing to a new
file.

We want the new file to start with a keyframe so that one can start
decoding the file immediately.

## Components

1)  We need an element that is able to detect the pattern in the video
    stream.

2)  We need to inform the video encoder that it should start encoding a
    keyframe starting from exactly the frame with the pattern.

3)  We need to inform the muxer that it should flush out any pending
    data and start creating the start of a new file with the keyframe as
    a first video frame.

4)  We need to inform the sink element that it should start writing to
    the next file. This requires application interaction to instruct the
    sink of the new filename. The application should also be free to
    ignore the boundary and continue to write to the existing file. The
    application will typically use an event pad probe to detect the
    custom event.

## Implementation

### Downstream

The implementation would consist of generating a `GST_EVENT_CUSTOM_DOWNSTREAM`
event that marks the keyframe boundary. This event is inserted into the
pipeline by the application upon a certain trigger. In the above use case
this trigger would be given by the element that detects the pattern, in the
form of an element message.

The custom event would travel further downstream to instruct encoder,
muxer and sink about the possible switch.

The information passed in the event consists of:

**GstForceKeyUnit**

 - **"timestamp"** (`G_TYPE_UINT64`): the timestamp of the buffer that
   triggered the event.

 - **"stream-time"** (`G_TYPE_UINT64`): the stream position that triggered the event.

 - **"running-time"** (`G_TYPE_UINT64`): the running time of the stream when
   the event was triggered.

 - **"all-headers"** (`G_TYPE_BOOLEAN`): Send all headers, including
   those in the caps or those sent at the start of the stream.

 - **...**: optional other data fields.

Note that this event is purely informational, no element is required to
perform an action but it should forward the event downstream, just like
any other event it does not handle.

Elements understanding the event should behave as follows:

1)  The video encoder receives the event before the next frame. Upon
    reception of the event it schedules to encode the next frame as a
    keyframe. Before pushing out the encoded keyframe it must push the
    `GstForceKeyUnit` event downstream.

2)  The muxer receives the `GstForceKeyUnit` event and flushes out its
    current state, preparing to produce data that can be used as a
    keyunit. Before pushing out the new data it pushes the
    `GstForceKeyUnit` event downstream.

3)  The application receives the `GstForceKeyUnit` on a sink padprobe of
    the sink and reconfigures the sink to make it perform new actions
    after receiving the next buffer.

### Upstream

When using RTP packets can get lost or receivers can be added at any
time, they may request a new key frame.

An downstream element sends an upstream `GstForceKeyUnit` event up the
pipeline.

When an element produces some kind of key unit in output, but has no
such concept in its input (like an encoder that takes raw frames), it
consumes the event (doesn't pass it upstream), and instead sends a
downstream `GstForceKeyUnit` event and a new keyframe.
