# Generic Raw Video Format

This document describes GStreamer generic raw format. This format is intended to
support any video format described in ISO/IEC 23001-17 and MXF ST 377-1
(Annex F/G) specifications. Raw video formats different from generic
are reserved for formats used by existing sensors, or formats for which we
have optimized packing/unpacking functions.

ST 377-1 Annex F defines the Generic Picture Essence Descriptor, CDCIDescriptor
and RGBADescriptor used to describe uncompressed video formats in MXF.

# Motivation

Several use cases require video formats that cannot be enumerated as individual
GstVideoFormat entries. Container formats such as MXF ST 377-1 and ISO/IEC
23001-17 describe uncompressed video parametrically, producing thousands of
valid combinations. Scientific and analytics pipelines operate on floating-point
pixel data not representable as integer formats. Image decoders such as OpenEXR
produce arbitrary component layouts that cannot be mapped to an existing
GstVideoFormat without loss of fidelity. Specialty workflows use component sets
— CMYK, depth, disparity, spectral bands that do not fit the RGB/YUV model.
A parametric mechanism will ease support of these formats without requiring a
new enum entry for each one.

# Context

GStreamer currently supports 150+ video formats enumerated in GstVideoFormat.
ISO/IEC 23001-17 (ISOBMFF uncompressed video) and MXF ST 377-1 (Annex F/G)
define generic parametric descriptions that support numerous format combinations
through parameters such as bit depth, component ordering, subsampling patterns,
alignment requirements, and endianness variations. Adding individual enum
entries for each combination would:

- Impact negotiation performance (caps matching iterates all formats)
- Bloat the GstVideoFormat enum indefinitely
- Not scale for future generic specifications

This design introduces GST_VIDEO_FORMAT_GENERIC with parametric extensions
to support these specifications without enum proliferation.

# Design Overview

## Video Format Intrinsic

GStreamer describes video format intrinsics using GstVideoFormatInfo. This
structure is already very general and can describe all RGB, YUV, and grayscale
formats. This structure can be easily extended as it's not part of
the API. All video formats supported in GStreamer are represented by a video
format symbolized by an entry in the enum GstVideoFormat and each entry has a
fully defined GstVideoFormatInfo value inside GStreamer internal data. More
specifically each enum has a corresponding value inside 'formats' which is a
static variable defined inside video-format.c. Extension of GstVideoFormatInfo
is not an issue as it's not a public structure, but addition of new format entry
has a performance cost on negotiation time. For this reason new format entries
need to be limited to truly intrinsic variants of video format.

## Video Format Extrinsic

GStreamer video description also includes extrinsic parameters. In GStreamer
video extrinsic parameters are represented by GstVideoInfo. Examples of video
extrinsic parameters are width, height, framerate, which are parameters
that will change even for a given GstVideoFormatInfo value. Extrinsic video
parameters are decided at runtime and therefore it's the application or
negotiation that will decide the specific value of GstVideoInfo.

## Complete Video Format

GstVideoInfo in addition to defining all extrinsic video parameters also has
a reference to a GstVideoFormat and therefore contains a complete description of
the video.

# Generic Format Representation

## Handling Large Number of Video Formats

ISO/IEC 23001-17 specification describes uncompressed video based on ISO base
media file format standard. This specification and other specifications like
SMPTE ST 377-1 (Annex F/G) (MXF) have generic semantics to describe uncompressed video
and image formats. Adding a new entry for each possible combination would lead
to slower negotiation and therefore is not a suitable solution for GStreamer to
support generic formats described in these specifications.

## Unified Parametric Representation

Both ISO 23001-17 and MXF ST 377-1 describe uncompressed video through
parametric component definitions. Rather than maintaining separate internal
representations for each specification, we define a single unified parametric
structure that covers both and is extensible for future specifications.

The parameter spaces of both specifications overlap substantially:

| Parameter Domain    | ISO 23001-17 (uncv)               | MXF ST 377-1 (CDCI/RGBA)              |
|---------------------|-----------------------------------|----------------------------------------|
| Components          | Per-component: index, depth,      | ComponentDepth (shared),               |
|                     | format, alignment                 | PixelLayout (RGBA only)                |
| Subsampling         | sampling_type enum                | HorizontalSubsampling +               |
|                     |                                   | VerticalSubsampling                    |
| Color siting        | Derived from sampling             | ColorSiting enum                       |
| Endianness          | components_little_endian          | ReversedByteOrder                      |
| Alignment/padding   | pixel_size, row_align_size,       | PaddingBits,                           |
|                     | component align_size              | ImageAlignmentOffset                   |
| Tiling              | num_tile_cols, num_tile_rows      | Not in picture descriptor              |
| Signal levels       | Not applicable                    | BlackRefLevel, WhiteRefLevel,          |
|                     |                                   | ColorRange                             |

The core pixel layout parameters of MXF CDCIDescriptor and RGBADescriptor can
be expressed using the ISO 23001-17 component model. MXF-specific signal-level
parameters (BlackRefLevel, WhiteRefLevel, ColorRange) are not pixel format
intrinsics; they are closer to colorimetry metadata that GStreamer handles
separately in GstVideoColorimetry.

The unified internal structure captures the superset of parameters:
- Component definition: array of (component_type, bit_depth, component_format,
  align_size) tuples, one per component in memory layout order.
- Sampling type / subsampling ratios
- Interleave type
- block_size
- flags: component endianness, component padding lsb, block padding lsb, block endianness, block reversed
- Pixel size, row alignment
- Tiling (optional)

### Block Alignment and Padding

Generic formats support block-based alignment where component values can be grouped
into fixed-size blocks. This allows formats to meet specific memory alignment
requirements:

**block_size**: When non-zero, component values are grouped into blocks of exactly
this many bytes. Any unused space within a block is filled with padding bits. When
zero, no blocking is applied and components may be tightly packed according to their
individual bit depths.

**Component alignment**: Individual components can also specify byte alignment
through the `align_size` field in component-definition. When 0, the component is packed
without byte alignment. When non-zero, each component value starts on the specified
byte boundary. When a component's bit depth is smaller than the aligned size,
the remaining bits are padding. The `components_pad_lsb` flag controls whether
this padding is placed at the MSB (default, value occupies the least significant
bits) or at the LSB (value occupies the most significant bits).

Block-level control flags (block_pad_lsb, block_little_endian, block_reversed)
determine padding location and byte ordering within blocks. These are described
in the format parameters section below.

## Generic Intrinsic Video Format And Dynamic Extrinsic Video Format

We can support all new generic raw video formats using only one new entry in
GstVideoFormat: GST_VIDEO_FORMAT_GENERIC. By allowing heap allocated extensions
to GstVideoInfo, this structure can be extended to support any generic video
format.

## GstVideoInfo Extension Point

GstVideoInfo has only one available slot in `_gst_reserved[]`. We use this slot
to store an opaque, reference-counted extension pointer. The internal
representation is not exposed and all access goes through typed accessor APIs
(e.g., `gst_video_info_get_generic_params()`). This allows the internal storage
to evolve without API breaks.

While the current foreseen use case is supporting GENERIC format parameters, this
extension mechanism is not tied exclusively to the GENERIC format. Future
extensions can be added with additional typed accessors.

**Internal representation (not public API):**

The extension pointer references a GstMiniObject subtype
(`GstVideoInfoExtensions`) that owns the individual payloads:

```c
struct _GstVideoInfoExtensions {
  GstMiniObject mini_object;
  GstGenericVideoParams *generic_params;  /* NULL if not GENERIC */
  GstMxfSignalLevels    *signal_levels;   /* NULL if not present */
  /* future fields added here */
};
```

Multiple GstVideoInfo instances may share the same extensions through
GstMiniObject refcounting. Sharing occurs when a GstVideoInfo is copied
from another via gst_video_info_copy() or gst_video_info_copy_into(),
for example, when GstVideoFrame copies from an element's in_info, or
when a codec state is duplicated. Elements that independently parse the
same caps via gst_video_info_from_caps_extended() each allocate their
own extension object.

```
+--------------------------------+            +---------------------------+
| formats: GstVideoFormatInfo    |            | inst: GstVideoInfo        |
+================================+            +===========================+
| ...                            |            | interlace_mode            |
+--------------------------------+            +---------------------------+
| v for GST_VIDEO_FORMAT_GENERIC *<-----------* finfo                     |
+--------------------------------+            +---------------------------+
                                              |                           |
+--------------------------------------+   /--* extensions (opaque)       |
|inst: GstVideoInfoExtensions          |  /   +---------------------------+
| (GstMiniObject, not public API)      *</
+======================================+
|                                      |
| *generic_params                      *--------->+------------------------------+
+--------------------------------------+          | inst: GstGenericVideoParams  |
| *signal_levels ---\                  |          +==============================+
+--------------------------------------+          | version                      |
                                  |               +------------------------------+
+------------------------------+  |               | component_definition[]       |
| inst: GstMxfSignalLevels     |<-/               |   - type, depth, format,     |
+==============================+                  |     align_size per component |
| black_ref_level              |                  +------------------------------+
+------------------------------+                  | sampling_type                |
| white_ref_level              |                  +------------------------------+
+------------------------------+                  | interleave_type              |
| color_range                  |                  +------------------------------+
+------------------------------+                  | block_size                   |
                                                  +------------------------------+
                                                  | components_little_endian     |
                                                  +------------------------------+
                                                  | components_pad_lsb           |
                                                  +------------------------------+
                                                  | pixel_size                   |
                                                  +------------------------------+
                                                  | row_align_size               |
                                                  +------------------------------+
                                                  | num_tile_cols, num_tile_rows |
                                                  +------------------------------+
```

## Generic Format Caps Specification

Caps for GENERIC format use media type "video/x-raw" with format=GENERIC.
The following tables define all supported caps fields.

### Required Fields

| Field                  | Type           | Description                                        |
|------------------------|----------------|----------------------------------------------------|
| format                 | string         | Must be "GENERIC"                                  |
| width                  | int            | Picture width in pixels                            |
| height                 | int            | Picture height in pixels                           |
| component-definition   | GstValueArray  | Array of component specifications (see below)      |
| sampling-type          | uint           | Subsampling pattern (0=none, 1=4:2:2, 2=4:2:0, 3=4:1:1) |


**component-definition array:**

Each entry in the component-definition array is a GstStructure that fully describes
one component in the pixel layout, including its semantic type and encoding parameters.

**Component structure fields:**

| Field Name | Type   | Description |
|------------|--------|-------------|
| type       | string | Component semantic type (see table below) |
| depth      | uint   | Component bit depth (number of bits per value) |
| format     | string | Numerical encoding format (see table below) |
| align_size | uint   | Component alignment size in bytes (0=packed, no byte alignment) |

**Component Type Values:**

| Component Type  | Description |
|-----------------|-------------|
| "grayscale"     | Monochrome component |
| "luma"          | Luma component (Y) |
| "chroma-blue"   | Chroma blue component (U/Cb) |
| "chroma-red"    | Chroma red component (V/Cr) |
| "red"           | Red component |
| "green"         | Green component |
| "blue"          | Blue component |
| "alpha"         | Transparency component |
| "depth"         | Depth component |
| "disparity"     | Disparity component |
| "palette"       | Palette index component |
| "filter"        | Filter array component (e.g., Bayer pattern) |
| "padded"        | Padding component (unused data) |
| "cyan"          | Cyan component |
| "magenta"       | Magenta component |
| "yellow"        | Yellow component |
| "key"           | Key component (black in CMYK) |

**Component Numerical Format Values:**

| Format String      | Description |
|--------------------|-------------|
| "unsigned-int"     | Unsigned integer values |
| "float"            | IEEE 754 floating point |
| "complex-number"   | Complex float (real + imaginary pairs) |
| "signed-int"       | Signed integer values |

**Array ordering:** The order of components in the component-definition array
determines the memory layout order (before applying interleaving modes).

**Multiple components of same type:** For formats like Multi-Y interleaving, the
same component type can appear multiple times in the array (e.g., multiple "luma"
entries with the same or different encoding parameters).


### General Fields

| Field                      | Type    | Default      | Description |
|----------------------------|---------|--------------|-------------|
| interleave-type            | string  | "interleave" | Component memory arrangement (see Interleave Types section) |
| block-size                 | uint    | 0            | Block alignment in bytes (0 = no blocking) |
| components-little-endian   | boolean | false        | Byte order for multi-byte component values |
| components-pad-lsb         | boolean | false        | Component padding location when align_size > depth: true = LSB (value in MSBs), false = MSB (value in LSBs) |
| pixel-size                 | uint    | 0            | Total bytes per pixel. Use 0 to auto-calculate from component bit depths. Set explicitly when padding or alignment makes automatic calculation incorrect. |
| row-align-size             | uint    | 0            | Row alignment in bytes (0 = no row alignment) |
| num-tile-cols              | uint    | 1            | Number of tile columns (1 = no tiling) |
| num-tile-rows              | uint    | 1            | Number of tile rows (1 = no tiling) |

### Block Control Flags

These flags control padding location and byte ordering within blocks (only applicable when block-size > 0):

| Field                      | Type    | Default | Description |
|----------------------------|---------|---------|-------------|
| block-pad-lsb              | boolean | false   | Padding bit location: true = LSB (least significant bits), false = MSB (most significant bits) |
| block-little-endian        | boolean | false   | Byte order within blocks: true = little-endian, false = big-endian |
| block-reversed             | boolean | false   | Reverse component order within blocks |

### Interleave Types

The interleave-type field controls how component values are arranged in memory:

**"component"** (Component interleaving / Planar)
All values of the first component for the entire frame, followed by all values of
the second component, etc. Also known as planar format.

**"interleave"** (Pixel interleaving / Packed)
All components of the first pixel, followed by all components of the second pixel,
etc. Also known as packed format.

**"mixed-interleave"** (Mixed interleaving)
Used for subsampled formats where some components are fully sampled and others are
subsampled. The fully sampled components are interleaved, while subsampled components
follow their own interleaving pattern. Commonly used for YUV 4:2:2 and 4:2:0.

**"row-interleave"** (Row interleaving)
Complete rows of each component are stored sequentially. All rows of the first
component, then all rows of the second component, etc.

**"tile-component"** (Tile-component interleaving)
The frame is divided into tiles. For each tile, all components are stored before
moving to the next tile. Requires num-tile-cols and num-tile-rows to be set.

**"multi-Y"** (Multi-Y pixel interleaving)
Used for formats with multiple luma samples per chroma sample (e.g., 4:2:2, 4:1:1).
Multiple luma component values are interleaved with shared chroma values in pixel
interleaving mode.

The sampling-type field works together with interleave-type to determine the complete
memory layout. Some interleave types have constraints on valid sampling-type values.


### Examples

**Predefined profile 'rgb3' - 8-bit RGB pixel-interleaved (from Table 5):**

From ISO 23001-17 Table 5: `{'rgb3', [{4,7},{5,7},{6,7}], 0, 1}`, i.e.
components R, G, B each 8-bit (bit_depth_minus_one=7), no subsampling
(sampling_type=0), pixel interleaved (interleave_type=1). All parameters
are implicit from the profile.

```
video/x-raw, format=GENERIC,
             component-definition=<
                "component, type=(string)red, depth=(uint)8, format=(string)unsigned-int, align_size=(uint)0;",
                "component, type=(string)green, depth=(uint)8, format=(string)unsigned-int, align_size=(uint)0;",
                "component, type=(string)blue, depth=(uint)8, format=(string)unsigned-int, align_size=(uint)0;">,
             sampling-type=(uint)0,
             interleave-type="interleave"

```

**Note**: the caps above match an existing video format in `GstVideoFormat` but
we use it to provide a simple example.

**RGB 10 bits components, tightly packed with pixel alignment byte boundary (32 bits):**


```
video/x-raw, format=GENERIC,
             component-definition=<
                "component, type=(string)red, depth=(uint)10, format=(string)unsigned-int, align_size=(uint)0;",
                "component, type=(string)green, depth=(uint)10, format=(string)unsigned-int, align_size=(uint)0;",
                "component, type=(string)blue, depth=(uint)10, format=(string)unsigned-int, align_size=(uint)0;">,
             sampling-type=(uint)0,
             interleave-type="interleave",
             block-size=4,
```

# API Changes

## New APIs

**gst_video_info_get_generic_params()**
```c
const GstGenericVideoParams *
gst_video_info_get_generic_params (const GstVideoInfo *info);
```
Returns the GENERIC format parameters from a GstVideoInfo, or NULL if the
format is not GENERIC or no extensions are present. The returned structure is
owned by the GstVideoInfo and must not be freed by the caller.

**gst_video_info_copy_into()**
```c
void gst_video_info_copy_into (GstVideoInfo *dest, const GstVideoInfo *src);
```
Copies a GstVideoInfo into an existing destination, properly refcounting
extensions. This is the safe replacement for struct assignment
(`dest = *src`) when extensions may be present. Unrefs any existing extensions
on dest before copying.

**gst_video_info_clear()**
```c
void gst_video_info_clear (GstVideoInfo *info);
```
Clears a GstVideoInfo structure, releasing any associated resources. When
extensions are present, unrefs the extensions. Should be called on
stack-allocated GstVideoInfo to clean up internal resources. Safe to call on
GstVideoInfo without extensions.

**gst_video_info_from_caps_extended()**
```c
gboolean gst_video_info_from_caps_extended (GstVideoInfo *info,
                                            const GstCaps *caps);
```
Parse caps and extract generic raw format details and set base and extended
extrinsic video parameters. It will allocate required extension structures.
It will also set the reference `finfo` to the entry corresponding to
GST_VIDEO_FORMAT_GENERIC. If the generic format corresponds to a non-generic
format, it uses the non-generic format instead and does not use extensions
(for optimization).

**Generic pack/unpack functions**
```c
gboolean gst_video_format_generic_pack (const GstVideoFormatInfo *finfo,
                                        GstVideoPackFlags flags,
                                        const gpointer src, gint sstride,
                                        gpointer data, gint stride,
                                        GstVideoChromaSite chroma_site,
                                        gint y, gint width,
                                        const GstVideoInfo *info);

gboolean gst_video_format_generic_unpack (const GstVideoFormatInfo *finfo,
                                          GstVideoPackFlags flags,
                                          gpointer dest, gint dstride,
                                          const gpointer data, gint stride,
                                          GstVideoChromaSite chroma_site,
                                          gint y, gint width,
                                          const GstVideoInfo *info);
```
Convert between generic format and a canonical unpacked format. The
`info` parameter is the GstVideoInfo from which the functions extract the
GENERIC format parameters to determine the format layout.

These functions return FALSE if the parameter combination described in
the extensions is not supported (e.g., an unknown component format or an
unsupported interleave mode). Callers must check the return value.

### Canonical Unpack Format Selection

The unpack destination format is determined by the GENERIC format's color model,
which is derived from the component definitions:

- **YCbCr components** -> unpack to AYUV64 (16-bit per component)
- **RGB components** -> unpack to ARGB64 (16-bit per component)
- **Grayscale** -> unpack to AYUV64 (16-bit per component)
- **Float components** -> unpack to ARGB256 (64-bit per component).

The pack function performs the inverse mapping. The canonical format is chosen
to match GStreamer's existing pack/unpack conventions, ensuring that
videoconvert and other conversion elements work without special-casing.

## Modified Existing APIs

**gst_video_info_copy()**
Extended to properly handle extensions. When extensions are present, the
extensions are ref'd (not deep-copy) since extensions are immutable after
negotiation. This makes the copy safe and efficient through refcounting.

**gst_video_info_free()**
Extended to call gst_video_info_clear() before freeing the GstVideoInfo.

**gst_video_info_from_caps()**
Returns FALSE when encountering format=GENERIC in caps. Code must use
gst_video_info_from_caps_extended() to support generic formats.

**gst_video_info_init()**
Extended to call gst_video_info_clear() first if extensions are present,
preventing leaks when re-initializing a GstVideoInfo that previously held
GENERIC format data.

**gst_video_info_to_caps()**
When format is GENERIC, checks if the generic format corresponds to a
non-generic format and uses the non-generic caps representation if possible.
Otherwise produces caps with format=GENERIC.

# Negotiation

## Element Support Levels

Elements can support GENERIC formats at three levels:

Transparent: Pass-through without understanding (queue, identity, videorate)
Inspection: Can parse extensions but not process (typefind, debuggers)
Processing: Full support for specific generic specifications (videoconvert using pack/unpack), isomp4mux

## Capability Advertisement

Elements advertising GENERIC support include format=GENERIC in their caps
templates:

```c
static GstStaticPadTemplate sink_template =
  GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      "video/x-raw, format=I420; "
      "video/x-raw, format=GENERIC
    )
  );
```

# Backwards Compatibility

## Element Compatibility via Conversion

Elements unaware of GENERIC formats will not include format=GENERIC in their
caps templates, causing negotiation to naturally exclude these paths. For
pipelines requiring both generic and non-generic elements, videoconvert acts
as a bridge:

- videoconvert uses generic pack/unpack functions to convert GENERIC formats
  to canonical unpacked formats (I420, RGBA, etc.)
- Most existing elements support these canonical unpacked formats

## GstVideoInfo Copy Safety

Existing code may copy GstVideoInfo through several patterns:

| Pattern                           | Location                      | Current behavior          |
|-----------------------------------|-------------------------------|---------------------------|
| `g_memdup2(info, sizeof(...))`    | `gst_video_info_copy()`      | Shallow pointer copy      |
| `frame->info = *info`            | `gst_video_frame_map_id()`   | Struct assignment = memcpy|
| `memset(info, 0, sizeof(...))`   | `gst_video_info_init()`      | Zeroes extension pointer  |
| Embedded in_info / out_info       | `GstVideoFilter`             | Base class struct assign  |
| Direct struct copy by user code   | Third-party applications      | Shallow pointer copy      |

### Safety Through the Gatekeeper Pattern

The primary safety mechanism is that **unaware code never encounters a GENERIC
GstVideoInfo**:

- `gst_video_info_from_caps()` returns FALSE for GENERIC caps
- Only `gst_video_info_from_caps_extended()` creates GENERIC GstVideoInfo
- Elements that don't advertise GENERIC support won't negotiate GENERIC caps
- Therefore old elements never receive a GstVideoInfo with extensions

### Safety Through Reference Counting

For GENERIC-aware code that does copy GstVideoInfo, the extensions are
reference-counted. All GStreamer internal functions that copy GstVideoInfo
will be updated to properly handle extensions:

- `gst_video_info_copy()` - refs the extensions on the new copy
- `gst_video_frame_map_id()` - uses `gst_video_info_copy_into()` for
  `frame->info`
- `gst_video_info_init()` - unrefs existing extensions before memset
- `gst_video_info_clear()` - unrefs extensions and NULLs the pointer
- `gst_video_converter_new()`/`gst_video_converter_free()` - uses
  `gst_video_info_copy_into()` for in_info/out_info, unrefs on free
- `gst_video_info_dma_drm_from_video_info()` - uses
  `gst_video_info_copy_into()` for `drm_info->vinfo`

### Remaining Risk: Third-Party/Application Struct Copies

Third-party code and applications written before GENERIC existed are **not
at risk**: the gatekeeper pattern ensures they never encounter a GstVideoInfo
with extensions. Since `gst_video_info_from_caps()` returns FALSE for GENERIC
caps, and old elements don't advertise GENERIC support, `_gst_reserved[3]`
remains NULL in any GstVideoInfo they handle. Copying NULL via struct
assignment or memcpy is safe. For example, the following application-level
code embeds GstVideoInfo in structs and populates it via
`gst_video_info_from_caps()`, which rejects GENERIC -- so these are safe:
- `gst-plugins-good/tests/examples/cairo/cairo_overlay.c` - GstVideoInfo
  embedded in CairoOverlayState struct
- `gst-plugins-base/tests/examples/overlaycomposition/overlaycomposition.c` -
  GstVideoInfo embedded in OverlayState struct
- `gst-plugins-bad/tests/examples/cuda/template-plugin/cuda-transform-ip-template.c` -
  GstVideoInfo embedded in element struct

The risk applies only to **new GENERIC-aware code** that copies GstVideoInfo
by struct assignment or memcpy instead of using `gst_video_info_copy()`.
Such a copy produces a shallow copy of the extensions pointer without
incrementing the reference count, which can lead to use-after-free if one
copy's extensions are freed.

Mitigation: GENERIC-aware code should use `gst_video_info_copy_into()` instead
of struct assignment. The GENERIC GstVideoFormatInfo's pack/unpack function
pointers validate that extensions are present and log a warning if the
extensions pointer is NULL when format is GENERIC, helping detect this class
of bug.

## Video Base Class Integration

### GstVideoEncoder / GstVideoDecoder

These base classes use GstVideoCodecState which heap-allocates GstVideoInfo.
The base classes manage the lifecycle through their API
(`gst_video_encoder_get_output_state()`, etc.). Internal updates are needed
to ref extensions when copying GstVideoInfo within the state management code,
but no API changes are required.

Subclasses access GENERIC parameters through the getter API:
```c
static gboolean
my_encoder_set_format (GstVideoEncoder *encoder, GstVideoCodecState *state)
{
  const GstGenericVideoParams *params;

  params = gst_video_info_get_generic_params (&state->info);
  if (params) {
    /* Handle GENERIC format */
  }
  return TRUE;
}
```

### GstVideoFilter

GstVideoFilter embeds `GstVideoInfo in_info` and `GstVideoInfo out_info`
directly in the struct. The base class sets these via internal code that
currently uses struct assignment during caps negotiation (in set_caps).

Required changes:
- The base class `set_caps` implementation must use
  `gst_video_info_copy_into()` for assigning in_info / out_info
- The base class `finalize` / state-change-to-NULL must call
  `gst_video_info_clear()` on in_info and out_info

### GstVideoFrame Limitations

GstVideoFrame embeds GstVideoInfo directly and has fixed-size arrays:
```c
gpointer data[GST_VIDEO_MAX_PLANES];   /* GST_VIDEO_MAX_PLANES = 4 */
GstMapInfo map[GST_VIDEO_MAX_PLANES];
```

This creates two constraints for GENERIC formats:

1. **Embedded GstVideoInfo copy:** `gst_video_frame_map_id()` performs
   `frame->info = *info` (struct assignment). This is updated to use
   `gst_video_info_copy_into()`. `gst_video_frame_unmap()` is updated to
   call `gst_video_info_clear()`.

2. **Maximum 4 planes:** GENERIC formats are limited to GST_VIDEO_MAX_PLANES
   (4) planes when used with GstVideoFrame. Formats with more components
   must map multiple components into fewer planes. Elements that need to
   handle GENERIC formats with >4 planes should use GstVideoMeta and direct
   GstBuffer mapping instead of GstVideoFrame. Increasing
   GST_VIDEO_MAX_PLANES is an ABI break and is out of scope for this design.

## ABI Stability

The extensions pointer replaces one slot in GstVideoInfo._gst_reserved[].
The opaque extension object stored at this location serves as an open-ended
extension point. No further _gst_reserved[] slots are consumed, and additional
data is carried as fields within the extension object. This maintains ABI
compatibility while providing unlimited extensibility for heap-allocated data.

# Appendix: Floating Point And Complex Number Components

This section describes how floating point and complex number components are
represented within the GENERIC format. The `format` field in the per-component
caps definition already distinguishes integer, float, and complex types. No new
GstVideoFormatInfo flags (GST_VIDEO_FORMAT_FLAG_FLOAT,
GST_VIDEO_FORMAT_FLAG_COMPLEX) are required for GENERIC -- those flags would
only be needed for future concrete GstVideoFormat enum entries and can be added
independently.

## Floating Point Components

When the component format is "float", the bit depth determines
the precision:
- 16-bit: half precision (IEEE 754 binary16)
- 32-bit: single precision (IEEE 754 binary32)
- 64-bit: double precision (IEEE 754 binary64)

## Complex Number Components

When the component format is "complex-number", each component is a pair of
floating-point values (real + imaginary) or in polar form (magnitude + phase).
The bit depth specifies the total size of the pair (e.g., 64-bit = two 32-bit
floats). This supports scientific imaging, radar data, and frequency domain
representations. The mechanism for distinguishing rectangular from polar
representation will be refined in a future revision of this design.

Complex formats use the same interleave-type caps field as other formats.
The component layout (interleaved RIRI... vs planar RRR...III...) follows
from the interleave-type value.

# Appendix: Future Extension Point Uses

The GstVideoInfo extension mechanism is not limited to GENERIC format support.
A natural candidate for future adoption is GstVideoInfoDmaDrm, which currently
exists as a separate struct wrapping GstVideoInfo plus DRM format info. The
DRM-specific data could instead be carried as a field in the extension object,
with the existing GstVideoInfoDmaDrm API maintained as wrappers for backwards
compatibility and deprecated over time.
