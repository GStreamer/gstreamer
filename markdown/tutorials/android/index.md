# Android tutorials

## Welcome to the GStreamer Android tutorials

These tutorials describe Android-specific topics. General GStreamer
concepts will not be explained in these tutorials, so the
[](tutorials/basic/index.md) should be reviewed first. The reader should
also be familiar with basic Android programming techniques.

Each Android tutorial builds on top of the previous one and adds
progressively more functionality, until a working media player
application is obtained in [](tutorials/android/a-complete-media-player.md).
This is the same media player application used to advertise
GStreamer on Android, and the download link can be found in
the [](tutorials/android/a-complete-media-player.md) page.

Make sure to have read the instructions in
[](installing/for-android-development.md) before jumping into the
Android tutorials.

### A note on the documentation

All Java methods, both Android-specific and generic, are documented in
the [Android reference
site](http://developer.android.com/reference/packages.html).

Unfortunately, there is no official online documentation for the NDK.
The header files, though, are well commented. If you installed the
Android NDK in the `$(ANDROID_NDK_ROOT)` folder, you can find the header
files
in `$(ANDROID_NDK_ROOT)\platforms\android-9\arch-arm\usr\include\android`.
