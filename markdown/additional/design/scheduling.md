# Scheduling

The scheduling in GStreamer is based on pads actively pushing
(producing) data or pulling (consuming) data from other pads.

## Pushing

A pad can produce data and push it to the next pad. A pad that behaves
this way exposes a loop function that will be repeatedly called until it
returns false. This loop function is allowed to block whenever it wants.
When the pad is deactivated the loop function should unblock though.

A pad operating in the push mode can only produce data to a pad that
exposes a chain function. This chain function will be called with the
buffer produced by the pushing pad.

This method of producing data is called the streaming mode since the
producer produces a constant stream of data.

## Pulling

Pads that operate in pulling mode can only pull data from a pad that
exposes the `pull_range()` function. In this case, the sink pad exposes a
loop function that will be called repeatedly until the task is stopped.

After pulling data from the peer pad, the loop function will typically
call the push function to push the result to the peer sinkpad.

## Deciding the scheduling mode

When a pad is activated, the `_activate()` function is called. The pad
can then choose to activate itself in push or pull mode depending on
upstream capabilities.

The GStreamer core will by default activate pads in push mode when there
is no activate function for the pad.

## The chain function

The chain function will be called when a upstream element performs a
`_push()` on the pad. The upstream element can be another chain based
element or a pushing source.

## The getrange function

The getrange function is called when a peer pad performs a
`_pull_range()` on the pad. This downstream pad can be a pulling element
or another `_pull_range()` based element.

## Scheduling Query

A sinkpad can ask the upstream srcpad for its scheduling attributes. It
does this with the `SCHEDULING` query.

* (out) **`modes`**: `G_TYPE_ARRAY` (default NULL): an array of `GST_TYPE_PAD_MODE` enums. Contains all the supported scheduling modes.

* (out) **`flags`**, `GST_TYPE_SCHEDULING_FLAGS` (default 0):

```c
typedef enum {
  GST_SCHEDULING_FLAG_SEEKABLE           = (1 << 0),
  GST_SCHEDULING_FLAG_SEQUENTIAL         = (1 << 1),
  GST_SCHEDULING_FLAG_BANDWIDTH_LIMITED  = (1 << 2)
} GstSchedulingFlags;
```

*
    * **`_SEEKABLE`**: the offset of a pull operation can be specified, if this
    flag is false, the offset should be -1.

    * **`_SEQUENTIAL`**: suggest sequential access to the data. If `_SEEKABLE`
    is specified, seeks are allowed but should be avoided. This is common for
    network streams.

    * **`_BANDWIDTH_LIMITED`**: suggest the element supports buffering data for
    downstream to cope with bandwidth limitations. If this flag is on, the
    downstream element might ask for more data than necessary for normal
    playback. This use-case is interesting for on-disk buffering scenarios for
    instance. Seek operations might be slow as well so downstream elements
    should take this into consideration.

* (out) **`minsize`**: `G_TYPE_INT` (default 1): the suggested minimum size of pull requests
* (out) **`maxsize`**: `G_TYPE_INT` (default -1, unlimited): the suggested maximum size of pull requests
* (out) **`align`**: `G_TYPE_INT` (default 0): the suggested alignment for the pull requests.

## Plug-in techniques

### Multi-sink elements

Elements with multiple sinks can either expose a loop function on each
of the pads to actively `pull_range` data or they can expose a chain
function on each pad.

Implementing a chain function is usually easy and allows for all
possible scheduling methods.

# Pad select

If the chain based sink wants to wait for one of the pads to receive a buffer, just
implement the action to perform in the chain function. Be aware that the action could
be performed in different threads and possibly simultaneously so grab the `STREAM_LOCK`.

# Collect pads

If the chain based sink pads all require one buffer before the element can operate on
the data, collect all the buffers in the chain function and perform the action when
all chainpads received the buffer.

In this case you probably also don't want to accept more data on a pad that has a buffer
queued. This can easily be done with the following code snippet:

``` c
static GstFlowReturn _chain (GstPad *pad, GstBuffer *buffer)
{
  LOCK (mylock);
  while (pad->store != NULL) {
    WAIT (mycond, mylock);
  }
  pad->store = buffer;
  SIGNAL (mycond);
  UNLOCK (mylock);

  return GST_FLOW_OK;
}

static void _pull (GstPad *pad, GstBuffer **buffer)
{
  LOCK (mylock);
  while (pad->store == NULL) {
    WAIT (mycond, mylock);
  }
  **buffer = pad->store;
  pad->store = NULL;
  SIGNAL (mycond);
  UNLOCK (mylock);
}
```

## Cases

Inside the braces below the pads is stated what function the pad
support:

* l: exposes a loop function, so it can act as a pushing source.
* g: exposes a getrange function
* c: exposes a chain function

Following scheduling decisions are made based on the scheduling methods exposed
by the pads:

* (g) - (l): sinkpad will pull data from src
* (l) - (c): srcpad actively pushes data to sinkpad
* ()  - (c): srcpad will push data to sinkpad.

* ()  - () : not schedulable.
* ()  - (l): not schedulable.
* (g) - () : not schedulable.
* (g) - (c): not schedulable.
* (l) - () : not schedulable.
* (l) - (l): not schedulable

* ()  - (g): impossible
* (g) - (g): impossible.
* (l) - (g): impossible
* (c) - () : impossible
* (c) - (g): impossible
* (c) - (l): impossible
* (c) - (c): impossible

```
+---------+    +------------+    +-----------+
| filesrc |    | mp3decoder |    | audiosink |
|        src--sink         src--sink         |
+---------+    +------------+    +-----------+
        (l-g) (c)           ()   (c)
```

When activating the pads:

  - audiosink has a chain function and the peer pad has no loop
    function, no scheduling is done.

  - mp3decoder and filesrc expose an (l) - (c) connection, a thread is
    created to call the srcpad loop function.

```
+---------+    +------------+    +----------+
| filesrc |    | avidemuxer |    | fakesink |
|        src--sink         src--sink        |
+---------+    +------------+    +----------+
        (l-g) (l)          ()   (c)
```

  - fakesink has a chain function and the peer pad has no loop function,
    no scheduling is done.

  - avidemuxer and filesrc expose an (g) - (l) connection, a thread is
    created to call the sinkpad loop function.

```
+---------+    +----------+    +------------+    +----------+
| filesrc |    | identity |    | avidemuxer |    | fakesink |
|        src--sink       src--sink         src--sink        |
+---------+    +----------+    +------------+    +----------+
        (l-g) (c)        ()   (l)          ()   (c)
```

  - fakesink has a chain function and the peer pad has no loop function,
    no scheduling is done.

  - avidemuxer and identity expose no schedulable connection so this
    pipeline is not schedulable.

```
+---------+    +----------+    +------------+    +----------+
| filesrc |    | identity |    | avidemuxer |    | fakesink |
|        src--sink       src--sink         src--sink        |
+---------+    +----------+    +------------+    +----------+
        (l-g) (c-l)      (g)  (l)          ()   (c)
```

  - fakesink has a chain function and the peer pad has no loop function,
    no scheduling is done.

  - avidemuxer and identity expose an (g) - (l) connection, a thread is
    created to call the sinkpad loop function.

  - identity knows the srcpad is getrange based and uses the thread from
    avidemux to getrange data from filesrc.

```
+---------+    +----------+    +------------+    +----------+
| filesrc |    | identity |    | oggdemuxer |    | fakesink |
|        src--sink       src--sink         src--sink        |
+---------+    +----------+    +------------+    +----------+
        (l-g) (c)        ()   (l-c)        ()   (c)
```

  - fakesink has a chain function and the peer pad has no loop function,
    no scheduling is done.

  - oggdemuxer and identity expose an () - (l-c) connection, oggdemux
    has to operate in chain mode.

  - identity chan only work chain based and so filesrc creates a thread
    to push data to it.
