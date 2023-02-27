# Installing for Android development

> ![information] All versions starting from 2.3.1 Gingerbread are supported

## Prerequisites

The development machine is where you will develop your Android
application, which then you will deploy on the target machine, which
should obviously be an Android device.

The development machine can either be a Linux, Mac OS X or Windows, and
needs to have installed:

-   The latest version of the [Android SDK]
-   The correct version of the [Android NDK] corresponding to the version of
    [GStreamer binaries] you're using (f.ex., r18b for 1.16.x)
-   GStreamer for Android is targeted at API version 9 (Android
    2.3.1, Gingerbread) or higher. Use the SDK Manager tool to make sure
    you have at least one Android SDK platform installed with API
    version 9 or higher.

<!-- Optionally, you can use the [Android Studio](https://developer.android.com/studio/index.html). As stated in
the Android documentation, *developing in Android Studio is highly
recommended and is the fastest way to get started*. -->

Before continuing, make sure you can compile and run the samples
included in the Android NDK, and that you understand how the integration
of C and Java works via the [Java Native Interface] (JNI). Besides the
[Android NDK] documentation, you can find some useful [Android JNI tips
here].

## Download and install GStreamer binaries

The GStreamer project provides [prebuilt binaries] you should download
the latest version and unzip it into any folder of your choice.

In the process of building GStreamer-enabled Android applications,
some tools will need to know where you installed the GStreamer
binaries. You must define an environment variable called
`GSTREAMER_ROOT_ANDROID` and point it to the folder where you
extracted the GStreamer binaries. This environment variable must be available at
build time, so maybe you want to make it available system-wide by
adding it to your `~/.profile` file (on Linux and Mac) or to the
Environment Variables in the System Properties dialog (on Windows).

Point `GSTREAMER_ROOT_ANDROID` to the folder where you unzipped the binaries.

> ![information] If you plan to use Android Studio and do not want to define this
> environment variable globally, you can set it inside the build.gradle.

> ![information] If you plan to use Eclipse, and do not want to define this
> environment variable globally, you can set it inside Eclipse. Go to
> Window → Preferences → C/C++ → Build → Build Variables and define
> `GSTREAMER_ROOT_ANDROID` there.

> ![warning] The NDK support in the Gradle build system used by
>  Android Studio is still in beta, so the recommended way to build
>  using the GStreamer SDK is still to use "ndk-build".

## Configure your development environment

There are two routes to use GStreamer in an Android application: Either
writing your GStreamer code in Java or in C.

Android applications are mainly written in Java, so adding GStreamer
code to them in the same language is a huge advantage. However, this
requires using [language bindings] for the GStreamer API which are not
complete yet. In the meantime, this documentation will use Java for the
User Interface (UI) part and C for the GStreamer code. Both parts
interact through [JNI][Java Native Interface].

### Building the tutorials

The tutorials code are in the
[gst-docs](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/main/subprojects/gst-docs) in the
`examples/tutorials/` folder.

There are a few Android-specific tutorials in the `tutorials/`
folder. Each tutorial is a folder containing source code (in Java and
C) and the resource files required to build a complete Android
application.

The rest of the GStreamer tutorials (basic and playback tutorials)
cannot be run on Android without modification.

Android projects with GStreamer support are built like conventional
Android NDK projects, so the instructions at the [Android NDK] home can
be followed:

<!--
#### Using Android Studio

> ![warning] To be completed!!
-->

#### Using Eclipse

Make sure you have installed the ADT and NDK plugins listed in the
prerequisites section, and that they are both aware of the location of
the Android SDK and NDK respectively.

Import a tutorial into the Eclipse workspace:
File → New → Project… → Android Project from Existing Code, and select
the folder called `android-tutorial-1`.

After reading in the project and generating some extra files and
folders, Eclipse might complain about missing files. **This is normal**,
we are not finished yet.

Provide native development support by activating the NDK plugin:
Right-click on the project in the Project Explorer (this should be the
top-most folder,
called `com.gst_sdk_tutorials.tutorial_1.Tutorial1`) → Android
tools → Add Native Support… Here the NDK plugin asks for a library name.
This is irrelevant and any valid file name will do. Accept.

Eclipse will still complain about errors in the code. **This is
normal**. Some files are missing because they are generated during the
first build run.

Build the project: Project → Build Project. If you bring up the Eclipse
Console, you should see some progress messages. Once finished, the
missing files will appear and all error messages should be gone. The
project is now ready to run. Hit Run → Run.

A new application called “Android tutorial 1” should now be available on
your device, with the GStreamer logo. If you want to run the
tutorial in an Android Virtual Device (AVD), make sure to create the
device with support for audio playback and GPU Emulation (to enable
OpenGL ES).

#### Using the command line

> ![warning] Note that, on Windows, this procedure requires a working Cygwin
> shell, as explained in the [Android NDK System Requirements]

For each tutorial, move to its folder and run:

    android update project -p . -s --target X

Where `X` is one of the targets available in your system (the ones you
installed with the SDK manager). Make sure to use a target with at least
API level 9.

To get a list of all available targets in your system issue this
command:

    android list

The “update project” command generates the `build.xml` file needed by
the build system. You only need to perform this action once per project.

To build the C part, just call:

    ndk-build

A few lines in the `Android.mk` file (reviewed later) pull up the
necessary machinery to compile the GStreamer bits and generate the
Shared Object libraries (.so) that the Java code can use as native
methods.

Finally, compile the Java code with:

    ant debug

And install on the device with:

    adb install -r bin/Tutorial1-debug.apk

The `-r` switch allows the installer to overwrite previous versions.
Otherwise, you need to manually uninstall previous versions of your
application.

A new application called “Android tutorial 1” should now be available on
your device, with the GStreamer logo. If you want to run the
tutorial in an Android Virtual Device (AVD), make sure to create the
device with support for audio playback and GPU Emulation (to enable
OpenGL ES).

> ![warning] Windows linkage problems
>
> Due to problems related to the standard linker, Google’s
> <a href="http://en.wikipedia.org/wiki/Gold_(linker)" class="external-link">Gold
> Linker</a> is used to build GStreamer applications.  Unfortunately,
> the Android NDK toolchain for Windows does not include the gold linker
> and the standard one has to be used.
>
> If you observe linkage problems, you can replace the linker in your
> Android NDK with the gold one from [this project]. Download the
> `android-ndk-r8b-ma-windows.7z` file, extract
> `\android-ndk-r8b\toolchains\arm-linux-androideabi-4.6\prebuilt\windows\arm-linux-androideabi\bin\ld.exe`
> (only this file is needed) and overwrite the one in the same folder in
> your Android NDK installation. You might need the free [7-Zip
> archiving utility]

#### Using gradle from the command-line

Edit examples/tutorials/android/gradle.properties in order to set gstAndroidRoot to point to the
unpacked GStreamer Android binaries.

Then, to build and deploy the tutorials to your device, use a command similar to:

```bash
$ cd examples/tutorials/android
$ PATH=~/dev/android/tools/bin:~/dev/android/ndk-bundle:$PATH ANDROID_HOME="$HOME/dev/android/" ./gradlew installDebug
```

To build and deploy a single tutorial:

```bash
$ cd examples/tutorials/android
$ GSTREAMER_ROOT_ANDROID=/path/to/gst-android-1.14/ PATH=~/dev/android/tools/bin:~/dev/android/ndk-bundle:$PATH ANDROID_HOME="$HOME/dev/android/" ./gradlew :android-tutorial-1:installDebug
```

To run the application, you can either directly launch it from the device,
or from the command line:

```bash
$ adb shell am start -n adb shell am start -n org.freedesktop.gstreamer.tutorials.tutorial_1/.Tutorial1
```

To see the GStreamer logs at runtime:

```bash
$ adb logcat | egrep '(gst)'
```

#### Using Android-studio

Edit examples/tutorials/android/gradle.properties in order to set gstAndroidRoot to point to the
unpacked GStreamer Android binaries.

Launch Android-studio, opening examples/tutorials/android/ as a project.

The project should build automatically, once it has done successfully,
it should be possible to run the tutorials with Run > Run 'tutorial X', provided
a device is attached and USB debugging enabled.

The logs can be seen in the logcat tab.

### Creating new projects

Create a normal NDK project, either from the command line as described
in the [Android NDK][2] home, or use Eclipse: File → New → Project…
→ Android Application Project, and, once the wizard is complete, right
click on the project → Android Tools → Add Native Support …

To add GStreamer support you only need to modify the
`jni/Android.mk` file. This file describes the native files in your
project, and its barebones structure (as auto-generated by Eclipse) is:

**Android.mk**

    LOCAL_PATH := $(call my-dir)

    include $(CLEAR_VARS)

    LOCAL_MODULE    := NativeApplication
    LOCAL_SRC_FILES := NativeApplication.c

    include $(BUILD_SHARED_LIBRARY)

Where line 5 specifies the name of the `.so` file that will contain your
native code and line 6 states all source files that compose your native
code, separated by spaces.

Adding GStreamer support only requires adding these lines:

**Android.mk with GStreamer support**

    LOCAL_PATH := $(call my-dir)

    include $(CLEAR_VARS)

    LOCAL_MODULE    := NativeApplication
    LOCAL_SRC_FILES := NativeApplication.c
    LOCAL_SHARED_LIBRARIES := gstreamer_android
    LOCAL_LDLIBS := -landroid

    include $(BUILD_SHARED_LIBRARY)

    ifndef GSTREAMER_ROOT
    ifndef GSTREAMER_ROOT_ANDROID
    $(error GSTREAMER_ROOT_ANDROID is not defined!)
    endif
    GSTREAMER_ROOT            := $(GSTREAMER_ROOT_ANDROID)
    endif

    GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/
    GSTREAMER_PLUGINS         := coreelements ogg theora vorbis videoconvert audioconvert audioresample playback glimagesink soup opensles
    G_IO_MODULES              := gnutls
    GSTREAMER_EXTRA_DEPS      := gstreamer-video-1.0

    include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer.mk

Where line 7 specifies an extra library to be included in the project:
`libgstreamer_android.so`. This library contains all GStreamer code,
tailored for your application’s needs, as shown below.

Line 8 specifies additional system libraries, in this case, in order to
access android-specific functionality.

Lines 12 and 13 simply define some convenient macros.

Line 20 lists the plugins you want statically linked into
`libgstreamer_android.so`. Listing only the ones you need makes your
application smaller.

Line 21 is required to have HTTPS/TLS support from GStreamer, through the
`souphttpsrc` element.

Line 22 defines which GStreamer libraries your application requires.

Finally, line 24 includes the make files which perform the rest of the
magic.

Listing all desired plugins can be cumbersome, so they have been grouped
into categories, which can be used by including the `plugins.mk` file,
and used as follows:

    include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
    GSTREAMER_PLUGINS  := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_CODECS) playbin souphttpsrc

#### List of categories and included plugins

| Category                       | Included plugins                                                                                                                                                                                                                                                                                                                                                                                                                                   |
|--------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `GSTREAMER_PLUGINS_CORE`       | coreelements adder app audioconvert audiorate audioresample audiotestsrc gio pango typefindfunctions videoconvert videorate videoscale videotestsrc volume autodetect videofilter |
| `GSTREAMER_PLUGINS_PLAYBACK`   | playback |
| `GSTREAMER_PLUGINS_VIS`        | libvisual goom goom2k1 audiovisualizers |
| `GSTREAMER_PLUGINS_EFFECTS`    | alpha alphacolor audiofx cairo cutter debug deinterlace dtmf effectv equalizer gdkpixbuf imagefreeze interleave level multifile replaygain shapewipe smpte spectrum videobox videocrop videomixer accurip aiff audiofxbad autoconvert bayer coloreffects debugutilsbad fieldanalysis freeverb frei0r gaudieffects geometrictransform inter interlace ivtc rawparse removesilence segmentclip smooth speed soundtouch videofiltersbad audiomixer compositor webrtcdsp |
| `GSTREAMER_PLUGINS_NET`        | tcp rtsp rtp rtpmanager soup udp dataurisrc sdp srtp rtspclientsink |
| `GSTREAMER_PLUGINS_NET_RESTRICTED` | rtmp |
| `GSTREAMER_PLUGINS_CODECS`     | subparse ogg theora vorbis opus alaw apetag audioparsers auparse avi dv flac flv flxdec icydemux id3demux isomp4 jpeg matroska mulaw multipart png speex taglib vpx wavenc wavpack wavparse y4menc adpcmdec adpcmenc dashdemux dvbsuboverlay dvdspu hls id3tag kate midi mxf openh264 opusparse pcapparse pnm rfbsrc schro gstsiren smoothstreaming subenc videoparsersbad y4mdec jpegformat gdp rsvg openjpeg spandsp sbc androidmedia |
| `GSTREAMER_PLUGINS_CODECS_GPL` | assrender |
| `GSTREAMER_PLUGINS_CODECS_RESTRICTED` | asfmux dtsdec faad mpegpsdemux mpegpsmux mpegtsdemux mpegtsmux voaacenc a52dec amrnb amrwbdec asf dvdsub dvdlpcmdec mad mpeg2dec xingmux realmedia x264 lame mpg123 libav |
| `GSTREAMER_PLUGINS_SYS`        | opensles opengl |
| `GSTREAMER_PLUGINS_CAPTURE`    | camerabin |
| `GSTREAMER_PLUGINS_ENCODING`   | encodebin |
| `GSTREAMER_PLUGINS_GES`        | nle |

Build and run your application as explained in the [Building the tutorials] section.

  [information]: images/icons/emoticons/information.svg
  [Android SDK]: http://developer.android.com/sdk/index.html
  [Android NDK]: http://developer.android.com/tools/sdk/ndk/index.html
  [GStreamer binaries]: https://gstreamer.freedesktop.org/download/#android
  [Java Native Interface]: http://en.wikipedia.org/wiki/Java_Native_Interface
  [Android JNI tips here]: http://developer.android.com/guide/practices/jni.html
  [prebuilt binaries]: https://gstreamer.freedesktop.org/data/pkg/android/
  [language bindings]: http://en.wikipedia.org/wiki/Language_binding
  [warning]: images/icons/emoticons/warning.svg
  [Android NDK System Requirements]: http://developer.android.com/tools/sdk/ndk/index.html#Reqs
  [this project]: http://code.google.com/p/mingw-and-ndk/downloads/detail?name=android-ndk-r8b-ma-windows.7z&can=2&q=
  [7-Zip archiving utility]: http://www.7-zip.org/
  [2]: http://developer.android.com/tools/sdk/ndk/index.html#GetStarted
