# GstBuffer

This document describes the design for buffers.

A `GstBuffer` is the object that is passed from an upstream element to a
downstream element and contains memory and metadata information.

## Requirements

  - It must be fast
      - allocation, free, low fragmentation
  - Must be able to attach multiple memory blocks to the buffer
  - Must be able to attach arbitrary metadata to buffers
  - efficient handling of subbuffer, copy, span, trim

## Lifecycle

`GstMemory` extends from `GstMiniObject` and therefore uses its lifecycle
management (See [miniobject](additional/design/miniobject.md)).

## Writability

When a `GstBuffer` is writable as returned by `gst_buffer_is_writable()`:

  - metadata can be added/removed and the metadata can be changed

  - `GstMemory` blocks can be added/removed

The individual memory blocks have their own locking and READONLY flags
that might influence their writability.

Buffers can be made writable with `gst_buffer_make_writable()`. This
will copy the buffer with the metadata and will ref the memory in the
buffer. This means that the memory is not automatically copied when
copying buffers.

# Managing GstMemory

A `GstBuffer` contains an array of pointers to `GstMemory` objects.

When the buffer is writable, `gst_buffer_insert_memory()` can be used
to add a new `GstMemory` object to the buffer. When the array of memory is
full, memory will be merged to make room for the new memory object.

`gst_buffer_n_memory()` is used to get the amount of memory blocks on
the `GstBuffer`.

With `gst_buffer_peek_memory()`, memory can be retrieved from the
memory array. The desired access pattern for the memory block should be
specified so that appropriate checks can be made and, in case of
`GST_MAP_WRITE`, a writable copy can be constructed when needed.

`gst_buffer_remove_memory_range()` and `gst_buffer_remove_memory()`
can be used to remove memory from the `GstBuffer`.

# Subbuffers

Subbuffers are made by copying only a region of the memory blocks and
copying all of the metadata.

# Span

Spanning will merge together the data of 2 buffers into a new buffer

# Data access

Accessing the data of the buffer can happen by retrieving the individual
`GstMemory` objects in the `GstBuffer` or by using the `gst_buffer_map()` and
`gst_buffer_unmap()` functions.

The `_map()` and `_unmap()` functions will always return the memory of all
blocks as one large contiguous region. Using these functions might be more
convenient than accessing the individual memory blocks at the expense of
being more expensive because it might perform memcpy operations.

For buffers with only one `GstMemory` object (the most common case), `_map()`
and `_unmap()` have no performance penalty at all.

- **Read access with 1 memory block**: The memory block is accessed and mapped
for read access. The memory block is unmapped after usage

- **write access with 1 memory block**: The buffer should be writable or this
operation will fail. The memory block is accessed. If the memory block is
readonly, a copy is made and the original memory block is replaced with this
copy. Then the memory block is mapped in write mode and unmapped after usage.

- **Read access with multiple memory blocks**: The memory blocks are combined
into one large memory block. If the buffer is writable, the memory blocks are
replaced with this new combined block. If the buffer is not writable, the
memory is returned as is. The memory block is then mapped in read mode.
When the memory is unmapped after usage and the buffer has multiple memory
blocks, this means that the map operation was not able to store the combined
buffer and it thus returned memory that should be freed. Otherwise, the memory
is unmapped.

- **Write access with multiple memory blocks**: The buffer should be writable
or the operation fails. The memory blocks are combined into one large memory
block and the existing blocks are replaced with this new block. The memory is
then mapped in write mode and unmapped after usage.

# Use cases

## Generating RTP packets from h264 video

We receive as input a `GstBuffer` with an encoded h264 image and we need
to create RTP packets containing this h264 data as the payload. We
typically need to fragment the h264 data into multiple packets, each
with their own RTP and payload specific header.

```
                     +-------+-------+---------------------------+--------+
input H264 buffer:   | NALU1 | NALU2 |  .....                    | NALUx  |
                     +-------+-------+---------------------------+--------+
                           |
                           V
array of             +-+ +-------+  +-+ +-------+            +-+ +-------+
output buffers:      | | | NALU1 |  | | | NALU2 |   ....     | | | NALUx |
                     +-+ +-------+  +-+ +-------+            +-+ +-------+
                     :           :  :           :
                     \-----------/  \-----------/
                       buffer 1        buffer 2
```

The output buffer array consists of x buffers consisting of an RTP
payload header and a subbuffer of the original input H264 buffer. Since
the rtp headers and the h264 data don’t need to be contiguous in memory,
they are added to the buffer as separate `GstMemory` blocks and we can
avoid to memcpy the h264 data into contiguous memory.

A typical udpsink will then use something like sendmsg to send the
memory regions on the network inside one UDP packet. This will further
avoid having to memcpy data into contiguous memory.

Using bufferlists, the complete array of output buffers can be pushed in
one operation to the peer element.
