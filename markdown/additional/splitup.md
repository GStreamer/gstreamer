# GStreamer Plug-ins splitup

*Note :* The GStreamer plugins were split-up starting from GStreamer 0.10.

Here is some explanation regarding the split-up of plug-ins into separate
modules. Without further ado ...


## GStreamer - Hung by a Thread

Starring:

* **GSTREAMER**

  The core around which all other modules revolve. Base functionality and
  libraries, some essential elements, documentation, and testing.


* **BASE**

  A well-groomed and well-maintained collection of GStreamer plug-ins and
  elements, spanning the range of possible types of elements one would want to
  write for GStreamer.


And introducing, for the first time ever, on the development screen ...

* **THE GOOD**

  > *Such ingratitude. After all the times I've saved your life.*

  A collection of plug-ins you'd want to have right next to you on the
  battlefield. Shooting sharp and making no mistakes, these plug-ins have it
  all: good looks, good code, and good licensing. Documented and dressed up in
  tests. If you're looking for a role model to base your own plug-in on, here it
  is.

  If you find a plot hole or a badly lip-synced line of code in them, let us
  know - it is a matter of honour for us to ensure Blondie doesn't look like
  he's been walking 100 miles through the desert without water.


* **THE UGLY**

  > *When you have to shoot, shoot. Don't talk.*

  There are times when the world needs a color between black and white.  Quality
  code to match the good's, but two-timing, backstabbing and ready to sell your
  freedom down the river. These plug-ins might have a patent noose around their
  neck, or a lock-up license, or any other problem that makes you think twice
  about shipping them.

  We don't call them ugly because we like them less. Does a mother love her son
  less because he's not as pretty as the other ones? No - she commends him on
  his great personality. These plug-ins are the life of the party.  And we'll
  still step in and set them straight if you report any unacceptable behaviour -
  because there are two kinds of people in the world, my friend: those with a
  rope around their neck and the people who do the cutting.


* **THE BAD**

  > *That an accusation?*

  No perfectly groomed moustache or any amount of fine clothing is going to
  cover up the truth - these plug-ins are Bad with a capital B.  They look fine
  on the outside, and might even appear to get the job done, but at the end of
  the day they're a black sheep. Without a golden-haired angel to watch over
  them, they'll probably land in an unmarked grave at the final showdown.

  Don't bug us about their quality - exercise your Free Software rights, patch
  up the offender and send us the patch on the fastest steed you can steal from
  the Confederates. Because you see, in this world, there's two kinds of people,
  my friend: those with loaded guns and those who dig.  You dig.


## The Lowdown

> *I've never seen so many plug-ins wasted so badly.*

GStreamer Plugins has grown so big that it's hard to separate the wheat from
the chaff. Also, distributors have brought up issues about the legal status
of some of the plug-ins we ship. To remedy this, we've divided the previous
set of available plug-ins into four modules:


### gst-plugins-base

A small and fixed set of plug-ins, covering a wide range of possible types of
elements; these are continuously kept up-to-date with any core changes during
the development series.

* We believe distributors can safely ship these plug-ins
* People writing elements should base their code on these elements
* These elements come with examples, documentation, and regression tests


### gst-plugins-good

A set of plug-ins that we consider to have good quality code, correct
functionality, our preferred license (LGPL for the plug-in code, LGPL or
LGPL-compatible for the supporting library).

* We believe distributors can safely ship these plug-ins
* People writing elements should base their code on these elements


### gst-plugins-ugly

A set of plug-ins that have good quality and correct functionality, but
distributing them might pose problems. The license on either the plug-ins or the
supporting libraries might not be how we'd like. The code might be widely known
to present patent problems.

* Distributors should check if they want/can ship these plug-ins
* People writing elements should base their code on these elements


### gst-plugins-bad

A set of plug-ins that aren't up to par compared to the rest. They might be
close to being good quality, but they're missing something - be it a good code
review, some documentation, a set of tests, a real live maintainer, or some
actual wide use.

If the blanks are filled in they might be upgraded to become part of either
gst-plugins-good or gst-plugins-ugly, depending on the other factors.

* If the plug-ins break, you can't complain - instead, you can fix the problem
  and send us a patch, or bribe someone into fixing them for you
* New contributors can start here for things to work on
