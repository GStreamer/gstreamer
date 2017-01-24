# Bufferpool

This document details the design of how buffers are allocated and
managed in pools.

Bufferpools increase performance by reducing allocation overhead and
improving possibilities to implement zero-copy memory transfer.

Together with the ALLOCATION query, elements can negotiate allocation
properties and bufferpools between themselves. This also allows elements
to negotiate buffer metadata between themselves.

## Requirements

- Provide a `GstBufferPool` base class to help the efficient
implementation of a list of reusable `GstBuffer` objects.

- Let upstream elements initiate the negotiation of a bufferpool and
its configuration. Allow downstream elements provide bufferpool
properties and/or a bufferpool. This includes the following
properties:

  - have minimum and maximum amount of buffers with the option of
    preallocating buffers.

  - allocator, alignment and padding support

  - buffer metadata

  - arbitrary extra options

- Integrate with dynamic caps renegotiation.

- Notify upstream element of new bufferpool availability. This is
important when a new element, that can provide a bufferpool, is
dynamically linked downstream.

## GstBufferPool

The bufferpool object manages a list of buffers with the same properties such
as size, padding and alignment.

The bufferpool has two states: active and inactive. In the inactive
state, the bufferpool can be configured with the required allocation
preferences. In the active state, buffers can be retrieved from and
returned to the pool.

The default implementation of the bufferpool is able to allocate buffers
from any allocator with arbitrary alignment and padding/prefix.

Custom implementations of the bufferpool can override the allocation and
free algorithms of the buffers from the pool. This should allow for
different allocation strategies such as using shared memory or hardware
mapped memory.

## Negotiation

After a particular media format has been negotiated between two pads (using the
CAPS event), they must agree on how to allocate buffers.

The srcpad will always take the initiative to negotiate the allocation
properties. It starts with creating a `GST_QUERY_ALLOCATION` with the negotiated
caps.

The srcpad can set the need-pool flag to TRUE in the query to optionally make
the peer pad allocate a bufferpool. It should only do this if it is able to use
the peer provided bufferpool.

It will then inspect the returned results and configure the returned pool or
create a new pool with the returned properties when needed.

Buffers are then allocated by the srcpad from the negotiated pool and pushed to
the peer pad as usual.

The allocation query can also return an allocator object when the buffers are
of different sizes and can't be allocated from a pool.

## Allocation query

The allocation query has the following fields:

* (in) **`caps`**, `GST_TYPE_CAPS`: the caps that was negotiated

* (in) **`need-pool`**, `G_TYPE_BOOLEAN`: if a `GstBufferPool` is requested

* (out) **`pool`**, `G_TYPE_ARRAY` of structure: an array of pool configurations:

``` c
    struct {
      GstBufferPool *pool;
      guint          size;
      guint          min_buffers;
      guint          max_buffers;
    }
```

Use `gst_query_parse_nth_allocation_pool()` to get the values.

The allocator can contain multiple pool configurations. If need-pool
was TRUE, the pool member might contain a `GstBufferPool` when the
downstream element can provide one.

Size contains the size of the bufferpool's buffers and is never 0.

`min_buffers` and `max_buffers` contain the suggested min and max amount of
buffers that should be managed by the pool.

The upstream element can choose to use the provided pool or make its own
pool when none was provided or when the suggested pool was not
acceptable.

The pool can then be configured with the suggested min and max amount of
buffers or a downstream element might choose different values.

* (out) **`allocator`**, `G_TYPE_ARRAY` of structure: an array of allocator
parameters that can be used.

``` c
    struct {
      GstAllocator *allocator;
      GstAllocationParams params;
    }
```

Use `gst_query_parse_nth_allocation_param()` to get the values.

The element performing the query can use the allocators and its
parameters to allocate memory for the downstream element.

It is also possible to configure the allocator in a provided pool.

* (out) **`metadata`**, `G_TYPE_ARRAY` of structure: an array of metadata
params that can be accepted.

``` c
    struct {
      GType api;
      GstStructure *params;
    }
```

Use `gst_query_parse_nth_allocation_meta()` to get the values.

These metadata items can be accepted by the downstream element when
placed on buffers. There is also an arbitrary `GstStructure` associated
with the metadata that contains metadata-specific options.

Some bufferpools have options to enable metadata on the buffers
allocated by the pool.

## Allocating from pool

Buffers are allocated from the pool of a pad:

``` c
res = gst_buffer_pool_acquire_buffer (pool, &buffer, &params);
```

A `GstBuffer` that is allocated from the pool will always be writable (have a
refcount of 1) and it will also have its pool member point to the
`GstBufferPool` that created the buffer.

Buffers are refcounted in the usual way. When the refcount of the buffer
reaches 0, the buffer is automatically returned to the pool.

Since all the buffers allocated from the pool keep a reference to the pool,
when nothing else is holding a refcount to the pool, it will be finalized
when all the buffers from the pool are unreffed. By setting the pool to
the inactive state we can drain all buffers from the pool.

When the pool is in the inactive state, `gst_buffer_pool_acquire_buffer()` will
return `GST_FLOW_FLUSHING` immediately.

Extra parameters can be given to the `gst_buffer_pool_acquire_buffer()` method
to influence the allocation decision. `GST_BUFFER_POOL_ACQUIRE_FLAG_KEY_UNIT`
and `GST_BUFFER_POOL_ACQUIRE_FLAG_DISCONT` serve as hints.

When the bufferpool is configured with a maximum number of buffers, allocation
will block when all buffers are outstanding until a buffer is returned to the
pool. This behaviour can be changed by specifying the
`GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT` flag in the parameters. With this flag
set, allocation will return `GST_FLOW_EOS` when the pool is empty.

## Renegotiation

Renegotiation of the bufferpool might need to be performed when the
configuration of the pool changes. Changes can be in the buffer size
(because of a caps change), alignment or number of buffers.

### Downstream

When the upstream element wants to negotiate a new format, it might need
to renegotiate a new bufferpool configuration with the downstream element.
This can, for example, happen when the buffer size changes.

We can not just reconfigure the existing bufferpool because there might
still be outstanding buffers from the pool in the pipeline. Therefore we
need to create a new bufferpool for the new configuration while we let the
old pool drain.

Implementations can choose to reuse the same bufferpool object and wait for
the drain to finish before reconfiguring the pool.

The element that wants to renegotiate a new bufferpool uses exactly the same
algorithm as when it first started. It will negotiate caps first then use the
ALLOCATION query to get and configure the new pool.

### Upstream

When a downstream element wants to negotiate a new format, it will send a
RECONFIGURE event upstream. This instructs upstream to renegotiate both
the format and the bufferpool when needed.

A pipeline reconfiguration happens when new elements are added or removed from
the pipeline or when the topology of the pipeline changes. Pipeline
reconfiguration also triggers possible renegotiation of the bufferpool and
caps.

A RECONFIGURE event tags each pad it travels on as needing reconfiguration.
The next buffer allocation will then require the renegotiation or
reconfiguration of a pool.

## Shutting down

In push mode, a source pad is responsible for setting the pool to the
inactive state when streaming stops. The inactive state will unblock any pending
allocations so that the element can shut down.

In pull mode, the sink element should set the pool to the inactive state when
shutting down so that the peer `_get_range()` function can unblock.

In the inactive state, all the buffers that are returned to the pool will
automatically be freed by the pool and new allocations will fail.

## Use cases

### `videotestsrc ! xvimagesink`

* Before videotestsrc can output a buffer, it needs to negotiate caps and
a bufferpool with the downstream peer pad.

* First it will negotiate a suitable format with downstream according to the
normal rules. It will send a `CAPS` event downstream with the negotiated
configuration.

* Then it does an `ALLOCATION` query. It will use the returned bufferpool or
configures its own bufferpool with the returned parameters. The bufferpool is
initially in the inactive state.

* The `ALLOCATION` query lists the desired configuration of the downstream
xvimagesink, which can have specific alignment and/or min/max amount of
buffers.

* videotestsrc updates the configuration of the bufferpool, it will likely set
the min buffers to 1 and the size of the desired buffers. It then updates the
bufferpool configuration with the new properties.

* When the configuration is successfully updated, videotestsrc sets the
bufferpool to the active state. This preallocates the buffers in the pool (if
needed). This operation can fail when there is not enough memory available.
Since the bufferpool is provided by xvimagesink, it will allocate buffers
backed by an XvImage and pointing to shared memory with the X server.

* If the bufferpool is successfully activated, videotestsrc can acquire
a buffer from the pool, fill in the data and push it out to xvimagesink.

* xvimagesink can know that the buffer originated from its pool by following
the pool member.

* when shutting down, videotestsrc will set the pool to the inactive state,
this will cause further allocations to fail and currently allocated buffers to
be freed. videotestsrc will then free the pool and stop streaming.

### `videotestsrc ! queue ! myvideosink`

* In this second use case we have a videosink that can at most allocate 3 video
buffers.

* Again videotestsrc will have to negotiate a bufferpool with the peer element.
For this it will perform the `ALLOCATION` query which queue will proxy to its
downstream peer element.

* The bufferpool returned from myvideosink will have a `max_buffers` set to 3.
queue and videotestsrc can operate with this upper limit because none of those
elements require more than that amount of buffers for temporary storage.

* Myvideosink's bufferpool will then be configured with the size of the buffers
for the negotiated format and according to the padding and alignment rules.
When videotestsrc sets the pool to active, the 3 video buffers will be
preallocated in the pool.

* videotestsrc acquires a buffer from the configured pool on its srcpad and
pushes this into the queue. When videotestsrc has acquired and pushed 3 frames,
the next call to `gst_buffer_pool_acquire_buffer()` will block (assuming the
`GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT` is not specified).

* When the queue has pushed out a buffer and the sink has rendered it, the
refcount of the buffer reaches 0 and the buffer is recycled in the pool. This
will wake up the videotestsrc that was blocked, waiting for more buffers and
will make it produce the next buffer.

* In this setup, there are at most 3 buffers active in the pipeline and the
videotestsrc is rate limited by the rate at which buffers are recycled in the
bufferpool.

* When shutting down, videotestsrc will first set the bufferpool on the srcpad
to inactive. This causes any pending (blocked) acquire to return with
a FLUSHING result and causes the streaming thread to pause.

### `.. ! myvideodecoder ! queue ! fakesink`

* In this case, the myvideodecoder requires buffers to be aligned to 128 bytes
and padded with 4096 bytes. The pipeline starts out with the decoder linked to
a fakesink but we will then dynamically change the sink to one that can provide
a bufferpool.

* When myvideodecoder negotiates the size with the downstream fakesink element,
it will receive a NULL bufferpool because fakesink does not provide
a bufferpool. It will then select its own custom bufferpool to start the data
transfer.

* At some point we block the queue srcpad, unlink the queue from the fakesink,
link a new sink and set the new sink to the PLAYING state. Linking the new sink
would automatically send a RECONFIGURE event upstream and, through queue,
inform myvideodecoder that it should renegotiate its bufferpool because
downstream has been reconfigured.

* Before pushing the next buffer, myvideodecoder has to renegotiate a new
bufferpool. To do this, it performs the usual bufferpool negotiation algorithm.
If it can obtain and configure a new bufferpool from downstream, it sets its
own (old) pool to inactive and unrefs it. This will eventually drain and unref
the old bufferpool.

* The new bufferpool is set as the new bufferpool for the srcpad and sinkpad of
the queue and set to the active state.

### `.. ! myvideodecoder ! queue ! myvideosink`

* myvideodecoder has negotiated a bufferpool with the downstream myvideosink to
handle buffers of size 320x240. It has now detected a change in the video
format and needs to renegotiate to a resolution of 640x480. This requires it to
negotiate a new bufferpool with a larger buffer size.

* When myvideodecoder needs to get the bigger buffer, it starts the negotiation
of a new bufferpool. It queries a bufferpool from downstream, reconfigures it
with the new configuration (which includes the bigger buffer size) and sets the
bufferpool to active. The old pool is inactivated and unreffed, which causes
the old format to drain.

* It then uses the new bufferpool for allocating new buffers of the new
dimension.

* If at some point, the decoder wants to switch to a lower resolution again, it
can choose to use the current pool (which has buffers that are larger than the
required size) or it can choose to renegotiate a new bufferpool.

### `.. ! myvideodecoder ! videoscale ! myvideosink`

* myvideosink is providing a bufferpool for upstream elements and wants to
change the resolution.

* myvideosink sends a `RECONFIGURE` event upstream to notify upstream that a new
format is desirable. Upstream elements try to negotiate a new format and
bufferpool before pushing out a new buffer. The old bufferpools are drained in
the regular way.
