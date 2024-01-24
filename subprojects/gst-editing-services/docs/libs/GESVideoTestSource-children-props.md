#### `alpha`

alpha of the stream

Value type: #gdouble

#### `background-color`

Background color to use (big-endian ARGB)

Value type: #guint

See #videotestsrc:background-color

#### `fheight`

height of the source in float

Value type: #gfloat

#### `foreground-color`

Foreground color to use (big-endian ARGB)

Value type: #guint

See #videotestsrc:foreground-color

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

#### `pattern`

Type of test pattern to generate

Valid values:
  - **SMPTE 100% color bars** (0) – smpte
  - **Random (television snow)** (1) – snow
  - **100% Black** (2) – black
  - **100% White** (3) – white
  - **Red** (4) – red
  - **Green** (5) – green
  - **Blue** (6) – blue
  - **Checkers 1px** (7) – checkers-1
  - **Checkers 2px** (8) – checkers-2
  - **Checkers 4px** (9) – checkers-4
  - **Checkers 8px** (10) – checkers-8
  - **Circular** (11) – circular
  - **Blink** (12) – blink
  - **SMPTE 75% color bars** (13) – smpte75
  - **Zone plate** (14) – zone-plate
  - **Gamut checkers** (15) – gamut
  - **Chroma zone plate** (16) – chroma-zone-plate
  - **Solid color** (17) – solid-color
  - **Moving ball** (18) – ball
  - **SMPTE 100% color bars** (19) – smpte100
  - **Bar** (20) – bar
  - **Pinwheel** (21) – pinwheel
  - **Spokes** (22) – spokes
  - **Gradient** (23) – gradient
  - **Colors** (24) – colors
  - **SMPTE test pattern, RP 219 conformant** (25) – smpte-rp-219

See #videotestsrc:pattern

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

