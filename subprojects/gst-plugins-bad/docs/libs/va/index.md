# VA library

Library for sharing and handling VADisplay inside GStreamer's pipelines.

This library should be linked to by getting cflags and libs from
gstreamer-va{{ gst_api_version.md }}.pc

More information about VA-API
[http://intel.github.io/libva/index.html](http://intel.github.io/libva/index.html)

## CPU access to decoded frames

VA decoders can negotiate hardware-backed buffers using the `VAMemory` caps
feature. Applications that negotiate `video/x-raw(memory:VAMemory)` are expected
to handle VA surfaces directly, for example by using VA-API primitives with the
surface handle.

Applications that need ordinary CPU-readable video frames should negotiate plain
`video/x-raw` instead. For example:

```none
vah264dec ! video/x-raw,format=NV12 ! appsink
```

In contrast, this negotiates VA surfaces rather than normal system-memory raw
video frames:

```none
vah264dec ! video/x-raw(memory:VAMemory),format=NV12 ! ...
```

Do not rely on mapping `video/x-raw(memory:VAMemory)` buffers as a general
CPU-access path. If CPU processing is needed, request plain `video/x-raw` so the
pipeline can perform the required download/copy and expose system-memory
buffers downstream.

> NOTE: This library API is considered *unstable*
