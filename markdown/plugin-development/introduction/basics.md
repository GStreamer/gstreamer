---
title: Foundations
...

# Foundations

This chapter of the guide introduces the basic concepts of GStreamer.
Understanding these concepts will help you grok the issues involved in
extending GStreamer. Many of these concepts are explained in greater
detail in the *GStreamer Application Development Manual*; the basic
concepts presented here serve mainly to refresh your memory.

## Elements and Plugins

Elements are at the core of GStreamer. In the context of plugin
development, an *element* is an object derived from the [`
GstElement`](GstElement) class. Elements
provide some sort of functionality when linked with other elements: For
example, a source element provides data to a stream, and a filter
element acts on the data in a stream. Without elements, GStreamer is
just a bunch of conceptual pipe fittings with nothing to link. A large
number of elements ship with GStreamer, but extra elements can also be
written.

Just writing a new element is not entirely enough, however: You will
need to encapsulate your element in a *plugin* to enable GStreamer to
use it. A plugin is essentially a loadable block of code, usually called
a shared object file or a dynamically linked library. A single plugin
may contain the implementation of several elements, or just a single
one. For simplicity, this guide concentrates primarily on plugins
containing one element.

A *filter* is an important type of element that processes a stream of
data. Producers and consumers of data are called *source* and *sink*
elements, respectively. *Bin* elements contain other elements. One type
of bin is responsible for synchronization of the elements that they
contain so that data flows smoothly. Another type of bin, called
*autoplugger* elements, automatically add other elements to the bin and
links them together so that they act as a filter between two arbitrary
stream types.

The plugin mechanism is used everywhere in GStreamer, even if only the
standard packages are being used. A few very basic functions reside in
the core library, and all others are implemented in plugins. A plugin
registry is used to store the details of the plugins in a binary
registry file. This way, a program using GStreamer does not have to load
all plugins to determine which are needed. Plugins are only loaded when
their provided elements are requested.

See the *GStreamer Library Reference* for the current implementation
details of [`GstElement`](GstElement) and [`GstPlugin`](GstPlugin).

## Pads

*Pads* are used to negotiate links and data flow between elements in
GStreamer. A pad can be viewed as a “place” or “port” on an element
where links may be made with other elements, and through which data can
flow to or from those elements. Pads have specific data handling
capabilities: A pad can restrict the type of data that flows through it.
Links are only allowed between two pads when the allowed data types of
the two pads are compatible.

An analogy may be helpful here. A pad is similar to a plug or jack on a
physical device. Consider, for example, a home theater system consisting
of an amplifier, a DVD player, and a (silent) video projector. Linking
the DVD player to the amplifier is allowed because both devices have
audio jacks, and linking the projector to the DVD player is allowed
because both devices have compatible video jacks. Links between the
projector and the amplifier may not be made because the projector and
amplifier have different types of jacks. Pads in GStreamer serve the
same purpose as the jacks in the home theater system.

For the most part, all data in GStreamer flows one way through a link
between elements. Data flows out of one element through one or more
*source pads*, and elements accept incoming data through one or more
*sink pads*. Source and sink elements have only source and sink pads,
respectively.

See the *GStreamer Library Reference* for the current implementation
details of a [`GstPad`](GstPad).

## GstMiniObject, Buffers and Events

All streams of data in GStreamer are chopped up into chunks that are
passed from a source pad on one element to a sink pad on another
element. *GstMiniObject* is the structure used to hold these chunks of
data.

GstMiniObject contains the following important types:

  - An exact type indicating what type of data (event, buffer, ...) this
    GstMiniObject is.

  - A reference count indicating the number of elements currently
    holding a reference to the miniobject. When the reference count
    falls to zero, the miniobject will be disposed, and its memory will
    be freed in some sense (see below for more details).

For data transport, there are two types of GstMiniObject defined: events
(control) and buffers (content).

Buffers may contain any sort of data that the two linked pads know how
to handle. Normally, a buffer contains a chunk of some sort of audio or
video data that flows from one element to another.

Buffers also contain metadata describing the buffer's contents. Some of
the important types of metadata are:

  - Pointers to one or more GstMemory objects. GstMemory objects are
    refcounted objects that encapsulate a region of memory.

  - A timestamp indicating the preferred display timestamp of the
    content in the buffer.

Events contain information on the state of the stream flowing between
the two linked pads. Events will only be sent if the element explicitly
supports them, else the core will (try to) handle the events
automatically. Events are used to indicate, for example, a media type,
the end of a media stream or that the cache should be flushed.

Events may contain several of the following items:

  - A subtype indicating the type of the contained event.

  - The other contents of the event depend on the specific event type.

Events will be discussed extensively in [Events: Seeking, Navigation and
More](plugin-development/advanced/events.md). Until then, the only event that
will be used is the *EOS* event, which is used to indicate the end-of-stream
(usually end-of-file).

See the *GStreamer Library Reference* for the current implementation
details of a [`GstMiniObject`](GstMiniObject), [`GstBuffer`](GstBuffer)
and [`GstEvent`](GstEvent).

### Buffer Allocation

Buffers are able to store chunks of memory of several different types.
The most generic type of buffer contains memory allocated by malloc().
Such buffers, although convenient, are not always very fast, since data
often needs to be specifically copied into the buffer.

Many specialized elements create buffers that point to special memory.
For example, the filesrc element usually maps a file into the address
space of the application (using mmap()), and creates buffers that point
into that address range. These buffers created by filesrc act exactly
like generic buffers, except that they are read-only. The buffer freeing
code automatically determines the correct method of freeing the
underlying memory. Downstream elements that receive these kinds of
buffers do not need to do anything special to handle or unreference it.

Another way an element might get specialized buffers is to request them
from a downstream peer through a GstBufferPool or GstAllocator. Elements
can ask a GstBufferPool or GstAllocator from the downstream peer
element. If downstream is able to provide these objects, upstream can
use them to allocate buffers. See more in [Memory
allocation](plugin-development/advanced/allocation.md).

Many sink elements have accelerated methods for copying data to
hardware, or have direct access to hardware. It is common for these
elements to be able to create a GstBufferPool or GstAllocator for their
upstream peers. One such example is ximagesink. It creates buffers that
contain XImages. Thus, when an upstream peer copies data into the
buffer, it is copying directly into the XImage, enabling ximagesink to
draw the image directly to the screen instead of having to copy data
into an XImage first.

Filter elements often have the opportunity to either work on a buffer
in-place, or work while copying from a source buffer to a destination
buffer. It is optimal to implement both algorithms, since the GStreamer
framework can choose the fastest algorithm as appropriate. Naturally,
this only makes sense for strict filters -- elements that have exactly
the same format on source and sink pads.

## Media types and Properties

GStreamer uses a type system to ensure that the data passed between
elements is in a recognized format. The type system is also important
for ensuring that the parameters required to fully specify a format
match up correctly when linking pads between elements. Each link that is
made between elements has a specified type and optionally a set of
properties. See more about caps negotiation in [Caps
negotiation](plugin-development/advanced/negotiation.md).

### The Basic Types

GStreamer already supports many basic media types. Following is a table
of a few of the basic types used for buffers in GStreamer. The table
contains the name ("media type") and a description of the type, the
properties associated with the type, and the meaning of each property. A
full list of supported types is included in [List of Defined
Types](plugin-development/advanced/media-types.md#list-of-defined-types).

<table>
<caption>Table of Example Types</caption>
<thead>
<tr class="header">
<th>Media Type</th>
<th>Description</th>
<th>Property</th>
<th>Property Type</th>
<th>Property Values</th>
<th>Property Description</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>audio/*</td>
<td><em>All audio types</em></td>
<td>rate</td>
<td>integer</td>
<td>greater than 0</td>
<td>The sample rate of the data, in samples (per channel) per second.</td>
</tr>
<tr class="even">
<td></td>
<td></td>
<td>channels</td>
<td>integer</td>
<td>greater than 0</td>
<td>The number of channels of audio data.</td>
</tr>
<tr class="odd">
<td>audio/x-raw</td>
<td>Unstructured and uncompressed raw integer audio data.</td>
<td>format</td>
<td>string</td>
<td>S8 U8 S16LE S16BE U16LE U16BE S24_32LE S24_32BE U24_32LE U24_32BE S32LE S32BE U32LE U32BE S24LE S24BE U24LE U24BE S20LE S20BE U20LE U20BE S18LE S18BE U18LE U18BE F32LE F32BE F64LE F64BE</td>
<td>The format of the sample data.</td>
</tr>
<tr class="even">
<td>audio/mpeg</td>
<td>Audio data compressed using the MPEG audio encoding scheme.</td>
<td>mpegversion</td>
<td>integer</td>
<td>1, 2 or 4</td>
<td>The MPEG-version used for encoding the data. The value 1 refers to MPEG-1, -2 and -2.5 layer 1, 2 or 3. The values 2 and 4 refer to the MPEG-AAC audio encoding schemes.</td>
</tr>
<tr class="odd">
<td></td>
<td></td>
<td>framed</td>
<td>boolean</td>
<td>0 or 1</td>
<td>A true value indicates that each buffer contains exactly one frame. A false value indicates that frames and buffers do not necessarily match up.</td>
</tr>
<tr class="even">
<td></td>
<td></td>
<td>layer</td>
<td>integer</td>
<td>1, 2, or 3</td>
<td>The compression scheme layer used to compress the data <em>(only if mpegversion=1)</em>.</td>
</tr>
<tr class="odd">
<td></td>
<td></td>
<td>bitrate</td>
<td>integer</td>
<td>greater than 0</td>
<td>The bitrate, in bits per second. For VBR (variable bitrate) MPEG data, this is the average bitrate.</td>
</tr>
<tr class="even">
<td>audio/x-vorbis</td>
<td>Vorbis audio data</td>
<td></td>
<td></td>
<td></td>
<td>There are currently no specific properties defined for this type.</td>
</tr>
</tbody>
</table>
