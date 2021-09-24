#### `alpha`

alpha of the stream

Value type: #gdouble

#### `fields`

Fields to use for deinterlacing

Valid values:
  - **All fields** (0) – all
  - **Top fields only** (1) – top
  - **Bottom fields only** (2) – bottom
  - **Automatically detect** (3) – auto

See #deinterlace:fields

#### `height`

height of the source

Value type: #gint

#### `mode`

Deinterlace Mode

Valid values:
  - **Auto detection (best effort)** (0) – auto
  - **Force deinterlacing** (1) – interlaced
  - **Run in passthrough mode** (2) – disabled
  - **Auto detection (strict)** (3) – auto-strict

See #deinterlace:mode

#### `posx`

x position of the stream

Value type: #gint

#### `posy`

y position of the stream

Value type: #gint

#### `tff`

Deinterlace top field first

Valid values:
  - **Auto detection** (0) – auto
  - **Top field first** (1) – tff
  - **Bottom field first** (2) – bff

See #deinterlace:tff

#### `video-direction`

Video direction: rotation and flipping

Valid values:
  - **GST_VIDEO_ORIENTATION_IDENTITY** (0) – identity
  - **GST_VIDEO_ORIENTATION_90R** (1) – 90r
  - **GST_VIDEO_ORIENTATION_180** (2) – 180
  - **GST_VIDEO_ORIENTATION_90L** (3) – 90l
  - **GST_VIDEO_ORIENTATION_HORIZ** (4) – horiz
  - **GST_VIDEO_ORIENTATION_VERT** (5) – vert
  - **GST_VIDEO_ORIENTATION_UL_LR** (6) – ul-lr
  - **GST_VIDEO_ORIENTATION_UR_LL** (7) – ur-ll
  - **GST_VIDEO_ORIENTATION_AUTO** (8) – auto
  - **GST_VIDEO_ORIENTATION_CUSTOM** (9) – custom

See #GstVideoDirection:video-direction

#### `width`

width of the source

Value type: #gint

