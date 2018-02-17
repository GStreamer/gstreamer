# GStreamer WebRTC demos

All demos use the same signalling server in the `signalling/` directory

## Build

The GStreamer WebRTC implementation has now been merged upstream, so all
you need is the latest GStreamer git master, as of 2 February 2018 or later:

 - http://cgit.freedesktop.org/gstreamer/gstreamer
 - http://cgit.freedesktop.org/gstreamer/gst-plugins-base
 - http://cgit.freedesktop.org/gstreamer/gst-plugins-good
 - http://cgit.freedesktop.org/gstreamer/gst-plugins-bad

You can build these with either Autotools gst-uninstalled:

https://arunraghavan.net/2014/07/quick-start-guide-to-gst-uninstalled-1-x/

Or with Meson gst-build:

https://cgit.freedesktop.org/gstreamer/gst-build/

You may need to install the following packages using your package manager:

json-glib, libsoup, libnice, libnice-gstreamer1 (the gstreamer plugin for libnice)

## Documentation

Currently, the best way to understand the API is to read the examples. This post breaking down the API should help with that:

http://blog.nirbheek.in/2018/02/gstreamer-webrtc.html

## Examples

### sendrecv: Send and receive audio and video

* Serve the `js/` directory on the root of your website, or open https://webrtc.nirbheek.in
  - The JS code assumes the signalling server is on port 8443 of the same server serving the HTML
* Build the sources in the `gst/` directory on your machine

```console
$ gcc webrtc-sendrecv.c $(pkg-config --cflags --libs gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0) -o webrtc-sendrecv
```

* Open the website in a browser and ensure that the status is "Registered with server, waiting for call", and note the `id` too.
* Run `webrtc-sendrecv --peer-id=ID` with the `id` from the browser. You will see state changes and an SDP exchange.
* You will see a bouncing ball + hear red noise in the browser, and your browser's webcam + mic in the gst app

TODO: Port to Python and Rust.

### multiparty-sendrecv: Multiparty audio conference with N peers

* Build the sources in the `gst/` directory on your machine

```console
$ gcc mp-webrtc-sendrecv.c $(pkg-config --cflags --libs gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0) -o mp-webrtc-sendrecv
```

* Run `mp-webrtc-sendrecv --room-id=ID` with `ID` as a room name. The peer will connect to the signalling server and setup a conference room.
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
