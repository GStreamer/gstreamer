This directory contains all files necessary for building elements written in Swift.

First, Swift Package Manager is used to download our external dependencies, such as `swift-collections`. Then, it builds them into a static library, for use in our Meson-only part of the build. It also builds an executable containing our debug macros (like GST_DEBUG etc.), which will be used as a compiler plugin when Meson calls it. See the `swiftpm` directory.

Then, Meson builds the pure Swift code (currently contained in `sckitsrc`) as a static library, linking to the dependencies built by SwiftPM in the previous step. This needs a few ugly hacks, like manually specifying the include dirs for generated GStreamer headers. It also generates an Obj-C bridging header, which will be used to call into our Swift methods the Obj-C/C code. See `meson.build` for more comments/details.

Finally, the lib built above is added as a dependency for the main `applemedia` plugin, which also receives a few linker flags to make sure the Swift part will work correctly on all supported macOS versions.
