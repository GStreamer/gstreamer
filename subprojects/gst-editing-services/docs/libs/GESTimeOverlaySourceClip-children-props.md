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

#### `font-desc`

Pango font description of font to be used for rendering. See documentation of
pango_font_description_from_string for syntax.

Value type: #gchararray

See #GstBaseTextOverlay:font-desc

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

#### `freq`

Frequency of test signal. The sample rate needs to be at least 2 times higher.

Value type: #gdouble

See #audiotestsrc:freq

#### `fwidth`

width of the source in float

Value type: #gfloat

#### `halignment`

Horizontal alignment of the text

Valid values:
  - **left** (0) – left
  - **center** (1) – center
  - **right** (2) – right
  - **Value_3** (3) – value-3
  - **position** (4) – Absolute position clamped to canvas

See #GstBaseTextOverlay:halignment

#### `height`

height of the source

Value type: #gint

#### `input-channels-reorder`

The positions configuration to use to reorder the input channels consecutively
according to their index.

Valid values:
  - **Reorder the input channels using the default GStreamer order** (0) – gst
  - **Reorder the input channels using the SMPTE order** (1) – smpte
  - **Reorder the input channels using the CINE order** (2) – cine
  - **Reorder the input channels using the AC3 order** (3) – ac3
  - **Reorder the input channels using the AAC order** (4) – aac
  - **Reorder and mix all input channels to a single mono channel** (5) – mono
  - **Reorder and mix all input channels to a single left and a single right stereo channels alternately** (6) – alternate

See #audioconvert:input-channels-reorder

#### `input-channels-reorder-mode`

The input channels reordering mode used to apply the selected positions
configuration.

Valid values:
  - **Never reorder the input channels** (0) – none
  - **Reorder the input channels only if they are unpositioned** (1) – unpositioned
  - **Always reorder the input channels according to the selected configuration** (2) – force

See #audioconvert:input-channels-reorder-mode

#### `mix-matrix`

Transformation matrix for input/output channels.

Value type: #GstValueArray

See #audioconvert:mix-matrix

#### `mute`

mute channel

Value type: #gboolean

See #volume:mute

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

#### `reverse`

Whether to playback the source reverse or not

Value type: #gboolean

See #nlesource:reverse

#### `text-width`

Resulting width of font rendering

Value type: #guint

See #GstBaseTextOverlay:text-width

#### `text-x`

Resulting X position of font rendering.

Value type: #gint

See #GstBaseTextOverlay:text-x

#### `text-y`

Resulting Y position of font rendering.

Value type: #gint

See #GstBaseTextOverlay:text-y

#### `time-mode`

What time to show

Valid values:
  - **buffer-time** (0) – buffer-time
  - **stream-time** (1) – stream-time
  - **running-time** (2) – running-time
  - **time-code** (3) – time-code
  - **elapsed-running-time** (4) – elapsed-running-time
  - **reference-timestamp** (5) – reference-timestamp
  - **buffer-count** (6) – buffer-count
  - **buffer-offset** (7) – buffer-offset

See #timeoverlay:time-mode

#### `valignment`

Vertical alignment of the text

Valid values:
  - **baseline** (0) – baseline
  - **bottom** (1) – bottom
  - **top** (2) – top
  - **position** (3) – Absolute position clamped to canvas
  - **center** (4) – center
  - **absolute** (5) – Absolute position

See #GstBaseTextOverlay:valignment

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

#### `volume`

Volume of test signal

Value type: #gdouble

See #audiotestsrc:volume

#### `volume`

volume factor, 1.0=100%

Value type: #gdouble

See #volume:volume

#### `width`

width of the source

Value type: #gint

#### `zorder`

z order of the stream.
**WARNING**: Setting it manually overrides the #GESLayer:priority and should be
used very carefully

Value type: #guint

