# Stereoscopic & Multiview Video Handling

There are two cases to handle:

 - Encoded video output from a demuxer to parser / decoder or from encoders
   into a muxer.

 - Raw video buffers

The design below is somewhat based on the proposals from
[bug 611157](https://bugzilla.gnome.org/show_bug.cgi?id=611157)

Multiview is used as a generic term to refer to handling both
stereo content (left and right eye only) as well as extensions for videos
containing multiple independent viewpoints.

## Encoded Signalling

This is regarding the signalling in caps and buffers from demuxers to
parsers (sometimes) or out from encoders.

For backward compatibility with existing codecs many transports of
stereoscopic 3D content use normal 2D video with 2 views packed spatially
in some way, and put extra new descriptions in the container/mux.

Info in the demuxer seems to apply to stereo encodings only. For all
MVC methods I know, the multiview encoding is in the video bitstream itself
and therefore already available to decoders. Only stereo systems have been retro-fitted
into the demuxer.

Also, sometimes extension descriptions are in the codec (e.g. H.264 SEI FPA packets)
and it would be useful to be able to put the info onto caps and buffers from the
parser without decoding.

To handle both cases, we need to be able to output the required details on
encoded video for decoders to apply onto the raw video buffers they decode.

*If there ever is a need to transport multiview info for encoded data the
same system below for raw video or some variation should work*

### Encoded Video: Properties that need to be encoded into caps

1. multiview-mode (called "Channel Layout" in bug 611157)
    * Whether a stream is mono, for a single eye, stereo, mixed-mono-stereo
      (switches between mono and stereo - mp4 can do this)
    * Uses a buffer flag to mark individual buffers as mono or "not mono"
      (single|stereo|multiview) for mixed scenarios. The alternative (not
      proposed) is for the demuxer to switch caps for each mono to not-mono
      change, and not used a 'mixed' caps variant at all.
    * _single_ refers to a stream of buffers that only contain 1 view.
      It is different from mono in that the stream is a marked left or right
      eye stream for later combining in a mixer or when displaying.
    * _multiple_ marks a stream with multiple independent views encoded.
      It is included in this list for completeness. As noted above, there's
      currently no scenario that requires marking encoded buffers as MVC.

2. Frame-packing arrangements / view sequence orderings
    * Possible frame packings: side-by-side, side-by-side-quincunx,
      column-interleaved, row-interleaved, top-bottom, checker-board
    * bug 611157 - sreerenj added side-by-side-full and top-bottom-full but
      I think that's covered by suitably adjusting pixel-aspect-ratio. If
      not, they can be added later.
    * _top-bottom_, _side-by-side_, _column-interleaved_, _row-interleaved_ are as the names suggest.
    * _checker-board_, samples are left/right pixels in a chess grid +-+-+-/-+-+-+
    * _side-by-side-quincunx_. Side By Side packing, but quincunx sampling -
      1 pixel offset of each eye needs to be accounted when upscaling or displaying
    * there may be other packings (future expansion)
    * Possible view sequence orderings: frame-by-frame, frame-primary-secondary-tracks, sequential-row-interleaved
    * _frame-by-frame_, each buffer is left, then right view etc
    * _frame-primary-secondary-tracks_ - the file has 2 video tracks (primary and secondary), one is left eye, one is right.
      Demuxer info indicates which one is which.
      Handling this means marking each stream as all-left and all-right views, decoding separately, and combining automatically (inserting a mixer/combiner in playbin)
      -> *Leave this for future expansion*
    * _sequential-row-interleaved_ Mentioned by sreerenj in bug patches, I can't find a mention of such a thing. Maybe it's in MPEG-2
      -> *Leave this for future expansion / deletion*

3. view encoding order
    * Describes how to decide which piece of each frame corresponds to left or right eye
    * Possible orderings left, right, left-then-right, right-then-left
    - Need to figure out how we find the correct frame in the demuxer to start decoding when seeking in frame-sequential streams
    - Need a buffer flag for marking the first buffer of a group.

4. "Frame layout flags"
    * flags for view specific interpretation
    * horizontal-flip-left, horizontal-flip-right, vertical-flip-left, vertical-flip-right
      Indicates that one or more views has been encoded in a flipped orientation, usually due to camera with mirror or displays with mirrors.
    * This should be an actual flags field. Registered GLib flags types aren't generally well supported in our caps - the type might not be loaded/registered yet when parsing a caps string, so they can't be used in caps templates in the registry.
    * It might be better just to use a hex value / integer

## Buffer representation for raw video

 - Transported as normal video buffers with extra metadata
 - The caps define the overall buffer width/height, with helper functions to
   extract the individual views for packed formats
 - pixel-aspect-ratio adjusted if needed to double the overall width/height
 - video sinks that don't know about multiview extensions yet will show the
   packed view as-is. For frame-sequence outputs, things might look weird, but
   just adding multiview-mode to the sink caps can disallow those transports.
 - _row-interleaved_ packing is actually just side-by-side memory layout with
   half frame width, twice the height, so can be handled by adjusting the
   overall caps and strides
 - Other exotic layouts need new pixel formats defined (checker-board,
   column-interleaved, side-by-side-quincunx)
 - _Frame-by-frame_ - one view per buffer, but with alternating metas marking
   which buffer is which left/right/other view and using a new buffer flag as
   described above to mark the start of a group of corresponding frames.
 - New video caps addition as for encoded buffers

### Proposed Caps fields

Combining the requirements above and collapsing the combinations into mnemonics:

* multiview-mode =
   mono | left | right | sbs | sbs-quin | col | row | topbot | checkers |
   frame-by-frame | mixed-sbs | mixed-sbs-quin | mixed-col | mixed-row |
   mixed-topbot | mixed-checkers | mixed-frame-by-frame | multiview-frames mixed-multiview-frames

* multiview-flags =
    + 0x0000 none
    + 0x0001 right-view-first
    + 0x0002 left-h-flipped
    + 0x0004 left-v-flipped
    + 0x0008 right-h-flipped
    + 0x0010 right-v-flipped

### Proposed new buffer flags

Add two new `GST_VIDEO_BUFFER_*` flags in video-frame.h and make it clear that
those flags can apply to encoded video buffers too. wtay says that's currently
the case anyway, but the documentation should say it.

 - **`GST_VIDEO_BUFFER_FLAG_MULTIPLE_VIEW`** - Marks a buffer as representing
   non-mono content, although it may be a single (left or right) eye view.

 - **`GST_VIDEO_BUFFER_FLAG_FIRST_IN_BUNDLE`** - for frame-sequential methods of
   transport, mark the "first" of a left/right/other group of frames

### A new GstMultiviewMeta

This provides a place to describe all provided views in a buffer / stream,
and through Meta negotiation to inform decoders about which views to decode if
not all are wanted.

* Logical labels/names and mapping to `GstVideoMeta` numbers
* Standard view labels LEFT/RIGHT, and non-standard ones (strings)

```c
        GST_VIDEO_MULTIVIEW_VIEW_LEFT = 1
        GST_VIDEO_MULTIVIEW_VIEW_RIGHT = 2

        struct GstVideoMultiviewViewInfo {
            guint view_label;
            guint meta_id; // id of the GstVideoMeta for this view

            padding;
        }

        struct GstVideoMultiviewMeta {
            guint n_views;
            GstVideoMultiviewViewInfo *view_info;
        }
```

The meta is optional, and probably only useful later for MVC


## Outputting stereo content

The initial implementation for output will be stereo content in glimagesink

### Output Considerations with OpenGL

 - If we have support for stereo GL buffer formats, we can output separate
   left/right eye images and let the hardware take care of display.

 - Otherwise, glimagesink needs to render one window with left/right in a
   suitable frame packing and that will only show correctly in fullscreen on a
   device set for the right 3D packing -> requires app intervention to set the
   video mode.

 - Which could be done manually on the TV, or with HDMI 1.4 by setting the
   right video mode for the screen to inform the TV or third option, we support
   rendering to two separate overlay areas on the screen - one for left eye,
   one for right which can be supported using the 'splitter' element and two
   output sinks or, better, add a 2nd window overlay for split stereo output

 - Intel hardware doesn't do stereo GL buffers - only nvidia and AMD, so
   initial implementation won't include that

## Other elements for handling multiview content

 - videooverlay interface extensions
   - __Q__: Should this be a new interface?
   - Element message to communicate the presence of stereoscopic information to the app
   - App needs to be able to override the input interpretation - ie, set multiview-mode and multiview-flags
     - Most videos I've seen are side-by-side or top-bottom with no frame-packing metadata
   - New API for the app to set rendering options for stereo/multiview content
   - This might be best implemented as a **multiview GstContext**, so that
     the pipeline can share app preferences for content interpretation and downmixing
     to mono for output, or in the sink and have those down as far upstream/downstream as possible.

 - Converter element
   - convert different view layouts
   - Render to anaglyphs of different types (magenta/green, red/blue, etc) and output as mono

 - Mixer element
   - take 2 video streams and output as stereo
   - later take n video streams
   - share code with the converter, it just takes input from n pads instead of one.

 - Splitter element
  - Output one pad per view

### Implementing MVC handling in decoders / parsers (and encoders)

Things to do to implement MVC handling

1. Parsing SEI in h264parse and setting caps (patches available in
   bugzilla for parsing, see below)
2. Integrate gstreamer-vaapi MVC support with this proposal
3. Help with [libav MVC implementation](https://wiki.libav.org/Blueprint/MVC)
4. generating SEI in H.264 encoder
5. Support for MPEG2 MVC extensions

## Relevant bugs

 - [bug 685215](https://bugzilla.gnome.org/show_bug.cgi?id=685215) - codecparser h264: Add initial MVC parser
 - [bug 696135](https://bugzilla.gnome.org/show_bug.cgi?id=696135) - h264parse: Add mvc stream parsing support
 - [bug 732267](https://bugzilla.gnome.org/show_bug.cgi?id=732267) - h264parse: extract base stream from MVC or SVC encoded streams

## Other Information

[Matroska 3D support notes](http://www.matroska.org/technical/specs/notes.html#3D)

## Open Questions

### Background

### Representation for GstGL

When uploading raw video frames to GL textures, the goal is to implement:

Split packed frames into separate GL textures when uploading, and
attach multiple `GstGLMemory` to the `GstBuffer`. The multiview-mode and
multiview-flags fields in the caps should change to reflect the conversion
from one incoming `GstMemory` to multiple `GstGLMemory`, and change the
width/height in the output info as needed.

This is (currently) targetted as 2 render passes - upload as normal
to a single stereo-packed RGBA texture, and then unpack into 2
smaller textures, output with `GST_VIDEO_MULTIVIEW_MODE_SEPARATED`, as
2 `GstGLMemory` attached to one buffer. We can optimise the upload later
to go directly to 2 textures for common input formats.

Separat output textures have a few advantages:

 - Filter elements can more easily apply filters in several passes to each
   texture without fundamental changes to our filters to avoid mixing pixels
   from separate views.

 - Centralises the sampling of input video frame packings in the upload code,
   which makes adding new packings in the future easier.

 - Sampling multiple textures to generate various output frame-packings
   for display is conceptually simpler than converting from any input packing
   to any output packing.

 - In implementations that support quad buffers, having separate textures
   makes it trivial to do `GL_LEFT`/`GL_RIGHT` output

For either option, we'll need new glsink output API to pass more
information to applications about multiple views for the draw signal/callback.

I don't know if it's desirable to support *both* methods of representing
views. If so, that should be signalled in the caps too. That could be a
new multiview-mode for passing views in separate `GstMemory` objects
attached to a `GstBuffer`, which would not be GL specific.

### Overriding frame packing interpretation

Most sample videos available are frame packed, with no metadata
to say so. How should we override that interpretation?

 - Simple answer: Use capssetter + new properties on playbin to
   override the multiview fields. *Basically implemented in playbin, using*
   *a pad probe. Needs more work for completeness*

### Adding extra GstVideoMeta to buffers

There should be one `GstVideoMeta` for the entire video frame in packed
layouts, and one `GstVideoMeta` per `GstGLMemory` when views are attached
to a `GstBuffer` separately. This should be done by the buffer pool,
which knows from the caps.

### videooverlay interface extensions

GstVideoOverlay needs:

- A way to announce the presence of multiview content when it is
  detected/signalled in a stream.
- A way to tell applications which output methods are supported/available
- A way to tell the sink which output method it should use
- Possibly a way to tell the sink to override the input frame
  interpretation / caps - depends on the answer to the question
  above about how to model overriding input interpretation.

### What's implemented

- Caps handling
- gst-plugins-base libsgstvideo pieces
- playbin caps overriding
- conversion elements - glstereomix, gl3dconvert (needs a rename),
  glstereosplit.

### Possible future enhancements

- Make GLupload split to separate textures at upload time?
  - Needs new API to extract multiple textures from the upload. Currently only outputs 1 result RGBA texture.
- Make GLdownload able to take 2 input textures, pack them and colorconvert / download as needed.
  - current done by packing then downloading which isn't OK overhead for RGBA download
- Think about how we integrate GLstereo - do we need to do anything special,
  or can the app just render to stereo/quad buffers if they're available?
