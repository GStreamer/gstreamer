---
title: Media Types and Properties
...

# Media Types and Properties

There is a very large set of possible media types that may be used to pass
data between elements. Indeed, each new element that is defined may use
a new data format (though unless at least one other element recognises
that format, it will be useless since nothing will be
able to link with it).

In order for media types to be useful, and for systems like autopluggers to
work, it is necessary that all elements agree on the media type definitions,
and which properties are required for each media type. The GStreamer framework
itself simply provides the ability to define media types and parameters, but
does not fix the meaning of media types and parameters, and does not enforce
standards on the creation of new media types. This is a matter for a policy to
decide, not technical systems to enforce.

For now, the policy is simple:

  - Do not create a new media type if you could use one which already exists.

  - If creating a new media type, discuss it first with the other GStreamer
    developers, on at least one of: IRC, mailing lists.

  - Try to ensure that the name for a new format does not
    conflict with anything else created already, and is not a more
    generalised name than it should be. For example: "audio/compressed"
    would be too generalised a name to represent audio data compressed
    with an mp3 codec. Instead "audio/mp3" might be an appropriate name,
    or "audio/compressed" could exist and have a property indicating the
    type of compression used.

  - Ensure that, when you do create a new media type, you specify it clearly,
    and get it added to the list of known media types so that other developers
    can use the media type correctly when writing their elements.

## Building a Simple Format for Testing

If you need a new format that has not yet been defined in our [List of
Defined Types](#list-of-defined-types), you will want to have some
general guidelines on media type naming, properties and such. A media
type would ideally be equivalent to the Mime-type defined by IANA; else,
it should be in the form type/x-name, where type is the sort of data
this media type handles (audio, video, ...) and name should be something
specific for this specific type. Audio and video media types should try
to support the general audio/video properties (see the list), and can
use their own properties, too. To get an idea of what properties we
think are useful, see (again) the list.

Take your time to find the right set of properties for your type. There
is no reason to hurry. Also, experimenting with this is generally a good
idea. Experience learns that theoretically thought-out types are good,
but they still need practical use to assure that they serve their needs.
Make sure that your property names do not clash with similar properties
used in other types. If they match, make sure they mean the same thing;
properties with different types but the same names are *not* allowed.

## Typefind Functions and Autoplugging

With only *defining* the types, we're not yet there. In order for a
random data file to be recognized and played back as such, we need a way
of recognizing their type out of the blue. For this purpose,
“typefinding” was introduced. Typefinding is the process of detecting
the type of a data stream. Typefinding consists of two separate parts:
first, there's an unlimited number of functions that we call *typefind
functions*, which are each able to recognize one or more types from an
input stream. Then, secondly, there's a small engine which registers and
calls each of those functions. This is the typefind core. On top of this
typefind core, you would normally write an autoplugger, which is able to
use this type detection system to dynamically build a pipeline around an
input stream. Here, we will focus only on typefind functions.

A typefind function usually lives in
`gst-plugins-base/gst/typefind/gsttypefindfunctions.c`, unless there's a
good reason (like library dependencies) to put it elsewhere. The reason
for this centralization is to reduce the number of plugins that need to
be loaded in order to detect a stream's type. Below is an example that
will recognize AVI files, which start with a “RIFF” tag, then the size
of the file and then an “AVI” tag:

``` c
static GstStaticCaps avi_caps = GST_STATIC_CAPS ("video/x-msvideo");
#define AVI_CAPS gst_static_caps_get(&avi_caps)
static void
gst_avi_typefind_function (GstTypeFind *tf,
              gpointer     pointer)
{
  guint8 *data = gst_type_find_peek (tf, 0, 12);

  if (data &&
      GUINT32_FROM_LE (&((guint32 *) data)[0]) == GST_MAKE_FOURCC ('R','I','F','F') &&
      GUINT32_FROM_LE (&((guint32 *) data)[2]) == GST_MAKE_FOURCC ('A','V','I',' ')) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM, AVI_CAPS);
  }
}

GST_TYPE_FIND_REGISTER_DEFINE(avi, "video/x-msvideo", GST_RANK_PRIMARY,
    gst_avi_typefind_function, "avi", AVI_CAPS, NULL, NULL);

static gboolean
plugin_init (GstPlugin *plugin)
{
  return GST_TYPEFIND_REGISTER(avi, plugin);
}

```

Note that `gst-plugins/gst/typefind/gsttypefindfunctions.c` has some
simplification macros to decrease the amount of code. Make good use of
those if you want to submit typefinding patches with new typefind
functions.

Autoplugging has been discussed in great detail in the Application
Development Manual.

## List of Defined Types

Below is a list of all the defined types in GStreamer. They are split up
in separate tables for audio, video, container, subtitle and other
types, for the sake of readability. Below each table might follow a list
of notes that apply to that table. In the definition of each type, we
try to follow the types and rules as defined by
[IANA](http://www.iana.org/assignments/media-types) for as far as
possible.

Jump directly to a specific table:

  - [Table of Audio Types](#table-of-audio-types)

  - [Table of Video Types](#table-of-video-types)

  - [Table of Container Types](#table-of-container-types)

  - [Table of Subtitle Types](#table-of-subtitle-types)

  - [Table of Other Types](#table-of-other-types)

Note that many of the properties are not *required*, but rather
*optional* properties. This means that most of these properties can be
extracted from the container header, but that - in case the container
header does not provide these - they can also be extracted by parsing
the stream header or the stream content. The policy is that your element
should provide the data that it knows about by only parsing its own
content, not another element's content. Example: the AVI header provides
samplerate of the contained audio stream in the header. MPEG system
streams don't. This means that an AVI stream demuxer would provide
samplerate as a property for MPEG audio streams, whereas an MPEG demuxer
would not. A decoder needing this data would require a stream parser in
between to extract this from the header or calculate it from the
stream.

### Table of Audio Types
<table>
<caption>Table of Audio Types</caption>
<colgroup>
<col width="14%" />
<col width="85%" />
</colgroup>
<thead>
<tr class="header">
<th>Media Type</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><em>All audio types.</em></td>
</tr>
<tr class="even">
<td>audio/*</td>
<td><em>All audio types</em></td>
</tr>
<tr class="odd">
<td>channels</td>
<td>integer</td>
</tr>
<tr class="even">
<td>channel-mask</td>
<td>bitmask</td>
</tr>
<tr class="odd">
<td>format</td>
<td>string</td>
</tr>
<tr class="even">
<td>layout</td>
<td>string</td>
</tr>
<tr class="odd">
<td><em>All raw audio types.</em></td>
</tr>
<tr class="even">
<td>audio/x-raw</td>
<td>Unstructured and uncompressed raw audio data.</td>
</tr>
<tr class="odd">
<td><em>All encoded audio types.</em></td>
</tr>
<tr class="even">
<td>audio/x-ac3</td>
<td>AC-3 or A52 audio streams.</td>
</tr>
<tr class="odd">
<td>audio/x-adpcm</td>
<td>ADPCM Audio streams.</td>
</tr>
<tr class="even">
<td>block_align</td>
<td>integer</td>
</tr>
<tr class="odd">
<td>audio/x-cinepak</td>
<td>Audio as provided in a Cinepak (Quicktime) stream.</td>
</tr>
<tr class="even">
<td>audio/x-dv</td>
<td>Audio as provided in a Digital Video stream.</td>
</tr>
<tr class="odd">
<td>audio/x-flac</td>
<td>Free Lossless Audio codec (FLAC).</td>
</tr>
<tr class="even">
<td>audio/x-gsm</td>
<td>Data encoded by the GSM codec.</td>
</tr>
<tr class="odd">
<td>audio/x-alaw</td>
<td>A-Law Audio.</td>
</tr>
<tr class="even">
<td>audio/x-mulaw</td>
<td>Mu-Law Audio.</td>
</tr>
<tr class="odd">
<td>audio/x-mace</td>
<td>MACE Audio (used in Quicktime).</td>
</tr>
<tr class="even">
<td>audio/mpeg</td>
<td>Audio data compressed using the MPEG audio encoding scheme.</td>
</tr>
<tr class="odd">
<td>framed</td>
<td>boolean</td>
</tr>
<tr class="even">
<td>layer</td>
<td>integer</td>
</tr>
<tr class="odd">
<td>bitrate</td>
<td>integer</td>
</tr>
<tr class="even">
<td>audio/x-qdm2</td>
<td>Data encoded by the QDM version 2 codec.</td>
</tr>
<tr class="odd">
<td>audio/x-pn-realaudio</td>
<td>Realmedia Audio data.</td>
</tr>
<tr class="even">
<td>audio/x-speex</td>
<td>Data encoded by the Speex audio codec</td>
</tr>
<tr class="odd">
<td>audio/x-vorbis</td>
<td>Vorbis audio data</td>
</tr>
<tr class="even">
<td>audio/x-wma</td>
<td>Windows Media Audio</td>
</tr>
<tr class="odd">
<td>audio/x-paris</td>
<td>Ensoniq PARIS audio</td>
</tr>
<tr class="even">
<td>audio/x-svx</td>
<td>Amiga IFF / SVX8 / SV16 audio</td>
</tr>
<tr class="odd">
<td>audio/x-nist</td>
<td>Sphere NIST audio</td>
</tr>
<tr class="even">
<td>audio/x-voc</td>
<td>Sound Blaster VOC audio</td>
</tr>
<tr class="odd">
<td>audio/x-ircam</td>
<td>Berkeley/IRCAM/CARL audio</td>
</tr>
<tr class="even">
<td>audio/x-w64</td>
<td>Sonic Foundry's 64 bit RIFF/WAV</td>
</tr>
</tbody>
</table>

### Table of Video Types
<table>
<caption>Table of Video Types</caption>
<colgroup>
<col width="14%" />
<col width="85%" />
</colgroup>
<thead>
<tr class="header">
<th>Media Type</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><em>All video types.</em></td>
</tr>
<tr class="even">
<td>video/*</td>
<td><em>All video types</em></td>
</tr>
<tr class="odd">
<td>height</td>
<td>integer</td>
</tr>
<tr class="even">
<td>framerate</td>
<td>fraction</td>
</tr>
<tr class="odd">
<td>max-framerate</td>
<td>fraction</td>
</tr>
<tr class="even">
<td>views</td>
<td>integer</td>
</tr>
<tr class="odd">
<td>interlace-mode</td>
<td>string</td>
</tr>
<tr class="even">
<td>chroma-site</td>
<td>string</td>
</tr>
<tr class="odd">
<td>colorimetry</td>
<td>string</td>
</tr>
<tr class="even">
<td>pixel-aspect-ratio</td>
<td>fraction</td>
</tr>
<tr class="odd">
<td>format</td>
<td>string</td>
</tr>
<tr class="even">
<td><em>All raw video types.</em></td>
</tr>
<tr class="odd">
<td>video/x-raw</td>
<td>Unstructured and uncompressed raw video data.</td>
</tr>
<tr class="even">
<td><em>All encoded video types.</em></td>
</tr>
<tr class="odd">
<td>video/x-3ivx</td>
<td>3ivx video.</td>
</tr>
<tr class="even">
<td>video/x-divx</td>
<td>DivX video.</td>
</tr>
<tr class="odd">
<td>video/x-dv</td>
<td>Digital Video.</td>
</tr>
<tr class="even">
<td>video/x-ffv</td>
<td>FFMpeg video.</td>
</tr>
<tr class="odd">
<td>video/x-h263</td>
<td>H-263 video.</td>
</tr>
<tr class="even">
<td>h263version</td>
<td>string</td>
</tr>
<tr class="odd">
<td>video/x-h264</td>
<td>H-264 video.</td>
</tr>
<tr class="even">
<td>video/x-huffyuv</td>
<td>Huffyuv video.</td>
</tr>
<tr class="odd">
<td>video/x-indeo</td>
<td>Indeo video.</td>
</tr>
<tr class="even">
<td>video/x-intel-h263</td>
<td>H-263 video.</td>
</tr>
<tr class="odd">
<td>video/x-jpeg</td>
<td>Motion-JPEG video.</td>
</tr>
<tr class="even">
<td>video/mpeg</td>
<td>MPEG video.</td>
</tr>
<tr class="odd">
<td>systemstream</td>
<td>boolean</td>
</tr>
<tr class="even">
<td>video/x-msmpeg</td>
<td>Microsoft MPEG-4 video deviations.</td>
</tr>
<tr class="odd">
<td>video/x-msvideocodec</td>
<td>Microsoft Video 1 (oldish codec).</td>
</tr>
<tr class="even">
<td>video/x-pn-realvideo</td>
<td>Realmedia video.</td>
</tr>
<tr class="odd">
<td>video/x-rle</td>
<td>RLE animation format.</td>
</tr>
<tr class="even">
<td>depth</td>
<td>integer</td>
</tr>
<tr class="odd">
<td>palette_data</td>
<td>GstBuffer</td>
</tr>
<tr class="even">
<td>video/x-svq</td>
<td>Sorensen Video.</td>
</tr>
<tr class="odd">
<td>video/x-tarkin</td>
<td>Tarkin video.</td>
</tr>
<tr class="even">
<td>video/x-theora</td>
<td>Theora video.</td>
</tr>
<tr class="odd">
<td>video/x-vp3</td>
<td>VP-3 video.</td>
</tr>
<tr class="even">
<td>video/x-wmv</td>
<td>Windows Media Video</td>
</tr>
<tr class="odd">
<td>video/x-xvid</td>
<td>XviD video.</td>
</tr>
<tr class="even">
<td><em>All image types.</em></td>
</tr>
<tr class="odd">
<td>image/gif</td>
<td>Graphics Interchange Format.</td>
</tr>
<tr class="even">
<td>image/jpeg</td>
<td>Joint Picture Expert Group Image.</td>
</tr>
<tr class="odd">
<td>image/png</td>
<td>Portable Network Graphics Image.</td>
</tr>
<tr class="even">
<td>image/tiff</td>
<td>Tagged Image File Format.</td>
</tr>
</tbody>
</table>

### Table of Container Types
<table>
<caption>Table of Container Types</caption>
<colgroup>
<col width="14%" />
<col width="85%" />
</colgroup>
<thead>
<tr class="header">
<th>Media Type</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>video/x-ms-asf</td>
<td>Advanced Streaming Format (ASF).</td>
</tr>
<tr class="even">
<td>video/x-msvideo</td>
<td>AVI.</td>
</tr>
<tr class="odd">
<td>video/x-dv</td>
<td>Digital Video.</td>
</tr>
<tr class="even">
<td>video/x-matroska</td>
<td>Matroska.</td>
</tr>
<tr class="odd">
<td>video/mpeg</td>
<td>Motion Pictures Expert Group System Stream.</td>
</tr>
<tr class="even">
<td>application/ogg</td>
<td>Ogg.</td>
</tr>
<tr class="odd">
<td>video/quicktime</td>
<td>Quicktime.</td>
</tr>
<tr class="even">
<td>application/vnd.rn-realmedia</td>
<td>RealMedia.</td>
</tr>
<tr class="odd">
<td>audio/x-wav</td>
<td>WAV.</td>
</tr>
</tbody>
</table>

### Table of Subtitle Types
<table>
<caption>Table of Subtitle Types</caption>
<colgroup>
<col width="14%" />
<col width="85%" />
</colgroup>
<thead>
<tr class="header">
<th>Media Type</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td></td>
<td></td>
</tr>
</tbody>
</table>

### Table of Other Types
<table>
<caption>Table of Other Types</caption>
<colgroup>
<col width="14%" />
<col width="85%" />
</colgroup>
<thead>
<tr class="header">
<th>Media Type</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td></td>
<td></td>
</tr>
</tbody>
</table>
