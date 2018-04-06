---
title: Pre-made base classes
...

# Pre-made base classes

So far, we've been looking at low-level concepts of creating any type of
GStreamer element. Now, let's assume that all you want is to create a
simple audiosink that works exactly the same as, say, “esdsink”, or a
filter that simply normalizes audio volume. Such elements are very
general in concept and since they do nothing special, they should be
easier to code than to provide your own scheduler activation functions
and doing complex caps negotiation. For this purpose, GStreamer provides
base classes that simplify some types of elements. Those base classes
will be discussed in this chapter.

## Writing a sink

Sinks are special elements in GStreamer. This is because sink elements
have to take care of *preroll*, which is the process that takes care
that elements going into the `GST_STATE_PAUSED` state will have buffers
ready after the state change. The result of this is that such elements
can start processing data immediately after going into the
`GST_STATE_PLAYING` state, without requiring to take some time to
initialize outputs or set up decoders; all that is done already before
the state-change to `GST_STATE_PAUSED` successfully completes.

Preroll, however, is a complex process that would require the same code
in many elements. Therefore, sink elements can derive from the
`GstBaseSink` base-class, which does preroll and a few other utility
functions automatically. The derived class only needs to implement a
bunch of virtual functions and will work automatically.

The base class implements much of the synchronization logic that a sink
has to perform.

The `GstBaseSink` base-class specifies some limitations on elements,
though:

  - It requires that the sink only has one sinkpad. Sink elements that
    need more than one sinkpad, must make a manager element with
    multiple GstBaseSink elements inside.

Sink elements can derive from `GstBaseSink` using the usual `GObject`
convenience macro `G_DEFINE_TYPE ()`:

``` c
G_DEFINE_TYPE (GstMySink, gst_my_sink, GST_TYPE_BASE_SINK);

[..]

static void
gst_my_sink_class_init (GstMySinkClass * klass)
{
  klass->set_caps = [..];
  klass->render = [..];
[..]
}

```

The advantages of deriving from `GstBaseSink` are numerous:

  - Derived implementations barely need to be aware of preroll, and do
    not need to know anything about the technical implementation
    requirements of preroll. The base-class does all the hard work.

  - Less code to write in the derived class, shared code (and thus
    shared bugfixes).

There are also specialized base classes for audio and video, let's look
at those a bit.

### Writing an audio sink

Essentially, audio sink implementations are just a special case of a
general sink. An audio sink has the added complexity that it needs to
schedule playback of samples. It must match the clock selected in the
pipeline against the clock of the audio device and calculate and
compensate for drift and jitter.

There are two audio base classes that you can choose to derive from,
depending on your needs: `GstAudioBasesink` and `GstAudioSink`. The
audiobasesink provides full control over how synchronization and
scheduling is handled, by using a ringbuffer that the derived class
controls and provides. The audiosink base-class is a derived class of
the audiobasesink, implementing a standard ringbuffer implementing
default synchronization and providing a standard audio-sample clock.
Derived classes of this base class merely need to provide a `_open
()`, `_close ()` and a `_write
()` function implementation, and some optional functions. This should
suffice for many sound-server output elements and even most interfaces.
More demanding audio systems, such as Jack, would want to implement the
`GstAudioBaseSink` base-class.

The `GstAudioBaseSink` has little to no limitations and should fit
virtually every implementation, but is hard to implement. The
`GstAudioSink`, on the other hand, only fits those systems with a simple
`open
()` / `close ()` / `write
()` API (which practically means pretty much all of them), but has the
advantage that it is a lot easier to implement. The benefits of this
second base class are large:

  - Automatic synchronization, without any code in the derived class.

  - Also automatically provides a clock, so that other sinks (e.g. in
    case of audio/video playback) are synchronized.

  - Features can be added to all audiosinks by making a change in the
    base class, which makes maintenance easy.

  - Derived classes require only three small functions, plus some
    `GObject` boilerplate code.

In addition to implementing the audio base-class virtual functions,
derived classes can (should) also implement the `GstBaseSink` `set_caps
()` and `get_caps ()` virtual functions for negotiation.

### Writing a video sink

Writing a videosink can be done using the `GstVideoSink` base-class,
which derives from `GstBaseSink` internally. Currently, it does nothing
yet but add another compile dependency, so derived classes will need to
implement all base-sink virtual functions. When they do this correctly,
this will have some positive effects on the end user experience with the
videosink:

  - Because of preroll (and the `preroll ()` virtual function), it is
    possible to display a video frame already when going into the
    `GST_STATE_PAUSED` state.

  - By adding new features to `GstVideoSink`, it will be possible to add
    extensions to videosinks that affect all of them, but only need to
    be coded once, which is a huge maintenance benefit.

## Writing a source

In the previous part, particularly [Providing random access][random-access],
we have learned that some types of elements can provide random access. This
applies most definitely to source elements reading from a randomly seekable
location, such as file sources. However, other source elements may be better
described as a live source element, such as a camera source, an audio
card source and such; those are not seekable and do not provide
byte-exact access. For all such use cases, GStreamer provides two base
classes: `GstBaseSrc` for the basic source functionality, and
`GstPushSrc`, which is a non-byte exact source base-class. The
pushsource base class itself derives from basesource as well, and thus
all statements about the basesource apply to the pushsource, too.

The basesrc class does several things automatically for derived classes,
so they no longer have to worry about it:

  - Fixes to `GstBaseSrc` apply to all derived classes automatically.

  - Automatic pad activation handling, and task-wrapping in case we get
    assigned to start a task ourselves.

The `GstBaseSrc` may not be suitable for all cases, though; it has
limitations:

  - There is one and only one sourcepad. Source elements requiring
    multiple sourcepads must implement a manager bin and use multiple
    source elements internally or make a manager element that uses a
    source element and a demuxer inside.

It is possible to use special memory, such as X server memory pointers
or `mmap ()`'ed memory areas, as data pointers in buffers returned from
the `create()` virtual function.

[random-access]: plugin-development/advanced/scheduling.md#providing-random-access

### Writing an audio source

An audio source is nothing more but a special case of a pushsource.
Audio sources would be anything that reads audio, such as a source
reading from a soundserver, a kernel interface (such as ALSA) or a test
sound / signal generator. GStreamer provides two base classes, similar
to the two audiosinks described in [Writing an audio
sink](#writing-an-audio-sink); one is ringbuffer-based, and requires the
derived class to take care of its own scheduling, synchronization and
such. The other is based on this `GstAudioBaseSrc` and is called
`GstAudioSrc`, and provides a simple `open ()`, `close ()` and `read ()`
interface, which is rather simple to implement and will suffice for most
soundserver sources and audio interfaces (e.g. ALSA or OSS) out there.

The `GstAudioSrc` base-class has several benefits for derived classes,
on top of the benefits of the `GstPushSrc` base-class that it is based
on:

  - Does syncronization and provides a clock.

  - New features can be added to it and will apply to all derived
    classes automatically.

## Writing a transformation element

A third base-class that GStreamer provides is the `GstBaseTransform`.
This is a base class for elements with one sourcepad and one sinkpad
which act as a filter of some sort, such as volume changing, audio
resampling, audio format conversion, and so on and so on. There is quite
a lot of bookkeeping that such elements need to do in order for things
such as buffer allocation forwarding, passthrough, in-place processing
and such to all work correctly. This base class does all that for you,
so that you just need to do the actual processing.

Since the `GstBaseTransform` is based on the 1-to-1 model for filters,
it may not apply well to elements such as decoders, which may have to
parse properties from the stream. Also, it will not work for elements
requiring more than one sourcepad or sinkpad.
