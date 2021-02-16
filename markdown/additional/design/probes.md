# Probes

Probes are callbacks that can be installed by the application and will notify
the application about the states of the dataflow.

## Requirements

Applications should be able to monitor and control the dataflow on pads.
We identify the following types:

  - be notified when the pad is/becomes idle and make sure the pad stays
    idle. This is essential to be able to implement dynamic relinking of
    elements without breaking the dataflow.

  - be notified when data, events or queries are pushed or sent on a
    pad. It should also be possible to inspect and modify the data.

  - be able to drop, pass and block on data based on the result of the
    callback.

  - be able to drop, pass data on blocking pads based on methods
    performed by the application
    thread.

## Overview

The function `gst_pad_add_probe()` is used to add a probe to a pad. It accepts a
probe type mask and a callback.

``` c
    gulong  gst_pad_add_probe    (GstPad *pad,
                                  GstPadProbeType mask,
                                  GstPadProbeCallback callback,
                                  gpointer user_data,
                                  GDestroyNotify destroy_data);
```

The function returns a gulong that uniquely identifies the probe and that can
be used to remove the probe with `gst_pad_remove_probe()`:

``` c
    void    gst_pad_remove_probe (GstPad *pad, gulong id);
```

The mask parameter is a bitwise or of the following flags:

``` c
typedef enum
{
  GST_PAD_PROBE_TYPE_INVALID          = 0,

  /* flags to control blocking */
  GST_PAD_PROBE_TYPE_IDLE             = (1 << 0),
  GST_PAD_PROBE_TYPE_BLOCK            = (1 << 1),

  /* flags to select datatypes */
  GST_PAD_PROBE_TYPE_BUFFER           = (1 << 4),
  GST_PAD_PROBE_TYPE_BUFFER_LIST      = (1 << 5),
  GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM = (1 << 6),
  GST_PAD_PROBE_TYPE_EVENT_UPSTREAM   = (1 << 7),
  GST_PAD_PROBE_TYPE_EVENT_FLUSH      = (1 << 8),
  GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM = (1 << 9),
  GST_PAD_PROBE_TYPE_QUERY_UPSTREAM   = (1 << 10),

  /* flags to select scheduling mode */
  GST_PAD_PROBE_TYPE_PUSH             = (1 << 12),
  GST_PAD_PROBE_TYPE_PULL             = (1 << 13),
} GstPadProbeType;
```

When adding a probe with the IDLE or BLOCK flag, the probe will become a
blocking probe (see below). Otherwise the probe will be a DATA probe.

The datatype and scheduling selector flags are used to select what kind of
datatypes and scheduling modes should be allowed in the callback.

The blocking flags must match the triggered probe exactly.

The probe callback is defined as:

``` c
GstPadProbeReturn (*GstPadProbeCallback) (GstPad *pad, GstPadProbeInfo *info,
    gpointer user_data);
```

A probe info structure is passed as an argument and its type is guaranteed
to match the mask that was used to register the callback. The data item in the
info contains type specific data, which is usually the data item that is blocked
or `NULL` when no data item is present.

The probe can return any of the following return values:

``` c
typedef enum
{
  GST_PAD_PROBE_DROP,
  GST_PAD_PROBE_OK,
  GST_PAD_PROBE_REMOVE,
  GST_PAD_PROBE_PASS,
  GST_PAD_PROBE_HANDLED
} GstPadProbeReturn;
```

`GST_PAD_PROBE_OK` is the normal return value. `_DROP` will drop the item that is
currently being probed. `GST_PAD_PROBE_REMOVE`: remove the currently executing probe from the
list of probes.

`GST_PAD_PROBE_PASS` is relevant for blocking probes and will temporarily unblock the
pad and let the item through, it will then block again on the next item.

## Blocking probes

Blocking probes are probes with `BLOCK` or `IDLE` flags set. They will always
block the dataflow and trigger the callback according to the following rules:

When the `IDLE` flag is set, the probe callback is called as soon as no data is
flowing over the pad. If at the time of probe registration, the pad is idle,
the callback will be called immediately from the current thread. Otherwise,
the callback will be called as soon as the pad becomes idle in the streaming
thread.

The `IDLE` probe is useful in performing dynamic linking, allowing the application
to wait to correctly execute an unlink/link operation. Since the probe is a
blocking probe, it will also make sure that the pad stays idle until the probe
is removed.

When the `BLOCK` flag is set, the probe callback will be called when new data
arrives on the pad and right before the pad goes into the blocking state. This
callback is thus only called when there is new data on the pad.

The blocking probe is removed with `gst_pad_remove_probe()` or when the probe
callback return `GST_PAD_PROBE_REMOVE`. In both cases, and if this was the last
blocking probe on the pad, the pad is unblocked and dataflow can continue.

## Non-Blocking probes

Non-blocking probes or DATA probes are probes triggered when data is flowing
over the pad. They are called after the blocking probes are run and always with
data.

## Push dataflow

Push probes have the `GST_PAD_PROBE_TYPE_PUSH` flag set in the
callbacks.

In push based scheduling, the blocking probe is called first with the
data item. Then the data probes are called before the peer pad chain or
event function is called.

The data probes are called before the peer pad is checked. This allows
for linking the pad in either the BLOCK or DATA probes on the pad.

Before the peerpad chain or event function is called, the peer pad block
and data probes are called.

Finally, the `IDLE` probe is called on the pad after the data was sent to
the peer pad.

The push dataflow probe behavior is the same for buffers and
bidirectional events.

```
                    pad                           peerpad
                     |                               |
gst_pad_push() /     |                               |
gst_pad_push_event() |                               |
-------------------->O                               |
                     O                               |
       flushing?     O                               |
       FLUSHING      O                               |
       < - - - - - - O                               |
                     O-> do BLOCK probes             |
                     O                               |
                     O-> do DATA probes              |
        no peer?     O                               |
       NOT_LINKED    O                               |
       < - - - - - - O                               |
                     O   gst_pad_chain() /           |
                     O   gst_pad_send_event()        |
                     O------------------------------>O
                     O                   flushing?   O
                     O                   FLUSHING    O
                     O< - - - - - - - - - - - - - - -O
                     O                               O-> do BLOCK probes
                     O                               O
                     O                               O-> do DATA probes
                     O                               O
                     O                               O---> chainfunc /
                     O                               O     eventfunc
                     O< - - - - - - - - - - - - - - -O
                     O                               |
                     O-> do IDLE probes              |
                     O                               |
       < - - - - - - O                               |
                     |                               |
```

## Pull dataflow

Pull probes have the `GST_PAD_PROBE_TYPE_PULL` flag set in the
callbacks.

The `gst_pad_pull_range()` call will first trigger the `BLOCK` probes
without a `DATA` item. This allows the pad to be linked before the peer
pad is resolved. It also allows the callback to set a data item in the
probe info.

After the blocking probe and the getrange function is called on the peer
pad and there is a data item, the DATA probes are called.

When control returns to the sinkpad, the `IDLE` callbacks are called. The
`IDLE` callback is called without a data item so that it will also be
called when there was an error.

If there is a valid `DATA` item, the `DATA` probes are called for the item.

```
                srcpad                          sinkpad
                  |                               |
                  |                               | gst_pad_pull_range()
                  |                               O<---------------------
                  |                               O
                  |                               O  flushing?
                  |                               O  FLUSHING
                  |                               O - - - - - - - - - - >
                  |             do BLOCK probes <-O
                  |                               O   no peer?
                  |                               O  NOT_LINKED
                  |                               O - - - - - - - - - - >
                  |          gst_pad_get_range()  O
                  O<------------------------------O
                  O                               O
                  O flushing?                     O
                  O FLUSHING                      O
                  O- - - - - - - - - - - - - - - >O
do BLOCK probes <-O                               O
                  O                               O
 getrangefunc <---O                               O
                  O  flow error?                  O
                  O- - - - - - - - - - - - - - - >O
                  O                               O
 do DATA probes <-O                               O
                  O- - - - - - - - - - - - - - - >O
                  |                               O
                  |              do IDLE probes <-O
                  |                               O   flow error?
                  |                               O - - - - - - - - - - >
                  |                               O
                  |              do DATA probes <-O
                  |                               O - - - - - - - - - - >
                  |                               |
```

## Queries

Query probes have the `GST_PAD_PROBE_TYPE_QUERY_*` flag set in the
callbacks.

```
                    pad                           peerpad
                     |                               |
gst_pad_peer_query() |                               |
-------------------->O                               |
                     O                               |
                     O-> do BLOCK probes             |
                     O                               |
                     O-> do QUERY | PUSH probes      |
        no peer?     O                               |
          FALSE      O                               |
       < - - - - - - O                               |
                     O   gst_pad_query()             |
                     O------------------------------>O
                     O                               O-> do BLOCK probes
                     O                               O
                     O                               O-> do QUERY | PUSH probes
                     O                               O
                     O                               O---> queryfunc
                     O                    error      O
       <- - - - - - - - - - - - - - - - - - - - - - -O
                     O                               O
                     O                               O-> do QUERY | PULL probes
                     O< - - - - - - - - - - - - - - -O
                     O                               |
                     O-> do QUERY | PULL probes      |
                     O                               |
       < - - - - - - O                               |
                     |                               |
```

For queries, the `PUSH` `ProbeType` is set when the query is traveling to
the object that will answer the query and the `PULL` type is set when the
query contains the answer.

## Use-cases

### Prerolling a partial pipeline

```
    .---------.      .---------.                .----------.
    | filesrc |      | demuxer |     .-----.    | decoder1 |
    |        src -> sink      src1 ->|queue|-> sink       src
    '---------'      |         |     '-----'    '----------' X
                     |         |                .----------.
                     |         |     .-----.    | decoder2 |
                     |        src2 ->|queue|-> sink       src
                     '---------'     '-----'    '----------' X
```

The purpose is to create the pipeline dynamically up to the decoders but
not yet connect them to a sink and without losing any data.

To do this, the source pads of the decoders is blocked so that no events
or buffers can escape and we don’t interrupt the stream.

When all of the dynamic pads are created (no-more-pads emitted by the
branching point, ie, the demuxer or the queues filled) and the pads are
blocked (blocked callback received) the pipeline is completely
prerolled.

It should then be possible to perform the following actions on the
prerolled pipeline:

  - query duration/position

  - perform a flushing seek to preroll a new position

  - connect other elements and unblock the blocked pads.

### dynamically switching an element in a PLAYING pipeline

```
 .----------.      .----------.      .----------.
 | element1 |      | element2 |      | element3 |
...        src -> sink       src -> sink       ...
 '----------'      '----------'      '----------'
                   .----------.
                   | element4 |
                  sink       src
                   '----------'
```

The purpose is to replace element2 with element4 in the `PLAYING`
pipeline.

1) block element1 src pad.
2) inside the block callback nothing is flowing between
   element1 and element2 and nothing will flow until unblocked.
3) unlink element1 and element2
4) optional step: make sure data is flushed out of element2:
   4a) pad event probe on element2 src
   4b) send `EOS` to element2, this makes sure that element2 flushes out the last bits of data it holds.
   4c) wait for `EOS` to appear in the probe, drop the `EOS`.
   4d) remove the `EOS` pad event probe.
5) unlink element2 and element3
   5a) optionally element2 can now be set to `NULL` and/or removed from the pipeline.
6) link element4 and element3
7) link element1 and element4
8) make sure element4 is in the same state as the rest of the elements. The
   element should at least be `PAUSED`.
9) unblock element1 src

The same flow can be used to replace an element in a `PAUSED` pipeline. Of
course in a `PAUSED` pipeline there might not be dataflow so the block
might not immediately happen.
