#  GStreamer SDK documentation : Installing for Android development 

This page last changed on May 24, 2013 by xartigas.

# Prerequisites

The development machine is where you will develop your Android
application, which then you will deploy on the target machine, which
should obviously be an Android device.

The development machine can either be a Linux, Mac OS X or Windows, and
needs to have installed:

  - The latest version of the [Android
    SDK](http://developer.android.com/sdk/index.html)
  - The latest version of the [Android
    NDK](http://developer.android.com/tools/sdk/ndk/index.html)
  - The GStreamer SDK for Android is targeted at API version 9 (Android
    2.3.1, Gingerbread) or higher. Use the SDK Manager tool to make sure
    you have at least one Android SDK platform installed with API
    version 9 or higher.

Optionally, you can use the [Eclipse
IDE](http://www.eclipse.org/eclipse/). As stated in the Android
documentation, *developing in Eclipse with ADT is highly recommended and
is the fastest way to get started*. If you plan to use the Eclipse IDE:

  - Install the [Android ADT
    plugin](http://developer.android.com/sdk/installing/installing-adt.html) for
    Eclipse
  - Install the [Android NDK
    plugin](http://tools.android.com/recent/usingthendkplugin) for
    Eclipse

Before continuing, make sure you can compile and run the samples
included in the Android NDK, and that you understand how the integration
of C and Java works via the [Java Native
Interface](http://en.wikipedia.org/wiki/Java_Native_Interface) (JNI).
Besides the [Android
NDK](http://developer.android.com/tools/sdk/ndk/index.html)
documentation, you can find some useful [Android JNI tips
here](http://developer.android.com/guide/practices/jni.html).

# Download and install the SDK

The SDK has two variants: **Debug** and **Release**. The Debug variant
produces lots of debug output and is useful while developing your
application. The Release variant is what you will use to produce the
final version of your application, since GStreamer code runs slightly
faster and the libraries are smaller.

Get the compressed file below and just unzip it into any folder of your
choice (Choose your preferred file format; both files have exactly the
same content)

<table>
<colgroup>
<col width="100%" />
</colgroup>
<thead>
<tr class="header">
<th>Debug variant</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><ul>
<li><a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.tar.bz2" class="external-link">GStreamer SDK 2013.6 (Congo) for Android ARM (Debug, tar.bz2)</a> - <a href="http://www.freedesktop.org/software/gstreamer-sdk/data/packages/android/arm/gstreamer-sdk-android-arm-debug-2013.6.tar.bz2" class="external-link">mirror</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.tar.bz2.md5" class="external-link">md5</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.tar.bz2.sha1" class="external-link">sha1</a></li>
<li><a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip" class="external-link">GStreamer SDK 2013.6 (Congo) for Android ARM (Debug, zip)</a> - <a href="http://www.freedesktop.org/software/gstreamer-sdk/data/packages/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip" class="external-link">mirror</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip.md5" class="external-link">md5</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip.sha1" class="external-link">sha1</a></li>
</ul></td>
</tr>
<tr class="even">
<td><span style="color: rgb(0,0,0);">Release variant</span></td>
</tr>
<tr class="odd">
<td><ul>
<li><a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-release-2013.6.tar.bz2" class="external-link">GStreamer SDK 2013.6 (Congo) for Android ARM (Release, tar.bz2)</a> - <a href="http://www.freedesktop.org/software/gstreamer-sdk/data/packages/android/arm/gstreamer-sdk-android-arm-release-2013.6.tar.bz2" class="external-link">mirror</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-release-2013.6.tar.bz2.md5" class="external-link">md5</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-release-2013.6.tar.bz2.sha1" class="external-link">sha1</a></li>
<li><a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip" class="external-link">GStreamer SDK 2013.6 (Congo) for Android ARM (Release, zip)</a> - <a href="http://www.freedesktop.org/software/gstreamer-sdk/data/packages/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip" class="external-link">mirror</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip.md5" class="external-link">md5</a> - <a href="http://cdn.gstreamer.com/android/arm/gstreamer-sdk-android-arm-debug-2013.6.zip.sha1" class="external-link">sha1</a></li>
</ul></td>
</tr>
</tbody>
</table>

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p>Due to the size of these files, usage of a <a href="http://en.wikipedia.org/wiki/Download_manager" class="external-link">Download Manager</a> is <strong>highly recommended</strong>. Take a look at <a href="http://en.wikipedia.org/wiki/Comparison_of_download_managers" class="external-link">this list</a> if you do not have one installed. If, after downloading, the installer reports itself as corrupt, chances are that the connection ended before the file was complete. A Download Manager will typically re-start the process and fetch the missing parts.</p></td>
</tr>
</tbody>
</table>

If you intend to build the tutorials in this same folder, make sure you
have writing permissions.

In the process of building GStreamer-enabled Android applications, some
tools will need to know where you installed the SDK. You must define an
environment variable called `GSTREAMER_SDK_ROOT_ANDROID` and point it to
the folder where you extracted the SDK. This environment variable must
be available at build time, so maybe you want to make it available
system-wide by adding it to your `~/.profile` file (on Linux and Mac) or
to the Environment Variables in the System Properties dialog (on
Windows).

  - Point `GSTREAMER_SDK_ROOT_ANDROID` to the folder where you unzipped
    the SDK.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><p>If you plan to use Eclipse and do not want to define this environment variable globally, you can set it inside Eclipse. Go to Window → Preferences → C/C++ → Build → Build Variables and define <code>GSTREAMER_SDK_ROOT_ANDROID</code> there.</p></td>
</tr>
</tbody>
</table>

# Configure your development environment

There are two routes to use GStreamer in an Android application: Either
writing your GStreamer code in Java or in C.

Android applications are mainly written in Java, so adding GStreamer
code to them in the same language is a huge advantage. However, this
requires using [language
bindings](http://en.wikipedia.org/wiki/Language_binding) for the
GStreamer API which are not complete yet. In the meantime, this
documentation will use Java for the User Interface (UI) part and C for
the GStreamer code. Both parts interact through
[JNI](http://en.wikipedia.org/wiki/Java_Native_Interface).

### Building the tutorials

There are a few Android-specific tutorials in the
`$GSTREAMER_SDK_ROOT_ANDROID\share\gst-sdk\tutorials` folder. Each
tutorial is a folder containing source code (in Java and C) and the
resource files required to build a complete Android application.

The rest of the GStreamer SDK tutorials (basic and playback tutorials)
cannot be run on Android without modification.

Android projects with GStreamer support are built like conventional
Android NDK projects, so the instructions at the [Android
NDK](http://developer.android.com/tools/sdk/ndk/index.html) home can be
followed:

![](images/icons/grey_arrow_down.gif)Using Eclipse (Click to expand)

Make sure you have installed the ADT and NDK plugins listed in the
prerequisites section, and that they are both aware of the location of
the Android SDK and NDK respectively.

Import a tutorial into the Eclipse workspace:
File → New → Project… → Android Project from Existing
Code, and select the folder called `android-tutorial-1`.

After reading in the project and generating some extra files and
folders, Eclipse might complain about missing files. **This is normal**,
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
your device, with the GStreamer SDK logo. If you want to run the
tutorial in an Android Virtual Device (AVD), make sure to create the
device with support for audio playback and GPU Emulation (to enable
OpenGL ES).

![](images/icons/grey_arrow_down.gif)Using the command line (Click to
expand)

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><p>Note that, on Windows, this procedure requires a working Cygwin shell, as explained in the <a href="http://developer.android.com/tools/sdk/ndk/index.html#Reqs" class="external-link">Android NDK System Requirements</a>.</p></td>
</tr>
</tbody>
</table>

For each tutorial, move to its folder and run:

``` theme: Default; brush: plain; gutter: false
android update project -p . -s --target X
```

Where `X` is one of the targets available in your system (the ones you
installed with the SDK manager). Make sure to use a target with at least
API level 9.

To get a list of all available targets in your system issue this
command:

``` theme: Default; brush: plain; gutter: false
android list
```

The “update project” command generates the `build.xml` file needed by
the build system. You only need to perform this action once per project.

To build the C part, just call:

``` theme: Default; brush: plain; gutter: false
ndk-build
```

A few lines in the `Android.mk` file (reviewed later) pull up the
necessary machinery to compile the GStreamer bits and generate the
Shared Object libraries (.so) that the Java code can use as native
methods.

Finally, compile the Java code with:

``` theme: Default; brush: plain; gutter: false
ant debug
```

And install on the device with:

``` theme: Default; brush: plain; gutter: false
adb install -r bin/Tutorial1-debug.apk
```

The `-r` switch allows the installer to overwrite previous versions.
Otherwise, you need to manually uninstall previous versions of your
application.

A new application called “Android tutorial 1” should now be available on
your device, with the GStreamer SDK logo. If you want to run the
tutorial in an Android Virtual Device (AVD), make sure to create the
device with support for audio playback and GPU Emulation (to enable
OpenGL ES).

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/warning.png" width="16" height="16" /></td>
<td><strong>Windows linkage problems</strong><br />

<p>Due to problems related to the standard linker, Google’s <a href="http://en.wikipedia.org/wiki/Gold_(linker)" class="external-link">Gold Linker</a> is used to build GStreamer applications.  Unfortunately, the Android NDK toolchain for Windows does not include the gold linker and the standard one has to be used.</p>
<p>If you observe linkage problems, you can replace the linker in your Android NDK with the gold one from <a href="http://code.google.com/p/mingw-and-ndk/downloads/detail?name=android-ndk-r8b-ma-windows.7z&amp;can=2&amp;q=" class="external-link">this project</a>. Download the <code>android-ndk-r8b-ma-windows.7z</code> file, extract <code>\android-ndk-r8b\toolchains\arm-linux-androideabi-4.6\prebuilt\windows\arm-linux-androideabi\bin\ld.exe</code> (only this file is needed) and overwrite the one in the same folder in your Android NDK installation. You might need the free <a href="http://www.7-zip.org/" class="external-link">7-Zip archiving utility</a>.</p></td>
</tr>
</tbody>
</table>

### Creating new projects

Create a normal NDK project, either from the command line as described
in the [Android
NDK](http://developer.android.com/tools/sdk/ndk/index.html#GetStarted)
home, or use Eclipse: File → New → Project… → Android Application
Project, and, once the wizard is complete, right click on the project
→ Android Tools → Add Native Support …

To add GStreamer support you only need to modify the
`jni/Android.mk` file. This file describes the native files in your
project, and its barebones structure (as auto-generated by Eclipse) is:

**Android.mk**

``` theme: Default; brush: plain; gutter: true
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := NativeApplication
LOCAL_SRC_FILES := NativeApplication.c

include $(BUILD_SHARED_LIBRARY)
```

Where line 5 specifies the name of the `.so` file that will contain your
native code and line 6 states all source files that compose your native
code, separated by spaces.

Adding GStreamer support only requires adding these lines:

**Android.mk with GStreamer support**

``` theme: Default; brush: plain; gutter: true
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := NativeApplication
LOCAL_SRC_FILES := NativeApplication.c
LOCAL_SHARED_LIBRARIES := gstreamer_android
LOCAL_LDLIBS := -landroid

include $(BUILD_SHARED_LIBRARY)

GSTREAMER_SDK_ROOT        := $(GSTREAMER_SDK_ROOT_ANDROID)
GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_SDK_ROOT)/share/gst-android/ndk-build/
GSTREAMER_PLUGINS         := coreelements ogg theora vorbis ffmpegcolorspace playback eglglessink soup opensles
G_IO_MODULES              := gnutls
GSTREAMER_EXTRA_DEPS      := gstreamer-interfaces-0.10 gstreamer-video-0.10

include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer.mk 
```

Where line 7 specifies an extra library to be included in the project:
`libgstreamer_android.so`. This library contains all GStreamer code,
tailored for your application’s needs, as shown below.

Line 8 specifies additional system libraries, in this case, in order to
access android-specific functionality.

Lines 12 and 13 simply define some convenient macros.

Line 14 lists the plugins you want statically linked into
`libgstreamer_android.so`. Listing only the ones you need makes your
application smaller.

Line 15 is required to have internet access from GStreamer, through the
`souphttpsrc` element.

Line 16 defines which GStreamer libraries your application requires.

Finally, line 18 includes the make files which perform the rest of the
magic.

Listing all desired plugins can be cumbersome, so they have been grouped
into categories, which can be used by including the `plugins.mk` file,
and used as follows:

``` theme: Default; brush: plain; gutter: false
include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
GSTREAMER_PLUGINS  := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_CODECS) playbin souphttpsrc
```

![](images/icons/grey_arrow_down.gif)List of categories and included
plugins (Click to expand)

<table>
<thead>
<tr class="header">
<th>Category</th>
<th>Included plugins</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><code>GSTREAMER_PLUGINS_CORE</code></td>
<td><code>coreelements coreindexers adder app audioconvert audiorate audioresample audiotestsrc</code> <code>ffmpegcolorspace gdp gio pango typefindfunctions videorate videoscale videotestsrc</code> <code>volume autodetect videofilter</code></td>
</tr>
<tr class="even">
<td><code>GSTREAMER_PLUGINS_PLAYBACK</code></td>
<td><code>decodebin2 playbin</code></td>
</tr>
<tr class="odd">
<td><code>GSTREAMER_PLUGINS_CODECS</code></td>
<td><code>subparse ogg theora vorbis alaw annodex apetag audioparsers auparse avi flac flv flxdec</code> <code>icydemux id3demux isomp4 jpeg matroska mulaw multipart png speex taglib wavenc wavpack</code> <code>wavparse y4menc adpcmdec adpcmenc aiff cdxaparse dtmf dvbsuboverlay dvdspu fragmented</code> <code>hdvparse id3tag ivfparse jp2k kate mve mxf nsf nuvdemux opus pcapparse pnm schro siren</code> <code>subenc tta videoparsersbad vmnc vp8 y4mdec</code></td>
</tr>
<tr class="even">
<td><code>GSTREAMER_PLUGINS_VIS</code></td>
<td><code>libvisual goom goom2k1 audiovisualizers</code></td>
</tr>
<tr class="odd">
<td><code>GSTREAMER_PLUGINS_EFFECTS</code></td>
<td><code>alpha alphacolor audiofx cutter debug deinterlace effectv equalizer gdkpixbuf imagefreeze</code> <code>interleave level multifile replaygain shapewipe smpte spectrum videobox videocrop videomixer</code> <code>autoconvert bayer coloreffects faceoverlay fieldanalysis freeverb frei0r gaudieffects</code> <code>geometrictransform interlace jp2kdecimator liveadder rawparse removesilence scaletempoplugin</code> <code>segmentclip smooth speed stereo videofiltersbad videomeasure videosignal</code></td>
</tr>
<tr class="even">
<td><code>GSTREAMER_PLUGINS_NET</code></td>
<td><code>rtsp rtp rtpmanager souphttpsrc udp dataurisrc rtpmux rtpvp8 sdpelem</code></td>
</tr>
<tr class="odd">
<td><code>GSTREAMER_PLUGINS_CODECS_GPL</code></td>
<td><code>assrender</code></td>
</tr>
<tr class="even">
<td><code>GSTREAMER_PLUGINS_SYS</code></td>
<td><code>eglglessink opensles amc</code></td>
</tr>
</tbody>
</table>

Build and run your application as explained in the **Building the
tutorials** section.

Document generated by Confluence on Oct 08, 2015 10:27

