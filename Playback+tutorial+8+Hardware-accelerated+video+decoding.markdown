# Playback tutorial 8: Hardware-accelerated video decoding

This page last changed on Jul 24, 2012 by xartigas.

# Goal

Hardware-accelerated video decoding has rapidly become a necessity, as
low-power devices grow more common. This tutorial (more of a lecture,
actually) gives some background on hardware acceleration and explains
how does GStreamer benefit from it.

Sneak peek: if properly setup, you do not need to do anything special to
activate hardware acceleration; GStreamer automatically takes advantage
of it.

# Introduction

Video decoding can be an extremely CPU-intensive task, especially for
higher resolutions like 1080p HDTV. Fortunately, modern graphics cards,
equipped with programmable GPUs, are able to take care of this job,
allowing the CPU to concentrate on other duties. Having dedicated
hardware becomes essential for low-power CPUs which are simply incapable
of decoding such media fast enough.

In the current state of things (July-2012) each GPU manufacturer offers
a different method to access their hardware (a different API), and a
strong industry standard has not emerged yet.

As of July-2012, there exist at least 8 different video decoding
acceleration APIs:

[VAAPI](http://en.wikipedia.org/wiki/Video_Acceleration_API) (*Video
Acceleration API*): Initially designed by
[Intel](http://en.wikipedia.org/wiki/Intel) in 2007, targeted at the X
Window System on Unix-based operating systems, now open-source. It is
currently not limited to Intel GPUs as other manufacturers are free to
use this API, for example, [Imagination
Technologies](http://en.wikipedia.org/wiki/Imagination_Technologies) or
[S3 Graphics](http://en.wikipedia.org/wiki/S3_Graphics). Accessible to
GStreamer through
the [gstreamer-vaapi](http://gitorious.org/vaapi/gstreamer-vaapi) and
[Fluendo](http://en.wikipedia.org/wiki/Fluendo)’s Video Acceleration
Decoder (fluvadec) plugins.

[VDPAU](http://en.wikipedia.org/wiki/VDPAU) (*Video Decode and
Presentation API for UNIX*): Initially designed by
[NVidia](http://en.wikipedia.org/wiki/NVidia) in 2008, targeted at the X
Window System on Unix-based operating systems, now open-source. Although
it is also an open-source library, no manufacturer other than NVidia is
using it yet. Accessible to GStreamer through
the [vdpau](http://cgit.freedesktop.org/gstreamer/gst-plugins-bad/tree/sys/vdpau) element
in plugins-bad and [Fluendo](http://en.wikipedia.org/wiki/Fluendo)’s
Video Acceleration Decoder (fluvadec) plugins.

[DXVA](http://en.wikipedia.org/wiki/DXVA) (*DirectX Video
Acceleration*): [Microsoft](http://en.wikipedia.org/wiki/Microsoft) API
specification for the Microsoft Windows and Xbox 360
platforms. Accessible to GStreamer through
the [Fluendo](http://en.wikipedia.org/wiki/Fluendo)’s Video
Acceleration Decoder (fluvadec) plugin.

[XVBA](http://en.wikipedia.org/wiki/Xvba) (*X-Video Bitstream
Acceleration*): Designed by [AMD
Graphics](http://en.wikipedia.org/wiki/AMD_Graphics), is an arbitrary
extension of the X video extension (Xv) for the X Window System on Linux
operating-systems. Currently only AMD's ATI Radeon graphics cards
hardware that have support for Unified Video Decoder version 2.0 or
later are supported by the proprietary ATI Catalyst device
driver. Accessible to GStreamer through
the [Fluendo](http://en.wikipedia.org/wiki/Fluendo)’s Video
Acceleration Decoder
(fluvadec) plugin.

[VDA](http://developer.apple.com/library/mac/#technotes/tn2267/_index.html)
(*Video Decode Acceleration*): Available on [Mac OS
X](http://en.wikipedia.org/wiki/OS_X) v10.6.3 and later with Mac models
equipped with the NVIDIA GeForce 9400M, GeForce 320M, GeForce GT 330M,
ATI HD Radeon GFX, Intel HD Graphics and others. Only accelerates
decoding of H.264 media. Accessible to GStreamer through
the [Fluendo](http://en.wikipedia.org/wiki/Fluendo)’s Video
Acceleration Decoder (fluvadec) plugin.

[OpenMAX](http://en.wikipedia.org/wiki/OpenMAX) (*Open Media
Acceleration*): Managed by the non-profit technology consortium [Khronos
Group](http://en.wikipedia.org/wiki/Khronos_Group "Khronos Group"),
it is a "royalty-free, cross-platform set of C-language programming
interfaces that provides abstractions for routines especially useful for
audio, video, and still images". Accessible to GStreamer through
the [gstreamer-omx](http://git.freedesktop.org/gstreamer/gst-omx) plugin.

[OVD](http://developer.amd.com/sdks/AMDAPPSDK/assets/OpenVideo_Decode_API.PDF)
(*Open Video Decode*): Another API from [AMD
Graphics](http://en.wikipedia.org/wiki/AMD_Graphics), designed to be a
platform agnostic method for softrware developers to leverage the
[Universal Video
Decode](http://en.wikipedia.org/wiki/Unified_Video_Decoder) (UVD)
hardware inside AMD Radeon graphics cards. Currently unavailable to
GStreamer.

[DCE](http://en.wikipedia.org/wiki/Distributed_Codec_Engine)
(*Distributed Codec Engine*): An open source software library ("libdce")
and API specification by [Texas
Instruments](http://en.wikipedia.org/wiki/Texas_Instruments), targeted
at Linux systems and ARM platforms. Accessible to GStreamer through
the [gstreamer-ducati](https://github.com/robclark/gst-ducati) plugin.

There exist some GStreamer plugins, like the
[gstreamer-vaapi](http://gitorious.org/vaapi/gstreamer-vaapi) project or
the
[vdpau](http://cgit.freedesktop.org/gstreamer/gst-plugins-bad/tree/sys/vdpau)
element in plugins-bad, which target one particular hardware
acceleration API and expose its functionality through different
GStreamer elements. The application is then responsible for selecting
the appropriate plugin depending on the available APIs.

Some other GStreamer plugins, like
[Fluendo](http://en.wikipedia.org/wiki/Fluendo)’s Video Acceleration
Decoder (fluvadec), detect at runtime the available APIs and select one
automatically. This makes any program using these plugins independent of
the API, or even the operating system.

# Inner workings of hardware-accelerated video decoding plugins

These APIs generally offer a number of functionalities, like video
decoding, post-processing, presentation of the decoded frames, or
download of such frames to system memory. Correspondingly, plugins
generally offer a different GStreamer element for each of these
functions, so pipelines can be built to accommodate any need.

For example, the `gstreamer-vaapi` plugin offers the `vaapidecode`,
`vaapiupload`, `vaapidownload` and `vaapisink` elements that allow
hardware-accelerated decoding through VAAPI, upload of raw video frames
to GPU memory, download of GPU frames to system memory and presentation
of GPU frames, respectively.

It is important to distinguish between conventional GStreamer frames,
which reside in system memory, and frames generated by
hardware-accelerated APIs. The latter reside in GPU memory and cannot be
touched by GStreamer. They can usually be downloaded to system memory
and treated as conventional GStreamer frames, but it is far more
efficient to leave them in the GPU and display them from there.

GStreamer needs to keep track of where these “hardware buffers” are
though, so conventional buffers still travel from element to element,
but their only content is a hardware buffer ID, or handler. If retrieved
with an `appsink`, for example, hardware buffers make no sense, since
they are meant to be handled only by the plugin that generated them.

To indicate this, these buffers have special Caps, like
`video/x-vdpau-output` or `video/x-fluendo-va`. In this way, the
auto-plugging mechanism of GStreamer will not try to feed hardware
buffers to conventional elements, as they would not understand the
received buffers. Moreover, using these Caps, the auto-plugger is able
to automatically build pipelines that use hardware acceleration, since,
after a VAAPI decoder, a VAAPI sink is the only element that fits.

This all means that, if a particular hardware acceleration API is
present in the system, and the corresponding GStreamer plugin is also
available, auto-plugging elements like `playbin2` are free to use
hardware acceleration to build their pipelines; the application does not
need to do anything special to enable it. Almost:

When `playbin2` has to choose among different equally valid elements,
like conventional software decoding (through `vp8dec`, for example) or
hardware accelerated decoding (through `vaapidecode`, for example), it
uses their *rank* to decide. The rank is a property of each element that
indicates its priority; `playbin2` will simply select the element that
is able to build a complete pipeline and has the highest rank.

So, whether `playbin2` will use hardware acceleration or not will depend
on the relative ranks of all elements capable of dealing with that media
type. Therefore, the easiest way to make sure hardware acceleration is
enabled or disabled is by changing the rank of the associated element,
as shown in this code:

``` lang=c
static void enable_factory (const gchar *name, gboolean enable) {
    GstRegistry *registry = NULL;
    GstElementFactory *factory = NULL;

    registry = gst_registry_get_default ();
    if (!registry) return;

    factory = gst_element_factory_find (name);
    if (!factory) return;

    if (enable) {
        gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), GST_RANK_PRIMARY + 1);
    }
    else {
        gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), GST_RANK_NONE);
    }

    gst_registry_add_feature (registry, GST_PLUGIN_FEATURE (factory));
    return;
}
```

The first parameter passed to this method is the name of the element to
modify, for example, `vaapidecode` or `fluvadec`.

The key method is `gst_plugin_feature_set_rank()`, which will set the
rank of the requested element factory to the desired level. For
convenience, ranks are divided in NONE, MARGINAL, SECONDARY and PRIMARY,
but any number will do. When enabling an element, we set it to
PRIMARY+1, so it has a higher rank than the rest of elements which
commonly have PRIMARY rank. Setting an element’s rank to NONE will make
the auto-plugging mechanism to never select it.

# Hardware-accelerated video decoding and the GStreamer SDK

There are no plugins deployed in the GStreamer SDK Amazon 2012.7 that
allow hardware-accelerated video decoding. The main reasons are that
some of them are not yet fully operational, or still have issues, or are
proprietary. Bear in mind that this situation is bound to change in the
near future, as this is a very active area of development.

Some of these plugins can be built from their publicly available
sources, using the Cerbero build system (see [Installing on
Linux](Installing%2Bon%2BLinux.html)) or independently (linking against
the GStreamer SDK libraries, obviously). Some other plugins are readily
available in binary form from their vendors.

The following sections try to summarize the current state of some of
these plugins.

### vdpau in gst-plugins-bad

  - GStreamer element for VDPAU, present in
    [gst-plugins-bad](http://cgit.freedesktop.org/gstreamer/gst-plugins-bad/tree/sys/vdpau).
  - Supported codecs: 

<table>
<thead>
<tr class="header">
<th>MPEG2</th>
<th>MPEG4</th>
<th>H.264</th>
</tr>
</thead>
<tbody>
</tbody>
</table>

### gstreamer-vaapi

  - GStreamer element for VAAPI. Standalone project hosted at
    [gstreamer-vaapi](http://gitorious.org/vaapi/gstreamer-vaapi).
  - Supported codecs:

<table>
<thead>
<tr class="header">
<th>MPEG2</th>
<th>MPEG4</th>
<th>H.264</th>
<th>VC1</th>
<th>WMV3</th>
</tr>
</thead>
<tbody>
</tbody>
</table>

  - Can interface directly with Clutter (See [Basic tutorial 15: Clutter
    integration](Basic%2Btutorial%2B15%253A%2BClutter%2Bintegration.html)),
    so frames do not need to leave the GPU.
  - Compatible with `playbin2`.

### gst-omx

  - GStreamer element for OpenMAX. Standalone project hosted at
    [gst-omx](http://git.freedesktop.org/gstreamer/gst-omx/).
  - Supported codecs greatly vary depending on the underlying hardware.

### fluvadec

  - GStreamer element for VAAPI, VDPAU, DXVA2, XVBA and VDA from
    [Fluendo](http://en.wikipedia.org/wiki/Fluendo) (propietary).
  - Supported codecs depend on the chosen API, which is selected at
    runtime depending on what is available on the system:

<table>
<thead>
<tr class="header">
<th> </th>
<th>MPEG2</th>
<th>MPEG4</th>
<th>H.264</th>
<th>VC1</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>VAAPI</td>
<td><span>✓</span></td>
<td><span>✓</span></td>
<td><span>✓</span></td>
<td><span>✓</span></td>
</tr>
<tr class="even">
<td>VDPAU</td>
<td><span>✓</span></td>
<td><span>✓</span></td>
<td><span>✓</span></td>
<td><span>✓</span></td>
</tr>
<tr class="odd">
<td>XVBA</td>
<td> </td>
<td> </td>
<td><span>✓</span></td>
<td><span>✓</span></td>
</tr>
<tr class="even">
<td>DXVA2</td>
<td> </td>
<td> </td>
<td><span>✓</span></td>
<td> </td>
</tr>
<tr class="odd">
<td>VDA</td>
<td> </td>
<td> </td>
<td><span>✓</span></td>
<td> </td>
</tr>
</tbody>
</table>

  - Can interface directly with Clutter (See [Basic tutorial 15: Clutter
    integration](Basic%2Btutorial%2B15%253A%2BClutter%2Bintegration.html)),
    so frames do not need to leave the GPU.
  - Compatible with `playbin2`.

# Conclusion

This tutorial has shown a bit how GStreamer internally manages hardware
accelerated video decoding. Particularly,

  - Applications do not need to do anything special to enable hardware
    acceleration if a suitable API and the corresponding GStreamer
    plugin are available.
  - Hardware acceleration can be enabled or disabled by changing the
    rank of the decoding element with `gst_plugin_feature_set_rank()`.

It has been a pleasure having you here, and see you soon\!

Document generated by Confluence on Oct 08, 2015 10:27
