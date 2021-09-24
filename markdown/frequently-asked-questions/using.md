# Using GStreamer

## Ok, I've installed GStreamer. What can I do next?

First of all, verify that you have a working installation and that
you can inspect plugins by typing

```
$ gst-inspect-1.0 fakesrc
```

This should print out a bunch of information about this particular
element. If this tells you that there is "no such element or plugin",
you haven't installed GStreamer correctly. Please check [how to get
GStreamer](#chapter-getting) If this fails with any other message, we
would appreciate a [bug report](#using-bugs-where).

It's time to try out a few things. Start with gst-launch and two
plug-ins that you really should have : fakesrc and fakesink. They do
nothing except pass empty buffers. Type this at the command-line:

```
$ gst-launch-1.0 -v fakesrc silent=false num-buffers=3 ! fakesink silent=false
```

This will print out output that looks similar to this:

```
RUNNING pipeline ...
fakesrc0: last-message = "get      ******* (fakesrc0:src)gt; (0 bytes, 0) 0x8057510"
fakesink0: last-message = "chain   ******* (fakesink0:sink)lt; (0 bytes, 0) 0x8057510"
fakesrc0: last-message = "get      ******* (fakesrc0:src)gt; (0 bytes, 1) 0x8057510"
fakesink0: last-message = "chain   ******* (fakesink0:sink)lt; (0 bytes, 1) 0x8057510"
fakesrc0: last-message = "get      ******* (fakesrc0:src)gt; (0 bytes, 2) 0x8057510"
fakesink0: last-message = "chain   ******* (fakesink0:sink)lt; (0 bytes, 2) 0x8057510"
execution ended after 5 iterations (sum 301479000 ns, average 60295800 ns, min 3000 ns, max 105482000 ns)
```

(Some parts of output have been removed for clarity) If it looks
similar, then GStreamer itself is running correctly.

To get a test video displayed, try:

```
$ gst-launch-1.0 videotestsrc ! videoconvert ! autovideosink
```

If `autovideosink` doesn't work, try an element that's specific for your
operating system and windowing system, such as `ximagesink` or `glimagesink`
or (on windows) `d3dvideosink`.

## Can my system play sound through GStreamer?

You can test this by trying to play a sine tone. For this, you
need to link the audiotestsrc element to an output element that matches
your hardware. A (non-complete) list of output plug-ins for audio is

  - `pulsesink` for Pulseaudio output

  - `alsasink` for ALSA output

  - `osssink` and `oss4sink` for OSS/OSSv4 output

  - `jackaudiosink` for JACK output

  - `autoaudiosink` for automatic audio output selection

First of all, run gst-inspect-1.0 on the output plug-in you want to use
to make sure you have it installed. For example, if you use Pulseaudio,
run

```
$ gst-inspect-1.0 pulsesink
```
and see if that prints out a bunch of properties for the plug-in.

Then try to play the sine tone by
    running

```
$ gst-launch-1.0 audiotestsrc ! audioconvert ! audioresample ! pulsesink
```

and see if you hear something. Make sure your volume is turned up, but
also make sure it is not too loud and you are not wearing your
headphones.

## How can I see what GStreamer plugins I have on my system?

To do this you use the gst-inspect command-line tool, which comes
standard with GStreamer. Invoked without any arguments,

```
$ gst-inspect-1.0
```

will print out a listing of installed plugins. To learn more about a
particular plugin, pass its name on the command line. For example,

```
$ gst-inspect-1.0 volume
```

will give you information about the volume plugin.

## Where should I report issues?

Issues are tracked in Freedesktop.org's Gitlab at
<https://gitlab.freedesktop.org/gstreamer>. Using Gitlab you can view past
issues, report new issues, submit merge requests etc. Gitlab requires you to
create an account there, which might seem cumbersome, but allows us to at least
have a chance at contacting you for further information, as we will often have
to do.

## How should I report bugs?

When doing a bug report, you should at least describe

  - your distribution, distribution version and GStreamer version

  - how you installed GStreamer (from git, source, packages, which?)

  - if you installed GStreamer before

If the application you are having problems with is segfaulting, then
provide us with the necessary gdb output. See
[???](#troubleshooting-segfault)

## How do I use the GStreamer command line interface?

You access the GStreamer command line interface using the command
`gst-launch-1.0`. For example, to play a file you could just use

```
gst-launch-1.0 playbin uri=file:///path/to/song.mp3
```

You can also use `gst-play`:

```
gst-play-1.0 song.mp3
```

To decode an mp3 audio file and play it through Pulseaudio, you could use:

```
gst-launch-1.0 filesrc location=thesong.mp3 ! mpegaudioparse ! mpg123audiodec ! audioconvert ! pulsesink
```

To automatically detect and select the right decoder for a given encoded stream
in a pipeline, try any of the following:

```
gst-launch-1.0 filesrc location=thesong.mp3 ! decodebin ! audioconvert ! pulsesink
```
```
gst-launch-1.0 filesrc location=my-random-media-file.mpeg ! decodebin ! pulsesink
```
```
gst-launch-1.0 filesrc location=my-random-media-file.mpeg ! decodebin ! videoconvert ! xvimagesink
```

Or even something more complicated like:

```
gst-launch-1.0 filesrc location=my-random-media-file.mpeg !decodebin name=decoder \
       decoder. ! queue ! videoconvert ! xvimagesink \
       decoder. ! queue ! audioconvert ! pulsesink
```

Building from the previous example, you can let GStreamer select an appropriate
set of default sinks by replacing the specific output elements with these automatic
alternatives:

```
gst-launch-1.0 filesrc location=my-random-media-file.mpeg !decodebin name=decoder \
       decoder. ! queue ! videoconvert ! autovideosink \
       decoder. ! queue ! audioconvert ! autoaudiosink
```

GStreamer also provides `playbin`, a basic media-playback plugin that
automatically takes care of most playback details. The following example shows
how to play any file as long as its format is supported, ie. you have the
necessary demuxing and decoding plugins installed:

```
gst-launch-1.0 playbin uri=file:///home/joe/my-random-media-file.mpeg
```

Additional examples can be found in the `gst-launch` manual page.
