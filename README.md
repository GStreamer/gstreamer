# gst-build

GStreamer [meson](http://mesonbuild.com/) based repositories aggregrator

You can build GStreamer and all its modules at once using
meson and its [subproject](https://github.com/mesonbuild/meson/wiki/Subprojects) feature.

## Getting started

### Install meson and ninja

You should get meson through your package manager or using:

  $ pip3 install --user meson

You should get `ninja` using your package manager or downloading it from
[here](https://github.com/ninja-build/ninja/releases).

### Build GStreamer and its modules

You can get all GStreamer built running:

```
mkdir build/ && meson build && ninja -C build/
```

NOTE: on fedora (and maybe other distributions) replace `ninja` with `ninja-build`

# Development environment

gst-build also contains a special `uninstalled` target that lets you enter an
uninstalled development environment where you will be able to work on GStreamer easily.
You can get into that environment running:

```
ninja -C build/ uninstalled
```

If your operating system handles symlinks, built modules source code will be available
at the root of `gst-build/` for example GStreamer core will be in `gstreamer/`. Otherwise
they will be present in `subprojects/`. You can simply hack in there and to rebuild you
just need to rerun `ninja -C build/`.
