# Installing for iOS/tvOS development

![](images/icons/emoticons/information.svg) All versions starting from iOS 12 are supported

### Prerequisites

For iOS development you need to download Xcode and the iOS SDK. Xcode
can be found at the App Store or
[here](https://developer.apple.com/devcenter/ios/index.action#downloads)
and the iOS SDK, if it is not already included in your version of Xcode,
can be downloaded from Xcode's preferences menu under the downloads tab.
The minimum required iOS version is 12.0.

In case you are not familiar with iOS, Objective-C or Xcode, we
recommend taking a look at the available documentation at Apple's
website.
[This](https://developer.apple.com/ios/get-started/) can be a good starting point.

## Download and unpack GStreamer binaries

Starting with version 1.28, GStreamer for iOS/tvOS is distributed as an
`.xcframework` archive. You can find the download links at:

[https://gstreamer.freedesktop.org/download/#ios](https://gstreamer.freedesktop.org/download/#ios)

Download the xcframework archive, then unpack it to a location of your choice.
You can then simply import it to your project in Xcode. As of GStreamer 1.28.1,
it supports iOS and tvOS ARM64 targets, as well as iOS/tvOS Simulator on ARM64
and X86_64 devices.

### Configure your development environment

GStreamer is written in C, and the iOS API uses mostly Objective-C (and
C for some parts), but this should cause no problems as those languages
interoperate freely. You can mix both in the same source code, for
example.

#### Building the tutorials

The tutorials code are in the
[gst-docs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gst-docs)
in the `examples/tutorials/xcode iOS` folder.

To run our tutorials, you need to first import the GStreamer.xcframework
downloaded in the previous step.

After downloading, please extract it to the `examples/tutorials/xcode iOS` folder,
so that `GStreamer.xcframework` sits next to `GStreamer iOS Tutorials.xcodeproj`.
The Xcode project is set up to detect it there automatically. Alternatively,
you can manually add the .xcframework from another directory by right-clicking
Frameworks in the source list on the left and using "Add Files to...`.

You can then pick any of the tutorial targets and try running it in a simulator
or on your actual device.

#### Creating new projects

For new projects, please use our tutorial sources as a reference for the basic
setup needed to run GStreamer in your project. To understand how all the pieces
interact, take a look at the [iOS tutorials](tutorials/ios/index.md) page.
