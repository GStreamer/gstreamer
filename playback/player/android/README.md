GST Player Android port
=======================

Prerequisites
-------------

1. Install Android SDK from https://developer.android.com/sdk/ & set `sdk.dir` in **local.properties** to the installation path
2. Install Android NDK from https://developer.android.com/tools/sdk/ndk/index.html & set `ndk.dir` in **local.properties** to the installation path
3. If you have a different special directory for pkg-config or other tools (e.g. on OSX when using Homebrew), then also set this path using the `ndk.extraPath` variable in **local.properties**
4. Download the GStreamer android ports http://gstreamer.freedesktop.org/data/pkg/android/ and set `gstreamer.$ABI.dir` properties in **local.properties**:

Sample local.properties:

    sdk.dir=/path/to/android-sdk/
    ndk.dir=/path/to/android-ndk/
    ndk.extraPath=/usr/local/bin
    gstreamer.arm.dir=/path/to/gstreamer-1.0-android-arm-release-1.4.5/
    gstreamer.armv7.dir=/path/to/gstreamer-1.0-android-armv7-release-1.4.5/
    gstreamer.x86.dir=/path/to/gstreamer-1.0-android-x86-release-1.4.5/


Compiling the sample
--------------------

Use

    ./gradlew installDebug

to compile and install a debug version onto all connected devices.

Please note this component is using the new Android build system based on Gradle. More information about this is available on http://tools.android.com/tech-docs/new-build-system.

Android Studio
--------------

Android Studio builds will work out of the box. Simply open `build.gradle` in this folder to import the project.

Manual NDK build
----------------

It is still possible to build just the NDK portion. This will speed up the process a bit as you don't need to start gradle first and compile the complete App.
First, make sure to set `NDK_PROJECT_PATH` to this projects main source path. Additionally the SDK & NDK tools are available in `$PATH`.

    export NDK_PROJECT_PATH=$PWD/app/src/main

Second, set the following environment variables to the GStreamer installation folders:

    export GSTREAMER_ROOT_ARM=/path/to/gstreamer-1.0-android-arm-release-1.4.5/
    export GSTREAMER_ROOT_ARMV7=/path/to/tmp/gstreamer-1.0-android-armv7-release-1.4.5/
    export GSTREAMER_ROOT_X86=/path/to/gstreamer-1.0-android-x86-release-1.4.5/

If you don't want to build all architectures, please modify the file `app/src/main/jni/Application.mk`

Finally, within the `app/src/main/` directory, invoke:

    ndk-build
