---
title: Memory allocation
...

# Memory allocation

Memory allocation and management are very important topics in
multimedia. High definition video uses many megabytes to store one
single image frame. It is important to reuse memory when possible
instead of constantly allocating and freeing it.

Multimedia systems usually use special-purpose chips, such as DSPs or
GPUs to perform the heavy lifting (especially for video). These
special-purpose chips usually have strict requirements for the memory
they operate on and how it is accessed.

This chapter talks about the memory-management features available to
GStreamer plugins. We will first talk about the lowlevel `GstMemory`
object that manages access to a piece of memory and then continue with
one of it's main users, the `GstBuffer`, which is used to exchange data
between elements and with the application. We will also discuss the `GstMeta`.
This object can be placed on buffers to provide extra info about them and
their memory. We will also discuss the `GstBufferPool`, which allows to
more-efficiently manage buffers of the same size.

To conclude this chapter we will take a look at the `GST_QUERY_ALLOCATION`
query, which is used to negotiate memory management options between
elements.

## GstMemory

`GstMemory` is an object that manages a region of memory. This memory
object points to a region of memory of “maxsize”. The area in this
memory starting at “offset” and size “size” bytes is the accessible
memory region. After a `GstMemory` is created its maxsize can no longer
be changed, however, its "offset" and "size" can.

### GstAllocator

`GstMemory` objects are created by a `GstAllocator` object. Most
allocators implement the default `gst_allocator_alloc()` method but some
might implement different ones, for example, when additional parameters
are needed to allocate the specific memory.

Different allocators exist for system memory, shared memory and memory
backed by a DMAbuf file descriptor. To implement support for a new kind
of memory type, you must implement a new allocator object.

### GstMemory API example

Data access to the memory wrapped by the `GstMemory` object is always
protected with a `gst_memory_map()` and `gst_memory_unmap()` pair. An
access mode (read/write) must be given when mapping memory. The map
function returns a pointer to the valid memory region that can then be
accessed according to the requested access mode.

Below is an example on creating a `GstMemory` object and using the
`gst_memory_map()` to access the memory region.

``` c
[...]

  GstMemory *mem;
  GstMapInfo info;
  gint i;

  /* allocate 100 bytes */
  mem = gst_allocator_alloc (NULL, 100, NULL);

  /* get access to the memory in write mode */
  gst_memory_map (mem, &info, GST_MAP_WRITE);

  /* fill with pattern */
  for (i = 0; i < info.size; i++)
    info.data[i] = i;

  /* release memory */
  gst_memory_unmap (mem, &info);

[...]


```

### Implementing a GstAllocator

WRITEME

## GstBuffer

A `GstBuffer` is a lightweight object that is passed from an upstream
to a downstream element and contains memory and metadata. It represents
the multimedia content that is pushed to or pulled by downstream elements.

A `GstBuffer` contains one or more `GstMemory` objects. These objects hold
the buffer's data.

Metadata in the buffer consists of:

  - DTS and PTS timestamps. These represent the decoding and
    presentation timestamps of the buffer content and are used by
    synchronizing elements to schedule buffers. These timestamps
    can be `GST_CLOCK_TIME_NONE` when unknown/undefined.

  - The duration of the buffer contents. This duration can be
    `GST_CLOCK_TIME_NONE` when unknown/undefined.

  - Media-specific `offset` and `offset_end` values. For video this is the
    frame number in the stream, for audio, the sample number. Other media
    might use different definitions.

  - Arbitrary structures via `GstMeta`, see below.

### Writability

A `GstBuffer` is writable when the refcount of the object is exactly 1,
meaning that only one object is holding a ref to the buffer. You can
only modify the buffer when it is writable. This means that you need to
call `gst_buffer_make_writable()` before changing the timestamps,
offsets, metadata or adding and removing memory blocks.

### API examples

You can create a `GstBuffer` with `gst_buffer_new ()` and then you can
add memory objects to it. You can alternatively use the convenience function
`gst_buffer_new_allocate ()` to perform both operations at once. It's also possible
to wrap existing memory with `gst_buffer_new_wrapped_full ()` and specify the
function to call when the memory should be freed.

You can access the memory of a `GstBuffer` by getting and mapping the
`GstMemory` objects individually or by using `gst_buffer_map ()`. The
latter merges all the memory into one big block and then gives you a
pointer to it.

Below is an example of how to create a buffer and access its memory.

``` c
[...]
  GstBuffer *buffer;
  GstMemory *mem;
  GstMapInfo info;

  /* make empty buffer */
  buffer = gst_buffer_new ();

  /* make memory holding 100 bytes */
  mem = gst_allocator_alloc (NULL, 100, NULL);

  /* add the buffer */
  gst_buffer_append_memory (buffer, mem);

[...]

  /* get WRITE access to the memory and fill with 0xff */
  gst_buffer_map (buffer, &info, GST_MAP_WRITE);
  memset (info.data, 0xff, info.size);
  gst_buffer_unmap (buffer, &info);

[...]

  /* free the buffer */
  gst_buffer_unref (buffer);

[...]
```

## GstMeta

With the `GstMeta` system you can add arbitrary structures to buffers.
These structures describe extra properties of the buffer such as
cropping, stride, region of interest, etc.

The metadata system separates API specification (what the metadata and
its API look like) and its implementation (how it works). This makes it
possible to have different implementations of the same API, for example,
depending on the hardware you are running on.

### API example

After allocating a new `GstBuffer`, you can add metadata to it with
the metadata-specific API. This means that you will need to link to the
header file where the metadata is defined to use its API.

By convention, a metadata API with name `FooBar` should provide two
methods, a `gst_buffer_add_foo_bar_meta ()` and a
`gst_buffer_get_foo_bar_meta ()`. Both functions should return a pointer
to a `FooBarMeta` structure that contains the metadata fields. Some of
the `_add_*_meta ()` can have extra parameters that will usually be used
to configure the metadata structure for you.

Let's have a look at the metadata that is used to specify a cropping
region for video frames.

``` c
#include <gst/video/gstvideometa.h>

[...]
  GstVideoCropMeta *meta;

  /* buffer points to a video frame, add some cropping metadata */
  meta = gst_buffer_add_video_crop_meta (buffer);

  /* configure the cropping metadata */
  meta->x = 8;
  meta->y = 8;
  meta->width = 120;
  meta->height = 80;
[...]
```

An element can then use the metadata on the buffer when rendering the
frame like this:

``` c
#include <gst/video/gstvideometa.h>

[...]
  GstVideoCropMeta *meta;

  /* buffer points to a video frame, get the cropping metadata */
  meta = gst_buffer_get_video_crop_meta (buffer);

  if (meta) {
    /* render frame with cropping */
    _render_frame_cropped (buffer, meta->x, meta->y, meta->width, meta->height);
  } else {
    /* render frame */
    _render_frame (buffer);
  }
[...]
```

### Implementing new GstMeta

In the next sections we show how you can add new metadata to the system
and use it on buffers.

#### Define the metadata API

First we need to define what our API will look like and we have to
register this API to the system. This is important because the API
definition will be used when elements negotiate what kind of metadata
they will exchange. The API definition also contains arbitrary tags that
give hints about what the metadata contains. This is important when we
see how metadata is preserved as buffers pass through the pipeline.

If you are making a new implementation of an existing API, you can skip
this step and move directly to the implementation.

First we start with making the `my-example-meta.h` header file that will
contain the definition of the API and structure for our metadata.

``` c
#include <gst/gst.h>

typedef struct _MyExampleMeta MyExampleMeta;

struct _MyExampleMeta {
  GstMeta       meta;

  gint          age;
  gchar        *name;
};

GType my_example_meta_api_get_type (void);
#define MY_EXAMPLE_META_API_TYPE (my_example_meta_api_get_type())

#define gst_buffer_get_my_example_meta(b) \
  ((MyExampleMeta*)gst_buffer_get_meta((b),MY_EXAMPLE_META_API_TYPE))
```

The metadata API definition consists of the definition of the structure
that holds a `gint` and a string. The first field in the structure must be
a `GstMeta`.

We also define a `my_example_meta_api_get_type ()` function that will
register our metadata API definition and a convenience
`gst_buffer_get_my_example_meta ()` macro that simply finds and returns the
metadata with our new API.

Let's have a look at how the `my_example_meta_api_get_type ()`
function is implemented in the `my-example-meta.c` file:

``` c
#include "my-example-meta.h"

GType
my_example_meta_api_get_type (void)
{
  static GType type;
  static const gchar *tags[] = { "foo", "bar", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("MyExampleMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}
```

As you can see, it simply uses the `gst_meta_api_type_register ()`
function to register a name and some tags for the API. The result is a
new `GType` pointer that defines the newly registered API.

#### Implementing a metadata API

Next we can make an implementation for a registered metadata API `GType`.

The implementation details of a metadata API are kept in a `GstMetaInfo`
structure that you make available to the users of your metadata API
implementation with a `my_example_meta_get_info ()` function and a
convenience `MY_EXAMPLE_META_INFO` macro. You also provide a method to
add your metadata implementation to a `GstBuffer`. Your
`my-example-meta.h` header file will need these additions:

``` c
[...]

/* implementation */
const GstMetaInfo *my_example_meta_get_info (void);
#define MY_EXAMPLE_META_INFO (my_example_meta_get_info())

MyExampleMeta * gst_buffer_add_my_example_meta (GstBuffer      *buffer,
                                                gint            age,
                                                const gchar    *name);
```

Let's have a look at how these functions are implemented in the
`my-example-meta.c` file.

``` c
[...]

static gboolean
my_example_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  MyExampleMeta *emeta = (MyExampleMeta *) meta;

  emeta->age = 0;
  emeta->name = NULL;

  return TRUE;
}

static gboolean
my_example_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  MyExampleMeta *emeta = (MyExampleMeta *) meta;

  /* we always copy no matter what transform */
  gst_buffer_add_my_example_meta (transbuf, emeta->age, emeta->name);

  return TRUE;
}

static void
my_example_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  MyExampleMeta *emeta = (MyExampleMeta *) meta;

  g_free (emeta->name);
  emeta->name = NULL;
}

const GstMetaInfo *
my_example_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (MY_EXAMPLE_META_API_TYPE,
        "MyExampleMeta",
        sizeof (MyExampleMeta),
        my_example_meta_init,
        my_example_meta_free,
        my_example_meta_transform);
    g_once_init_leave (&meta_info, mi);
  }
  return meta_info;
}

MyExampleMeta *
gst_buffer_add_my_example_meta (GstBuffer   *buffer,
                                gint         age,
                                const gchar *name)
{
  MyExampleMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  meta = (MyExampleMeta *) gst_buffer_add_meta (buffer,
      MY_EXAMPLE_META_INFO, NULL);

  meta->age = age;
  meta->name = g_strdup (name);

  return meta;
}
```

`gst_meta_register ()` registers the implementation details, like the
API that you implement and the size of the metadata structure, alongside
methods to initialize and free the memory area. You can also implement a
transform function that will be called when a certain transformation
(identified by the quark and quark specific data) is performed on a
buffer.

Lastly, you implement a `gst_buffer_add_*_meta()` that adds the metadata
implementation to a buffer and sets the values of the metadata.

## GstBufferPool

The `GstBufferPool` object provides a convenient base class for managing
lists of reusable buffers. Essential for this object is that all the
buffers have the same properties such as size, padding, metadata and
alignment.

A `GstBufferPool` can be configured to manage a minimum and maximum
amount of buffers of a specific size. It can also be configured to use a
specific `GstAllocator` for the memory of the buffers. There is also
support in the bufferpool to enable bufferpool specific options, such as
adding `GstMeta` to the pool's buffers or enabling specific padding on the
buffers' memory.

A `GstBufferPool` can be either inactivate or active. In the inactive state, you
can configure the pool. In the active state, you can't change the
configuration anymore but you can acquire and release buffers from/to
the pool.

In the following sections we take a look at how you can use a `GstBufferPool`.

### API example

There can be many different `GstBufferPool` implementations; they are all
subclasses of the `GstBufferPool` base class. For this example, we will
assume we somehow have access to a buffer pool, either because we created
it ourselves or because we were given one as a result of the `ALLOCATION`
query, as we will see below.

The `GstBufferPool` is initially in the inactive state so that we can
configure it. Trying to configure a `GstBufferPool` that is not in the
inactive state will fail. Likewise, trying to activate a bufferpool that
is not configured will also fail.

``` c
  GstStructure *config;

[...]

  /* get config structure */
  config = gst_buffer_pool_get_config (pool);

  /* set caps, size, minimum and maximum buffers in the pool */
  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  /* configure allocator and parameters */
  gst_buffer_pool_config_set_allocator (config, allocator, &params);

  /* store the updated configuration again */
  gst_buffer_pool_set_config (pool, config);

[...]

```

The configuration of a `GstBufferPool` is maintained in a generic
`GstStructure` that can be obtained with `gst_buffer_pool_get_config()`.
Convenience methods exist to get and set the configuration options in
this structure. After updating the structure, it is set as the current
configuration in the `GstBufferPool` again with
`gst_buffer_pool_set_config()`.

The following options can be configured on a `GstBufferPool`:

  - The caps of the buffers to allocate.

  - The size of the buffers. This is the suggested size of the buffers
    in the pool. The pool might decide to allocate larger buffers to add
    padding.

  - The minimum and maximum amount of buffers in the pool. When minimum
    is set to `\> 0`, the bufferpool will pre-allocate this amount of
    buffers. When maximum is not 0, the bufferpool will allocate up to
    maximum amount of buffers.

  - The allocator and parameters to use. Some bufferpools might ignore
    the allocator and use its internal one.

  - Other arbitrary bufferpool options identified with a string. a
    bufferpool lists the supported options with
    `gst_buffer_pool_get_options()` and you can ask if an option is
    supported with `gst_buffer_pool_has_option()`. The option can be
    enabled by adding it to the configuration structure with
    `gst_buffer_pool_config_add_option ()`. These options are used to
    enable things like letting the pool set metadata on the buffers or
    to add extra configuration options for padding, for example.

After the configuration is set on the bufferpool, the pool can be
activated with `gst_buffer_pool_set_active (pool, TRUE)`. From that
point on you can use `gst_buffer_pool_acquire_buffer ()` to retrieve a
buffer from the pool, like this:

``` c
  [...]

  GstFlowReturn ret;
  GstBuffer *buffer;

  ret = gst_buffer_pool_acquire_buffer (pool, &buffer, NULL);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto pool_failed;

  [...]
```

It is important to check the return value of the acquire function
because it is possible that it fails: When your element shuts down, it
will deactivate the bufferpool and then all calls to acquire will return
`GST_FLOW_FLUSHING`.

All buffers that are acquired from the pool will have their pool member
set to the original pool. When the last ref is decremented on the
buffer, GStreamer will automatically call
`gst_buffer_pool_release_buffer()` to release the buffer back to the
pool. You (or any other downstream element) don't need to know if a
buffer came from a pool, you can just unref it.

### Implementing a new GstBufferPool

WRITEME

## GST\_QUERY\_ALLOCATION

The `ALLOCATION` query is used to negotiate `GstMeta`, `GstBufferPool` and
`GstAllocator` between elements. Negotiation of the allocation strategy
is always initiated and decided by a srcpad after it has negotiated a
format and before it decides to push buffers. A sinkpad can suggest an
allocation strategy but it is ultimately the source pad that will decide
based on the suggestions of the downstream sink pad.

The source pad will do a `GST_QUERY_ALLOCATION` with the negotiated caps
as a parameter. This is needed so that the downstream element knows what
media type is being handled. A downstream sink pad can answer the
allocation query with the following results:

  - An array of possible `GstBufferPool` suggestions with suggested
    size, minimum and maximum amount of buffers.

  - An array of `GstAllocator` objects along with suggested allocation
    parameters such as flags, prefix, alignment and padding. These
    allocators can also be configured in a bufferpool when this is
    supported by the bufferpool.

  - An array of supported `GstMeta` implementations along with metadata
    specific parameters. It is important that the upstream element knows
    what kind of metadata is supported downstream before it places that
    metadata on buffers.

When the `GST_QUERY_ALLOCATION` returns, the source pad will select from
the available bufferpools, allocators and metadata how it will allocate
buffers.

### ALLOCATION query example

Below is an example of the `ALLOCATION` query.

``` c
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

  GstCaps *caps;
  GstQuery *query;
  GstStructure *structure;
  GstBufferPool *pool;
  GstStructure *config;
  guint size, min, max;

[...]

  /* find a pool for the negotiated caps now */
  query = gst_query_new_allocation (caps, TRUE);

  if (!gst_pad_peer_query (scope->srcpad, query)) {
    /* query failed, not a problem, we use the query defaults */
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    /* we got configuration from our peer, parse them */
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
  } else {
    pool = NULL;
    size = 0;
    min = max = 0;
  }

  if (pool == NULL) {
    /* we did not get a pool, make one ourselves then */
    pool = gst_video_buffer_pool_new ();
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* and activate */
  gst_buffer_pool_set_active (pool, TRUE);

[...]
```

This particular implementation will make a custom `GstVideoBufferPool`
object that is specialized in allocating video buffers. You can also
enable the pool to put `GstVideoMeta` metadata on the buffers from the
pool doing:

``` c
gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META)
```

### The ALLOCATION query in base classes

In many base classes you will see the following virtual methods for
influencing the allocation strategy:

  - `propose_allocation ()` should suggest allocation parameters for the
    upstream element.

  - `decide_allocation ()` should decide the allocation parameters from
    the suggestions received from downstream.

Implementors of these methods should modify the given `GstQuery` object
by updating the pool options and allocation options.

### Negotiating the exact layout of video buffers

Hardware elements may have specific constraints on the layout
of their input buffers, requiring to add vertical and/or horizontal paddings
to their planes.
If the producer is able to create buffers fulfilling these requirements,
we can ensure zero-copy by configuring its driver accordingly before starting to produce
buffers.

In such setup on Linux we'll generally use dmabuf to exchange buffers in order
to reduce memory copies. The producer can either export its buffers
to the consumer (dmabuf export) or import them from it (dmabuf import).

In this section we'll outline the steps for how the consumer can inform the
producer of its expected buffer layout for import and export use cases.
Let's consider `v4l2src` (the producer) feeding buffers to
`v4l2h264enc` (the consumer) for encoding.

#### v4l2src importing buffers from v4l2h264enc

  1. *v4l2h264enc*: query the hardware for its requirements and create a
    `GstVideoAlignment` accordingly.
  2. *v4l2h264enc*: in its buffer pool `alloc_buffer` implementation, call
    `gst_buffer_add_video_meta_full()` and then
    `gst_video_meta_set_alignment()` on the returned meta with the
    requested alignment. The alignment will be added to the meta, allowing
    `v4l2src` to configure its driver before trying to import buffers.
``` c
      meta = gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_INFO_FORMAT (&pool->video_info),
          GST_VIDEO_INFO_WIDTH (&pool->video_info),
          GST_VIDEO_INFO_HEIGHT (&pool->video_info),
          GST_VIDEO_INFO_N_PLANES (&pool->video_info), offset, stride);

      gst_video_meta_set_alignment (meta, align);
```
  3. *v4l2h264enc*: propose its pool to the producer when replying to the
    `ALLOCATION` query (`propose_allocation()`).
  4. *v4l2src*: when receiving the reply from the `ALLOCATION` query
    (`decide_allocation()`) acquire
    a single buffer from the suggested pool and retrieve its layout
    using `GstVideoMeta.stride` and `gst_video_meta_get_plane_height()`.
  5. *v4l2src*: configure its driver to produce data matching those requirements,
    if possible, then try to import the buffer.
    If not, `v4l2src` won't be able to import from `v4l2h264enc` and so will
    fallback to sending its own buffers to `v4l2h264enc` which will
    have to copy each input buffer to fit its requirements.

#### v4l2src exporting buffers to v4l2h264enc

  1. *v4l2h264enc*: query the hardware for its requirements and create a
    `GstVideoAlignment` accordingly.
  2. *v4l2h264enc*: create a `GstStructure` named `video-meta` serializing the alignment:
``` c
params = gst_structure_new ("video-meta",
    "padding-top", G_TYPE_UINT, align.padding_top,
    "padding-bottom", G_TYPE_UINT, align.padding_bottom,
    "padding-left", G_TYPE_UINT, align.padding_left,
    "padding-right", G_TYPE_UINT, align.padding_right,
    NULL);
```
  3. *v4l2h264enc*: when handling the `ALLOCATION` query (`propose_allocation()`),
    pass this structure as parameter when adding the `GST_VIDEO_META_API_TYPE`
    meta:
``` c
gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, params);
```
  4. *v4l2src*: when receiving the reply from the `ALLOCATION` query
    (`decide_allocation()`) retrieve the `GST_VIDEO_META_API_TYPE` parameters
    to compute the expected buffers layout:
``` c
guint video_idx;
GstStructure *params;

if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, &video_idx)) {
  gst_query_parse_nth_allocation_meta (query, video_idx, &params);

  if (params) {
    GstVideoAlignment align;
    GstVideoInfo info;
    gsize plane_size[GST_VIDEO_MAX_PLANES];

    gst_video_alignment_reset (&align);

    gst_structure_get_uint (s, "padding-top", &align.padding_top);
    gst_structure_get_uint (s, "padding-bottom", &align.padding_bottom);
    gst_structure_get_uint (s, "padding-left", &align.padding_left);
    gst_structure_get_uint (s, "padding-right", &align.padding_right);

    gst_video_info_from_caps (&info, caps);

    gst_video_info_align_full (&info, align, plane_size);
  }
}
```
  5. *v4l2src*: retrieve the requested buffers layout using
    `GstVideoInfo.stride` and `GST_VIDEO_INFO_PLANE_HEIGHT()`.
  6. *v4l2src*: configure its driver to produce data matching those requirements,
    if possible.
    If not, driver will produce buffers using its own layout but `v4l2h264enc` will
    have to copy each input buffer to fit its requirements.
