# Building for non-linux platforms

Compiling the gstqmlgl plugin for non-linux platforms is not so trivial.
This file explains the steps that need to be followed for a successful build.

## Step 1

Build GStreamer for the target platform using cerbero.

## Step 2

Enter the cerbero shell:
```
./cerbero-uninstalled -c config/<target platform config>.cbc shell
```

## Step 3

Export the following environment variables:
```
export PATH=/path/to/Qt/<version>/<platform>/bin:$PATH
```

if you are cross-compiling (ex. for android), also export:
```
export PKG_CONFIG_SYSROOT_DIR=/
```

Additionally, if you are building for android:
```
export ANDROID_NDK_ROOT=$ANDROID_NDK
```

**Note**: the ANDROID_NDK variable is set by the cerbero shell; if you are not
using this shell, set it to the directory where you have installed the android
NDK. Additionally, if you are not building through the cerbero shell, it is also
important to have set PKG_CONFIG_LIBDIR to $GSTREAMER_ROOT/lib/pkgconfig.

## Step 4

cd to the directory of the gstqmlgl plugin and run:
```
qmake .
make
```

## Step 5

Copy the built plugin to your $GSTREAMER_ROOT/lib/gstreamer-1.0 or link to it
directly if it is compiled statically

# Building for Windows using pre-built gstreamer development package and Qt Creator 

## Step 1

Open `qtplugin.pro` in Qt Creator as project and configure it as usual. 

## Step 2

Open `qtplugin.pro` in the editor and make sure `GSTREAMER_PATH` 
variable in `qmlplugin.pro` is set to the path of your gstreamer SDK installation. This directory 
should contain subdirectories `bin`, `include`, `lib` etc. Pay attention to the correct choice 
of x86 or x86_64 platform. 

## Step 3

Build the project as usual.

## Step 3 

Copy the built plugin to your $GSTREAMER_ROOT/lib/gstreamer-1.0 or link to it
directly if it is compiled statically.

# Building for Windows using pre-built gstreamer development package and Qt on MinGW command line 

## Step 1 

Launch Qt developer command line from the Start menu.

## Step 2 

cd to the directory of the gstqmlgl plugin and make sure `GSTREAMER_PATH` 
variable in `qmlplugin.pro` is set to the path of your gstreamer SDK installation. This directory 
should contain subdirectories `bin`, `include`, `lib` etc. Pay attention to the correct choice 
of x86 or x86_64 platform. 

## Step 3

Run the following commands in the gstqmlgl plugin directory:

```
qmake 
mingw32-make
```

## Step 4

Copy the built plugin to your $GSTREAMER_ROOT/lib/gstreamer-1.0 or link to it
directly if it is compiled statically.