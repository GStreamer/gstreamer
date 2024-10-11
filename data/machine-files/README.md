# Cross compiling GStreamer with gst-build

GStreamer can be cross compiled for various platforms using gst-build. However,
only dependencies that are ported to the Meson build system will be built. It is
recommended to use Cerbero to cross compile GStreamer when other external
dependencies are required.

Once the toolchain is installed and a Meson cross file is created, to build
GStreamer simply run for example: `meson --cross-file data/cross-files/mingw_w64_x86-64.txt builddir`.

## Android

Requires Android API level >= 28, previous versions are missing *iconv* dependency.

- Download and extract the [NDK](https://developer.android.com/ndk/)
- Create a Meson cross file, you can use `android_arm64_api28.txt` as example
  and change CPU architectures and toolchain path using the prebuilt toolchains
  from the NDK.

Notes:
- On fedora the Android NDK requires the `ncurses-compat-libs` package.

## Windows

GStreamer can be cross compiled for Windows using mingw packaged in most
distribution.

The Meson cross file `mingw_w64_x86-64.txt` can be used when targeting amd64
architecture, or adapted for i686 arch.

### Fedora

- Install the toolchain packages: `mingw64-gcc`, `mingw64-gcc-c++`. Fedora
  provides many other optional dependencies that could be installed as well.
  For example: `mingw64-gettext`, `mingw64-libffi`, `mingw64-zlib`.

### Ubuntu

- Install the toolchain package: `gcc-mingw-w64`.
