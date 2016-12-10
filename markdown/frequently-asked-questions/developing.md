# Developing applications with GStreamer

## How do I compile programs that use GStreamer ?

<!-- FIXME: update for windows, macOS, and meson build, get rid of libtool things -->

This depends all a bit on what your development environment and target
operating systems is. The following is mostly aimed at Linux/unix setups.

GStreamer uses the `pkg-config` utility to provide applications with the right
compiler and linker flags. `pkg-config` is a standard build tool that is widely
used unix systems to locate libraries and retrieve build settings, so if you're
familiar with using it already then you're basically set.

If you're not familiar with `pkg-config` to compile and link a small
one-file program, pass the `--cflags` and `--libs` arguments to `pkg-config`.
For
    example:

    $ libtool --mode=link gcc `pkg-config --cflags --libs gstreamer-1.0` -o myprog myprog.c

would be sufficient for a gstreamer-only program. If (for example) your
application also used GTK+ 3.0, you could use

    $ libtool --mode=link gcc `pkg-config --cflags --libs gstreamer-1.0 gtk+-3.0` -o myprog myprog.c

Those are back-ticks (on the same key with the tilde on US keyboards),
not single quotes.

For bigger projects, you should integrate pkg-config use in your
Makefile, or integrate with autoconf using the pkg.m4 macro (providing
`PKG_CONFIG_CHECK`).

## How do I develop against an uninstalled GStreamer copy ?

It is possible to develop and compile against an uninstalled copy
of gstreamer and gst-plugins-\* (for example, against git checkouts).
This allows you to develop against and test the latest GStreamer version
without having to install it and without interfering with your
system-wide GStreamer setup.

There are two ways to achieve such a setup:

1. [`gst-build`][gst-build] is our new meta-build module based on the
   [Meson build system][meson]. This is the shiny new thing. It's fast and
   simple to get started with, but you will need a recent version of Meson
   installed. Just check out the git repository and run the `setup.py` script.
   Once the initial meson configure stage has passed, you can enter an
   uninstalled environment by running `ninja uninstalled` in the build
   directory. This will make sure tools and plugin from the uninstalled build
   tree will be used. Any problems, let us know.

2. [`gst-uninstalled`][gst-uninstalled] is our traditional autotools-
   and libtool-based build setup. The easiest way too create such a setup
   is using the [latest version of the `create-uninstalled-setup.sh`
   script][create-uninstalled]. This setup makes use of the [latest version of
   the `gst-uninstalled` script][gst-uninstalled]. Running this script, you'll
   be in an environment where the uninstalled tools and plugins will be used by
   default. Also, `pkg-config` will detect the uninstalled copies before (and
   prefer them to) any installed copies.

Multiple uninstalled setups can be used in parallel, e.g. one for the
latest stable branch and one for git master. Have a look at the
[gst-uninstalled][gst-uninstalled] script to see how it determines which
environment is used.

[gst-build]: https://cgit.freedesktop.org/gstreamer/gst-build/
[meson]: http://mesonbuild.com
[gst-uninstalled]: http://cgit.freedesktop.org/gstreamer/gstreamer/tree/scripts/gst-uninstalled
[create-uninstalled]: http://cgit.freedesktop.org/gstreamer/gstreamer/tree/scripts/create-uninstalled-setup.sh

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

If you build GStreamer using the Meson build system, libtool will not
be used and this is not a problem. You can run `gdb`, `valgrind` or any
debugging tools directly on the binaries Meson creates in the build
directory.

## Why is mail traffic so low on gstreamer-devel ?

Our main arena for coordination and discussion are IRC and bugzilla, not
mailing lists. Join us in [`#gstreamer`][irc-gstreamer] on irc.freenode.net.
There is also a [webchat interface][webchat-gstreamer]. For larger picture
questions or getting more input from more people, a mail to the gstreamer-devel
mailing list is never a bad idea, however.

[irc-gstreamer]: irc://irc.freenode.net/#gstreamer
[webchat-gstreamer]: https://webchat.freenode.net

## What kind of versioning scheme does GStreamer use ?

For public releases, GStreamer uses a standard MAJOR.MINOR.MICRO
version scheme. If the release consists of mostly bug fixes or
incremental changes, the MICRO version is incremented. If the release
contains big changes, the MINOR version is incremented. A change in the
MAJOR version indicates incompatible API or ABI changes, which happens
very rarely (the last one dates back to 2012). This is also known as
[semantic versioning](http://semver.org).

Even MINOR numbers indicate *stable releases*: 1.0.x, 1.2.x, 1.4.x, 1.6.x,
1.8.x, and 1.10.x are our stable release series. Odd MINOR numbers are used
for *unstable development releases* and *prereleases* which should only be
used temporarily for testing; your help in testing these tarballs and packages
is very much appreciated!

During the development cycle, GStreamer also uses a fourth or NANO
number. If this number is 1, then it's a git development version. Any
tarball or package that has a nano number of 1 is made from git and thus
not supported. Additionally, if you didn't get this package or tarball
from the GStreamer team, don't have high hopes on it doing whatever you
want it to do.

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
`gst-indent` script in the GStreamer core source tree in the tools
directory which wraps this and contains the latest option. The easiest
way to get the indenting right is probably to develop against a git
checkout. The local git commit hook will ensure correct indentation. We
only require code files to be indented, header files may be indented
manually for better readability (however, please use spaces for
indenting, not tabs, even in header files).

Comments should be in `/* ANSI C comment style */` and code should generally
be compatible with ANSI C89, so please declare all variables at the beginning
of the block etc.

Patches should ideally be made against git master or a recent release and
should be created using `git format-patch` format. They should then be
attached individually to a bug report or feature request in
[bugzilla](http://bugzilla.gnome.org). Please don't send patches to the
mailing list, they will likely get lost there.

See [How to submit patches][submit-patches] for more details.

[submit-patches]: contribute/index.md#how-to-submit-patches

## I have translated one of the module .po files into a new language. How do I get it included?

GStreamer translations are uniformly managed through the
[Translation Project](http://translationproject.org). There are some
instructions on how to join the Translation Project team and submit new
translations at http://translationproject.org/html/translators.html.

New translations submitted via the Translation Project are merged
periodically into git by the maintainers by running `make download-po`
in the various modules when preparing a new release.

We won't merge new translations or translation fixes directly, everything
must go via the Translation Project.
