# DMA buffers

This document describes the GStreamer caps negotiation of DMA buffers on
Linux-like platforms.

The DMA buffer sharing is the efficient way to share the buffer/memory
between different Linux kernel driver, such as codecs/3D/display/cameras.
For example, the decoder may want its output to be directly shared with the
display server for rendering without a copy.

Any device driver which is part of DMA buffer sharing, can do so as either
the *exporter* or *importer* of buffers.

This kind of buffer/memory is usually stored in non-system memory (maybe in
device's local memory or something else not directly accessible by the
CPU), then its memory mapping for CPU access may impose a big overhead and
low performance, or even impossible.

DMA buffers are exposed to user-space as *file descriptors* allowing to pass
them between processes.


# DRM PRIME buffers

PRIME is the cross device buffer sharing framework in DRM kernel
subsystem. These are the ones normally used in GStreamer which might
contain video frames.

PRIME buffers requires some metadata to describe how to interpret them,
such as a set of file descriptors (for example, one per plane), color
definition in fourcc, and DRM-modifiers. If the frame is going to be mapped
onto system's memory, also is needed padding, strides, offsets, etc.


## File descriptor

Each file descriptor represents a chunk of a frame, usually a plane. For
example, when a DMA buffer contains NV12 format data, it might be
composited by 2 planes: one for its Y component and the other for both UV
components. Then, the hardware may use two detached memory chunks, one per
plane, exposed as two file descriptors. Otherwise, if hardware uses only
one continuous memory chunk for all the planes, the DMA buffer should just
have one file descriptor.


## DRM fourcc

Just like fourcc common usage, DRM-fourcc describes the underlying format
of the video frame, such as `DRM_FORMAT_YVU420` or `DRM_FORMAT_NV12`. All
of them with the prefix `DRM_FORMAT_`. Please refer to `drm_fourcc.h` in
the kernel for a full list. This list of fourcc formats maps to GStreamer
video formats, although the GStreamer formats may have a slighly different.
For example, DRM_FORMAT_ARGB8888 corresponds to GST_VIDEO_FORMAT_BGRA.


## DRM modifier

DRM-modifier describes the translation mechanism between pixel to memory
samples and the actual memory storage of the buffer. The most
straightforward modifier is LINEAR, where each pixel has contiguous storage
and pixel location in memory can be easily calculated with the stride. This
is considered the baseline interchange format, and most convenient for CPU
access. Nonetheless, modern hardware employs more sophisticated memory
access mechanisms, such as tiling and possibly compression.  For example,
the TILED modifier describes memory storage where pixels are stored in 4x4
blocks arranged in row-major ordering. For example, the first tile in
memory stores pixels (0,0) to (3,3) inclusive, and the second tile in
memory stores pixels (4,0) to (7,3) inclusive, and so on.

DRM-modifier is a sixteen hexadecimal digits to represent these memory
layouts. For example, `0x0000000000000000` means linear,
`0x0100000000000001` means Intel's X tile mode, etc. Please refer to
`drm_fourcc.h` in kernel for a full list.

Excepting the linear modifier, the first 8 bits represent the vendor ID and
the other 56 bits describe the memory layout, which may be hardware
dependent. Users should be careful when interpreting non-linear memory by
themselves.

Please bear in mind that, even for the linear modifier, as the access to
DMA memory's content is through `map()` / `unmap()` functions, its
read/write performance may be low or even bad, because of its cache type
and coherence assurance. So, most of the times, it's advised to avoid that
code path for upload or download frame data.


## Meta Data

The meta data contains information about how to interpret the memory
holding the video frame, either when the frame mapped and its DRM modifier
is linear, or by other API that imports those DMA buffers.


# DMABufs in GStreamer


## Representation

In GStreamer, a full DMA buffer-based video frame is mapped to a
`GstBuffer`, and each file descriptor used to describe the whole frame is
held by a `GstMemory` mini-object. A derived class of `GstDmaBufAllocator`
would be implemented for every wrapped API *exporting* DMA buffers to
user-space, as memory allocator.


## DRM format caps field

The *GstCapsFeatures* *memory:DMABuf* is usually used to negotiate DMA
buffers. It is recommended to allow DMAbuf to flow without the
*GstCapsFeatures* *memory:DMABuf* if the DRM-modifier is linear.

But also, in order to negotiate *memory:DMABuf* thoroughly, it's required
to match the DRM-modifiers between upstream and downstream. Otherwise video
sinks might end rendering wrong frames assuming linear access.

Because DRM-fourcc and DRM-modifier are both necessary to render frames
DMABuf-backed, we now consider both as a pair and combine them together to
assure uniqueness. In caps, we use a *:* to link them together and write in
the mode of *DRM_FORMAT:DRM_MODIFIER*, which represents a totally new single video
format. For example, `NV12:0x0100000000000002` is a new video format
combined by video format NV12 and the modifier `0x0100000000000002`. It's
not NV12 and it's not its subset either.

*DRM_FORMAT* can be printed by using
`GST_FOURCC_FORMAT` and `GST_FOURCC_ARGS` macros from the
`DRM_FORMAT_*` constants, it is NOT a `GstVideoFormat`, so it would be
different from the content of the `format` field in a non-dmabuf caps.
A modifier must always be present, except if the modifier is linear,
then it should not be included, so `NV12:0x0000000000000000` is
invalid, it must be `drm-format=NV12`. DRM fourcc are used
instead of a `GstVideoFormat` to make it easier for non-GStreamer
developers to understand what the system is trying to achieve.

Please note that this form of video format only appears within
*memory:DMABuf* feature. It must not appear in any other video caps
feature.

Unlike other type of video buffers, DMABuf frames might not be mappable and
its internal format is opaque to the user. Then, unless the modifier is
linear (0x0000000000000000) or some other well known tiled format such as
NV12_4L4, NV12_16L16, NV12_64Z32, NV12_16L32S, etc. (which are defined in
video-format.h), we always use `GST_VIDEO_FORMAT_DMA_DRM` in
`GstVideoFormat` enum to represent its video format.

In order to not misuse this new format with the common video format, **in**
*memory:DMABuf* feature, the traditional *format* should be set to DMA_DRM.
And a new *drm-format* field in caps is introduced to represent the video
format in details(the composing of fourcc:modifier).

So a DMABuf-backed video caps may look like:

```
     video/x-raw(memory:DMABuf), \
                format=(string)DMA_DRM, \
                drm-format=(string)NV12:0x0x0100000000000001, \
                width=(int)1920, \
                height=(int)1080, \
                interlace-mode=(string)progressive, \
                multiview-mode=(string)mono, \
                multiview-flags=(GstVideoMultiviewFlagsSet)0:ffffffff:/right-view-first/left-flipped/left-flopped/right-flipped/right-flopped/half-aspect/mixed-mono, \
                pixel-aspect-ratio=(fraction)1/1, \
                framerate=(fraction)24/1, \
                colorimetry=(string)bt709"
```

And when we call a video info API such as `gst_video_info_from_caps()` with
this caps, it should return an video format as `GST_VIDEO_FORMAT_DMA_DRM`,
leaving other fields unchanged as normal video caps.

In addition, a new structure

```
struct GstDrmVideoInfo
{
  GstVideoInfo vinfo;
  guint32 drm_fourcc;
  guint64 drm_modifier;
};
```

is introduced to represent more info of DMA video caps. User should use
this DMABuf related API such as `gst_drm_video_info_from_caps()` to recognize
the video format and parse the DMA info from caps.


## Meta data

Besides the *file descriptors*, there may be a `GstVideoMeta` data attached
to each `GstBuffer` to describe more information such as the width, height,
pitches, strides and plane offsets for that DMA buffer (Please note that
the mandatory width and height information appears both in "caps" and here,
and they should be always equal). This kind of information is only obtained
by each module's API, such as the functions
`VkImageDrmFormatModifierExplicitCreateInfoEXT()` in Vulkan, and
`vaExportSurfaceHandle()` in VA-API. The information should be translated
into `GstVideoMeta`'s fields when the DMA buffer is created and
exported. These meta data is useful when other module wants to import the
DMA buffers.

For example, we may create a `GstBuffer` using `vaExportSurfaceHandle()`
VA-API, and set each field of `GstVideoMeta` with information from
`VADRMPRIMESurfaceDescriptor`. Later, a downstream Vulkan element imports
these DMA buffers with `VkImageDrmFormatModifierExplicitCreateInfoEXT()`,
translating fields form buffer's `GstVideoMeta` into the
`VkSubresourceLayout` parameter.

In short, the `GstVideoMeta` contains the common extra video information
about the DMA buffer, which can be interpreted by each module.

Information in `GstVideoMeta` depends on the hardware context and
setting. Its values, such as stride and pitch, may differ from the standard
video format because of the hardware's requirement. For example, if a DMA
buffer represents a compressed video in memory, its pitch and stride may be
smaller than the standard linear one because of the compression. Please
remind that users should not use this meta data to interpret and access the
DMA buffer, **unless the modifier is linear**.


# Negotiation of DMA buffer

If two elements of different modules (for example, VA-API decoder to
Wayland sink) want to transfer dmabufs, the negotiation should ensure a
common *drm-format* (*DRM_FORMAT:DRM_MODIFIER*).  As we already illustrate how to
represent both of them in caps before, so the negotiation here in fact has
no special operation except finding the intersection.


## Static Template Caps

If an element can list all the DRM fourcc/modifier composition at register
time, `gst-inspect` result should look like:

```
SRC template: 'src'
    Availability: Always
      Capabilities:
        video/x-raw(memory:DMABuf)
          width:  [ 16, 16384 ]
          height: [ 16, 16384 ]
          format: DMA_DRM
          drm-format: { (string)NV12:0x0100000000000001, \
                        (string)YU12, (string)YV12, \
                        (string)YUYV:0x0100000000000002, \
                        (string)P010:0x0100000000000002, \
                        (string)AR24:0x0100000000000002, \
                        (string)AB24:0x0100000000000002, \
                        (string)AR39:0x0100000000000002, \
                        (string)AYUV:0x0100000000000002 }
```

But because sometimes it is impossible to enumerate and list all
drm_fourcc/modifier composition in static templates (for example, we may
need a runtime context which is not available at register time to detect
the real modifers a HW can support), we can let the *drm-format* field
absent to mean the super set of all formats.


## Renegotiation

Sometimes, a renegotiation may happen if the downstream element is not
pleased with the caps set by the upstream element. For example, some sink
element may not know the preferred DRM fourcc/modifier until the real
render target window is realized. Then, it will send a "reconfigure" event
to upstream element to require a renegotiation. At this round negotiation,
the downstream element will provide a more precise *drm-format* list.


## Example

Consider the pipeline of:

```
vapostproc ! video/x-raw(memory:DMABuf) ! glupload
```

both `vapostproc` and `glupload` work on the same GPU. (DMABuf caps filter
is just for illustration, it doesn't need to be specified, since DMA
negotiation is well supported.)

The VA-API based `vapostproc` element can detect the modifiers at the
element registration time and the src template should be:

```
SRC template: 'src'
    Availability: Always
      Capabilities:
        video/x-raw(memory:DMABuf)
          width:  [ 16, 16384 ]
          height: [ 16, 16384 ]
          format: DMA_DRM
          drm-format: { (string)NV12:0x0100000000000001, \
                        (string)NV12, (string)I420, (string)YV12, \
                        (string)BGRA:0x0100000000000002 }
```

While `glupload` needs the runtime EGL context to check the DRM fourcc and
modifiers, so it can just leave the *drm-format* field absent in its sink
template:

```
SINK template: 'sink'
    Availability: Always
      Capabilities:
        video/x-raw(memory:DMABuf)
          width:  [ 1, 2147483647 ]
          height: [ 1, 2147483647 ]
          format: DMA_DRM
```

At runtime, when the `vapostproc` wants to decide its src caps, it first
query the downstream `glupload` element about all possible DMA caps. The
`glupload` should answer that query based on the GL/EGL query result, such
as:

```
drm-format: { (string)NV12:0x0100000000000001, (string)BGRA }
```

So, the intersection with `vapostproc`'s src caps will be
`NV12:0x0100000000000001`. It will be the sent to downstream (`glupload`)
by a CAPS event. The `vapostproc` element may also query the allocation
after that CAPS event, but downstream `glupload` will not provide a DMA
buffer pool because EGL API is mostly for DMAbuf importing. Then
`vapostproc` will create its own DMA pool, the buffers created from that
new pool should conform *drm-format*, described in this document, with
`NV12:0x0100000000000001`. Also, the downstream `glupload` should make sure
that it can import other DMA buffers which are not created in the pool it
provided, as long as they conform with *drm-format*
`NV12:0x0100000000000001`.

Then, when `vapostproc` handles each frame, it creates GPU surfaces with
*drm-format* `NV12:0x0100000000000001`. Each surface is also exported as a
set of file descriptors, each one wrapped in `GstMemory` allocated by a
subclass of `GstDmaBufAllocator`. All the `GstMemory` are appended to a
`GstBuffer`. There may be some extra information about the pitch, stride
and plane offset when we export the surface, we also need to translate them
into `GstVideoMeta` and attached it to the `GstBuffer`.

Later `glupload`, when it receives a `GstBuffer`, it can use those file
descriptors with *drm-format* `NV12:0x0100000000000001` to import an
EGLImage. If the `GstVideoMeta` exists, this extra parameters should also
be provided to the importing API.
