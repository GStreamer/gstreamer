# Adaptive Demuxers for DASH, HLS and Smooth Streaming

There are two sets of elements implementing client-side adaptive streaming
(HLS, DASH, Microsoft Smooth Streaming) in GStreamer:

 - The old legacy elements `dashdemux`, `hlsdemux`, `mssdemux` in the
   gst-plugins-bad module.

 - New `dashdemux2`, `hlsdemux2`, `mssdemux2` elements in gst-plugins-good
   (added in GStreamer 1.22).

The legacy adaptive streaming support in `gst-plugins-bad` had several pitfalls
that prevented improving it easily. The legacy design used a model where an
adaptive streaming element (`dashdemux`, `hlsdemux`) downloaded multiplexed
fragments of media, but then relied on other components in the pipeline to
provide download buffering, demuxing, elementary stream handling and decoding.

The problems with the old design included:

1. An assumption that fragment streams (to download) are equal to output
   (elementary) streams.

   * This made it hard to expose `GstStream` and `GstStreamCollection`
     describing the available media streams, and by extension made it
     difficult to provide efficient stream selection support

2. By performing download buffering outside the adaptive streaming elements,
   the download scheduling had no visibility into the presentation timeline.

   * This made it impossible to handle more efficient variant selection and
     download strategy

3. Several issues with establishing accurate timing/duration of fragments due to
   not dealing with parsed data

   * Especially with HLS, which does not provide detailed timing information
     about the underlying media streams to the same extent that DASH does.

4. Aging design that grew organically since the initial adaptive demuxer
   implementation with a much more limited feature set, and misses a better
   understanding of how a feature-rich implementation should work nowadays.

   * The code was complicated and interwoven in ways that were hard to follow
     and reason about.

5. Use of GStreamer pipeline sources for downloading.

   * An internal download pipeline that contained a `httpsrc -> queue2 -> src`
     chain made download management, bandwidth estimation and stream parsing
     more difficult, and used a new thread for each download.

# New design

The rest of this document describes the new adaptive streaming client
implementation that landed in gst-plugins-good in GStreamer 1.22.

The new elements only work in combination with the "streams-aware"
`playbin3` and `uridecodebin3` elements that support advanced stream
selection functionality, they won't work with the legacy `playbin`
element.

## High-level overview of the new internal AdaptiveDemux2 base class:

* Buffering is handled inside the adaptive streaming element, based on
  elementary streams (i.e. de-multiplexed from the downloaded fragments) and
  stored inside the `adaptivedemux`-based element.

* The download strategy has full visibility on bitrates, bandwidth, per-stream
  queueing level (in time and bytes), playback position, etc. This opens up the
  possibility of much more intelligent adaptive download strategies.

* Output pads are not handled directly by the subclasses. Instead subclasses
  specify which `tracks` of elementary streams they can provide and what
  "download streams" can provide contents for those tracks. The baseclass
  handles usage and activation of the `tracks` based on application
  `select-streams` requests, and activation of the `stream` needed to feed each
  selected `track`.

* Output is done from a single thread, with the various elementary streams
  packets being output in time order (i.e. behaving like a regular demuxer, with
  interleaving reduced to its minimum). There is minimal buffering downstream
  in the pipeline - only the amount required to perform decode and display.

* The adaptive streaming element only exposes `src` pads for the selected
  `GstStream`s. Typically, there will be one video track, one audio track and
  perhaps one subtitle track exposed at a time, for example.

* Stream selection is handled by the element directly. When switching on a
  new media stream, the output to the relevant source pad is switched once
  there is enough content buffered on the newly requested stream,
  providing rapid and seamless stream switching.

* Only 3 threads are used regardless of the number of streams/tracks. One is
  dedicated to download, one for output, and one for scheduling and feeding
  contents to the tracks.


The main components of the new adaptive demuxers are:

* `GstAdaptiveDemuxTrack` : end-user meaningful elementary streams. Those can be
  selected by the user. They are provided by the subclasses based on the
  manifest.
  
  * They each correspond to a `GstStream` of a `GstStreamCollection`
  * They are unique by `GstStreamType` and any other unique identifier specified
    by the manifest (ex: language)
  * The caps *can* change through time

* `OutputSlot` : A track being exposed via a source pad. This is handled by the
  parent class.

* `GstAdaptiveDemuxStream` : implementation-specific download stream. This is
  linked to one or more `GstAdaptiveDemuxTrack`. The contents of that stream
  will be parsed (via `parsebin`) and fed to the target tracks.
  
  * What tracks are provided by a given `GstAdaptiveDemuxStream` is specified by
    the subclass. But can also be discovered at runtime if the manifest did not
    provide enough information (for example with HLS).

* Download thread : Receives download requests from the scheduling thread that
  can be queried and interrupted. Performs all download servicing in a
  single dedicated thread that can estimate download bandwidth across all
  simultaneous requests.

* Scheduling thread : In charge of deciding what new downloads should be started
  based on overall position, track buffering levels, selected tracks, current
  time ... It is also in charge of handling completed downloads. Fragment
  downloads are sent to dedicated `parsebin` elements that feed the parsed
  elementary data to `GstAdaptiveDemuxTrack`

* Output thread : In charge of deciding which track should be
  outputted/removed/switched (via `OutputSlot`) based on requested selection and
  track levels. 


## Track(s) and Stream(s)

Adaptive Demuxers provide one or more *Track* of elementary streams. They are
each unique in terms of:

* Their type (audio, video, text, ..). Ex : `GST_STREAM_TYPE_AUDIO`
* Optional: Their codec. Ex : `video/x-h264`
* Optional: Their language. ex : `GST_TAG_LANGUAGE : "fr"`
* Optional: Their number of channels (ex: stereo vs 5.1). ex
  `audio/x-vorbis,channels=2`
* Any other feature which would make the stream "unique" either because of their
  nature (ex: video angle) or specified by the manifest as being "unique".

But tracks can vary over time by:

* bitrate
* profile or level
* resolution

They correspond to what an end-user might want to select (i.e. will be exposed
in a `GstStreamCollection`). They are each identified by a `stream_id` provided
by the subclass.

> **Note:** A manifest *can* specify that tracks that would normally be separate
> based on the above rules (for example different codecs or channels) are
> actually the same "end-user selectable stream" (i.e. track). In such case only
> one track is provided and the nature of the elementary stream can change
> through time.

Adaptive Demuxers subclasses also need to provide one or more *Download Stream*
(`GstAdaptiveDemuxStream`) which are the implementation-/manifest-specific
"streams" that each feed one or more *Track*. Those streams can also vary over
time by bitrate/profile/resolution/... but always target the same tracks.

The downloaded data from each of those `GstAdaptiveDemuxStream` is fed to a
`parsebin` element which will put the output in the associated
`GstAdaptiveDemuxTrack`.

The tracks have some buffering capability, handled by the baseclass.


This separation allows the base-class to:

* decide which download stream(s) should be (de)activated based on the current
  track selection
* decide when to (re)start download requests based on buffering levels, positions and
  external actions.
* Handle buffering, output and stream selection.

The subclass is responsible for deciding:

* *Which* next download should be requested for that stream based on current
  playback position, the provided encoded bitrates, estimates of download
  bandwidth, buffering levels, etc..


Subclasses can also decide, before passing the downloaded data over, to:

* pre-parse specific headers (ex: ID3 and webvtt headers in HLS, MP4 fragment
  position, etc..).

* pre-parse actual content if needed because a position estimation is needed
  (ex: HLS missing accurate positioning of fragments in the overall timeline)

* rewrite the content altogether (for example webvtt fragments which require
  timing to be re-computed)


## Timeline, position, playout

Adaptive Demuxers decide what to download based on a *Timeline* made of one or
more *Tracks*.

The output of that *Timeline* is synchronized (each *Track* pushes downstream at
more or less the same position in time). That position is the "Global Output
Position".

The *Timeline* should have sufficient data in each track to allow all tracks to
be decoded and played back downstream without introducing stalls. It is the goal
of the *Scheduling thread* of adaptive demuxers to determine which fragment of
data to download and at which moment, in order for:

* each track to have sufficient data for continuous playback downstream
* the overall buffering to not exceed specified limits (in time and/or bytes)
* the playback position to not stray away in case of live sources and
  low-latency scenarios.

Which *Track* is selected on that *Timeline* is either:

 * decided by the element (default choices)
 * decided by the user (via `GST_EVENT_SELECT_STREAMS`)

The goal of an Adaptive Demuxer is to establish *which* fragment to download and
*when* based on:

* The selected *Tracks*
* The current *Timeline* output position
* The current *Track* download position (i.e. how much is buffered)
* The available bandwidth (calculated based on download speed)
* The bitrate of each fragment stream provided
* The current time (for live sources)

In the future, an Adaptive Demuxer will be able to decide to discard a fragment
if it estimates it can switch to a higher/lower variant in time to still satisfy
the above requirements.


## Download helper and thread

Based on the above, each Adaptive Demuxer implementation specifies to a
*Download Loop* which fragment to download next and when.

Multiple downloads can be requested at the same time on that thread. It is the
responsibility of the *Scheduler thread* to decide what to do when a download is
completed.

Since all downloads are done in a dedicated thread without any blocking, it can
estimate current bandwidth and latency, which the element can use to switch
variants and improve buffering strategy.

> **Note**: Unlike the old design, the `libsoup` library is used directly for
> downloading, and not via external GStreamer elements. In the future, this
> could be made modular so that other HTTP libraries can be used instead.


## Stream Selection

When sending `GST_EVENT_STREAM_COLLECTION` downstream, the adaptive demuxer also
specifies on the event that it can handle stream-selection. Downstream elements
(i.e. `decodebin3`) won't attempt to do any selection but will
handle/decode/expose all the streams provided by the adaptive demuxer (including
streams that get added/removed at runtime).

When handling a `GST_EVENT_SELECT_STREAMS`, the adaptive demuxer will:

* mark the requested tracks as `selected` (and no-longer requested ones as not
selected)
* instruct the streams associated to no-longer selected tracks to stop
* set the current output position on streams associated to newly selected
  tracks and instruct them to be started
* return

The actual changes in output (because of a stream selection change) are done in
the output thread

* If a track is no longer selected and there are no candidate replacement tracks
  of the same type, the associated output/pad is removed and the track is
  drained.

* If a track is selected and doesn't have a candidate replacement slot of the
  same type, a new output/pad is added for that track

* If a track is selected and has a candidate replacement slot, it will only be
  switched if the track it is replacing is empty *OR* when it has enough
  buffering so the switch can happen without re-triggering buffering.

## Periods

The number and type of `GstAdaptiveDemuxTrack` and `GstAdaptiveDemuxStream` can
not change once the initial manifests are parsed.

In order to change that (for example in the case of a new DASH period), a new
`GstAdaptiveDemuxPeriod` must be started.

All the tracks and streams that are created at any given time are associated to
the current `input period`. The streams of the input period are the ones that
are active (i.e. downloading), and by extension the tracks of that input period
are the ones that are being filled (if selected).

That period *could* also be the `output period`. The (selected) tracks of that
period are the ones that are used for output by the output thread.

But due to buffering, the input and output period *could* be different, the
baseclass will automatically handle switch over.

The only requirement for subclasses is to ask the parent class to start a new
period when needed and then create the new tracks and streams.


## Responsibility split

The `GstAdaptiveDemux2` base class is in charge of:

* helper for all downloads.
* helper for parsing (using `parsebin` and custom parsing functions) stream data.
* provides *parsed* elementary content for each fragment (note: could be more
  than one output stream for a given fragment)
* helper for providing `Tracks` that can be filled by subclasses.
* dealing with stream selection and output, including notifying subclasses which
  of those *are* active or not
* handling buffering and deciding when to request new data from associated stream

Subclasses are in charge of:

* specifying which `GstAdaptiveDemuxTrack` and `GstAdaptiveDemuxStream` they
  provide (based on the manifest) and their relationship.
* when requested by the base class, specify which `GstAdaptiveDemuxFragment`
  should be downloaded next for a given (selected) stream.


