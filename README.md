# gst-all

GStreamer meson based repositories aggregrator

You can build GStreamer and all its component at once using
meson and its "subproject" feature.

## GStreamer uninstalled

gst-all also contains a special `uninstalled` target that lets you enter
an uninstalled development environment where you will be able
to work on GStreamer easily.

Inside that the environment you will find the GStreamer module
in subprojects/, you can simply hack in there and to rebuild you
just need to rerun ninja.

## Build a project based on GStreamer

You can make your own project that uses GStreamer and all its
components depend on `gst-all` making it Meson subproject
of your own project.
