Building GStreamer on Windows
-----------------------------

Running GStreamer on Windows is currently experimental, but improving.

Building on MinGW/MSys
----------------------
Should work out of the box from the toplevel directory using the standard
Unix build system provided.

This build type is fairly well supported.

Building with Visual Studio 6
-----------------------------
The directory vs6/ contains the workspaces needed to build GStreamer from
Visual Studio.

This build type is fairly well supported.

Building with Visual Studio 7
-----------------------------
vs7/ contains the files needed, but they haven't been updated since the
0.8 series.

This build is currently unsupported.

The common/ directory contains support files that can be shared between
these two versions of Visual Studio.
