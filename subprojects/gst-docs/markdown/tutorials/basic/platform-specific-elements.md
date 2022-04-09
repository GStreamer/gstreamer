# Basic tutorial 16: Platform-specific elements

## Goal

Even though GStreamer is a multiplatform framework, not all the elements
are available on all platforms. For example, the video sinks
depend heavily on the underlying windowing system, and a different one
needs to be selected depending on the platform. You normally do not need
to worry about this when using elements like `playbin` or
`autovideosink`, but, for those cases when you need to use one of the
sinks that are only available on specific platforms, this tutorial hints
you some of their peculiarities.

## Cross Platform

### `glimagesink`

This video sink is based on
[OpenGL](http://en.wikipedia.org/wiki/OpenGL) or [OpenGL ES](https://en.wikipedia.org/wiki/OpenGL_ES). It supports rescaling
and filtering of the scaled image to alleviate aliasing. It implements
the VideoOverlay interface, so the video window can be re-parented
(embedded inside other windows). This is the video sink recommended on
most platforms except for Windows (On Windows, `d3d11videosink` is recommended).
In particular, on Android and iOS, it is the only
available video sink. It can be decomposed into
`glupload ! glcolorconvert ! glimagesinkelement` to insert further OpenGL
hardware accelerated processing into the pipeline.

## Linux

### `ximagesink`

A standard RGB only X-based video sink. It implements the VideoOverlay
interface, so the video window can be re-parented (embedded inside
other windows). It does not support scaling or color formats other
than RGB; it has to be performed by different means (using the
`videoscale` element, for example).

### `xvimagesink`

An X-based video sink, using the [X Video
Extension](http://en.wikipedia.org/wiki/X_video_extension) (Xv). It
implements the VideoOverlay interface, so the video window can be
re-parented (embedded inside other windows). It can perform scaling
efficiently, on the GPU. It is only available if the hardware and
corresponding drivers support the Xv extension.

### `alsasink`

This audio sink outputs to the sound card via
[ALSA](http://www.alsa-project.org/) (Advanced Linux Sound
Architecture). This sink is available on almost every Linux platform. It
is often seen as a “low level” interface to the sound card, and can be
complicated to configure (See the comment on
[](tutorials/playback/digital-audio-pass-through.md)).

### `pulsesink`

This sink plays audio to a [PulseAudio](http://www.pulseaudio.org/)
server. It is a higher level abstraction of the sound card than ALSA,
and is therefore easier to use and offers more advanced features. It has
been known to be unstable on some older Linux distributions, though.

## Mac OS X

### `osxvideosink`

This is the  video sink available to GStreamer on Mac OS X. It is also
possible to draw using `glimagesink` using OpenGL.

### `osxaudiosink`

This is the only audio sink available to GStreamer on Mac OS X.

## Windows

### `d3d11videosink`

This video sink is based on [Direct3D11](https://en.wikipedia.org/wiki/Direct3D#Direct3D_11)
and is the recommended element on Windows.
It supports VideoOverlay interface and rescaling/colorspace conversion
in [zero-copy](https://en.wikipedia.org/wiki/Zero-copy) manner. This element
is the most performant and featureful video sink element on Windows.

### `d3dvideosink`

This video sink is based on
[Direct3D9](https://en.wikipedia.org/wiki/Direct3D#Direct3D_9).
It supports rescaling and filtering of the scaled image to alleviate aliasing.
It implements the VideoOverlay interface, so the video window can be re-parented (embedded inside other windows).
This element is not recommended for applications targetting Windows 8 or more recent.


### `dshowvideosink (deprecated)`

This video sink is based on [Direct
Show](http://en.wikipedia.org/wiki/Direct_Show).  It can use different
rendering back-ends, like
[EVR](http://en.wikipedia.org/wiki/Enhanced_Video_Renderer),
[VMR9](http://en.wikipedia.org/wiki/Direct_Show#Video_rendering_filters)
or
[VMR7](http://en.wikipedia.org/wiki/Direct_Show#Video_rendering_filters),
EVR only being available on Windows Vista or more recent. It supports
rescaling and filtering of the scaled image to alleviate aliasing. It
implements the VideoOverlay interface, so the video window can be
re-parented (embedded inside other windows). This element is not recommended
in most cases.

### `wasapisink` and `wasapi2sink`

Those elements are the default audio sink elements on Windows, based on
[WASAPI](https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi),
which is available on Vista or more recent. Note that `wasapi2sink` is
a replacement of `wasapisink` and `wasapi2sink` is default for Windows 8 or
more recent. Otherwise `wasapisink` will be default audio sink element.

### `directsoundsink (deprecated)`

This audio sink element is based on
[DirectSound](http://en.wikipedia.org/wiki/DirectSound), which is available in
all Windows versions.

### `dshowdecwrapper`

[Direct Show](http://en.wikipedia.org/wiki/Direct_Show) is a multimedia
framework similar to GStreamer. They are different enough, though, so
that their pipelines cannot be interconnected. However, through this
element, GStreamer can benefit from the decoding elements present in
Direct Show. `dshowdecwrapper` wraps multiple Direct Show decoders so
they can be embedded in a GStreamer pipeline. Use the `gst-inspect-1.0` tool
(see [](tutorials/basic/gstreamer-tools.md)) to see the
available decoders.

## Android

### `openslessink`

This is the only audio sink available to GStreamer on Android. It is
based on [OpenSL ES](http://en.wikipedia.org/wiki/OpenSL_ES).

### `openslessrc`

This is the only audio source available to GStreamer on Android. It is
based on [OpenSL ES](http://en.wikipedia.org/wiki/OpenSL_ES).

### `androidmedia`

[android.media.MediaCodec](http://developer.android.com/reference/android/media/MediaCodec.html)
is an Android specific API to access the codecs that are available on
the device, including hardware codecs. It is available since API level
16 (JellyBean) and GStreamer can use it via the androidmedia plugin
for audio and video decoding. On Android, attaching the hardware
decoder to the `glimagesink` element can produce a high performance
zero-copy decodebin pipeline.

### `ahcsrc`

This video source can capture from the cameras on Android devices, it is part
of the androidmedia plugin and uses the [android.hardware.Camera API](https://developer.android.com/reference/android/hardware/Camera.html).

## iOS

### `osxaudiosink`

This is the only audio sink available to GStreamer on iOS.

### `iosassetsrc`

Source element to read iOS assets, this is, documents stored in the
Library (like photos, music and videos). It can be instantiated
automatically by `playbin` when URIs use the
`assets-library://` scheme.

### `iosavassetsrc`

Source element to read and decode iOS audiovisual assets, this is,
documents stored in the Library (like photos, music and videos). It can
be instantiated automatically by `playbin` when URIs use the
`ipod-library://` scheme. Decoding is performed by the system, so
dedicated hardware will be used if available.

## Conclusion

This tutorial has shown a few specific details about some GStreamer
elements which are not available on all platforms. You do not have to
worry about them when using multiplatform elements like `playbin` or
`autovideosink`, but it is good to know their personal quirks if
instancing them manually.

It has been a pleasure having you here, and see you soon!
