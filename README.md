# gst-all

GStreamer [meson](http://mesonbuild.com/) based repositories aggregrator

You can build GStreamer and all its component at once using
meson and its "subproject" feature.

## Getting started

We have an helper script to get started, will get the right [meson](http://mesonbuild.com/)
version and get you ready to build. You can just get all GStreamer built running:

```
./configure && ninja -C build/
```

## GStreamer uninstalled

gst-all also contains a special `uninstalled` target that lets you enter
an uninstalled development environment where you will be able
to work on GStreamer easily.

Inside that environment you will find the GStreamer modules
in subprojects/, you can simply hack in there and to rebuild you
just need to rerun `ninja`.

## Build a project based on GStreamer

You can make your own project that uses GStreamer and all its
components depend on `gst-all` making it Meson subproject
of your own project.
