# Installing for iOS development

![](images/icons/emoticons/information.svg) All versions starting from iOS 6 are supported

### Prerequisites

For iOS development you need to download Xcode and the iOS SDK. Xcode
can be found at the App Store or
[here](https://developer.apple.com/devcenter/ios/index.action#downloads)
and the iOS SDK, if it is not already included in your version of Xcode,
can be downloaded from Xcode's preferences menu under the downloads tab.
The minimum required iOS version is 6.0. The minimum required version of
Xcode is 4, but 7.3 is recommended.

In case you are not familiar with iOS, Objective-C or Xcode, we
recommend taking a look at the available documentation at Apple's
website.
[This](http://developer.apple.com/library/ios/#DOCUMENTATION/iPhone/Conceptual/iPhone101/Articles/00_Introduction.html) can be a good starting point.

## Download and install GStreamer binaries

GStreamer binary installer can be found at:

[https://gstreamer.freedesktop.org/data/pkg/ios/](https://gstreamer.freedesktop.org/data/pkg/ios/)

Double click the package file and follow the instructions presented by
the install wizard. In case the system complains about the package not
being signed, you can control-click it and open to start the
installation. When you do this, it will warn you, but there is an option
to install anyway. Otherwise you can go to System Preferences → Security
and Privacy → General and select the option to allow installation of
packages from "anywhere".

The GStreamer SDK installs itself in your home directory, so it is
available only to the user that installed it. The SDK library is
installed to `~/Library/Developer/GStreamer/iPhone.sdk`. Inside this
directory there is the GStreamer.framework that contains the libs,
headers and resources, and there is a `Templates` directory that has
Xcode application templates for GStreamer development. Those templates
are also copied to `~/Library/Developer/Xcode/Templates` during
installation so that Xcode can find them.

### Configure your development environment

GStreamer is written in C, and the iOS API uses mostly Objective-C (and
C for some parts), but this should cause no problems as those languages
interoperate freely. You can mix both in the same source code, for
example.

#### Building the tutorials

The tutorials code are in the
[gst-docs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gst-docs) in the
`examples/tutorials/xcode iOS` folder. We recommend that you open the project
in Xcode, take a look at the sources and build them. This should
confirm that the installation works and give some insight on how
simple it is to mix Objective-C and C code.

#### Creating new projects

After installation, when creating a new Xcode project, you should see
the GStreamer project templates under the `Templates` category. OS X and
iOS have a different way of organizing libraries headers and binaries.
They are grouped into Frameworks, and that's how we ship GStreamer and
its dependencies for iOS (and OS X). Due to this difference from
traditional linux development, we strongly recommend using the SDK
templates, as they set a few variables on your project that allows Xcode
to find, use and link GStreamer just like in traditional linux
development. For example, if you don't use the templates, you'll have to
use:

```
#include <GStreamer/gst/gst.h>
```

instead of the usual:

```
#include <gst/gst.h>
```

Among some other things the template does, this was a decision made to
keep development consistent across all the platforms the SDK supports.

Once a project has been created using a GStreamer SDK Template, it is
ready to build and run. All necessary infrastructure is already in
place. To understand what files have been created and how they interact,
take a look at the [iOS tutorials](tutorials/ios/index.md).
