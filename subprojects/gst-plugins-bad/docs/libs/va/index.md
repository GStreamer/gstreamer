# VA library

Library for sharing and handling VADisplay inside GStreamer's pipelines.

This library should be linked to by getting cflags and libs from
gstreamer-va{{ gst_api_version.md }}.pc

More information about VA-API
[http://intel.github.io/libva/index.html](http://intel.github.io/libva/index.html)

### CPU Access to VA Frames

VA elements can negotiate three types of caps features:

- `video/x-raw(memory:DMABuf)`
- `video/x-raw(memory:VAMemory)`
- `video/x-raw`

#### `video/x-raw(memory:DMABuf)`

See [DMABuf design](additional/design/dmabuf.md).

#### `video/x-raw(memory:VAMemory)`

These are `GstBuffer` objects with a single `GstMemory` wrapping a
`VASurfaceID`. They can usually be mapped to system memory, but typically
require `VideoMeta` to correctly access the mapped information. Users can also
obtain the `VASurfaceID` and manipulate it directly with VA-API primitives.

Note that mapping to system memory and unmapping can be time-consuming,
especially if the frame is modified and re-uploaded (e.g., for subtitle
blending).

#### `video/x-raw`

For VA elements that pushes buffers in its source pad (video decoders and
post-processor) this caps can mean either:

1. An alias of `video/x-raw(memory:VAMemory)`
2. A system-memory-backed buffer.

When it's an alias of `video/x-raw(memory:VAMemory)`, the surfaces can be
mapped, and all the rules above apply.

If a VA decoder or VA postproc negotiates `video/x-raw` and downstream does not
support `VideoMeta`, then (if `VideoMeta` is required by the buffer's format)
the element internally copies the surface into a newly allocated system-memory
buffer. Otherwise, if `VideoMeta` is not required by the surface's format, the
surface is shared as if it were `video/x-raw(memory:VAMemory)`.

Applications that need ordinary CPU-readable video frames should negotiate plain
`video/x-raw`. For example:

```none
vah264dec ! video/x-raw,format=NV12 ! appsink
```

In contrast, the following negotiates VA surfaces rather than normal
system-memory raw video frames:

```none
vah264dec ! video/x-raw(memory:VAMemory),format=NV12 ! ...
```

As general recommendation, do not rely on mapping `video/x-raw(memory:VAMemory)`
buffers as a general CPU-access path. If CPU processing is needed, request plain
`video/x-raw` so the pipeline can perform the required download/copy and expose
system-memory buffers downstream.

> NOTE: This library API is considered *unstable*
