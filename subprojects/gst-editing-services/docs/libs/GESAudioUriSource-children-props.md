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

#### `reverse`

Whether to playback the source reverse or not

Value type: #gboolean

See #nlesource:reverse

#### `volume`

volume factor, 1.0=100%

Value type: #gdouble

See #volume:volume

