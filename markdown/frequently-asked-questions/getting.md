# Getting GStreamer

## How do I get GStreamer ?

Generally speaking, you have three options, ranging from easy to hard :

  - [distribution-specific packages](#getting-gstreamer-packages)

  - [source tarballs](#getting-gstreamer-source)

  - [git](#getting-gstreamer-packages)

## There seem to be different GStreamer versions, like 0.10 and 1.0? What's up with that?

GStreamer-0.10 and GStreamer-1.0 are the main version 'series'
currently in use. For all practical purposes you should think of them as
two completely different libraries which just happen to have a similar
name. They can be installed in parallel and are completely independent.

For the 0.10 version you will need the 0.10 plugins and bindings
(gst-plugins 0.10.x, gst-ffmpeg 0.10.x, gst-python 0.10.x etc.), while
for the 1.0 version you will need the 1.0 plugins and bindings (ie.
gst-plugins-base 1.0.x, gst-plugins-good 1.0.x, gst-plugins-ugly 1.0.x,
gst-plugins-bad 1.0.x, gst-ffmpeg 1.0.x, gst-python 1.0.x). The micro
version for each main version does not have to match exactly, only the
major versions needs to be the same (ie. it may be that the current
gst-plugins-good version is 1.0.6 and the current GStreamer core version
is 1.0.13). GStreamer-1.0 will not see or use any of the GStreamer-0.10
plugins and vice versa.

All GStreamer command line tools are suffixed with their main version,
e.g. gst-launch-0.10 and gst-launch-1.0, or gst-inspect-0.10 and
gst-inspect-1.0.

Applications will use either GStreamer-0.10 or GStreamer-1.0, since the
0.10 and 1.0 API/ABI are not compatible.

Odd-numbered versions such as 0.9.x, 0.11.x, etc. are unstable developer
releases that should generally not be used.

## So which GStreamer version should I get?

You should download GStreamer-1.0. GStreamer-0.10 is end-of-life.

## How can I install GStreamer from source ?

We provide tarballs of our releases on our own site, at
<http://gstreamer.freedesktop.org/src/>

When compiling from source, make sure you specify PKG\_CONFIG\_PATH
correctly when building against GStreamer. For example, if you
configured GStreamer with the default prefix (which is /usr/local), then
you need to

    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

before building gst-plugins.

## Are there premade binaries available ?

Yes we currently provide [precompiled packages for Windows, OS/X,
Android and iOS](http://gstreamer.freedesktop.org/pkg/).

We currently do not provide packages for Linux distributions, but rather
rely on the distributions for that. GStreamer packages should be
available for all major (and minor) distributions.

## Why don't you provide premade binaries for distribution XY ?

GStreamer is run on a volunteer basis. The package that are
provided are made by non-paid people who do this on their own time. The
distributions we support with binaries are the distributions that we
have people who have volunteered to make binaries for. If you are
interested in maintaining GStreamer binaries for other distributions or
Unices we would be happy to hear from you. Contact us through the
GStreamer-devel mailing list.

I am having trouble compiling GStreamer on my LFS installation, why ?

If you are running LFS our basic opinion is that you should be
knowledgeable enough to solve any build issues you get on your own.
Being volunteered based we can't promise support to anyone of course,
but are you using LFS consider yourself extra unsupported. We neither
can or want to know enough, about how your unique system is configured,
to be able to help you. That said, if you come to the \#gstreamer
channel on irc.openprojects.net we might of course be able to give you
some general hints and pointers.

## How do I get GStreamer through git ?

See this page : <http://gstreamer.freedesktop.org/dev/> for git
access (anonymous and developer).
