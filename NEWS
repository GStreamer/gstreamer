# GStreamer 1.12 Release Notes

GStreamer 1.12.0 was originally released on 4th May 2017.
The latest bug-fix release in the 1.12 series is [1.12.3](#1.12.3) and was
released on 18 September 2017.

The GStreamer team is proud to announce a new major feature release in the
stable 1.x API series of your favourite cross-platform multimedia framework!

As always, this release is again packed with new features, bug fixes and other
improvements.

See [https://gstreamer.freedesktop.org/releases/1.12/][latest] for the latest
version of this document.

*Last updated: Monday 19 September 2017, 12:30 UTC [(log)][gitlog]*

[latest]: https://gstreamer.freedesktop.org/releases/1.12/
[gitlog]: https://cgit.freedesktop.org/gstreamer/www/log/src/htdocs/releases/1.12/release-notes-1.12.md

## Introduction

The GStreamer team is proud to announce a new major feature release in the
stable 1.x API series of your favourite cross-platform multimedia framework!

As always, this release is again packed with new features, bug fixes and other
improvements.

## Highlights

- new `msdk` plugin for Intel's Media SDK for hardware-accelerated video
  encoding and decoding on Intel graphics hardware on Windows or Linux.

- `x264enc` can now use multiple x264 library versions compiled for different
  bit depths at runtime, to transparently provide support for multiple bit
  depths.

- `videoscale` and `videoconvert` now support multi-threaded scaling and
  conversion, which is particularly useful with higher resolution video.

- `h264parse` will now automatically insert AU delimiters if needed when
  outputting byte-stream format, which improves standard compliance and
  is needed in particular for HLS playback on iOS/macOS.

- `rtpbin` has acquired bundle support for incoming streams

## Major new features and changes

### Noteworthy new API

- The video library gained support for a number of new video formats:

  - `GBR_12LE`, `GBR_12BE`, `GBRA_12LE`, `GBRA_12BE` (planar 4:4:4 RGB/RGBA, 12 bits per channel)
  - `GBRA_10LE`, `GBRA_10BE` (planar 4:4:4:4 RGBA, 10 bits per channel)
  - `GBRA` (planar 4:4:4:4 ARGB, 8 bits per channel)
  - `I420_12BE`, `I420_12LE` (planar 4:2:0 YUV, 12 bits per channel)
  - `I422_12BE`,`I422_12LE` (planar 4:2:2 YUV, 12 bits per channel)
  - `Y444_12BE`, `Y444_12LE` (planar 4:4:4 YUV, 12 bits per channel)
  - `VYUY` (another packed 4:2:2 YUV format)

- The high-level `GstPlayer` API was extended with functions for taking video
  snapshots and enabling accurate seeking. It can optionally also use the
  still-experimental `playbin3` element now.

### New Elements

- msdk: new plugin for Intel's Media SDK for hardware-accelerated video encoding
  and decoding on Intel graphics hardware on Windows or Linux. This includes
  an H.264 encoder/decoder (`msdkh264dec`, `msdkh264enc`),
  an H.265 encoder/decoder (`msdkh265dec`, `msdkh265enc`),
  an MJPEG encoder/encoder (`msdkmjpegdec`, `msdkmjpegenc`),
  an MPEG-2 video encoder (`msdkmpeg2enc`) and a VP8 encoder (`msdkvp8enc`).

- `iqa` is a new Image Quality Assessment plugin based on [DSSIM][dssim],
  similar to the old (unported) videomeasure element.

- The `faceoverlay` element, which allows you to overlay SVG graphics over
  a detected face in a video stream, has been ported from 0.10.

- our `ffmpeg` wrapper plugin now exposes/maps the ffmpeg Opus audio decoder
  (`avdec_opus`) as well as the GoPro CineForm HD / CFHD decoder (`avdec_cfhd`),
  and also a parser/writer for the IVF format (`avdemux_ivf` and `avmux_ivf`).

- `audiobuffersplit` is a new element that splits raw audio buffers into
  equal-sized buffers

- `audiomixmatrix` is a new element that mixes N:M audio channels according to
  a configured mix matrix.

- The `timecodewait` element got renamed to `avwait` and can operate in
  different modes now.

- The `opencv` video processing plugin has gained a new `dewarp` element that
  dewarps fisheye images.

- `ttml` is a new plugin for parsing and rendering subtitles in Timed Text
  Markup Language (TTML) format. For the time being these elements will not
  be autoplugged during media playback however, unless the `GST_TTML_AUTOPLUG=1`
  environment variable is set. Only the EBU-TT-D profile is supported at this
  point.

[dssim]: https://github.com/pornel/dssim

### New element features and additions

- `x264enc` can now use multiple x264 library versions compiled for different
  bit depths at runtime, to transparently provide support for multiple bit
  depths. A new configure parameter `--with-x264-libraries` has been added to
  specify additional paths to look for additional x264 libraries to load.
  Background is that the libx264 library is always compile for one specific
  bit depth and the `x264enc` element would simply support the depth supported
  by the underlying library. Now we can support multiple depths.

- `x264enc` also picks up the interlacing mode automatically from the input
  caps now and passed interlacing/TFF information correctly to the library.

- `videoscale` and `videoconvert` now support multi-threaded scaling and
  conversion, which is particularly useful with higher resolution video.
  This has to be enabled explicitly via the `"n-threads"` property.

- `videorate`'s new `"rate"` property lets you set a speed factor
  on the output stream

- `splitmuxsink`'s buffer collection and scheduling was rewritten to make
  processing and splitting deterministic; before it was possible for a buffer
  to end up in a different file chunk in different runs. `splitmuxsink` also
  gained a new `"format-location-full"` signal that works just like the existing
  `"format-location"` signal only that it is also passed the primary stream's
  first buffer as argument, so that it is possible to construct the file name
  based on metadata such as the buffer timestamp or any GstMeta attached to
  the buffer. The new `"max-size-timecode"` property allows for timecode-based
  splitting. `splitmuxsink` will now also automatically start a new file if the
  input caps change in an incompatible way.

- `fakesink` has a new `"drop-out-of-segment"` property to not drop
  out-of-segment buffers, which is useful for debugging purposes.

- `identity` gained a `"ts-offset"` property.

- both `fakesink` and `identity` now also print what kind of metas are attached
  to buffers when printing buffer details via the `"last-message"` property
  used by `gst-launch-1.0 -v`.

- multiqueue: made `"min-interleave-time"` a configurable property.

- video nerds will be thrilled to know that `videotestsrc`'s snow is now
  deterministic. `videotestsrc` also gained some new properties to make the
  ball pattern based on system time, and invert colours each second
  (`"animation-mode"`, `"motion"`, and `"flip"` properties).

- `oggdemux` reverse playback should work again now. You're welcome.

- `playbin3` and `urisourcebin` now have buffering enabled by default, and
  buffering message aggregation was fixed.

- `tcpclientsrc` now has a `"timeout"` property

- `appsink` has gained support for buffer lists. For backwards compatibility
  reasons users need to enable this explicitly with `gst_app_sink_set_buffer_list_support()`,
  however. Once activated, a pulled `GstSample` can contain either a buffer
  list or a single buffer.

- `splitmuxsrc` reverse playback was fixed and handling of sparse streams, such
  as subtitle tracks or metadata tracks, was improved.

- `matroskamux` has acquired support for muxing G722 audio; it also marks all
  buffers as keyframes now when streaming only audio, so that `tcpserversink`
  will behave properly with audio-only streams.

- `qtmux` gained support for ProRes 4444 XQ, HEVC/H.265 and CineForm (GoPro) formats,
  and generally writes more video stream-related metadata into the track headers.
  It is also allows configuration of the maximum interleave size in bytes and
  time now. For fragmented mp4 we always write the `tfdt` atom now as required
  by the DASH spec.

- `qtdemux` supports FLAC, xvid, mp2, S16L and CineForm (GoPro) tracks now, and
  generally tries harder to extract more video-related information from track
  headers, such as colorimetry or interlacing details. It also received a
  couple of fixes for the scenario where upstream operates in TIME format and
  feeds chunks to qtdemux (e.g. DASH or MSE).

- `audioecho` has two new properties to apply a delay only to certain channels
  to create a surround effect, rather than an echo on all channels. This is
  useful when upmixing from stereo, for example. The `"surround-delay"` property
  enables this, and the `"surround-mask"` property controls which channels
  are considered surround sound channels in this case.

- `webrtcdsp` gained various new properties for gain control and also exposes
  voice activity detection now, in which case it will post `"voice-activity"`
  messages on the bus whenever the voice detection status changes.

- The `decklink` capture elements for Blackmagic Decklink cards have seen a
  number of improvements:

  - `decklinkvideosrc` will post a warning message on "no signal" and an info
    message when the signal lock has been (re)acquired. There is also a new
    read-only `"signal"` property that can be used to query the signal lock
    status. The `GAP` flag will be set on buffers that are captured without
    a signal lock. The new `drop-no-signal-frames` will make `decklinkvideosrc`
    drop all buffers that have been captured without an input signal. The
    `"skip-first-time"` property will make the source drop the first few
    buffers, which is handy since some devices will at first output buffers
    with the wrong resolution before they manage to figure out the right input
    format and decide on the actual output caps.

  - `decklinkaudiosrc` supports more than just 2 audio channels now.

  - The capture sources no longer use the "hardware" timestamps which turn
    out to be useless and instead just use the pipeline clock directly.

- `srtpdec` now also has a readonly `"stats"` property, just like `srtpenc`.

- `rtpbin` gained RTP bundle support, as used by e.g. WebRTC. The first
   rtpsession will have a `rtpssrcdemux` element inside splitting the streams
   based on their SSRC and potentially dispatch to a different rtpsession.
   Because retransmission SSRCs need to be merged with the corresponding media
   stream the `::on-bundled-ssrc` signal is emitted on `rtpbin` so that the
   application can find out to which session the SSRC belongs.

- `rtprtxqueue` gained two new properties exposing retransmission
  statistics (`"requests"` and `"fulfilled-requests"`)

- `kmssink` will now use the preferred mode for the monitor and render to the
  base plane if nothing else has set a mode yet. This can also be done forcibly
  in any case via the new `"force-modesetting"` property. Furthermore, `kmssink`
  now allows only the supported connector resolutions as input caps in order to
  avoid scaling or positioning of the input stream, as `kmssink` can't know
  whether scaling or positioning would be more appropriate for the use case at
  hand.

- `waylandsink` can now take DMAbuf buffers as input in the presence
  of a compatible Wayland compositor. This enables zero-copy transfer
  from a decoder or source that outputs DMAbuf. It will also set surface
  opacity hint to allow better rendering optimization in the compositor.

- `udpsrc` can be bound to more than one interface when joining a
  multicast group, this is done by giving a comma separate list of
  interfaces such as multicast-iface="eth0,eth1".

### Plugin moves

- `dataurisrc` moved from gst-plugins-bad to core

- The `rawparse` plugin containing the `rawaudioparse` and `rawvideoparse`
  elements moved from gst-plugins-bad to gst-plugins-base. These elements
  supersede the old `videoparse` and `audioparse` elements. They work the
  same, with just some minor API changes. The old legacy elements still
  exist in gst-plugins-bad, but may be removed at some point in the future.

- `timecodestamper` is an element that attaches time codes to video buffers
  in form of `GstVideoTimeCodeMeta`s. It had a `"clock-source"` property
  which has now been removed because it was fairly useless in practice. It
  gained some new properties however: the `"first-timecode"` property can
  be used to set the inital timecode; alternatively `"first-timecode-to-now"`
  can be set, and then the current system time at the time the first buffer
  arrives is used as base time for the time codes.


### Plugin removals

- The `mad` mp1/mp2/mp3 decoder plugin was removed from gst-plugins-ugly,
  as libmad is GPL licensed, has been unmaintained for a very long time, and
  there are better alternatives available. Use the `mpg123audiodec` element
  from the `mpg123` plugin in gst-plugins-ugly instead, or `avdec_mp3` from
  the `gst-libav` module which wraps the ffmpeg library. We expect that we
  will be able to move mp3 decoding to gst-plugins-good in the next cycle
  seeing that most patents around mp3 have expired recently or are about to
  expire.

- The `mimic` plugin was removed from gst-plugins-bad. It contained a decoder
  and encoder for a video codec used by MSN messenger many many years ago (in
  a galaxy far far away). The underlying library is unmaintained and no one
  really needs to use this codec any more. Recorded videos can still be played
  back with the MIMIC decoder in gst-libav.

## Miscellaneous API additions

- Request pad name templates passed to `gst_element_request_pad()` may now
  contain multiple specifiers, such as e.g. `src_%u_%u`.

- [`gst_buffer_iterate_meta_filtered()`][buffer-iterate-meta-filtered] is a
  variant of `gst_buffer_iterate_meta()` that only returns metas of the
  requested type and skips all other metas.

- [`gst_pad_task_get_state()`][pad-task-get-state] gets the current state of
  a task in a thread-safe way.

- [`gst_uri_get_media_fragment_table()`][uri-get-fragment-table] provides the
  media fragments of an URI as a table of key=value pairs.

- [`gst_print()`][print], [`gst_println()`][println], [`gst_printerr()`][printerr],
  and [`gst_printerrln()`][printerrln] can be used to print to stdout or stderr.
  These functions are similar to `g_print()` and `g_printerr()` but they also
  support all the additional format specifiers provided by the GStreamer
  logging system, such as e.g. `GST_PTR_FORMAT`.

- a `GstParamSpecArray` has been added, for elements who want to have array
  type properties, such as the `audiomixmatrix` element for example. There are
  also two new functions to set and get properties of this type from bindings:
   - gst_util_set_object_array()
   - gst_util_get_object_array()

- various helper functions have been added to make it easier to set or get
  GstStructure fields containing caps-style array or list fields from language
  bindings (which usually support GValueArray but don't know about the GStreamer
  specific fundamental types):
   - [`gst_structure_get_array()`][get-array]
   - [`gst_structure_set_array()`][set-array]
   - [`gst_structure_get_list()`][get-list]
   - [`gst_structure_set_list()`][set-list]

- a new ['dynamic type' registry factory type][dynamic-type] was added to
  register dynamically loadable GType types. This is useful for automatically
  loading enum/flags types that are used in caps, such as for example the
  `GstVideoMultiviewFlagsSet` type used in multiview video caps.

- there is a new [`GstProxyControlBinding`][proxy-control-binding] for use
  with GstController. This allows proxying the control interface from one
  property on one GstObject to another property (of the same type) in another
  GstObject. So e.g. in parent-child relationship, one may need to call
  `gst_object_sync_values()` on the child and have a binding (set elsewhere)
  on the parent update the value. This is used in `glvideomixer` and `glsinkbin`
  for example, where `sync_values()` on the child pad or element will call
  `sync_values()` on the exposed bin pad or element.

  Note that this doesn't solve GObject property forwarding, that must
  be taken care of by the implementation manually or using GBinding.

- `gst_base_parse_drain()` has been made public for subclasses to use.

- `gst_base_sink_set_drop_out_of_segment()' can be used by subclasses to
  prevent GstBaseSink from dropping buffers that fall outside of the segment.

- [`gst_calculate_linear_regression()`][calc-lin-regression] is a new utility
  function to calculate a linear regression.

- [`gst_debug_get_stack_trace`][get-stack-trace] is an easy way to retrieve a
  stack trace, which can be useful in tracer plugins.

- allocators: the dmabuf allocator is now sub-classable, and there is a new
  `GST_CAPS_FEATURE_MEMORY_DMABUF` define.

- video decoder subclasses can use the newly-added function
  `gst_video_decoder_allocate_output_frame_with_params()` to
  pass a `GstBufferPoolAcquireParams` to the buffer pool for
  each buffer allocation.

- the video time code API has gained a dedicated [`GstVideoTimeCodeInterval`][timecode-interval]
  type plus related API, including functions to add intervals to timecodes.

- There is a new `libgstbadallocators-1.0` library in gst-plugins-bad, which
  may go away again in future releases once the `GstPhysMemoryAllocator`
  interface API has been validated by more users and was moved to
  `libgstallocators-1.0` from gst-plugins-base.

[timecode-interval]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-libs/html/gst-plugins-base-libs-gstvideo.html#gst-video-time-code-interval-new
[buffer-iterate-meta-filtered]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstBuffer.html#gst-buffer-iterate-meta-filtered
[pad-task-get-state]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstPad.html#gst-pad-task-get-state
[uri-get-fragment-table]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstUri.html#gst-uri-get-media-fragment-table
[print]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstInfo.html#gst-print
[println]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstInfo.html#gst-println
[printerr]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstInfo.html#gst-printerr
[printerrln]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstInfo.html#gst-printerrln
[get-array]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstStructure.html#gst-structure-get-array
[set-array]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstStructure.html#gst-structure-set-array
[get-list]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstStructure.html#gst-structure-get-list
[set-list]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstStructure.html#gst-structure-set-list
[dynamic-type]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/GstDynamicTypeFactory.html
[proxy-control-binding]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer-libs/html/gstreamer-libs-GstProxyControlBinding.html
[calc-lin-regression]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstUtils.html#gst-calculate-linear-regression
[get-stack-trace]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/gstreamer-GstUtils.html#gst-debug-get-stack-trace

### GstPlayer

New API has been added to:

 - get the number of audio/video/subtitle streams:
   - `gst_player_media_info_get_number_of_streams()`
   - `gst_player_media_info_get_number_of_video_streams()`
   - `gst_player_media_info_get_number_of_audio_streams()`
   - `gst_player_media_info_get_number_of_subtitle_streams()`

 - enable accurate seeking: `gst_player_config_set_seek_accurate()`
   and `gst_player_config_get_seek_accurate()`

 - get a snapshot image of the video in RGBx, BGRx, JPEG, PNG or
   native format: [`gst_player_get_video_snapshot()`][snapshot]

 - selecting use of a specific video sink element
   ([`gst_player_video_overlay_video_renderer_new_with_sink()`][renderer-with-vsink])

 - If the environment variable `GST_PLAYER_USE_PLAYBIN3` is set, GstPlayer will
   use the still-experimental `playbin3` element and the `GstStreams` API for
   playback.

[snapshot]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-libs/html/gst-plugins-bad-libs-gstplayer.html#gst-player-get-video-snapshot
[renderer-with-vsink]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad-libs/html/gst-plugins-bad-libs-gstplayer-videooverlayvideorenderer.html#gst-player-video-overlay-video-renderer-new-with-sink

## Miscellaneous changes

- video caps for interlaced video may contain an optional `"field-order"` field
  now in the case of `interlaced-mode=interleaved` to signal that the field
  order is always the same throughout the stream. This is useful to signal to
  muxers such as mp4mux. The new field is parsed from/to `GstVideoInfo` of course.

- video decoder and video encoder base classes try harder to proxy
  interlacing, colorimetry and chroma-site related fields in caps properly.

- The buffer stored in the `PROTECTION` events is now left unchanged. This is a
  change of behaviour since 1.8, especially for the mssdemux element which used to
  decode the base64 parsed data wrapped in the protection events emitted by the
  demuxer.

- `PROTECTION` events can now be injected into the pipeline from the application;
  source elements deriving from GstBaseSrc will forward those downstream now.

- The DASH demuxer is now correctly parsing the MSPR-2.0 ContentProtection nodes
  and emits Protection events accordingly. Applications relying on those events
  might need to decode the base64 data stored in the event buffer before using it.

- The registry can now also be disabled by setting the environment variable
  `GST_REGISTRY_DISABLE=yes`, with similar effect as the `GST_DISABLE_REGISTRY`
  compile time switch.

- Seeking performance with gstreamer-vaapi based decoders was improved. It would
  recreate the decoder and surfaces on every seek which can be quite slow.

- more robust handling of input caps changes in videoaggregator-based elements
  such as `compositor`.

- Lots of adaptive streaming-related fixes across the board (DASH, MSS, HLS). Also:

  - `mssdemux`, the Microsoft Smooth Streaming demuxer, has seen various
    fixes for live streams, duration reporting and seeking.

  - The DASH manifest parser now extracts MS PlayReady ContentProtection objects
    from manifests and sends them downstream as `PROTECTION` events. It also
    supports multiple Period elements in external xml now.

- gst-libav was updated to ffmpeg 3.3 but should still work with any 3.x
  version.

- GstEncodingProfile has been generally enhanced so it can, for
  example, be used to get possible profiles for a given file
  extension. It is now possible to define profiles based on element
  factory names or using a path to a `.gep` file containing a
  serialized profile.

- `audioconvert` can now do endianness conversion in-place. All other
  conversions still require a copy, but e.g. sign conversion and a few others
  could also be implemented in-place now.

- The new, experimental `playbin3` and `urisourcebin` elements got many
  bugfixes and improvements and should generally be closer to a full
  replacement of the old elements.

- `interleave` now supports > 64 channels.

- OpenCV elements, `grabcut` and `retinex` has been ported to use
  `GstOpencvVideoFilter` base class, increasing code reuse and fixing buffer
  map/unmap issues. Redundant copie of images has been removed in `edgedetect`,
  `cvlaplace` and `cvsobel`. This comes with various cleanup and Meson support.

### OpenGL integration

- As usual the GStreamer OpenGL integration library has seen numerous
  fixes and performance improvements all over the place, and is hopefully
  ready now to become API stable and be moved to gst-plugins-base during the
  1.14 release cycle.

- The GStreamer OpenGL integration layer has also gained support for the
  Vivante EGL FB windowing system, which improves performance on platforms
  such as Freescale iMX.6 for those who are stuck with the proprietary driver.
  The `qmlglsink` element also supports this now if Qt is used with eglfs or
  wayland backend, and it works in conjunction with [gstreamer-imx][gstreamer-imx]
  of course.

- various `qmlglsrc` improvements

[gstreamer-imx]: https://github.com/Freescale/gstreamer-imx

## Tracing framework and debugging improvements

- New tracing hooks have been added to track GstMiniObject and GstObject
  ref/unref operations.

- The memory leaks tracer can optionally use this to retrieve stack traces if
  enabled with e.g. `GST_TRACERS=leaks(filters="GstEvent,GstMessage",stack-traces-flags=full)`

- The `GST_DEBUG_FILE` environment variable, which can be used to write the
  debug log output to a file instead of printing it to stderr, can now contain
  a name pattern, which is useful for automated testing and continuous
  integration systems. The following format specifiers are supported:

   - `%p`: will be replaced with the PID
   - `%r`: will be replaced with a random number, which is useful for instance
     when running two processes with the same PID but in different containers.

## Tools

- `gst-inspect-1.0` can now list elements by type with the new `--types`
   command-line option, e.g. `gst-inspect-1.0 --types=Audio/Encoder` will
   show a list of audio encoders.

- `gst-launch-1.0` and `gst_parse_launch()` have gained a new operator (`:`)
   that allows linking all pads between two elements. This is useful in cases
   where the exact number of pads or type of pads is not known beforehand, such
   as in the `uridecodebin : encodebin` scenario, for example. In this case,
   multiple links will be created if the encodebin has multiple profiles
   compatible with the output of uridecodebin.

- `gst-device-monitor-1.0` now shows a `gst-launch-1.0` snippet for each
  device that shows how to make use of it in a `gst-launch-1.0` pipeline string.

## GStreamer RTSP server

- The RTSP server now also supports Digest authentication in addition to Basic
  authentication.

- The `GstRTSPClient` class has gained a `pre-*-request` signal and virtual
  method for each client request type, emitted in the beginning of each rtsp
  request. These signals or virtual methods let the application validate the
  requests, configure the media/stream in a certain way and also generate error
  status codes in case of an error or a bad request.

## GStreamer VAAPI

- GstVaapiDisplay now inherits from GstObject, thus the VA display logging
  messages are better and tracing the context sharing is more readable.

- When uploading raw images into a VA surfaces now VADeriveImages are tried
  fist, improving the upload performance, if it is possible.

- The decoders and the post-processor now can push dmabuf-based buffers to
  downstream under certain conditions. For example:

  `GST_GL_PLATFORM=egl gst-play-1.0 video-sample.mkv --videosink=glimagesink`

- Refactored the wrapping of VA surface into gstreamer memory, adding lock
  when mapping and unmapping, and many other fixes.

- Now `vaapidecodebin` loads `vaapipostproc` dynamically. It is possible to
  avoid it usage with the environment variable `GST_VAAPI_DISABLE_VPP=1`.

- Regarding encoders: they have primary rank again, since they can discover,
  in run-time, the color formats they can use for upstream raw buffers and
  caps renegotiation is now possible. Also the encoders push encoding info
  downstream via tags.

- About specific encoders: added constant bit-rate encoding mode for VP8 and
  H265 encoder handles P010_10LE color format.

- Regarding decoders, flush operation has been improved, now the internal VA
  encoder is not recreated at each flush. Also there are several improvements
  in the handling of H264 and H265 streams.

- VAAPI plugins try to create their on GstGL context (when available) if they
  cannot find it in the pipeline, to figure out what type of VA Display they
  should create.

- Regarding `vaapisink` for X11, if the backend reports that it is unable to
  render correctly the current color format, an internal VA post-processor, is
  instantiated (if available) and converts the color format.

## GStreamer Editing Services and NLE

- Enhanced auto transition behaviour

- Fix some races in `nlecomposition`

- Allow building with msvc

- Added a UNIX manpage for `ges-launch`

- API changes:
  - Added ges_deinit (allowing the leak tracer to work properly)
  - Added ges_layer_get_clips_in_interval
  - Finally hide internal symbols that should never have been exposed

## GStreamer validate

- Port `gst-validate-launcher` to python 3

- `gst-validate-launcher` now checks if blacklisted bugs have been fixed on
  bugzilla and errors out if it is the case

- Allow building with msvc

- Add ability for the launcher to run GStreamer unit tests

- Added a way to activate the leaks tracer on our tests and fix leaks

- Make the http server multithreaded

- New testsuite for running various test scenarios on the DASH-IF test vectors

## GStreamer Python Bindings

- Overrides has been added for IntRange, Int64Range, DoubleRange,
  FractionRange, Array and List. This finally enables Python programmers
  to fully read and write GstCaps objects.

## Build and Dependencies

- Meson build files are now disted in tarballs, for jhbuild and so distro
  packagers can start using it. Note that the Meson-based build system is not
  100% feature-equivalent with the autotools-based one yet.

- Some plugin filenames have been changed to match the plugin names: for example
  the file name of the `encoding` plugin in gst-plugins-base containing the
  `encodebin` element was `libgstencodebin.so` and has been changed to
  `libgstencoding.so`. This affects only a handful of plugins across modules.

  **Developers who install GStreamer from source and just do `make install`**
  **after updating the source code, without doing `make uninstall` first, will**
  **have to manually remove the old installed plugin files from the installation**
  **prefix, or they will get 'Cannot register existing type' critical warnings.**

- Most of the docbook-based documentation (FAQ, Application Development Manual,
  Plugin Writer's Guide, design documents) has been converted to markdown and
  moved into a new gst-docs module. The gtk-doc library API references and
  the plugins documentation are still built as part of the source modules though.

- GStreamer core now optionally uses libunwind and libdw to generate backtraces.
  This is useful for tracer plugins used during debugging and development.

- There is a new `libgstbadallocators-1.0` library in gst-plugins-bad (which
  may go away again in future releases once the `GstPhysMemoryAllocator`
  interface API has been validated by more users).

- `gst-omx` and `gstreamer-vaapi` modules can now also be built using the
  Meson build system.

- The `qtkitvideosrc` element for macOS was removed. The API is deprecated
  since 10.9 and it wasn't shipped in the binaries since a few releases.

## Platform-specific improvements

### Android

- androidmedia: add support for VP9 video decoding/encoding and Opus audio
  decoding (where supported)

### OS/X and iOS

- `avfvideosrc`, which represents an iPhone camera or, on a Mac, a screencapture
  session, so far allowed you to select an input device by device index only.
  New API adds the ability to select the position (front or back facing) and
  device-type (wide angle, telephoto, etc.). Furthermore, you can now also
  specify the orientation (portrait, landscape, etc.) of the videostream.

### Windows

- `dx9screencapsrc` can now optionally also capture the cursor.

## Contributors

Aleix Conchillo Flaque, Alejandro G. Castro, Aleksandr Slobodeniuk, Alexandru
Băluț, Alex Ashley, Andre McCurdy, Andrew, Anton Eliasson, Antonio Ospite,
Arnaud Vrac, Arun Raghavan, Aurélien Zanelli, Axel Menzel, Benjamin Otte,
Branko Subasic, Brendan Shanks, Carl Karsten, Carlos Rafael Giani, ChangBok
Chae, Chris Bass, Christian Schaller, christophecvr, Claudio Saavedra,
Corentin Noël, Dag Gullberg, Daniel Garbanzo, Daniel Shahaf, David Evans,
David Schleef, David Warman, Dominique Leuenberger, Dongil Park, Douglas
Bagnall, Edgard Lima, Edward Hervey, Emeric Grange, Enrico Jorns, Enrique
Ocaña González, Evan Nemerson, Fabian Orccon, Fabien Dessenne, Fabrice Bellet,
Florent Thiéry, Florian Zwoch, Francisco Velazquez, Frédéric Dalleau, Garima
Gaur, Gaurav Gupta, George Kiagiadakis, Georg Lippitsch, Göran Jönsson, Graham
Leggett, Guillaume Desmottes, Gurkirpal Singh, Haihua Hu, Hanno Boeck, Havard
Graff, Heekyoung Seo, hoonhee.lee, Hyunjun Ko, Imre Eörs, Iñaki García
Etxebarria, Jagadish, Jagyum Koo, Jan Alexander Steffens (heftig), Jan
Schmidt, Jean-Christophe Trotin, Jochen Henneberg, Jonas Holmberg, Joris
Valette, Josep Torra, Juan Pablo Ugarte, Julien Isorce, Jürgen Sachs, Koop
Mast, Kseniia Vasilchuk, Lars Wendler, leigh123linux@googlemail.com, Luis de
Bethencourt, Lyon Wang, Marcin Kolny, Marinus Schraal, Mark Nauwelaerts,
Mathieu Duponchelle, Matthew Waters, Matt Staples, Michael Dutka, Michael
Olbrich, Michael Smith, Michael Tretter, Miguel París Díaz, namanyadav12, Neha
Arora, Nick Kallen, Nicola Murino, Nicolas Dechesne, Nicolas Dufresne, Nicolas
Huet, Nirbheek Chauhan, Ole André Vadla Ravnås, Olivier Crête, Patricia
Muscalu, Peter Korsgaard, Peter Seiderer, Petr Kulhavy, Philippe Normand,
Philippe Renon, Philipp Zabel, Rahul Bedarkar, Reynaldo H. Verdejo Pinochet,
Ricardo Ribalda Delgado, Rico Tzschichholz, Руслан Ижбулатов, Samuel Maroy,
Santiago Carot-Nemesio, Scott D Phillips, Sean DuBois, Sebastian Dröge, Sergey
Borovkov, Seungha Yang, shakin chou, Song Bing, Søren Juul, Sreerenj
Balachandran, Stefan Kost, Stefan Sauer, Stepan Salenikovich, Stian Selnes,
Stuart Weaver, suhas2go, Thiago Santos, Thibault Saunier, Thomas Bluemel,
Thomas Petazzoni, Tim-Philipp Müller, Ting-Wei Lan, Tobias Mueller, Todor
Tomov, Tomasz Zajac, Ulf Olsson, Ursula Maplehurst, Víctor Manuel Jáquez Leal,
Victor Toso, Vincent Penquerc'h, Vineeth TM, Vinod Kesti, Vitor Massaru Iha,
Vivia Nikolaidou, WeiChungChang, William Manley, Wim Taymans, Wojciech
Przybyl, Wonchul Lee, Xavier Claessens, Yasushi SHOJI

... and many others who have contributed bug reports, translations, sent
suggestions or helped testing.

## Bugs fixed in 1.12

More than [635 bugs][bugs-fixed-in-1.12] have been fixed during
the development of 1.12.

This list does not include issues that have been cherry-picked into the
stable 1.10 branch and fixed there as well, all fixes that ended up in the
1.10 branch are also included in 1.12.

This list also does not include issues that have been fixed without a bug
report in bugzilla, so the actual number of fixes is much higher.

[bugs-fixed-in-1.12]: https://bugzilla.gnome.org/buglist.cgi?bug_status=RESOLVED&bug_status=VERIFIED&classification=Platform&limit=0&list_id=213265&order=bug_id&product=GStreamer&query_format=advanced&resolution=FIXED&target_milestone=1.10.1&target_milestone=1.10.2&target_milestone=1.10.3&target_milestone=1.10.4&target_milestone=1.11.1&target_milestone=1.11.2&target_milestone=1.11.3&target_milestone=1.11.4&target_milestone=1.11.90&target_milestone=1.11.91&target_milestone=1.12.0

## Stable 1.12 branch

After the 1.12.0 release there will be several 1.12.x bug-fix releases which
will contain bug fixes which have been deemed suitable for a stable branch,
but no new features or intrusive changes will be added to a bug-fix release
usually. The 1.12.x bug-fix releases will be made from the git 1.12 branch, which
is a stable branch.

### 1.12.0

1.12.0 was released on 4th May 2017.

<a name="1.12.1"></a>

### 1.12.1

The first 1.12 bug-fix release (1.12.1) was released on 20 June 2017.
This release only contains bugfixes and it should be safe to update from 1.12.x.

#### Major bugfixes in 1.12.1

 - Various fixes for crashes, assertions, deadlocks and memory leaks
 - Fix for regression when seeking to the end of ASF files
 - Fix for regression in (raw)videoparse that caused it to omit video metadata
 - Fix for regression in discoverer that made it show more streams than
   actually available
 - Numerous bugfixes to the adaptive demuxer base class and the DASH demuxer
 - Various playbin3/urisourcebin related bugfixes
 - Vivante DirectVIV (imx6) texture uploader works with single-plane (e.g.
   RGB) video formats now
 - Intel Media SDK encoder now outputs valid PTS and keyframe flags
 - OpenJPEG2000 plugin can be loaded again on MacOS and correctly displays
   8 bit RGB images now
 - Fixes to DirectSound source/sink for high CPU usage and wrong
   latency/buffer size calculations
 - gst-libav was updated to ffmpeg n3.3.2
 - ... and many, many more!

For a full list of bugfixes see [Bugzilla][buglist-1.12.1]. Note that this is
not the full list of changes. For the full list of changes please refer to the
GIT logs or ChangeLogs of the particular modules.

[buglist-1.12.1]: https://bugzilla.gnome.org/buglist.cgi?bug_status=RESOLVED&bug_status=VERIFIED&classification=Platform&limit=0&list_id=225693&order=bug_id&product=GStreamer&query_format=advanced&resolution=FIXED&target_milestone=1.12.1

<a name="1.12.2"></a>

### 1.12.2

The second 1.12 bug-fix release (1.12.2) was released on 14 July 2017.
This release only contains bugfixes and it should be safe to update from 1.12.x.

#### Major bugfixes in 1.12.2

 - Various fixes for crashes, assertions, deadlocks and memory leaks
 - Regression fix for playback of live HLS streams
 - Regression fix for crash when playing back a tunneled RTSP stream
 - Regression fix for playback of RLE animations in MOV containers
 - Regression fix for RTP GSM payloading producing corrupted output
 - Major bugfixes to the MXF demuxer, mostly related to seeking and
   fixes to the frame reordering handling in the MXF muxer and demuxer
 - Fix for playback of mono streams on MacOS
 - More fixes for index handling of ASF containers
 - Various fixes to adaptivedemux, DASH and HLS demuxers
 - Fix deadlock in gstreamer-editing-services during class initialization
 - ... and many, many more!

For a full list of bugfixes see [Bugzilla][buglist-1.12.2]. Note that this is
not the full list of changes. For the full list of changes please refer to the
GIT logs or ChangeLogs of the particular modules.

[buglist-1.12.2]: https://bugzilla.gnome.org/buglist.cgi?bug_status=RESOLVED&bug_status=VERIFIED&classification=Platform&limit=0&list_id=225693&order=bug_id&product=GStreamer&query_format=advanced&resolution=FIXED&target_milestone=1.12.2

<a name="1.12.3"></a>

### 1.12.3

The second 1.12 bug-fix release (1.12.3) was released on 14 July 2017.
This release only contains bugfixes and it should be safe to update from 1.12.x.

#### Major bugfixes in 1.12.3

 - Fix for infinite recursion on buffer free in v4l2
 - Fix for glimagesink crash on macOS when used via autovideosink
 - Fix for huge overhead in matroskamux caused by writing one Cluster per
   audio-frame in audio-only streams. Also use SimpleBlocks for Opus and other
   audio codecs, which works around a bug in VLC that prevented Opus streams
   to be played and decreases overhead even more
 - Fix for flushing seeks in rtpmsrc always causing an error
 - Fix for timestamp overflows in calculations in audio encoder base class
 - Fix for RTP h265 depayloader marking P-frames as I-frames
 - Fix for long connection delays of clients in RTSP server
 - Fixes for event handling in queue and queue2 elements, and updates to
   buffering levels on NOT_LINKED streams
 - Various fixes to event and buffering handling in decodebin3/playbin3
 - Various fixes for memory leaks, deadlocks and crashes in all modules
 - ... and many, many more!

For a full list of bugfixes see [Bugzilla][buglist-1.12.3]. Note that this is
not the full list of changes. For the full list of changes please refer to the
GIT logs or ChangeLogs of the particular modules.

[buglist-1.12.3]: https://bugzilla.gnome.org/buglist.cgi?bug_status=RESOLVED&bug_status=VERIFIED&classification=Platform&limit=0&list_id=248880&order=bug_id&product=GStreamer&query_format=advanced&resolution=FIXED&target_milestone=1.12.3

## Known Issues

- The `webrtcdsp` element is currently not shipped as part of the Windows
  binary packages due to a [build system issue][bug-770264].

[bug-770264]: https://bugzilla.gnome.org/show_bug.cgi?id=770264

## Schedule for 1.14

Our next major feature release will be 1.14, and 1.13 will be the unstable
development version leading up to the stable 1.14 release. The development
of 1.13/1.14 will happen in the git master branch.

The plan for the 1.14 development cycle is yet to be confirmed, but it is
expected that feature freeze will be around October 2017
followed by several 1.13 pre-releases and the new 1.14 stable release
in October.

1.14 will be backwards-compatible to the stable 1.12, 1.10, 1.8, 1.6, 1.4,
1.2 and 1.0 release series.

- - -

*These release notes have been prepared by Olivier Crête, Sebastian Dröge,
Nicolas Dufresne, Víctor Manuel Jáquez Leal, Tim-Philipp Müller, Philippe
Normand and Thibault Saunier.*

*License: [CC BY-SA 4.0](http://creativecommons.org/licenses/by-sa/4.0/)*
