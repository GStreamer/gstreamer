## GStreamer WebRTC demos

All demos use the same signalling server in the `signalling/` directory

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
