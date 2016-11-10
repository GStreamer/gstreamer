# Using GStreamer

## Ok, I've installed GStreamer. What can I do next ?

First of all, verify that you have a working registry and that
you can inspect it by typing

    $ gst-inspect-1.0 fakesrc

This should print out a bunch of information about this particular
element. If this tells you that there is "no such element or plugin",
you haven't installed GStreamer correctly. Please check [how to get
GStreamer](#chapter-getting) If this fails with any other message, we
would appreciate a [bug report](#using-bugs-where).

It's time to try out a few things. Start with gst-launch and two
plug-ins that you really should have : fakesrc and fakesink. They do
nothing except pass empty buffers. Type this at the command-line
    :

    $ gst-launch-1.0 -v fakesrc silent=0 num-buffers=3 ! fakesink silent=0

This will print out output that looks similar to this :

    RUNNING pipeline ...
    fakesrc0: last-message = "get      ******* (fakesrc0:src)gt; (0 bytes, 0) 0x8057510"
    fakesink0: last-message = "chain   ******* (fakesink0:sink)lt; (0 bytes, 0) 0x8057510"
    fakesrc0: last-message = "get      ******* (fakesrc0:src)gt; (0 bytes, 1) 0x8057510"
    fakesink0: last-message = "chain   ******* (fakesink0:sink)lt; (0 bytes, 1) 0x8057510"
    fakesrc0: last-message = "get      ******* (fakesrc0:src)gt; (0 bytes, 2) 0x8057510"
    fakesink0: last-message = "chain   ******* (fakesink0:sink)lt; (0 bytes, 2) 0x8057510"
    execution ended after 5 iterations (sum 301479000 ns, average 60295800 ns, min 3000 ns, max 105482000 ns)

(Some parts of output have been removed for clarity) If it looks
similar, then GStreamer itself is running correctly.

## Can my system play sound through GStreamer ?

You can test this by trying to play a sine tone. For this, you
need to link the audiotestsrc element to an output element that matches
your hardware. A (non-complete) list of output plug-ins for audio is

  - pulsesink for Pulseaudio output

  - osssink for OSS output

  - esdsink for ESound output

  - alsasink for ALSA output

  - alsaspdifsink for ALSA S/PDIF output

  - jackaudiosink for JACK output

First of all, run gst-inspect-1.0 on the output plug-in you want to use
to make sure you have it installed. For example, if you use Pulseaudio,
run

    $ gst-inspect-1.0 pulsesink

and see if that prints out a bunch of properties for the plug-in.

Then try to play the sine tone by
    running

    $ gst-launch-1.0 audiotestsrc ! audioconvert ! audioresample ! pulsesink

and see if you hear something. Make sure your volume is turned up, but
also make sure it is not too loud and you are not wearing your
headphones.

In GNOME, you can configure audio output for most applications by
running

    $ gstreamer-properties

which can also be found in the start menu (Applications -\> Preferences
-\> Multimedia Systems Selector). In KDE, there is not yet a shared way
of setting audio output for all applications; however, applications such
as Amarok allow you to specify an audio output in their preferences
dialog.

## How can I see what GStreamer plugins I have on my system ?

To do this you use the gst-inspect command-line tool, which comes
standard with GStreamer. Invoked without any arguments,

    $ gst-inspect-1.0

will print out a listing of installed plugins. To learn more about a
particular plugin, pass its name on the command line. For example,

    $ gst-inspect-1.0 volume

will give you information about the volume plugin.

## Where should I report bugs ?

Bug management is now hosted on GNOME's Bugzilla at
<http://bugzilla.gnome.org>, under the product GStreamer. Using bugzilla
you can view past bug history, report new bugs, etc. Bugzilla requires
you to make an account here, which might seem cumbersome, but allows us
to at least have a chance at contacting you for further information, as
we will most likely have to.

## How should I report bugs ?

When doing a bug report, you should at least describe

  - your distribution

  - how you installed GStreamer (from git, source, packages, which ?)

  - if you installed GStreamer before

If the application you are having problems with is segfaulting, then
provide us with the necessary gdb output. See
[???](#troubleshooting-segfault)

## How do I use the GStreamer command line interface ?

You access the GStreamer command line interface using the command
gst-launch. To decode an mp3 and play it through Pulseaudio, you could
use

    gst-launch-1.0 filesrc location=thesong.mp3 ! mad ! pulsesink

. More examples can be found in the gst-launch man page.

To automatically detect the right codec in a pipeline,
    try

    gst-launch-1.0 filesrc location=my-random-media-file.mpeg ! decodebin !
     pulsesink

.
    or

    gst-launch-1.0 filesrc location=my-random-media-file.mpeg ! decodebin !
     videoconvert ! xvimagesink

Something more
    complicated:

    gst-launch-1.0 filesrc location=my-random-media-file.mpeg ! decodebin name=decoder
       decoder. ! videoconvert ! xvimagesink
       decoder. ! audioconvert ! pulsesink

We also have a basic media playing plugin that will take care of most
things for you. This plugin is called playbin. Try
    this:

    gst-launch-1.0 playbin uri=file:///home/joe/my-random-media-file.mpeg

This should play the file if the format is supported, ie. you have all
the necessary demuxing and decoding and some output plugins installed.
