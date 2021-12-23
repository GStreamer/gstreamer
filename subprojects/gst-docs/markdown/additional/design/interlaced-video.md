# Interlaced Video

Video buffers have a number of states identifiable through a combination
of caps and buffer flags.

Possible states:
- Progressive
- Interlaced
 - Plain
  - One field
  - Two fields
  - Three fields - this should be a progressive buffer with a repeated 'first'
    field that can be used for telecine pulldown
 - Telecine
   - One field
   - Two fields
    - Progressive
    - Interlaced (a.k.a. 'mixed'; the fields are from different frames)
   - Three fields - this should be a progressive buffer with a repeated 'first'
     field that can be used for telecine pulldown

Note: It can be seen that the difference between the plain interlaced
and telecine states is that in the latter, buffers containing
two fields may be progressive.

Tools for identification:
 - `GstVideoInfo`
  - `GstVideoInterlaceMode` - enum `GST_VIDEO_INTERLACE_MODE_...`
   - PROGRESSIVE
   - INTERLEAVED
   - MIXED
 - Buffers flags - `GST_VIDEO_BUFFER_FLAG_...`
   - TFF
   - RFF
   - ONEFIELD
   - INTERLACED

## Identification of Buffer States

Note that flags are not necessarily interpreted in the same way for all
different states nor are they necessarily required nor make sense in all
cases.

### Progressive

If the interlace mode in the video info corresponding to a buffer is
**"progressive"**, then the buffer is progressive.

### Plain Interlaced

If the video info interlace mode is **"interleaved"**, then the buffer is
plain interlaced.

`GST_VIDEO_BUFFER_FLAG_TFF` indicates whether the top or bottom field
is to be displayed first. The timestamp on the buffer corresponds to the
first field.

`GST_VIDEO_BUFFER_FLAG_RFF` indicates that the first field (indicated
by the TFF flag) should be repeated. This is generally only used for
telecine purposes but as the telecine state was added long after the
interlaced state was added and defined, this flag remains valid for
plain interlaced buffers.

`GST_VIDEO_BUFFER_FLAG_ONEFIELD` means that only the field indicated
through the TFF flag is to be used. The other field should be ignored.

### Telecine

If video info interlace mode is **"mixed"** then the buffers are in some
form of telecine state.

The `TFF` and `ONEFIELD` flags have the same semantics as for the plain
interlaced state.

`GST_VIDEO_BUFFER_FLAG_RFF` in the telecine state indicates that the
buffer contains only repeated fields that are present in other buffers
and are as such unneeded. For example, in a sequence of three telecined
frames, we might have:

    AtAb AtBb BtBb

In this situation, we only need the first and third buffers as the
second buffer contains fields present in the first and third.

Note that the following state can have its second buffer identified
using the `ONEFIELD` flag (and `TFF` not set):

    AtAb AtBb BtCb

The telecine state requires one additional flag to be able to identify
progressive buffers.

The presence of the `GST_VIDEO_BUFFER_FLAG_INTERLACED` means that the
buffer is an 'interlaced' or 'mixed' buffer that contains two fields
that, when combined with fields from adjacent buffers, allow
reconstruction of progressive frames. The absence of the flag implies
the buffer containing two fields is a progressive frame.

For example in the following sequence, the third buffer would be mixed
(yes, it is a strange pattern, but it can happen):

    AtAb AtBb BtCb CtDb DtDb

### Alternate fields

Since: 1.16

If the video info interlace mode is **"alternate"**, then each buffer
carries a single field of interlaced video.

`GST_VIDEO_BUFFER_FLAG_TOP_FIELD` and `GST_VIDEO_BUFFER_FLAG_BOTTOM_FIELD`
indicate whether the buffer carries a top or bottom field. The order of
buffers/fields in the stream and the timestamps on the buffers indicate the
temporal order of the fields.

Top and bottom fields are expected to alternate in this mode.

Caps for this interlace mode must also carry a `format:Interlaced` caps feature
(`GST_CAPS_FEATURE_FORMAT_INTERLACED`) to ensure backwards compatibility for
the new mode.

The frame rate in the caps still signals the *frame* rate, so the notional *field*
rate will be twice the frame rate from the caps (see `GST_VIDEO_INFO_FIELD_RATE_N`).

In the same vein the width and height in the caps will indicate *frame*
dimensions not field dimensions, meaning the height of the video data inside
the buffers (1 field) will be half of the height in the caps.
