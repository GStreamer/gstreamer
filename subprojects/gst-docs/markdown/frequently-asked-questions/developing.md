# Developing applications with GStreamer

## How do I compile programs that use GStreamer?

<!-- FIXME: update for windows, macOS, and meson build, get rid of libtool things -->

This depends all a bit on what your development environment and target
operating system is. The following is mostly aimed at Linux/unix setups.

GStreamer uses the `pkg-config` utility to provide applications with the right
compiler and linker flags. `pkg-config` is a standard build tool that is widely
used in unix systems to locate libraries and retrieve build settings. If
you're already familiar with it, then you're basically set.

If you're not familiar with `pkg-config`, to compile and link a small
one-file program, pass the `--cflags` and `--libs` arguments to `pkg-config`.
The following should be sufficient for a gstreamer-only program:

```
$ libtool --mode=link gcc `pkg-config --cflags --libs gstreamer-1.0` -o myprog myprog.c
```

If your application also used GTK+ 3.0, you could use

```
$ libtool --mode=link gcc `pkg-config --cflags --libs gstreamer-1.0 gtk+-3.0` -o myprog myprog.c
```

Those are back-ticks (on the same key with the tilde on US keyboards),
not single quotes.

For bigger projects, you should integrate `pkg-config` use in your
Makefile, or with autoconf using the pkg.m4 macro (providing
`PKG_CONFIG_CHECK`).

## How do I develop against a GStreamer copy within a development environment?

It is possible to develop and compile against a copy of GStreamer and its
plugins within a development environment, for example, against git checkouts.
This enables you to test the latest version of GStreamer without interfering
with your system-wide installation. See the [Building from source using
meson](installing/building-from-source-using-meson.md) documentation.


## How can I use GConf to get the system-wide defaults?

<!-- FIXME: Consider removing. GConf was deprecated half a decade ago -->

GStreamer used to have GConf-based elements but these were removed in 2011,
after `GConf` itself was deprecated in favor of `GSettings`.

If what you want is automatic audio/video sinks, consider using the
`autovideosink` and `autoaudiosink` elements.

## How do I debug these funny shell scripts that libtool makes?

When you link a program against a GStreamer within a development environment
using libtool, funny shell scripts are made to modify your shared object search
path and then run your program. For instance, to debug `gst-launch`, try:

```
libtool --mode=execute gdb /path/to/gst-launch
```

If this does not work, you're probably using a broken version of
libtool.

If you build GStreamer using the Meson build system, libtool will not
be used and this is not a problem. You can run `gdb`, `valgrind` or any
debugging tools directly on the binaries Meson creates in the build
directory.

## Why is mail traffic so low on gstreamer-devel?

Our main arenas for coordination and discussion are IRC and Gitlab, not
the mailing lists. Join us in [`#gstreamer`][irc-gstreamer] on irc.oftc.net.
There is also a [webchat interface][webchat-gstreamer]. For larger picture
questions or getting more input from more people, a mail to the gstreamer-devel
mailing list is never a bad idea, however.

[irc-gstreamer]: irc://irc.oftc.net/#gstreamer
[webchat-gstreamer]: https://webchat.oftc.net/?channels=%23gstreamer

## What kind of versioning scheme does GStreamer use?

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

Basically, the core and almost all plugin modules use K\&R with 2-space
indenting. Just follow what's already there and you'll be fine. We only require
code files to be indented, header may be indented manually for better
readability. Please use spaces for indenting, not tabs, even in header files.

Individual plugins in gst-plugins-\* or plugins that you want considered
for addition to these modules should use the same style. It's easier if
everything is consistent. Consistency is, of course, the goal.

One way to make sure you are following our coding style is to run your code
(remember, only the `*.c` files, not the headers) through GNU Indent using the
following options:

```
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
```

There is also a `gst-indent` script in the GStreamer core source tree in the
tools directory which wraps GNU Indent and uses the right options.

The easiest way to get the indenting right is probably to develop against a git
checkout. The local git commit hook will ensure correct indentation.

Comments should be in `/* ANSI C comment style */` and code should generally
be compatible with ANSI C89, so please declare all variables at the beginning
of the block, etc.

Merge requests should ideally be made against git master or a recent release.
Please don't send patches to the mailing list. They will likely get lost there.

See [How to submit patches][submit-patches] for more details.

[submit-patches]: contribute/index.md#how-to-submit-patches

## How do I get my translations included?

I have translated one of the module .po files into a new language. How do I get it included?

GStreamer translations are uniformly managed through the
[Translation Project](http://translationproject.org). There are some
instructions on how to join the Translation Project team and submit new
translations at http://translationproject.org/html/translators.html.

New translations submitted via the Translation Project are merged
periodically into git by the maintainers by running `make download-po`
in the various modules when preparing a new release.

We don't merge new translations or translation fixes directly, everything
must go via the Translation Project.
