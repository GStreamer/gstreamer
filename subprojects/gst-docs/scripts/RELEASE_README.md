# GStreamer documentation

This is the released version of the [GStreamer documentation](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gst-docs), it contains
two folders:

* html/: The static website documentation which can be hosted anywhere and
         read in any web browser.
* devhelp/: The documentation to be browsed with [devhelp](https://wiki.gnome.org/Apps/Devhelp).
            The content of that folder should be installed in `/usr/share/gtk-doc/html/GStreamer-@GST_API_VERSION@/`
            by documentation packages.

## Disk usage considerations

Both folders contain a "search" folder made up of a large amount
of very small files (50 K+). This can cause the unpacked size of both
these folders to grow up to 500 MB+ on filesystems that weren't
configured for such a use case (eg `mkfs.ext4 -Tnews`).

It is safe to remove these search folders, this will simply cause
the search box to be hidden when viewing the documentation.

If packages are produced both for the devhelp and html folders,
one may choose to remove the search folders for devhelp, as
devhelp exposes a (more limited) search feature, and keep the
search folder in the html package, so that users of the package
can replicate the behaviour of the online documentation with
the offline version.

Choosing to strip the search folders in both, either or neither
package is ultimately left to the packagers' discretion, users
of most distributions should usually have enough disk space
to accomodate these.

## Licensing

The content of this module comes from a number of different sources and is
licensed in different ways:

### Tutorial source code

All tutorial code is licensed under any of the following licenses (your choice):

 - 2-clause BSD license ("simplified BSD license") (`LICENSE.BSD`)
 - MIT license (`LICENSE.MIT`)
 - LGPL v2.1 (`LICENSE.LGPL-2.1`), or (at your option) any later version

This means developers have maximum flexibility and can pick the right license
for any derivative work.

### Application Developer Manual and Plugin Writer's Guide

These are licensed under the [Open Publication License v1.0][op-license]
(`LICENSE.OPL`), for historical reasons.

[op-license]: http://www.opencontent.org/openpub/

### Documentation

#### Tutorials

The tutorials are licensed under the [Creative Commons CC-BY-SA-4.0 license][cc-by-sa-4.0]
(`LICENSE.CC-BY-SA-4.0`).

[cc-by-sa-4.0]: https://creativecommons.org/licenses/by-sa/4.0/

#### API Reference and Design Documentation

The remaining documentation, including the API reference and Design Documentation,
is licensed under the LGPL v2.1 (`LICENSE.LGPL-2.1`), or (at your option) any later
version.
