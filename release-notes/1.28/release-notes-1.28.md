# GStreamer 1.28 Release Notes

GStreamer 1.28.0 was originally released on 27 January 2026.

The latest bug-fix release in the stable 1.28 series is [1.28.1](#1.28.1) and was released on 26 February 2026.

See [https://gstreamer.freedesktop.org/releases/1.28/][latest] for the latest version of this document.

*Last updated: Thursday 26 February 2026, 01:00 UTC [(log)][gitlog]*

[latest]: https://gstreamer.freedesktop.org/releases/1.28/
[gitlog]: https://gitlab.freedesktop.org/gstreamer/www/commits/main/src/htdocs/releases/1.28/release-notes-1.28.md

<a id="introduction"></a>
## Introduction

The GStreamer team is proud to announce a new major feature release in the
stable 1.x API series of your favourite cross-platform multimedia framework!

As always, this release is again packed with many new features, bug fixes, and other improvements.

<a id="highlights"></a>
## Highlights

- AMD HIP plugin and integration helper library
- Vulkan Video AV1 and VP9 decoding, H.264 encoding, and 10-bit support for H.265 decoder
- waylandsink: Parse and set the HDR10 metadata and other color management improvements
- Audio source separation element based on demucs in Rust
- Analytics combiner and splitter elements plus batch meta to batch buffers from one or more streams
- LiteRT inference element; move modelinfo to analytics lib; add script to help with modelinfo generation and upgrade
- Add general classifier tensor-decoder, facedetector, and more analytics convenience API
- New tensordecodebin element to auto-plug compatible tensor decoders based on their caps and many other additions and improvements
- Add a burn-based YOLOX inference element and a YOLOX tensor decoder in Rust
- applemedia: VideoToolbox VP9 and AV1 hardware-accelerated decoding support, and 10-bit HEVC encoding
- Add new GIF decoder element in Rust with looping support
- input-selector: implements a two-phase sinkpad switch now to avoid races when switching input pads
- The inter wormhole sink and source elements gained a way to forward upstream events to the producer as well as new fine-tuning properties
- webrtcsink: add renegotiation support and support for va hardware encoders
- webrtc WHEP client and server signaller
- New ST-2038 ancillary data combiner and extractor elements
- fallbacksrc gained support for encoded streams
- flv: enhanced rtmp H.265 video support, and support for multitrack audio
- glupload: Implement udmabuf uploader to share buffers between software decoders/sources and GPUs, display engines (wayland), and other dma devices
- video: Add crop, scale, rotate, flip, shear and more GstMeta transformation
- New task pool GstContext to share a thread pool amongst elements for better resource management and performance, especially for video conversion and compositing
- New Deepgram speech-to-text transcription plugin and many other translation and transcription improvements
- Speech synthesizers: expose new "compress" overflow mode that can speed up audio while preserving pitch
- ElevenLabs voice cloning element and support for Speechmatics speaker identification API
- textaccumulate: new element for speech synthesis or translation preprocessing
- New vmaf element to calculate perceptual video quality assessment scores using Netflix's VMAF framework
- decodebin3: expose KLV, ID3 PES and ST-2038 ancillary data streams with new metadata GstStream type
- New MPEG-H audio decoding plugin plus MP4 demuxing support
- LCEVC: Add autoplugging decoding support for LCEVC H265 and H266 video streams and LCEVC H.265 and H.266 encoders
- RTP "robust MPEG audio", raw audio (L8, L16, L24), and SMPTE ST291 ancillary metadata payloaders/depayloaders in Rust
- Add a Rust-based icecastsink element with AAC support
- The Windows IPC plugin gained support for passing generic data in addition to raw audio/video, and various properties
- New D3D12 interlace and overlay compositor elements, plus many other D3D12 improvements
- Blackmagic Decklink elements gained support for capturing and outputting all types of VANC via GstAncillaryMeta
- GstLogContext API to reduce log spam in several components and `GST_DEBUG_ONCE` (etc) convenience macros to log things only once
- hlssink3, hlscmafsink: Support the use of a single media file, plus I-frame only playlist support
- Webkit: New wpe2 plugin making use of the "WPE Platform API"
- MPEG-TS demuxer can now disable skew corrections
- New Qt6 QML render source element
- qml6gloverlay: support directly passing a QQuickItem for QML the render tree
- unifxfdsink: Add a property to allow copying to make sink usable with more upstream elements
- dots-viewer: Improve dot file generation and interactivity
- Python bindings: more syntactic sugar, analytics API improvements and type annotations
- cerbero: add support for Python wheel packaging, Windows ARM64, new iOS xcframework, Gtk4 on macOS and Windows, and more plugins
- Smaller binary sizes of Rust plugins in Windows and Android binary packages
- Peel: New C++ bindings for GStreamer
- Lots of new plugins, features, performance improvements and bug fixes
- Countless bug fixes, build fixes, memory leak fixes, and other stability and reliability improvements

<a id="major-changes"></a>
## Major new features and changes

<a id="hip"></a>
### AMD HIP plugin and integration library

- HIP (formerly known as Heterogeneous-computing Interface for Portability) is AMD’s
  GPU programming API that enables portable, CUDA-like development across both AMD and
  NVIDIA platforms:

    - On AMD GPUs, HIP runs natively via the ROCm stack.
    - On NVIDIA GPUs, HIP operates as a thin translation layer over the CUDA runtime and driver APIs.

  This allows developers to maintain a single codebase that can target multiple GPU vendors with minimal effort.

- The [**new HIP plugin**][hip-plugin] provides the following elements:

    - **hipcompositor**: a HIP-based video mixer/compositor
    - **hipconvert**: Converts video from one colorspace to another using HIP
    - **hipconvertscale**: Resizes video and allow color conversion using HIP
    - **hipscale**: Resize video using HIP
    - **hipdownload**: Downloads HIP device memory into system memory
    - **hipupload**: Uploads system memory into HIP device memory

- The **GStreamer HIP integration helper library** provides HIP integration functionality
  to applications and other HIP users.

- Watch the [Bringing AMD HIP into GStreamer][gstconf-hip] talk from last year's GStreamer Conference
  for more details or read Seungha's [devlog post][devlog-hip] on the subject.

[hip-plugin]: https://gstreamer.freedesktop.org/documentation/hip/index.html?gi-language=c#hip-page
[gstconf-hip]: https://gstconf.ubicast.tv/videos/bringing-amd-hip-into-gstreamer/
[devlog-hip]: https://centricular.com/devlog/2025-07/amd-hip-integration/

<a id="lvecv"></a>
### Low Complexity Enhancement Video Coding (LCEVC) support for H.265 and H.266

- [LCEVC][lcevc] is a codec that provides an enhancement layer on top of
  another codec such as H.264 for example. It is standardised as MPEG-5 Part 2.

- **LCEVC H.265 and H.266 encoder and decoder elements** based on V-Nova's SDK libraries
  were added in this cycle

- **Autoplugging support for LCEVC H265 and H266 video streams**, so these can be decoded
  automatically in a decodebin3 or playbin3 scenario.

[lcevc]: https://www.lcevc.org

<a id="closedcaptions"></a>
### Closed captions and text handling improvements

- **cea708overlay**: suport non-relative positioning for streams with CCs
  that do not have relative positions. Instead of displaying them at the
  top, they are positioned relatively.

- **cea708mux**: expose "discarded-services" property on sink pads. This can be
  useful when muxing in an original caption stream with a newly-created one
  (e.g. transcription / translation), in which case one might wish to discard
  select services from the original stream in order to avoid garbled captions.

- **sccparse**: Better handling of streams with more byte tuples in the SCC field.

- **tttocea608**: expose "speaker-prefix" property

- Miscellaneous improvements and spec compliance fixes

- Also see SMPTE ST-2038 metadata section below.

<a id="stt-translations-and-speech-synthesis"></a>
### Speech to Text, Translation and Speech Synthesis

- New **audio source separation element based on demucs** in Rust. This is useful
  to separate speech from background audio before running speech to text transcription,
  but could also be used to separate vocals from music for karaoke.

- New **Deepgram speech-to-text transcription** plugin in Rust.

- The **Speechmatics transcriber** has seen a **major refactoring** for better timings,
  gap and discontinuity handling and has gained **support for the new Speechmatics speaker
  identification API** as well as a new property to **mask profanities**.

- New **ElevenLabs voice cloning element**. The new element can operate in two modes:
    - In single speaker mode, the element will directly clone a single voice
      from its input, without storing any samples.
    - Otherwise, the element will store a backlog of samples, and wait to
      receive certain events from a transcriber on its source pad before draining
      them to create potentially multiple voices.

- New **"compress" overflow mode for speech synthesizers** that can speed up
  the audio while preserving pitch. This may be needed to keep or regain audio/video
  synchronisation if translated speech output has been consistently longer in duration
  than the original and there hasn't been a sufficient amount of silence that could be
  filled in to make up the difference.

- **awstranslate**: new "brevity-on" property for turning brevity on.

- The **awstranscriber2** has been **refactored** to match the speechmatics transcriber design
  and gained a "show-speaker-label" property that defines whether to partition speakers in the
  transcription output.

- New **textaccumulate** element for speech synthesis or translation preprocessing that
  can be used to accumulate words and punctuation into complete sentences (or sentence fragments)
  for synthesis and / or translation by further elements downstream.

<a id="adaptive-streaming"></a>
### HLS DASH adaptive streaming improvements

- Reverse playback, seeking and stream selection fixes in the HLS/DASH clients.

- **hlscmafsink** can generate I-frame only playlists now

- Both **hlssink3** and **hlscmafsink** gained support for use of a single media file,
  in which case the media playlist will use byte range tags for each chunk whilst
  always referencing the same single media file. This can be useful for VOD use cases.

<a id="playback"></a>
### decodebin3 and playbin3 improvements

 - **decodebin3** now has a separate pad template for **metadata streams** and considers
   KLV, ID3 PES streams and ST-2038 ancillary streams as raw formats for meta streams. This comes
   also with a new dedicated `GST_STREAM_TYPE_METADATA` stream type in the stream collection.

<a id="eflv"></a>
### Enhanced RTMP and multitrack audio/video support in FLV

- The FLV container used for RTMP streaming is fairly old and limited in terms of features:
  It only supports one audio and one video track, and also only a very limited number of
  audio and video codecs, most of which are by now quite long in the tooth.

- The [Enhanced RTMP (V2) specification](https://veovera.org/docs/enhanced/enhanced-rtmp-v2.html) seeks
  to remedy this and adds **support for modern video codecs such H.265 and AV1** as well as
  **support for more than one audio and video track** inside the container.

- Both H.265 video and multiple audio/video tracks are now supported for FLV in GStreamer.

- Support for this comes in form of a **new [eflvmux][eflvmux] muxer element**, which is needed
  to accommodate both the need of backwards compatibility in the existing FLV muxer and the
  requirements of the new format. See [Tarun's blog post][eflv-devlog] for more details.

[eflvmux]: https://gstreamer.freedesktop.org/documentation/flv/eflvmux.html?gi-language=c#eflvmux-page
[eflv-devlog]: https://centricular.com/devlog/2025-11/Multitrack-Audio-Capability-in-FLV/

<a id="mpeg-ts"></a>
### MPEG-TS container format improvements

- The **MPEG-TS demuxer** gained a "skew-corrections" **property that allows disabling
  of skew corrections**, which are done by default for live inputs to make sure
  downstream consumes data at the same rate as it comes in if the local clock and the
  sender clock drift apart (as they usually do). Disabling skew corrections is useful if
  the input stream has already been clock-corrected (for example with `mpegtslivesrc`) or
  where the output doesn't require synchronisation against a clock, e.g. when it's
  re-encoded and/or remuxed and written to file (incl. HLS/DASH output) where it's
  desirable to maintain the original timestamps and frame spacings.

  It is also useful for cases where we want to refer to the PCR stream to figure out global
  positioning, gap detection and wrapover correction.

- **tsdemux** now also supports demuxing of ID3 tags in MPEG-TS as specified in the
  [Apple Timed Metadata for HTTP Live Streaming specification][apple-timed-metadata-spec].
  These timed ID3 tags have a media type of `meta/x-id3` which is different from the one
  used to tag audio files, and an `id3metaparse` element is needed to properly frame the
  PES data coming out of the demuxer.

- The **MPEG-TS muxer** now also reads `prog-map[PMT_ORDER_<PID>]` for PMT order
  key in addition to `prog-map[PMT_%d]`, which fixes a wart in the API and provides
  an unambiguous way to specify ordering keys.

[apple-timed-metadata-spec]: https://developer.apple.com/library/archive/documentation/AudioVideo/Conceptual/HTTP_Live_Streaming_Metadata_Spec/2/2.html

<a id="matroska"></a>
### Matroska container format improvements

- **matroskademux** now supports relative position cues in the seek table
  and also had its maximum block size restrictions updated so that it can
  support uncompressed video frames also in 4k UHD resolution and higher
  bit depths.

<a id="isomp4"></a>
### ISO MP4 container format improvements

- **mp4mux** now supports E-AC3 muxing

- **qtdemux**, the MP4 demuxer, has seen countless fixes for various
  advanced use cases (with lots more in the pipeline for 1.28.1).

- The **isomp4mux** from the Rust plugins set now support caps changes
  and has also gained support for raw audio as per ISO/IEC 23003-5.
  Plus improved brand selection.

- The **isomp4mux**, **isofmp4mux** and related elements were merged into a single **isobmff** plugin, which allows sharing more code. As part of this, codec support was consolidated between the two.

<a id="mxf"></a>
### MXF container format improvements

- The MXF muxer and demuxer gained **support for non-closed-caption VANC ancillary metdata**:

    - Extends mxfdemux with support for outputting VANC (ST436M) essence
      tracks as ST2038 streams instead of extracting closed captions internally.

    - Extends mxfmux with support for consuming ST2038 streams for outputting
      VANC (ST436M) essence tracks instead of only supporting closed captions.

To support ST2038 instead of the earlier closed captions, we introduce a
_breaking change_ to the caps handling on the pad. This was deemed the cleanest
way and should hopefully not cause too much breakage in the real world, as it
is likely not something that was used much in practice in this form. The
`st2038anctocc` element can be used to convert a ST2038 stream to plain
closed captions.

We also now support both 8 and 10-bit VANC data when reading from MXF.

<a id="mpeg-h"></a>
### MPEG-H audio support

- New **MPEG-H audio decoding** plugin based on the Fraunhofer MPEG-H decoder
  implementation plus MP4 demuxing support

### SMPTE 2038 ancillary data stream handling improvements

- New **ST-2038 ancillary data combiner and extractor elements** in
  the `rsclosedcaption` Rust plugin that extract ST-3028 metadata streams
  from `GstAncillaryMeta`s on video frames or converts ST-2038 metadata
  streams to `GstAncillaryMeta` and combines it with a given video stream.

- The MXF demuxer and muxer gained support for muxing and demuxing generic
  ancillary metadata in ST-2038 format (see below).

- decodebin3 now treats ST-2038 metadata streams as a "raw metadata format"
  and exposes those streams as `GST_STREAM_TYPE_METADATA`.

<a id="analytics"></a>
### Analytics

This release introduces a major improvement in how analytics pipelines are built,
moving away from manual configuration toward a fully negotiated analytics pipeline.

- **Robust Tensor Negotiation & Smart Selection**: All inference and tensor decoder
  elements adopt the tensor capability negotiation mechanism. This provides informative
  error handling by validating the pipeline during the setup phase and providing
  descriptive error messages for configuration mismatches before processing begins.
  Complementing this, the new tensordecodebin acts as an intelligent proxy that abstracts
  decoder selection by auto-plugging the correct tensor decoder. This simplifies the use of
  existing tensor decoders and allows new tensor decoders to be utilized instantly without
  requiring changes to pipeline definitions.

- **Simplified Model Integration with modelinfo**: The modelinfo library, configuration files,
  and the modelinfo-generator.py script work together to make using any ML model inside
  a GStreamer pipeline very simple. The new utility script helps you quickly generate or
  upgrade metadata files for existing models. Combined with tensor negotiation and tensordecodebin,
  these tools facilitate the seamless utilization of new models within the analytics chain.

- **analyticsoverlay**: New "expire-overlay" property added to objectdetectionoverlay and can
  also show tracking-id; New 'segmentationoverlay' to visualize segmented regions.

- Add **LiteRT inference element**

- Analytics: add **general classifier tensor-decoder**, **facedetector**, **YOLOv8 (detection)**,
  **YOLOv8segmentation tensor decoders** and more convenience API.

- **onnx**: Add **Verisilicon provider** support

- New **IoU based tracker**

- Add [**`GstAnalyticsBatchMeta`**][batchmeta] representing a batch of buffers from one or
  more streams together with the relevant events to be able to interpret the buffers and to
  be able to reconstruct the original streams.

- New [**analyticscombiner**][analyticscombiner] and [**analyticssplitter**][analyticssplitter]
  elements in the Rust plugin set which batch buffers from one or more streams into a single
  stream via the new `GstAnalyticsBatchMeta` and allow splitting that single stream into the
  individual ones again later.

- Add a [burn-based YOLOX inference element][burnyoloxinference] and a [YOLOX tensor decoder][yoloxtensordec] in Rust.

[batchmeta]: https://gstreamer.freedesktop.org/documentation/analytics/gstanalyticsbatchmeta.html?gi-language=c#GstAnalyticsBatchMeta
[analyticscombiner]: https://gstreamer.freedesktop.org/documentation/rsanalytics/analyticscombiner.html?gi-lang
[analyticssplitter]: https://gstreamer.freedesktop.org/documentation/rsanalytics/analyticssplitter.html?gi-language=c#analyticssplitter-page
[burnyoloxinference]: https://gstreamer.freedesktop.org/documentation/burn/index.html?gi-language=c#burnyoloxinference-page
[yoloxtensordec]: https://gstreamer.freedesktop.org/documentation/rsanalytics/yoloxtensordec.html?gi-language=c#yoloxtensordec-page

<a id="vulkan"></a>
### Vulkan integration enhancements

- The Vulkan Video encoders and decoders now dynamically generate their
  pad template caps at runtime instead of hardcoding them, so they more
  accurately reflect the actual capabilities of the hardware and drivers.

- New **Vulkan AV1 and VP9 video decoding** support

- New **Vulkan H.264 encoding** support

- The **Vulkan H.265 decoder** now also supports **10-bit** depth

<a id="opengl"></a>
### OpenGL integration enhancements

- Implement keyboard, mouse, and scroll wheel navigation event handling for the
  OpenGL Cocoa backend.

- Added support for the `NV24` and `Y444_12` pixel formats. The latter is used by
  certain HEVC decoders for 12-bit non-subsampled profiles.

<a id="udmabuf"></a>
### udmabuf allocator with glupload support

- Implement a **udmabuf-based memory allocator for user-space mappable dmabufs**.

- **glupload**: add udmabuf uploader to **share buffers between software decoders/sources and GPUs,
  display engines (wayland), and other dma devices**. This can help reduce memory copies and
  can massively improve performance in video players like Showtime or Totem for software-decoded
  video such as AV1 with `dav1ddec`.

- **gtk4paintablesink**: Similar to `glupload`, this now proposes the udmabuf memory allocator to upstream which can reduce memory copies and improve performance with certain software decoders.

<a id="wayland"></a>
### Wayland integration

- Added **basic colorimetry support**

- **waylandsink**:

    - Parse and set the **HDR10 metadata** and other **color management improvements**

    - **udmabuf support** (see above)

    - video **crop meta support**

    - New **"fullscreen-output"** and **"force-aspect-ratio" properties**

<a id="qt"></a>
### Qt5 + Qt6 QML integration improvements

- New **Qt6 QML qml6 render source element**

- **qml6gloverlay**: support directly passing a QQuickItem for QML the render tree

<a id="gtk"></a>
### GTK4 integration improvements

- **gtk4paintablesink**: Added YCbCr memory texture formats and
  improve color-state fallbacks. The sink will also propose a **udmabuf** buffer pool
  and allocator now if upstream asks for sysmem, which would allow direct imports of the
  memory by GL/Vulkan or the compositor. Plus many other improvements which have also
  been backported into the 0.14 branch.

<a id="cuda-nvcodec"></a>
### CUDA / NVCODEC integration and feature additions

- **cudacompositor**, **cudaconvert** and its variants gained **crop meta support**

- nvencoder: interlaced video handling improvements and "emit-frame-stats" property
  which if enabled makes the encoder emit the "frame-stats" signal for each encoded
  frame, allowing applications to monitor things like the average QP per frame.

- **nvjpegenc**: Add an autogpu mode element (nvautogpunvenc) similar to `nvautogpu{h264,h265,av1}enc`.

- **nvh264enc**,  **nvh265enc** gained a new "num-slices" property which is
  conditionally available based on device support for dynamic slice mode

- **nvdsdewarp**: performance improvements and support for output resizing support,
  along with a new "add-borders" property.

<a id="aja-decklink"></a>
### Capture and playout cards support

- **Blackmagic Decklink** elements gained support for **capturing and outputting all types of VANC via GstAncillaryMeta**

<a id="rtp"></a>
### RTP and RTSP stack improvements

- **rtspsrc** now sends RTSP keepalives also in TCP/interleaved modes. This fixes problems
  with some cameras that don't see the RTCP traffic as sufficient proof of liveness,
  when using TCP/HTTP tunnelled modes.

- New **Rust RTP mparobust depayloader** for "robust mp3" audio**, a more
  loss-tolerant RTP payload format for MP3 audio (RFC 5219)

- New **Rust RTP L8/L16/L24 raw audio payloader and depayloader**, which offer
  more correct timestamp handling compared to the old payloader and depayloader
  and more correctly implements multichannel support.

- New **Rust RTP SMTPE ST291 ancillary data payloader and depayloader** for sending
  or receiving ancillary data over RTP. This is also the payload format used by ST2110-40.

- Various performance improvements and fixes for `rtprecv` / `rtpsend` ("rtpbin2").

- Support for "multi-time aggregation packets" (MTAP) in the H264 RTP depayloader `rtph264depay`.

<a id="webrtc"></a>
### WebRTC improvements

- **webrtcbin** and **GstWebRTC library** improvements:

    - Add support for getting the selected ICE candidate pairs

    - Improve spec compliance for ICE candidate stats by filling the foundation, related-address,
      related-port, username-fragment and tcp-type fields of stats.

    - improve compatibility with LiveKit

- **webrtcsink** and **webrtcsrc** enhancements:

    - **webrtcsink** gained **renegotiation support**, and **support for va hardware encoders**

- Added a WHEP client signaller and server signaller to the Rust webrtc plugin, including
  support for server side offers for the WHEP client.

- webrtc-api: Set **default bundle policy** to max-bundle.

- The **dtls** plugin now uses a ECDSA private key for the default certificate. ECDSA is
  widely used in browsers and SFUs, and some servers such as the ones using BouncyCastle
  only accept certificates signed with ECDSA.

<a id="new-cplusplus-bindings"></a>
### New GStreamer C++ bindings

The old GStreamer C++ bindings (gstreamermm and qt-gstreamer) have been unmaintained for a
long time, leaving C++ developers only with the option to use the GStreamer C API.

In recent years, a new approach for C++ bindings was developed by the GNOME community:
[peel](https://gitlab.gnome.org/bugaevc/peel). While initially developed for GTK, with
various GObject Introspection and API fixes included in GStreamer 1.28, this is now also
usable for GStreamer.

Compared to gstreamermm this offers a much lower overhead, headers-only C++ binding that
just depends on the C libraries and not even the C++ STL, and provides a modern C++ API
top of the GStreamer C API. Compared to qt-gstreamer there is no dependency on Qt.

It's still in active development and various [MRs](https://gitlab.gnome.org/bugaevc/peel/-/merge_requests)
for improving the GStreamer development experience are not merged yet, but it's already usable and a
great improvement over using the plain C API from C++.

Various GStreamer examples can be found in [Sebastian's GStreamer peel examples repository][peel-examples].

[peel-examples]: https://gitlab.freedesktop.org/slomo/gstreamer-peel-examples

<a id="new-plugins"></a>
## New elements and plugins

- Many exciting **new Rust elements**, see Rust section below.

- New **D3D12** interlace, overlay compositor, fish eye dewarp and uv coordinate remapping elements

- **VMAF**: New element to **calculate perceptual video quality assessment scores using Netflix's VMAF framework**

- **Webkit**: New wpe2 plugin that makes use of the "WPE Platform API" with support
  for rendering into GL and SHM buffers and navigation events (but not audio yet).

- Many other new elements mentioned in other sections (e.g. CUDA, NVCODEC, D3D12, Speech, AMD HIP, Rust etc.)

<a id="new-element-features"></a>
## New element features and additions

- The **AWS S3 sink and source** elements now support S3 compatible URI schemes.

- **clocksync**: new "rate" property and "resync" action signal so that clocksync
  can synchronise buffer running time against the pipeline clock with a specified
  rate factor. This can be useful if one wants to throttle pipeline throughput such
  as e.g. in a non-realtime transcoding pipeline where the pipeline's CPU and/or
  hardware resource consumption needs to be limited.

- **fallbacksrc** is able to support **encoded outputs** now, not just uncompressed
  audio/video. As part of this it supports stream selection via the `GstStream` API now.

- **h265parse** now automatically inserts AUDs where needed if it outputs
  byte-stream format, which helps fix decoding artefacts for multi-slice HEVC
  streams with some hardware decoders.

- **input-selector** now implements a two-phase sinkpad switch to avoid races when switching
  input pads. Extensive tests have been added to avoid regressions.

- The **inter plugin wormhole sink and source elements** for sending data between pipelines
  within the same application process gained new **properties to fine tune the inner elements**.
  intersrc can now also be configured to **forward upstream events to the producer pipeline** via
  the new "event-types" property.

- The **quinn** plugin supports **sharing of the QUIC/WebTransport connection/session** with an
  element upstream or downstream. This is required for supporting Media over QUIC (MoQ) later, for
  which [an MR is already pending](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/1906).

- **replaygain** will use EBU-R128 gain tags now if available.

- **threadshare: many improvements** to the various threadshare elements, plus examples and
  a new benchmark program. The plugin was also relicensed to MPL-2.0.

- The **unixfdsink** element for zero-copy 1:N IPC on Linux can now also copy the
  input data if needed, which makes it usable with more upstream elements. Before
  it would only work with elements that made use of the special memory allocator
  it advertised. This (copy if needed) is enabled by default, but can be disabled
  by setting the new "min-memory-size" property to -1.

  There's also a new "num-clients" property that gets notified when the number
  of clients (unixfdsrc elements tapping the same unixfdsink) changes.

- **videorate** and **imagefreeze** now also support JPEG XS.

- **videorate**'s formerly defunct "new-pref" property was revived for
  better control which frame to prefer for output in case of caps changes.

<a id="plugin-library-moves"></a>
## Plugin and library moves and renames

- The `y4mdec` plugin moved from gst-plugins-bad into gst-plugins-good and
  was merged with the existing `y4menc` there into a single `y4m` plugin
  containing both a YUV4MPEG encoder and decoder.

- The `fmp4` and `mp4` plugins in the Rust plugins set were merged into a
  single `isobmff` plugin.

<a id="plugin-element-deprecations"></a>
## Plugin and element deprecations

- The old librtmp-based **rtmpsrc** and **rtmpsink** elements are deprecated
  are scheduled for removal in the next release cycle. Use the **rtmp2src**
  and **rtmp2sink** elements instead (which will likely also be registered
  under the old names after removal of the old rtmp plugin).

- Deprecate the **webrtchttp** plugin in the Rust plugins set along with its
  **whipsink** and **whepsrc** elements, in favour of the whipclientsink and
  whepclientsrc elements from the webrtc plugin in the Rust plugins set.

- The libmpeg2-based **mpeg2dec** element is deprecated and scheduled for
  removal in the next release cycle, as libmpeg2 has been unmaintained for
  a very long time. The libavcodec-based decoder has had a higher rank for
  many years already and is also more performant. We would recommend that
  distros that also ship the FFmpeg-based decoder out of the box stop shipping
  the mpeg2dec plugin now or reduce its rank to `GST_RANK_NONE`.

<a id="plugin-element-removals"></a>
## Plugin and element removals

- The **cc708overlay** element has been removed. It is replaced by the
  [cea708overlay][cea708overlay] element from the `rsclosedcaption` plugin
  in the Rust plugins module.

[cea708overlay]: https://gstreamer.freedesktop.org/documentation/rsclosedcaption/cea708overlay.html?gi-language=c

- Drop registration of **rusotos3src** and **rusotos3sink** in the AWS
  plugin in the Rust plugins set. These were legacy names that were renamed
  to awss3src and awss3sink in 2022, but had been kept around for a while
  so applications had time to move to the new name space.

<a id="new-api"></a>
## Miscellaneous API additions

<a id="core-api"></a>
### GStreamer Core

- [`gst_call_async()`][gst-call-async] and [`gst_object_call_async()`][gst-object-call-async]
  are more generic and convenient replacements for `gst_element_call_async()`

- [`gst_check_version()`][gst-check-version] is a new convenience function to check for
  a minimum GStreamer core version at runtime.

- GstClock: Add [gst_clock_is_system_monotonic()][gst_clock_is_system_monotonic] utility function

- GstController: [gst_timed_value_control_source_list_control_points()][gst_timed_value_control_source_list_control_points]
  is a thread-safe method to retrieve the list of control points, replacing
  `gst_timed_value_control_source_get_all()`.

- GstCpuId: [`gst_cpuid_supports_x86_avx()`][cpuid-supports] and friends can be used to check
  which SIMD instruction sets are supported on the current machine's CPU without
  relying on liborc for that. This is useful for plugins that rely on an external
  library that wants to be told which SIMD code paths to use.

- [`gst_object_get_toplevel()`][gst_object_get_toplevel] can be used to get the
  toplevel parent of an object, e.g. the pipeline an element is in.

- **New API for tensor caps descriptions**:

    - [`GstUniqueList`][GstUniqueList] is a new unordered, unique container value type for `GValue`s
      similar to `GstValueList` but guaranteed to have unique values. Can only be queried and
      manipulated via the `gst_value_*` API same as `GstValueList` and `GstValueArray`.

    - [`gst_structure_get_caps()`][gst_structure_get_caps] gets a `GstCaps` from a structure

- More accessor functions for `GstPadProbeInfo` fields and the `GstMapInfo` `data` field, as well as a generic `gst_map_info_clear()` which is useful for language bindings.

- New EBU-R128 variants of the replay gain tags: `GST_TAG_TRACK_GAIN_R128` and `GST_TAG_ALBUM_GAIN_R128`

- **GstReferenceTimestampMeta**: additional information about the timestamp can be provided via the
  new optional info `GstStructure`. This should only be used for information about the timestamp and
  not for information about the clock source. This is used in an implementation of the TAI timestamp
  functionality described in ISO/IEC 23001-17 Amendment 1 in the Rust MP4 muxer.

- GstValue: add [`gst_value_hash()`][gst_value_hash] and support `0b` / `0B` prefix for
  bitmasks when deserialising.

 - Add missing `_take()` and `_steal()` functions for some mini objects:
   - `gst_buffer_take()`, `gst_buffer_steal()`
   - `gst_buffer_list_steal()`
   - `gst_caps_steal()`
   - `gst_memory_take()`, `gst_memory_replace()`, `gst_memory_steal()`
   - `gst_message_steal()`
   - `gst_query_steal()`

- GstElement: Deprecate `gst_element_state_*()` API and provide `gst_state_*()`
  replacements with the right namespace

[gst-call-async]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstutils.html?gi-language=c#gst_call_async
[gst-object-call-async]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstobject.html#gst_object_call_async
[gst-check-version]: https://gstreamer.freedesktop.org/documentation/gstreamer/gst.html?gi-language=c#gst_check_version
[gst_clock_is_system_monotonic]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstclock.html?gi-language=c#gst_clock_is_system_monotonic
[gst_structure_get_caps]: https://gstreamer.freedesktop.org/documentation/gstreamer/gststructure.html?gi-language=c#gst_structure_get_caps
[gst_timed_value_control_source_list_control_points]: https://gstreamer.freedesktop.org/documentation/controller/gsttimedvaluecontrolsource.html?gi-language=c#gst_timed_value_control_source_list_control_points
[GstUniqueList]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstvalue.html?gi-language=c#gst_value_unique_list_append_value
[cpuid-supports]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstcpuid.html?gi-language=c
[gst_object_get_toplevel]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstobject.html?gi-language=c#gst_object_get_toplevel
[gst_value_hash]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstvalue.html?gi-language=c#gst_value_hash

<a id="meta-factory"></a>
#### GstMetaFactory to dynamically register metas

- [`gst_meta_factory_register()`][meta-factory-register] allows to dynamically register metas
  and store them in the registry by name. This is useful in combination with the `GstMeta`
  serialisation and deserialisation functionality introduced in GStreamer 1.24, for metas that
  are not provided by GStreamer core. If an element comes across a meta name that is not
  registered yet with GStreamer, it can check the registry and load the right plugin which will
  in turn register the meta with GStreamer. This is similar to how flag and enum types can be
  stored in the registry so that if during caps deserialisation an unknown enum or flag type is
  encountered, it can be loaded dynamically and registered with the type system before
  deserialisation continues.

  The `pbtypes` plugin in gst-plugins-base registers `GstAudioMeta` and `GstVideoMeta` in the
  registry so that e.g. `unixfdsrc` and other elements can make sure they get pulled in and
  registered with GStreamer before deserialising them.

[meta-factory-register]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstmetafactory.html?gi-language=c#gst_meta_factory_register

<a id="gstapp"></a>
### App Sink and Source Library

- **appsrc** and **appsink** gained support for a more bindings-friendly
  "simple callbacks" API that can be used instead of GObject signals (which have considerable overhead) or the normal callbacks API (which couldn't be used from most bindings).

<a id="gstaudio"></a>
### Audio Library

- added **support for 20-bit PCM audio stored in 32-bit containers**,
  both signed (**`S20_32`**) and unsigned (**`U20_32`**), each in
  little-endian and big-endian variants.

<a id="gstpbutils"></a>
### Plugins Base Utils Library

- Many minor improvements.

<a id="gsttag"></a>
### Tag Library

- Vorbis comments: parse EBU R128 tags

<a id="gstvideo"></a>
### Video Library and OpenGL Library

- Add DRM equivalents for various 10/12/16 bit SW-decoders formats

- New **`GstVideoMetaTransformMatrix`** that adds crop, scale, rotate,
  flip, shear and more meta transformations. The current "scaling" transformation
  doesn't work if either the input buffer is cropped or if any kinds of borders
  are added. And it completely falls down with more complex transformations like
  compositor.

- **`GstVideoOverlayCompositionMeta`**: handling of multiple video overlay composition metas
  on a single buffer has been fixed in lots of places (overlays and sinks). Many elements
  assumed there would only ever be a single overlay composition meta per buffer. For
  that reason `gst_buffer_get_video_overlay_composition_meta()` has been deprecated,
  so that elements have to iterate over the metas and handle multiple occurences of it.

#### New Raw Video Formats

- Add **more 10bit RGB formats** commonly used on ARM SoCs in GStreamer Video, OpenGL and Wayland,
  as well as in deinterlace and gdkpixbufoverlay:
    - **`BGR10x2_LE`**: packed 4:4:4 RGB (B-G-R-x), 10 bits for R/G/B channel and MSB 2 bits for padding
    - **`RGB10x2_LE`**: packed 4:4:4 RGB (R-G-B-x), 10 bits for R/G/B channel and MSB 2 bits for padding

- Add 10-bit 4:2:2 **`NV16_10LE40`** format, which is a fully-packed variant of `NV16_10LE32` and
  also known as `NV20` and is produced by Rockchip `rkvdec` decoders.

<a id="gstplay"></a>
### GstPlay Library

- GstPlay: Add **support for gapless looping**

<a id="optimisations"></a>
## Miscellaneous performance, latency and memory optimisations

- New **task pool GstContext to share a thread pool amongst elements in a pipeline for better resource
  management and performance**, especially for video conversion and compositing. This is currently only
  made use of automatically in the GStreamer Editing Services library.

- **glupload**: Implement **udmabuf uploader to share buffers between software decoders/sources and GPUs,
  display engines (wayland)**, and other dma devices (see above).

- **GstDeviceMonitor** now starts device providers in a **separate thread**. This **avoids blocking** the
  application when `gst_device_monitor_start()` is called, which avoids each app having to spawn
  a separate thread just to start device monitoring. This is especially important on Windows, where
  device probing can take several seconds or on macOS where device access can block on user input.
  A new `GST_MESSAGE_DEVICE_MONITOR_STARTED` is posted on the bus to signal to the application that
  the device monitor has completed its async startup.

- On Windows **audioresample** now has SIMD optimisations enabled also for the MSVC build.

- **audiomixmatrix** / **audioconvert**: sparse matrix LUT optimisation which uses precomputed LUTs for
  non-zero coefficients instead of blindly traversing all input/output channel combinations.

- As always there have been plenty of performance, latency and memory optimisations
  all over the place.

<a id="misc-changes"></a>
## Miscellaneous other changes and enhancements

- The **ALSA device provider** now supports enumerating virtual PCM sinks

- The **ASIO** device monitor can now detect dynamically added and removed devices by monitoring USB events.

<a id="tracing"></a>
## Tracing framework and debugging improvements

- There are **new hooks to track when buffers are queued or dequeued from**
  **buffer pools** in the tracing system.

- The **pad-push-timings tracer** gained a new **"write-log" action signal**

<!--
### New tracers

- None

-->

#### Dot tracer/viewer

- **Enhanced dots-viewer**: Major refactoring with modular JavaScript
  architecture, bundled dependencies (no more CDN), clickable pipeline
  references for navigation between related dot files, download SVG button,
  and improved UI/UX with better text handling and zoom fixes.

### Dot file pipeline graphs

- Dot file dumps of **pipeline graphs now show the list of active tracers**
  at the bottom along with the tracer configuration.

### Debug logging system improvements

#### GstLogContext to fine-tune logging output and reduce log message spam

- [`GstLogContext`][GstLogContext] is a new API to control logging behavior, particularly for implementing "log once" functionality and periodic logging. This helps avoid spamming logs with repetitive messages. This comes with a whole suite of new [`GST_CTX_*`][GST_CTX] debug log macros that take a context argument in addition to the usual arguments.

- A number of [`GST_{MEMDUMP,TRACE,LOG,DEBUG,INFO,WARNING,ERROR}_ONCE`][LOG_ONCE] convenience macros for logging something only once.

[GstLogContext]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstinfo.html?gi-language=c#GstLogContext
[GST_CTX]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstinfo.html?gi-language=c#GST_CTX_LOG
[LOG_ONCE]: https://gstreamer.freedesktop.org/documentation/gstreamer/gstinfo.html?gi-language=c#GST_LOG_ONCE_OBJECT

- The source code of elements and plugins has to be updated to make use of this new feature,
  so if there are any particular log messages in certain elements that you feel are particularly
  spammy, please feel free to file an issue in GitLab so we can see if it would make sense to
  use the new API there.

<a id="tools"></a>
## Tools

- **gst-inspect-1.0** now shows the type of each field when it prints caps
  and also pretty-prints tensor caps.

<a id="ffmpeg"></a>
## GStreamer FFmpeg wrapper

- The avdec video decoders have seen many improvements and fixes for their
  buffer pool and allocation query handling.

<a id="rtsp"></a>
## GStreamer RTSP server

- rtsp-client: Add a "pre-closed" signal which provides a way for an application
  to be notified when a connection is closed, before the client's sessions are
  cleaned up. This is useful when a client terminates its session improperly,
  for example, by sending a TCP RST.

- rtsp-stream-transport: expose new "timed-out" property. Upon RTCP timeout,
  rtpsession emits a signal that we can catch and then also expose the timed
  out state a property of the transport in order for users (such as rtspclientsink)
  to get notified about it.

- **rtspclientsink** now errors out on timeout.

<a id="vaapi"></a>
## VA-API integration

### VA plugin for Hardware-Accelerated Video Encoding and Decoding on Intel/AMD

- **vaav1enc**: Enable intrablock copy and palette mode.

- Lots of other improvements and bug fixes.

### GStreamer-VAAPI has been removed in favour of the va plugin

- **gstreamer-vaapi has been removed and is no longer updated going forward**
  Users who relied on gstreamer-vaapi are encouraged to migrate to the `va`
  plugin's elements at the earliest opportunity. It should still be possible
  to build old versions of gstreamer-vaapi against newer versions of GStreamer.

<!--

<a id="v4l2"></a>
## GStreamer Video4Linux2 support

- Nothing major?

-->

<a id="ges"></a>
## GStreamer Editing Services and NLE

- **Task Pool Context Support**: GESPipeline now supports task pool context
  handling for better resource management. It automatically creates and manages
  a GstSharedTaskPool with threads set to the number of processors, also
  allowing applications to provide their own shared task pool via context
  negotiation.

- **MT-Safe Controller API**: New
  `gst_timed_value_control_source_list_control_points()` function provides
  thread-safe access to control points, addressing use-after-free bugs in the
  previous API which returned references to internal structures.

- **OTIO Formatter Migration**: The OpenTimelineIO formatter has been moved from
  embedded GLib resources to a standalone Python plugin located in gst-python,
  simplifying the implementation and avoiding duplicated code.

- **Framepositioner Z-order Enhancements**: The z-order property is now
  controllable and exposed for manipulation, enabling dynamic adjustment of
  layer stacking order during timeline editing.

- **Clip Layer Movement Detection**: New `ges_clip_is_moving_between_layers()`
  API distinguishes actual layer moves from other operations like split/ungroup,
  with separate flags for track element freezing and layer movement.

- **GES Error Domain**: Added `ges_error_quark()` function for proper GError
  domain support, enabling automatic ErrorDomain implementation generation in
  language bindings.

- **Timeline Error Reporting**: Added GError parameter to
  `ges_base_bin_set_timeline()` for proper error reporting when timeline setup
  fails.

- Various bug fixes for memory leaks, frame position calculations with
  non-square pixel aspect ratios, and control binding handling.


<a id="validate"></a>

## GStreamer validate

- **New `check-last-frame-qrcode` action type**: New action type (from the Rust
  validate plugin) to validate QR code content in video frames. Supports exact
  string matching for single or multiple QR codes, and JSON field validation.

- **Override Severity Levels**: New `overrides` parameter in the `meta` action
  type allows changing issue severity levels during test execution. Tests can
  now pass when encountering known issues by downgrading severity from critical
  to warning/issue/ignore.

- **Enhanced dots-viewer** (see dots-viewer section above)

- **SSIM Validation Improvements**: Changed validation to check all images
  before reporting errors instead of stopping at the first error.

- **Reverse Playback Validation**: Changed segment.time mismatch from critical
  to warning for reverse playback scenarios, acknowledging the additional
  complexity demuxers face during reverse playback.

- **Launcher Improvements**: Log files for passing tests are now removed by
  default to reduce storage usage (with option to keep them), and debug log
  colors are now supported when redirected to files.

- **Python 3.14 Compatibility**: Fixed file:/// URI generation for Python 3.14
  with proper RFC 8089 compliance.

- Various bug fixes for scenario handling, memory leaks, and improved backward
  compatibility with GLib 2.64.

<a id="python"></a>
## GStreamer Python Bindings

gst-python is an extension of the regular GStreamer Python bindings based on
gobject-introspection information and PyGObject, and provides "syntactic sugar"
in form of overrides for various GStreamer APIs that makes them easier to use
in Python and more pythonic; as well as support for APIs that aren't available
through the regular gobject-introspection based bindings, such as e.g.
GStreamer's fundamental GLib types such as `Gst.Fraction`, `Gst.IntRange` etc.

- More **pythonic API for analytics**

- Type annotations have been updated in [PyGObject-stubs](https://pypi.org/project/PyGObject-stubs/).

- Writability of `Gst.Structure`, `Gst.Caps` and other objects has been improved.
  * `caps.writable_structure()` now returns a ContextManager inside of which
     the returned Gst.Structure can be modified.
  * `obj.make_writable()` makes any MiniObject writable.
  * Pad probe callbacks now has `info.writable_object()` and `info.set_object()`
    to modify objects inside the callback.
- Breaking change: `Gst.ElementFactory.make` and `Gst.Bin.make_and_add` now
  raise `Gst.MissingPluginError` exception when the element is not found.

<a id="csharp"></a>
## GStreamer C# Bindings

- The C# bindings have been updated for the latest GStreamer 1.28 APIs.

<a id="rust"></a>
## GStreamer Rust Bindings and Rust Plugins

The GStreamer Rust bindings and plugins are released separately with a different
release cadence that's tied to the gtk-rs release cycle.

The latest release of the bindings (0.24) has already been updated for the new
GStreamer 1.28 APIs, and works with any GStreamer version starting from 1.14.

`gst-plugins-rs`, the module containing GStreamer plugins written in Rust,
has also seen lots of activity with many new elements and plugins.

The GStreamer 1.28 binaries will be tracking the `main` branch of `gst-plugins-rs`
for starters and then track the 0.15 branch once that has been released (around
the end of February 2026). After that, fixes from newer versions will be
backported as needed into the new 0.15 branch for future 1.28.x bugfix releases.

Rust plugins can be used from any programming language. To applications,
they look just like a plugin written in C or C++.

<a id="rust-elements"></a>
### New Rust elements

- New **icecastsink element with AAC support** that is similar in functionality
  to the existing shout2send element but also supports AAC, which upstream
  libshout is not planning to support.

- New **audio source separation element based on demucs** (see above).

- New **Deepgram speech-to-text transcription** plugin,
  **ElevenLabs voice cloning element** and  **textaccumulate** element. See
  <a href="#stt-translations-and-speech-synthesis">Speech to Text, Translation and Speech Synthesis</a>
  section above.

- New **analytics combiner and splitter elements** for batch metas (see above).

- New **mpa robust RTP depayloader**, **L8/L16/L24** raw audio payloaders and depayloaders and **SMPTE ST291** ancillary data payloader and depayloader.

- New **GIF decoder** element that supports looping.

- New **ST-2038 ancillary data combiner and extractor elements** (see above)

- Added a **burn-based YOLOX inference element and a YOLOX tensor decoder**

- s302mparse: Add new **S302M audio parser**

- New **Rust validate plugin** with a **check-last-frame-qrcode action**.

<!--
<a id="rust-other-improvements"></a>
### Other improvements

- to be filled in
-->

For a full list of changes in the Rust plugins see the
[gst-plugins-rs ChangeLog][rs-changelog] between versions 0.14
(shipped with GStreamer 1.26) and current `main` (soon `0.15`) branch
(shipped with GStreamer 1.28).

Note that at the time of GStreamer 1.28.0 gst-plugins-rs 0.15 was not
released yet and the git `main` branch was included instead (see above).

[rs-changelog]: https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/blob/main/CHANGELOG.md

<a id="build-and-deps"></a>
## Build and Dependencies

- Meson >= 1.4 is now required for all modules

- liborc >= 0.4.42 is strongly recommended

- libnice >= 0.1.23 is now required for the WebRTC library.

- The `closedcaption` plugin in gst-plugins-bad no longer depends on pangocairo after
  removal of the `cc708overlay` element (see above).

- Please also note plugin removals and deprecations.

### Monorepo build

- Updated wraps, incl. glib: cairo, directxmath, expat, fdk-aac, ffmpeg, flac,
  freetype2, gdk-pixbuf, gtest, harfbuzz, json-glib, lame, libjpeg-turbo, libnice,
  libopenjp2, libpng, libsrtp2, libxml2, nghttp2, ogg, pango, pcre2, pygobject,
  soundtoch, sqlite3, wayland-protocols, zlib.

- Added wraps: librsvg, svtjpegxs

<!--

### gstreamer-full

- Nothing major

-->

### Development environment

- Local pre-commit checks via git hooks have been moved over to `pre-commit`,
  including the code indentation check.

- Code indentation checking no longer relies on a locally installed copy of
  GNU indent (which had different outcomes depending on the exact version
  installed). Instead, pre-commit will automatically install the gst-indent-1.0
  indentation tool through pip, which also works on Windows and macOS.

- A pre-commit hook has been added to check documentation cache updates and
  since tags.

- Many meson wrap updates, including to FFmpeg 7.1 (FFmpeg 8.0 is pending)

- The uninstalled development environment should work better on macOS now,
  also in combination with homebrew (e.g. when libsoup comes from homebrew).

- New `python-exe` Meson build option to override the target Python installation
  to use. This will be picked up by the `gst-python` and `gst-editing-sevices`
  subprojects.

<a id="platform-specific"></a>
## Platform-specific changes and improvements

<a id="android"></a>
### Android

- **Overhaul hw-accelerated video codecs detection**:

    - Android 10 (API 29) added support for `isHardwareAccelerated()` to
      `MediaCodecInfo` to detect whether a particular MediaCodec is backed by
      hardware or not. We can now use that to ensure that the video hw-codec
      is rank `PRIMARY+1` on Android, since using a software codec for video is
      simply not feasible most of the time.

    - If we're not able to detect `isHardwareAccelerated()`, perhaps because
      the Android API version is too old, we try to use the codec name as
      a fallback and also rank as `PRIMARY+1` the `c2.android`, `c2.exynos` and
      `c2.amlogic` audio codecs alongside `OMX.google`, because they are known-good.

<a id="apple"></a>
### Apple macOS and iOS

- **VP9 and AV1 hardware-accelerated video decoding** support

- Support for **10-bit HEVC encoding**

- Implement keyboard, mouse, and scroll wheel navigation event
  handling for the OpenGL Cocoa backend.

<a id="windows"></a>
### Windows

<a id="d3d12"></a>
#### GStreamer Direct3D12 integration

- New elements:
    - [d3d12interlace][d3d12interlace]: A Direct3D12 based **interlacing** element
    - [d3d12overlaycompositor][d3d12overlaycompositor]: A Direct3D12-based **overlay composing** element
    - [d3d12fisheyedewarp][d3d12fisheyedewarp]: A Direct3D12-based **fisheye dewarping** element
    - [d3d12remap][d3d12remap]: A Direct3D12-based UV coordinate remapping element

- Upload/download optimisations via a staging memory implementation

- **d3d12swapchainsink** improvements:
    - added a "last-rendered-sample" action signal to retrieve the last rendered frame
    - added "uv-remap" and "redraw" action signals

[d3d12interlace]: https://gstreamer.freedesktop.org/documentation/d3d12/d3d12interlace.html#d3d12interlace
[d3d12overlaycompositor]: https://gstreamer.freedesktop.org/documentation/d3d12/d3d12overlaycompositor.html?gi-language=c
[d3d12fisheyedewarp]: https://gstreamer.freedesktop.org/documentation/d3d12/d3d12fisheyedewarp.html?gi-language=c
[d3d12remap]: https://gstreamer.freedesktop.org/documentation/d3d12/d3d12remap.html?gi-language=c

<a id="win32ipc"></a>
#### Windows inter process communication

- The **Windows IPC plugin gained support for passing generic data** in addition to
  raw audio/video, and various new properties. It also serialises metas now where that
  is supported.

<a id="wasapi2"></a>
#### Windows audio

- **wasapi2**: add **support for dynamic audio device switching, exclusive mode and format negotiation**,
  in addition to device provider improvements and latency enhancements.

- **Disable all audio device providers except wasapi2** by default (by setting the
  others' rank to NONE). We had too many device providers outputting duplicate device
  entries, and it wasn't clear to people what they should be using. After the recent
  device switching work done on WASAPI2, there is no reason to use directsound anymore.

<!--

<a id="linux"></a>
### Linux

- Many improvements which are described in other sections.

-->


<a id="cerbero"></a>
### Cerbero

Cerbero is a meta build system used to build GStreamer plus dependencies on platforms where dependencies are not readily available, such as Windows, Android, iOS, and macOS. It is also used to create the GStreamer Python Wheels.

#### General improvements

- **New features:**

  - Support for generating Python wheels for macOS and Windows
    - These will be uploaded to PyPI, currently [blocked on PyPI](https://github.com/pypi/support/issues/8847)
  - Support for iPhone Simulator on ARM64 macOS, via the new iOS xcframework
  - Inno Setup is now used for Windows installers, which also bundle the MSVC runtime
  - An installer is now shipped for Windows ARM64, built using MSVC
  - GTK4 is now shipped on macOS and Windows (MSVC and MinGW)
  - Smaller binary sizes of Rust plugins on all platforms except macOS and iOS
  - Linux builds now integrate better with system dependencies
  - Debuginfo is now correctly shipped on Windows and macOS

- **API/ABI changes:**

  - Android NDK r25 is now used, targeting API level 24 (Android 7.0)
  - Merge modules are no longer shipped for Windows
  - Windows installers are no longer MSIs
  - The legacy iOS framework with iPhone ARM64 and iPhoneSimulator x86_64 binaries is now **deprecated**. It will be removed in the next release. Please use the new iOS xcframework which supports iPhone ARM64 and iPhoneSimulator ARM64+x86_64.

- **Plugins added:**

  - `pbtypes` is now shipped on all platforms
  - `curl` is now shipped on all platforms except iOS and Android
  - `lcevcdec` is now shipped on all platforms except Windows ARM64 and Windows 32-bit x86
  - `svtjpegxs` is now shipped on Linux and Windows, only on 64-bit
  - `unixfd` is now shipped on all platforms except Windows
  - `mediafoundation` is now shipped additionally on MinGW
  - `wasapi2` is now shipped additionally on MinGW
  - New Rust plugins on all platforms except Windows ARM64:
    - `analytics`
    - `audioparsers`
    - `burn`
    - `demucs`
    - `elevenlabs`
    - `gopbuffer`
    - `hlsmultivariantsink`
    - `icecastsink`
    - `mpegtslive`
    - `raptorq`
    - `speechmatics`
    - `streamgrouper`
    - `vvdec`

- **Plugins changed:**

  - `mp4` and `fmp4` plugins have been merged into `isobmff`

- **Development improvements**:

  - Debuginfo is now correctly shipped on Windows and macOS
  - Support for iPhone Simulator on ARM64 macOS, via the new iOS xcframework

- **Known issues**:

  - [cerbero: Rust plugins fail to link with Xcode 26 on macOS](https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/538)
  - [cerbero: Rust plugins are not shipped in the Windows ARM64 installer](https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/548)
  - [cerbero: Android devices with API level >= 30 cannot play tutorials 4 or 5](https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/529) -- Fix aimed for 1.28.1
  - [cerbero: Missing pkg-config for macOS in the Android release](https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/522)

<a id="docs"></a>
## Documentation improvements

- Added a **Windows section** to "building from source" page

- New **Python tutorials for dynamic pipelines** and time handling

- The **Android tutorials were updated**: provided projects were updated to Gradle 8.11 and API level 24

- Updates of the **Machine Learning and Analytics design documentation** and the **GstMeta design docs**

<a id="breaking-changes"></a>
## Possibly Breaking Changes

- The MXF muxer and demuxer used to have direct support for standalone closed caption streams
  (`closedcaption/x-cea-708`) as ancillary data, but that was removed in favour of more generic
  ST 2038 ancillary metadata which is a better fit for how the data is stored internally and
  also supports generic ancillary metadata. Closed captions can still be stored or extracted
  by using the ST 2038 elements from the Rust plugins module. Also see the MXF section above.

- Analytics: Previously it was guaranteed that there is only ever up to one `GstTensorMeta`
  per buffer. This is no longer true and code working with `GstTensorMeta` must be able to
  handle multiple `GstTensorMeta` now (after this [Merge Request](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9564),
  which was apparently backported into 1.26 as well).

- The thread ID reported in debug logs is no longer prefixed with a `0x` on Windows, Linux and
  FreeBSD platforms. This change can potentially break log parsers. GstDebugViewer was adapted accordingly.

- Python bindings: `Gst.ElementFactory.make` and `Gst.Bin.make_and_add` now
  raise `Gst.MissingPluginError` exception when the element is not found.


<a id="known-issues"></a>
## Known Issues

- There are some [open issues with the Apple hardware-accelerated AV1 decoding][vtdec-av1-issues],
  which we hope will be fixed in due course. Please let us know if you run into them and can
  test patches.

[vtdec-av1-issues]: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4768

- Autoplugging LCEVC H.264/H.265/H.266 streams is currently disabled until [an issue][lcevc-issue]
  with decodebin3 and non-LCEVC streams has been resolved. It is still possible to re-enable this
  locally by overriding the rank of `lcevch26*decodebin` using the `GST_PLUGIN_FEATURE_RANK`
  environment variable.

[lcevc-issue]: https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4870

<a id="statistics"></a>
## Statistics

- 3548 commits
- 1765 Merge requests merged
- 476 Issues closed
- 190+ Contributors

- more than 35% of all commits and Merge Requests were in Rust modules/code

- 5430 files changed
- 395856 lines added
- 249844 lines deleted
- 146012 lines added (net)

<a id="contributors"></a>

## Contributors

Aaron Boxer, Abd Razak, Adrian Perez de Castro, Adrien Plazas,
Albert Sjolund, Aleix Pol, Alexander Slobodeniuk, Alicia Boya García,
Alyssa Ross, Amotz Terem, Amy Ko, Andoni Morales Alastruey,
Andrew Yooeun Chun, Andrey Khamukhin, anonymix007, Arnout Engelen,
Artem Martus, Arun Raghavan, Ben Butterworth, Biswapriyo Nath, Brad Hards,
Brad Reitmeyer, Branko Subasic, Camilo Celis Guzman, Carlos Bentzen,
Carlos Falgueras García, Carlos Rafael Giani,
César Alejandro Torrealba Vázquez, Changyong Ahn, Chengfa Wang,
Christian Gräfe, Christo Joseph, Christopher Degawa, Christoph Reiter,
Daniel Almeida, Daniel Morin, David Maseda Neira, David Monge,
David Smitmanis, Denis Shimizu, Derek Foreman, Detlev Casanova,
Devon Sookhoo, Diego Nieto, Dominique Leroux, DongJoo Kim, Dongyun Seo,
Doug Nazar, Edward Hervey, Ekwang Lee, eipachte, Eli Mallon, Elliot Chen,
Enock Gomes Neto, Enrique Ocaña González, Eric, Eva Pace, F. Duncanh,
François Laignel, Gang Zhao, Glyn Davies, Guillaume Desmottes,
Gustav Fahlen, Haejung Hwang, Haihua Hu, Havard Graff, Hanna Weiß,
He Junyan, Hou Qi, Hyunjun Ko, Ian Napier, Inbok Kim, Jaehoon Lee,
Jakub Adam, James Cowgill, Jan Alexander Steffens (heftig), Jan Schmidt,
Jan Tojnar, Jan Vermaete, Jaslo Ziska, Jeehyun Lee, Jeffery Wilson,
jeongmin kwak, Jeongmin Kwak, Jerome Colle, Jiayin Zhang, Jihoon Lee,
Jochen Henneberg, Johan Sternerup, Jonathan Lui, Jordan Petridis,
Jordan Yelloz, Jorge Zapata, Julian Bouzas, Kevin Scott, Kevin Wolf,
L. E. Segovia, Linus Svensson, Loïc Le Page, Manuel Torres,
Marc-André Lureau, Marc Leeman, Marek Olejnik, Mark Nauwelaerts,
Marko Kohtala, Markus Hofstaetter, Mathieu Duponchelle, Matteo Bruni,
Matthew Semeniuk, Matthew Waters, Max Goltzsche, Mazdak Farzone,
Michael Grzeschik, Michael Olbrich, Michiel Westerbeek, Monty C,
Muhammad Azizul Hazim, Nicholas Jin, Nicolas Dufresne, Nirbheek Chauhan,
Norbert Hańderek, Ognyan Tonchev, Ola Fornander, Olivier Blin,
Olivier Crête, Oz Donner, Pablo García, Patricia Muscalu, Patrick Fischer,
Paul Fee, Paweł Kotiuk, Paxton Hare, Peter Stensson, pfee, Philippe Normand,
Piotr Brzeziński, Piotr Brzeziński, Pratik Pachange, Qian Hu (胡骞),
r4v3n6101Rafael Caricio, Raghavendra Rao, Rares Branici,
Ratchanan Srirattanamet, Razvan Grigore, Rick Ye, Rinat Zeh,
Robert Ayrapetyan, Robert Mader, Ross Burton, Ruben Gonzalez, Ruben Sanchez,
Samuel Thibault, Sanchayan Maity, Santiago Carot-Nemesio, Santosh Mahto,
Sebastian Dröge, Seungha Yang, Shengqi Yu (喻盛琪), Sjoerd Simons,
Slava Sokolovsky, Stefan Andersson, Stefan Dangl, Stéphane Cerveau, stevn,
Sven Püschel, Sylvain Garrigues, Taruntej Kanakamalla, Teus Groenewoud,
Théo Maillart, Thibault Saunier, Tim-Philipp Müller, Tjitte de Wert,
Tobias Schlager, Tobias Koenig, Tomasz Mikolajczyk, Tulio Beloqui,
Val Packett, Vasiliy Doylov, Víctor Manuel Jáquez Leal,
Vincent Beng Keat Cheah, Vineet Suryan, Vivia Nikolaidou, Vivian Lee,
Vivienne Watermeier, Wilhelm Bartel, William Wedler, Wim Taymans,
Xavier Claessens, Yun Liu,

... and many others who have contributed bug reports, translations,
sent suggestions or helped testing. Thank you all!

## Stable 1.28 branch

After the 1.28.0 release there will be several 1.28.x bug-fix releases which
will contain bug fixes which have been deemed suitable for a stable branch,
but no new features or intrusive changes will be added to a bug-fix release
usually. The 1.28.x bug-fix releases will be made from the git 1.28 branch,
which is a stable release series branch.

<a id="1.28.1"></a>

### 1.28.1

The first 1.28 bug-fix release (1.28.1) was released on 26 February 2026.

This release only contains bugfixes and important [security fixes][security].
It *should* be safe to update from 1.28.0 and we recommend you do so at your
earliest convenience.

[security]: https://gstreamer.freedesktop.org/security/

#### Highlighted bugfixes in 1.28.1

 - Various [security fixes][security] and playback fixes
 - Add new whisper-based speech-to-text transcription element
 - Add new debugseimetainserter plugin for testing SEI meta insertion
 - Fix scaling and resizing with UIView on EAGL and Vulkan
 - Reverse playback and gap handling fixes in various components
 - avviddec: Handle field/order changes in mixed interlace mode
 - awstranscriber2: workaround for suspected Rust SDK regression
 - cudaupload, cudadownload: Fix CUDA/GL interop copy path
 - decodebin3: Fix switch to smaller collections and improve collection change on existing pad
 - devenv: Add a subproject for providing the LunarG MoltenVK SDK
 - livesync: fixes and reverse playback handling; ignore upstream latency when upstream is not live
 - objectdetectionoverlay: add support for rotated bounding boxes
 - qml6glsrc: Fix rendering of scene with clipped items
 - speechmatics: allow configuring audio events such as detecting applause, laughter and music
 - livekit webrtc: emit session-requested only for Producer role
 - tsdemux: Fix Continuity Counter handling and handle clock change/resets without skew correction
 - v4l2: Add support for AV1 stateful V4l2 decoder
 - vpxdec: Support downstream pools with alignment requirements
 - vtdec, vtenc: Lots of Apple VideoToolbox decoder and encoder fixes
 - applemedia build improvements, patches for tvOS support, tvos cross file 
 - wavpack: Fix handling of format changes, extend parser with new features, handle non-S32 samples
 - webrtcsink: allow specifying custom headers to signalling server
 - webrtcsink: negotiate profile and level for input encoded in H.264
 - webrtcsrc: add request type pads and allow sending encoded data downstream
 - cerbero: wheel: Add a new `gstreamer_meta` package with fewer deps
 - Various bug fixes, build fixes, memory leak fixes, and other stability and reliability improvements

#### gstreamer

 - [aggregator: Handle gap event before segment](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10789)
 - [aggregator: Various introspection annotations / docs fixes for vfuncs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10723)
 - [basesink: Race condition when pausing can cause `render()` to not block on prerolling but process buffers like in PLAYING state](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4846)
 - [bitwriter: Steal owned data in reset_and_get_data()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10805)
 - [caps: Fix the features leak in gst_caps_append_structure_full](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10689)
 - [filesink: Report write error correctly on Windows](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10758)
 - [gst: Add explanatory comment to call_async implementation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10872)
 - [input-selector: fix several shortcomings](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10621)
 - [multiqueue: reverse playback: use segment stop position as start time](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10770)
 - [registry: Skip .dSYM bundles when loading plugins, try 2](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10853)
 - [typefindfunctions: Promote y4m_typefind](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10632)
 - [gst-stats: Also allow ANSI colored logs without 0x in front of the thread id](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10857)
 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10656)

#### gst-plugins-base

 - [Fix scaling and resizing with UIView on EAGL and Vulkan](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10669)
 - [audiobuffersplit: Various smaller fixes and implement handling of negative rates correctly](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10670)
 - [audiodecoder / videodecoder: Fix gap event handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10750)
 - [compositor: Do copy_metas also for background frame](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10825)
 - [decodebin3: Fix switch to smaller collections](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10674)
 - [decodebin3: Improve handling collection change on existing pad](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10638)
 - [input-selector: fix several shortcomings](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10621)
 - [playsink: unref color balance channels with g_object_unref()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10710)
 - [riff: Correctly check that enough RGB palette data is available](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10892)
 - [rtp: Add mappings for H266 and AV1 encoding-names](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10661)
 - [rtsp: Validate transport parameter parsing in RFC 2326](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10717)
 - [typefindfunctions: Promote y4m_typefind](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10632)
 - [video-converter: Do not transform_metas with 0 width or height](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10833)
 - [videodecoder: Handle recovery from temporary reordered output](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10692)
 - [videofilter: Add VIDEO_ALIGNMENT to downstream pool](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10811)
 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10656)
 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10729)
 
#### gst-plugins-good

 - [qml6glsrc: Fix rendering of scene with clipped items](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10678)
 - [qtdemux: Don't ignore flow return when pushing queued buffers downstream](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10680)
 - [qtdemux: Fix out-of-bounds read when parsing PlayReady DRM UUIDs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10890)
 - [qtdemux: Make sure to not output the same samples multiple times in reverse playback mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10690)
 - [qtdemux: Push raw audio/video buffers downstream in reverse order if rate < 0](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10708)
 - [qtdemux: Set the segment position to the start on EOS in reverse playback mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10629)
 - [rtpqdm2depay: error out if anyone tries to use this element](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10897)
 - [rtpsource: Add locking for receive reports table](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10700)
 - [rtspsrc: Set new mki in the encoder upon crypto update](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10756)
 - [rtspsrc: fix Memory leak in gst_rtspsrc_close() when GST_RTSP_EEOF error occurs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10768)
 - [rtspsrc: Not handling valid RTP-Info headers for RTSP 2.0](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/3064)
 - [v4l2: Add support for AV1 stateful V4l2 decoder](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10664)
 - [vpxdec: Support downstream pools with alignment requirements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10666)
 - [wavpack: Fix handling of format changes, extend parser with new features, handle non-S32 samples in all variations](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10707)
 - [wavparse: Avoid integer overflow and out-of-bounds read when parsing adtl chunks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10903)
 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10729)
 - [meson: Fix libxml2 not building due to wrong option type](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10618)

#### gst-plugins-bad

 - [Fix scaling and resizing with UIView on EAGL and Vulkan](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10669)
 - [GstPlay: fix segmentation fault due to use after free](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10660)
 - [ajasink: Only allow 6 / 8 / 16 audio channels](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10624)
 - [ajasinkcombiner: Only forward the segment events from the video sinkpad](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10748)
 - [analytics: Fix dims_order handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10862)
 - [applemedia: Fix vtenc draining logic, port other existing fixes between vtdec and vtenc](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10860)
 - [applemedia: elements can now be individually registered with gst-full](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10828)
 - [applemedia build improvements, patches for tvOS support, tvos cross file](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10866)
 - [asiosink: Fill silence when paused](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10830)
 - [asio: asiosink can not handle pause event properly and generates noise when paused.](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4909)
 - [audiobuffersplit: Various smaller fixes and implement handling of negative rates correctly](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10670)
 - [audiobuffersplit: fix reverse playback](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10712)
 - [ccconverter: Reset counters on flush-stop](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10845)
 - [cea608mux: fix overflow when calculating output PTS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10726)
 - [codecs: v4l2: Add short and long term ref sets support in v4l2 codecs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10851)
 - [codectimestamper: Fix latency query handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10653)
 - [cudaupload, cudadownload: Fix CUDA/GL interop copy path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10620)
 - [decklink: Explicitly use cpp_std=c++11 for decklink](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10804)
 - [dvbsuboverlay: Add missing bounds checks to the parser everywhere](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10894)
 - [h264,h265ccextractor: Fix framerate in initial caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10841)
 - [h265parser: Validate num_decoding_units_minus1 in pic_timing SEI](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10904)
 - [h266parser: Fix APS ID bounds check in APS parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10899)
 - [h266parser: Fix out of bounds write when parsing pic_timing SEI](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10901)
 - [h266parser: Validate tile index bounds in picture partition parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10898)
 - [jpegparser: boundary checks before copying it](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10896)
 - [mpeghdec: memory leak fix in MPEG-H Audio decoder plugin](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10687)
 - [mpegtspacketizer: Handle clock change/resets without skew correction](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10635)
 - [mxfdemux: always send a segment before sending eos or segment-done](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10650)
 - [mxfdemux: fix gst_mxf_demux_pad_get_stream_time ()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10633)
 - [objectdetectionoverlay: add support for rotated bounding boxes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10671)
 - [openh264enc: remove broken drain and simplify handle_frame](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10739)
 - [tsdemux: Fix Continuity Counter handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10643)
 - [tsmux: reduce noise for DEBUG log level](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10736)
 - [v4l2: Add support for AV1 stateful V4l2 decoder](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10664)
 - [vabasetransform: copy buffer's metadata at copy when import buffer](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10810)
 - [vavp8enc: set color format chroma](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10806)
 - [vkav1dec: fix to set SavedOrderHints properly](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10846)
 - [vtdec: Avoid busy looping when queue length is smaller than DPB size](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10827)
 - [vtdec: Don't re-create session if only the framerate changed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10702)
 - [vtdec: Fix CM memory leak due to incorrect unref](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10794)
 - [vtdec: Fix race condition when negotiating during playback](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10715)
 - [vtdec: Reverse playback fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10654)
 - [vtdec: Seeking to a frame with a simple pipeline causes a hang for some (ProRes only?, MOV-only?) videos in macOS (arm64, x86_64)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4861)
 - [vtdec: Fix wrong DPB size check in the output loop](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10863)
 - [vtenc: Fix DTS offset calculation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10873)
 - [vulkan: load video function pointers conditionally based on codec operation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10773)
 - [wayland: Fix CLAMP operation of maxFALL and maxCLL](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10685)
 - [waylandsink: make gst_wl_window_commit_buffer handle NULL buffers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10793)
 - [webrtc: sink floating refs of ICE transports](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10627)
 - [webrtcbin: Check the presence of encoding-name fields in answer caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10662)
 - [zxing: Fix version check for zxing-cpp 3.0.1](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10813)
 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10656)
 - [meson: Add a subproject for providing the LunarG MoltenVK SDK](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10752)
 - [meson: Fix libxml2 not building due to wrong option type](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10618)
 - [Minimal support for compiling with zxing-cpp 3.x](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10799)
 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10729)
 - [gst-plugins-bad is incompatible with zxing-cpp 3.0.0+](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4893)

#### gst-plugins-ugly

 - [asfdemux: Error out on files with more than 32 streams](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10891)
 - [rmdemux: Check if new video fragment overflows the fragment storage before storing it](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10893)

#### GStreamer Rust plugins

 - [audio: add new whisper-based transcription element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2851)
 - [aws: Don't run whole GStreamer tests in a tokio runtime](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2883)
 - [awstranscriber2: workaround suspected rust SDK regression](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2849)
 - [Block on a tokio runtime if available instead of going via futures::executor()](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2835)
 - [debugseimetainserter: add new plugin for testing SEI meta insertion](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2756)
 - [demucs: document how to build with a specific python version](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2871)
 - [demucs: Don't fail if std deviation of the input is too close to zero](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2872)
 - [demucs: improve url doc](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2868)
 - [demucs: pin python 3.13 in uv setup](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2867)
 - [demucs, speechmatics: don't take stream lock on FlushStart](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2869)
 - [elevenlabs,speechmatics: fix license](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2863)
 - [isobmff: add support for bayer video formats](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2855)
 - [isomp4mux: Improving level indicator in vpcC for vp8/vp9 with helpers in pbutils](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2815)
 - [livesync: fixes & reverse playback handling](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2830)
 - [livesync: ignore upstream latency when upstream is not live](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2877)
 - [polly: don't panic when no caps were received before first buffer](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2879)
 - [polly: fix panic in async send()](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2838)
 - [rtpav1pay: insert sequence header if a keyframe is missing it](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2902)
 - [rtprecv: Don't panic if no buffers of a bufferlist can be directly forwarded](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2844)
 - [rtprecv: Various improvements and bugfixes](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2852)
 - [rtpsend: send mandatory events on the rtcp srcpad before sending the first buffer](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2904)
 - [rtpsmpte291depay: Drop the current packet after processing if it was empty](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2875)
 - [rtp: smpte291: Use upper-case encoding-name](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2865)
 - [rtp: smpte291: Use video as media type instead of application](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2847)
 - [speechmatics: allow configuring audio events](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2837)
 - [st2038combiner: Sort by line and then horizontal offset](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2873)
 - [st2038combiner: Various minor fixes](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2857)
 - [transcriberbin: forward handled error messages as warning messages](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2839)
 - [transcriberbin: fix latency reported when transcriber=translationbin](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2885)
 - [transcribers: Ignore return value when pushing gap events](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2840)
 - [webrtc-api: Report pressed mouse buttons as modifiers](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2860)
 - [webrtc/livekit: emit session-requested only for Producer role](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2807)
 - [webrtcsink: allow specifying custom headers to signalling server](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2882)
 - [webrtcsink: multiple improvements](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2842)
 - [webrtcsink: negotiate profile and level for input encoded in H.264](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2658)
 - [webrtcsrc: add request type pads](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2796)
 - [webrtcsrc: allow sending encoded data downstream](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2786)
 - [cargo_wrapper: Add nasm dir to path only if needed](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2903)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2834)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2853)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2891)

#### gst-libav

 - [audiodecoder / videodecoder: Fix gap event handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10750)
 - [avviddec: Allow stride changes for some decoders](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10663)
 - [avviddec: Fix handling of mixed interlaced content](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10879)
 - [avviddec: Handle field/order changes in mixed interlace mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10696)
 - [avvidcmp: set colorimetry on AVFrame](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10665)
 - [libav: Don't process lines that won't be outputted in the debug log](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10815)
 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10729)

#### gst-rtsp-server

 - [rtspclientsink: don't error out when stream transport notifies timeout](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10864)
 - [rtspclientsink <-> MediaMTX times out when migrating to 1.28](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4911)

#### gstreamer-sharp

 - No changes

#### gst-python

 - [More Python typing fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10625)

#### gst-editing-services

 - [meson: Fix libxml2 not building due to wrong option type](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10618)

#### gst-devtools, gst-validate + gst-integration-testsuites

 - [Update Rust dependencies](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10870)

#### gst-examples

 - [Update Rust dependencies](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10870)

#### gstreamer-docs

 - No changes

#### Development build environment

 - [Add a Meson subproject for providing the LunarG MoltenVK SDK](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10752)
 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10807)
 - [pcre2.wrap: Add patch to fix the build on non-macOS Apple platforms](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10741)
 - [Revert "pygobject: Update to 3.55.0"](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10732)

#### Cerbero build tool and packaging changes in 1.28.1

 - [Add environment variable for building run with background priority](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2137)
 - [Rename vaapi variant to va and by default on Linux, test built plugins correctly on CI](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2135)
 - [fetch: use the Oven for parsing the dependency tree](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2118)
 - [fetch-package` may not fetch all dependencies needed for `package`](https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/125)
 - [filesprovider: Check missing files at the recipe level on CI runs](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2109)
 - [glib: Upstream old iOS GIO_MODULE_DIR hack](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2095)
 - [gtk.recipe: Enable gstreamer media backend on Windows](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2125)
 - [gst-plugins-rs: pin to 0.15 branch for 1.28](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2134)
 - [inno: Fix incorrect MSVC redistributable when cross-building for Arm64](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2096)
 - [inno: Restrict usage to native Windows as exposed by sys.platform](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2119)
 - [libpng: update to 1.6.55](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2101)
 - [osx-framework: Add missing gstreamer libraries](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2129)
 - [osx: fix file duplication in .pkg payloads](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2117)
 - [pygobject.recipe: Fix overflow when comparing GstMessageType on Windows](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2098)
 - [wheel: Add a new gstreamer_meta package with fewer deps](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2097)
 - [wheels: Add proper description / long_description](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2112)
 - [wheels: Don't raise an exception if cli exits with non-zero code](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2128)
 - [wheels: Use static python requires for all wheels](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2124)
 - [wheel: Add debuginfo wheel](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2136)
 - [x265: drop common source recipe](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2123)
 - [Bump up to NDK 29 and pin cpp_std to c++11 in opencore-amr](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2111)
 - [Update to Rust 1.93 / cargo-c 0.10.20](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2094)

#### Contributors to 1.28.1

Adrian Perez de Castro, Andrey Sidorov, Axxel, Carlos Bentzen, Christian Gräfe,
Daniel Morin, Deepa Guthyappa Madivalara, Detlev Casanova, Dominique Leroux,
Edward Hervey, François Laignel, Guillaume Desmottes, He Junyan, Hyunjun Ko,
Jakub Adam, Jan Alexander Steffens (heftig), Jeongmin Kwak, L. E. Segovia,
Mathieu Duponchelle, Matthew Waters, Monty C, Nicolas Dufresne, Nirbheek Chauhan,
Ognyan Tonchev, Peter Stensson, Philippe Normand, Piotr Brzeziński, Rinat Zeh,
Robert Mader, Ruben Gonzalez, Sebastian Dröge, Seungha Yang, Sjoerd Simons,
Stéphane Cerveau, Sven Püschel, Taruntej Kanakamalla, Thibault Saunier,
Tim-Philipp Müller, Tobias Koenig, Víctor Manuel Jáquez Leal, Vivia Nikolaidou,
Xabier Rodriguez Calvar, Xavier Claessens, Xi Ruoyao,

... and many others who have contributed bug reports, translations, sent
suggestions or helped testing. Thank you all!

#### List of merge requests and issues fixed in 1.28.1

- [List of Merge Requests applied in 1.28.1](https://gitlab.freedesktop.org/groups/gstreamer/-/merge_requests?scope=all&utf8=%E2%9C%93&state=merged&milestone_title=1.28.1)
- [List of Issues fixed in 1.28.1](https://gitlab.freedesktop.org/groups/gstreamer/-/issues?scope=all&utf8=%E2%9C%93&state=closed&milestone_title=1.28.1)

## Schedule for 1.30

Our next major feature release will be 1.30, and 1.29 will be the unstable
development version leading up to the stable 1.30 release. The development
of 1.29/1.30 will happen in the git `main` branch of the GStreamer mono
repository.

The schedule for 1.30 is still to be determined, but it will likely be
in Q4/2026.

1.30 will be backwards-compatible to the stable 1.28, 1.26, 1.24, 1.22, 1.20,
1.18, 1.16, 1.14, 1.12, 1.10, 1.8, 1.6, 1.4, 1.2 and 1.0 release series.

<a id="1.27.1"></a><a id="1.27.2"></a><a id="1.27.50"></a><a id="1.27.90"></a>
## 1.27 pre-releases (superseded by 1.28)

<!-- Add some anchors for the old pre-releases since anchors are handled
     client side so we can't add server side redirects to the 1.27 page.
  -->

- [1.27.1 development snapshot release notes][rn-1.27.1]
- [1.27.2 development snapshot release notes][rn-1.27.2]
- [1.27.50 development snapshot release notes][rn-1.27.50]
- [1.27.90 pre-release release notes][rn-1.27.90]

[rn-1.27.1]: https://gstreamer.freedesktop.org/releases/1.27/#1.27.1
[rn-1.27.2]: https://gstreamer.freedesktop.org/releases/1.27/#1.27.2
[rn-1.27.50]: https://gstreamer.freedesktop.org/releases/1.27/#1.27.50
[rn-1.27.90]: https://gstreamer.freedesktop.org/releases/1.27/#1.27.90


- - -

*These release notes have been prepared by Tim-Philipp Müller with contributions from Daniel Morin, Nirbheek Chauhan, Philippe Normand, Sebastian Dröge, Thibault Saunier, Víctor Manuel Jáquez Leal, and Xavier Claessens*

*License: [CC BY-SA 4.0](http://creativecommons.org/licenses/by-sa/4.0/)*
