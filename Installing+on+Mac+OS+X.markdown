#  Installing on Mac OS X

## Supported platforms

 *  10.6 (Snow Leopard)
 *  10.7 (Lion)
 *  10.8 (Mountain Lion)

## Prerequisites

**FIXME: Those all need updating**

To develop applications using the GStreamer SDK for OS X you will need
OS X Snow Leopard (10.6) or later and
[XCode 3.2.6](https://developer.apple.com/devcenter/mac/index.action) or
later.

The recommended system is [Mac OS X
Lion](http://www.apple.com/macosx/) with
[XCode 4](https://developer.apple.com/xcode/)

Installing the SDK for 32-bits platforms requires approximately 145MB of
free disk space for the runtime and 193MB for the development files.

Installing the SDK for 64-bits platforms requires approximately 152MB of
free disk space for the runtime and 223MB for the development files.

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

Get **both the runtime and the development installers** from here:

** FIXME: Update links **

> ![Warning](images/icons/emoticons/warning.png)
> On Mac OS X 10.6 (Snow Leopard) you have to install Python 2.7 manually. It is included in later versions of OS X already. You can get it from [here](http://www.python.org/getit).

> ![Warning](images/icons/emoticons/warning.png)
> Due to the size of these files, usage of a [Download Manager](http://en.wikipedia.org/wiki/Download_manager) is **highly recommended**. Take a look at [this list](http://en.wikipedia.org/wiki/Comparison_of_download_managers) if you do not have one installed. If, after downloading, the installer reports itself as corrupt, chances are that the connection ended before the file was complete. A Download Manager will typically re-start the process and fetch the missing parts.

The downloads are [Apple Disk Images
(.dmg)](http://en.wikipedia.org/wiki/Apple_Disk_Image) containing an
[Installer Package
(.pkg)](http://en.wikipedia.org/wiki/Installer_%28Mac_OS_X%29). Double
click in the installer to start the installation process.

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
    commands provided by the framework, such as gst-inspect-0.10 or
    gst-launch-0.10

For more information on OS X Frameworks anatomy, you can consult the
following [link](https://developer.apple.com/library/mac/#documentation/MacOSX/Conceptual/BPFrameworks/Concepts/FrameworkAnatomy.html)

## Configure your development environment

### Building the tutorials

The tutorial's code, along with project files and a solution file for
them all are included in the SDK. The source code and the XCode project
files are located
in `/Library/Frameworks/GStreamer.framework/Current/share/gst-sdk/tutorials.`

To start building the tutorials, create a new folder in your Documents
directory and copy the
folder `/Library/Frameworks/GStreamer.framework/Current/share/gst-sdk/tutorials`.

You can fire up XCode and load the project file.

Press the **Run **button to build and run the first tutorial. You can
switch the tutorial to build selecting one of the available schemes.

### Creating new projects

The GStreamer SDK provides a
[framework](https://developer.apple.com/library/mac/#documentation/MacOSX/Conceptual/BPFrameworks/Tasks/IncludingFrameworks.html)
that you can drag and drop to XCode to start using it, or using the
linker option ***-framework GStreamer****.*

There is a small exception to the regular use of frameworks, and you
will need to manually include the headers search
path  `/Library/Frameworks/GStreamer.framework/Headers`

  - XCode: Add the headers path to **Search Paths-\> Header Search
    Paths**
  - GCC: Using the compiler
    option** -I/Library/Frameworks/GStreamer.framework/Headers**
