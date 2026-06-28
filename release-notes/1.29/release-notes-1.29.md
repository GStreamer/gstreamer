# GStreamer 1.29.x Release Notes

GStreamer 1.29.x is an API/ABI-unstable development series leading up to the stable 1.30 series.

The latest unstable development snapshot in the 1.29 series is [1.29.2](#1.29.2) and was released on 29 June 2026.

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

 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10637)
 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [aggregator: Handle gap event before segment](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10787)
 - [aggregator: Various introspection annotations / docs fixes for vfuncs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10720)
 - [baseparse, Preserve upstream buffer duration if possible](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10672)
 - [baseparse: Fix out_buffer leak in frame_free and missing ref in frame_copy](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11046)
 - [bin: iterator is not nullable](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11007)
 - [bitwriter: Steal owned data in reset_and_get_data()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10802)
 - [caps: Fix the features leak in gst_caps_append_structure_full](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10605)
 - [filesink: Fix wrong open() in overwrite mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10988)
 - [filesink: Report write error correctly on Windows](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10754)
 - [ges: Plug all leaks reported running valgrind on our testsuite](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10784)
 - [gst-stats: Also allow ANSI colored logs without 0x in front of the thread id](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10854)
 - [gst: Add explanatory comment to call_async implementation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10871)
 - [input-selector: fix several shortcomings](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10507)
 - [multiqueue: reverse playback: use segment stop position as start time](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10764)
 - [registry: Skip .dSYM bundles when loading plugins, try 2](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10850)
 - [registry: Skip .dSYM bundles when loading plugins, try 3](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10964)
 - [typefindfunctions: Promote y4m_typefind](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10542)

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

 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10637)
 - [Fix scaling and resizing with UIView on EAGL and Vulkan](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10556)
 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [audiobuffersplit: Various smaller fixes and implement handling of negative rates correctly](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10568)
 - [audiodecoder / videodecoder: Fix gap event handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10738)
 - [compositor: Do copy_metas also for background frame](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10824)
 - [compositor: move gst_compositor_init_blend() to element class_init](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11078)
 - [decodebin3: Fix switch to smaller collections](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10583)
 - [decodebin3: Improve handling collection change on existing pad](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8700)
 - [glcolorconvert: Fix NULL pointer dereference on buffers without video meta](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10939)
 - [glupload: Fix linking glupload with restrictive caps filter](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11029)
 - [input-selector: fix several shortcomings](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10507)
 - [opusenc: Use correct memcpy() size when copying Vorbis channel positions](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10982)
 - [playsink: unref color balance channels with g_object_unref()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10701)
 - [riff: Correctly check that enough RGB palette data is available](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10882)
 - [rtcp: Fix buffer overread in SDES packet parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10730)
 - [rtp: Add mappings for H266 and AV1 encoding-names](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10649)
 - [rtpbuffer: Add validation for CSRC list length](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10956)
 - [rtsp: Validate transport parameter parsing in RFC 2326](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10500)
 - [rtsp: gstrtspurl: Parse URL having user without password](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10847)
 - [typefindfunctions: Promote y4m_typefind](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10542)
 - [video-converter: Do not transform_metas with 0 width or height](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10832)
 - [video-converter: fix I420/A420 BGRA/ARGB output on big-endian](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11013)
 - [video: fix too small default stride for UYVP with odd widths](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11033)
 - [videodecoder: Handle recovery from temporary reordered output](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10558)
 - [videofilter: Add VIDEO_ALIGNMENT to downstream pool](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10808)
 - [videorate: Fix unrestored caps on backward PTS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11020)

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
 - [rtp: Lower Opus (de)payloader ranks to SECONDARY](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10940)
 - [rtspsrc: add backchannel-http-method property for HTTP tunnel mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10999)
 - [rtspsrc: Correctly parse RTSP 2.0 RTP-Info headers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8571)
 - [splitmuxsink: Include start/end timecodes in fragment-opened/closed messages](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10420)
 - [srtpdec: Add API for invalidating keys for a specific SSRC](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10980)
 - [test-client-managed-mikey: Allow user to select cipher](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10745)
 - [test-client-managed-mikey: Fix parsing of auth](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10783)
 - [wavparse: Add support for ID3 tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10610)

Backported into 1.28:

 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [Qt6GLVideoItem: caps update fixed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10959)
 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [gstrtspsrc: Set new mki in the encoder upon crypto update](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10744)
 - [hlsdemux2: fix seekable range for live HLS streams](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11044)
 - [meson: Fix libxml2 not building due to wrong option type](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10615)
 - [qml6glsrc: Fix rendering of scene with clipped items](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10572)
 - [qtdemux: Don't ignore flow return when pushing queued buffers downstream](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10570)
 - [qtdemux: Don't immediately push segment after moov in push mode for fmp4](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11014)
 - [qtdemux: Fix handling of in-between fragments without tfdt](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10590)
 - [qtdemux: Fix invalid WebVTT timestamps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11017)
 - [qtdemux: Fix out-of-bounds read when parsing PlayReady DRM UUIDs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10880)
 - [qtdemux: Make sure to not output the same samples multiple times in reverse playback mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10548)
 - [qtdemux: Push raw audio/video buffers downstream in reverse order if rate < 0](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10571)
 - [qtdemux: Set the segment position to the start on EOS in reverse playback mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10534)
 - [rgvolume: don't apply dBSPL reference level compensation for LUFS values](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10997)
 - [rtph264depay: fix invalid memory access in gst_rtp_h264_finish_fragmentation_unit](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10951)
 - [rtpqdm2depay: error out if anyone tries to use this element (and remove it)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10886)
 - [rtpsource: Add locking for receive reports table](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10683)
 - [rtptwcc: fix feedback packet count wrapping at 255](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10920)
 - [rtspsrc: fix Memory leak in gst_rtspsrc_close() when GST_RTSP_EEOF error occurs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10759)
 - [v4l2: Add support for AV1 stateful V4l2 decoder](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9892)
 - [vpxdec: Support downstream pools with alignment requirements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10442)
 - [wavenc: Skip writing empty LIST INFO chunk](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11049)
 - [wavpack: Fix handling of format changes, extend parser with new features, handle non-S32 samples in all variations](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10496)
 - [wavparse: Avoid integer overflow and out-of-bounds read when parsing adtl chunks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10900)
 - [wavparse: Avoid overflow in length when setting ignore-length=true](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11004)
 - [wavparse: Fix parsing of RF64 wave files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11008)

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

 - [analytics: Set default pixel-aspect-ratio for inference elements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10987)
 - [Fix a couple of new const-ness warnings around strstr() usage, out-of-bounds read in PTP clock and uninitialized variable compiler warning](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10637)
 - [Fix dims_order handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10861)
 - [Fix scaling and resizing with UIView on EAGL and Vulkan](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10556)
 - [Fix support for cross-compiling to iOS and add a cross file for it, remove HAVE_IOS, port to tvOS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10667)
 - [GThreadFunc return type fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10682)
 - [GstPlay: fix segmentation fault due to use after free](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10647)
 - [Minimal support for compiling with zxing-cpp 3.x](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10790)
 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [ajasink: Only allow 6 / 8 / 16 audio channels](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10521)
 - [ajasinkcombiner: Only forward the segment events from the video sinkpad](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10747)
 - [applemedia: Fix vtenc draining logic, port other existing fixes between vtdec and vtenc](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10849)
 - [applemedia: elements can now be individually registered with gst-full](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10780)
 - [asiosink: Fill silence when paused](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10823)
 - [audiobuffersplit: Various smaller fixes and implement handling of negative rates correctly](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10568)
 - [audiobuffersplit: fix reverse playback](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10705)
 - [av1dec: Enable VIDEO_META and VIDEO_ALIGNMENT for pool](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10865)
 - [av1parse:  split the alignment and stream type logic](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10868)
 - [av1parse: Misc fixes 2 typo](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11001)
 - [av1parse, vp9parse: Remove segment clipping to let downstream handle frame boundaries](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11081)
 - [baseparse, h264parse, h265parse: Preserve upstream buffer duration if possible](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10672)
 - [ccconverter: Reset counters on flush-stop](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10842)
 - [cea608mux: fix overflow when calculating output PTS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10706)
 - [codecs: v4l2: Add short and long term ref sets support in v4l2 codecs -- stable ABI](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10820)
 - [codectimestamper: Fix latency query handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10588)
 - [cudaupload, cudadownload: Fix CUDA/GL interop copy path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10495)
 - [dashsink: test: use playbin3 for DASH playback verification](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10834)
 - [decklinkvideosink: fix element leak in decklink callback](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11006)
 - [dtls: unregister signal handlers from connection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11054)
 - [dvbsuboverlay: Add missing bounds checks to the parser everywhere](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10884)
 - [gdppay: Fix null pointer dereference on duplicated caps event](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10911)
 - [h264,h265ccextractor: Fix framerate in initial caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10840)
 - [h264parse: Fix framerate calculation and interlaced features](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8310)
 - [h264parser: Fix memory leak in gst_h264_parser_parse_nal()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11052)
 - [h265parser: Validate num_decoding_units_minus1 in pic_timing SEI](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10902)
 - [h266parser: Fix APS ID bounds check in APS parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10888)
 - [h266parser: Fix out of bounds write when parsing pic_timing SEI](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10889)
 - [h266parser: Validate tile index bounds in picture partition parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10887)
 - [libs: jpegparser: boundary checks before copying it](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10885)
 - [meson: Add a subproject for providing the LunarG MoltenVK SDK](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10743)
 - [meson: Explicitly use cpp_std=c++11 for decklink](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10801)
 - [meson: Fix downloading MoltenVK SDK, make it work when meson-installed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10797)
 - [meson: Fix libxml2 not building due to wrong option type](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10615)
 - [mpeghdec: memory leak fix in MPEG-H Audio decoder plugin](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10593)
 - [mpegtspacketizer: Handle clock change/resets without skew correction](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10559)
 - [mxfdemux: always send a segment before sending eos or segment-done](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10569)
 - [mxfdemux: fix gst_mxf_demux_pad_get_stream_time ()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10550)
 - [nvcodec: Add capability caching to speed up plugin initialization](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10339)
 - [objectdetectionoverlay: add support for rotated bounding boxes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10552)
 - [openh264enc: remove broken drain and simplify handle_frame](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10589)
 - [soundtouch: Only allow up to 192kHz and 16 channels](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11000)
 - [srtpenc: preserve ROC when master key is updated for an ongoing session](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10969)
 - [svtav1: fix "Level of parallelism" property type discrepencies](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10836)
 - [tsdemux: Fix Continuity Counter handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10560)
 - [tsmux: reduce noise for DEBUG log level](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10474)
 - [tsmux: Fix integer overflow in SCTE35 NULL interval](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11066)
 - [v4l2: Add support for AV1 stateful V4l2 decoder](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9892)
 - [vabasetransform: copy buffer's metadata at copy when import buffer](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10800)
 - [vavp8enc: set color format chroma](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10803)
 - [vkav1dec: fix to set SavedOrderHints properly](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10829)
 - [vtdec: Avoid busy looping when queue length is smaller than DPB size](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10818)
 - [vtdec: Don't re-create session if only the framerate changed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10686)
 - [vtdec: Fix race condition when negotiating during playback](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10679)
 - [vtdec: Fix wrong DPB size check in the output loop](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10858)
 - [vtdec: Reverse playback fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10613)
 - [vtenc: Fix DTS offset calculation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9097)
 - [vtdec: Store supplemental codec support in a global variable](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11018)
 - [vulkan: load video function pointers conditionally based on codec operation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10771)
 - [vulkan: Clear mutex when GstVulkanImageMemory is freed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11047)
 - [wayland: Fix CLAMP operation of maxFALL and maxCLL](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10585)
 - [wayland: display: Add protection when replacing wl_output](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11002)
 - [waylandsink: make gst_wl_window_commit_buffer handle NULL buffers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10786)
 - [webrtc: sink floating refs of ICE transports](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10532)
 - [webrtcbin: Check the presence of encoding-name fields in answer caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10639)
 - [zxing: Fix version check for zxing-cpp 3.0.1](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10812)

#### gst-plugins-ugly

 - No changes that haven't been backported

Backported into 1.28:

 - [asfdemux: Error out on files with more than 32 streams](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10881)
 - [rmdemux: Check if new video fragment overflows the fragment storage before storing it](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10883)

#### GStreamer Rust plugins

 - [ac4parse: Basic AC-4 parser element creation](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2919)
 - [analytics: Add a hand tracking tensor decoder element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2870)
 - [compress: adding deflate algo support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2908)
 - [compress: Adding brotli support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2933)
 - [dashsink2: add new DASH sink element that uses CMAF muxer without splitmuxsink](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2186)
 - [flate: adding flate, for general purpose compress/decompress](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2889)
 - [hlssink3: Add HLS WebVTT sink element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2890)
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

 - [burn: yoloxinference: Restrict widths/heights to a multiple of 32](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2966)
 - [cargo_wrapper: Add nasm dir to path only if needed](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2897)
 - [Don't transform push_event() false returns into flow errors](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2911)
 - [fallbacksrc: Send select-streams event to collection source element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2907)
 - [gst-plugins-rs: only add example features when dependencies are found](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2824)
 - [gtk4paintablesink: Error out in NULL->READY if there is no default GDK display](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2930)
 - [png: implement image repacking when buffer is padded](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2962)
 - [rtpav1pay: insert sequence header if a keyframe is missing it](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2888)
 - [rtpbin2: don't panic in Drop impl](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2948)
 - [rtpsend: send mandatory events on the rtcp srcpad before sending the first buffer](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2894)
 - [rtspsrc2: Implement authentication support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2957)
 - [speechmatics: fix first_buffer_pts race condition in dispatch_message](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2898)
 - [speechmatics, textaccumulate: fix flushing issues](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2926)
 - [threadshare: examples: add rtpbin2 send / recv](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2938)
 - [threadshare: fix socket leak in ts-udpsink](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2760)
 - [threadshare: udpsink/src: don't error out failing to send packet to a client / receiving an ICMP error](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2928)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2916)
 - [webrtc: Silence new clippy warning](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2934)
 - [whisper: update to latest release 0.16](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2951)

#### gst-libav

 - No changes that haven't been backported

Backported into 1.28:

 - [Various element factory metadata fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10728)
 - [audiodecoder / videodecoder: Fix gap event handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10738)
 - [avviddec: Allow stride changes for some decoders](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8963)
 - [avviddec: Fix handling of mixed interlaced content](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9956)
 - [avviddec: Handle field/order changes in mixed interlace mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10578)
 - [gst-libav: avvidcmp: set colorimetry on AVFrame](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10090)
 - [libav: Don't process lines that won't be outputted](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10557)

#### gst-rtsp-server

 - No changes that haven't been backported

Backported into 1.28:

 - [rtsp-client: Lock media when unlinking session medias](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11040)
 - [rtsp-stream: Clear send_thread when it's freed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10844)
 - [rtspclient: expose property to control error posting on RTCP timeout](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10819)

#### gstreamer-sharp

 - [gstreamer-sharp: update for latest API additions and introspection annotation changes in 1.29.0.1](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11073)

#### gst-python

 - [analytics: adding group mtd and keypoint mtd](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10551)

Backported into 1.28:

 - [Python typing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10526)
 - [bin: iterator is not nullable](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11007)

#### gst-editing-services

 - [ges-launcher: fix crash when error message has no source](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11093)
 - [validate: Cleanup media and scenarios and add documentation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10537)
 - [validateflow: auto-derive directories from test file path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10962)

Backported into 1.28:

 - [Plug all leaks reported running valgrind on our testsuite](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10784)
 - [Remove spurious python-embed dependency from libges](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11065)
 - [meson: Fix libxml2 not building due to wrong option type](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10615)

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

 - [Update Rust dependencies](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10867)

#### gstreamer-docs

 - [doc: sync cerbero doc with last system-recipe change](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8428)
 - [doc: add design doc for generic-raw-format](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10607)

Backported into 1.28:

 - [tutorials/android: bump up ndkVersion to 29](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10782)

#### Development build environment

 - [Add release notes snippets directory](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10604)
 - [vscode: avoid to track launch.json](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8009)
 - [gst-env.py: Don't change CWD when entering the devenv](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10876)
 - [gst-env.py: Keep a simple wrapper for meson devenv](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9802)
 - [macos: enforce meson >= 1.8.3 for Xcode 26+](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9813)
 - [wraps: update ffmpeg to 7.1.1, cairo to 1.18.4](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8627)
 - [wraps: bump libvpx to 1.15](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10848)

Backported into 1.28:

 - [Add a subproject for providing the LunarG MoltenVK SDK](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10743)
 - [Fix downloading MoltenVK SDK, make it work when meson-installed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10797)
 - [wraps: update libxml2 to v2.15.2](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10974)

#### Cerbero build tool and packaging changes in 1.29.1

 - [Adding Support for Android on RISC-V64](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/1739)
 - [build-tools: remove gtk-doc-lite recipe](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2145)
 - [cerbero: fixes for background build setup](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2143)
 - [hacks: Remove xml hack in favor of etree.indent](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/1673)
 - [system_recipes: drop arch assert](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2108)
 - [x265: unify all recipes in a single file](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2126)
 - [oven: do not mark skipped recipes as built](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/1820)

Backported into 1.28:

 - [Add tvOS support](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2080)
 - [Ensure there's no gstreamer in the artifact cache](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2144)
 - [Rename vaapi variant to va and by default on Linux, test built plugins correctly on CI](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2131)
 - [Update to Rust 1.93 / cargo-c 0.10.20](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2071)
 - [Update to Rust 1.94 and cargo-c 0.10.21](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2156)
 - [build: include plugin's .pc file in Linux and macOS](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2154)
 - [bump up to NDK 29 and pin cpp_std to c++11 in opencore-amr](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2090)
 - [cerbero: Use static python requires for all wheels](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2116)
 - [cerbero: add environment variable for building run with background priority](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2082)
 - [cerbero: os.PRIO_DARWIN_PROCESS is only available on Python 3.12+](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2142)
 - [fetch: use the Oven for parsing the dependency tree](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2115)
 - [filesprovider: Check missing files at the recipe level on CI runs](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2068)
 - [glib.recipe: Upstream old iOS GIO_MODULE_DIR hack](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2073)
 - [gtk.recipe: Enable gstreamer media backend on Windows](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2121)
 - [gtk: Enable GStreamer media backend on Windows](https://gitlab.freedesktop.org/gstreamer/cerbero/-/issues/556)
 - [inno: assorted fixes for Registry key handling](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2170)
 - [inno: Fix incorrect MSVC redistributable when cross-building for Arm64](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2081)
 - [inno: Restrict usage to native Windows as exposed by sys.platform](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2083)
 - [inno: fix environment variable being created outside SessionManager/Environment](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2152)
 - [libpng: update to 1.6.55](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2100)
 - [libsoup: update to 3.6.6](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2150)
 - [libsrtp: update to v2.8.0](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2167)
 - [osx-framework: Add missing gstreamer libraries](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2122)
 - [osx: fix file duplication in .pkg payloads](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2113)
 - [packaging: Fix missing devel payloads for gstreamer-1.0-python](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2171)
 - [pygobject.recipe: Fix overflow when comparing GstMessageType on Windows](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2089)
 - [recipe: do not run symbolication if nodebug](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2149)
 - [wheel: Add a new gstreamer_meta package with fewer deps](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2087)
 - [wheel: Add debuginfo wheel](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2077)
 - [wheels: Add a new meta-package 'gstreamer_bundle'](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2103)
 - [wheels: Add proper description / long_description](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2110)
 - [wheels: Don't raise an exception if cli exits with non-zero code](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2120)
 - [x265: drop common source recipe](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2114)

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

<a id="1.29.2"></a>

### 1.29.2

The first API/ABI-unstable 1.29.x development snapshot release (1.29.2) was released on 29 June 2026.

Any newly-added API in the 1.29.x series may still change or be removed again before 1.30 and should be considered unstable until 1.30 is released.

The 1.29.x release series is for testing and development purposes, and distros should probably not package it.

#### Highlighted changes in 1.29.2

 - webrtcbin2: New scalable WebRTC bin that uses fewer threads and native Rust DTLS, ICE and RTP session management implementations
 - rtspsrc2: implement support for SRTP, authentication, HTTP tunnelling, keep alive, stream selection, TLS validation, latency configuration
 - rtp2: New Rust RTP payloader and depayloader implementations for MPA audio, MPEG-2 video and raw video
 - Digitally Signed Content (DSC) support, initially for H.266 video
 - New D3D12-based element which performs color lookup operations using user-provided Adobe .cube LUT files
 - videoencoders: Support adaptive presets with resolution-dependent properties and implement in x264enc and nvh264enc
 - closedcaption: add h266seiinserter; add "do-timestamp" property to codecseiiinserter/merge_requests/10514)
 - flv: Add support for AV1 video
 - v4l2: add capture timestamps to buffers in v4l2src, and "bitrate" and "gop-size" properties to video encoders
 - applemedia: new iosurface helper library providing a memory:IOSurface abstraction
 - AMF: add super resolution hq-scaler component
 - New OpenGL glalphacombine element for RGBA inputs and Vulkan/OpenGL memory support for alphacombine
 - openjpeg: Add support for high bit depth formats
 - analytics: add semantic tag getter to GstAnalyticsMtd
 - tensordecoders: Add YOLO26 detection decoder
 - tfliteinference: support for external delegates, XNNPACK, and GRAY8 input
 - MP4: add support for losslessly-compressed video
 - matroskamux: add TrueHD audio and HDMV PGS subpicture support
 - playsink reconfiguration stability improvements
 - New agingradio and sofalizer audio effect elements
 - New image-rs based imagersoverlay element in Rust to replace gdkpixbufoverlay
 - webrtcsink: add v4l2h264enc (Raspberry Pi) encoder support and a signaller for Unreal Engine PixelStreaming
 - webrtcsink, webrtcsource: make custom signalling protocol extendable
 - Speech elements: implement support for non-synchronized output
 - d3d12: Add support for hardware-accelerated decoding of VP9 with alpha
 - androidmedia: New source element for Android assets
 - filesink: Add support for Windows file sharing mode to allow reading of file while it's being written to
 - tracing: custom spans and events API and new tracers that integrate with the Rust tracing ecosystem
 - editing-services: auto-plug raw video converters from the registry based on compositor memory family and prepare for multi-threaded usage
 - dots-viewer: General enhancement of the web app
 - New viuersink video sink that plays video in the terminal, replacing aasink and cacasink
 - cerbero: Added ccache support for MSVC
 - Countless bug fixes, build fixes, memory leak fixes, and other stability and reliability improvements

#### gstreamer

Core:

 - [Add a GST_SEGMENT debug category](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11181)
 - [cpuid: Add RISC-V Vector extension detection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11768)
 - [doc: Improve documentation of gst_debug_bin_to_dot_file()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11289)
 - [value: Add gst_value_take_structure() and use it where possible](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/7647)
 - [systemclock: Add gst_system_clock_new() to create a new instance of the system clock](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11474)
 - [pluginfeature: export the rank as a property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11390)
 - [plugin: Fix typo in documentation comment](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11593)
 - [preset: Support adaptive presets with resolution-dependent properties](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11092)
 - [ReferenceTimestampMeta: add documentation for timestamp/x-system-monotonic](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10211)
 - [tracer: Add a custom span API](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11708)
 - [riscv: Add host_defines and ABI struct definitions](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11773)

Core Libraries:

 - [aggregator: Improve locking, fix flushing deadlock](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11864)
 - [aggregator: Don't clear the pad's segment on flush](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/612)
 - [aggregator: Wake up src pad task on reconfigure event](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/707)
 - [basesrc, basetransform: add a virtual method to prepare the pool & allocator](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11425)
 - [ptp-helper: Fix copy&paste mistakes in error messages](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11353)

Core Elements and Tracers:

 - [filesink: Add support for Windows file sharing mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11733)
 - [inputselector several raciness fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11928)
 - [Pass a strong reference to the user_data to gst_pad_start_task()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11489)
 - [tracers: gst-dots: General enhancement of the web app](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11082)
 - [tools: fix gst-inspect for nested caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11521)

Backported into 1.28:

 - [aggregator: Fix documentation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11485)
 - [allocator: Use g_try_malloc() instead of g_malloc() for sysmem](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11540)
 - [baseparse: Fix memory leak when subclass returns error](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11525)
 - [bitwriter: Allow unsetting set bits when overwriting them](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11442)
 - [bufferpool: avoid leaking partially preallocated buffers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11663)
 - [caps: fix multiple caps leaks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11751)
 - [check: Add API to run tests without fork(), adjust existing tests](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11196)
 - [datetime: Improve correctness of ISO-8601 string parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11507)
 - [devicemonitor: Wait for start thread to finish when listing devices](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11502)
 - [gstinfo: Don't use fwrite() on Windows for debug logging](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11586)
 - [gstinfo: Use stack allocation for <=1KB messages](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11577)
 - [gsttask: Fix racy tests by making unref deterministic](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11635)
 - [gstvalue: fix crash when converting NULL G_TYPE_VALUE_ARRAY to G_TYPE_STRING](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11706)
 - [pads: Fix sticky event raciness when linked mid-push](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11929)
 - [queue: Fix potential use-after-free in log function](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11191)
 - [registry: detect libgstreamer load from Android container and skip canonicalization](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11574)
 - [streams: Add METADATA to the valid stream flags for serialization](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11519)
 - [value: On buffer deserialization errors first unmap the buffer and then unref it](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11583)
 - [gst-inspect-1.0: type for string caps fields should be 'string' not 'gchararray'](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11449)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)
 - [Tests: Fix build with glib <= 2.67.2](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11605)

#### gst-plugins-base

 - [allocators: round GstShmAllocator maxsize up to page size](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11118)
 - [alsa: fix deadlock during shutdown](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10265) <!-- FIXME: might get reverted before release -->
 - [audioaggregator: Add gst_audio_aggregator_has_current_output_buffer()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11486)
 - [audio/video base classes: add prepare_allocator virtual method](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11425)
 - [audiovisualizer: copy metas to the output video buffer](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11857)
 - [compositor: Fix caps negotiation for messy downstream caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11342)
 - [exiftag: Use byte readers for parsing consistently](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11296)
 - [glcolorbalance: Fix caps transformation in passthrough mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11408)
 - [glcolorscale: fails to renegotiate when resolution changes, if it is a passthrough at some point](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/work_items/4973)
 - [glfilter: Fix regression from "fix not rangifying size caps when in passthrough"](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/work_items/5052)
 - [glfilter: fix not rangifying size caps when in passthrough](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11090)
 - [videotimecode: Add timecode discont flag, bind it with pic_timing discontinuity in h264parse and drift resync in timecodestamper](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11268)
 - [mikey: Allow disabling SRTP authentication via SP type 10](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11880)
 - [opengl: Add new glalphacombine element for RGBA inputs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11678)
 - [codecutils: va, v4l2: Add shared H.264 level calculation helper and automatic level selection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11154)
 - [discoverer: add builder API for GstDiscovererInfo](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11469)
 - [playsink: Ensure that pad-blocking waits for an in-progress reconfigure](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11401)
 - [playsink: Fixes for reconfiguration to avoid deadlocks and out-of-lock accesses](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11195)
 - [rtspsrc: add independent keepalive worker for non-live TCP-interleaved](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11544)
 - [timecodestamper: Add scale property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/7317)
 - [video: add missing classifications in scaler/colorspace/uploader/downloader elements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11388)
 - [video: Add Digitally Signed Content (DSC) buffer meta](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10435)
 - [video: fix doc chunk for GST_VIDEO_DSC_VERIFICATION_META_INFO](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11665)
 - [video-format: Add static assert to prevent list of formats mismatch](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11917)
 - [videodecoder: send a new gap event with current position if the received gap event is cached](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11278)
 - [videoencoder: Support adaptive presets with resolution-dependent properties](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11092)
 - [wavparse: handle uppercase 'ID3 ' chunk fourcc for ReplayGain tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11711)
 - [Pass a strong reference to the user_data to gst_pad_start_task()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11489)
 - [Use new gst_system_clock_new() which creates a new instance of the system clock](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11474)
 - [Use gst_value_take_structure() where possible](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/7647)

Backported into 1.28:

 - [GstAudio/VideoDecoder: Fix different seqnum for eos event error](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11119)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)
 - [Tag: Prevent ubsan and wrong fraction usage](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11287)
 - [appsink, appsrc: Allow passing NULL callbacks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11407)
 - [appsink: Clear local sample storage when flushing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11931)
 - [appsrc: Uniformly handle EOS events being pushed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11924)
 - [appsrc: Fix dropped counting with bufferlist](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11311)
 - [audio-resampler-neon: fix Thumb encoding and use Clang O2 calculation for strides](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11472)
 - [audio-resampler-neon: fix accumulated stride](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11672)
 - [audio-resampler-neon: re-increment address](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11752)
 - [audio-resampler-neon: read array operand by hand](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11205)
 - [audio-resampler-neon: read array operand by hand, part 2](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11270)
 - [audioaggregator: Don't drop pending input buffers on sinkpads on srcpad caps changes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11482)
 - [audioaggregator: Don't reset samples_per_buffer unless sample rate / output-buffer-duration has changed](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11484)
 - [audioaggregator: Don't try converting buffers on caps changes if impossible](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11477)
 - [audioaggregator: Remove brittle conversion of in-progress buffers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/1037)
 - [audioencoder: Remove fixed caps from srcpad](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11115)
 - [audioresample: Fix extra samples produced at speech-to-silence transitions](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11394)
 - [av1parse / typefind: Avoid signed 32 bit integer overflow and OOB reads when parsing LEB128 values](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11245)
 - [decodebin2: fix leak of endpads list on shutdown while exposing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11468)
 - [discoverer: Lock the DISCO_LOCK whenever accessing the streams list](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11838)
 - [discoverer: Take the DISCO_LOCK while parsing stream topology](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11497)
 - [exiftag: Add missing bounds check and integer overflow protections in various places](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11246)
 - [exiftag: Ignore invalid fractions with numerator/denominator G_MININT](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11227)
 - [exiftag: Unmap buffer if parsing a rational number gives a zero denominator](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11207)
 - [exiftag: Use a hashtable instead of a linked list for storing the pending tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11326)
 - [gl: add GBRA swizzle support](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11405)
 - [gl: egl: Set TRANSFER_NEED_DOWNLOAD flag](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11681)
 - [glupload: fix memleak on failure path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11623)
 - [glwindow: Allow setting a NULL window handle](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11542)
 - [id3v2: Add input validation and refactor id3v2_ununsync_data](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11314)
 - [id3v2: Check valid frame sizes more](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11379)
 - [id3v2: Don't modify const data and check for enough data when reading RVA2 tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11794)
 - [id3v2: Don't unnecessarily assert on size==0 when unsyncing data](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11636)
 - [libs: video: add precondition check on dma helpers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11445)
 - [opengl: Fix glcolorconvert vertical flip issue on crop](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11406)
 - [pbutils: Add NULL check for tmpcaps parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11745)
 - [pbutils: Fix possible null dereferene when empty string is provided](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11744)
 - [playback: Make sure to check for empty/any caps before getting the first structure](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11201)
 - [rtcpbuffer: Add some missing bounds checks when parsing SDES](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11791)
 - [sdp: keep level-asymmetry-allowed in the caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11667)
 - [subparse / samiparse: Various robustness fixes and minor other fixes.](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11325)
 - [subparse: Avoid NULL-pointer dereferences in mdvdsub parsing code](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11238)
 - [subparse: Avoid zero and extreme fps when parsing mdvdsub subtitles](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11795)
 - [subparse: Don't allow very small framerates for microdvd subtitles](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11849)
 - [subparse: Fix integer overflow when calculating qttext timestamp](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11255)
 - [subparse: Fix memory leakage for text colour and background colour](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11357)
 - [subparse: Replace regex string matching / replacing with plain C string parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11237)
 - [uridecodebin3: Use PLAY_ITEMS_LOCK for URI-related getter](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11627)
 - [uridecodebin3: deactivate input_item in erroneous ready->paused transition](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11950)
 - [uridecodebin: Protect missing_plugin_errors list from concurrent access](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11576)
 - [videodmabufpool: Break ref cycle between the pool and its thread](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11313)
 - [videodmabufpool: Fix debug category](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11676)
 - [vorbistag: Check for enough base64 data before trying to decode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11855)
 - [xmptag: Correctly initialize pointer to the end of the input array](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11793)
 - [xmptag: Don't allocate -1 bytes of memory if there's only a single tag](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11850)
 - [xmptag: Handle fractions with 0 denominator as invalid](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11912)

#### gst-plugins-good

 - [aacparse: Overhaul PCE handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11742)
 - [adaptivedemux2: Ensure we are preserving target time on non-snapping seeks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9884)
 - [cacasink, aasink: remove libcaca and libaa-based ASCII rendering plugins](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11172)
 - [flv: Add support for enhanced-rtmp's support for av1](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/5087)
 - [matroskademux: Don't ignore encoded seek table](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11279)
 - [matroskamux: HDMV PGS support (subpicture/x-pgs)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8688)
 - [matroskamux: TrueHD support (audio/x-true-hd)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8689)
 - [mpegaudioparse: Add channel mask to output caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11116)
 - [pbutils, va, v4l2: Add shared H.264 level calculation helper and automatic level selection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11154)
 - [qtdemux: add 'max-atom-size' property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/5215)
 - [qtdemux: add support for losslessly-compressed video](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10998)
 - [rtspsrc: add independent keepalive worker for non-live TCP-interleaved](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11544)
 - [rtspsrc: add mTLS file properties](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11735)
 - [udpsrc: don't query downstream for Allocation / pool parameters](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11425)
 - [v4l2: Fix empty name warning in dqbuf for M2M devices](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11457)
 - [v4l2src: add v4l2_buffer timestamp to buffer](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10211)
 - [v4l2videoenc: Add bitrate and gop-size GObject properties](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11125)
 - [wavparse: handle uppercase 'ID3 ' chunk fourcc for ReplayGain tags](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11711)
 - [Use gst_system_clock_new() to create a new instance of the system clock](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11474)
 - [Pass a strong reference to the user_data to gst_pad_start_task()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11489)

Backported into 1.28:

 - [adaptivedemux/hlsdemux assertions / fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11550)
 - [applemedia: Require Xcode 12.4 for all builds](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11225)
 - [audioinvert: fix float truncation in transform_float](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11170)
 - [avidemux: Fix divide by zero if VPRP contains fields==0](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11302)
 - [flacenc: Fix g_object_notify on loose-mid-side-stereo property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11508)
 - [flvdemux: Avoid assertions on corrupted streams](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11244)
 - [flvmux: fix race condition on caps get and check](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11220)
 - [gdkpixbufdec: remove Sun and Andrew raster formats](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11183)
 - [isomp4: Fix memory leak when file is corrupted](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11515)
 - [isomp4: qtdemux: Add bounds checks for ESDS descriptors](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11377)
 - [qtdemux: push_buffer() should use global GstClockTime](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11951)
 - [matroska-mux: Write ReferenceBlock for non-keyframe video in BlockGroups](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11705)
 - [matroska: Fix wrong object type bug](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11514)
 - [matroskademux: Add missing parenthesis when calculating bz2 buffer sizes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11248)
 - [matroskademux: Don't pass non-GstElement pointers to GST_ELEMENT_ERROR](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11909)
 - [osxaudio: Fix stack overflow with >64ch audio devices](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11656)
 - [qml6glsink: Fix redraw issues on buffer change](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11335)
 - [qt6: remove an unneeded QOpenGLContext->makeCurrent()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11788)
 - [qt: Avoid parsing caps on every buffer (same fix for both qt5 and qt6)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8256)
 - [qtdemux: Add various integer overflow and bounds checks to uncompressed video handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11242)
 - [qtdemux: Avoid a couple of integer overflows](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11102)
 - [qtdemux: Check for minimum stride requirements and width/height constraints with uncompressed video](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11584)
 - [qtdemux: Preserve Metas and Flags when doing row alignment](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11122)
 - [qtdemux: Various fixes related to audio channel counts](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11243)
 - [qtdemux: parse mastering luminance as u32 instead of u16](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11657)
 - [rtph265depay: fix mem leak](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11714)
 - [rtspsrc: Discard early data in ONVIF mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11161)
 - [rtspsrc: Fix const-correctness issue around strchr() usage](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11214)
 - [rtspsrc: include user-agent property in HTTP tunnel requests](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11282)
 - [rtspsrc: mki is optional upon crypto update](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11393)
 - [sbcparse: Add bounds checking to header parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11799)
 - [splitmuxsink: Fix some failure-to-shutdown race conditions](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11723)
 - [splitmuxsink: Require buffers to have a valid PTS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/6776)
 - [tests: mpegaudioparse: Fix raciness in the state change handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11721)
 - [v4l2: Fix buffer leak on qbuf failure](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11712)
 - [v4l2: object: Fix caps filtering in caps negotiation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11222)
 - [v4l2transform: release input buffers earlier](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9850)
 - [wavpack: Various channel / channel-mask related fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11729)
 - [wavpackdec: Avoid integer overflow when calculating output buffer size and related fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11797)
 - [wavparse: Fix integer overflow when checking available buffer size for reading cues](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11247)
 - [wavparse: Remove assertion about upstream file size](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11465)
 - [wavparse: recover from invalid av_bps instead of failing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11328)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)

#### gst-plugins-bad

 - [adaptivedemux: Fix caps query](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11168)
 - [alphacombine: support GLMemory, VulkanMemory and all applicable planar formats](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11399)
 - [amfcodec: Fix a build error for gstamfbasefilter](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11770)
 - [amfcodec: Fix a build error in amfencoder](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11750)
 - [amfcodec: add super resolution hq-scaler component](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11674)
 - [amfcodec: add High quality preset values to h264 and h265 encoders](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11873)
 - [analytics: add semantic tag getter to GstAnalyticsGroupMtd](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11155)
 - [analytics: move semantic tag from GstAnalyticsGroupMtd to generic GstAnalyticsMtd](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11423)
 - [androidmedia: Add aassetsrc element for Android assets](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11718)
 - [androidmedia: Read video codec details from VideoCapabilities](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11549)
 - [applemedia: Add stable unique-id device selection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11158)
 - [applemedia: avf sources now honour downstream GstVideoMeta requests](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11358)
 - [applemedia: add iosurface helper library and introduce memory:IOSurface](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11560)
 - [av1parse: support T.35 UK country code (0xB4) for LCEVC detection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11730)
 - [avfvideosrc: Improve concurrent usage detection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11463)
 - [closedcaption: add h266seiinserter](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10514)
 - [codecparser: expose publicly H265 bitwriter API](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11330)
 - [codecparsers: h265bitwriter: implement gst_h265_bit_writer_filler()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11323)
 - [codecseiinserter: Add do-timestamp property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10591)
 - [cuda-appsrc: Fix missing sentinel in nvcodec example](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11402)
 - [cuda: check CuMemFree/CuMemFreeAsync return values](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11106)
 - [cuda: fix CONVET typo in format macro names](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11104)
 - [cuda: fix typos in gstcudacontext](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11105)
 - [d3d12: Add most-detailed-mip property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11790)
 - [d3d12: Add support for VP9 with alpha](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11084)
 - [d3d12: Implement fast-path mipmap texture generation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11218)
 - [d3d12converter: Add src-roi properties for mipmap level selection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11704)
 - [errorignore: Add ignore-flushing property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/7388)
 - [errorignore: post warning on first flow conversion](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11103)
 - [examples: Fix nvcodec example build on Windows](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11111)
 - [facedetector: Support model without built-in post-processing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/9418)
 - [videotimecode: Add timecode discont flag, bind it with pic_timing discontinuity in h264parse and drift resync in timecodestamper](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11268)
 - [h26456parse: Trigger caps update when HDR SEI expires](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11437)
 - [h264parse: Ignore bitstream restrictions when it has out of bound values](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11384)
 - [h266parse: add support to both read and parse Digitally Signed Content (DSC) SEI messages, and attach their metas to the buffers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10435)
 - [hlssink2: Add enable_program_date_time property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11354)
 - [mpegtsdemux: Add stats to mpegtsbase](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/3455)
 - [mpegtsmux: Accumulate audio packets for larger PES and a couple of small fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/2085)
 - [mpegtsmux: Remove streamheader from caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11113)
 - [mpegtssection: Write serializer for mpegts_sections](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11169)
 - [msdkav1enc: Add intrabc and palette for scc encode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11250)
 - [nvcodec: add example for pushing cuda runtime api frames into appsrc](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/8967)
 - [onnxinference: Accept ranges with only one entry](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11319)
 - [onnxinference: Add unit tests](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11417)
 - [onnxinference: fix typo](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11260)
 - [openjpeg: Add support for high bit depth formats](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11749)
 - [openjpeg, jpeg200parse: Fix JPEG 2K for YUV input](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11787)
 - [va, v4l2: use shared H.264 level calculation helper and automatic level selection](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11154)
 - [play: Fix GstStream leaks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11177)
 - [shm: fix shmpipe lockup due to wrong buffer ack and fallback allocator](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11162)
 - [shmsink: fix hang](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/work_items/4346)
 - [srtp: mikey: Allow disabling SRTP authentication via SP type 10](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11880)
 - [ssdtensordecoder: Allow fixed and variable tensor dimensions](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11492)
 - [tensordecoder: fix tensordecodebin classification](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11361)
 - [tensordecoders: Add YOLO26 detection decoder](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11140)
 - [tensordecoders: yolo-seg: fix mask stride metadata for ROI masks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11224)
 - [tfliteinference tests: Add missing model files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11644)
 - [tfliteinference: Accept ranges with one entry](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11416)
 - [tfliteinference: Add GRAY8 input support](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11415)
 - [tfliteinference: Add XNNPACK support](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11443)
 - [tfliteinference: Add support for external delegate](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11478)
 - [tfliteinference: Drop video meta API to avoid strided data](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11493)
 - [tfliteinference: Fix caps negotiation with upstream element](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11643)
 - [tfliteinference: fix compilation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11490)
 - [timecodestamper: Add scale property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/7317)
 - [timecodestamper: Ignore upstream timecodes from corrupted frames](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11153)
 - [tsdemux: define IGNORE_PCR_THRESHOLD constant, raise to 1000ms](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11166)
 - [tsdemux: Add ignore-continuity-counter property for HLS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11884)
 - [tsmux: Don't write HDMV descriptor for H264 files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11346)
 - [ultralightfacedetectortensordec: Add caps for without-postproc](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11221)
 - [va: Document CPU access to decoded frames](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11496)
 - [va: add background-color property for vacompositor and vapostproc](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11694)
 - [va: misc fixes for vaav1enc and vavp9enc](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11604)
 - [validate: Reindent all validatetest files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10965)
 - [video: add missing classifications in scaler/colorspace/uploader/downloader elements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11388)
 - [videoencoder: Support adaptive presets with resolution-dependent properties](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11092)
 - [videoparsers: Use parsed resolution instead of upstream one](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11148)
 - [vkh264enc: enable a simple unit test](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11446)
 - [vmaf: change "threads" property default to 0 for automatic number of CPUs determination](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11133)
 - [vulkan: Assert encode query result offset is zero per spec](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11300)
 - [vulkan: caps leaks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11336)
 - [vulkan: fix encode feedback query handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11023)
 - [vulkan: fix planar VulkanImage download bookkeeping](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11436)
 - [vulkan: h264encoder: base class miscelaneous improvements](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11267)
 - [vulkan: merge plugin extension requirements into default instance/device setup](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11280)
 - [vulkan: opt-in multi-planar YUV VkImage for image buffer pool](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11766)
 - [vulkan: remove propose_allocation override in GstVulkanVideoFilter](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11668)
 - [vulkan: requested-extensions - use-after-free on merged_req](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11765)
 - [vulkanh264{enc,dec}: add missing profiles constrained-high and progressive-high](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11462)
 - [Vulkan H.264 encoder base class updates](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11447)
 - [wayland: Add sync_store to prevent callback errors](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11634)
 - [wayland: Rework gstwayland render path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11157)
 - [wayland: fix crash due to double-destroy wl_callback](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11327)
 - [wayland: handle padded buffers in wl_shm buffer creation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11370)
 - [webrtcbin: Drop late received data on bundled EOS branches](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11748)
 - [Use gst_system_clock_new() to create a new instance of the system clock](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11474)
 - [Pass a strong reference to the user_data to gst_pad_start_task()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11489)

Backported into 1.28:

 - [ahcsrc: Register exposure-mode property for GstPhotography interface](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11545)
 - [ajasink: Correctly set reference source](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11304)
 - [amc: Don't try printing NULL caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11685)
 - [amcvideodec: Don't keep crop-rectangle uninitialized if not specified](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11684)
 - [amcvideodec: Fix double-free happening when codec gets reconfigured](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11900)
 - [analytics: fix meta transform function for copy cases](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11392)
 - [android: trim tutorials by using fewer plugins, ensure that an AMC video decoder is always found, fix decoding of constrained-baseline files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10547)
 - [androidmedia: Add various new codec mime / profile mappings](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11547)
 - [androidmedia: Don't print error logs if downstream returns flushing / EOS](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11543)
 - [androidmedia: Fix typo in error message](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11691)
 - [androidmedia: Free element name after use for logging](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11555)
 - [androidmedia: support decoding flac](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10753)
 - [applemedia: Fix MoltenVK usage with vtdec-imported textures](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11062)
 - [applemedia: Fix test instability and nofork issues](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11458)
 - [applemedia: Require Xcode 12.4 for all builds](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11225)
 - [applemedia: fix planar CoreVideo buffer plane offsets](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11412)
 - [av1parse / typefind: Avoid signed 32 bit integer overflow and OOB reads when parsing LEB128 values](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11245)
 - [av1parse: Fix null pointer deference](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11523)
 - [av1parser: Fix bytes/bits confusion when parsing tile data size](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11803)
 - [avfassetsrc: Now supports vp9/av1, and able to read incompletely supported files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10684)
 - [avtp: Correct ptime generation from avtp timestamp](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11887)
 - [bpmdetect: Fix calculation of number of samples for >1 channels](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11283)
 - [camerabin: Fix caps negotiation when starting video capture](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11440)
 - [cudaconvert: fix performance regression caused by double precision floating point constants](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11344)
 - [d3d12decoder: Fix decoding on Qualcomm GPUs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11764)
 - [decklink: Fix various refcount issues and related leaks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11509)
 - [h263parse: Fix wrong ratio masking](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11528)
 - [h263parse: Missing handling of reserved invalid EPAR_D value](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11527)
 - [h264parse: Avoid NULL pointer dereferences when freeing partially parsed SPS/MVC data](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11240)
 - [h264parse: Check for enough slice header data being available](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11938)
 - [h264parse, h265parse: Unset GValue in every code path](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11911)
 - [h265decoder: Fix HEVC with alpha decoding](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11144)
 - [h265parser: use sub-layer 0 CPB count in buffering_period SEI loops](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11334)
 - [h266parser: Add missing clearing function for H266 SEI message](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11526)
 - [h266parser: Avoid integer overflow when parsing profile / tier / level](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11241)
 - [h266parser: Avoid out-of-bounds write when parsing PPS tile slices](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11581)
 - [h266parser: Check aspect ratio index against lookup table length](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11939)
 - [h266parser: Use long bit skipping function for potentially large values](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11910)
 - [interlace: Revert "Drop framerate from query caps of sinkpad"](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11421)
 - [jp2kdecimator: Avoid integer overflows and divisions by zero on invalid tile configurations](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11236)
 - [librfb: Validate framebuffer update rectangles against the framebuffer size](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11866)
 - [mpegdemux: Add various bounds checks related to PES header parsing](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11580)
 - [mpegtsdemux: Various fixes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11450)
 - [mpegtsdemux: Improve PTS rollover handling in ignore-pcr mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11903)
 - [mpegtspacketizer: Avoid potential overflow](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11303)
 - [mpegtspacketizer: Do not seek before the first PCR](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11782)
 - [mpegtsmux: Always assign PTS to output buffers in CBR mode (regression fix)](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11930)
 - [mse: Also disable the library if the option is disabled](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11467)
 - [mxf: Fix multiple writing / parsing issues when handling VANC packets](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11585)
 - [mxfdemux: Fix essence track offsets array population](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11882)
 - [mxfdemux: Fix remaining offsets index entry insertion call site](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11895)
 - [mxfdemux: Fix reverse temporal offsets array upper bounds check](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11582)
 - [mxfdemux: Use unsigned integers in more places and don't truncate 64 bit integers](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11792)
 - [mxfdemux: hardening](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11124)
 - [mxfmux: aes-bwf: Use correct size when serializing user data / channel status mode](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11340)
 - [netsim: Fix racy test failures](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11927)
 - [nvcodec: Fix missing adapter-luid when loading decoders](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11317)
 - [pcapparse: Add missing bounds checks to ensure packets are large enough](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11867)
 - [pngparse: Fix Use-after-free bug](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11524)
 - [pnmdec: Avoid overflows when calculating frame sizes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11913)
 - [qml6d3d11sink: Clear texture on Paused-to-Ready transition](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11510)
 - [qroverlay: Fix use after free bug](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11959)
 - [qt6d3d11: fix null check in SetForceAspectRatio()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11456)
 - [rtmp2: Remove socket timeout after handshake completes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11888)
 - [rtpsink: fix mutex not unlocked on invalid URI in set_property](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11937)
 - [sctp: Set number of outgoing & incoming streams to the same value](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11204)
 - [shm: fix shmsink exit code 1 on clean shutdown](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11109)
 - [svtav1enc: Scale MDCV and CLL to SVT-AV1's expected units](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11518)
 - [tfliteinference: Add unit tests](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11318)
 - [tsdemux: Fix parsing of PES ESCR and following PES header fields](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11292)
 - [tsdemux: Fix segfault when trying to handle SCTE-35 with incorrect program specified](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11167)
 - [va: surfacecopy: get surface's image in `gst_va_surface_copy`](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11899)
 - [va: do not post error message when push fails](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11398)
 - [va: drm: Fix fd leak and return type in create_va_display](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11637)
 - [vajpegdecoder: Validate that enough data is available for the current JPEG segment](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11805)
 - [vaoverlaycompositor: Fix textoverlay transparency](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11619)
 - [vkswapper/vksink: Don't advertise unsupported formats](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11149)
 - [vkupload/vkdownload: Fix possible corrupted image due to mismatched stride/padding](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11147)
 - [vmncdec: Set cursormask to NULL to prevent double free](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11254)
 - [vmncdec: Avoid integer overflows when rectangle positions and sizes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11869)
 - [vtdec: Avoid locking up during a decoder reset](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11386)
 - [vtdec: Do not hold the stream lock when pushing out frames](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11096)
 - [vtdec: Prefer outputting VulkanImage instead of sysmem, fix some leaks, ensure vulkansink provides a window](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11070)
 - [vtdec: Support decoding HEVC+alpha](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11130)
 - [vtdec: fix deadlock when restarting pipeline](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11310)
 - [vtdec: handle decoder error status for iOS, vtenc: restart if VTCompressionSessionCompleteFrames fails](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11097)
 - [vulkan: Fix tests crashing on macOS due to fork() usage](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11196)
 - [vulkanupload: Don't reallocate the pool when the framerate changes](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11500)
 - [vulkanvp9dec: Fix case in device-specific factory name](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11107)
 - [wasapi2: Don't reset process loopback capture client](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11673)
 - [wasapi2: Log target device information](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11189)
 - [wasapi2sink: Ignore device errors from default device](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11110)
 - [waylandsink: Properly reset the tag orientation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11180)
 - [waylandsink: fix waylandsink crash when call window flush](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11128)
 - [webrtc: nice: Fix leak of nice thread](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11088)
 - [webrtc: take ownership of src_bin and sink_bin and don't leak error message](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11352)
 - [wlwindow: fix viewport source outside buffer when play resolution change stream](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11129)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)
 - [Tests: Fix build with glib <= 2.67.2](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11605)
 - [meson: fix building -bad tests with disabled tflite, onnx and mse](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11594)
 - [Fix a couple of const correctness bugs around strchr() usage](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11185)

#### gst-plugins-ugly

 - [mpeg2dec: remove plugin](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11173)
 - [x264enc: Add adaptive bitrate preset for YouTube](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11092)
 - [x264enc: Refactor SEI handling](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11114)
 - [Pass a strong reference to the user_data to gst_pad_start_task()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11489)

Backported into 1.28:

 - [mpegpsdemux: Release stream lock when seeking fails](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11862)
 - [realmedia: Fixes for various out-of-bounds reads](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11825)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)

#### GStreamer Rust plugins

 - [Add sofalizer element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/1212)
 - [Add viuersink - a video sink that plays video in your terminal](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2973)
 - [colorlut: Allow any keyword line before data table](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3076)
 - [image: add imagersoverlay element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3024)
 - [isobmff: add support for lossless compression](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2942)
 - [pcap_writer: Mark target-factory and pad-path props as construct](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2971)
 - [quinn: Do not make source elements live by default](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3051)
 - [quinn: Miscellaneous clean ups](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3093)
 - [rsaudiofx: Add agingradio plugin](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3087)
 - [rtp2: Add MPEG-1/2 video RTP payloader/depayloader](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3092)
 - [rtp2: Add MPEG-1 Audio RTP payloader/depayloader](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/1596)
 - [rtp2: Add raw video RTP payloader/depayloader](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2861)
 - [rtpbin2: By default use the same CNAME for all sessions created by the same rtprecv/rtpsend](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2977)
 - [rtpbin2: Do NTP timestamp calculations with integers instead of floating point numbers](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2976)
 - [rtpbin2: jitterbuffer: move deadline calculation where it's needed](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3002)
 - [rtpbin2: Remove BYE / timeout SSRCs from sync context and add a signal for timeout SSRCs](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2975)
 - [rtpbin2/rtpsend: calculate NTP capture time for packets](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3027)
 - [rtpbin2/time: handle NTP rollovers](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3026)
 - [speech elements: implement support for non-synchronized output](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3001)
 - [tracers: add Rust tracing ecosystem integration](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2820)
 - [udpsrc2: override prepare_allocator](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3049)
 - [udpsrc2: set socket in NULL to READY](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3054)
 - [validate: dump failing frame to PNG on QR code check failure](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2929)
 - [video: Add D3D12-based color LUT element](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3052)
 - [webrtcbin2: Add new scalable WebRTC bin with fewer threads](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2955)
 - [webrtcbin2: require rice-proto for webrtcbin2 build](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3082)
 - [webrtcbin2/example: wait for caps event before creating answer](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3090)
 - [webrtcrecv: add threadshare-mode](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3123)
 - [webrtcsend: add sink pad early-data-mode property](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3106)
 - [webrtsend: move early-data-mode property to webrtcsend](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3122)
 - [webrtc/signalling: make protocol extendable](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3028)
 - [webrtcsink: add v4l2h264enc (Raspberry Pi) encoder support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2893)
 - [webrtcsink: don't forward duplicate timecode metas](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2192)
 - [webrtcsink: Unreal Engine PixelStreaming Signalling](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2024)
 - [Build: Don't build utils/validate by default](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2936)

Backported into 0.15:

 - [fallbacksrc: Add fallback-source and enable-dummy properties](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2989)
 - [fmp4mux: Fix draining in chunk mode after partial GOPs were drained](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3057)
 - [gopbuffer: add support for H.266/VVC](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3135)
 - [isobmff: Change caps updates in test to not be delayed](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3040)
 - [isobmff/fmp4mux: Various fixes for splitting at fragment boundaries](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3116)
 - [onvifmeta2relatiometa: fix inversion between width and height](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3108)
 - [png: implement image repacking when buffer is padded](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2962)
 - [quinn: Disable tests](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2983)
 - [quinnwtsrc/sink: Fix session close](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3033)
 - [Rtp2Session: add ParamSpec for property stats](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3060)
 - [rtpbin2: examples: fix audio resyncs, stream offsets and frame drops](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2969)
 - [rtpbin2: improve logs](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2980)
 - [rtpbin2: jitterbuffer: fix deadline for re-ordered packets](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2992)
 - [rtpbin2: more log improvements](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2991)
 - [rtprecv: extend jitter accounted for](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3015)
 - [rtprecv: fix buffer list split handling](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3109)
 - [rtprecv: fix race conditions handling rapid FlushStart / FlushStop](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3004)
 - [rtprecv: JitterBufferStream: avoid polling JitterBuffer when possible](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2996)
 - [rtspsrc2: Add support for SET_PARAMETER and GET_PARAMETER using signals](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3085)
 - [rtspsrc2: Add support for SRTP](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3045)
 - [rtspsrc2: Add TLS support](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2970)
 - [rtspsrc2: handle parse errors with tcp interleaved rtsp more gracefully](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3073)
 - [rtspsrc2: Implement support for HTTP tunnelling](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2987)
 - [rtspsrc2: Implement support for keep alive](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2990)
 - [rtspsrc2: Implement support for streams](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3009)
 - [rtspsrc2: Include user-agent property in HTTP tunnel requests](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3022)
 - [rtspsrc2: Support latency configuration property](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3099)
 - [rtspsrc2: Support TLS validation flags for server certificate](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3101)
 - [rtspsrc2: Update README with implemented features](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3068)
 - [rtspsrc2: Update sha2 and md-5 dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3007)
 - [st2038combiner: only forward video pad segment](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3096)
 - [Switch from `std::os::raw` to `std::ffi` for C types](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3010)
 - [threadshare: add leaky mode to dataqueue-based elements](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3032)
 - [threadshare: add ts-clocksync (backported)](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3132)
 - [tracers: feature gate remaining PluginAPIFlags makers behind doc](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3012)
 - [transcriberbin: ignore flow errors from transcription branch](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2974)
 - [webrtc: set level in negotiated caps only if level asymmetry not allowed](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3059)
 - [webrtcsink: actually allow custom signaller to be set](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3042)
 - [webrtcsink: Adding imx8mp vpuenc_hevc support for 265](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3018)
 - [webrtcsink: handle payloader timestamp-offset prop type variants](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3075)
 - [webrtcsink: read rav1enc bitrate as i32](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3112)
 - [webrtcsink: add support for h264 profile constrained-high](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3121)
 - [webrtcsink: allow change of interlace-mode in the input caps](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3127)
 - [webrtcsink: allow renegotiation if caps is missing interlace-mode](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3124)
 - [webrtcsink: fix negotiation for nvh264enc in the GLMemory case](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3131)
 - [webrtc: tests: run signalling server with unique port number](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2988)
 - [whisper: fix compiling on ARM](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/2985)
 - [Clippy: address clippy 1.95.0 suggestions](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3035)
 - [Clippy: Fix new 1.95 clippy warnings](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3047)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3005)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3020)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3038)
 - [Update dependencies](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3055)
 - [Add script to convert git sourced dependencies to crates.io packages](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3083)
 - [gst-plugin-version-helper: Relax version requirements and update to 0.8.4](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/merge_requests/3071)

#### gst-libav

 - [avcodecmap: Take the dual mono case into account](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/5211)
 - [avcodecmap: Declare undeclared loop iterator variable](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11419)
 - [Pass a strong reference to the user_data to gst_pad_start_task()](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11489)

Backported into 1.28:

 - [avdemux: Always free AVIOContext and open failure and don't dereference NULL AVFormatContext](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11802)
 - [avprotocol: Don't free GstFFMpegPipe when closing the AVIOContext](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11801)
 - [avviddec: Refcount codec frame associated with video frame](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10915)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)

#### gst-rtsp-server

 - [tests: Fix potential deadlock in unit tests](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11516)

Backported into 1.28:

 - [RTSPMediaFactory: make create_pipeline introspectable](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11719)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)
 - [gst: Fix a couple of const correctness bugs around strchr() usage](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11185)

#### gstreamer-sharp

 - [gstreamer-sharp: update for latest API additions](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11906)

#### gst-python

 - [analytics: add semantic tag getter to GstAnalyticsGroupMtd](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11155)
 - [analytics: move semantic tag from GstAnalyticsGroupMtd to generic GstAnalyticsMtd](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11423)

#### gst-editing-services

 - [ges: add internal locking to enable multi-threaded GES usage](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10198)
 - [ges: auto-plug raw-video converters from the registry based on compositor memory family](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11434)
 - [ges: make ges:+clip URIs work out of the box](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11671)
 - [nle: ghostpad: Replace gst_pad_get_element_private with NleGhostPad subclass](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10992)
 - [validate: Reindent all validatetest files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10965)
 - [validate: flow: compare expectations live as lines are produced](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10975)
 - [test: updat efor compositor: Fix caps negotiation for messy downstream caps](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11342)

Backported into 1.28:

 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)
 - [ges: fix use-after-free in GESUriSource decodebin callbacks](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11374)
 - [ges: fix use-after-free in structured-interface and asset cache](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11348)
 - [gst: Fix a couple of const correctness bugs around strchr() usage](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11185)

#### gst-devtools, gst-validate + gst-integration-testsuites

 - [dots-viewer: General enhancement of the web app](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11082)
 - [dots-viewer: Update dependencies](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11350)
 - [modelinfo-helper: Use shape_signature for tflite when available](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11281)
 - [validateflow: Compare expectations live as lines are produced](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10975)
 - [validateflow: Wait for all async calls when stopping](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11517)
 - [validate: Reindent all validatetest files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10965)
 - [Use gst_value_take_structure() where possible](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/7647)

Backported into 1.28:

 - [validate: use relative paths in the printed validate command, fix flaky tests due to non-deterministic pad task unref](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11626)
 - [Require C std gnu11 or c11, remove custom 'restrict' definition, fixing build with Qt 6.11](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11266)

#### gstreamer-docs

 - [docs: Update iOS tutorials to use the new .xcframework package, add tvOS target](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10954)
 - [documentation: misc improvements made while building on Windows](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11701)

Backported into 1.28:

 - [android: trim tutorials by using fewer plugins, ensure that an AMC video decoder is always found, fix decoding of constrained-baseline files](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/10547)

#### Development build environment

 - [Update wraps: expat, flac, gdk-pixbuf, jpeg-turbo, png, srtp, xml, opus, proxy-intl, soundtouch, sqlite3, theora, zlib, wayland-protocols, drm, glib-networking](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11630)
 - [Move many wrap files from wrap-git to wrap-file, ban new ones with a check in pre-commit](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11503)

Backported into 1.28:

 - [gobject-introspection.wrap: Assorted fixes for Alpine RISC-V and MSVC build](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/11784)

#### Cerbero build tool and packaging changes in 1.29.2

 - [Add ccache support for MSVC](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2255)
 - [cmake: support LibraryType.BOTH under MSVC on all remaining recipes](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2254)
 - [ffmpeg: update to 8.1.2](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2281)
 - [gst-plugins-bad: Package libgstiosurface on Apple targets](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2268)
 - [gst-plugins-good: update for removal of cacasink and aasink](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2181)
 - [gst-plugins-rs.recipe: Add colorlut](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2249)
 - [gst-plugins-rs: also build webrtcbin2 plugin](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2175)
 - [gst-plugins-rs: fix webrtcbin2 build for MinGW](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2253)
 - [openssl: Add parallel build support for MSVC with Jom](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2262)
 - [pyproject.toml: fix version after 1.29.1 release](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2197)
 - [tinyalsa: switch to AOSP source](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2208)
 - [Cannot link rust plugins with Xcode 26](https://gitlab.freedesktop.org/gstreamer/cerbero/-/work_items/538)
 - [docs: Copy AI policy from the monorepo](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2265)

Backported into 1.28:

 - [Adjust CI for xcframework iOS tutorials](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2148)
 - [Fixes for cross-compiling to android on macOS and consuming the built binaries](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2184)
 - [Update to Rust 1.95 / rustup 1.29.0 / cargo-c 0.10.22](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2228)
 - [Update to Rust 1.96 / cargo-c 0.10.23](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2260)
 - [bindgen-cli: Update to 0.72.1](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2275)
 - [bundlesource: Output xz tarballs instead of gzip, fix pkg-config-dist packaging](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2195)
 - [cerbero: Fix entering into the build environment, add explicit non-interactive mode](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2192)
 - [cerbero: Fix invalid target_subsystem when targeting Android on macOS](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2204)
 - [cerbero: Read recipe files as UTF-8](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2258)
 - [cerbero: fixes for background mode on macOS](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2178)
 - [cerbero: support differently named Xcode for iOS and tvOS builds](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2161)
 - [cerbero: Reenable deps cache for gst-libav and pkg-config](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2269)
 - [cerbero: Use capi command for cargo-c](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2272)
 - [cmake: Fix destination for the Java plugin initializers](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2240)
 - [glib-networking: Backport various OpenSSL backend fixes](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2246)
 - [gst-plugins-rs: Extend melding to Darwin platforms](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/1935)
 - [gst-plugins-rs: fix MSVC link errors for cargo binary consumers](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2227)
 - [inno: allow setting User or Admin-level install via command line](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2216)
 - [inno: do not automatically create envvars or Registry keys with portable mode enabled](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2234)
 - [ios: Execute postinstall template copy only with user level installs](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2225)
 - [ios: fix syntax error in postinstall script](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2233)
 - [libffi.recipe: Update to latest 3.2.9999.5 release, update proxy-libintl to 0.5](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2207)
 - [libpng: update to 1.6.56](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2180)
 - [libproxy: add backend, gio module and add to gstreamer-1.0-net.package](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2193)
 - [m4: Update to 1.4.21](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2232)
 - [osx: Add uninstaller script via osascript](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2217)
 - [osxrelocator: relocate absolute paths to Python.framework](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2206)
 - [pkg-config.recipe: Never enable --define-prefix](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2257)
 - [pkg-config.recipe: Fix pcfiledir substitution with whitespace](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2277)
 - [soundtouch: update to 2.4.1](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2187)
 - [source: serialize tarball extracts that share an unpack path](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2250)
 - [srt: update to 1.5.5](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2218)
 - [vvdec: upgrade to 3.1.0](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2194)
 - [vvdev: silence -Werror=nontrivial-memcall on macos and friends](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2244)
 - [wheel: rename and placehold the msvc runtime wheel](https://gitlab.freedesktop.org/gstreamer/cerbero/-/merge_requests/2221)

#### Contributors to 1.29.2

Albert Sjölund, Alicia Boya García, Anders Hellerup Madsen, Andreas Frisch,
Andrew Yooeun Chun, Arthur Chan, Azat Nurgaliev, Batalev Ilya, Benoît Mauduit,
Cameron O'Neal, Carlos Falgueras García, Carlos Bentzen, Christoph Seitz,
Cole Richardson, Corentin Noël, Daniel Morin, David Maseda Neira, dec05eba,
Dennis Han, Diego Nieto, Dino Spiller, Dominique Leroux, Edward Hervey,
Elliot Chen, Emil Ljungdahl, Emmanuel Madrigal, Fabian Orccon, Fabien Danieau,
François Laignel, Felix Gong, Frédéric Chanal, Gordon Smith,
Guillaume Desmottes, Haihua Hu, Havard Graff, He Junyan, Hou Qi, Ilya Batalev,
Jakub Adam, Jan Alexander Steffens (heftig), Jan Schmidt, Jeehyun Lee,
Jeongmin Kwak, Jeremy Whiting, Jerome Colle, Jochen Henneberg, Johan Sternerup,
Jordan Petridis, László Károlyi, Leonardo Salvatore, L. E. Segovia,
Marcus Hanestad, Mathieu Duponchelle, Matthew Waters, Michael Olbrich,
Michiel Westerbeek, MilkClouds, Monty C, Niclas Götting, Nicolas Dufresne,
Nirbheek Chauhan, nitroxis, Ognyan Tonchev, Olivier Crête, Oscar Carter,
Oskar Fiedot, Pablo García, Pavel Guzenfeld, Per Enstedt, Philippe Normand,
Philipp Wallrich, Pieter Willem Jordaan, Piotr Brzeziński, Qian Hu (胡骞),
Rares Branici, Robert Mader, Rolf Eike Beer, romain, RSWilli, Ruben Gonzalez,
Sanchayan Maity, Santiago Carot-Nemesio, Sebastian Dröge, Seungha Yang,
Seungmin Lee, Shengqi Yu, Shigeharu Kamiya, Stéphane Cerveau,
Taruntej Kanakamalla, Thibault Saunier, Thomas Devoogdt, Tim-Philipp Müller,
Tjitte, Tobias Koenig, Tomas Granath, Tomasz Andrzejak, Tomasz Bujewski,
Tulio Beloqui, Tushar Darote, Vadym Markov, Víctor Manuel Jáquez Leal,
Vincent Beng Keat Cheah, Vivia Nikolaidou, Walisiewicz,
Xabier Rodriguez Calvar, Xavier Claessens, Zeeshan Ali Khan,

... and many others who have contributed bug reports, translations, sent
suggestions or helped testing. Thank you all!

#### List of merge requests and issues fixed in 1.29.2

- [List of Merge Requests applied in 1.29.2](https://gitlab.freedesktop.org/groups/gstreamer/-/merge_requests?scope=all&utf8=%E2%9C%93&state=merged&milestone_title=1.29.2)
- [List of Issues fixed in 1.29.2](https://gitlab.freedesktop.org/groups/gstreamer/-/work_items?scope=all&utf8=%E2%9C%93&state=closed&milestone_title=1.29.2)

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
