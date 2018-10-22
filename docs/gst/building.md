---
title: Building GStreamer and GStreamer Applications
short-description: How to build the GStreamer framework and applications using it.
...

# Building GStreamer on UNIX

On UNIX, GStreamer uses the standard GNU build system, using autoconf
for package configuration and resolving portability issues, automake for
building makefiles that comply with the GNU Coding Standards, and
libtool for building shared libraries on multiple platforms. The normal
sequence for compiling and installing the GStreamer library is thus:
`./configure` `make` `make install`

The standard options provided by GNU autoconf may be passed to the
`configure` script. Please see the autoconf documentation or run
`./configure --help` for information about the standard options.

In addition there are several options to activate or deactivate
features. E.g. passing `--disable-gst-debug` to `configure` will turn
the debugging subsystem into a non-functional stub and remove all macro
based invocations from within the library (and anything compiled against
the library afterwards.)

If library size matters and one builds in a controlled environment, it
is also possible to totally remove subsystem code. This is intentionally
not offered as a configure option as it causes an ABI break. Code built
against a version of GStreamer without these modifications needs to be
recompiled.
`make CFLAGS="-DGST_REMOVE_DEPRECATED -DGST_REMOVE_DISABLED"`

-   `GST_REMOVE_DEPRECATED` - Omit deprecated functions from the
    library.

-   `GST_REMOVE_DISABLED` - Omit stubs for disabled subsystems from the
    library.

# Building GStreamer Applications

Applications and libraries can use `pkg-config` to get all the needed
compiler and linker flags to build against GStreamer. Please note that
GStreamer is split into several libraries itself.
`pkg-config --list-all | grep gstreamer` will list the available
libraries.
