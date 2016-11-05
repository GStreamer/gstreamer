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

Get **both the runtime and the development installers** from [here](https://gstreamer.freedesktop.org/data/pkg/osx/).


> ![Warning](images/icons/emoticons/warning.png)
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
[gst-docs](https://cgit.freedesktop.org/gstreamer/gst-docs/) in the
`examples/tutorials` subdirectory.

To start building the tutorials, create a new folder in your Documents
directory and copy the
folder `/Library/Frameworks/GStreamer.framework/Current/share/gst-sdk/tutorials`.

You can fire up XCode and load the project file.

Press the **Run** button to build and run the first tutorial. You can
switch the tutorial to build selecting one of the available schemes.

### Creating new projects

The GStreamer binaries provides a
[framework](https://developer.apple.com/library/mac/#documentation/MacOSX/Conceptual/BPFrameworks/Tasks/IncludingFrameworks.html)
that you can drag and drop to XCode to start using it, or using the
linker option ***-framework GStreamer****.*

There is a small exception to the regular use of frameworks, and you
will need to manually include the headers search
path  `/Library/Frameworks/GStreamer.framework/Headers`

  - XCode: Add the headers path to **Search Paths → Header Search
    Paths**
  - GCC: Using the compiler
    option **-I/Library/Frameworks/GStreamer.framework/Headers**
