# GstMeta

This document describes the design for arbitrary per-buffer metadata.

Buffer metadata typically describes the low level properties of the
buffer content. These properties are commonly not negotiated with caps
but they are negotiated in the bufferpools.

Some examples of metadata:

  - interlacing information

  - video alignment, cropping, panning information

  - extra container information such as granulepos, …

  - extra global buffer properties

## Requirements

  - It must be fast

      - allocation, free, low fragmentation

      - access to the metadata fields, preferably not much slower than
        directly accessing a C structure field

  - It must be extensible. Elements should be able to add new arbitrary
    metadata without requiring much effort. Also new metadata fields
    should not break API or ABI.

  - It plays nice with subbuffers. When a subbuffer is created, the
    various buffer metadata should be copied/updated correctly.

  - We should be able to negotiate metadata between elements

## Use cases

- **Video planes**: Video data is sometimes allocated in non-contiguous planes
for the Y and the UV data. We need to be able to specify the data on a buffer
using multiple pointers in memory. We also need to be able to specify the
stride for these planes.

- **Extra buffer data**: Some elements might need to store extra data for
a buffer. This is typically done when the resources are allocated from another
subsystem such as OMX or X11.

- **Processing information**: Pan and crop information can be added to the
buffer data when the downstream element can understand and use this metadata.
An imagesink can, for example, use the pan and cropping information when
blitting the image on the screen with little overhead.

## GstMeta

A `GstMeta` is a structure as follows:

``` c
struct _GstMeta {
  GstMetaFlags       flags;
  const GstMetaInfo *info;    /* tag and info for the meta item */
};
```

The purpose of this structure is to serve as a common header for all
metadata information that we can attach to a buffer. Specific metadata,
such as timing metadata, will have this structure as the first field.
For example:

``` c
struct _GstMetaTiming {
  GstMeta        meta;        /* common meta header */

  GstClockTime   dts;         /* decoding timestamp */
  GstClockTime   pts;         /* presentation timestamp */
  GstClockTime   duration;    /* duration of the data */
  GstClockTime   clock_rate;  /* clock rate for the above values */
};
```

Or another example for the video memory regions that consists of both
fields and methods.

``` c
#define GST_VIDEO_MAX_PLANES 4

struct GstVideoMeta {
  GstMeta       meta;

  GstBuffer         *buffer;

  GstVideoFlags      flags;
  GstVideoFormat     format;
  guint              id
  guint              width;
  guint              height;

  guint              n_planes;
  gsize              offset[GST_VIDEO_MAX_PLANES];   /* offset in the buffer memory region of the
                                                    * first pixel. */
  gint               stride[GST_VIDEO_MAX_PLANES];   /* stride of the image lines. Can be negative when
                                                    * the image is upside-down */

  gpointer (*map)    (GstVideoMeta *meta, guint plane, gpointer * data, gint *stride,
                      GstMapFlags flags);
  gboolean (*unmap)  (GstVideoMeta *meta, guint plane, gpointer data);
};

gpointer gst_meta_video_map   (GstVideoMeta *meta, guint plane, gpointer * data,
                               gint *stride, GstMapflags flags);
gboolean gst_meta_video_unmap (GstVideoMeta *meta, guint plane, gpointer data);
```

`GstMeta` derived structures define the API of the metadata. The API can
consist of fields and/or methods. It is possible to have different
implementations for the same `GstMeta` structure.

The implementation of the `GstMeta` API would typically add more fields to
the public structure that allow it to implement the API.

`GstMetaInfo` will point to more information about the metadata and looks
like this:

``` c
struct _GstMetaInfo {
  GType                      api;       /* api type */
  GType                      type;      /* implementation type */
  gsize                      size;      /* size of the structure */

  GstMetaInitFunction        init_func;
  GstMetaFreeFunction        free_func;
  GstMetaTransformFunction   transform_func;
};
```

The `api` member will contain a `GType` of the metadata API. A repository of
registered `MetaInfo` will be maintained by the core. We will register some
common metadata structures in core and some media specific info for
audio/video/text in -base. Plugins can register additional custom metadata.

For each implementation of api, there will thus be a unique `GstMetaInfo`.
In the case of metadata with a well defined API, the implementation
specific init function will setup the methods in the metadata structure.
A unique `GType` will be made for each implementation and stored in the
type field.

Along with the metadata description we will have functions to
initialize/free (and/or refcount) a specific `GstMeta` instance. We also
have the possibility to add a custom transform function that can be used
to modify the metadata when a transformation happens.

There are no explicit methods to serialize and deserialize the metadata.
Since each type has a `GType`, we can reuse the `GValue` transform functions
for this.

The purpose of the separate `MetaInfo` is to not have to carry the
free/init functions in each buffer instance but to define them globally.
We still want quick access to the info so we need to make the buffer
metadata point to the info.

Technically we could also specify the field and types in the `MetaInfo`
and provide a generic API to retrieve the metadata fields without the
need for a header file. We will not do this yet.

Allocation of the `GstBuffer` structure will result in the allocation of a
memory region of a customizable size (512 bytes). Only the first `sizeof
(GstBuffer)` bytes of this region will initially be used. The remaining
bytes will be part of the free metadata region of the buffer. Different
implementations are possible and are invisible in the API or ABI.

The complete buffer with metadata could, for example, look as follows:

```
                         +----------------------------------+
GstMiniObject            |  GType (GstBuffer)               |
                         |  refcount, flags, copy/disp/free |
                         +----------------------------------+
GstBuffer                |  pool,pts,dts,duration,offsets   |
                         |  <private data>                  |
                         +..................................+
                         |  next                           ---+
                      +- |  info                           ------> GstMetaInfo
GstMetaTiming         |  |                                  | |
                      |  |  dts                             | |
                      |  |  pts                             | |
                      |  |  duration                        | |
                      +- |  clock_rate                      | |
                         +  . . . . . . . . . . . . . . . . + |
                         |  next                           <--+
GstVideoMeta       +- +- |  info                           ------> GstMetaInfo
                   |  |  |                                  | |
                   |  |  |  flags                           | |
                   |  |  |  n_planes                        | |
                   |  |  |  planes[]                        | |
                   |  |  |  map                             | |
                   |  |  |  unmap                           | |
                   +- |  |                                  | |
                      |  |  private fields                  | |
GstVideoMetaImpl      |  |  ...                             | |
                      |  |  ...                             | |
                      +- |                                  | |
                         +  . . . . . . . . . . . . . . . . + .
                         .                                    .
```

## API examples

Buffers are created using the normal `gst_buffer_new()` functions. The
standard fields are initialized as usual. A memory area that is bigger
than the structure size is allocated for the buffer metadata.

``` c
gst_buffer_new ();
```

After creating a buffer, the application can set caps and add metadata
information.

To add or retrieve metadata, a handle to a `GstMetaInfo` structure needs
to be obtained. This defines the implementation and API of the metadata.
Usually, a handle to this info structure can be obtained by calling a
public `_get_info()` method from a shared library (for shared metadata).

The following defines can usually be found in the shared .h file.

``` c
GstMetaInfo * gst_meta_timing_get_info();
#define GST_META_TIMING_INFO  (gst_meta_timing_get_info())
```

Adding metadata to a buffer can be done with the
`gst_buffer_add_meta()` call. This function will create new metadata
based on the implementation specified by the `GstMetaInfo`. It is also
possible to pass a generic pointer to the `add_meta()` function that can
contain parameters to initialize the new metadata fields.

Retrieving the metadata on a buffer can be done with the
`gst_buffer_meta_get()` method. This function retrieves an existing
metadata conforming to the API specified in the given info. When no such
metadata exists, the function will return NULL.

``` c
GstMetaTiming *timing;

timing = gst_buffer_get_meta (buffer, GST_META_TIMING_INFO);
```

Once a reference to the info has been obtained, the associated metadata
can be added or modified on a buffer.

``` c
timing->timestamp = 0;
timing->duration = 20 * GST_MSECOND;
```

Other convenience macros can be made to simplify the above code:

``` c
#define gst_buffer_get_meta_timing(b) \
   ((GstMetaTiming *) gst_buffer_get_meta ((b), GST_META_TIMING_INFO)
```

This makes the code look like this:

``` c
GstMetaTiming *timing;

timing = gst_buffer_get_meta_timing (buffer);
timing->timestamp = 0;
timing->duration = 20 * GST_MSECOND;
```

To iterate the different metainfo structures, one can use the
`gst_buffer_meta_get_next()` methods.

``` c
GstMeta *current = NULL;

/* passing NULL gives the first entry */
current = gst_buffer_meta_get_next (buffer, current);

/* passing a GstMeta returns the next */
current = gst_buffer_meta_get_next (buffer, current);
```

## Memory management

### allocation

We initially allocate a reasonable sized `GstBuffer` structure (say 512 bytes).

Since the complete buffer structure, including a large area for metadata, is
allocated in one go, we can reduce the number of memory allocations while still
providing dynamic metadata.

When adding metadata, we need to call the init function of the associated
metadata info structure. Since adding the metadata requires the caller to pass
a handle to the info, this operation does not require table lookups.

Per-metadata memory initialisation is needed because not all metadata is
initialized in the same way. We need to, for example, set the timestamps to
NONE in the MetaTiming structures.

The init/free functions can also be used to implement refcounting for a metadata
structure. This can be useful when a structure is shared between buffers.

When the `free_size` of the `GstBuffer` is exhausted, we will allocate new
memory for each newly added Meta and use the next pointers to point to this. It
is expected that this does not occur often and we might be able to optimize
this transparently in the future.

### free

When a `GstBuffer` is freed, we potentially might have to call a custom `free()`
function on the metadata info. In the case of the Memory metadata, we need to
call the associated `free()` function to free the memory.

When freeing a `GstBuffer`, the custom buffer free function will iterate all of
the metadata in the buffer and call the associated free functions in the
`MetaInfo` associated with the entries. Usually, this function will be NULL.

## Serialization

When a buffer should be sent over the wire or be serialized in GDP, we
need a way to perform custom serialization and deserialization on the
metadata. For this we can use the `GValue` transform functions.

## Transformations

After certain transformations, the metadata on a buffer might not be
relevant anymore.

Consider, for example, metadata that lists certain regions of interest
on the video data. If the video is scaled or rotated, the coordinates
might not make sense anymore. A transform element should be able to
adjust or remove the associated metadata when it becomes invalid.

We can make the transform element aware of the metadata so that it can
adjust or remove in an intelligent way. Since we allow arbitrary
metadata, we can’t do this for all metadata and thus we need some other
way.

One proposition is to tag the metadata type with keywords that specify
what it functionally refers too. We could, for example, tag the metadata
for the regions of interest with a tag that notes that the metadata
refers to absolute pixel positions. A transform could then know that the
metadata is not valid anymore when the position of the pixels changed
(due to rotation, flipping, scaling and so on).

## Subbuffers

Subbuffers are implemented with a generic copy. Parameters to the copy
are the offset and size. This allows each metadata structure to
implement the actions needed to update the metadata of the subbuffer.

It might not make sense for some metadata to work with subbuffers. For
example when we take a subbuffer of a buffer with a video frame, the
`GstVideoMeta` simply becomes invalid and is removed from the new
subbuffer.

## Relationship with GstCaps

The difference between `GstCaps`, used in negotiation, and the metadata is
not clearly defined.

We would like to think of the `GstCaps` containing the information needed
to functionally negotiate the format between two elements. The Metadata
should then only contain variables that can change between each buffer.

For example, for video we would have width/height/framerate in the caps
but then have the more technical details, such as stride, data pointers,
pan/crop/zoom etc in the metadata.

A scheme like this would still allow us to functionally specify the
desired video resolution while the implementation details would be
inside the metadata.

## Relationship with GstMiniObject qdata

qdata on a miniobject is element private and is not visible to other
element. Therefore qdata never contains essential information that
describes the buffer content.

## Compatibility

We need to make sure that elements exchange metadata that they both
understand, This is particularly important when the metadata describes
the data layout in memory (such as strides).

The `ALLOCATION` query is used to let upstream know what metadata we can
support.

It is also possible to have a bufferpool add certain metadata to the
buffers from the pool. This feature is activated by enabling a buffer
option when configuring the pool.

## Notes

Some structures that we need to be able to add to buffers.

- Clean Aperture
- Arbitrary Matrix Transform
- Aspect ratio
- Pan/crop/zoom
- Video strides

Some of these overlap, we need to find a minimal set of metadata
structures that allows us to define all use cases.
