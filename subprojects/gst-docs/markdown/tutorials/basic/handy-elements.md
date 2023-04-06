# Basic tutorial 14: Handy elements

## Goal

This tutorial gives a list of handy GStreamer elements that are worth
knowing. They range from powerful all-in-one elements that allow you to
build complex pipelines easily (like `playbin`), to little helper
elements which are extremely useful when debugging.

For simplicity, the following examples are given using the
`gst-launch-1.0` tool (Learn about it in
[](tutorials/basic/gstreamer-tools.md)). Use the `-v` command line
parameter if you want to see the Pad Caps that are being negotiated.

## Bins

These are Bin elements which you treat as a single element and they take
care of instantiating all the necessary internal pipeline to accomplish
their task.

### `playbin`

This element has been extensively used throughout the tutorials. It
manages all aspects of media playback, from source to display, passing
through demuxing and decoding. It is so flexible and has so many options
that a whole set of tutorials are devoted to it. See the [](tutorials/playback/index.md) for more details.

### `uridecodebin`

This element decodes data from a URI into raw media. It selects a source
element that can handle the given URI scheme and connects it to
a `decodebin` element. It acts like a demuxer, so it offers as many
source pads as streams are found in the
media.

``` bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! videoconvert ! autovideosink
```

``` bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! audioconvert ! autoaudiosink
```

### `decodebin`

This element automatically constructs a decoding pipeline using
available decoders and demuxers via auto-plugging until raw media is
obtained.  It is used internally by `uridecodebin` which is often more
convenient to use, as it creates a suitable source element as well. It
replaces the old `decodebin` element. It acts like a demuxer, so it
offers as many source pads as streams are found in the
media.

``` bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! decodebin ! autovideosink
```

## File input/output

### `filesrc`

This element reads a local file and produces media with `ANY` Caps. If
you want to obtain the correct Caps for the media, explore the stream by
using a `typefind` element or by setting the `typefind` property
of `filesrc` to
`TRUE`.

``` c
gst-launch-1.0 filesrc location=f:\\media\\sintel\\sintel_trailer-480p.webm ! decodebin ! autovideosink
```

### `filesink`

This element writes to a file all the media it receives. Use the
`location` property to specify the file
name.

```
gst-launch-1.0 audiotestsrc ! vorbisenc ! oggmux ! filesink location=test.ogg
```

## Network

### `souphttpsrc`

This element receives data as a client over the network via HTTP using
the [libsoup](https://wiki.gnome.org/Projects/libsoup) library. Set the URL to retrieve through the `location`
property.

``` bash
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! decodebin ! autovideosink
```

## Test media generation

These elements are very useful to check if other parts of the pipeline
are working, by replacing the source by one of these test sources which
are “guaranteed” to work.

### `videotestsrc`

This element produces a video pattern (selectable among many different
options with the `pattern` property). Use it to test video pipelines.

``` bash
gst-launch-1.0 videotestsrc ! videoconvert ! autovideosink
```

### `audiotestsrc`

This element produces an audio wave (selectable among many different
options with the `wave` property). Use it to test audio pipelines.

``` bash
gst-launch-1.0 audiotestsrc ! audioconvert ! autoaudiosink
```

## Video adapters

### `videoconvert`

This element converts from one color space (e.g. RGB) to another one
(e.g. YUV). It can also convert between different YUV formats (e.g.
I420, NV12, YUY2 …) or RGB format arrangements (e.g. RGBA, ARGB, BGRA…).

This is normally your first choice when solving negotiation problems.
When not needed, because its upstream and downstream elements can
already understand each other, it acts in pass-through mode having
minimal impact on the performance.

As a rule of thumb, always use `videoconvert` whenever you use
elements whose Caps are unknown at design time, like `autovideosink`, or
that can vary depending on external factors, like decoding a
user-provided file.

``` bash
gst-launch-1.0 videotestsrc ! videoconvert ! autovideosink
```

### `videorate`

This element takes an incoming stream of time-stamped video frames and
produces a stream that matches the source pad's frame rate. The
correction is performed by dropping and duplicating frames, no fancy
algorithm is used to interpolate frames.

This is useful to allow elements requiring different frame rates to
link. As with the other adapters, if it is not needed (because there is
a frame rate on which both Pads can agree), it acts in pass-through mode
and does not impact performance.

It is therefore a good idea to always use it whenever the actual frame
rate is unknown at design time, just in
case.

``` c
gst-launch-1.0 videotestsrc ! video/x-raw,framerate=30/1 ! videorate ! video/x-raw,framerate=1/1 ! videoconvert ! autovideosink
```

### `videoscale`

This element resizes video frames. By default the element tries to
negotiate to the same size on the source and sink Pads so that no
scaling is needed. It is therefore safe to insert this element in a
pipeline to get more robust behavior without any cost if no scaling is
needed.

This element supports a wide range of color spaces including various YUV
and RGB formats and is therefore generally able to operate anywhere in a
pipeline.

If the video is to be output to a window whose size is controlled by the
user, it is a good idea to use a `videoscale` element, since not all
video sinks are capable of performing scaling
operations.

``` bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! videoscale ! video/x-raw,width=178,height=100 ! videoconvert ! autovideosink
```

## Audio adapters

### `audioconvert`

This element converts raw audio buffers between various possible
formats. It supports integer to float conversion, width/depth
conversion, signedness and endianness conversion and channel
transformations.

Like `videoconvert` does for video, you use this to solve
negotiation problems with audio, and it is generally safe to use it
liberally, since this element does nothing if it is not needed.

``` bash
gst-launch-1.0 audiotestsrc ! audioconvert ! autoaudiosink
```

### `audioresample`

This element resamples raw audio buffers to different sampling rates
using a configurable windowing function to enhance quality.

Again, use it to solve negotiation problems regarding sampling rates and
do not fear to use it
generously.

``` bash
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! audioresample ! audio/x-raw,rate=4000 ! audioconvert ! autoaudiosink
```

### `audiorate`

This element takes an incoming stream of time-stamped raw audio frames
and produces a perfect stream by inserting or dropping samples as
needed. It does not allow the sample rate to be changed
as `videorate` does, it just fills gaps and removes overlapped samples
so the output stream is continuous and “clean”.

It is useful in situations where the timestamps are going to be lost
(when storing into certain file formats, for example) and the receiver
will require all samples to be present. It is cumbersome to exemplify
this, so no example is given.

![Warning](images/icons/emoticons/warning.svg)
Most of the time, `audiorate` is not what you want.

## Multithreading

### `queue`

Queues have been explained in [](tutorials/basic/multithreading-and-pad-availability.md). Basically, a queue performs two tasks:

  - Data is queued until a selected limit is reached. Any attempt to
    push more buffers into the queue blocks the pushing thread until
    more space becomes available.
  - The queue creates a new thread on the source Pad to decouple the
    processing on sink and source Pads.

Additionally, `queue` triggers signals when it is about to become empty
or full (according to some configurable thresholds), and can be
instructed to drop buffers instead of blocking when it is full.

As a rule of thumb, prefer the simpler `queue` element
over `queue2` whenever network buffering is not a concern to you.
See [](tutorials/basic/multithreading-and-pad-availability.md)
for an example.

### `queue2`

This element is not an evolution of `queue`. It has the same design
goals but follows a different implementation approach, which results in
different features. Unfortunately, it is often not easy to tell which
queue is the best choice.

`queue2` performs the two tasks listed above for `queue`, and,
additionally, is able to store the received data (or part of it) on a
disk file, for later retrieval. It also replaces the signals with the
more general and convenient buffering messages described in
[](tutorials/basic/streaming.md).

As a rule of thumb, prefer `queue2` over `queue` whenever network
buffering is a concern to you. See [](tutorials/basic/streaming.md)
for an example (`queue2` is hidden inside `playbin`).

### `multiqueue`

This element provides queues for multiple streams simultaneously, and
eases their management, by allowing some queues to grow if no data is
being received on other streams, or by allowing some queues to drop data
if they are not connected to anything (instead of returning an error, as
a simpler queue would do). Additionally, it synchronizes the different
streams, ensuring that none of them goes too far ahead of the others.

This is an advanced element. It is found inside `decodebin`, but you
will rarely need to instantiate it yourself in a normal playback
application.

### `tee`

[](tutorials/basic/multithreading-and-pad-availability.md) already
showed how to use a `tee` element, which splits data to multiple pads.
Splitting the data flow is useful, for example, when capturing a video
where the video is shown on the screen and also encoded and written to a
file. Another example is playing music and hooking up a visualization
module.

One needs to use separate `queue` elements in each branch to provide
separate threads for each branch. Otherwise a blocked dataflow in one
branch would stall the other
branches.

```
gst-launch-1.0 audiotestsrc ! tee name=t ! queue ! audioconvert ! autoaudiosink t. ! queue ! wavescope ! videoconvert ! autovideosink
```

## Capabilities

### `capsfilter`
[](tutorials/basic/gstreamer-tools.md) already
explained how to use Caps filters with `gst-launch-1.0`. When building a
pipeline programmatically, Caps filters are implemented with
the `capsfilter` element. This element does not modify data as such,
but enforces limitations on the data format.

``` bash
gst-launch-1.0 videotestsrc ! video/x-raw, format=GRAY8 ! videoconvert ! autovideosink
```

### `typefind`

This element determines the type of media a stream contains. It applies
typefind functions in the order of their rank. Once the type has been
detected it sets its source Pad Caps to the found media type and emits
the `have-type` signal.

It is instantiated internally by `decodebin`, and you can use it too to
find the media type, although you can normally use the
`GstDiscoverer` which provides more information (as seen in
[](tutorials/basic/media-information-gathering.md)).

## Debugging

### `fakesink`

This sink element simply swallows any data fed to it. It is useful when
debugging, to replace your normal sinks and rule them out of the
equation. It can be very verbose when combined with the `-v` switch
of `gst-launch-1.0`, so use the `silent` property to remove any unwanted
noise.

```
gst-launch-1.0 audiotestsrc num-buffers=1000 ! fakesink sync=false
```

### `identity`

This is a dummy element that passes incoming data through unmodified. It
has several useful diagnostic functions, such as offset and timestamp
checking, or buffer dropping. Read its documentation to learn all the
things this seemingly harmless element can
do.

```
gst-launch-1.0 audiotestsrc ! identity drop-probability=0.1 ! audioconvert ! autoaudiosink
```

## Conclusion

This tutorial has listed a few elements which are worth knowing, due to
their usefulness in the day-to-day work with GStreamer. Some are
valuable for production pipelines, whereas others are only needed for
debugging purposes.

It has been a pleasure having you here, and see you soon!
