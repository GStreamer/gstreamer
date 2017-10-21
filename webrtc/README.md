## GStreamer WebRTC demos

All demos use the same signalling server in the `signalling/` directory

You will need the following repositories till the GStreamer WebRTC implementation is merged upstream:

https://github.com/ystreet/gstreamer/tree/promise

https://github.com/ystreet/gst-plugins-bad/tree/webrtc

You can build these with either Autotools gst-uninstalled:

https://arunraghavan.net/2014/07/quick-start-guide-to-gst-uninstalled-1-x/

Or with Meson gst-build:

https://cgit.freedesktop.org/gstreamer/gst-build/

### sendrecv: Send and receive audio and video

* Serve the `js/` directory on the root of your website, or open https://webrtc.nirbheek.in
  - The JS code assumes the signalling server is on port 8443 of the same server serving the HTML
* Build and run the sources in the `gst/` directory on your machine

```console
$ gcc webrtc-sendrecv.c $(pkg-config --cflags --libs gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0) -o webrtc-sendrecv
```

* Open the website in a browser and ensure that the status is "Registered with server, waiting for call", and note the `id` too.
* Run `webrtc-sendrecv --peer-id=ID` with the `id` from the browser. You will see state changes and an SDP exchange.
* You will see a bouncing ball + hear red noise in the browser, and your browser's webcam + mic in the gst app
