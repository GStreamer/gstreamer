# Frequently Asked Questions

<table>
<colgroup>
<col width="50%" />
<col width="50%" />
</colgroup>
<tbody>
<tr class="odd">
<td><p>This is a list of frequently asked questions. Use the menu on the right to quickly locate your enquire.</p>
<h1 id="FrequentlyAskedQuestions-WhatisGStreamer" class="western">What is GStreamer?</h1>
<p>As stated in the <a href="http://gstreamer.freedesktop.org/" class="external-link">GStreamer home page</a>: GStreamer is a library for constructing graphs of media-handling components. The applications it supports range from simple Ogg/Vorbis playback, audio/video streaming to complex audio (mixing) and video (non-linear editing) processing.</p>
<p>Applications can take advantage of advances in codec and filter technology transparently. Developers can add new codecs and filters by writing a simple plugin with a clean, generic interface.</p>
<p>GStreamer is released under the LGPL. The 0.10 series is API and ABI stable.</p></td>
<td><div class="panel" style="border-width: 1px;">
<div class="panelContent">
<p></p>
<div>
<ul>
<li><a href="#FrequentlyAskedQuestions-WhatisGStreamer">What is GStreamer?</a></li>
<li><a href="#FrequentlyAskedQuestions-WhatistheGStreamerSDK">What is the GStreamer SDK?</a></li>
<li><a href="#FrequentlyAskedQuestions-Whatisthedifferencebetweenthissiteandtheoneatfreedesktop">What is the difference between this site and the one at freedesktop?</a></li>
<li><a href="#FrequentlyAskedQuestions-WhousesGStreamer">Who uses GStreamer?</a></li>
<li><a href="#FrequentlyAskedQuestions-Whatisthetargetaudience">What is the target audience?</a></li>
<li><a href="#FrequentlyAskedQuestions-HowmanyversionsoftheGStreamerSDKarethere">How many versions of the GStreamer SDK are there?</a></li>
<li><a href="#FrequentlyAskedQuestions-IsthereanSDKforAndroid">Is there an SDK for Android?</a></li>
<li><a href="#FrequentlyAskedQuestions-WhataboutiOS">What about iOS?</a></li>
</ul>
</div>
</div>
</div></td>
</tr>
</tbody>
</table>

# What is the GStreamer SDK?

GStreamer has sometimes proven to be difficult to understand, in part
due to the complex nature of the data it manipulates (multimedia
graphs), and in part due to it being a live framework which continuously
evolves.

This SDK, essentially, does four things for you:

  - Sticks to one particular version of GStreamer, so you do not have to
    worry about changing documentation and features being added or
    deprecated.
  - Provides a wide range of tutorials to get you up to speed as fast as
    possible.
  - Provides a “build system”, this is, instructions and tools to build
    your application from the source code. On Linux, this is more or
    less straightforward, but GStreamer support for other operating
    system has been historically more cumbersome. The SDK is also
    available on Windows and Mac OS X to ease building on these systems.
  - Provides a one-stop place for all documentation related to
    GStreamer, including the libraries it depends on.

# What is the difference between this site and the one at freedesktop?

The main and most important difference between these two sites is the
SDK: while [gstreamer.com](http://gstreamer.com/) provides a binary
ready for use for anyone interested in this
framework, [gstreamer.freedesktop.org](http://gstreamer.freedesktop.org/) pursues
other objectives:

  - [gstreamer.freedesktop.org](http://gstreamer.freedesktop.org/) is
    the main vehicle for the GStreamer community members to communicate
    with each other and improve this framework. This site is oriented to
    developers contributing to both, the development of the framework
    itself and building multimedia applications.
  - In contrast, the objective
    of [gstreamer.com](http://gstreamer.com/) is facilitating the use
    of GStreamer by providing a stable version of this framework,
    pre-built and ready to use. People using this SDK are mainly
    interested in the use of GStreamer as a tool that will help them
    build multimedia applications.

In summary:

<table>
<thead>
<tr class="header">
<th> </th>
<th><a href="http://gstreamer.freedesktop.org/" class="external-link">gstreamer.freedesktop.org</a></th>
<th><a href="http://gstreamer.com/" class="external-link">gstreamer.com</a></th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td>Version</td>
<td>All GStreamer versions</td>
<td>One single stable version</td>
</tr>
<tr class="even">
<td>Documentation</td>
<td>Mainly for consulting, covering all aspects of developing on and for GStreamer.</td>
<td>Consulting, How-to’s and tutorials only for the version of the SDK</td>
</tr>
<tr class="odd">
<td>SDK</td>
<td>No</td>
<td>Yes</td>
</tr>
</tbody>
</table>

# Who uses GStreamer?

Some cool media apps using GStreamer:

  -  [Banshee](http://banshee.fm/)
  - [Songbird](http://getsongbird.com/)
  -  [Snappy](http://live.gnome.org/snappy)   
  -  [Empathy](https://live.gnome.org/Empathy)
  -  [Totem](http://projects.gnome.org/totem/)
  -  [Transmaggedon](http://www.linuxrising.org/)
  -  [Flumotion](http://www.flumotion.net/)
  -  [Landell](http://landell.holoscopio.com/)
  -  [Longo match](http://longomatch.org/)
  -  [Rygel](https://live.gnome.org/Rygel)
  -  [Sound
    juicer](http://www.burtonini.com/blog/computers/sound-juicer)
  -  [Buzztard](http://wiki.buzztard.org/index.php/Overview)
  -  [Moovida](http://www.moovida.com/) (Based on Banshee)
  -  [Fluendo DVD
    Player](http://www.fluendo.com/shop/product/fluendo-dvd-player/)
  - and many [more](http://gstreamer.freedesktop.org/apps/)

# What is the target audience?

This SDK is mainly intended for application developers wanting to add
cross-platform multimedia capabilities to their programs.

Developers wanting to write their own plug-ins for GStreamer should
already be familiar with the topics covered in this documentation, and
should refer to the [GStreamer
documentation](http://gstreamer.freedesktop.org/documentation/) (particularly,
the plug-in writer's guide).

# How many versions of the GStreamer SDK are there?

Check out the [Releases](Releases.html) page for detailed information.

# Is there an SDK for Android?

The GStreamer SDK supports the Android platform since [version 2012.11
(Brahmaputra)](2012.11%2BBrahmaputra.html).

# What about iOS?

The GStreamer SDK supports the iOS platform since [version 2013.6
(Congo)](2013.6%2BCongo.html).
