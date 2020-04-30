# gst-libav

This module contains a GStreamer plugin for using the encoders, decoders,
muxers, and demuxers provided by FFmpeg. It is called gst-libav for historical
reasons.

# Plugin Dependencies and Licenses

GStreamer is developed under the terms of the LGPL-2.1 (see COPYING file for
details), and that includes the code in this repository.

However, this repository depends on FFmpeg, which can be built in the following
modes using various `./configure` switches: LGPL-2.1, LGPL-3, GPL, or non-free.

This can mean, for example, that if you are distributing an application which
has a non-GPL compatible license (like a closed-source application) with
GStreamer, you have to make sure not to build FFmpeg with GPL code enabled.

Overall, when using plugins that link to GPL libraries, GStreamer is for all
practical reasons under the GPL itself.

The above recommendations are not legal advice, and you are responsible for
ensuring that you meet your licensing obligations.
