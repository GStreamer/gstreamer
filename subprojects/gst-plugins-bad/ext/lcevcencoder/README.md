LCEVC Encoder Build Instructions
--------------------------------

1. Download and manually install the pre-built V-Nova's LCEVC Encoder SDK:

- You can get a trial pre-built V-Nova's LCEVC Encoder SDK directly just by signing up at: https://download.v-nova.com
- There should be at least a pre-built `encoder_sdk.tar.gz` package for Ubuntu 20, Ubuntu 22 and Windows.
- Once you have downloaded the correct package for your system, extract the tar file:

```
$ cd Downloads
$ tar xvf encoder_sdk.tar.gz
```

- Now you need to manually install the headers, libraries and plugins in $INSTALL_DIR
- For example, $INSTALL_DIR for Linux can be `/usr/local`:

```
$ cp -v include/*.h /usr/local/include
$ cp -v *.so /usr/local/lib
```

- Afterwards, you need to manually create the `lcevc_eil.pc` package config file with this contents:

```
prefix=/usr/local
includedir=${prefix}/include
libdir=${prefix}/lib

Name: lcevc_eil
Description: LCEVC Encoder EIL library
Version: 3.11.3
Libs: -L${libdir} -llcevc_eil
Cflags: -I${includedir}
```

- And finally install it under `/usr/local/lib/pkgconfig`

```
$ cp -v lcevc_eil.pc /usr/local/lib/pkgconfig
```

2. Build LCEVC encoder (lcevcencoder) plugin for GStreamer:

- You can now build the lcevcencoder plugin by using the '-Dgst-plugins-bad:lcevcencoder=enabled' meson flag
- $BUILD_DIR and $INSTALL_DIR are local build and install directories
- For example:

```
$ cd GStreaner
$ meson setup $BUILD_DIR --pkg-config-path=$INSTALL_DIR/lib/pkgconfig -Dgst-plugins-bad:lcevcencoder=enabled
$ ninja -C $BUILD_DIR
```

3. Run GStreamer LCEVC encoder pipeline:

- If the build was successful, you can test LCEVC encoding (H264) with the following pipeline:

```
$ gst-launch-1.0 videotestsrc pattern=ball num-buffers=1024 ! video/x-raw,width=1920,height=1080,format=I420,framerate=25/1 ! lcevch264enc plugin-name="x264" plugin-props="preset=superfast;tune=zerolatency" ! h264parse ! mp4mux ! filesink location=lcevc-sample.mp4
```

