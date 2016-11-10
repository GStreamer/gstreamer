# Dependencies

## Why are there so many dependencies ?

Making a full-featured media framework is a huge undertaking in
itself. By using the work done by others, we both reduce the amount of
redundant work being done and leave ourselves free to work on the
architecture itself instead of working on the low-level stuff. We would
be stupid not to reuse the code others have written.

However, do realize that in no way you are forced to have all
dependencies installed. None of the core developers has all of them
installed. GStreamer has only a few obligate dependencies : GLib 2.0,
liborc, and very common stuff like glibc, a C compiler, and so on. All
of the other dependencies are optional.

So, in closing, let's rephrase the question to “Why are you giving me so
many choices and such a rich environment ?”

## Is GStreamer X independent ?

Yes, we have no hard X dependency in any of our modules. There
are many GStreamer applications that run fine without any need for X,
for example streaming servers, transcoding applications, or audio
applications that don't output any video. Other applications output
video to a framebuffer, custom-made hardware sinks, or via wayland.

## What is GStreamer's position on efforts such as LADSPA ?

GStreamer actively supports such efforts, and in the case of
[LADSPA](http://www.ladspa.org/), we already have a wrapper plugin. This
wrapper plug-in detects the LADSPA plugins present on your system at run
time.

## Does GStreamer support MIDI ?

Not yet. The GStreamer architecture should be able to support the
needs of MIDI applications very well however. If you are a developer
interested in adding MIDI support to GStreamer we are very interested in
getting in touch with you.

MIDI playback is provided by plugins such as wildmidi and timidity.

## Does GStreamer depend on GNOME or GTK+ ?

No. But many of the applications developed for GStreamer do,
including some of our sample applications. Other applications use the Qt
toolkit, or are written for Mac OS/X or Windows. We aim to provide API
that is toolkit agnostic and can be used from any toolkit, desktop
environment or operating system.
