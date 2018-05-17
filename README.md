gstreamer-sharp
=========

gstreamer-sharp is a .NET/mono binding for Gstreamer
generated from gobject-introspection data using the [bindinator].
gstreamer-sharp currently wraps the API exposed by Gstreamer 1.12
and is compatible with newer gstreamer versions. It was developed
under GSoC 2014 for the mono organization. gstreamer-sharp covers
the core and base gstreamer libraries.

Prerequisites
----
These libraries are needed for gstreamer-sharp to compile:
* gstreamer core, base and good 1.14 or higher
* [gtk-sharp] 3.22.0 or higher - *NOTE: This can be built as a meson subproject.*

You will also need various .NET/mono bits (mcs and al). On debian-based distros
you can install these with:

    sudo apt-get install mono-mcs mono-devel

Building & Installing
----

    meson build && ninja -C build/

HACKING
-------

While hacking on the code generator or the `.metadata` files, you will
need to force code regeneration with `ninja update-code`, a full rebuild
is triggered right after.

Updating to new GStreamer version
--------------------------------

Make sure you are in an environement where latest `.gir` files are available (either installed
or through the `$GI_TYPELIB_PATH` env var), those files are automatically copied to `girs/`.

    ninja -C update-all

* Verify newly copied gir files in `girs/` and `git add` them
* Verify newly generated code and `git add` files in `sources/generated/` and `ges/generated`
* Commit

Supported Platforms
----
* Linux
* Mac OS X

Quick Start
----
gstreamer-sharp provides ports of all samples from gst-docs in the samples folder.

Documentation
----

Since this is a gobject-introspection binding the recommended documentation is
the native [gstreamer] documentation. A monodoc generated documentation will be installed.

Roadmap
----
* Add an easy way to compile on Windows
* iOS and Android support
* Provide binaries for these platforms

License
----
gstreamer-sharp is licensed under the [LGPL 2.1](https://www.gnu.org/licenses/lgpl-2.1.html)

[bindinator]:https://github.com/GLibSharp/bindinator
[gtk-sharp]:https://github.com/GLibSharp/GtkSharp
[gstreamer]: http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/
