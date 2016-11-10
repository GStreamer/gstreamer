# General

## Is GStreamer a media player ?

No, GStreamer is a development framework for creating applications like
media players, video editors, streaming media broadcasters and so on.
That said, very good media players can easily be built on top of GStreamer
especially when using the high-level object called playbin.

## Why is GStreamer written in C ? Why not C++/Objective-C/... ?

We like C. Aside from "personal preference", there are a number
of technical reasons why C is nice in this project:

  - C is extremely portable.

  - C is fast.

  - It is easy to make language bindings for libraries written in C.

  - The GObject object system provided by GLib implements objects in C,
    in a portable, powerful way. This library provides for introspection
    and runtime dynamic typing. It is a full OO system, but without the
    syntactic sugar. If you want sugar, take a look at
    [Vala](http://live.gnome.org/Vala).

  - Use of C integrates nicely with Gtk+ and GNOME. Some people like
    this a lot, but neither Gtk+ nor GNOME are required by GStreamer.

So, in closing, we like C. If you don't, that's fine; if you still want
to help out on GStreamer, we always need more language binding people.
And if not, don't bother us; we're working :-)

## What applications are available for GStreamer ?

Many media player applications have chosen GStreamer for their
backend. Also a couple of media format conversion tools have been
written using the powers of GStreamer. With the advent of GStreamer-0.10
several media editing applications have been started.

For a list of projects, look at the [application
list](http://gstreamer.freedesktop.org/apps/) on the GStreamer project
website.

## Does GStreamer support the format of my media files?

GStreamer aims to support every format imaginable, but that
doesn't mean the developers have managed to achieve that aim yet. If a
GStreamer enabled application doesn't play back your files, you can help
us solve that problem by [filing an enhancement request
bug](http://bugzilla.gnome.org) for that format. If you have it, please
provide:

  - links to other players, preferably Open Source and working on Unix

  - links to explanations of the format.

  - ways to obtain mediafiles in that format to test.

## What are the exact licensing terms for GStreamer and its plugins
?

All of GStreamer, including our own plugin code, is licensed
under the [GNU LGPL 2.1](http://www.gnu.org/licenses/lgpl-2.1.html)
license. Some of the libraries we use for some of the plugins are
however under the GPL, which means that those plugins can not be used by
a non-GPL-compatible application.

As part of the GStreamer source download you find a file called
LICENSE\_readme in gst-plugins package. That file contains information
in the exact licensing terms of the libraries we use. As a general rule,
GStreamer aims at using only LGPL or BSD licensed libraries if available
and only use GPL or proprietary libraries where no good LGPL or BSD
alternatives are available.

From GStreamer 0.4.2 on, we implemented a license field for all of the
plugins, and in the future we might have the application enforce a
stricter policy (much like tainting in the kernel).

## Is GStreamer a sound server ?

No, GStreamer is not a soundserver. GStreamer does however have
plugins supporting most of the major soundservers available today,
including pulseaudio, ESD, aRTSd, Jack and others.

## Will GStreamer be available for platforms other than Unix ?

Depends. Our main target is the Unix platform. It also works on
Win32 and Mac OS X, but it may still be a bit challenging to get
everything up and running. That said, interest has been expressed in
porting GStreamer to other platforms and the GStreamer core team will
gladly accept patches to accomplish this.

## What is GStreamer's relationship with the GNOME community ?

While GStreamer is operated as an independent project, we do have
a close relationship with the GNOME community. Many of our hackers
consider themselves also to be members of the GNOME community. GStreamer
is officialy bundled with the GNOME desktop, as lots of packages (like
gnome-media, totem and rhythmbox) are using it. This does not exclude
use of GStreamer by other communities at all, of course.

## What is GStreamer's relationship with the KDE community ?

The GStreamer community wants to have as good a relationship as
possible with KDE, and we hope that someday KDE decides to adopt
GStreamer as their multimedia API (planned for KDE 4). There have been
contacts from time to time between the GStreamer community and KDE and
we do already have support for the aRTSd sound server used by KDE. Also,
some of the KDE hackers have created Qt bindings of GStreamer, made a
simple video player and using it in some audio players (JuK and AmaroK).

## I'm considering adding GStreamer output to my application...

That doesn't really make sense. GStreamer is not a sound server,
so you don't output directly to GStreamer, and it's not an intermediate
API between audio data and different kinds of audio sinks. It is a
fundamental design decision to use GStreamer in your app; there are no
easy ways of somehow 'transfering' data from your app to GStreamer.
Instead, your app would have to use or implement a number of GStreamer
elements, string them together, and tell them to run. In that manner the
data would all be internal to the GStreamer pipeline.

That said, it is possible to write a plugin specific to your app that
can get at the audio data.
