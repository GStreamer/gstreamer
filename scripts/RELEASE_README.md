# GStreamer documentation

This is the released version of the [GStreamer documentation](https://gitlab.freedesktop.org/gstreamer/gst-docs), it contains
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
