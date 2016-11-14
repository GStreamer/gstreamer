Building GStreamer on Windows
-----------------------------

Running GStreamer on Windows is supported.

Official Windows binaries for each release can be found at:

  https://gstreamer.freedesktop.org/data/pkg/windows/


Building with MinGW/MSys
------------------------

Should work out of the box from the toplevel directory using the standard
Unix build system provided.

This build type is officially supported.

You can build Windows binaries including all required dependencies
using the 'cerbero' build tool:

  http://cgit.freedesktop.org/gstreamer/cerbero/

This works both natively on Windows or as cross-compile from Linux.


Building with Visual Studio
---------------------------

Building with Visual Studio is possible using the Meson-based build
definitions, but there is currently no support for this in cerbero yet,
so it's not for the faint-hearted.
