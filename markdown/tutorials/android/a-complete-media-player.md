# Android tutorial 5: A Complete media player

## Goal!

![screenshot]

This tutorial wants to be the “demo application” that showcases what can
be done with GStreamer in the Android platform.

It is intended to be downloaded in final, compiled, form rather than
analyzed for its pedagogical value, since it adds very little GStreamer
knowledge over what has already been shown in [](tutorials/android/media-player.md).


**FIXME: Do we want to provide a binary of the app?**

## Introduction

The previous tutorial already implemented a basic media player. This one
simply adds a few finishing touches. In particular, it adds the
capability to choose the media to play, and disables the screensaver
during media playback.

These are not features directly related to GStreamer, and are therefore
outside the scope of these tutorials. Only a few implementation pointers
are given here.

## Registering as a media player

The `AndroidManifest.xml` tells the Android system the capabilities of
the application. By specifying in the `intent-filter` of the activity
that it understands the `audio/*`, `video/*` and `image/*` MIME types,
the tutorial will be offered as an option whenever an application
requires such medias to be viewed.

“Unfortunately”, GStreamer knows more file formats than Android does,
so, for some files, Android will not provide a MIME type. For these
cases, a new `intent-filter` has to be provided which ignores MIME types
and focuses only in the filename extension. This is inconvenient because
the list of extensions can be large, but there does not seem to be
another option. In this tutorial, only a very short list of extensions
is provided, for simplicity.

Finally, GStreamer can also playback remote files, so URI schemes like
`http` are supported in another `intent-filter`. Android does not
provide MIME types for remote files, so the filename extension list has
to be provided again.

Once we have informed the system of our capabilities, it will start
sending
[Intents](http://developer.android.com/reference/android/content/Intent.html)
to invoke our activity, which will contain the desired URI to play. In
the `onCreate()` method the intent that invoked the activity is
retrieved and checked for such URI.

## Implementing a file chooser dialog

The UI includes a new button ![media-next) which
was not present in [](tutorials/android/media-player.md). It
invokes a file chooser dialog (based on the [Android File
Dialog](http://code.google.com/p/android-file-dialog/) project) that
allows you to choose a local media file, no matter what extension or
MIME type it has.

If a new media is selected, it is passed onto the native code (which
will set the pipeline to READY, pass the URI onto `playbin`, and bring
the pipeline back to the previous state). The current position is also
reset, so the new clip does not start in the previous position.

## Preventing the screen from turning off

While watching a movie, there is typically no user activity. After a
short period of such inactivity, Android will dim the screen, and then
turn it off completely. To prevent this, a [Wake
Lock](http://developer.android.com/reference/android/os/PowerManager.WakeLock.html)
is used. The application acquires the lock when the Play button is
pressed, so the screen is never turned off, and releases it when the
Pause button is pressed.

## Conclusion

This finishes the series of Android tutorials. Each one of the
preceding tutorials has evolved on top of the previous one, showing
how to implement a particular set of features, and concluding in this
tutorial 5. The goal of tutorial 5 is to build a complete media player
which can already be used to showcase the integration of GStreamer and
Android.

It has been a pleasure having you here, and see you soon!

 [screenshot]: images/tutorials/android-a-complete-media-player-screenshot.png
 [media-next]: images/media-next.png
