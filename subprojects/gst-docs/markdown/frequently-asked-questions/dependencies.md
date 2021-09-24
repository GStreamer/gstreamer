# Dependencies

## Why are there so many dependencies?

Making a full-featured media framework is a huge undertaking in
itself. By using the work done by others, we both reduce the amount of
redundant work being done and leave ourselves free to work on the
architecture itself instead of working on the low-level stuff. We would
be stupid not to reuse the code others have written.

However, do realize that in no way you are forced to have all
dependencies installed. None of the core developers has all of them
installed. GStreamer has only a few mandatory dependencies : GLib 2.0,
liborc, and very common stuff like glibc, a C compiler, and so on. All
other dependencies are optional.

In closing, let's rephrase the question to “Why are you giving me so
many choices and such a rich environment?”

## Is GStreamer X11 independent? Can it be used headless?

Yes, we have no hard dependency on X11 or any other windowing system in
any of our modules. There are many GStreamer applications that run fine
without any need for display servers or windowing systems,
for example streaming servers, transcoding applications, or audio
applications that don't output any video. Other applications output
video to a framebuffer, custom-made hardware sinks, or via wayland.

## What is GStreamer's position on efforts such as LADSPA or LV2?

GStreamer actively supports such efforts, and in the case of [LADSPA][ladspa]
or [LV2][lv2] we already have wrapper plugins. These wrapper plug-ins detect
the LADSPA/LV2 plugins present on your system at run-time and make them
available as GStreamer elements.

[ladspa]: https://en.wikipedia.org/wiki/LADSPA
[lv2]: http://lv2plug.in/

## Does GStreamer support MIDI?

There is some rudimentary MIDI support in GStreamer, but it's not complete yet.

The GStreamer architecture should be able to support the needs of MIDI
applications very well, a full implementation is still missing, however.
If you are a developer interested in adding MIDI support to GStreamer please
get in touch, we would definitely be interested in that.

As for what exists today: the [`alsamidisrc`][alsamidisrc] element can be used
to fetch ALSA MIDI sequencer events and makes them available to elements that
understand the `audio/x-midi-events` format.

MIDI playback is provided by plugins such as `midiparse`, `fluiddec`,
`wildmidi` and `timidity`.

[alsamidisrc]: https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-base-plugins/html/gst-plugins-base-plugins-alsamidisrc.html

## Does GStreamer depend on GNOME or GTK+?

No, it's just that many GStreamer applications, including some of our sample
ones, happen to be GNOME or GTK+ applications, but there are just as many
using the Qt toolkit or written for Mac OS/X, Windows, Android or iOS.

We aim to provide an API that is toolkit-agnostic, so that GStreamer can be used
from any toolkit, desktop environment or operating system.
