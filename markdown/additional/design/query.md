# Query

## Purpose

Queries are used to get information about the stream. A query is started
on a specific pad and travels up or downstream.

## Requirements

  - multiple return values, grouped together when they make sense.

  - one pad function to perform the query

  - extensible queries.

## Implementation

  - `GstQuery` extends `GstMiniObject` and contains a `GstStructure` (see
    `GstMessage`)

  - some standard query types are defined below

  - methods to create and parse the results in the `GstQuery`.

  - define pad
        method:

``` c
        gboolean                (*GstPadQueryFunction)          (GstPad    *pad,
                                                                 GstObject *parent,
                                                                 GstQuery  *query);
```

pad returns result in query structure and TRUE as result or FALSE when query is
not supported.

## Query types

**`GST_QUERY_POSITION`**: get info on current position of the stream in `stream_time`.

**`GST_QUERY_DURATION`**: get info on the total duration of the stream.

**`GST_QUERY_LATENCY`**: get amount of latency introduced in the pipeline. (See [latency](additional/design/latency.md))

**`GST_QUERY_RATE`**: get the current playback rate of the pipeline

**`GST_QUERY_SEEKING`**: get info on how seeking can be done
    - getrange, with/without offset/size
    - ranges where seeking is efficient (for caching network sources)
    - flags describing seeking behaviour (forward, backward, segments,
                play backwards, ...)

**`GST_QUERY_SEGMENT`**: get info about the currently configured playback segment.

**`GST_QUERY_CONVERT`**: convert format/value to another format/value pair.

**`GST_QUERY_FORMATS`**: return list of supported formats that can be used for `GST_QUERY_CONVERT`.

**`GST_QUERY_BUFFERING`**: query available media for efficient seeking (See [buffering](additional/design/buffering.md))

**`GST_QUERY_CUSTOM`**: a custom query, the name of the query defines the properties of the query.

**`GST_QUERY_URI`**: query the uri of the source or sink element

**`GST_QUERY_ALLOCATION`**: the buffer allocation properties (See [bufferpool](additional/design/bufferpool.md))

**`GST_QUERY_SCHEDULING`**: the scheduling properties (See [scheduling](additional/design/scheduling.md))

**`GST_QUERY_ACCEPT_CAPS`**: check if caps are supported (See [negotiation](additional/design/negotiation.md))

**`GST_QUERY_CAPS`**: get the possible caps (See [negotiation](additional/design/negotiation.md))
