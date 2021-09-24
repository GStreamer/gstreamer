# GstMemory

This document describes the design of the memory objects.

`GstMemory` objects are usually added to `GstBuffer` objects and contain the
multimedia data passed around in the pipeline.

``` c
struct GstMemory {
  GstMiniObject   mini_object;

  GstAllocator   *allocator;

  GstMemory      *parent;
  gsize           maxsize;
  gsize           align;
  gsize           offset;
  gsize           size;
};
```

## Requirements

- It must be possible to have different memory allocators
- It must be possible to efficiently share memory objects, copy, span and trim.

## Memory layout

A `GstMemory` has a pointer to a memory region of `maxsize`. The accessible part
of this managed region is defined by an `offset` relative to the start of the
region and a `size`. This means that the managed region can be larger than what
is visible to the user of the `GstMemory` API.

```
           memory
GstMemory  ->*----------------------------------------------------*
             ^----------------------------------------------------^
                               maxsize
                  ^--------------------------------------^
                 offset            size
```

The current properties of the accessible memory can be retrieved with:

``` c
gsize gst_memory_get_sizes (GstMemory *mem, gsize *offset, gsize *maxsize);
```

The offset and size can be changed with:

``` c
void  gst_memory_resize (GstMemory *mem, gssize offset, gsize size);
```

## Allocators

`GstMemory` objects are created by allocators. Allocators are a subclass
of `GstObject` and can be subclassed to make custom allocators.

``` c
struct _GstAllocator {
  GstObject                 object;

  const gchar               *mem_type;

  GstMemoryMapFunction       mem_map;
  GstMemoryUnmapFunction     mem_unmap;
  GstMemoryCopyFunction      mem_copy;
  GstMemoryShareFunction     mem_share;
  GstMemoryIsSpanFunction    mem_is_span;

  GstMemoryMapFullFunction   mem_map_full;
  GstMemoryUnmapFullFunction mem_unmap_full;
};
```

The allocator class has 2 virtual methods. One to create a `GstMemory`,
another to free it.

``` c
struct _GstAllocatorClass {
  GstObjectClass object_class;

  GstMemory *  (*alloc)      (GstAllocator *allocator, gsize size,
                              GstAllocationParams *params);
  void         (*free)       (GstAllocator *allocator, GstMemory *memory);
};
```

Allocators are refcounted. It is also possible to register the allocator to the
GStreamer system. This way, the allocator can be retrieved by name.

After an allocator is created, new `GstMemory` can be created with

``` c
GstMemory * gst_allocator_alloc (const GstAllocator * allocator,
                                 gsize size, GstAllocationParams *params);
```

`GstAllocationParams` contain extra info such as flags, alignment, prefix and
padding.

The `GstMemory` object is a refcounted object that must be freed with
`gst_memory_unref()`.

The `GstMemory` keeps a ref to the allocator that allocated it. Inside the
allocator are the most common `GstMemory` operations listed. Custom
`GstAllocator` implementations must implement the various operations on
the memory they allocate.

It is also possible to create a new `GstMemory` object that wraps existing
memory with:

``` c
GstMemory * gst_memory_new_wrapped  (GstMemoryFlags flags,
                                     gpointer data, gsize maxsize,
                                     gsize offset, gsize size,
                                     gpointer user_data,
                                     GDestroyNotify notify);
```

## Lifecycle

`GstMemory` extends from `GstMiniObject` and therefore uses its lifecycle
management (See [miniobject](additional/design/miniobject.md)).

## Data Access

Access to the memory region is always controlled with a `map()` and `unmap()` method
call. This allows the implementation to monitor the access patterns or set up
the required memory mappings when needed.

The access of the memory object is controlled with the locking mechanism on
`GstMiniObject` (See [miniobject](additional/design/miniobject.md)).

Mapping a memory region requires the caller to specify the access method: READ
and/or WRITE. Mapping a memory region will first try to get a lock on the
memory in the requested access mode. This means that the map operation can
fail when WRITE access is requested on a non-writable memory object (it has
an exclusive counter > 1, the memory is already locked in an incompatible
access mode or the memory is marked readonly).

After the data has been accessed in the object, the `unmap()` call must be
performed, which will unlock the memory again.

It is allowed to recursively map multiple times with the same or narrower
access modes. For each of the `map()` calls, a corresponding `unmap()` call
needs to be made. WRITE-only memory cannot be mapped in READ mode and
READ-only memory cannot be mapped in WRITE mode.

The memory pointer returned from the `map()` call is guaranteed to remain
valid in the requested mapping mode until the corresponding `unmap()` call is
performed on the pointer.

When multiple `map()` operations are nested and return the same pointer, the
pointer is valid until the last `unmap()` call is done.

When the final reference on a memory object is dropped, all outstanding
mappings should have been unmapped.

Resizing a `GstMemory` does not influence any current mappings in any way.

## Copy

A `GstMemory` copy can be made with the `gst_memory_copy()` call. Normally,
allocators will implement a custom version of this function to make a copy of
the same kind of memory as the original one. This is what the fallback version
of the copy function does, albeit slower than what a custom implementation
could do.

The copy operation is only required to copy the visible range of the memory
block.

## Share

A memory region can be shared between `GstMemory` objects with the
`gst_memory_share()` operation.
