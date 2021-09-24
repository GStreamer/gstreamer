# General

## Is GStreamer a media player?

No, GStreamer is a development framework for creating applications like
media players, video editors, streaming media broadcasters and so on.
That said, very good media players can easily be built on top of GStreamer
especially when using the high-level APIs we provide such as `GstPlayer` or
the `playbin` and `playbin3` elements.

## Why is GStreamer written in C? Why not C++/Objective-C/...?

We like C. Aside from "personal preference", there are a number
of technical reasons why C is nice in this project:

  - C is extremely portable.

  - C is fast.

  - It is easy to make language bindings for libraries written in C.

  - The `GObject` object system provided by `GLib` implements objects in C,
    in a portable and powerful way. This library provides for introspection
    and runtime dynamic typing. It is a full OO system, but without the
    syntactic sugar. If you want sugar, take a look at
    [Vala](http://live.gnome.org/Vala).

  - Use of C integrates nicely with Gtk+ and GNOME. Some people like
    this a lot, but neither Gtk+ nor GNOME are required by GStreamer.

There are also historical reasons: When GStreamer was started, C++ and
free and open source C++ compilers were not as stable and mature as they
are today, and there was the desire to provide a certain degree of
API and ABI stability.

In closing, we like C. If you don't, that's fine; if you still want
to help out on GStreamer, we always need more language binding people.
If not, don't bother us; we're working :-)

## What kind of applications have been written in GStreamer?

GStreamer has been designed from the start to be a general-purpose multimedia
framework that can be used to write all kinds of multimedia applications.

There are a huge number of media playback applications of course, but also
audio and video capture applications that record to file or live stream the
captured content. Audio CD and DVD backup applications. Media format conversion
and transcoding tools. Streaming servers, both for small embedded devices where
resource usage needs to be tightly controlled, and for servers where scalability
is key. And of course audio and video editing applications.

For a list of projects, look at the [application
list](http://gstreamer.freedesktop.org/apps/) on the GStreamer project
website.

## Does GStreamer support the format of my media files?

GStreamer is plugin based and was designed to be extensible from the start,
so it can pretty much support any media format provided suitable plugins
are available.

It features demuxers, parsers and decoders for all common media formats and
hundreds of uncommon ones. If you have trouble playing back a file please
first make sure you have all the required plug-ins installed.

GStreamer aims to support every format imaginable, but that
doesn't mean the developers have managed to achieve that aim yet. If a
GStreamer enabled application doesn't play back your files, you can help
us solve that problem by [filing an enhancement request
issue](https://gitlab.freedesktop.org/gstreamer) for that format. If you have it,
please provide:

  - links to other players, preferably Open Source and working on Unix

  - links to explanations of the format.

  - ways to obtain mediafiles in that format to test.

## What are the exact licensing terms for GStreamer and its plugins?

All of GStreamer, including our own plugin code, is licensed
under the [GNU LGPL 2.1](http://www.gnu.org/licenses/lgpl-2.1.html)
license. Some of the libraries we use for some of the plugins are
however under the GPL, which means that those plugins can not be used by
a non-GPL-compatible application. Those are few and far between though
and there are usually non-GPL alternatives available for those GPL libraries.

As a general rule, GStreamer aims at using only LGPL or BSD licensed
libraries and only use GPL or proprietary libraries when no good LGPL or BSD
alternatives are available.

You can see the effective license of a plugin as the **License** field in
the output of the following command:

```
gst-inspect-1.0 <plugin name>
```

## Is GStreamer a sound server?

No, GStreamer is not a sound server. GStreamer does, however, have
plugins supporting most of the major sound servers available today,
including pulseaudio, Jack and others.

## Is GStreamer available for platforms other than Linux?

Yes, GStreamer is a cross-platform multimedia framework that
works on all major operating systems, including but not limited to
Linux, Android, iOS, macOS, Windows, and *BSD, and there are official
SDK binary packages for Android, iOS, macOS and Windows made available
with every GStreamer release.

## What is GStreamer's relationship with the GNOME community?

GStreamer is an independent project, but we do traditionally have
a close relationship with the GNOME community. Many of our hackers
consider themselves also to be members of the GNOME community. GStreamer
is officially bundled with the GNOME desktop, as lots of GNOME applications
are using it. This does not exclude use of GStreamer by other communities
at all, of course.

## What is GStreamer's relationship with the KDE community?

The GStreamer community wants to have as good a relationship as
possible with KDE, and we hope that someday KDE decides to adopt
GStreamer as their multimedia platform. There have been contacts
from time to time between the GStreamer community and KDE and
GStreamer is used by various KDE and Qt multimedia APIs. Also,
some of the KDE hackers have created Qt bindings of GStreamer, made a
simple video player and using it in some audio players (JuK and AmaroK).

## I'm considering adding GStreamer output to my application...

That doesn't really make sense. GStreamer is not a sound server,
so you don't output directly to GStreamer, and it's not an intermediate
API between audio data and different kinds of audio sinks. It is a
fundamental design decision to use GStreamer in your application; there are
no easy ways of somehow 'transfering' data from your app to GStreamer (well,
there are of course, but they would be reserved for special use cases).
Instead, your application would have to use or implement a number of GStreamer
elements, string them together, and tell them to run. In that manner the
data would all be internal to the GStreamer pipeline.

That said, it is possible to write a plugin specific to your app that
can get at the audio or video data.
