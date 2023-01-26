# GStreamer WebRTC demos

All demos use the same signalling server in the `signalling/` directory

## Downloading GStreamer

The GStreamer WebRTC implementation has now been merged upstream, and is in the GStreamer 1.14 release. Binaries can be found here:

https://gstreamer.freedesktop.org/download/

## Building GStreamer from source

If you don't want to use the binaries provided by GStreamer or on your Linux distro, you can build GStreamer from source.

The easiest way to build the webrtc plugin and all the plugins it needs, is to [use Cerbero](https://gstreamer.freedesktop.org/documentation/installing/building-from-source-using-cerbero.html). These instructions should work out of the box for all platforms, including cross-compiling for iOS and Android.

## Building GStreamer manually from source

For hacking on the webrtc plugin, you may want to build manually using the git repositories:

 - http://gitlab.freedesktop.org/gstreamer/gstreamer
 - http://gitlab.freedesktop.org/libnice/libnice

You may need to install the following packages using your package manager:

json-glib, libsoup, libnice, libnice-gstreamer1 (the gstreamer plugin for libnice, called gstreamer1.0-nice Debian)

### Ubuntu 18.04

Here are the commands for Ubuntu 18.04.

```
sudo apt-get install -y gstreamer1.0-tools gstreamer1.0-nice gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-plugins-good libgstreamer1.0-dev git libglib2.0-dev libgstreamer-plugins-bad1.0-dev libsoup2.4-dev libjson-glib-dev
```

## Filing bugs

Please only file bugs about the demos here. Bugs about GStreamer's WebRTC implementation should be filed on the [GStreamer gitlab](https://gitlab.freedesktop.org/gstreamer/gst-plugins-bad/-/issues/new).

You can also find us on IRC by joining #gstreamer @ FreeNode.

## Documentation

Currently, the best way to understand the API is to read the examples. This post breaking down the API should help with that:

http://blog.nirbheek.in/2018/02/gstreamer-webrtc.html

## Examples

### Building

Most of the examples that require a build process can be built using the meson build system in the top-level gst-examples directory by using the following commands:

```console
cd /path/to/gst-examples
meson _builddir
ninja -C _builddir
```

Build outputs will be placed in the directory `_builddir`.

### sendrecv: Send and receive audio and video

* Serve the `js/` directory on the root of your website, or open https://webrtc.gstreamer.net
  - The JS code assumes the signalling server is on port 8443 of the same server serving the HTML

* Open the website in a browser and ensure that the status is "Registered with server, waiting for call", and note the `id` too.

#### Running the C version

* Run `webrtc-sendrecv --peer-id=ID` with the `id` from the browser. You will see state changes and an SDP exchange.

#### Running the Python version

* python3 -m pip install --user websockets
* run `python3 sendrecv/gst/webrtc_sendrecv.py ID` with the `id` from the browser. You will see state changes and an SDP exchange.

> The python version requires at least version 1.14.2 of gstreamer and its plugins.

#### Running the Rust version

* Install a recent Rust toolchain, e.g. via [rustup](https://rustup.rs/).
* Run `cargo build` for building the executable.
* Run `cargo run -- --peer-id=ID` with the `id` from the browser. You will see state changes and an SDP exchange.

With all versions, you will see a bouncing ball + hear red noise in the browser, and your browser's webcam + mic in the gst app.

You can pass a --server argument to all versions, for example `--server=wss://127.0.0.1:8443`.

#### Running the Java version

`cd sendrecv/gst-java`\
`./gradlew build`\
`java -jar build/libs/gst-java.jar --peer-id=ID` with the `id` from the browser.

You can optionally specify the server URL too (it defaults to wss://webrtc.gstreamer.net:8443):

`java -jar build/libs/gst-java.jar --peer-id=1 --server=ws://localhost:8443`

### multiparty-sendrecv: Multiparty audio conference with N peers

* Run `_builddir/multiparty-sendrecv/gst/mp-webrtc-sendrecv --room-id=ID` with `ID` as a room name. The peer will connect to the signalling server and setup a conference room.
* Run this as many times as you like, each will spawn a peer that sends red noise and outputs the red noise it receives from other peers.
  - To change what a peer sends, find the `audiotestsrc` element in the source and change the `wave` property.
  - You can, of course, also replace `audiotestsrc` itself with `autoaudiosrc` (any platform) or `pulsesink` (on linux).
* TODO: implement JS to do the same, derived from the JS for the `sendrecv` example.

### TODO: Selective Forwarding Unit (SFU) example

* Server routes media between peers
* Participant sends 1 stream, receives n-1 streams

### TODO: Multipoint Control Unit (MCU) example

* Server mixes media from all participants
* Participant sends 1 stream, receives 1 stream
