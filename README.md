gstreamer-sharp
=========

gstreamer-sharp is a .NET/mono binding for Gstreamer generated from gobject-introspection data using the [bindinator]. gstreamer-sharp currently wraps the API exposed by Gstreamer 1.4 and is compatible with newer gstreamer versions. It was developed under GSoC 2014 for the mono organization.
gstreamer-sharp covers the core and base gstreamer libraries.

Prerequisites
----
These libraries are needed for clutter-sharp to compile:
* [gtk-sharp] 2.99.4 or higher
* gstreamer core, base and good 1.4 or higher

Building & Installing
----
Simply type ./autogen.sh --prefix=/usr && make install

Supported Platforms
----
* Linux
* Mac OS X

Quick Start
----
gstreamer-sharp provides ports of all samples from the gstreamer SDK in the samples folder. 

Documentation
----
Since this is a gobject-introspection binding the recommended documentation is the native [gstreamer](http://gstreamer.freedesktop.org/data/doc/gstreamer/head/gstreamer/html/) documentation. A monodoc generated documentation will be installed.

Roadmap
----
* Add an easy way to compile on Windows
* iOS and Android support
* Provide binaries for these platforms

License
----
gstreamer-sharp is licensed under the [LGPL 2.1](https://www.gnu.org/licenses/lgpl-2.1.html)

[bindinator]:https://github.com/andreiagaita/bindinator/
[gtk-sharp]:https://github.com/mono/gtk-sharp

