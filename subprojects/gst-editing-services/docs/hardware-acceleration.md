# Hardware-accelerated rendering

GES picks its compositor and the raw-video helpers it plugs around every
clip (color converter, scaler, deinterlacer, video flip, GPU
upload/download) from whatever GStreamer plugins are installed, ranked
by `GstRank`. The default ranks pick the software `compositor`, so an
HW backend (Vulkan, GL, CUDA, D3D11/12, HIP, ...) only takes over after
its compositor is promoted - see [Selecting a backend](#selecting-a-backend)
below.

## What the application sees

- The clip filter chain and the timeline's smart mixer use the highest
  ranked `Compositor`-klass element registered with GStreamer at the time
  GES first builds video.
- The supporting elements - color converter, scaler, deinterlacer, video
  flipper, uploader, downloader - are picked from the same plugin
  whenever possible, falling back to other plugins of the same memory
  type (e.g. `memory:GLMemory`), and finally to system memory.
- If a backend's compositor only consumes its native memory (for example
  `glvideomixerelement`), the whole video chain stays on that memory:
  no implicit downloads at the tail. Otherwise system memory is used as
  a common denominator between roles, with uploads/downloads added only
  around the compositor.
- If a role has no native element for the chosen memory but the backend
  ships an uploader/downloader pair, GES wraps a software element in a
  `download ! sw_core ! upload` bin so the rest of the chain stays
  native. If no such pair exists, the backend is rejected and the next
  one (or finally software) is tried.

The application sets clip geometry (`posx`, `posy`, `width`, `height`,
`alpha`, etc.) the same way regardless of the backend - the smart mixer
forwards those properties onto whichever compositor was picked.

## Selecting a backend

To make GES pick a hardware compositor, raise its rank above the software
one. Two equivalent mechanisms:

1. **`GST_PLUGIN_FEATURE_RANK`** environment variable (standard
   GStreamer mechanism), e.g.
   ```
   GST_PLUGIN_FEATURE_RANK=glvideomixer:MAX ges-launch-1.0 ...
   ```
2. **`gst_plugin_feature_set_rank()`** at runtime. GES listens for rank
   changes on every `Compositor`-klass feature and re-resolves the next
   time it builds video, so the override can happen before or after
   `ges_init()`.

## Internals

For the role-by-role registry-query rules, klass keywords, sysmem
fallback policy and wrapper shapes, see
`subprojects/gst-editing-services/docs/design/video-element-selection.md`
in the source tree.
