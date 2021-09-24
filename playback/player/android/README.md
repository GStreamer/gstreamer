# Android GStreamer sample player application

## Build and deploy

First of all you need to replace the gold linker with bfd in the `gst-android-1.14/armv7/share/gst-android/ndk-build/gstreamer-1.0.mk`
See also this [NDK issue](https://github.com/android-ndk/ndk/issues/337)

Then to build and deploy the player app to your device, use a command similar to:

```bash
$ GSTREAMER_ROOT_ANDROID=/path/to/gst-android-1.14/ PATH=~/dev/android/tools/bin:~/dev/android/ndk-bundle:$PATH ANDROID_HOME="$HOME/dev/android/" ./gradlew installDebug
```

## Run the application on the device

```bash
$ adb shell am start -n org.freedesktop.gstreamer.play/.Play http://ftp.nluug.nl/pub/graphics/blender/demo/movies/Sintel.2010.720p.mkv
```

To see the GStreamer logs at runtime:

```bash
$ adb logcat | egrep '(gst)'
```
