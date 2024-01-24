#### `alpha`

alpha of the stream

Value type: #gdouble

#### `fheight`

height of the source in float

Value type: #gfloat

#### `fposx`

x position of the stream in float

Value type: #gfloat

#### `fposy`

y position of the stream in float

Value type: #gfloat

#### `fwidth`

width of the source in float

Value type: #gfloat

#### `height`

height of the source

Value type: #gint

#### `operator`

Blending operator to use for blending this pad over the previous ones

Valid values:
  - **Source** (0) – source
  - **Over** (1) – over
  - **Add** (2) – add

#### `posx`

x position of the stream

Value type: #gint

#### `posy`

y position of the stream

Value type: #gint

#### `reverse`

Whether to playback the source reverse or not

Value type: #gboolean

See #nlesource:reverse

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

#### `zorder`

z order of the stream.
**WARNING**: Setting it manually overrides the #GESLayer:priority and should be
used very carefully

Value type: #guint

