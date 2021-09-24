# Metadata

This draft recaps the current metadata handling in GStreamer and
proposes some additions.

## Supported Metadata standards

The paragraphs below list supported native metadata standards sorted by
type and then in alphabetical order. Some standards have been extended
to support additional metadata. GStreamer already supports all of those
to some extend. This is showns in the table below as either `[--]`,
`[r-]`, `[-w]` or `[rw]` depending on read/write support (08.Feb.2010).

### Audio
- mp3
  * ID3v2: `[rw]`
     * http://www.id3.org/Developer_Information
  * ID3v1: `[rw]`
    * http://www.id3.org/ID3v1
  * XMP: `[--]` (inside ID3v2 PRIV tag of owner XMP)
    * http://www.adobe.com/devnet/xmp/
- ogg/vorbis
  * vorbiscomment: `[rw]`
    * http://www.xiph.org/vorbis/doc/v-comment.html
    * http://wiki.xiph.org/VorbisComment
- wav
  * LIST/INFO chunk: `[rw]`
    * http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/RIFF.html#Info
    * http://www.kk.iij4u.or.jp/~kondo/wave/mpidata.txt
  * XMP: `[--]`
    * http://www.adobe.com/devnet/xmp/

### Video
- 3gp
  * {moov,trak}.udta:  `[rw]`
     * http://www.3gpp.org/ftp/Specs/html-info/26244.htm
  * ID3V2: `[--]`
     * http://www.3gpp.org/ftp/Specs/html-info/26244.htm
     * http://www.mp4ra.org/specs.html#id3v2
- avi
  * LIST/INFO chunk: `[rw]`
    * http://www.sno.phy.queensu.ca/~phil/exiftool/TagNames/RIFF.html#Info
    * http://www.kk.iij4u.or.jp/~kondo/wave/mpidata.txt
  * XMP: `[--]` (inside "_PMX" chunk)
    * http://www.adobe.com/devnet/xmp/
- asf
  * ??:
  * XMP: `[--]`
    * http://www.adobe.com/devnet/xmp/
- flv `[--]`
  * XMP: (inside onXMPData script data tag)
    * http://www.adobe.com/devnet/xmp/
- mkv
  * tags: `[rw]`
    * http://www.matroska.org/technical/specs/tagging/index.html
- mov
  * XMP: `[--]` (inside moov.udta.XMP_ box)
    * http://www.adobe.com/devnet/xmp/
- mp4
  * {moov,trak}.udta: `[rw]`
    * http://standards.iso.org/ittf/PubliclyAvailableStandards/c051533_ISO_IEC_14496-12_2008.zip
  * moov.udta.meta.ilst: `[rw]`
    * http://atomicparsley.sourceforge.net/
    * http://atomicparsley.sourceforge.net/mpeg-4files.html
  * ID3v2: `[--]`
    * http://www.mp4ra.org/specs.html#id3v2
  * XMP: `[--]` (inside UUID box)
    * http://www.adobe.com/devnet/xmp/
- mxf
  * ??

### Images
- gif
  * XMP: `[--]`
    * http://www.adobe.com/devnet/xmp/
- jpg
  * jif: `[rw]` (only comments)
  * EXIF: `[rw]` (via metadata plugin)
    * http://www.exif.org/specifications.html
  * IPTC: `[rw]` (via metadata plugin)
    * http://www.iptc.org/IPTC4XMP/
  * XMP: `[rw]` (via metadata plugin)
    * http://www.adobe.com/devnet/xmp/
- png
  * XMP: `[--]`
    * http://www.adobe.com/devnet/xmp/

### further Links:

http://age.hobba.nl/audio/tag_frame_reference.html
http://wiki.creativecommons.org/Tracker_CC_Indexing

## Current Metadata handling

When reading files, demuxers or parsers extract the metadata. It will be
sent a `GST_EVENT_TAG` to downstream elements. When a sink element
receives a tag event, it will post a `GST_MESSAGE_TAG` message on the
bus with the contents of the tag event.

Elements receiving `GST_EVENT_TAG` events can mangle them, mux them into
the buffers they send or just pass them through. Usually is muxers that
will format the tag data into the form required by the format they mux.
Such elements would also implement the `GstTagSetter` interface to receive
tags from the application.

```
 +----------+
 | demux    |
sink       src --> GstEvent(tag) over GstPad to downstream element
 +----------+

           method call over GstTagSetter interface from application
                                                          |
                                                          v
                                                    +----------+
                                                    | mux      |
GstEvent(tag) over GstPad from upstream element --> sink       src
                                                    +----------+
```

The data used in all those interfaces is `GstTagList`. It is based on a
`GstStructure` which is like a hash table with differently typed entries.
The key is always a string/GQuark. Many keys are predefined in GStreamer
core. More keys are defined in gst-plugins-base/gst-libs/gst/tag/tag.h.
If elements and applications use predefined types, it is possible to
transcode a file from one format into another while preserving all known
and mapped metadata.

## Issues

### Unknown/Unmapped metadata

Right now GStreamer can lose metadata when transcoding and/or remuxing
content. This can happens as we don’t map all metadata fields to generic
ones.

We should probably also add the whole metadata blob to the `GstTagList`.
We would need a `GST_TAG_SYSTEM_xxx` define (e.g.
`GST_TAG_SYSTEM_ID3V2`) for each standard. The content is not printable
and should be treated as binary if not known. The tag is not mergeable -
call `gst_tag_register()` with `GstTagMergeFunc=NULL`. Also the tag data
is only useful for upstream elements, not for the application.

A muxer would first scan a taglist for known system tags. Unknown tags
are ignored as already. It would first populate its own metadata store
with the entries from the system tag and the update the entries with the
data in normal tags.

Below is an initial list of tag systems: `ID3V1` - `GST_TAG_SYSTEM_ID3V1`
`ID3V2` - `GST_TAG_SYSTEM_ID3V2` `RIFF_INFO` -
`GST_TAG_SYSTEM_RIFF_INFO` XMP - `GST_TAG_SYSTEM_XMP`

We would basically need this for each container format.

See also <https://bugzilla.gnome.org/show_bug.cgi?id=345352>

### Lost metadata

A case slighly different from the previous is that when an application
sets a `GstTagList` on a pipeline. Right elements consuming tags do not
report which tags have been consumed. Especially when using elements
that make metadata persistent, we have no means of knowing which of the
tags made it into the target stream and which were not serialized.
Ideally the application would like to know which kind of metadata is
accepted by a pipleine to reflect that in the UI.

Although it is in practise so that elements implementing `GstTagSetter`
are the ones that serialize, this does not have to be so. Otherwise we
could add a means to that interface, where elements add the tags they
have serialized. The application could build one list from all the tag
messages and then query all the serialized tags from tag-setters. The
delta tells what has not been serialized.

A different approach would be to query the list of supported tags in
advance. This could be a query (`GST_QUERY_TAG_SUPPORT`). The query
result could be a list of elements and their tags. As a convenience we
could flatten the list of tags for the top-level element (if the query
was sent to a bin) and add that.

### Tags are per Element

In many cases we want tags per stream. Even metadata standards like
mp4/3gp metadata supports that. Right now `GST_MESSAGE_SRC(tags)` is the
element. We tried changing that to the pad, but that broke applications.
Also we miss the symmetric functionality in `GstTagSetter`. This interface
is usually implemented by
elements.

### Open bugs

<https://bugzilla.gnome.org/buglist.cgi?query_format=advanced;short_desc=tag;bug_status=UNCONFIRMED;bug_status=NEW;bug_status=ASSIGNED;bug_status=REOPENED;bug_status=NEEDINFO;short_desc_type=allwordssubstr;product=GStreamer>

