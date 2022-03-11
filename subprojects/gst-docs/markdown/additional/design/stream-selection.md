# Stream selection

This document describes the events and objects involved in stream selection in
GStreamer pipelines, elements and applications

## History / Availability

Since 1.10:
* `GstStream` and `GstStreamCollection`
* `enum GstStreamType`
* `GST_EVENT_SELECT_STREAMS`, `GST_EVENT_STREAM_COLLECTION`,
  `GST_MESSAGE_STREAMS_SELECTED`, `GST_MESSAGE_STREAM_COLLECTION`
* `GstStream` present in `GST_EVENT_STREAM_START`
* `GST_BIN_FLAG_STREAMS_AWARE`

Since 1.22:
* `gst_query_new_selectable()`
* `gst_query_set_selectable()`
* `gst_query_parse_selectable()`

Initial version: June 2015 (Edward Hervey)

Last reviewed: March 2022 (Edward Hervey)


##  Background

This new API is *intended* to address the use cases described in this section:

1) As a user/app I want an overview and control of the media streams that can be
  configured within a pipeline for processing, even when some streams are
  mutually exclusive or logical constructs only.

2) The user/app can disable entirely streams it's not interested in so they
  don't occupy memory or processing power - discarded as early as possible in
  the pipeline. The user/app can also (re-)enable them at a later time.

3) If the set of possible stream configurations is changing, the user/app should
  be aware of the pending change and be able to make configuration choices for
  the new set of streams, as well as possibly still reconfiguring the old set

4) Elements that have some other internal mechanism for triggering stream
  selections (DVD, or maybe some scripted playback playlist) should be able to
  trigger 'selection' of some particular stream.

5) Indicate known relationships between streams - for example that 2 separate
  video feeds represent the 2 views of a stereoscopic view, or that certain
  streams are mutually exclusive.

> Note: the streams that are "available" are not automatically the ones active,
> or present in the pipeline as pads. Think HLS/DASH alternate streams.


## Example use cases

1. Playing an MPEG-TS multi-program stream, we want to tell the app that there
   are multiple programs that could be extracted from the incoming
   feed.
   
   Furthermore, we want to provide a mechanism for the app to select which
   program(s) to decode, and once that is known to further tell the app which
   elementary streams are then available within those program(s) so the app/user
   can choose which audio track(s) to decode and/or use.


2. A new PMT arrives for an MPEG-TS stream, due to a codec or channel
   change. The pipeline will need to reconfigure to play the desired streams
   from new program. Equally, there may be multiple seconds of content buffered
   from the old program and it should still be possible to switch (for example)
   subtitle tracks responsively in the draining out data, as well as selecting
   which subs track to play from the new feed.
   
   This same scenario applies when doing gapless transition to a new source
   file/URL, except that likely the element providing the list of streams also
   changes as a new demuxer is installed.


3. When playing a multi-angle DVD, the DVD Virtual Machine needs to extract 1
   angle from the data for presentation. It can publish the available angles as
   logical streams, even though only one stream can be chosen.


4. When playing a DVD, the user can make stream selections from the DVD menu to
   choose audio or sub-picture tracks, or the DVD VM can trigger automatic
   selections.
   
   In addition, the player UI should be able to show which audio/subtitle tracks
   are available and allow direct selection in a GUI the same as for normal
   files with subtitle tracks in them.


5. Playing a SCHC (3DTV) feed, where one view is MPEG-2 and the other is H.264
   and they should be combined for 3D presentation, or not bother decoding 1
   stream if displaying 2D.  (bug
   https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/issues/118)


6. Playing an adaptive stream (HLS or DASH), the user can know the various
   streams it can select, and only the required streams will be downloaded. This
   is independent of the bitrate selection mechanism.


## Design Overview

Stream selection in GStreamer is implemented in several parts:

1. Objects describing streams : `GstStream`
2. Objects describing a collection of streams : `GstStreamCollection`
3. Events from the app allowing selection and activation of some streams:
   `GST_EVENT_SELECT_STREAMS`
4. Messages informing the user/application about the available streams and
   current status: `GST_MESSAGE_STREAM_COLLECTION` and
   `GST_MESSAGE_STREAMS_SELECTED`
5. A field on `GST_EVENT_SELECT_STREAMS` to inform downstream elements that a
   certain element can handle the stream selection itself.

##  GstStream objects

API:

``` c
typedef enum {
  GST_STREAM_TYPE_UNKNOWN   = 1 << 0,
  GST_STREAM_TYPE_AUDIO     = 1 << 1,
  GST_STREAM_TYPE_VIDEO     = 1 << 2,
  GST_STREAM_TYPE_CONTAINER = 1 << 3,
  GST_STREAM_TYPE_TEXT      = 1 << 4
} GstStreamType;

struct {
	const gchar *stream_id;
} GstStream;

GstStream *gst_stream_new(...);

GstStream *gst_stream_get_*(...);

GstStream *gst_stream_set_*(...);

GstStream *gst_event_set_stream(GstEvent *stream_start, GstStream *stream)

void gst_event_parse_stream (GstEvent * event, GstStream ** stream);
```

`GstStream` objects are a high-level convenience object containing information
regarding a possible data stream that can be exposed by GStreamer elements.

They are mostly the aggregation of information present in other GStreamer
components (`STREAM_START`, `CAPS`, `TAGS` events) but are not tied to the
presence of a `GstPad`, and for some use-cases provide information that the
existing components don't provide.


The various properties (available via setters/getters) of a `GstStream` object
are:
  - stream_id (from the `STREAM_START` event)
  - flags (from the `STREAM_START` event)
  - caps
  - tags
  - type (high-level type of stream: Audio, Video, Container,...)


`GstStream` objects can be subclassed so that they can be re-used by elements
already using the notion of stream (which is common for example in demuxers).

**Note**: While the intent of the design was indeed to allow providers to
subclass `GstStream`, it turns out it is better to *not* subclass it. Stream
providers should instead have separate structures to track their internal
properties of streams, and use a separate `GstStream` for storing information
they want to share.


Elements that create `GstStream` should also set it on the
`GST_EVENT_STREAM_START` event of the relevant pad. This helps downstream
elements have all information in one location.


## Exposing collections of streams

API:

``` c
struct GstStreamCollection;

gst_stream_collection_new(...);
gst_stream_collection_add_stream(...);
gst_stream_collection_get_size(...);
gst_stream_collection_get_stream(...);

GST_MESSAGE_STREAM_COLLECTION
gst_message_new_stream_collection(...)
gst_message_parse_stream_collection(...)

GST_EVENT_STREAM_COLLECTION
gst_event_new_stream_collection(...)
gst_event_parse_stream_collection(...)
```

Elements that create new streams (such as demuxers) or can create new streams
(like the HLS/DASH alternative streams) can list the streams they can make
available with the `GstStreamCollection` object.

Other elements that might generate `GstStreamCollections` are the DVD-VM, which
handles internal switching of tracks, or `parsebin` and `decodebin3` when it
aggregates and presents multiple internal stream sources as a single
configurable collection.


The `GstStreamCollection` object is a flat listing of `GstStream` objects.

The various properties of a `GstStreamCollection` are:
  - 'identifier'
    - the identifier of the collection (unique name)
    - Generated from the 'upstream stream id' (or stream ids, plural)

  - the list of `GstStreams` in the collection.

  - **Not implemented** : Flags
  
    For now, the only flag is `INFORMATIONAL` - used by container parsers to
    publish information about detected streams without allowing selection of the
    streams. *Solving this might be possible through different means and needs
    more investigation.*

  - **Not implemented yet** : The relationship between the various streams
  
    This specifies which streams are exclusive (can not be selected at the same
    time), are related (such as `LINKED_VIEW` or `ENHANCEMENT`), or need to be
    selected together. *This requires more investigation*


An element will inform outside components about that collection of streams via:

* a `GST_MESSAGE_STREAM_COLLECTION` message on the bus.
* a `GST_EVENT_STREAM_COLLECTION` on each source pads.

Applications and container bin elements can listen and collect the various
stream collections to know the full range of streams available within a
bin/pipeline.


Once posted on the bus, a `GstStreamCollection` is immutable. It is updated by
subsequent messages with a matching identifier.


If the element that provided the collection goes away, there is no way to know
that the streams are no longer valid (without having the user/app track that
element). The exception to that is if the bin containing that element (such as
`parsebin` or `decodebin3`) informs that the next collection is a replacement of
the former one.

The mutual exclusion and relationship lists use stream-ids rather than
`GstStream` references in order to avoid circular referencing problems.


### Usage from elements

When a demuxer knows the list of streams it can expose, it creates a new
`GstStream` for each stream it can provide with the appropriate information
(stream id, flag, tags, caps, ...).

The demuxer then creates a `GstStreamCollection` object in which it will put the
list of `GstStream` it can expose.  That collection is then both posted on the
bus (via a `GST_MESSAGE_COLLECTION`) and on each pad (via a
`GST_EVENT_STREAM_COLLECTION`).

That new collection must be posted on the bus *before* the changes are made
available. i.e. before pads corresponding to that selection are added/removed.

If an element can handle selecting a sub-set of those streams (for example a
HLS/DASH demuxer which only download the required streams), it can inform
downstream elements by answer the `GST_QUERY_SELECTABLE` with `TRUE`. It should
therefore be able to handle `GST_EVENT_SELECT_STREAMS` at any point in time
(including while posting the initial collection on the bus).

In order to be backwards-compatible and support elements that don't create
streams/collection yet, the `parsebin` element (used by `decodebin3`) will
automatically create the stream and collection if not provided by the elements
within.


### Usage from application

Applications can know what streams are available by listening to the
`GST_MESSAGE_STREAM_COLLECTION` messages posted on the bus.

The application can list the available streams per-type (such as all the audio
streams, or all the video streams) by iterating the streams available in the
collection by `GST_STREAM_TYPE`.

The application will also be able to use these stream information to decide
which streams should be activated or not (see the stream selection event below).


### Backwards compatibility

Not all demuxers will create the various `GstStream` and `GstStreamCollection`
objects. In order to remain backwards compatible, a parent bin (parsebin in
decodebin3) will create the `GstStream` and `GstStreamCollection` based on the
pads being added/removed from an element.

This allows providing stream listing/selection for any demuxer-like element even
if it doesn't implement the `GstStreamCollection` usage.


## Adding and removing pads dynamically

API:

``` c
GST_BIN_FLAG_STREAMS_AWARE
```

If an element is within a `GstBin` with the `GST_BIN_FLAG_STREAMS_AWARE` set, it
can add and remove streams (and corresponding source `GstPad`) dynamically. It
does **not** have to (re)add all pads of the new stream set, emit `no-more-pads`
and then remove olds source pads.

This method is more efficient than the legacy behaviour, but is opt-in. Several
elements (`playbin3`, `decodebin3`, `urisourcebin` and more) have this flag set
by default. Application developers creating custom `GstBin` can set the flag to
make compatible elements add/remove pads dynamically.


## Stream selection event

API:

``` c
GST_EVENT_SELECT_STREAMS

gst_event_new_select_streams(...)
gst_event_parse_select_streams(...)
```

Stream selection events are generated by the application and sent into the
pipeline to configure the streams.

The event carries:

  * List of `GstStreams` to activate
    * a subset of the `GstStreamCollection`

  * **Not implemented** List of `GstStreams` to be kept discarded
    * a subset of streams for which hot-swapping will not be desired, allowing
    elements (such as decodebin3, demuxers, ...) to not parse or buffer those
    streams at all. *This will be solved in a different fashion.*


### Usage from application

There are two use-cases where an application needs to specify in a generic
fashion which streams it wants in output:

1. When there are several present streams of which it only wants a subset (such
   as one audio, one video and one subtitle stream). Those streams are demuxed
   and present in the pipeline.
  
2. When the stream the user wants require some element to undertake some action
   to expose that stream in the pipeline (such as DASH/HLS alternative streams).


From the point of view of the application, those two use-cases are treated
identically.  The streams are all available through the `GstStreamCollection`
posted on the bus, and the applications selects a subset.

The application can select the streams it wants by creating a
`GST_EVENT_SELECT_STREAMS` event with the list of stream-id of the streams it
wants. That event is then sent on the pipeline, eventually traveling all the way
upstream from each sink.

In some cases, selecting one stream may trigger the availability of other
dependent streams, resulting in new `GstStreamCollection` messages. This can
happen in the case where choosing a different DVB channel would create a new
single-program collection.


### Usage in elements

Elements that receive the `GST_EVENT_SELECT_STREAMS` event and that can
activate/deactivate streams need to look at the list of stream-id contained in
the event and decide if they need to do some action.

In the standard demuxer case (demuxing and exposing all streams), there is
nothing to do by default (see below).

In `decodebin3`, activating or deactivating streams is taken care of by linking
only the streams present in the event to decoders and output ghostpad.

In the case of elements that can expose alternate streams that are not present
in the pipeline as pads, they will take the appropriate action to add/remove
those streams.

Containers that receive the event (via `GstElement::send_event()`) should pass
it to any elements with no downstream peers, so that streams can be configured
during pre-roll before a pipeline is completely linked down to sinks.


#### Which elements should handle stream-selection ?

By default demuxers *must* process and expose all streams contained within the
multiplex. This is to allow downstream stream switching to happen as fast as
possible.

The only exception to this is if extra processing (such as network download) can
be avoided upstream as is the case with HLS/DASH sources. Those elements inform
other downstream elements of this capability by replying to the
`GST_QUERY_SELECTABLE`. They are also responsible for selecting a default
selection of streams to output (one of each available `GstStreamType`) if no
`GST_EVENT_SELECT_STREAMS` was received before.

Furthermore, for all elements, stream-selection can only happen if the `GstBin`
they are in has the `GST_BIN_FLAG_STREAMS_AWARE` flag set.

Later design/API will be researched to allow use-cases where regular demuxers
could completely disable outputting some of the contained streams (to allow more
efficient I/O and memory usage).


## decodebin3 usage and example

This is an example of how decodebin3 works by using the above-mentioned
objects/events/messages.

For clarity/completeness, we will consider a MPEG-TS stream that has multiple
audio streams. Furthermore that stream might have changes at some point
(switching video codec, or adding/removing audio streams).


### Initial differences with `decodebin2`

`decodebin3` is different, compared to `decodebin2`, in the sense that, by
default:

* it will only expose as output ghost source pads one stream of each type (one
  audio, one video, ..).
* It will only decode the exposed streams


The `multiqueue` element is still used and takes in all elementary (non-decoded)
streams. If parsers are needed/present they are placed before the
multiqueue. This is needed in order for multiqueue to work only with packetized
and properly timestamped streams.

Note that the whole typefinding of streams, and optional depayloading, demuxing
and parsing are done in a new `parsebin` element.

Just like the current implementation, demuxers will expose all streams present
within a program as source pads. They will connect to parsers and multiqueue.

Initial setup. 1 video stream, 2 audio streams.

```
  +---------------------+
  | parsebin            |
  | ---------           | +-------------+
  | | demux |--[parser]-+-| multiqueue  |--[videodec]---[
]-+-|       |--[parser]-+-|             |
  | |       |--[parser]-+-|             |--[audiodec]---[
  | ---------           | +-------------+
  +---------------------+
```

### Generating GstStreamCollection

When parsing the initial PAT/PMT, the demuxer will:

1. create the various `GstStream` objects for each stream.

2. create the `GstStreamCollection` for that initial PMT

3. post the `GST_MESSAGE_STREAM_COLLECTION`. Decodebin will intercept that
   message and know what the demuxer will be exposing.

4. The demuxer creates the various pads and sends the corresponding
   `STREAM_START` event (with the same stream-id as the corresponding
   `GstStream` objects), `CAPS` event, and `TAGS` event.

  * `parsebin` will add all relevant parsers and expose those streams.

  * `decodebin3` will be able to correlate, based on `STREAM_START` event
    stream-id, what pad corresponds to which stream. It links each stream from
    parsebin to multiqueue.

  * `decodebin3` knows all the streams that will be available. Since by default
    it is configured to only expose a stream of each type, it will pick a stream
    of each for which it will complete the auto-plugging (finding a decoder and
    then exposing that stream as a source ghostpad.

> Note: If the demuxer doesn't create/post the `GstStreamCollection`, `parsebin`
> will create it on itself, as explained in the previous sections above.


### Changing the active selection from the application

The user wants to change the audio track. The application received the
`GST_MESSAGE_STREAM_COLLECTION` containing the list of available streams. For
clarity, we will assume those stream-ids are `"video-main"`, `"audio-english"`
and `"audio-french"`.


The user prefers to use the french soundtrack (which it knows based on the
language tag contained in the `GstStream` objects).

The application will create and send a `GST_EVENT_SELECT_STREAM` event
containing the list of streams: `"video-main", "audio-french"`.

That event gets sent on the pipeline, the sinks send it upstream and eventually
reach `decodebin3`.

`decodebin3` compares:

* The currently active selection (`"video-main", "audio-english"`)
* The available stream collection (`"video-main", "audio-english",
  "audio-french"`)
* The list of streams in the event (`"video-main", "audio-french"`)

`decodebin3` determines that no change is required for `"video-main"`, but sees
that it needs to deactivate `"audio-english"` and activate `"audio-french"`.


It unlinks the `multiqueue` source pad connected to the current audio
decoder. Then it queries that audio decoder, using the `GST_QUERY_ACCEPT_CAPS`,
whether it can accept as-is the caps from the `"audio-french"` stream.

1. If it does, the multiqueue source pad corresponding to `"audio-french"` is
   linked to the decoder.
   
2. If it does not, the existing audio decoder is removed, a new decoder is
   selected (like during initial auto-plugging), and replaces the old audio
   decoder element.

The newly selected stream gets decoded and output through the same pad as the
previous audio stream.


*Note:* The default behavior is to only expose one stream of each type. But
nothing prevents `decodebin3` from outputting more/less of each type if the
`GST_EVENT_SELECT_STREAM` event specifies that. This allows covering more
use-case than the simple playback one. Such examples could be :
  * Wanting just a video stream or just an audio stream
  * Wanting all decoded streams
  * Wanting all audio streams
  ...


### Changes of stream collection

At some point in time, a PMT change happens. Let's assume a change in
video-codec and/or PID.

The demuxer creates a new `GstStream` for the changed/new stream, creates a new
`GstStreamCollection` for the updated PMT and posts it.


`decodebin3` sees the new `GstStreamCollection` message.

The demuxer (and `parsebin`) then adds and removes pads:

1. `decodebin3` will match the new pads to `GstStream` in the *new*
   `GstStreamCollection` the [same way it did](#generating-gststreamcollection)
   for the initial pads previously.
  
2. `decodebin3` will see whether the new stream can re-use a `multiqueue` slot
   used by a stream of the same type no longer present (it compares the old
   collection to the new collection).  In this case, `decodebin3` sees that the
   new video stream can re-use the same slot as the previous video stream.
  
3. If the new stream is going to be active by default (in this case it does
   because we are replacing the only video stream, which was active), it will
   check whether the caps are compatible with the existing video decoder (in the
   same way it was done for the [audio decoder
   switch](#changing-the-active-selection-from-the-application)).


Finally, the stream that switched will be decoded and output through the same
pad as the previous video stream in a gapless fashion.


### Further examples

##### DASH/HLS alternates (selection handled upstream of decodebin3)

There is a main (multi-bitrate or not) stream with audio and video interleaved
in MPEG-TS. The manifest also indicates the presence of alternate language
audio-only streams.

* (main) variant stream : `video`, `audio-eng`
* alternate rendition streams : `audio-kor`, `audio-fre`

A new element, `hlsdemux2`, used within `urisourcebin`, exposes one collection
containing all individual elementary streams (regardless of bitrates). This is
based on the contents of the manifest:

* `[ video , audio-eng, audio-kor, audio-fre ]`

Similar to other demuxers, it will post the collection on the bus, and will
add/remove pads dynamically based on `GST_EVENT_SELECT_STREAMS` (or make a
default choice if no event was received).

Since it can offer a selection of "elementary" streams, it will internally
demux, parse and buffer the requested streams.

If ever it receives the `GST_QUERY_SELECTABLE`, it will set the `selectable`
field of that query to TRUE. This informs `decodebin3` that it should handle all
incoming streams and not do any selection (it is done upstream).

```
  +-----------+               + decodebin3 -----------------------------+
  | hlsdemux2 |               |                                         |
  |           |               |             +------------+              |
  |           +-   `video`   -+-[parsebin]--+ multiqueue +--[videodec]--[
  |           +- `audio-eng` -+-[parsebin]--+------------+--[audiodec]--[
  |           |               |                                         |
  +-----------+               +-----------------------------------------+
```

The user might want to use the korean audio track instead of the default english
one.

```
  => SELECT_STREAMS ("video", "kor")
```

0) When `decodebin3` received `GST_EVENT_STREAM_START` on the *initial* incoming
  streams, it sent a `GST_QUERY_SELECTABLE` which `hlsdemux2` answered
  succesfully. `decodebin3` therefore knows that the upstream of that stream can
  handle stream-selection itself.

1) `decodebin3` receives the new `SELECT_STREAMS` event, and knows that upstream
  can handle stream selection and therefore forwards it upstream.

2) `hlsdemux2` receives the event, and prepares the `audio-kor` backing stream
  to be downloaded and returns. `decodebin3` also returns the event as is and
  doesn't do any selection itself.

3) Once `hlsdemux2` has downloaded enough from the target `audio-kor` stream,
  which it has demuxed, parsed and buffered internally, it will output it to the
  same output pad (replacing `audio-eng`):
  * Send a new `GST_EVENT_STREAM_START` containing the information about
    `audio-kor`
  * Send the various required events (caps, stream-collection,...)
  * Send the buffers for the new stream

4) If the incoming stream isn't compatible with the existing `parsebin`,
  `decodebin3` will reset it so that it reconfigures itself.


```
  +-----------+               + decodebin3 -----------------------------+
  | hlsdemux2 |               |                                         |
  |           |               |             +------------+              |
  |           +-   `video`   -+-[parsebin]--+ multiqueue +--[videodec]--[
  |           +- `audio-kor` -+-[parsebin]--+------------+--[audiodec]--[
  |           |               |                                         |
  +-----------+               +-----------------------------------------+
```


#### Multi-program MPEG-TS

**NOTE:** Not properly handled yet.

Assuming the case of a MPEG-TS stream which contains multiple programs.

There would be three "levels" of collection:
1. The collection of programs presents in the stream
2. The collection of elementary streams presents in a stream
3. The collection of streams decodebin can expose

Initially `tsdemux` exposes the first program present (default)

```
    [ Collection 1 ]         [ Collection 2     ]        [ Collection 3    ]
    [  (tsdemux)   ]         [   (tsdemux)      ]        [ (decodebin)     ]
    [ id:Programs  ]<-\      [ id:BBC1          ]<-\     [ id:BBC1-decoded ]
    [ upstream:nil ]   \-----[ upstream:Programs]   \----[ upstream:BBC1   ]
    [              ]         [                  ]        [                 ]
    [ "BBC1" (C)   ]         [ id:"bbcvideo"(V) ]        [ id:"bbcvideo"(V)]
    [ "ITV"  (C)   ]         [ id:"bbcaudio"(A) ]        [ id:"bbcaudio"(A)]
    [ "NBC"  (C)   ]         [                  ]        [                 ]
```

At some point the user wants to switch to ITV (of which we do not know the
topology at this point in time. A `SELECT_STREAMS` event is sent with "ITV" in
it and the pointer to the Collection1.

1. The event travels up the pipeline until `tsdemux` receives it and begins the
   switch.

2. `tsdemux` publishes a new 'Collection 2a/ITV' and marks 'Collection 2/BBC' as
   replaced.

   1. App may send a `SELECT_STREAMS` event configuring which demuxer output
      streams should be selected (parsed)

3. `tsdemux` adds/removes pads as needed (flushing pads as it removes them?)

4. `decodebin3` feeds new pad streams through existing parsers/decoders as
   needed. As data from the new collection arrives out each decoder,
   `decodebin3` sends new `GstStreamCollection` messages to the app so it can
   know that the new streams are now switchable at that level.
   
   1. As new `GstStreamCollections` are published, the app may override the
      default decodebin stream selection to expose more/fewer streams.  The
      default is to decode and output 1 stream of each type.


Final state:

```
    [ Collection 1 ]         [ Collection 4     ]        [ Collection 5    ]
    [  (tsdemux)   ]         [   (tsdemux)      ]        [ (decodebin)     ]
    [ id:Programs  ]<-\      [ id:ITV           ]<-\     [ id:ITV-decoded  ]
    [ upstream:nil ]   \-----[ upstream:Programs]   \----[ upstream:ITV    ]
    [              ]         [                  ]        [                 ]
    [ "BBC1" (C)   ]         [ id:"itvvideo"(V) ]        [ id:"itvvideo"(V)]
    [ "ITV"  (C)   ]         [ id:"itvaudio"(A) ]        [ id:"itvaudio"(A)]
    [ "NBC"  (C)   ]         [                  ]        [                 ]
```


### TODO

- Figure out how to handle *mutual-exclusion* and *relationship* API with
  collection of streams.

- ~~Add flags to `GstStreamCollection`~~

- Add helper API to figure out whether a collection is a replacement of another
  or a completely new one. This will require a more generic system to know
  whether a certain stream-id is a replacement of another or not.
  

### Open Questions

- ~~Is a `FLUSHING` flag for stream-selection required or not?~~

  This would make the handler of the `SELECT_STREAMS` event send `FLUSH
  START/STOP` before switching to the other streams. This is tricky when dealing
  where situations where we keep some streams and only switch some others. Do we
  flush all streams? Do we only flush the new streams, potentially resulting in
  delay to fully switch?  Furthermore, due to efficient buffering in
  `decodebin3`, the switching time has been minimized extensively, to the point
  where flushing might not bring a noticeable improvement.
  
  **Note:** This flushing capability was not needed in the end. Leveraging
  minimal buffering in `decodebin3` and various other elements allows
  *almost-instantaneous* stream switching without this.


- Store the stream collection in bins/pipelines?

  A Bin/Pipeline could store all active collection internally, so that it could
  be queried later on. This could be useful to then get, on any pipeline, at any
  point in time, the full list of collections available without having to listen
  to all COLLECTION messages on the bus. This would require fixing the "is a
  collection a replacement or not" issue first.


- When switching to new collections, should `decodebin3` make any effort to
  *map* corresponding streams from the old to new PMT - that is, try and stick
  to the *english* language audio track, for example?  Alternatively, rely on
  the app to do such smarts with stream-select messages?

  **Note:** If an application listens synchronously to the collection bus
  messages, it can indeed decide which streams it wants to select. And therefore
  stick to the (new) english audio track.
