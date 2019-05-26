# Implementing GstToc support in GStreamer elements

## General info about GstToc structure

`GstToc` introduces a general way to handle chapters within multimedia
formats. `GstToc` can be represented as tree structure with arbitrary
hierarchy. Tree item can be either of two types: sequence or
alternative. Sequence types acts like a part of the media data, for
example audio track in CUE sheet, or part of the movie. Alternative
types acts like some kind of selection to process a different version of
the media content, for example DVD angles. `GstToc` has one constraint on
the tree structure: it does not allow different entry types on the same
level of the hierarchy, i.e. you shouldn’t have editions and chapters
mixed together. Here is an example of right TOC:

```
-------  TOC  -------
         /  \
 edition1    edition2
 |           |
 -chapter1   -chapter3
 -chapter2
```

Here are two editions (alternatives), the first contains two chapters
(sequence type), and the second has only one chapter. And here is an
example of invalid TOC:

```
-------  TOC  -------
         /  \
 edition1    chapter1
 |
 -chapter1
 -chapter2
```

Here you have edition1 and chapter1 mixed on the same level of
hierarchy, and such TOC will be considered broken.

`GstToc` has *entries* field of GList type which consists of children
items. Each item is of type `GstTocEntry`. Also `GstToc` has list of tags
and `GstStructure` called *info*. Please, use `GstToc.info` and
`GstTocEntry.info` fields this way: create a `GstStructure`, put all info
related to your element there and put this structure into the *info*
field under the name of your element. Some fields in the *info*
structure can be used for internal purposes, so you should use it in the
way described above to not to overwrite already existent fields.

Let’s look at `GstTocEntry` a bit closer. One of the most important fields
is *uid*, which must be unique for each item within the TOC. This is
used to identify each item inside TOC, especially when element receives
TOC select event with UID to seek on. Field *subentries* of type GList
contains children items of type `GstTocEntry`. Thus you can achieve
arbitrary hierarchy level. Field *type* can be either
`GST_TOC_ENTRY_TYPE_CHAPTER` or `GST_TOC_ENTRY_TYPE_EDITION` which
corresponds to chapter or edition type of item respectively. Field
*tags* is a list of tags related to the item. And field *info* is
similar to `GstToc.info` described above.

So, a little more about managing `GstToc`. Use `gst_toc_new()` and
`gst_toc_unref()` to create/free it. `GstTocEntry` can be created using
`gst_toc_entry_new()`. While building `GstToc` you can set start and stop
timestamps for each item using `gst_toc_entry_set_start_stop()` and
`loop_type` and `repeat_count` using `gst_toc_entry_set_loop()`. The
best way to process already created `GstToc` is to recursively go through
the *entries* and *subentries* fields.

Applications and plugins should not rely on TOCs having a certain kind
of structure, but should allow for different alternatives. For example,
a simple CUE sheet embedded in a file may be presented as a flat list of
track entries, or could have a top-level edition node (or some other
alternative type entry) with track entries underneath that node; or even
multiple top-level edition nodes (or some other alternative type
entries) each with track entries underneath, in case the source file has
extracted a track listing from different sources).

##  TOC scope: global and current

There are two main consumers for TOC information: applications and
elements in the pipeline that are TOC writers (such as e.g.
matroskamux).

Applications typically want to know the entire table of contents (TOC)
with all entries that can possibly be selected.

TOC writers in the pipeline, however, would not want to write a TOC for
all possible/available streams, but only for the current stream.

When transcoding a title from a DVD, for example, the application would
still want to know the entire TOC, with all titles, the chapters for
each title, and the available angles. When transcoding to a file, we
only want the TOC information that is relevant to the transcoded stream
to be written into the file structure, e.g. the chapters of the title
being transcoded (or possibly only chapters 5-7 if only those have been
selected for playback/ transcoding).

This is why we may need to create two different TOCs for those two types
of consumers.

Elements that extract TOC information should send TOC events downstream.

Like with tags, sinks will post a TOC message on the bus for the
application with the global TOC, once a global TOC event reaches the
sink.

##  Working with GstMessage

If a table of contents is available, applications will receive a TOC
message on the pipeline’s `GstBus`.

A TOC message will be posted on the bus by sinks when the receive a TOC
event containing a TOC with global scope. Elements extracting TOCs
should not post a TOC message themselves, but send a TOC event
downstream.

The reason for this is that there may be cascades of TOCs (e.g. a zip
archive containing multiple matroska files, each with a TOC).

`GstMessage` with `GstToc` can be created using `gst_message_new_toc()` and
parsed with `gst_message_parse_toc()`. The *updated* parameter in these
methods indicates whether the TOC was just discovered (set to false) or
TOC was already found and have been updated (set to true). This message
will typically be posted by sinks to pipeline in case you have
discovered TOC data within your element.

##  Working with GstEvent

There are two types of TOC-related events:

  - downstream TOC events that contain TOC information and travel
    downstream

  - toc-select events that travel upstream and can be used to select a
    certain TOC entry for playback (similar to seek events)

`GstToc` supports select event through `GstEvent` infrastructure. The idea
is the following: when you receive TOC select event, parse it with
`gst_event_parse_toc_select()` and seek stream (if it is not
streamable) for specified TOC UID (you can use `gst_toc_find_entry()`
to find entry in TOC by UID). To create TOC select event use
`gst_event_new_toc_select()`. The common action on such event is to
seek to specified UID within your element.

## Implementation coverage, Specifications, …

Below is a list of container formats, links to documentation and a
summary of toc related features. Each section title also indicates
whether reading/writing a toc is implemented. Below hollow bullet point
*o* indicate no support and filled bullets *\*\* indicate that this
feature is handled.

### AIFC: -/-
<http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/Docs/AIFF-1.3.pdf>
o *MARK* o *INST*

The *MARK* chunk defines a list of (cue-id, `position_in_samples`,
label).

The *INST* chunk contains a sustainLoop and releaseLoop, each consisting
of (loop-type, cue-begin, cue-end)

### FLAC: read/write

<http://xiph.org/flac/format.html#metadata_block_cuesheet> \*
METADATA\_BLOCK\_CUESHEET \* CUESHEET\_TRACK o CUESHEET\_TRACK\_INDEX

Both `CUESHEET_TRACK` and `CUESHEET_TRACK_INDEX` have a (relative) offset
in samples. `CUESHEET_TRACK` has ISRC metadata.

### MKV: read/write

<http://matroska.org/technical/specs/chapters/index.html> \* Chapters
and Editions each having a uid \* Chapter have start/end time and
metadata: ChapString, ChapLanguage, ChapCountry

### MP4: \* elst

The *elst* atom contains a list of edits. Each edit consists of (length,
start, play-back speed).

### OGG: -/- <https://wiki.xiph.org/Chapter_Extension> o VorbisComment

fields called CHAPTERxxx and CHAPTERxxxNAME with xxx being a number
between 000 and 999.

### WAV: read/write <http://www.sonicspot.com/guide/wavefiles.html> \* *cue
' o 'plst* \* *adtl* \* *labl* \* *note* o *ltxt* o *smpl*

The `*cue` chunk defines a list of markers in the stream with `cue-id`s.
The `smpl*` chunk defines a list of regions in the stream with `cue-id`s
in the same namespace (?).

The various *adtl* chunks: *labl*, *note* and *ltxt* refer to the
'cue-id’s.

A *plst* chunk defines a sequence of segments (`cue-id`, `length_samples`,
repeats). The *smpl* chunk defines a list of loops (`cue-id`, `beg`, `end`,
`loop-type`, `repeats`).

## Conclusion/Ideas/Future work

Based on the data of chapter 5, a few thoughts and observations that can
be used to extend and refine our API. These things below are not
reflecting the current implementation.

All formats have table of `[cue-id, cue-start, (cue-end), (extra tags)]`
- `cue-id` is commonly represented as and unsigned int 32bit - `cue-end` is
optional. Extra tags could be represented as a structure/taglist

Many formats have metadata that references the cue-table. - loops in
instruments in wav, aifc - edit lists in wav, mp4

For mp4.edtl, wav.plst we could expose two editions. 1) the edit list is
flattened: default, for playback 2) the stream has the raw data and the
edit list is there as chapter markers: useful for editing software

We might want to introduce a new `GST_TOC_ENTRY_TYPE_MARKER` or `_CUE`.
This would be a sequence entry-type and it would not be used for
navigational purposes, but to attach data to a point in time (envelopes,
loops, …).

API wise there is some overlap between: - exposing multiple audio/video
tracks as pads or as ToC editions. For ToC editions, we have the
TocSelect event. - exposing subtitles as a sparse stream or as ToC
sequence of markers with labels
