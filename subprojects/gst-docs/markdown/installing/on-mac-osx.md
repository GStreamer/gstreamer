#  Installing on Mac OS X

## Supported platforms

 *  10.6 (Snow Leopard)
 *  10.7 (Lion)
 *  10.8 (Mountain Lion)
 *  10.9 (Mavericks)
 *  10.10 (Yosemite)
 *  10.11 (El Capitan)

## Prerequisites

To develop applications using the GStreamer SDK for OS X you will need
OS X Snow Leopard (10.6) or later and
[XCode 3.2.6](https://developer.apple.com/devcenter/mac/index.action) or
later.

The recommended system is [macOS Sierra](http://www.apple.com/macosx/) with
[XCode 8](https://developer.apple.com/xcode/)

## Download and install the SDK

There are 3 sets of files in the SDK:

  - The runtime files are needed to run GStreamer applications. You
    probably want to distribute these files with your application (or
    the installer below).
  - The development files are **additional** files you need to create
    GStreamer applications.
  - Mac OS X packages that you can use
    with [PackageMaker](https://developer.apple.com/library/mac/#documentation/DeveloperTools/Conceptual/PackageMakerUserGuide/Introduction/Introduction.html)
    to deploy GStreamer with your application

Get **both the runtime and the development installers** from
the [GStreamer download page](https://gstreamer.freedesktop.org/download/#macos)
and **please install both of them**:
 - The runtime installer is e.g. `gstreamer-1.0-{VERSION}-x86_64.pkg`, and
 - The development installer is e.g. `gstreamer-1.0-devel-{VERSION}-x86_64.pkg`

> ![Warning](images/icons/emoticons/warning.svg)
> On Mac OS X 10.6 (Snow Leopard) you have to install Python 2.7 manually. It is included in later versions of OS X already. You can get it from [here](http://www.python.org/getit).

The downloads are
[Installer Packages
(.pkg)](http://en.wikipedia.org/wiki/Installer_%28Mac_OS_X%29).

Double click the package file and follow the instructions presented by
the install wizard. In case the system complains about the package not
being signed, you can control-click it and open to start the
installation. When you do this, it will warn you, but there is an option
to install anyway. Otherwise you can go to System Preferences → Security
and Privacy → General and select the option to allow installation of
packages from "anywhere".


These are some paths of the GStreamer framework that you might find
useful:

  - /Library/Frameworks/GStreamer.framework/: Framework's root path
  - /Library/Frameworks/GStreamer.framework/Versions: path with all the
    versions of the framework
  - /Library/Frameworks/GStreamer.framework/Versions/Current: link to
    the current version of the framework
  - /Library/Frameworks/GStreamer.framework/Headers: path with the
    development headers
  - /Library/Frameworks/GStreamer.framework/Commands: link to the
    commands provided by the framework, such as gst-inspect-1.0 or
    gst-launch-1.0

For more information on OS X Frameworks anatomy, you can consult the
following [link](https://developer.apple.com/library/mac/#documentation/MacOSX/Conceptual/BPFrameworks/Concepts/FrameworkAnatomy.html)

## Configure your development environment

### Building the tutorials

The tutorials code, along with project files and a solution file for
them all, are in the
[gst-docs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gst-docs) in the
`examples/tutorials` subdirectory.

To start building the tutorials, create a new folder in your Documents
directory and copy the
folder `/Library/Frameworks/GStreamer.framework/Current/share/gst-sdk/tutorials`.

You can fire up XCode and load the project file.

Press the **Run** button to build and run the first tutorial. You can
switch the tutorial to build selecting one of the available schemes.

### Creating new projects

#### XCode

The GStreamer binaries provide a
[framework](https://developer.apple.com/library/mac/#documentation/MacOSX/Conceptual/BPFrameworks/Tasks/IncludingFrameworks.html)
that you can drag and drop to XCode to start using it. There is a small
exception to the regular use of frameworks, and you will need to manually
include the headers (`/Library/Frameworks/GStreamer.framework/Headers`) and
libraries (`/Library/Frameworks/GStreamer.framework/Libraries`) search path. In
XCode you will need to do the following:

  - Add `GStreamer.framework` to **General → Frameworks and Libraries**
  - Add the libraries path to **Build Settings → Library Search Paths**
  - Add the headers path to **Build Settings → System Header Search Paths**
  - Disable hardened runtime **Build Settings → Enable Hardened Runtime**. This
    is needed because the `GStreamer.framework` is not signed.

#### Manual compilation

If instead of XCode you use GCC (or CLANG) directly you can follow a similar
approach by providing the header and libraries search path to the compiler and
linker. Here's a very simple example to show you how:

Let's say we have a file `main.c` that requires GStreamer and looks like this:

```
#include <gst/gst.h>

int
main(int argc, char *argv[])
{
  gst_init(NULL, NULL);

  return 0;
}
```

We can compile it and link it with the following commands:

```
# Compile
$ clang -c main.c -o main.o -I/Library/Frameworks/GStreamer.framework/Headers

# Link
$ clang -o main main.o -L/Library/Frameworks/GStreamer.framework/Libraries -F/Library/Frameworks -framework GStreamer
```

Note how we use `-I/Library/Frameworks/GStreamer.framework/Headers` to specify
the headers search path (same as with XCode) and in the linking step we specify
`-L/Library/Frameworks/GStreamer.framework/Libraries` to indicate the libraries
search path (as we also did in XCode), `-F/Library/Frameworks` to tell the
linker where to find frameworks and finally `-framework GStreamer` to specify
the GStreamer framework.

Finally, we can even inspect the generated executable and verify it's pointing
to our GStreamer framework:

```
$ otool -L main
main:
        @rpath/GStreamer.framework/Versions/1.0/lib/GStreamer (compatibility version 0.0.0, current version 0.0.0)
        /usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1311.100.3)
```

#### Manual compilation (with pkg-config)

The `GStreamer.framework` also comes with some developer tools such as
`pkg-config`. `pkg-config` is a tool used to query what compiler and linker
flags an application requires if they want to use a certain library. So we will
now build the same example we used above with `pkg-config` and asking for the
required GStreamer flags.

```
# Tell pkg-config where to find the .pc files
$ export PKG_CONFIG_PATH=/Library/Frameworks/GStreamer.framework/Versions/1.0/lib/pkgconfig

# We will use the pkg-config provided by the GStreamer.framework
$ export PATH=/Library/Frameworks/GStreamer.framework/Versions/1.0/bin:$PATH

# Compile
$ clang -c main.c -o main.o `pkg-config --cflags gstreamer-1.0`

# Link
$ clang -o main main.o `pkg-config --libs gstreamer-1.0`
```

It's important to use the `pkg-config` provided by the `GStreamer.framework`
(not the one provided by Homebrew for example), that's why we set `PATH` to find
`pkg-config` from the right location.

Note how we have used `pkg-config --cflags gstreamer-1.0` to obtain all the
compilation flags and then `pkg-config --libs gstreamer-1.0` to get all the
linker flags.

The commands above should have generated an executable that, as before, we can
inspect:

```
$ otool -L main
main:
        @rpath/libgstreamer-1.0.0.dylib (compatibility version 2101.0.0, current version 2101.0.0)
        @rpath/libgobject-2.0.0.dylib (compatibility version 6201.0.0, current version 6201.6.0)
        @rpath/libglib-2.0.0.dylib (compatibility version 6201.0.0, current version 6201.6.0)
        @rpath/libintl.8.dylib (compatibility version 10.0.0, current version 10.5.0)
        /usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1311.100.3)
```

You can see how the dependencies are different from the ones we saw above. The
reason is because in this case we have linked directly to the GStreamer
libraries included in the framework instead of the framework itself (there's a
slight difference there).
