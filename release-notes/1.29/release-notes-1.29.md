# GStreamer 1.29.x Release Notes

GStreamer 1.29.x is an API/ABI-unstable development series leading up to the stable 1.30 series.

The latest unstable development snapshot in the 1.29 series is [1.29.1](#1.29.1) and was released on 22 March 2026.

<a id="1.29.1"></a>

### 1.29.1

The first API/ABI-unstable 1.29.x development snapshot release (1.29.1) was released on 22 March 2026.

Any newly-added API in the 1.29.x series may still change or be removed again before 1.30 and should be considered unstable until 1.30 is released.

The 1.29.x release series is for testing and development purposes, and distros should probably not package it.

#### Highlighted changes in 1.29.1

 - ac4parse: New basic AC-4 parser element, plus AC-4 typefinding
 - analytics: New GstAnalyticsMtd derivative to represent grouping of Mtd's and Keypoint
 - analytics: Added a hand tracking tensor decoder element
 - Parse HDR10+ metadata out of H.265 and AV1 bitstreams
 - Matroska demuxer: Can build a dynamic seek index now if needed
 - New h264seiinserter and h265seiinserter elements that support both closed captions and unregistered user data SEIs
 - Add HLS WebVTT sink element to the hlssink3 plugin
 - New DASH sink element that uses CMAF muxer without splitmuxsink
 - New plugin for general purpose compress/decompress
 - New udpsrc2 element with better performance for high bitrate streams
 - New VA-API overlay compositor
 - Opus audio support for F32 and S24_32 samples and 96kHz sample rate
 - Playbin3 subtitle switching fixes
 - Bump ranks of the new Rust RTP (de)payloaders to PRIMARY and default to mtu 1200 for payloaders
 - rtspsrc2 authentication support
 - GstPlay track selection notification improvements
 - QML6 GL Source now supports navigation events
 - QuickTime demuxer gained Bayer support
 - Splitmuxsink now includes the start and end timecodes in fragment-opened and closed messages
 - srtpdec gained a way to invalidate keys for a specific SSRC
 - The APE tag demuxer can extract cover art tags now
 - translationbin can control the textaccumulate latency now via a new property
 - Allow device providers rank override using GST_PLUGIN_FEATURE_RANK
 - cerbero gained support for Android on RISC-V64
 - Countless bug fixes, build fixes, memory leak fixes, and other stability and reliability improvements

#### gstreamer

 - [Allow device providers rank override using GST_PLUGIN_FEATURE_RANK](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11035)
 - [basesink: Unset have_preroll if not actually waiting for preroll](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10530)
 - [debugutils: add tooltip with full params to dot graph elements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10511)
 - [dots: add features parameter to control pipeline-snapshot composition](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10855)
 - [logging: improve performance by reducing allocations in __gst_vasnprintf](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10785)
 - [structure/tracer: Some simple UX enhancements to set tracer properties](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10826)
 - [tracer: add object-parent-set hook](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10814)
 - [validateflow: auto-derive directories from test file path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10962)

Backported into 1.28:

 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10637)
 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [aggregator: Handle gap event before segment (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10787)
 - [aggregator: Various introspection annotations / docs fixes for vfuncs (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10720)
 - [baseparse, Preserve upstream buffer duration if possible (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10672)
 - [baseparse: Fix out_buffer leak in frame_free and missing ref in frame_copy (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11046)
 - [bin: iterator is not nullable (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11007)
 - [bitwriter: Steal owned data in reset_and_get_data() (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10802)
 - [caps: Fix the features leak in gst_caps_append_structure_full (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10605)
 - [filesink: Fix wrong open() in overwrite mode (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10988)
 - [filesink: Report write error correctly on Windows (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10754)
 - [ges: Plug all leaks reported running valgrind on our testsuite (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10784)
 - [gst-stats: Also allow ANSI colored logs without 0x in front of the thread id (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10854)
 - [gst: Add explanatory comment to call_async implementation (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10871)
 - [input-selector: fix several shortcomings (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10507)
 - [multiqueue: reverse playback: use segment stop position as start time (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10764)
 - [registry: Skip .dSYM bundles when loading plugins, try 2 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10850)
 - [registry: Skip .dSYM bundles when loading plugins, try 3 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10964)
 - [typefindfunctions: Promote y4m_typefind (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10542)

#### gst-plugins-base

 - [Parse HDR10+ metadata out of H.265 and AV1 bitstreams](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9668)
 - [video-hdr: HDR10plus signalling by using only blob data might be suboptimal](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4725)
 - [audiobasesink: Fix rounding when calculating render start/stop](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10755)
 - [gl: cocoa: Handle non-Cocoa windows in CGL context creation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10564)
 - [gst-env.py: Keep a simple wrapper for meson devenv](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9802)
 - [opusenc/dec: Add support for F32 / S24_32 samples](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10423)
 - [opusenc/dec: Support 96kHz sample rate](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10425)
 - [playbin3: Reproducible deadlock switching subtitle tracks in MKV containers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4744)
 - [playsink: don't wait for text pad block during reconfiguration](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10606)
 - [pbutils, qtmux, qtdemux, vtdec, vp9parse: vp9 improvements to unify vpcC creation and level handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10538)
 - [pbutils: Remove fixed caps check when not strictly necessary](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10856)
 - [typefind: Add AC-4 support](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10267)
 - [validateflow: auto-derive directories from test file path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10962)

Backported into 1.28:

 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10637)
 - [Fix scaling and resizing with UIView on EAGL and Vulkan (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10556)
 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [Various element factory metadata fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [audiobuffersplit: Various smaller fixes and implement handling of negative rates correctly (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10568)
 - [audiodecoder / videodecoder: Fix gap event handling (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10738)
 - [compositor: Do copy_metas also for background frame (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10824)
 - [compositor: move gst_compositor_init_blend() to element class_init (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11078)
 - [decodebin3: Fix switch to smaller collections (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10583)
 - [decodebin3: Improve handling collection change on existing pad (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8700)
 - [glcolorconvert: Fix NULL pointer dereference on buffers without video meta (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10939)
 - [glupload: Fix linking glupload with restrictive caps filter (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11029)
 - [input-selector: fix several shortcomings (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10507)
 - [opusenc: Use correct memcpy() size when copying Vorbis channel positions (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10982)
 - [playsink: unref color balance channels with g_object_unref() (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10701)
 - [riff: Correctly check that enough RGB palette data is available (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10882)
 - [rtcp: Fix buffer overread in SDES packet parsing (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10730)
 - [rtp: Add mappings for H266 and AV1 encoding-names (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10649)
 - [rtpbuffer: Add validation for CSRC list length (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10956)
 - [rtsp: Validate transport parameter parsing in RFC 2326 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10500)
 - [typefindfunctions: Promote y4m_typefind (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10542)
 - [video-converter: Do not transform_metas with 0 width or height (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10832)
 - [video-converter: fix I420/A420 BGRA/ARGB output on big-endian (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11013)
 - [video: fix too small default stride for UYVP with odd widths (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11033)
 - [videodecoder: Handle recovery from temporary reordered output (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10558)
 - [videofilter: Add VIDEO_ALIGNMENT to downstream pool (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10808)
 - [videorate: Fix unrestored caps on backward PTS (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11020)

#### gst-plugins-good

 - [Bayer support for qtdemux (mp4/uncv)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10658)
 - [apedemux: Add support for cover art tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10603)
 - [av1dec, vpxdec, openh264dec, jpegdec: Streamline and simplify decide_allocation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10869)
 - [deinterlace: Allow allocation queries to pass through in mixed mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10579)
 - [dtmfsrc: expose properties for controlling minimum durations](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10334)
 - [flvdemux: Make no-more-pads threshold configurable](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11042)
 - [flvmux: Timestamp outgoing buffers from mux, not incoming buffer](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/1034)
 - [gst-plugins-good: Fix unused parameter warnings in alphacolor](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10600)
 - [level: Port from GstBaseTransform to GstAudioFilter](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10461)
 - [matroska-demux: parse TrackLanguageBcp47 tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10766)
 - [matroskademux: Build dynamic seek index if needed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10642)
 - [matroska: Read and write vp9 CodecPrivate to assist initial decoder config](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10875)
 - [pbutils, qtmux, qtdemux, vtdec, vp9parse: vp9 improvements to unify vpcC creation and level handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10538)
 - [qml6glsrc: Add support to handle navigation events](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10733)
 - [qml6glsrc: Simplify implementation of Qt6GLWindow](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10573)
 - [qt6: fixed build for Android](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10781)
 - [qtdemux: Handle 'pict' tracks as if they were video tracks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10688)
 - [qtmux: include gstqtmux-doc.c in doc_sources](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10763)
 - [rgvolume: don't apply dBSPL reference level compensation for LUFS values (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10997)
 - [rtp: Lower Opus (de)payloader ranks to SECONDARY](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10940)
 - [rtspsrc: add backchannel-http-method property for HTTP tunnel mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10999)
 - [rtspsrc: Correctly parse RTSP 2.0 RTP-Info headers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8571)
 - [splitmuxsink: Include start/end timecodes in fragment-opened/closed messages](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10420)
 - [srtpdec: Add API for invalidating keys for a specific SSRC](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10980)
 - [test-client-managed-mikey: Allow user to select cipher](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10745)
 - [test-client-managed-mikey: Fix parsing of auth](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10783)
 - [wavparse: Add support for ID3 tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10610)

Backported into 1.28:

 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [Qt6GLVideoItem: caps update fixed (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10959)
 - [Various element factory metadata fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [gstrtspsrc: Set new mki in the encoder upon crypto update (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10744)
 - [hlsdemux2: fix seekable range for live HLS streams (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11044)
 - [meson: Fix libxml2 not building due to wrong option type (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10615)
 - [qml6glsrc: Fix rendering of scene with clipped items (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10572)
 - [qtdemux: Don't ignore flow return when pushing queued buffers downstream (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10570)
 - [qtdemux: Don't immediately push segment after moov in push mode for fmp4 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11014)
 - [qtdemux: Fix handling of in-between fragments without tfdt (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10590)
 - [qtdemux: Fix invalid WebVTT timestamps (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11017)
 - [qtdemux: Fix out-of-bounds read when parsing PlayReady DRM UUIDs (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10880)
 - [qtdemux: Make sure to not output the same samples multiple times in reverse playback mode (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10548)
 - [qtdemux: Push raw audio/video buffers downstream in reverse order if rate < 0 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10571)
 - [qtdemux: Set the segment position to the start on EOS in reverse playback mode (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10534)
 - [rtph264depay: fix invalid memory access in gst_rtp_h264_finish_fragmentation_unit (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10951)
 - [rtpqdm2depay: error out if anyone tries to use this element (and remove it) (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10886)
 - [rtpsource: Add locking for receive reports table (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10683)
 - [rtptwcc: fix feedback packet count wrapping at 255 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10920)
 - [rtspsrc: fix Memory leak in gst_rtspsrc_close() when GST_RTSP_EEOF error occurs (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10759)
 - [v4l2: Add support for AV1 stateful V4l2 decoder (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9892)
 - [vpxdec: Support downstream pools with alignment requirements (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10442)
 - [wavenc: Skip writing empty LIST INFO chunk (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11049)
 - [wavpack: Fix handling of format changes, extend parser with new features, handle non-S32 samples in all variations (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10496)
 - [wavparse: Avoid integer overflow and out-of-bounds read when parsing adtl chunks (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10900)
 - [wavparse: Avoid overflow in length when setting ignore-length=true (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11004)
 - [wavparse: Fix parsing of RF64 wave files (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11008)

#### gst-plugins-bad

 - [Parse HDR10+ metadata out of H.265 and AV1 bitstreams](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9668)
 - [VA-API overlay compositor](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10365)
 - [amfcodec: Add Linux support](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10761)
 - [amfcodec: fix build for platforms other than Windows and Linux](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10878)
 - [amfcodec: update the SDK headers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11064)
 - [analytics: adding group mtd and keypoint mtd](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10551)
 - [av1dec, vpxdec, openh264dec, jpegdec: Streamline and simplify decide_allocation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10869)
 - [av1parse: now adds av1c in caps codec_data](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10704)
 - [avfassetsrc: Replace file:// URI support with avf+file://](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10816)
 - [closedcaption: Add h264seiinserter and h265seiinserter elements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10367)
 - [decklink: Fix timecode handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10772)
 - [doc: cleanup gst-launch comment](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10777)
 - [doc: correct inference elements doc](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11037)
 - [mxfdemux: fix uninitialized warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10910)
 - [mxfdemux: handle reverse playback in pull mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10407)
 - [pbutils, qtmux, qtdemux, vtdec, vp9parse: vp9 improvements to unify vpcC creation and level handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10538)
 - [play: Add new tracks-selected message as signal to the signal adapter](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10719)
 - [play: Add new tracks-selected message to notify about track selections having happened](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10509)
 - [rtmp2: Don't retry on G_IO_ERROR_TIMED_OUT](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8989)
 - [srtpdec: Add API for invalidating keys for a specific SSRC](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10980)
 - [rtsp: gstrtspurl: Parse URL having user without password (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10847)
 - [tests: add vulkan av1 decode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10725)
 - [tests: fix vp9 vulkan decode test on radv](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10919)
 - [timecodestamper: Add timecode source mode based on ST12-2/3 ancillary meta](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10774)
 - [vah264dec: Set VA_PICTURE_H264_NON_EXISTING](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10549)
 - [validateflow: auto-derive directories from test file path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10962)
 - [vkh264enc: fix level if required in new_sequence() and avoid renegotiation if possible](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10226)
 - [vkvideo: move dedicated DPB detection after capability query](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10967)
 - [vulkan: add H.26X GOP mapper utility for video encoding](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10523)
 - [vulkan: fix grammar of function _has_feature_timeline_semaphore()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10737)
 - [vulkan: ignore Setting-Limit-Adjusted validation layer warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11022)
 - [vulkan: Expose Vulkan physical device features and properties](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10351)
 - [webrtc: Populate certificate stats](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10313)
 - [webrtcbin: Fill crypto-related transport stats](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10472)

Backported into 1.28:

 - [analytics: Set default pixel-aspect-ratio for inference elements (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10987)
 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10637)
 - [Fix dims_order handling (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10861)
 - [Fix scaling and resizing with UIView on EAGL and Vulkan (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10556)
 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [GstPlay: fix segmentation fault due to use after free (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10647)
 - [Minimal support for compiling with zxing-cpp 3.x (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10790)
 - [Various element factory metadata fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [ajasink: Only allow 6 / 8 / 16 audio channels (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10521)
 - [ajasinkcombiner: Only forward the segment events from the video sinkpad (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10747)
 - [applemedia: Fix vtenc draining logic, port other existing fixes between vtdec and vtenc (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10849)
 - [applemedia: elements can now be individually registered with gst-full (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10780)
 - [asiosink: Fill silence when paused (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10823)
 - [audiobuffersplit: Various smaller fixes and implement handling of negative rates correctly (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10568)
 - [audiobuffersplit: fix reverse playback (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10705)
 - [av1dec: Enable VIDEO_META and VIDEO_ALIGNMENT for pool (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10865)
 - [av1parse:  split the alignment and stream type logic (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10868)
 - [av1parse: Misc fixes 2 typo (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11001)
 - [av1parse, vp9parse: Remove segment clipping to let downstream handle frame boundaries (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11081)
 - [baseparse, h264parse, h265parse: Preserve upstream buffer duration if possible (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/ - [baseparse, h264parse, h265parse: Preserve upstream buffer duration if possible (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10672)
 - [ccconverter: Reset counters on flush-stop (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10842)
 - [cea608mux: fix overflow when calculating output PTS (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10706)
 - [codecs: v4l2: Add short and long term ref sets support in v4l2 codecs -- stable ABI (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10820)
 - [codectimestamper: Fix latency query handling (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10588)
 - [cudaupload, cudadownload: Fix CUDA/GL interop copy path (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10495)
 - [dashsink: test: use playbin3 for DASH playback verification (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10834)
 - [decklinkvideosink: fix element leak in decklink callback (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11006)
 - [dtls: unregister signal handlers from connection (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11054)
 - [dvbsuboverlay: Add missing bounds checks to the parser everywhere (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10884)
 - [gdppay: Fix null pointer dereference on duplicated caps event (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10911)
 - [h264,h265ccextractor: Fix framerate in initial caps (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10840)
 - [h264parse: Fix framerate calculation and interlaced features (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8310)
 - [h264parser: Fix memory leak in gst_h264_parser_parse_nal() (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11052)
 - [h265parser: Validate num_decoding_units_minus1 in pic_timing SEI (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10902)
 - [h266parser: Fix APS ID bounds check in APS parsing (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10888)
 - [h266parser: Fix out of bounds write when parsing pic_timing SEI (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10889)
 - [h266parser: Validate tile index bounds in picture partition parsing (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10887)
 - [libs: jpegparser: boundary checks before copying it (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10885)
 - [meson: Add a subproject for providing the LunarG MoltenVK SDK (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10743)
 - [meson: Explicitly use cpp_std=c++11 for decklink (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10801)
 - [meson: Fix downloading MoltenVK SDK, make it work when meson-installed (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10797)
 - [meson: Fix libxml2 not building due to wrong option type (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10615)
 - [mpeghdec: memory leak fix in MPEG-H Audio decoder plugin (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10593)
 - [mpegtspacketizer: Handle clock change/resets without skew correction (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10559)
 - [mxfdemux: always send a segment before sending eos or segment-done (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10569)
 - [mxfdemux: fix gst_mxf_demux_pad_get_stream_time () (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10550)
 - [nvcodec: Add capability caching to speed up plugin initialization (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10339)
 - [objectdetectionoverlay: add support for rotated bounding boxes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10552)
 - [openh264enc: remove broken drain and simplify handle_frame (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10589)
 - [soundtouch: Only allow up to 192kHz and 16 channels (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11000)
 - [srtpenc: preserve ROC when master key is updated for an ongoing session (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10969)
 - [svtav1: fix "Level of parallelism" property type discrepencies (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10836)
 - [tsdemux: Fix Continuity Counter handling (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10560)
 - [tsmux: reduce noise for DEBUG log level (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10474)
 - [tsmux: Fix integer overflow in SCTE35 NULL interval (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11066)
 - [v4l2: Add support for AV1 stateful V4l2 decoder (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9892)
 - [vabasetransform: copy buffer's metadata at copy when import buffer (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10800)
 - [vavp8enc: set color format chroma (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10803)
 - [vkav1dec: fix to set SavedOrderHints properly (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10829)
 - [vtdec: Avoid busy looping when queue length is smaller than DPB size (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10818)
 - [vtdec: Don't re-create session if only the framerate changed (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10686)
 - [vtdec: Fix race condition when negotiating during playback (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10679)
 - [vtdec: Fix wrong DPB size check in the output loop (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10858)
 - [vtdec: Reverse playback fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10613)
 - [vtenc: Fix DTS offset calculation (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9097)
 - [vtdec: Store supplemental codec support in a global variable (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11018)
 - [vulkan: load video function pointers conditionally based on codec operation (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10771)
 - [vulkan: Clear mutex when GstVulkanImageMemory is freed (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11047)
 - [wayland: Fix CLAMP operation of maxFALL and maxCLL (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10585)
 - [wayland: display: Add protection when replacing wl_output (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11002)
 - [waylandsink: make gst_wl_window_commit_buffer handle NULL buffers (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10786)
 - [webrtc: sink floating refs of ICE transports (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10532)
 - [webrtcbin: Check the presence of encoding-name fields in answer caps (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10639)
 - [zxing: Fix version check for zxing-cpp 3.0.1 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10812)

#### gst-plugins-ugly

 - No changes that haven't been backported

Backported into 1.28:

 - [asfdemux: Error out on files with more than 32 streams (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10881)
 - [rmdemux: Check if new video fragment overflows the fragment storage before storing it (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10883)

#### GStreamer Rust plugins

 - [ac4parse: Basic AC-4 parser element creation](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2919)
 - [analytics: Add a hand tracking tensor decoder element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2870)
 - [compress: adding deflate algo support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2908)
 - [compress: Adding brotli support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2933)
 - [dashsink2: add new DASH sink element that uses CMAF muxer without splitmuxsink](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2186)
 - [flate: adding flate, for general purpose compress/decompress](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2889)
 - [hlssink3: Add HLS WebVTT sink element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2890)
 - [png: implement image repacking when buffer is padded (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2962)
 - [rtp: Bump ranks of new Rust (de)payloaders to PRIMARY](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2918)
 - [rtp: basepay2: default to lower MTU of 1200 instead of 1400](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2785)
 - [rtprecv: Add `add-reference-timestamp-meta` property](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2953)
 - [speechmatics: Fix infinite reconnection on discont buffer](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2927)
 - [transcriberbin: handle error messages fully asynchronously](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2895)
 - [translationbin: expose property to control textaccumulate latency](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2924)
 - [udp: Add new plugin with udpsrc2](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2862)
 - [udpsrc2: handle ICMP errors](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2946)
 - [webrtcsink: add support for qtic2venc encoder](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2913)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2891)
 - [gstwebrtc-api: fix example offer options](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/1886)
 - [Add explanation to the README.md why gst-plugins-rs is a separate repository](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2959)

Backported into 0.15:

 - [burn: yoloxinference: Restrict widths/heights to a multiple of 32 (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2966)
 - [cargo_wrapper: Add nasm dir to path only if needed (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2897)
 - [Don't transform push_event() false returns into flow errors (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2911)
 - [fallbacksrc: Send select-streams event to collection source element (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2907)
 - [gst-plugins-rs: only add example features when dependencies are found (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2824)
 - [gtk4paintablesink: Error out in NULL->READY if there is no default GDK display (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2930)
 - [rtpav1pay: insert sequence header if a keyframe is missing it (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2888)
 - [rtpbin2: don't panic in Drop impl (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2948)
 - [rtpsend: send mandatory events on the rtcp srcpad before sending the first buffer (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2894)
 - [rtspsrc2: Implement authentication support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2957)
 - [speechmatics: fix first_buffer_pts race condition in dispatch_message (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2898)
 - [speechmatics, textaccumulate: fix flushing issues (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2926)
 - [threadshare: examples: add rtpbin2 send / recv (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2938)
 - [threadshare: fix socket leak in ts-udpsink (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2760)
 - [threadshare: udpsink/src: don't error out failing to send packet to a client / receiving an ICMP error (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2928)
 - [Update dependencies (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2916)
 - [webrtc: Silence new clippy warning (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2934)
 - [whisper: update to latest release 0.16 (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2951)

#### gst-libav

 - No changes that haven't been backported

Backported into 1.28:

 - [Various element factory metadata fixes (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [audiodecoder / videodecoder: Fix gap event handling (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10738)
 - [avviddec: Allow stride changes for some decoders (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8963)
 - [avviddec: Fix handling of mixed interlaced content (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9956)
 - [avviddec: Handle field/order changes in mixed interlace mode (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10578)
 - [gst-libav: avvidcmp: set colorimetry on AVFrame (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10090)
 - [libav: Don't process lines that won't be outputted (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10557)

#### gst-rtsp-server

 - No changes that haven't been backported

Backported into 1.28:

 - [rtsp-client: Lock media when unlinking session medias (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11040)
 - [rtsp-stream: Clear send_thread when it's freed (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10844)
 - [rtspclient: expose property to control error posting on RTCP timeout (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10819)

#### gstreamer-sharp

 - [gstreamer-sharp: update for latest API additions and introspection annotation changes in 1.29.0.1](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11073)

#### gst-python

 - [analytics: adding group mtd and keypoint mtd](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10551)

Backported into 1.28:

 - [Python typing (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10526)
 - [bin: iterator is not nullable (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11007)

#### gst-editing-services

 - [ges-launcher: fix crash when error message has no source](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11093)
 - [validate: Cleanup media and scenarios and add documentation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10537)
 - [validateflow: auto-derive directories from test file path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10962)

Backported into 1.28:

 - [Plug all leaks reported running valgrind on our testsuite (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10784)
 - [Remove spurious python-embed dependency from libges (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11065)
 - [meson: Fix libxml2 not building due to wrong option type (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10615)

#### gst-devtools, gst-validate + gst-integration-testsuites

 - [gst-validate-launcher: Fix --help for Python 3.14](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10408)
 - [validate: Cleanup media and scenarios and add documentation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10537)
 - [validate: support .media_info files in the main repo](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10791)
 - [validateflow: auto-derive directories from test file path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10962)
 - [validate-launcher: don't generate tests for skipped media info files after updating them](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10831)
 - [validate: Cleanup media and scenarios and add documentation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10537)
 - [validate: launcher: fix total time spent when re-running flaky tests](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10839)
 - [validate: support .media_info files in the main repo](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10791)
 - [validateflow: auto-derive directories from test file path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10962)

Backported into 1.28:

 - [Update Rust dependencies (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10867)

#### gstreamer-docs

 - [doc: sync cerbero doc with last system-recipe change](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8428)
 - [doc: add design doc for generic-raw-format](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10607)

Backported into 1.28:

 - [tutorials/android: bump up ndkVersion to 29 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10782)

#### Development build environment

 - [Add release notes snippets directory](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10604)
 - [vscode: avoid to track launch.json](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8009)
 - [gst-env.py: Don't change CWD when entering the devenv](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10876)
 - [gst-env.py: Keep a simple wrapper for meson devenv](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9802)
 - [macos: enforce meson >= 1.8.3 for Xcode 26+](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9813)
 - [wraps: update ffmpeg to 7.1.1, cairo to 1.18.4](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8627)
 - [wraps: bump libvpx to 1.15](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10848)

Backported into 1.28:

 - [Add a subproject for providing the LunarG MoltenVK SDK (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10743)
 - [Fix downloading MoltenVK SDK, make it work when meson-installed (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10797)
 - [wraps: update libxml2 to v2.15.2 (backported)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10974)

#### Cerbero build tool and packaging changes in 1.29.1

 - [Adding Support for Android on RISC-V64](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/1739)
 - [build-tools: remove gtk-doc-lite recipe](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2145)
 - [cerbero: fixes for background build setup](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2143)
 - [hacks: Remove xml hack in favor of etree.indent](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/1673)
 - [system_recipes: drop arch assert](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2108)
 - [x265: unify all recipes in a single file](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2126)
 - [oven: do not mark skipped recipes as built](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/1820)

Backported into 1.28:

 - [Add tvOS support (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2080)
 - [Ensure there's no gstreamer in the artifact cache (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2144)
 - [Rename vaapi variant to va and by default on Linux, test built plugins correctly on CI (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2131)
 - [Update to Rust 1.93 / cargo-c 0.10.20 (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2071)
 - [Update to Rust 1.94 and cargo-c 0.10.21 (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2156)
 - [build: include plugin's .pc file in Linux and macOS (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2154)
 - [bump up to NDK 29 and pin cpp_std to c++11 in opencore-amr (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2090)
 - [cerbero: Use static python requires for all wheels (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2116)
 - [cerbero: add environment variable for building run with background priority (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2082)
 - [cerbero: os.PRIO_DARWIN_PROCESS is only available on Python 3.12+ (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2142)
 - [fetch: use the Oven for parsing the dependency tree (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2115)
 - [filesprovider: Check missing files at the recipe level on CI runs (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2068)
 - [glib.recipe: Upstream old iOS GIO_MODULE_DIR hack (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2073)
 - [gtk.recipe: Enable gstreamer media backend on Windows (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2121)
 - [gtk: Enable GStreamer media backend on Windows](https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/556)
 - [inno: assorted fixes for Registry key handling (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2170)
 - [inno: Fix incorrect MSVC redistributable when cross-building for Arm64 (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2081)
 - [inno: Restrict usage to native Windows as exposed by sys.platform (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2083)
 - [inno: fix environment variable being created outside SessionManager/Environment (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2152)
 - [libpng: update to 1.6.55 (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2100)
 - [libsoup: update to 3.6.6 (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2150)
 - [libsrtp: update to v2.8.0 (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2167)
 - [osx-framework: Add missing gstreamer libraries (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2122)
 - [osx: fix file duplication in .pkg payloads (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2113)
 - [packaging: Fix missing devel payloads for gstreamer-1.0-python (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2171)
 - [pygobject.recipe: Fix overflow when comparing GstMessageType on Windows (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2089)
 - [recipe: do not run symbolication if nodebug (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2149)
 - [wheel: Add a new gstreamer_meta package with fewer deps (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2087)
 - [wheel: Add debuginfo wheel (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2077)
 - [wheels: Add a new meta-package 'gstreamer_bundle' (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2103)
 - [wheels: Add proper description / long_description (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2110)
 - [wheels: Don't raise an exception if cli exits with non-zero code (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2120)
 - [x265: drop common source recipe (backported)](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2114)

#### Contributors to 1.29.1

Adrian Perez de Castro, Albert Sjölund, Alicia Boya García, Amyspark,
Andoni Morales Alastruey, Andrey Sidorov, ArokyaMatthewNathan, axxel,
Azat Nurgaliev, Carlos Bentzen, Charles, Chris Bainbridge, Christian Gräfe,
Cole Richardson, Daniel Morin, David Rosca, Deepa Guthyappa Madivalara,
Detlev Casanova, Dominique Leroux, Edward Hervey, Fabian Orccon,
François Laignel, freedesktop, Guillaume Desmottes, He Junyan, Hou Qi,
Hyunjun Ko, Jakub Adam, James Liu, Jan Alexander Steffens (heftig),
Jan Schmidt, jeongmin kwak, Jeremy Whiting, Jorge Zapata, László Károlyi,
L. E. Segovia, Mao Han, Marko Kohtala, Martin Rodriguez Reboredo,
Mathieu Duponchelle, Matthew Waters, Mattia, Matus Gajdos, Monty C,
Nicolas Dufresne, Nirbheek Chauhan, Ognyan Tonchev, Olivier Crête,
Pablo García, Peter Stensson, Philippe Normand, Piotr Brzeziński,
Rinat Zeh, Robert Mader, Roberto Viola, Ruben Gonzalez, Sanchayan Maity,
Sebastian Dröge, Sergey Radionov, Seungha Yang, Shigeharu Kamiya,
Sjoerd Simons, Stéphane Cerveau, Sven Püschel, Taruntej Kanakamalla,
Thibault Saunier, Tim-Philipp Müller, Tobias Koenig, Tobias Rapp,
Tobias Schlager, Víctor Manuel Jáquez Leal, Vitaly Vlasov,
Vivia Nikolaidou, Vivienne Watermeier, Wojciech Kapsa,
Xabier Rodriguez Calvar, Xavier Claessens, Xi Ruoyao,

... and many others who have contributed bug reports, translations, sent
suggestions or helped testing. Thank you all!

#### List of merge requests and issues fixed in 1.29.1

- [List of Merge Requests applied in 1.29.1](https://gitlab.freedesktop.org/groups/gstreamer/-/merge_requests?scope=all&utf8=%E2%9C%93&state=merged&milestone_title=1.29.1)
- [List of Issues fixed in 1.29.1](https://gitlab.freedesktop.org/groups/gstreamer/-/issues?scope=all&utf8=%E2%9C%93&state=closed&milestone_title=1.29.1)

## Schedule for 1.30

Our next major feature release will be 1.30, and 1.29.x is the unstable
development series leading up to the stable 1.30 release. The development
of 1.29/1.30 will happen in the git `main` branch of the GStreamer mono
repository.

The schedule for 1.30 is yet to be decided, but we're targetting Q4/2026.

1.30 will be backwards-compatible to the stable 1.28, 1.26, 1.24, 1.22, 1.20,
1.18, 1.16, 1.14, 1.12, 1.10, 1.8, 1.6, 1.4, 1.2 and 1.0 release series.

- - -

*These release notes have been prepared by Tim-Philipp Müller.*

*License: [CC BY-SA 4.0](http://creativecommons.org/licenses/by-sa/4.0/)*
