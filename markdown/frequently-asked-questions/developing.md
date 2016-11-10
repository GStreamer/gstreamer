# Developing applications with GStreamer

## How do I compile programs that use GStreamer ?

GStreamer uses pkg-config to assist applications with compilation
and linking flags. pkg-config is already used by GTK+, GNOME, SDL, and
others; so if you are familiar with using it for any of those, you're
set.

If you're not familiar with pkg-config to compile and link a small
one-file program, pass the --cflags and --libs arguments to pkg-config.
For
    example:

    $ libtool --mode=link gcc `pkg-config --cflags --libs gstreamer-GST_API_VERSION` -o myprog myprog.c

would be sufficient for a gstreamer-only program. If (for example) your
app also used GTK+ 2.0, you could
    use

    $ libtool --mode=link gcc `pkg-config --cflags --libs gstreamer-GST_API_VERSION gtk+-2.0` -o myprog myprog.c

Those are back-ticks (on the same key with the tilde on US keyboards),
not single quotes.

For bigger projects, you should integrate pkg-config use in your
Makefile, or integrate with autoconf using the pkg.m4 macro (providing
PKG\_CONFIG\_CHECK).

## How do I develop against an uninstalled GStreamer copy ?

It is possible to develop and compile against an uninstalled copy
of gstreamer and gst-plugins-\* (for example, against git checkouts).
This allows you to develop against and test the latest GStreamer version
without having to install it and without interfering with your
system-wide GStreamer setup.

The easiest way too create such a setup is the [latest version of
create-uninstalled-setup.sh](http://cgit.freedesktop.org/gstreamer/gstreamer/tree/scripts/create-uninstalled-setup.sh)

This setup makes use of the [latest version of
gst-uninstalled](http://cgit.freedesktop.org/gstreamer/gstreamer/tree/scripts/gst-uninstalled).
Running this script, you'll be in an environment where the uninstalled
tools and plugins will be used by default. Also, pkg-config will detect
the uninstalled copies before (and prefer them to) any installed copies.

Multiple uninstalled setups can be used in parallel. Have a look at
[gst-uninstalled](http://cgit.freedesktop.org/gstreamer/gstreamer/tree/scripts/gst-uninstalled)
to see how it determines which environment is used.

## How can I use GConf to get the system-wide defaults ?

For GNOME applications it's a good idea to use GConf to find the
default ways of outputting audio and video. You can do this by using the
'gconfaudiosink' and 'gconfvideosink' elements for audio and video
output. They will take care of everything GConf-related for you and
automatically use the outputs that the user configured. If you are using
gconfaudiosink, your application should set the 'profile' property.

## How do I debug these funny shell scripts that libtool makes ?

When you link a program against uninstalled GStreamer using
libtool, funny shell scripts are made to modify your shared object
search path and then run your program. For instance, to debug
gst-launch, try

    libtool --mode=execute gdb /path/to/gst-launch

. If this does not work, you're probably using a broken version of
libtool.

## Why is mail traffic so low on gstreamer-devel ?

Our main arena for coordination and discussion is IRC, not email.
Join us in [\#gstreamer on
irc.freenode.net](irc://irc.freenode.net/#gstreamer) For larger picture
questions or getting more input from more persons, a mail to
gstreamer-devel is never a bad idea.

## What kind of versioning scheme does GStreamer use ?

For public releases, GStreamer uses a standard MAJOR.MINOR.MICRO
version scheme. If the release consists of mostly bug fixes or
incremental changes, the MICRO version is incremented. If the release
contains big changes, the MINOR version is incremented. If we're
particularly giddy, we might even increase the MAJOR number. Don't hold
your breath for that though.

During the development cycle, GStreamer also uses a fourth or NANO
number. If this number is 1, then it's a git development version. Any
tarball or package that has a nano number of 1 is made from git and thus
not supported. Additionally, if you didn't get this package or tarball
from the GStreamer team, don't have high hopes on it doing whatever you
want it to do.

If the number is 2 or higher, it's an official pre-release in
preparation of an actual complete release. Your help in testing these
tarballs and packages is very much appreciated.

## What is the coding style for GStreamer code?

The core and almost all plugin modules are basically coded in
K\&R with 2-space indenting. Just follow what's already there and you'll
be fine.

Individual plugins in gst-plugins-\* or plugins that you want considered
for addition to one of the gst-plugins-\* modules should be coded in the
same style. It's easier if everything is consistent. Consistency is, of
course, the goal.

Simply run your code (only the \*.c files, not the header files) through

    indent \
      --braces-on-if-line \
      --case-brace-indentation0 \
      --case-indentation2 \
      --braces-after-struct-decl-line \
      --line-length80 \
      --no-tabs \
      --cuddle-else \
      --dont-line-up-parentheses \
      --continuation-indentation4 \
      --honour-newlines \
      --tab-size8 \
      --indent-level2

before submitting a patch. (This is using GNU indent.) There is also a
gst-indent script in the GStreamer core source tree in the tools
directory which wraps this and contains the latest option. The easiest
way to get the indenting right is probably to develop against a git
checkout. The local git commit hook will ensure correct indentation. We
only require code files to be indented, header files may be indented
manually for better readability (however, please use spaces for
indenting, not tabs, even in header files).

Where possible, we try to adhere to the spirit of GObject and use
similar coding idioms.

Patches should be made against git master or the latest release and
should be in 'unified context' format (use diff -u -p). They should be
attached to a bug report (or feature request) in
[bugzilla](http://bugzilla.gnome.org) rather than sent to the mailing
list.

## I have translated one of the module .po files into a new language. How do I get it included?

GStreamer translations are uniformly managed through the
Translation Project (http://translationproject.org). There are some
instructions on how to join the Translation Project team and submit new
translations at http://translationproject.org/html/translators.html.

New translations submitted via the Translation Project are merged
periodically into git by the maintainers by running 'make download-po'
in the various modules.
