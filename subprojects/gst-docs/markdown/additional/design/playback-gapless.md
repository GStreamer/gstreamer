Gapless and instant URI switching in playback elements
===

This document explains the various changes and improvements to the playback
elements in order to support gapless playback and instantaneous URI switching.

Last Update: November 23rd 2022


# Background

The new `playbin3` element and its components (`uridecodebin3`, `decodebin3` and
`urisourcebin`) are replacements to the legacy `playbin2` and `decodebin2`
elements.

The goals of these new elements are to both allow new use-cases and improve
performance (lower memory/cpu/io usage, lower latency). One of the key
principles is also to re-use elements as much as possible. For example, when
switching audio tracks the decoder can be re-used (if compatible).

The separation of roles was also more clearly split up into various new elements
(from lowest-level to highest-level):

* `urisourcebin` handles choosing the right source elements for the given URI,
  and handles buffering (via `queue2`) if needed (for network sources for example).

* `parsebin` takes an input stream and figures out which demuxer, parsers and/or
  payloaders are needed to provide timed elementary streams.

* `decodebin3` internally uses `parsebin` to handle any input stream and will
  handle the decoding, inter-stream muxing interleave, stream selection and
  switching. It can also handle multiple inputs (such as an audio/video file and
  a separate subtitle file).

* `uridecodebin3` wraps `urisourcebin`s and `decodebin3` for any use-cases where
  one wishes to have decoded streams from given URIs.

* Finally `playbin3` combines `uridecodebin3` and `playsink` for providing a
  high-level convenience pipeline for playing back content.


This design has received many improvements over time:

* `decodebin3` was able to detect input changes (caps changes) and reconfigure
  the associated `parsebin` if incompatible. This allows use-cases where
  upstream is an HLS/DASH stream where codecs are different across bitrates. The
  playback remains seamless if the decoders are compatible.

* `decodebin3` was able to bypass the usage of `parsebin` altogether if the
  incoming stream is pull-based, provides a `GstStreamCollection` and is
  compatible with the decoders or output caps.

* `urisourcebin` can handle sources that handle buffering internally, avoiding
  dual-buffering.

* A new core query `GST_QUERY_SELECTABLE` was added so that (source) elements
  could notify `decodebin3` that they can handle stream selection and switching
  themselves.

* Several improvements were made to `playbin3` to allow complete stream type
  changes (such as going from playing audio+video to just audio or just video,
  and back), This allows temporarily disabling whole chains of elements when not
  needed.


# Limitation/Issue

Two limitations existed though, which are both related:

* Changing URI required bringing `playbin3` (and all contained elements) down to
  `GST_STATE_READY`, setting the uri, and then bringing all elements back to
  `GST_STATE_PAUSED`.
  * This meant that all elements contained within were either discarded
    (decoders, demuxers, parsers, sources, ...) or reset (sinks)... despite
    potentially being 100% compatible (ex: going from h264/aac to h264/aac).

* Gapless playback (i.e. automatically switching from one source to another, and
  removing any potential gap in the data arriving to the sinks) was implemented by
  pre-rolling a full `uridecodebin3` for the next item to play and switching the
  inputs to `playsink` when the original `uridecodebin3` was EOS.
  * This meant that none of the existing elements (demuxers, parsers, decoders,
    ..) contained in the original `uridecodebin3` were re-used.

Those two use-cases are the same thing: We want to change the URI
(i.e. `urisourcebin`) but re-use as much as possible of existing elements
(i.e. `decodebin3` and `playsink`). The only difference between the two
use-cases is that changing URI should happen instantaneously in the first case,
whereas in the second case it happens when the initial source is done (EOS).

Fixing this will allow:

* Reducing memory and cpu usage (no duplicate elements)

* Lowering latency (no longer re-instantiate/reconfigure elements and re-use
  compatible ones as fast as possible).

Another issue which is related, is figuring out the *optimal* time at which the
next item should be prepared so that it has enough data to playback immediately:
* This shouldn't be too early, some URIs expire after a given time, or the user
  might change their mind in between
* This shouldn't be too late, otherwise we risk not having enough data to
  playback seamlessly.


# Changes

## parsebin in urisourcebin

In order to figure out the *optimal* time at which a switch should happen
(i.e. a given amount of "time" before the end of the previous play entry), this
can only be done on "timed" data (i.e. parsed elementary streams).

There is therefore a new option on `urisourcebin` : `parse-streams`, which if
set to `TRUE` (non-default) will add a `parsebin` (if and where needed) so that
`urisourcebin` only outputs elementary streams. A `multiqueue` will also be
present to handle any interleave present (i.e. only queue up what is needed to
offer coherent streams downstream).

If buffering is activated on `urisourcebin`, the `multiqueue` present after the
`parsebin` will be configured in order to handle it (and post the appropriate
buffering messages).

This offers the following benefits:
* `about-to-finish` can be emitted by `urisourcebin` as soon as `EOS` enters
  those `multiqueue`, which will be more precise than the previous usage (before
  `queue2` on non-timed data)

* buffering is much closer to the actual buffering amount (in time) which is
  specified on the properties.

* *ALL* scheduling downstream of `urisourcebin` is push-based, removing a lot of
  issues when trying to change scheduling modes (push vs pull) dynamically.

The `parse-streams` property is set to `TRUE` when used in `uridecodebin3`


## Only use a single uridecodebin3 in playbin3

Only a single `uridecodebin3` is in use in `playbin3` and the source pads it
provides are directly linked to `playsink`.

There can only be at most one stream of each stream type (audio, video, text) on
the output side of `uridecodebin3`. The exception to this is if the user/application
configured a specific multi-sinkpad combiner element for a given stream type,
in which case all streams of that given stream type are linked to that.

All uri-related properties are forwarded directly to `uridecodebin3`, which will
handle switching the sources to the single `decodebin3` it contains.


## uridecodebin3 URI and source handling

The URI for a given entry are handled in a `GstPlayItem` structure which
controls (via intermediary structures):

* The `urisourcebin` associated with the specified URI (and optional subtitle
  URI)

* The pads provided by those sources, and which states they are in (eos,
  blocked, ...) and the associated GstStream (if present)

* The buffering messages posted by those sources.


At any given point there is:

* A `input_play_item`, which is the play item currently feeding data into
  `decodebin3`

* A `output_play_itm`, which is the play item currently being outputted by
  `decodebin3`

Most of the time those two will be the same. But when switching play items
(going from one URI to another, whether gapless or not) this switch will happen
asynchronously.


## Switching inputs to decodebin3

The high-level goal is to add to `uridecodebin3` the capability of being able to
change `GstPlayItem` with the same `decodebin3` either:

* When the previous `GstPlayItem` has finished and there is a pending next
  `GstPlayItem`. This is the "gapless" scenario.

* Or immediately switch to the given `GstPlayItem` *without* having to change
  state. This is the "instantaneous URI switch" scenario.
  
For this, the following points need to be solved:

1. both scenarios: Add a way for "next" `GstPlayItem` to be pre-rolled
2. gapless: Determining when the switch can happen
3. instant-uri: pre-roll next `GstPlayItem` and flush downstream (to make the
  switch as quick as possile)
4. both scenarios: Do the actual switch


### pre-rolling play items

In order to be able to re-use the same decoders (within `decodebin3`) as much as
possible from the outside, we need to ensure that we feed the ideal
"replacement" stream to the same `decodebin3` sink pad.

For example, if we are switching from an audio+video HLS source to another
audio+video DASH source, we want to make sure we link the new `urisourcebin`
source pad providing video to the `decodebin3` pad that was previously consuming
the old video stream.

In order to do this, the `urisourcebin` we wish to switch to needs to be
pre-rolled (set to PAUSED, new pads are set to be blocked, and we wait for a
buffer/GAP to arrive on at least one of the pads).

At that point we will know the streams which are present in the new and old
`urisourcebin`s and can unlink/relink compatible pads. If new sink pads are
required they will be requested, and if old pads are no longer needed (for
example switching from two streams to a single one) they will be removed.

> Note: Doing this also has the benefit that "replacing" the inputs to
> `decodebin3` are done from a new streaming thread, and not the old
> `urisourcebin` streaming thread which could cause deadlocks.

> Note: This "waiting" is only done when "switching", i.e. on sources which
> aren't in the current input play item. If the pads are from the current play
> entry they are linked/unlinked as soon as they are added/removed.

The moment at which the next play item is pre-rolled is done:

* When the current play item has posted `about-to-finish` and the
  user/application has set a new play item.

* When a new play item has been set and the `instant-uri` property has been set
  to TRUE.

When a play item is pre-rolled, it is marked as "active". There can only be one
"active" play item in addition to the input play item.


### gapless: determining when the switch can happen

For gapless use-cases, we want to know the earliest time we can switch from one
play item to another.

Since all streams coming from `urisourcebin parse-streams=True` are push-based,
this is when the last EOS has been pushed through all pads of the source.


### Instantaneous URI switching

In order to be able to switch URI as soon as possible while re-using as many
existing elements as possible, there is a new `instant-uri` boolean property on
`uridecodebin3`/`playbin3`. The default value is FALSE.

If it is set to TRUE, the following happens whenever the `uri` property is set:

* On all pads of the current input play item:
  * `FLUSH_START` is sent to the downstream peer pads
  * The pad is made blocking
  * The pad is marked as EOS (i.e. as if EOS had been seen)

* And then again on all pads:
  * `FLUSH_STOP` is sent to the downstream peer pads

* Finally the new play item for the new URI is activated (pre-rolled).
  * Once it is pre-rolled it will switch over

This ensures all downstream elements are kept and are ready to receive the new
data.


### Switching play items

Switching play items requires special attention since it needs to be done
"atomically". We need to ensure it is done by a single thread. This is done by
having a lock (`play_items_lock`) which is taken whenever we need to modify the
list of play items and which play item is the current input/output.

We need to ensure the streaming thread(s) that were previously used are
stopped. Since we are only dealing with push-based sources this is simple: we
wait for the moment EOS is pushed on the last pad of the play item.

Another important consideration is that we need to ensure the thread that does
the switch is not the previous streaming thread (it needs to be stopped).

In order to solve those issues, the actual replacement of the inputs will always
happen from the streaming thread of the *new* play item, i.e. the one we wish to
make the current input. This is done in a pad block probe on the new item source
pad. Whenever a buffer (or GAP event) is received, we check whether we can
switch:

* If the current input play item is completely EOS, the switch can happen
  immediately. This will always be the case in instant-uri scenario and if the
  current input play item is pull-based.

* If the current input play item is not completely EOS, the probe waits on the
  `GCond input_source_drained`. This is the case that will commonly happen in
  gapless push-based scenarios, since we are waiting for the current input play
  item to be finished.
  
Once the switch can happen, we unlink all pads from `decodebin3` and attempt to
match compatible new source pads from `urisourcebin` to `decodebin3`. If new
sink pads are required they are requested, and if some sink pads are no longer
needed or do not match they are released.

Once all pads are linked, the new play item is set as the current play item.


## uridecodebin3 handles `about-to-finish` signalling

In regards to gapless playback, the API does not change. Users are still
expected to listen to `about-to-finish` and set the next URI to play back.

One thing that needs to be taken care of is making sure we don't emit
`about-to-finish` for play items which aren't currently used. This would end up
in a situation where `about-to-finish` would cause a snowball effect of pending
play items emitting it, which would cause a future entry to be created,
prerolled and emitting it again.

For that reason, if a play item emits that signal but isn't the input or output
play item, then it is just stored and not propagated upstream. When that play
entry becomes the new input entry it will be propagated.
