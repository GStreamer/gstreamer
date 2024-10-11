LCEVC Decoder Build Instructions
--------------------------------

1. Build and install the V-Nova's LCEVC Decoder SDK (LCEVCdec):

- Checkout the repository: https://github.com/v-novaltd/LCEVCdec

```
$ git clone https://github.com/v-novaltd/LCEVCdec.git
$ cd LCEVCdec
```

- Build and install the SDK
- $BUILD_DIR and $INSTALL_DIR are local build and install directories

```
$ mkdir $BUILD_DIR
$ cd $BUILD_DIR
$ cmake  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR ..
$ cmake --build .
$ cmake --install .
```

2. Build LCEVC decoder (lcevcdecoder) plugin for GStreamer:

- You can now build the lcevcdecoder plugin by using the '-Dgst-plugins-bad:lcevcdecoder=enabled' meson flag
- $BUILD_DIR and $INSTALL_DIR are local build and install directories
- For example:

```
$ cd GStreaner
$ meson setup $BUILD_DIR --pkg-config-path=$INSTALL_DIR/lib/pkgconfig -Dgst-plugins-bad:lcevcdecoder=enabled
$ ninja -C $BUILD_DIR
```

3. Run GStreamer LCEVC decoder pipeline:

- If the build was successful, you can test LCEVC decoding with the following pipeline:

```
$ gst-launch-1.0 filesrc location=/home/user/lcevc-sample.mp4 ! qtdemux ! h264parse ! openh264dec ! lcevcdec ! videoconvert ! autovideosink
```

- LCEVC decoding should also work with autoplugging elements:

```
$ gst-launch-1.0 playbin uri=file:///home/user/lcevc-sample.mp4
```
